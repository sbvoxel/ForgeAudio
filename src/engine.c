/*
 * ForgeAudio
 * Forked from FAudio.
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 * Portions copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma,
 * and the MonoGame Team.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "core_internal.h"
#include "batch_internal.h"
#include "simd_internal.h"

bool fa_audio_insert_submix_sorted(ForgeLinkedList **start, ForgeSubmixVoice *toAdd, ForgeAudioMutex lock,
                                   ForgeMallocFunc malloc_func) {
    ForgeLinkedList *newEntry, *latest;
    newEntry = (ForgeLinkedList *)malloc_func(sizeof(ForgeLinkedList));
    if (newEntry == NULL) {
        return false;
    }
    newEntry->entry = toAdd;
    newEntry->next = NULL;
    fa_platform_lock_mutex(lock);
    if (*start == NULL) {
        *start = newEntry;
    } else {
        latest = *start;

        /* Special case if the new stage is lower than everyone else */
        if (toAdd->mix.processingStage < ((ForgeSubmixVoice *)latest->entry)->mix.processingStage) {
            newEntry->next = latest;
            *start = newEntry;
        } else {
            /* If we got here, we know that the new stage is
             * _at least_ as high as the first submix in the list.
             *
             * Each loop iteration checks to see if the new stage
             * is smaller than `latest->next`, meaning it fits
             * between `latest` and `latest->next`.
             */
            while (latest->next != NULL) {
                if (toAdd->mix.processingStage < ((ForgeSubmixVoice *)latest->next->entry)->mix.processingStage) {
                    newEntry->next = latest->next;
                    latest->next = newEntry;
                    break;
                }
                latest = latest->next;
            }
            /* If newEntry didn't get a `next` value, that means
             * it didn't fall in between any stages and `latest`
             * is the last entry in the list. Add it to the end!
             */
            if (newEntry->next == NULL) {
                latest->next = newEntry;
            }
        }
    }
    fa_platform_unlock_mutex(lock);
    return true;
}

static uint32_t get_bytes_requested(ForgeSourceVoice *voice, uint32_t decoding) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t result = decoding * block_size;

    LOG_FUNC_ENTER(voice->audio)

    for (size_t i = 0; i < voice->src.queued_buffer_count; i += 1) {
        const struct queued_buffer *buffer = &voice->src.queued_buffers[i];
        uint32_t size = 0;

        if (buffer->buffer.loop_count > 0) {
            size += buffer->loop_bytes * buffer->buffer.loop_count;
        }
        size += buffer->play_bytes;
        size -= voice->src.curBufferOffset * block_size;

        if (size > result) {
            LOG_FUNC_EXIT(voice->audio)
            return 0;
        }
        result -= size;
    }

    LOG_FUNC_EXIT(voice->audio)
    return result;
}

static uint32_t buffer_get_end_with_loop_count(ForgeSourceVoice *voice, const struct queued_buffer *buffer,
                                               uint32_t loop_count) {
    const uint32_t block_size = voice->src.format->block_align;

    if (loop_count != 0) {
        return buffer->buffer.loop_begin + (buffer->loop_bytes / block_size);
    }

    return buffer->buffer.play_begin + (buffer->play_bytes / block_size);
}

static uint32_t buffer_get_end(ForgeSourceVoice *voice, const struct queued_buffer *buffer) {
    return buffer_get_end_with_loop_count(voice, buffer, buffer->buffer.loop_count);
}

static uint32_t decode_padding(ForgeSourceVoice *voice, float *dst, uint32_t padding_frames) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t decoded = 0;
    uint32_t offset;
    uint32_t loop_count;
    size_t buffer_index = 0;

    if (voice->src.queued_buffer_count == 0 || padding_frames == 0) {
        return 0;
    }

    offset = voice->src.curBufferOffset;
    loop_count = voice->src.queued_buffers[0].buffer.loop_count;

    while (decoded < padding_frames && buffer_index < voice->src.queued_buffer_count) {
        struct queued_buffer *buffer = &voice->src.queued_buffers[buffer_index];
        uint32_t end = buffer_get_end_with_loop_count(voice, buffer, loop_count);

        if (offset < end) {
            uint32_t decode_count = forge_min(padding_frames - decoded, end - offset);

            voice->src.decode(voice, buffer->buffer.audio_data + (offset * block_size),
                              dst + (decoded * voice->src.format->channels), decode_count);

            decoded += decode_count;
            offset += decode_count;
            if (decoded == padding_frames) {
                break;
            }
        }

        if (loop_count != 0) {
            offset = buffer->buffer.loop_begin;
            if (loop_count < FORGE_AUDIO_LOOP_INFINITE) {
                loop_count -= 1;
            }
        } else {
            buffer_index += 1;
            if (buffer_index < voice->src.queued_buffer_count) {
                const struct queued_buffer *next_buffer = &voice->src.queued_buffers[buffer_index];
                offset = next_buffer->buffer.play_begin;
                loop_count = next_buffer->buffer.loop_count;
            }
        }
    }

    return decoded;
}

/* Source callbacks must run without the mixer/source lock stack held. */
#define FORGE_AUDIO_UNLOCK_SOURCE_CALLBACK_LOCKS(voice)                                                                \
    do {                                                                                                               \
        fa_platform_unlock_mutex((voice)->src.bufferLock);                                                             \
        LOG_MUTEX_UNLOCK((voice)->audio, (voice)->src.bufferLock);                                                     \
        fa_platform_unlock_mutex((voice)->sendLock);                                                                   \
        LOG_MUTEX_UNLOCK((voice)->audio, (voice)->sendLock);                                                           \
        fa_platform_unlock_mutex((voice)->audio->sourceLock);                                                          \
        LOG_MUTEX_UNLOCK((voice)->audio, (voice)->audio->sourceLock);                                                  \
    } while (0)

#define FORGE_AUDIO_LOCK_SOURCE_CALLBACK_LOCKS(voice)                                                                  \
    do {                                                                                                               \
        fa_platform_lock_mutex((voice)->audio->sourceLock);                                                            \
        LOG_MUTEX_LOCK((voice)->audio, (voice)->audio->sourceLock);                                                    \
        fa_platform_lock_mutex((voice)->sendLock);                                                                     \
        LOG_MUTEX_LOCK((voice)->audio, (voice)->sendLock);                                                             \
        fa_platform_lock_mutex((voice)->src.bufferLock);                                                               \
        LOG_MUTEX_LOCK((voice)->audio, (voice)->src.bufferLock);                                                       \
    } while (0)

static void start_buffer(ForgeSourceVoice *voice, struct queued_buffer *buffer) {
    if (!buffer->sent_OnStartBuffer) {
        buffer->sent_OnStartBuffer = true;

        if (voice->src.callback != NULL && voice->src.callback->on_buffer_start != NULL) {
            FORGE_AUDIO_UNLOCK_SOURCE_CALLBACK_LOCKS(voice);
            voice->src.callback->on_buffer_start(voice->src.callback, buffer->buffer.context);
            FORGE_AUDIO_LOCK_SOURCE_CALLBACK_LOCKS(voice);
        }
    }
}

static void end_buffer(ForgeSourceVoice *voice) {
    struct queued_buffer *buffer = &voice->src.queued_buffers[0];
    bool eos = buffer->buffer.flags & FORGE_AUDIO_END_OF_STREAM;
    ForgeVoiceCallback *callback = voice->src.callback;
    void *context = buffer->buffer.context;

    if (buffer->buffer.loop_count > 0) {
        voice->src.curBufferOffset = buffer->buffer.loop_begin;
        if (buffer->buffer.loop_count < FORGE_AUDIO_LOOP_INFINITE) {
            buffer->buffer.loop_count -= 1;
        }

        if (callback != NULL && callback->on_loop_end != NULL) {
            FORGE_AUDIO_UNLOCK_SOURCE_CALLBACK_LOCKS(voice);
            callback->on_loop_end(callback, context);
            FORGE_AUDIO_LOCK_SOURCE_CALLBACK_LOCKS(voice);
        }
        return;
    }

    if (eos) {
        voice->src.curBufferOffsetDec = 0;
        voice->src.totalSamples = 0;
    }

    LOG_INFO(voice->audio, "Voice %p, finished with buffer %p", (void *)voice, (void *)buffer)

    forge_memmove(&voice->src.queued_buffers[0], &voice->src.queued_buffers[1],
                  (voice->src.queued_buffer_count - 1) * sizeof(*voice->src.queued_buffers));
    voice->src.queued_buffer_count -= 1;

    if (voice->src.queued_buffer_count != 0) {
        voice->src.curBufferOffset = voice->src.queued_buffers[0].buffer.play_begin;
    }

    if (callback != NULL) {
        FORGE_AUDIO_UNLOCK_SOURCE_CALLBACK_LOCKS(voice);
        if (callback->on_buffer_end != NULL) {
            callback->on_buffer_end(callback, context);
        }

        if (eos && callback->on_stream_end != NULL) {
            callback->on_stream_end(callback);
        }

        FORGE_AUDIO_LOCK_SOURCE_CALLBACK_LOCKS(voice);
    }
}

static void decode_buffers(ForgeSourceVoice *voice, uint64_t *toDecode) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t decoded = 0;

    LOG_FUNC_ENTER(voice->audio)

    /* This should never go past the max ratio size */
    forge_assert(*toDecode <= voice->src.decodeSamples);

    while (decoded < *toDecode && voice->src.queued_buffer_count != 0) {
        float *dst = voice->audio->decodeCache + (decoded * voice->src.format->channels);
        struct queued_buffer *buffer;
        uint32_t decode_count;

        buffer = &voice->src.queued_buffers[0];
        start_buffer(voice, buffer);

        decode_count =
            forge_min((uint32_t)*toDecode - decoded, buffer_get_end(voice, buffer) - voice->src.curBufferOffset);

        voice->src.decode(voice, buffer->buffer.audio_data + (voice->src.curBufferOffset * block_size), dst,
                          decode_count);

        LOG_INFO(voice->audio, "Voice %p, buffer %p, decoded %u samples from [%u,%u)", (void *)voice, (void *)buffer,
                 decode_count, voice->src.curBufferOffset, voice->src.curBufferOffset + decode_count)

        decoded += decode_count;
        voice->src.curBufferOffset += decode_count;
        voice->src.totalSamples += decode_count;

        if (decoded < *toDecode) {
            end_buffer(voice);
        }
    }

    if (decoded < *toDecode) {
        forge_zero(voice->audio->decodeCache + (decoded * voice->src.format->channels),
                   sizeof(float) * ((*toDecode - decoded) * voice->src.format->channels));
    }

    if (voice->src.queued_buffer_count != 0) {
        float *dst = voice->audio->decodeCache + (decoded * voice->src.format->channels);
        uint32_t decode_count;
#ifdef FORGE_AUDIO_TESTING
        size_t queued_buffer_count = voice->src.queued_buffer_count;
        uint32_t cur_buffer_offset = voice->src.curBufferOffset;
        struct queued_buffer first_buffer = voice->src.queued_buffers[0];
#endif

        decode_count = decode_padding(voice, dst, EXTRA_DECODE_PADDING);

#ifdef FORGE_AUDIO_TESTING
        forge_assert(queued_buffer_count == voice->src.queued_buffer_count);
        forge_assert(cur_buffer_offset == voice->src.curBufferOffset);
        if (voice->src.queued_buffer_count != 0) {
            forge_assert(forge_memcmp(&first_buffer, &voice->src.queued_buffers[0], sizeof(first_buffer)) == 0);
        }
#endif

        if (decode_count < EXTRA_DECODE_PADDING) {
            forge_zero(voice->audio->decodeCache + ((decoded + decode_count) * voice->src.format->channels),
                       sizeof(float) * ((EXTRA_DECODE_PADDING - decode_count) * voice->src.format->channels));
        }
    } else {
        forge_zero(voice->audio->decodeCache + (decoded * voice->src.format->channels),
                   sizeof(float) * (EXTRA_DECODE_PADDING * voice->src.format->channels));
    }

    *toDecode = decoded;
    LOG_FUNC_EXIT(voice->audio)
}

static float filter_coefficient_from_cutoff_hz(float cutoff_hz, uint32_t sample_rate) {
    const double pi = 3.14159265358979323846264338327950288;

    if (sample_rate == 0) {
        return 0.0f;
    }
    return 2.0f * forge_sinf((float)(pi * (double)cutoff_hz / (double)sample_rate));
}

static void refresh_filter_runtime_dsp(ForgeFilterRuntime *filter) {
    filter->frequency = filter_coefficient_from_cutoff_hz(filter->cutoff_hz, filter->sample_rate);
    filter->one_over_q = 1.0f / filter->q;
}

static uint8_t advance_filter_field_one_frame(ForgeFilterFieldAutomation *automation, float *value) {
    if (!automation->active) {
        return 0;
    }

    automation->remainingFrames -= 1;
    if (automation->remainingFrames == 0) {
        *value = automation->target;
        automation->active = 0;
    } else {
        *value += automation->step;
    }
    return 1;
}

static void advance_filter_one_frame_locked(ForgeFilterRuntime *filter) {
    uint8_t advanced = 0;

    advanced |= advance_filter_field_one_frame(&filter->automation.cutoff_hz, &filter->cutoff_hz);
    advanced |= advance_filter_field_one_frame(&filter->automation.q, &filter->q);
    advanced |= advance_filter_field_one_frame(&filter->automation.wet_dry_mix, &filter->wet_dry_mix);

    if (!advanced) {
        return;
    }
    refresh_filter_runtime_dsp(filter);
}

static uint8_t filter_automation_active(const ForgeFilterRuntime *filter) {
    return filter->automation.cutoff_hz.active || filter->automation.q.active || filter->automation.wet_dry_mix.active;
}

static void advance_filter_locked(ForgeFilterRuntime *filter, uint32_t frames) {
    while (frames > 0 && filter_automation_active(filter)) {
        advance_filter_one_frame_locked(filter);
        frames -= 1;
    }
}

static void advance_output_filters_locked(ForgeVoice *voice, uint32_t frames) {
    for (uint32_t sendIndex = 0; sendIndex < voice->sends.send_count; sendIndex += 1) {
        advance_filter_locked(&voice->sends.sends[sendIndex].filter, frames);
    }
}

static inline void filter_voice(ForgeAudioEngine *audio, ForgeFilterRuntime *filter,
                                         ForgeAudioFilterState *filterState, float *samples, uint32_t numSamples,
                                         uint16_t numChannels) {
    uint32_t j, ci;

    LOG_FUNC_ENTER(audio)

    /* Apply a digital state-variable filter to the voice.
     * The difference equations of the filter are:
     *
     * Yl(n) = F Yb(n - 1) + Yl(n - 1)
     * Yh(n) = x(n) - Yl(n) - one_over_q Yb(n - 1)
     * Yb(n) = F Yh(n) + Yb(n - 1)
     * Yn(n) = Yl(n) + Yh(n)
     *
     * ForgeFilterRuntime.frequency is the internal coefficient:
     * 2 * sin(pi * cutoff_hz / sampleRate).
     *
     * Public filter automation advances cutoff_hz, q, and wet_dry_mix once per
     * rendered frame. The current implementation linearly steps those public
     * values, then refreshes this coefficient. We are deliberately not making
     * the exact cutoff curve a long-term pre-1.0 promise; log/curve-selectable
     * cutoff motion can replace this without changing the public target API.
     *
     * - @JohanSmet
     */

    for (j = 0; j < numSamples; j += 1) {
        for (ci = 0; ci < numChannels; ci += 1) {
            filterState[ci][ForgeFilterLowPass] =
                filterState[ci][ForgeFilterLowPass] + (filter->frequency * filterState[ci][ForgeFilterBandPass]);
            filterState[ci][ForgeFilterHighPass] = samples[j * numChannels + ci] - filterState[ci][ForgeFilterLowPass] -
                                                   (filter->one_over_q * filterState[ci][ForgeFilterBandPass]);
            filterState[ci][ForgeFilterBandPass] =
                (filter->frequency * filterState[ci][ForgeFilterHighPass]) + filterState[ci][ForgeFilterBandPass];
            filterState[ci][ForgeFilterNotch] =
                filterState[ci][ForgeFilterHighPass] + filterState[ci][ForgeFilterLowPass];
            samples[j * numChannels + ci] = filterState[ci][filter->type] * filter->wet_dry_mix +
                                            samples[j * numChannels + ci] * (1.0 - filter->wet_dry_mix);
        }
        advance_filter_one_frame_locked(filter);
    }

    LOG_FUNC_EXIT(audio)
}

static float *get_effect_chain_cache(ForgeAudioEngine *audio, const void *source_buffer, uint32_t samples) {
    float **cache;
    uint32_t *cache_samples;

    LOG_FUNC_ENTER(audio)

    if (source_buffer != audio->effectChainCache) {
        cache = &audio->effectChainCache;
        cache_samples = &audio->effectChainSamples;
    } else {
        cache = &audio->effectChainCache2;
        cache_samples = &audio->effectChainSamples2;
    }

    if (samples > *cache_samples) {
        *cache_samples = samples;
        *cache = (float *)audio->realloc_func(*cache, sizeof(float) * *cache_samples);
    }

    LOG_FUNC_EXIT(audio)
    return *cache;
}

static inline float *process_effect_chain(ForgeVoice *voice, float *buffer, uint32_t *samples) {
    ForgeEffect *effect;
    ForgeEffectProcessBuffer srcParams, dstParams;

    LOG_FUNC_ENTER(voice->audio)

    /* Set up the buffer to be written into */
    srcParams.buffer = buffer;
    srcParams.buffer_flags = FORGE_EFFECT_BUFFER_SILENT;
    srcParams.valid_frame_count = *samples;
    for (uint32_t i = 0; i < srcParams.valid_frame_count; i += 1) {
        if (buffer[i] != 0.0f) /* Arbitrary! */
        {
            srcParams.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
            break;
        }
    }

    /* initialize output parameters to something sane */
    dstParams.buffer = srcParams.buffer;
    dstParams.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
    dstParams.valid_frame_count = srcParams.valid_frame_count;

    /* Update parameters, process! */
    for (uint32_t i = 0; i < voice->effects.count; i += 1) {
        effect = voice->effects.desc[i].effect;

        if (!voice->effects.inPlaceProcessing[i]) {
            dstParams.buffer = get_effect_chain_cache(
                voice->audio, srcParams.buffer, voice->effects.desc[i].output_channels * srcParams.valid_frame_count);

            forge_zero(dstParams.buffer,
                       voice->effects.desc[i].output_channels * srcParams.valid_frame_count * sizeof(float));
        }

        if (voice->effects.parameterUpdates[i]) {
            effect->set_parameters(effect, voice->effects.parameters[i], voice->effects.parameterSizes[i]);
            voice->effects.parameterUpdates[i] = 0;
        }

        effect->process(effect, 1, &srcParams, 1, &dstParams, voice->effects.desc[i].initial_state);

        forge_memcpy(&srcParams, &dstParams, sizeof(dstParams));
    }

    *samples = dstParams.valid_frame_count;

    /* Save the output buffer-flags so the mixer-function can determine when it's save to stop processing the effect
     * chain */
    voice->effects.state = dstParams.buffer_flags;

    LOG_FUNC_EXIT(voice->audio)
    return (float *)dstParams.buffer;
}

static void apply_voice_volume_locked(ForgeVoice *voice, float *samples, uint32_t frames, uint32_t channels) {
    uint32_t frame = 0;

    if (voice->volumeAutomation.active) {
        while (frame < frames && voice->volumeAutomation.active) {
            float volume = voice->volume;

            if (volume != 1.0f) {
                for (uint32_t channel = 0; channel < channels; channel += 1) {
                    samples[frame * channels + channel] *= volume;
                }
            }

            voice->volumeAutomation.remainingFrames -= 1;
            if (voice->volumeAutomation.remainingFrames == 0) {
                voice->volume = voice->volumeAutomation.target;
                voice->volumeAutomation.active = 0;
                if (voice->volumeAutomation.stopSourceOnComplete && voice->type == FORGE_AUDIO_VOICE_SOURCE) {
                    voice->volumeAutomation.stopSourceOnComplete = 0;
                    voice->src.active = 0;
                    frame += 1;
                    if (frame < frames) {
                        forge_zero(samples + (frame * channels), (frames - frame) * channels * sizeof(float));
                    }
                    return;
                }
            } else {
                voice->volume += voice->volumeAutomation.step;
            }
            frame += 1;
        }
    }

    if (frame < frames && voice->volume != 1.0f) {
        fa_mix_amplify(samples + (frame * channels), (frames - frame) * channels, voice->volume);
    }
}

static void advance_voice_volume_locked(ForgeVoice *voice, uint32_t frames) {
    while (frames > 0 && voice->volumeAutomation.active) {
        voice->volumeAutomation.remainingFrames -= 1;
        if (voice->volumeAutomation.remainingFrames == 0) {
            voice->volume = voice->volumeAutomation.target;
            voice->volumeAutomation.active = 0;
            if (voice->volumeAutomation.stopSourceOnComplete && voice->type == FORGE_AUDIO_VOICE_SOURCE) {
                voice->volumeAutomation.stopSourceOnComplete = 0;
                voice->src.active = 0;
            }
        } else {
            voice->volume += voice->volumeAutomation.step;
        }
        frames -= 1;
    }
}

static uint8_t channel_volumes_are_unity(const ForgeVoice *voice, uint32_t channels) {
    for (uint32_t channel = 0; channel < channels; channel += 1) {
        if (voice->channelVolume[channel] != 1.0f) {
            return 0;
        }
    }
    return 1;
}

static void apply_channel_volumes_locked(ForgeVoice *voice, float *samples, uint32_t frames, uint32_t channels) {
    uint32_t frame = 0;

    if (voice->channelVolumeAutomation.active) {
        while (frame < frames && voice->channelVolumeAutomation.active) {
            for (uint32_t channel = 0; channel < channels; channel += 1) {
                samples[frame * channels + channel] *= voice->channelVolume[channel];
            }

            voice->channelVolumeAutomation.remainingFrames -= 1;
            if (voice->channelVolumeAutomation.remainingFrames == 0) {
                for (uint32_t channel = 0; channel < channels; channel += 1) {
                    voice->channelVolume[channel] = voice->channelVolumeAutomation.target[channel];
                }
                voice->channelVolumeAutomation.active = 0;
            } else {
                for (uint32_t channel = 0; channel < channels; channel += 1) {
                    voice->channelVolume[channel] += voice->channelVolumeAutomation.step[channel];
                }
            }
            frame += 1;
        }
    }

    if (frame < frames && !channel_volumes_are_unity(voice, channels)) {
        for (; frame < frames; frame += 1) {
            for (uint32_t channel = 0; channel < channels; channel += 1) {
                samples[frame * channels + channel] *= voice->channelVolume[channel];
            }
        }
    }
}

static void advance_channel_volumes_locked(ForgeVoice *voice, uint32_t frames, uint32_t channels) {
    while (frames > 0 && voice->channelVolumeAutomation.active) {
        voice->channelVolumeAutomation.remainingFrames -= 1;
        if (voice->channelVolumeAutomation.remainingFrames == 0) {
            for (uint32_t channel = 0; channel < channels; channel += 1) {
                voice->channelVolume[channel] = voice->channelVolumeAutomation.target[channel];
            }
            voice->channelVolumeAutomation.active = 0;
        } else {
            for (uint32_t channel = 0; channel < channels; channel += 1) {
                voice->channelVolume[channel] += voice->channelVolumeAutomation.step[channel];
            }
        }
        frames -= 1;
    }
}

static void mix_one_matrix_frame(float *src, float *dst, float *coefficients, uint32_t srcChans, uint32_t dstChans) {
    for (uint32_t dstChan = 0; dstChan < dstChans; dstChan += 1) {
        for (uint32_t srcChan = 0; srcChan < srcChans; srcChan += 1) {
            dst[dstChan] += src[srcChan] * coefficients[dstChan * srcChans + srcChan];
        }
    }
}

static void commit_output_matrix_target_locked(ForgeVoice *voice, uint32_t sendIndex, uint32_t coefficientCount) {
    ForgeVoiceSendRuntime *send = &voice->sends.sends[sendIndex];

    forge_memcpy(send->sendCoefficients, send->matrixAutomation.target, sizeof(float) * coefficientCount);
    send->matrixAutomation.active = 0;
    fa_voice_recalc_mix_matrix(voice, sendIndex);
}

static void apply_output_matrix_locked(ForgeVoice *voice, uint32_t sendIndex, uint32_t frames, uint32_t srcChans,
                                       uint32_t dstChans, float *src, float *dst) {
    ForgeVoiceSendRuntime *send = &voice->sends.sends[sendIndex];
    uint32_t coefficientCount = srcChans * dstChans;
    uint32_t frame = 0;

    if (send->matrixAutomation.active) {
        while (frame < frames && send->matrixAutomation.active) {
            mix_one_matrix_frame(src + (frame * srcChans), dst + (frame * dstChans), send->sendCoefficients, srcChans,
                                 dstChans);

            send->matrixAutomation.remainingFrames -= 1;
            if (send->matrixAutomation.remainingFrames == 0) {
                commit_output_matrix_target_locked(voice, sendIndex, coefficientCount);
            } else {
                for (uint32_t coefficient = 0; coefficient < coefficientCount; coefficient += 1) {
                    send->sendCoefficients[coefficient] += send->matrixAutomation.step[coefficient];
                }
            }
            frame += 1;
        }
    }

    if (frame < frames) {
        send->mix(frames - frame, srcChans, dstChans, src + (frame * srcChans), dst + (frame * dstChans),
                  send->mixCoefficients);
    }
}

static void advance_output_matrices_locked(ForgeVoice *voice, uint32_t frames) {
    for (uint32_t sendIndex = 0; sendIndex < voice->sends.send_count; sendIndex += 1) {
        ForgeVoiceSendRuntime *send = &voice->sends.sends[sendIndex];
        ForgeVoice *out = send->send.output_voice;
        uint32_t dstChans = (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputChannels : out->mix.inputChannels;
        uint32_t coefficientCount = voice->outputChannels * dstChans;
        uint32_t framesToAdvance = frames;

        while (framesToAdvance > 0 && send->matrixAutomation.active) {
            send->matrixAutomation.remainingFrames -= 1;
            if (send->matrixAutomation.remainingFrames == 0) {
                commit_output_matrix_target_locked(voice, sendIndex, coefficientCount);
            } else {
                for (uint32_t coefficient = 0; coefficient < coefficientCount; coefficient += 1) {
                    send->sendCoefficients[coefficient] += send->matrixAutomation.step[coefficient];
                }
            }
            framesToAdvance -= 1;
        }
    }
}

#ifdef FORGE_AUDIO_TESTING
float *forge_audio_test_process_effect_chain(ForgeVoice *voice, float *buffer, uint32_t *samples) {
    return process_effect_chain(voice, buffer, samples);
}
#endif

static void resize_resample_cache(ForgeAudioEngine *audio, uint32_t samples) {
    LOG_FUNC_ENTER(audio)
    if (samples > audio->resampleSamples) {
        audio->resampleSamples = samples;
        audio->resampleCache =
            (float *)audio->realloc_func(audio->resampleCache, sizeof(float) * audio->resampleSamples);
    }
    LOG_FUNC_EXIT(audio)
}

static void mix_source(ForgeSourceVoice *voice) {
    /* Decode/Resample variables */
    uint64_t toDecode;
    uint64_t toResample;
    /* Output mix variables */
    float *stream;
    uint32_t mixed;
    uint32_t oChan;
    ForgeVoice *out;
    uint32_t outputRate;
    double stepd;
    float *finalSamples;

    LOG_FUNC_ENTER(voice->audio)

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Calculate the resample stepping value */
    if (voice->src.resampleFreq != voice->src.freqRatio * voice->src.format->sample_rate) {
        out = (voice->sends.send_count == 0) ? voice->audio->master : /* Barf */
                  voice->sends.sends->send.output_voice;
        outputRate = (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputSampleRate : out->mix.inputSampleRate;
        stepd = (voice->src.freqRatio * (double)voice->src.format->sample_rate / (double)outputRate);
        voice->src.resampleStep = DOUBLE_TO_FIXED(stepd);
        voice->src.resampleFreq = voice->src.freqRatio * voice->src.format->sample_rate;
    }

    if (voice->src.active == 2) {
        /* We're just playing tails, skip all buffer stuff */
        resize_resample_cache(voice->audio, voice->src.resampleSamples * voice->src.format->channels);
        mixed = voice->src.resampleSamples;
        forge_zero(voice->audio->resampleCache, mixed * voice->src.format->channels * sizeof(float));
        finalSamples = voice->audio->resampleCache;
        goto sendwork;
    }

    /* Base decode size, int to fixed... */
    toDecode = voice->src.resampleSamples * voice->src.resampleStep;
    /* ... rounded up based on current offset... */
    toDecode += voice->src.curBufferOffsetDec + FIXED_FRACTION_MASK;
    /* ... fixed to int, truncating extra fraction from rounding. */
    toDecode >>= FIXED_PRECISION;

    /* First voice callback */
    if (voice->src.callback != NULL && voice->src.callback->on_voice_processing_pass_start != NULL) {
        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        fa_platform_unlock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        voice->src.callback->on_voice_processing_pass_start(voice->src.callback,
                                                            get_bytes_requested(voice, (uint32_t)toDecode));

        fa_platform_lock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        fa_platform_lock_mutex(voice->sendLock);
        LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    }

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    /* Nothing to do? */
    if (voice->src.queued_buffer_count == 0) {
        fa_platform_unlock_mutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

        if (voice->effects.count > 0 && voice->effects.state != FORGE_EFFECT_BUFFER_SILENT) {
            /* do not stop while the effect chain generates a non-silent buffer */
            resize_resample_cache(voice->audio, voice->src.resampleSamples * voice->src.format->channels);
            mixed = voice->src.resampleSamples;
            forge_zero(voice->audio->resampleCache, mixed * voice->src.format->channels * sizeof(float));
            finalSamples = voice->audio->resampleCache;
            goto sendwork;
        }

        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        fa_platform_unlock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        if (voice->src.callback != NULL && voice->src.callback->on_voice_processing_pass_end != NULL) {
            voice->src.callback->on_voice_processing_pass_end(voice->src.callback);
        }

        fa_platform_lock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    /* Decode... */
    decode_buffers(voice, &toDecode);

    /* Subtract any padding samples from the total, if applicable */
    if (voice->src.curBufferOffsetDec > 0 && voice->src.totalSamples > 0) {
        voice->src.totalSamples -= 1;
    }

    /* Okay, we're done messing with client data */
    if (voice->src.callback != NULL && voice->src.callback->on_voice_processing_pass_end != NULL) {
        FORGE_AUDIO_UNLOCK_SOURCE_CALLBACK_LOCKS(voice);
        voice->src.callback->on_voice_processing_pass_end(voice->src.callback);
        FORGE_AUDIO_LOCK_SOURCE_CALLBACK_LOCKS(voice);
    }

    /* Nothing to resample? */
    if (toDecode == 0) {
        fa_platform_unlock_mutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    /* int to fixed... */
    toResample = toDecode << FIXED_PRECISION;
    /* ... round back down based on current offset... */
    toResample -= voice->src.curBufferOffsetDec;
    /* ... but also ceil for any fraction value... */
    toResample += FIXED_FRACTION_MASK;
    /* ... undo step size, fixed to int. */
    toResample /= voice->src.resampleStep;
    /* EXTRA_DECODE_PADDING keeps one decoded frame available for interpolation.
     * Covered by source_resampler padding and split-vs-contiguous tests.
     */
    toResample += EXTRA_DECODE_PADDING;
    /* Clamp to the per-pass resample cache capacity. Source resampler tests verify
     * decode sizing leaves interpolation padding available.
     */
    toResample = forge_min(toResample, voice->src.resampleSamples);

    /* Resample... */
    if (voice->src.resampleStep == FIXED_ONE) {
        /* Actually, just use the existing buffer... */
        finalSamples = voice->audio->decodeCache;
    } else {
        resize_resample_cache(voice->audio, voice->src.resampleSamples * voice->src.format->channels);
        voice->src.resample(voice->audio->decodeCache, voice->audio->resampleCache, &voice->src.resampleOffset,
                            voice->src.resampleStep, toResample, (uint8_t)voice->src.format->channels);
        finalSamples = voice->audio->resampleCache;
    }

    /* Update buffer offsets */
    if (voice->src.queued_buffer_count != 0) {
        /* Increment fixed offset by resample size, int to fixed... */
        voice->src.curBufferOffsetDec += toResample * voice->src.resampleStep;
        /* ... chop off any ints we got from the above increment */
        voice->src.curBufferOffsetDec &= FIXED_FRACTION_MASK;

        /* Preserve the previous source frame for interpolation across buffer
         * boundaries. Split-vs-contiguous resampler tests cover this at
         * fractional source-rate ratios.
         */
        if (voice->src.curBufferOffsetDec > 0 && voice->src.curBufferOffset > 0) {
            voice->src.curBufferOffset -= 1;
        }
    } else {
        voice->src.curBufferOffsetDec = 0;
        voice->src.curBufferOffset = 0;
    }

    /* Done with buffers, finally. */
    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    mixed = (uint32_t)toResample;

sendwork:

    /* Filters */
    if (voice->flags & FORGE_AUDIO_VOICE_USEFILTER) {
        fa_platform_lock_mutex(voice->filterLock);
        LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
        filter_voice(voice->audio, &voice->filter, voice->filterState, finalSamples, mixed,
                              voice->src.format->channels);
        fa_platform_unlock_mutex(voice->filterLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    }

    /* process effect chain */
    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.count > 0) {
        /* If we didn't get the full size of the update, we have to fill
         * it with silence so the effect can process a whole update
         */
        if (mixed < voice->src.resampleSamples) {
            forge_zero(finalSamples + (mixed * voice->src.format->channels),
                       (voice->src.resampleSamples - mixed) * voice->src.format->channels * sizeof(float));
            mixed = voice->src.resampleSamples;
        }
        finalSamples = process_effect_chain(voice, finalSamples, &mixed);
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    /* No sends means no audible output, but source automation still follows the rendered audio timeline. */
    if (voice->sends.send_count == 0) {
        fa_platform_lock_mutex(voice->volumeLock);
        LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
        advance_voice_volume_locked(voice, mixed);
        advance_channel_volumes_locked(voice, mixed, voice->outputChannels);
        advance_output_matrices_locked(voice, mixed);
        fa_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    /* Apply source volume, then send float cache to sends. */
    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    apply_voice_volume_locked(voice, finalSamples, mixed, voice->outputChannels);
    apply_channel_volumes_locked(voice, finalSamples, mixed, voice->outputChannels);
    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        ForgeVoiceSendRuntime *send = &voice->sends.sends[i];

        out = send->send.output_voice;
        if (out->type == FORGE_AUDIO_VOICE_MASTER) {
            stream = out->master.output;
            oChan = out->master.inputChannels;
        } else {
            stream = out->mix.inputCache;
            oChan = out->mix.inputChannels;
        }

        apply_output_matrix_locked(voice, i, mixed, voice->outputChannels, oChan, finalSamples, stream);

        if (send->send.flags & FORGE_AUDIO_SEND_USEFILTER) {
            filter_voice(voice->audio, &send->filter, send->filterState, stream, mixed, oChan);
        }
    }
    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_FUNC_EXIT(voice->audio)
}

#ifdef FORGE_AUDIO_TESTING
ForgeAudioTestSourceResampleResult forge_audio_test_decode_resample_source(ForgeSourceVoice *voice, float *output) {
    ForgeAudioTestSourceResampleResult result = {0};
    float *finalSamples;
    uint64_t toDecode;
    uint64_t toResample;

    LOG_FUNC_ENTER(voice->audio)

    if (voice->src.decode == NULL) {
        voice->src.decode = fa_decode_pcm32f;
    }
    if (voice->src.resample == NULL) {
        voice->src.resample = fa_resample_generic;
    }

    toDecode = voice->src.resampleSamples * voice->src.resampleStep;
    toDecode += voice->src.curBufferOffsetDec + FIXED_FRACTION_MASK;
    toDecode >>= FIXED_PRECISION;
    result.requested_decode_frames = (uint32_t)toDecode;

    decode_buffers(voice, &toDecode);
    result.decoded_frames = (uint32_t)toDecode;

    if (voice->src.curBufferOffsetDec > 0 && voice->src.totalSamples > 0) {
        voice->src.totalSamples -= 1;
    }

    if (toDecode == 0) {
        result.cur_buffer_offset = voice->src.curBufferOffset;
        result.cur_buffer_offset_dec = voice->src.curBufferOffsetDec;
        result.queued_buffer_count = voice->src.queued_buffer_count;
        LOG_FUNC_EXIT(voice->audio)
        return result;
    }

    toResample = toDecode << FIXED_PRECISION;
    toResample -= voice->src.curBufferOffsetDec;
    toResample += FIXED_FRACTION_MASK;
    toResample /= voice->src.resampleStep;
    toResample += EXTRA_DECODE_PADDING;
    result.unclamped_resample_frames = toResample;
    toResample = forge_min(toResample, voice->src.resampleSamples);

    if (voice->src.resampleStep == FIXED_ONE) {
        finalSamples = voice->audio->decodeCache;
    } else {
        resize_resample_cache(voice->audio, voice->src.resampleSamples * voice->src.format->channels);
        voice->src.resample(voice->audio->decodeCache, voice->audio->resampleCache, &voice->src.resampleOffset,
                            voice->src.resampleStep, toResample, (uint8_t)voice->src.format->channels);
        finalSamples = voice->audio->resampleCache;
    }

    if (voice->src.queued_buffer_count != 0) {
        voice->src.curBufferOffsetDec += toResample * voice->src.resampleStep;
        voice->src.curBufferOffsetDec &= FIXED_FRACTION_MASK;
        if (voice->src.curBufferOffsetDec > 0 && voice->src.curBufferOffset > 0) {
            voice->src.curBufferOffset -= 1;
        }
    } else {
        voice->src.curBufferOffsetDec = 0;
        voice->src.curBufferOffset = 0;
    }

    result.resampled_frames = (uint32_t)toResample;
    result.cur_buffer_offset = voice->src.curBufferOffset;
    result.cur_buffer_offset_dec = voice->src.curBufferOffsetDec;
    result.queued_buffer_count = voice->src.queued_buffer_count;

    if (output != NULL && result.resampled_frames > 0) {
        forge_memcpy(output, finalSamples, result.resampled_frames * voice->src.format->channels * sizeof(float));
    }

    LOG_FUNC_EXIT(voice->audio)
    return result;
}
#endif

static void mix_submix(ForgeSubmixVoice *voice) {
    float *stream;
    uint32_t oChan;
    ForgeVoice *out;
    uint32_t resampled;
    uint64_t resampleOffset = 0;
    float *finalSamples;

    LOG_FUNC_ENTER(voice->audio)
    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Resample */
    if (voice->mix.resampleStep == FIXED_ONE) {
        /* Actually, just use the existing buffer... */
        finalSamples = voice->mix.inputCache;
    } else {
        resize_resample_cache(voice->audio, voice->mix.outputSamples * voice->mix.inputChannels);
        voice->mix.resample(voice->mix.inputCache, voice->audio->resampleCache, &resampleOffset,
                            voice->mix.resampleStep, voice->mix.outputSamples, (uint8_t)voice->mix.inputChannels);
        finalSamples = voice->audio->resampleCache;
    }
    resampled = voice->mix.outputSamples * voice->mix.inputChannels;

    resampled /= voice->mix.inputChannels;

    /* Submix volume is applied before effects/filters. */
    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    apply_voice_volume_locked(voice, finalSamples, resampled, voice->mix.inputChannels);
    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    /* Filters */
    if (voice->flags & FORGE_AUDIO_VOICE_USEFILTER) {
        fa_platform_lock_mutex(voice->filterLock);
        LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
        filter_voice(voice->audio, &voice->filter, voice->filterState, finalSamples, resampled,
                              voice->mix.inputChannels);
        fa_platform_unlock_mutex(voice->filterLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    }

    /* process effect chain */
    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.count > 0) {
        finalSamples = process_effect_chain(voice, finalSamples, &resampled);
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    /* Nothing more to do? */
    if (voice->sends.send_count == 0) {
        fa_platform_lock_mutex(voice->volumeLock);
        LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
        advance_channel_volumes_locked(voice, resampled, voice->outputChannels);
        fa_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        goto end;
    }

    /* Apply channel volumes, then send float cache to sends. */
    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    apply_channel_volumes_locked(voice, finalSamples, resampled, voice->outputChannels);
    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        ForgeVoiceSendRuntime *send = &voice->sends.sends[i];

        out = send->send.output_voice;
        if (out->type == FORGE_AUDIO_VOICE_MASTER) {
            stream = out->master.output;
            oChan = out->master.inputChannels;
        } else {
            stream = out->mix.inputCache;
            oChan = out->mix.inputChannels;
        }

        apply_output_matrix_locked(voice, i, resampled, voice->outputChannels, oChan, finalSamples, stream);

        if (send->send.flags & FORGE_AUDIO_SEND_USEFILTER) {
            filter_voice(voice->audio, &send->filter, send->filterState, stream, resampled, oChan);
        }
    }
    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    /* Zero this at the end, for the next update */
end:
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    forge_zero(voice->mix.inputCache, sizeof(float) * voice->mix.inputSamples);
    LOG_FUNC_EXIT(voice->audio)
}

static void flush_pending_buffers(ForgeSourceVoice *voice) {
    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.callback == NULL || voice->src.callback->on_buffer_end == NULL) {
        voice->src.flush_buffer_count = 0;
    }

    /* Remove pending flushed buffers and send an event for each one */
    else
        while (voice->src.flush_buffer_count > 0) {
            void *context = voice->src.flush_buffers[0].buffer.context;

            voice->src.flush_buffer_count -= 1;
            forge_memmove(&voice->src.flush_buffers[0], &voice->src.flush_buffers[1],
                          voice->src.flush_buffer_count * sizeof(*voice->src.flush_buffers));

            fa_platform_unlock_mutex(voice->src.bufferLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock);

            fa_platform_unlock_mutex(voice->audio->sourceLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

            voice->src.callback->on_buffer_end(voice->src.callback, context);

            fa_platform_lock_mutex(voice->audio->sourceLock);
            LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

            fa_platform_lock_mutex(voice->src.bufferLock);
            LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock);
        }

    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
}

static void FORGE_AUDIO_CALL fa_audio_generate_output(ForgeAudioEngine *audio, float *output) {
    uint32_t totalSamples;
    ForgeLinkedList *list;
    float *effectOut;
    ForgeEngineCallback *callback;

    LOG_FUNC_ENTER(audio)
    if (!audio->active) {
        LOG_FUNC_EXIT(audio)
        return;
    }

    /* Apply pass-boundary commands that are ready */
    fa_batch_execute(audio);

    /* ProcessingPassStart callbacks */
    fa_platform_lock_mutex(audio->callbackLock);
    LOG_MUTEX_LOCK(audio, audio->callbackLock)
    list = audio->callbacks;
    while (list != NULL) {
        callback = (ForgeEngineCallback *)list->entry;
        if (callback->on_processing_pass_start != NULL) {
            callback->on_processing_pass_start(callback);
        }
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->callbackLock);
    LOG_MUTEX_UNLOCK(audio, audio->callbackLock)

    /* Writes to master will directly write to output, but ONLY if there
     * isn't any channel-changing effect processing to do first.
     */
    if (audio->master->master.effectCache != NULL) {
        audio->master->master.output = audio->master->master.effectCache;
        forge_zero(audio->master->master.effectCache,
                   (sizeof(float) * audio->updateSize * audio->master->master.inputChannels));
    } else {
        audio->master->master.output = output;
    }

    /* Mix sources */
    fa_platform_lock_mutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    list = audio->sources;
    while (list != NULL) {
        audio->processingSource = (ForgeSourceVoice *)list->entry;

        flush_pending_buffers(audio->processingSource);
        if (audio->processingSource->src.active) {
            mix_source(audio->processingSource);
            flush_pending_buffers(audio->processingSource);
        } else {
            uint32_t sourceTimelineFrames = audio->processingSource->src.resampleSamples;

            if (audio->processingSource->flags & FORGE_AUDIO_VOICE_USEFILTER) {
                fa_platform_lock_mutex(audio->processingSource->filterLock);
                LOG_MUTEX_LOCK(audio, audio->processingSource->filterLock)
                advance_filter_locked(&audio->processingSource->filter, sourceTimelineFrames);
                fa_platform_unlock_mutex(audio->processingSource->filterLock);
                LOG_MUTEX_UNLOCK(audio, audio->processingSource->filterLock)
            }
            fa_platform_lock_mutex(audio->processingSource->volumeLock);
            LOG_MUTEX_LOCK(audio, audio->processingSource->volumeLock)
            advance_voice_volume_locked(audio->processingSource, sourceTimelineFrames);
            advance_channel_volumes_locked(audio->processingSource, sourceTimelineFrames,
                                           audio->processingSource->outputChannels);
            advance_output_matrices_locked(audio->processingSource, sourceTimelineFrames);
            fa_platform_unlock_mutex(audio->processingSource->volumeLock);
            LOG_MUTEX_UNLOCK(audio, audio->processingSource->volumeLock)
            fa_platform_lock_mutex(audio->processingSource->sendLock);
            LOG_MUTEX_LOCK(audio, audio->processingSource->sendLock)
            advance_output_filters_locked(audio->processingSource, sourceTimelineFrames);
            fa_platform_unlock_mutex(audio->processingSource->sendLock);
            LOG_MUTEX_UNLOCK(audio, audio->processingSource->sendLock)
        }

        list = list->next;
    }
    audio->processingSource = NULL;
    fa_platform_unlock_mutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)

    /* Mix submixes, ordered by processing stage */
    fa_platform_lock_mutex(audio->submixLock);
    LOG_MUTEX_LOCK(audio, audio->submixLock)
    list = audio->submixes;
    while (list != NULL) {
        mix_submix((ForgeSubmixVoice *)list->entry);
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->submixLock);
    LOG_MUTEX_UNLOCK(audio, audio->submixLock)

    /* Apply master volume before the master effect chain. */
    fa_platform_lock_mutex(audio->master->volumeLock);
    LOG_MUTEX_LOCK(audio, audio->master->volumeLock)
    apply_voice_volume_locked(audio->master, audio->master->master.output, audio->updateSize,
                              audio->master->master.inputChannels);
    fa_platform_unlock_mutex(audio->master->volumeLock);
    LOG_MUTEX_UNLOCK(audio, audio->master->volumeLock)

    /* process master effect chain */
    fa_platform_lock_mutex(audio->master->effectLock);
    LOG_MUTEX_LOCK(audio, audio->master->effectLock)
    if (audio->master->effects.count > 0) {
        totalSamples = audio->updateSize;
        effectOut = process_effect_chain(audio->master, audio->master->master.output, &totalSamples);

        if (effectOut != output) {
            forge_memcpy(output, effectOut, totalSamples * audio->master->outputChannels * sizeof(float));
        }
        if (totalSamples < audio->updateSize) {
            forge_zero(output + (totalSamples * audio->master->outputChannels),
                       (audio->updateSize - totalSamples) * sizeof(float));
        }
    }
    fa_platform_unlock_mutex(audio->master->effectLock);
    LOG_MUTEX_UNLOCK(audio, audio->master->effectLock)

    /* on_processing_pass_end callbacks */
    fa_platform_lock_mutex(audio->callbackLock);
    LOG_MUTEX_LOCK(audio, audio->callbackLock)
    list = audio->callbacks;
    while (list != NULL) {
        callback = (ForgeEngineCallback *)list->entry;
        if (callback->on_processing_pass_end != NULL) {
            callback->on_processing_pass_end(callback);
        }
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->callbackLock);
    LOG_MUTEX_UNLOCK(audio, audio->callbackLock)

    LOG_FUNC_EXIT(audio)
}

void fa_audio_update_engine(ForgeAudioEngine *audio, float *output) {
    LOG_FUNC_ENTER(audio)
    if (audio->client_engine_proc) {
        audio->client_engine_proc(&fa_audio_generate_output, audio, output, audio->clientEngineUser);
    } else {
        fa_audio_generate_output(audio, output);
    }
    LOG_FUNC_EXIT(audio)
}

#ifdef FORGE_AUDIO_TESTING
ForgeResult forge_audio_test_render(ForgeAudioEngine *audio, float *output, uint32_t frame_count) {
    uint32_t channels;
    uint32_t rendered = 0;

    if (audio == NULL || output == NULL) {
        return ForgeResultInvalidArgument;
    }

    LOG_FUNC_ENTER(audio)

    if (audio->master == NULL || audio->updateSize == 0 || frame_count % audio->updateSize != 0) {
        LOG_FUNC_EXIT(audio)
        return ForgeResultInvalidArgument;
    }

    channels = audio->master->outputChannels;
    while (rendered < frame_count) {
        float *quantum = output + ((size_t)rendered * channels);

        forge_zero(quantum, sizeof(float) * audio->updateSize * channels);
        fa_audio_update_engine(audio, quantum);
        rendered += audio->updateSize;
    }

    LOG_FUNC_EXIT(audio)
    return 0;
}
#endif

bool fa_audio_resize_decode_cache(ForgeAudioEngine *audio, uint32_t samples) {
    LOG_FUNC_ENTER(audio)
    fa_platform_lock_mutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    if (samples > audio->decodeSamples) {
        float *decode_cache;

        decode_cache = (float *)audio->realloc_func(audio->decodeCache, sizeof(float) * samples);
        if (decode_cache == NULL) {
            fa_platform_unlock_mutex(audio->sourceLock);
            LOG_MUTEX_UNLOCK(audio, audio->sourceLock)
            LOG_FUNC_EXIT(audio)
            return false;
        }
        audio->decodeCache = decode_cache;
        audio->decodeSamples = samples;
    }
    fa_platform_unlock_mutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)
    LOG_FUNC_EXIT(audio)
    return true;
}

ForgeResult fa_audio_alloc_effect_chain(ForgeVoice *voice, const ForgeEffectChain *effect_chain) {
    LOG_FUNC_ENTER(voice->audio)
    voice->effects.state = FORGE_EFFECT_BUFFER_VALID;
    voice->effects.count = effect_chain->effect_count;
    if (voice->effects.count == 0) {
        LOG_FUNC_EXIT(voice->audio)
        return ForgeResultSuccess;
    }

    voice->effects.desc = (ForgeEffectDesc *)voice->audio->malloc_func(voice->effects.count * sizeof(ForgeEffectDesc));
    if (voice->effects.desc == NULL) {
        forge_zero(&voice->effects, sizeof(voice->effects));
        LOG_FUNC_EXIT(voice->audio)
        return ForgeResultOutOfMemory;
    }
    forge_memcpy(voice->effects.desc, effect_chain->effects, voice->effects.count * sizeof(ForgeEffectDesc));
#define ALLOC_EFFECT_PROPERTY(prop, type)                                                                              \
    voice->effects.prop = (type *)voice->audio->malloc_func(voice->effects.count * sizeof(type));                      \
    if (voice->effects.prop == NULL) {                                                                                 \
        voice->audio->free_func(voice->effects.desc);                                                                  \
        voice->audio->free_func(voice->effects.parameters);                                                            \
        voice->audio->free_func(voice->effects.parameterSizes);                                                        \
        voice->audio->free_func(voice->effects.parameterUpdates);                                                      \
        voice->audio->free_func(voice->effects.inPlaceProcessing);                                                     \
        forge_zero(&voice->effects, sizeof(voice->effects));                                                           \
        LOG_FUNC_EXIT(voice->audio)                                                                                    \
        return ForgeResultOutOfMemory;                                                                                 \
    }                                                                                                                  \
    forge_zero(voice->effects.prop, voice->effects.count * sizeof(type));
    ALLOC_EFFECT_PROPERTY(parameters, void *)
    ALLOC_EFFECT_PROPERTY(parameterSizes, uint32_t)
    ALLOC_EFFECT_PROPERTY(parameterUpdates, uint8_t)
    ALLOC_EFFECT_PROPERTY(inPlaceProcessing, uint8_t)
#undef ALLOC_EFFECT_PROPERTY
    LOG_FUNC_EXIT(voice->audio)
    return ForgeResultSuccess;
}

void fa_audio_free_effect_chain(ForgeVoice *voice) {
    LOG_FUNC_ENTER(voice->audio)
    if (voice->effects.count == 0) {
        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    for (uint32_t i = 0; i < voice->effects.count; i += 1) {
        voice->effects.desc[i].effect->unlock_for_process(voice->effects.desc[i].effect);
        forge_effect_destroy(voice->effects.desc[i].effect);
    }

    voice->audio->free_func(voice->effects.desc);
    voice->audio->free_func(voice->effects.parameters);
    voice->audio->free_func(voice->effects.parameterSizes);
    voice->audio->free_func(voice->effects.parameterUpdates);
    voice->audio->free_func(voice->effects.inPlaceProcessing);
    LOG_FUNC_EXIT(voice->audio)
}

ForgeResult fa_audio_voice_output_frequency(ForgeVoice *voice, const ForgeSendList *send_list) {
    uint32_t outSampleRate;
    uint32_t newResampleSamples;
    uint64_t resampleSanityCheck;

    LOG_FUNC_ENTER(voice->audio)

    if ((send_list == NULL) || (send_list->send_count == 0)) {
        /* When we're deliberately given no sends, use master rate! */
        outSampleRate = voice->audio->master->master.inputSampleRate;
    } else {
        outSampleRate = send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_MASTER
                            ? send_list->sends[0].output_voice->master.inputSampleRate
                            : send_list->sends[0].output_voice->mix.inputSampleRate;
    }
    newResampleSamples = (uint32_t)forge_ceil(voice->audio->updateSize * (double)outSampleRate /
                                              (double)voice->audio->master->master.inputSampleRate);
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
        if ((voice->src.resampleSamples != 0) && (newResampleSamples != voice->src.resampleSamples) &&
            (voice->effects.count > 0)) {
            LOG_FUNC_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
        voice->src.resampleSamples = newResampleSamples;
    } else /* (voice->type == FORGE_AUDIO_VOICE_SUBMIX) */
    {
        if ((voice->mix.outputSamples != 0) && (newResampleSamples != voice->mix.outputSamples) &&
            (voice->effects.count > 0)) {
            LOG_FUNC_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
        voice->mix.outputSamples = newResampleSamples;

        voice->mix.resampleStep = DOUBLE_TO_FIXED(((double)voice->mix.inputSampleRate / (double)outSampleRate));

        /* Because we used ceil earlier, there's a chance that
         * downsampling submixes will go past the number of samples
         * available. Sources can do this thanks to padding, but we
         * do not have that padding for submixes, so undo the ceil and
         * turn it into a floor.
         */
        resampleSanityCheck = (voice->mix.resampleStep * voice->mix.outputSamples) >> FIXED_PRECISION;
        if (resampleSanityCheck > (voice->mix.inputSamples / voice->mix.inputChannels)) {
            voice->mix.outputSamples -= 1;
        }
    }

    LOG_FUNC_EXIT(voice->audio)
    return 0;
}

const float fa_audio_matrix_defaults[8][8][64] = {
#include "matrix_defaults.inl"
};
