/* ForgeAudioEngine
 *
 * Copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma, and the MonoGame Team
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

#include "forge_audio_internal.h"

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
void ForgeAudio_Internal_debug(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const char *fmt,
    ...
) {
    char output[1024];
    char *out = output;
    va_list va;
    out[0] = '\0';

    /* Logging extras */
    if (audio->debug.log_thread_id)
    {
        out += ForgeAudio_snprintf(
            out,
            sizeof(output) - (out - output),
            "0x%" ForgeAudio_PRIx64 " ",
            ForgeAudio_PlatformGetThreadID()
        );
    }
    if (audio->debug.log_fileline)
    {
        out += ForgeAudio_snprintf(
            out,
            sizeof(output) - (out - output),
            "%s:%u ",
            file,
            line
        );
    }
    if (audio->debug.log_function_name)
    {
        out += ForgeAudio_snprintf(
            out,
            sizeof(output) - (out - output),
            "%s ",
            func
        );
    }
    if (audio->debug.log_timing)
    {
        out += ForgeAudio_snprintf(
            out,
            sizeof(output) - (out - output),
            "%dms ",
            ForgeAudio_timems()
        );
    }

    /* The actual message... */
    va_start(va, fmt);
    ForgeAudio_vsnprintf(
        out,
        sizeof(output) - (out - output),
        fmt,
        va
    );
    va_end(va);

    /* Print, finally. */
    ForgeAudio_Log(output);
}

static const char *get_wformattag_string(const ForgeAudioFormat *fmt)
{
#define FMT_STRING(suffix) \
    if (fmt->format_tag == FORGE_AUDIO_FORMAT_##suffix) \
    { \
        return #suffix; \
    }
    FMT_STRING(PCM)
    FMT_STRING(IEEE_FLOAT)
    FMT_STRING(XMAUDIO2)
    FMT_STRING(WMAUDIO2)
    FMT_STRING(WMAUDIO3)
    FMT_STRING(EXTENSIBLE)
#undef FMT_STRING
    return "UNKNOWN!";
}

static const char *get_subformat_string(const ForgeAudioFormat *fmt)
{
    const ForgeAudioFormatExtensible *fmtex = (const ForgeAudioFormatExtensible*) fmt;

    if (fmt->format_tag != FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        return "N/A";
    }
    if (!ForgeAudio_memcmp(&fmtex->sub_format, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid)))
    {
        return "IEEE_FLOAT";
    }
    if (!ForgeAudio_memcmp(&fmtex->sub_format, &FORGE_AUDIO_SUBTYPE_PCM, sizeof(ForgeGuid)))
    {
        return "PCM";
    }
    return "UNKNOWN!";
}

void ForgeAudio_Internal_debug_fmt(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const ForgeAudioFormat *fmt
) {
    ForgeAudio_Internal_debug(
        audio,
        file,
        line,
        func,
        (
            "{"
            "format_tag: 0x%x %s, "
            "channels: %u, "
            "sample_rate: %u, "
            "bits_per_sample: %u, "
            "block_align: %u, "
            "sub_format: %s"
            "}"
        ),
        fmt->format_tag,
        get_wformattag_string(fmt),
        fmt->channels,
        fmt->sample_rate,
        fmt->bits_per_sample,
        fmt->block_align,
        get_subformat_string(fmt)
    );
}
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */

bool array_reserve(ForgeAudioEngine *audio, void **elements, size_t *capacity, size_t count, size_t size)
{
    size_t new_capacity, max_capacity;
    void *new_elements;

    if (count <= *capacity)
    {
        return true;
    }

    max_capacity = ~(size_t)0 / size;
    if (count > max_capacity)
    {
        return false;
    }

    new_capacity = ForgeAudio_max(4, *capacity);
    while (new_capacity < count && new_capacity <= max_capacity / 2)
    {
        new_capacity *= 2;
    }
    if (new_capacity < count)
    {
        new_capacity = max_capacity;
    }

    new_elements = audio->realloc_func(*elements, new_capacity * size);
    if (new_elements == NULL)
    {
        return false;
    }

    *elements = new_elements;
    *capacity = new_capacity;
    return true;
}

void LinkedList_AddEntry(
    LinkedList **start,
    void* toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
) {
    LinkedList *newEntry, *latest;
    newEntry = (LinkedList*) malloc_func(sizeof(LinkedList));
    newEntry->entry = toAdd;
    newEntry->next = NULL;
    ForgeAudio_PlatformLockMutex(lock);
    if (*start == NULL)
    {
        *start = newEntry;
    }
    else
    {
        latest = *start;
        while (latest->next != NULL)
        {
            latest = latest->next;
        }
        latest->next = newEntry;
    }
    ForgeAudio_PlatformUnlockMutex(lock);
}

void LinkedList_PrependEntry(
    LinkedList **start,
    void* toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
) {
    LinkedList *newEntry;
    newEntry = (LinkedList*) malloc_func(sizeof(LinkedList));
    newEntry->entry = toAdd;
    ForgeAudio_PlatformLockMutex(lock);
    newEntry->next = *start;
    *start = newEntry;
    ForgeAudio_PlatformUnlockMutex(lock);
}

void LinkedList_RemoveEntry(
    LinkedList **start,
    void* toRemove,
    ForgeAudioMutex lock,
    ForgeFreeFunc free_func
) {
    LinkedList *latest, *prev;
    ForgeAudio_PlatformLockMutex(lock);
    latest = *start;
    prev = latest;
    while (latest != NULL)
    {
        if (latest->entry == toRemove)
        {
            if (latest == prev) /* First in list */
            {
                *start = latest->next;
            }
            else
            {
                prev->next = latest->next;
            }
            free_func(latest);
            ForgeAudio_PlatformUnlockMutex(lock);
            return;
        }
        prev = latest;
        latest = latest->next;
    }
    ForgeAudio_PlatformUnlockMutex(lock);
    ForgeAudio_assert(0 && "LinkedList element not found!");
}

void ForgeAudio_Internal_InsertSubmixSorted(
    LinkedList **start,
    ForgeSubmixVoice *toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
) {
    LinkedList *newEntry, *latest;
    newEntry = (LinkedList*) malloc_func(sizeof(LinkedList));
    newEntry->entry = toAdd;
    newEntry->next = NULL;
    ForgeAudio_PlatformLockMutex(lock);
    if (*start == NULL)
    {
        *start = newEntry;
    }
    else
    {
        latest = *start;

        /* Special case if the new stage is lower than everyone else */
        if (toAdd->mix.processingStage < ((ForgeSubmixVoice*) latest->entry)->mix.processingStage)
        {
            newEntry->next = latest;
            *start = newEntry;
        }
        else
        {
            /* If we got here, we know that the new stage is
             * _at least_ as high as the first submix in the list.
             *
             * Each loop iteration checks to see if the new stage
             * is smaller than `latest->next`, meaning it fits
             * between `latest` and `latest->next`.
             */
            while (latest->next != NULL)
            {
                if (toAdd->mix.processingStage < ((ForgeSubmixVoice *) latest->next->entry)->mix.processingStage)
                {
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
            if (newEntry->next == NULL)
            {
                latest->next = newEntry;
            }
        }
    }
    ForgeAudio_PlatformUnlockMutex(lock);
}

static uint32_t ForgeAudio_Internal_GetBytesRequested(
    ForgeSourceVoice *voice,
    uint32_t decoding
) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t result = decoding * block_size;

    LOG_FUNC_ENTER(voice->audio)

    for (size_t i = 0; i < voice->src.queued_buffer_count; i += 1)
    {
        const struct queued_buffer *buffer = &voice->src.queued_buffers[i];
        uint32_t size = 0;

        if (buffer->buffer.loop_count > 0)
        {
            size += buffer->loop_bytes * buffer->buffer.loop_count;
        }
        size += buffer->play_bytes;
        size -= voice->src.curBufferOffset * block_size;

        if (size > result)
        {
            LOG_FUNC_EXIT(voice->audio)
            return 0;
        }
        result -= size;
    }

    LOG_FUNC_EXIT(voice->audio)
    return result;
}

static uint32_t buffer_get_end(ForgeSourceVoice *voice, const struct queued_buffer *buffer)
{
    const uint32_t block_size = voice->src.format->block_align;

    if (buffer->buffer.loop_count != 0)
    {
        return buffer->buffer.loop_begin + (buffer->loop_bytes / block_size);
    }

    return buffer->buffer.play_begin + (buffer->play_bytes / block_size);
}

static void start_buffer(ForgeSourceVoice *voice, struct queued_buffer *buffer)
{
    if (!buffer->sent_OnStartBuffer)
    {
        buffer->sent_OnStartBuffer = true;

        if (    voice->src.callback != NULL &&
            voice->src.callback->on_buffer_start != NULL    )
        {
            ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

            ForgeAudio_PlatformUnlockMutex(voice->sendLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

            ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

            voice->src.callback->on_buffer_start(
                voice->src.callback,
                buffer->buffer.context
            );

            ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
            LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

            ForgeAudio_PlatformLockMutex(voice->sendLock);
            LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

            ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
            LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
        }
    }
}

static void end_buffer(ForgeSourceVoice *voice)
{
    struct queued_buffer *buffer = &voice->src.queued_buffers[0];
    bool eos = buffer->buffer.flags & FORGE_AUDIO_END_OF_STREAM;
    ForgeVoiceCallback *callback = voice->src.callback;
    void *context = buffer->buffer.context;

    if (buffer->buffer.loop_count > 0)
    {
        voice->src.curBufferOffset = buffer->buffer.loop_begin;
        if (buffer->buffer.loop_count < FORGE_AUDIO_LOOP_INFINITE)
        {
            buffer->buffer.loop_count -= 1;
        }

        if (callback != NULL && callback->on_loop_end != NULL)
        {
            ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

            ForgeAudio_PlatformUnlockMutex(voice->sendLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

            ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

            callback->on_loop_end(callback, context);

            ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
            LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

            ForgeAudio_PlatformLockMutex(voice->sendLock);
            LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

            ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
            LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
        }
        return;
    }

    if (eos)
    {
        voice->src.curBufferOffsetDec = 0;
        voice->src.totalSamples = 0;
    }

    LOG_INFO(voice->audio, "Voice %p, finished with buffer %p", (void*) voice, (void*) buffer)

    ForgeAudio_memmove(
        &voice->src.queued_buffers[0],
        &voice->src.queued_buffers[1],
        (voice->src.queued_buffer_count - 1) * sizeof(*voice->src.queued_buffers)
    );
    voice->src.queued_buffer_count -= 1;

    if (voice->src.queued_buffer_count != 0)
    {
        voice->src.curBufferOffset = voice->src.queued_buffers[0].buffer.play_begin;
    }

    if (callback != NULL)
    {
        ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        if (callback->on_buffer_end != NULL)
        {
            callback->on_buffer_end(callback, context);
        }

        if (eos && callback->on_stream_end != NULL)
        {
            callback->on_stream_end(callback);
        }

        ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        ForgeAudio_PlatformLockMutex(voice->sendLock);
        LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

        ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
        LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
    }
}

static void ForgeAudio_Internal_DecodeBuffers(
    ForgeSourceVoice *voice,
    uint64_t *toDecode
) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t decoded = 0;

    LOG_FUNC_ENTER(voice->audio)

    /* This should never go past the max ratio size */
    ForgeAudio_assert(*toDecode <= voice->src.decodeSamples);

    while (decoded < *toDecode && voice->src.queued_buffer_count != 0)
    {
        float *dst = voice->audio->decodeCache + (decoded * voice->src.format->channels);
        struct queued_buffer *buffer;
        uint32_t decode_count;

        buffer = &voice->src.queued_buffers[0];
        start_buffer(voice, buffer);

        decode_count = ForgeAudio_min(
            (uint32_t) *toDecode - decoded,
            buffer_get_end(voice, buffer) - voice->src.curBufferOffset
        );

        voice->src.decode(
            voice,
            buffer->buffer.audio_data + (voice->src.curBufferOffset * block_size),
            dst,
            decode_count
        );

        LOG_INFO(
            voice->audio,
            "Voice %p, buffer %p, decoded %u samples from [%u,%u)",
            (void*) voice,
            (void*) buffer,
            decode_count,
            voice->src.curBufferOffset,
            voice->src.curBufferOffset + decode_count
        )

        decoded += decode_count;
        voice->src.curBufferOffset += decode_count;
        voice->src.totalSamples += decode_count;

        if (decoded < *toDecode)
        {
            end_buffer(voice);
        }
    }

    if (decoded < *toDecode)
    {
        ForgeAudio_zero(
            voice->audio->decodeCache + (decoded * voice->src.format->channels),
            sizeof(float) * ((*toDecode - decoded) * voice->src.format->channels)
        );
    }

    if (voice->src.queued_buffer_count != 0)
    {
        float *dst = voice->audio->decodeCache + (decoded * voice->src.format->channels);
        struct queued_buffer *buffer = &voice->src.queued_buffers[0];
        uint32_t decode_count;

        decode_count = ForgeAudio_min(
            EXTRA_DECODE_PADDING,
            buffer_get_end(voice, buffer) - voice->src.curBufferOffset
        );

        voice->src.decode(
            voice,
            buffer->buffer.audio_data + (voice->src.curBufferOffset * block_size),
            dst,
            decode_count
        );

        if (decode_count < EXTRA_DECODE_PADDING)
        {
            ForgeAudio_zero(
                voice->audio->decodeCache + (decoded * voice->src.format->channels),
                sizeof(float) * ((EXTRA_DECODE_PADDING - decode_count) * voice->src.format->channels)
            );
        }
    }
    else
    {
        ForgeAudio_zero(
            voice->audio->decodeCache + (
                decoded * voice->src.format->channels
            ),
            sizeof(float) * (
                EXTRA_DECODE_PADDING *
                voice->src.format->channels
            )
        );
    }

    *toDecode = decoded;
    LOG_FUNC_EXIT(voice->audio)
}

static inline void ForgeAudio_Internal_FilterVoice(
    ForgeAudioEngine *audio,
    const ForgeFilterParameters *filter,
    ForgeAudioFilterState *filterState,
    float *samples,
    uint32_t numSamples,
    uint16_t numChannels
) {
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
     * Please note that ForgeFilterParameters.frequency is defined as:
     *
     * (2 * sin(pi * (desired filter cutoff frequency) / sampleRate))
     *
     * - @JohanSmet
     */

    for (j = 0; j < numSamples; j += 1)
    for (ci = 0; ci < numChannels; ci += 1)
    {
        filterState[ci][ForgeFilterLowPass] = filterState[ci][ForgeFilterLowPass] + (filter->frequency * filterState[ci][ForgeFilterBandPass]);
        filterState[ci][ForgeFilterHighPass] = samples[j * numChannels + ci] - filterState[ci][ForgeFilterLowPass] - (filter->one_over_q * filterState[ci][ForgeFilterBandPass]);
        filterState[ci][ForgeFilterBandPass] = (filter->frequency * filterState[ci][ForgeFilterHighPass]) + filterState[ci][ForgeFilterBandPass];
        filterState[ci][ForgeFilterNotch] = filterState[ci][ForgeFilterHighPass] + filterState[ci][ForgeFilterLowPass];
        samples[j * numChannels + ci] = filterState[ci][filter->type] * filter->wet_dry_mix + samples[j * numChannels + ci] * (1.0 - filter->wet_dry_mix);
    }

    LOG_FUNC_EXIT(audio)
}

static void ForgeAudio_Internal_ResizeEffectChainCache(ForgeAudioEngine *audio, uint32_t samples)
{
    LOG_FUNC_ENTER(audio)
    if (samples > audio->effectChainSamples)
    {
        audio->effectChainSamples = samples;
        audio->effectChainCache = (float*) audio->realloc_func(
            audio->effectChainCache,
            sizeof(float) * audio->effectChainSamples
        );
    }
    LOG_FUNC_EXIT(audio)
}

static inline float *ForgeAudio_Internal_ProcessEffectChain(
    ForgeVoice *voice,
    float *buffer,
    uint32_t *samples
) {
    ForgeEffect *effect;
    ForgeEffectProcessBuffer srcParams, dstParams;

    LOG_FUNC_ENTER(voice->audio)

    /* Set up the buffer to be written into */
    srcParams.buffer = buffer;
    srcParams.buffer_flags = FORGE_EFFECT_BUFFER_SILENT;
    srcParams.valid_frame_count = *samples;
    for (uint32_t i = 0; i < srcParams.valid_frame_count; i += 1)
    {
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
    for (uint32_t i = 0; i < voice->effects.count; i += 1)
    {
        effect = voice->effects.desc[i].effect;

        if (!voice->effects.inPlaceProcessing[i])
        {
            if (dstParams.buffer == buffer)
            {
                ForgeAudio_Internal_ResizeEffectChainCache(
                    voice->audio,
                    voice->effects.desc[i].output_channels * srcParams.valid_frame_count
                );
                dstParams.buffer = voice->audio->effectChainCache;
            }
            else
            {
                /* FIXME: What if this is smaller because
                 * inputChannels < desc[i].output_channels?
                 */
                dstParams.buffer = buffer;
            }

            ForgeAudio_zero(
                dstParams.buffer,
                voice->effects.desc[i].output_channels * srcParams.valid_frame_count * sizeof(float)
            );
        }

        if (voice->effects.parameterUpdates[i])
        {
            effect->set_parameters(
                effect,
                voice->effects.parameters[i],
                voice->effects.parameterSizes[i]
            );
            voice->effects.parameterUpdates[i] = 0;
        }

        effect->process(
            effect,
            1,
            &srcParams,
            1,
            &dstParams,
            voice->effects.desc[i].initial_state
        );

        ForgeAudio_memcpy(&srcParams, &dstParams, sizeof(dstParams));
    }

    *samples = dstParams.valid_frame_count;

    /* Save the output buffer-flags so the mixer-function can determine when it's save to stop processing the effect chain */
    voice->effects.state = dstParams.buffer_flags;

    LOG_FUNC_EXIT(voice->audio)
    return (float*) dstParams.buffer;
}

static void ForgeAudio_Internal_ResizeResampleCache(ForgeAudioEngine *audio, uint32_t samples)
{
       LOG_FUNC_ENTER(audio)
       if (samples > audio->resampleSamples)
       {
               audio->resampleSamples = samples;
               audio->resampleCache = (float*) audio->realloc_func(
                       audio->resampleCache,
                       sizeof(float) * audio->resampleSamples
               );
       }
       LOG_FUNC_EXIT(audio)
}

static void ForgeAudio_Internal_MixSource(ForgeSourceVoice *voice)
{
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

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Calculate the resample stepping value */
    if (voice->src.resampleFreq != voice->src.freqRatio * voice->src.format->sample_rate)
    {
        out = (voice->sends.send_count == 0) ?
            voice->audio->master : /* Barf */
            voice->sends.sends->output_voice;
        outputRate = (out->type == FORGE_AUDIO_VOICE_MASTER) ?
            out->master.inputSampleRate :
            out->mix.inputSampleRate;
        stepd = (
            voice->src.freqRatio *
            (double) voice->src.format->sample_rate /
            (double) outputRate
        );
        voice->src.resampleStep = DOUBLE_TO_FIXED(stepd);
        voice->src.resampleFreq = voice->src.freqRatio * voice->src.format->sample_rate;
    }

    if (voice->src.active == 2)
    {
        /* We're just playing tails, skip all buffer stuff */
        ForgeAudio_Internal_ResizeResampleCache(
                voice->audio,
                voice->src.resampleSamples * voice->src.format->channels
        );
        mixed = voice->src.resampleSamples;
        ForgeAudio_zero(
            voice->audio->resampleCache,
            mixed * voice->src.format->channels * sizeof(float)
        );
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
    if (    voice->src.callback != NULL &&
        voice->src.callback->on_voice_processing_pass_start != NULL    )
    {
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        voice->src.callback->on_voice_processing_pass_start(
            voice->src.callback,
            ForgeAudio_Internal_GetBytesRequested(voice, (uint32_t) toDecode)
        );

        ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        ForgeAudio_PlatformLockMutex(voice->sendLock);
        LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    }

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    /* Nothing to do? */
    if (voice->src.queued_buffer_count == 0)
    {
        ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

        if (voice->effects.count > 0 && voice->effects.state != FORGE_EFFECT_BUFFER_SILENT)
        {
            /* do not stop while the effect chain generates a non-silent buffer */
            ForgeAudio_Internal_ResizeResampleCache(
                    voice->audio,
                    voice->src.resampleSamples * voice->src.format->channels
            );
            mixed = voice->src.resampleSamples;
            ForgeAudio_zero(
                voice->audio->resampleCache,
                mixed * voice->src.format->channels * sizeof(float)
            );
            finalSamples = voice->audio->resampleCache;
            goto sendwork;
        }

        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        if (    voice->src.callback != NULL &&
            voice->src.callback->on_voice_processing_pass_end != NULL)
        {
            voice->src.callback->on_voice_processing_pass_end(
                voice->src.callback
            );
        }

        ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    /* Decode... */
    ForgeAudio_Internal_DecodeBuffers(voice, &toDecode);

    /* Subtract any padding samples from the total, if applicable */
    if (    voice->src.curBufferOffsetDec > 0 &&
        voice->src.totalSamples > 0    )
    {
        voice->src.totalSamples -= 1;
    }

    /* Okay, we're done messing with client data */
    if (    voice->src.callback != NULL &&
        voice->src.callback->on_voice_processing_pass_end != NULL)
    {
        ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

        ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        voice->src.callback->on_voice_processing_pass_end(
            voice->src.callback
        );

        ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        ForgeAudio_PlatformLockMutex(voice->sendLock);
        LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

        ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
        LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
    }

    /* Nothing to resample? */
    if (toDecode == 0)
    {
        ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
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
    /* Add the padding, for some reason this helps? */
    toResample += EXTRA_DECODE_PADDING;
    /* FIXME: I feel like this should be an assert but I suck */
    toResample = ForgeAudio_min(toResample, voice->src.resampleSamples);

    /* Resample... */
    if (voice->src.resampleStep == FIXED_ONE)
    {
        /* Actually, just use the existing buffer... */
        finalSamples = voice->audio->decodeCache;
    }
    else
    {
        ForgeAudio_Internal_ResizeResampleCache(
                voice->audio,
                voice->src.resampleSamples * voice->src.format->channels
        );
        voice->src.resample(
            voice->audio->decodeCache,
            voice->audio->resampleCache,
            &voice->src.resampleOffset,
            voice->src.resampleStep,
            toResample,
            (uint8_t) voice->src.format->channels
        );
        finalSamples = voice->audio->resampleCache;
    }

    /* Update buffer offsets */
    if (voice->src.queued_buffer_count != 0)
    {
        /* Increment fixed offset by resample size, int to fixed... */
        voice->src.curBufferOffsetDec += toResample * voice->src.resampleStep;
        /* ... chop off any ints we got from the above increment */
        voice->src.curBufferOffsetDec &= FIXED_FRACTION_MASK;

        /* Dec >0? We need one frame from the past...
         * FIXME: We can't go back to a prev buffer though?
         */
        if (    voice->src.curBufferOffsetDec > 0 &&
            voice->src.curBufferOffset > 0    )
        {
            voice->src.curBufferOffset -= 1;
        }
    }
    else
    {
        voice->src.curBufferOffsetDec = 0;
        voice->src.curBufferOffset = 0;
    }

    /* Done with buffers, finally. */
    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    mixed = (uint32_t) toResample;

sendwork:

    /* Filters */
    if (voice->flags & FORGE_AUDIO_VOICE_USEFILTER)
    {
        ForgeAudio_PlatformLockMutex(voice->filterLock);
        LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
        ForgeAudio_Internal_FilterVoice(
            voice->audio,
            &voice->filter,
            voice->filterState,
            finalSamples,
            mixed,
            voice->src.format->channels
        );
        ForgeAudio_PlatformUnlockMutex(voice->filterLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    }

    /* process effect chain */
    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.count > 0)
    {
        /* If we didn't get the full size of the update, we have to fill
         * it with silence so the effect can process a whole update
         */
        if (mixed < voice->src.resampleSamples)
        {
            ForgeAudio_zero(
                finalSamples + (mixed * voice->src.format->channels),
                (voice->src.resampleSamples - mixed) * voice->src.format->channels * sizeof(float)
            );
            mixed = voice->src.resampleSamples;
        }
        finalSamples = ForgeAudio_Internal_ProcessEffectChain(
            voice,
            finalSamples,
            &mixed
        );
    }
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    /* Nowhere to send it? Just skip the rest...*/
    if (voice->sends.send_count == 0)
    {
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    /* Send float cache to sends */
    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    for (uint32_t i = 0; i < voice->sends.send_count; i += 1)
    {
        out = voice->sends.sends[i].output_voice;
        if (out->type == FORGE_AUDIO_VOICE_MASTER)
        {
            stream = out->master.output;
            oChan = out->master.inputChannels;
        }
        else
        {
            stream = out->mix.inputCache;
            oChan = out->mix.inputChannels;
        }

        voice->sendMix[i](
            mixed,
            voice->outputChannels,
            oChan,
            finalSamples,
            stream,
            voice->mixCoefficients[i]
        );

        if (voice->sends.sends[i].flags & FORGE_AUDIO_SEND_USEFILTER)
        {
            ForgeAudio_Internal_FilterVoice(
                voice->audio,
                &voice->sendFilter[i],
                voice->sendFilterState[i],
                stream,
                mixed,
                oChan
            );
        }
    }
    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_FUNC_EXIT(voice->audio)
}

static void ForgeAudio_Internal_MixSubmix(ForgeSubmixVoice *voice)
{
    float *stream;
    uint32_t oChan;
    ForgeVoice *out;
    uint32_t resampled;
    uint64_t resampleOffset = 0;
    float *finalSamples;

    LOG_FUNC_ENTER(voice->audio)
    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Resample */
    if (voice->mix.resampleStep == FIXED_ONE)
    {
        /* Actually, just use the existing buffer... */
        finalSamples = voice->mix.inputCache;
    }
    else
    {
        ForgeAudio_Internal_ResizeResampleCache(
                voice->audio,
                voice->mix.outputSamples * voice->mix.inputChannels
        );
        voice->mix.resample(
            voice->mix.inputCache,
            voice->audio->resampleCache,
            &resampleOffset,
            voice->mix.resampleStep,
            voice->mix.outputSamples,
            (uint8_t) voice->mix.inputChannels
        );
        finalSamples = voice->audio->resampleCache;
    }
    resampled = voice->mix.outputSamples * voice->mix.inputChannels;

    /* Submix overall volume is applied _before_ effects/filters, blech! */
    if (voice->volume != 1.0f)
    {
        ForgeAudio_Internal_Amplify(
            finalSamples,
            resampled,
            voice->volume
        );
    }
    resampled /= voice->mix.inputChannels;

    /* Filters */
    if (voice->flags & FORGE_AUDIO_VOICE_USEFILTER)
    {
        ForgeAudio_PlatformLockMutex(voice->filterLock);
        LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
        ForgeAudio_Internal_FilterVoice(
            voice->audio,
            &voice->filter,
            voice->filterState,
            finalSamples,
            resampled,
            voice->mix.inputChannels
        );
        ForgeAudio_PlatformUnlockMutex(voice->filterLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    }

    /* process effect chain */
    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.count > 0)
    {
        finalSamples = ForgeAudio_Internal_ProcessEffectChain(
            voice,
            finalSamples,
            &resampled
        );
    }
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    /* Nothing more to do? */
    if (voice->sends.send_count == 0)
    {
        goto end;
    }

    /* Send float cache to sends */
    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    for (uint32_t i = 0; i < voice->sends.send_count; i += 1)
    {
        out = voice->sends.sends[i].output_voice;
        if (out->type == FORGE_AUDIO_VOICE_MASTER)
        {
            stream = out->master.output;
            oChan = out->master.inputChannels;
        }
        else
        {
            stream = out->mix.inputCache;
            oChan = out->mix.inputChannels;
        }

        voice->sendMix[i](
            resampled,
            voice->outputChannels,
            oChan,
            finalSamples,
            stream,
            voice->mixCoefficients[i]
        );

        if (voice->sends.sends[i].flags & FORGE_AUDIO_SEND_USEFILTER)
        {
            ForgeAudio_Internal_FilterVoice(
                voice->audio,
                &voice->sendFilter[i],
                voice->sendFilterState[i],
                stream,
                resampled,
                oChan
            );
        }
    }
    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    /* Zero this at the end, for the next update */
end:
    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    ForgeAudio_zero(
        voice->mix.inputCache,
        sizeof(float) * voice->mix.inputSamples
    );
    LOG_FUNC_EXIT(voice->audio)
}

static void ForgeAudio_Internal_FlushPendingBuffers(ForgeSourceVoice *voice)
{
    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.callback == NULL || voice->src.callback->on_buffer_end == NULL)
    {
        voice->src.flush_buffer_count = 0;
    }

    /* Remove pending flushed buffers and send an event for each one */
    else while (voice->src.flush_buffer_count > 0)
    {
        void *context = voice->src.flush_buffers[0].buffer.context;

        voice->src.flush_buffer_count -= 1;
        ForgeAudio_memmove(
            &voice->src.flush_buffers[0],
            &voice->src.flush_buffers[1],
            voice->src.flush_buffer_count * sizeof(*voice->src.flush_buffers)
        );

        ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock);

        ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        voice->src.callback->on_buffer_end(voice->src.callback, context);

        ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)

        ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
        LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock);
    }

    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
}

static void FORGE_AUDIO_CALL ForgeAudio_Internal_GenerateOutput(ForgeAudioEngine *audio, float *output)
{
    uint32_t totalSamples;
    LinkedList *list;
    float *effectOut;
    ForgeEngineCallback *callback;

    LOG_FUNC_ENTER(audio)
    if (!audio->active)
    {
        LOG_FUNC_EXIT(audio)
        return;
    }

    /* Apply any committed changes */
    ForgeAudio_OperationSet_Execute(audio);

    /* ProcessingPassStart callbacks */
    ForgeAudio_PlatformLockMutex(audio->callbackLock);
    LOG_MUTEX_LOCK(audio, audio->callbackLock)
    list = audio->callbacks;
    while (list != NULL)
    {
        callback = (ForgeEngineCallback*) list->entry;
        if (callback->on_processing_pass_start != NULL)
        {
            callback->on_processing_pass_start(
                callback
            );
        }
        list = list->next;
    }
    ForgeAudio_PlatformUnlockMutex(audio->callbackLock);
    LOG_MUTEX_UNLOCK(audio, audio->callbackLock)

    /* Writes to master will directly write to output, but ONLY if there
     * isn't any channel-changing effect processing to do first.
     */
    if (audio->master->master.effectCache != NULL)
    {
        audio->master->master.output = audio->master->master.effectCache;
        ForgeAudio_zero(
            audio->master->master.effectCache,
            (
                sizeof(float) *
                audio->updateSize *
                audio->master->master.inputChannels
            )
        );
    }
    else
    {
        audio->master->master.output = output;
    }

    /* Mix sources */
    ForgeAudio_PlatformLockMutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    list = audio->sources;
    while (list != NULL)
    {
        audio->processingSource = (ForgeSourceVoice*) list->entry;

        ForgeAudio_Internal_FlushPendingBuffers(audio->processingSource);
        if (audio->processingSource->src.active)
        {
            ForgeAudio_Internal_MixSource(audio->processingSource);
            ForgeAudio_Internal_FlushPendingBuffers(audio->processingSource);
        }

        list = list->next;
    }
    audio->processingSource = NULL;
    ForgeAudio_PlatformUnlockMutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)

    /* Mix submixes, ordered by processing stage */
    ForgeAudio_PlatformLockMutex(audio->submixLock);
    LOG_MUTEX_LOCK(audio, audio->submixLock)
    list = audio->submixes;
    while (list != NULL)
    {
        ForgeAudio_Internal_MixSubmix((ForgeSubmixVoice*) list->entry);
        list = list->next;
    }
    ForgeAudio_PlatformUnlockMutex(audio->submixLock);
    LOG_MUTEX_UNLOCK(audio, audio->submixLock)

    /* Apply master volume */
    if (audio->master->volume != 1.0f)
    {
        ForgeAudio_Internal_Amplify(
            audio->master->master.output,
            audio->updateSize * audio->master->master.inputChannels,
            audio->master->volume
        );
    }

    /* process master effect chain */
    ForgeAudio_PlatformLockMutex(audio->master->effectLock);
    LOG_MUTEX_LOCK(audio, audio->master->effectLock)
    if (audio->master->effects.count > 0)
    {
        totalSamples = audio->updateSize;
        effectOut = ForgeAudio_Internal_ProcessEffectChain(
            audio->master,
            audio->master->master.output,
            &totalSamples
        );

        if (effectOut != output)
        {
            ForgeAudio_memcpy(
                output,
                effectOut,
                totalSamples * audio->master->outputChannels * sizeof(float)
            );
        }
        if (totalSamples < audio->updateSize)
        {
            ForgeAudio_zero(
                output + (totalSamples * audio->master->outputChannels),
                (audio->updateSize - totalSamples) * sizeof(float)
            );
        }
    }
    ForgeAudio_PlatformUnlockMutex(audio->master->effectLock);
    LOG_MUTEX_UNLOCK(audio, audio->master->effectLock)

    /* on_processing_pass_end callbacks */
    ForgeAudio_PlatformLockMutex(audio->callbackLock);
    LOG_MUTEX_LOCK(audio, audio->callbackLock)
    list = audio->callbacks;
    while (list != NULL)
    {
        callback = (ForgeEngineCallback*) list->entry;
        if (callback->on_processing_pass_end != NULL)
        {
            callback->on_processing_pass_end(
                callback
            );
        }
        list = list->next;
    }
    ForgeAudio_PlatformUnlockMutex(audio->callbackLock);
    LOG_MUTEX_UNLOCK(audio, audio->callbackLock)

    LOG_FUNC_EXIT(audio)
}

void ForgeAudio_Internal_UpdateEngine(ForgeAudioEngine *audio, float *output)
{
    LOG_FUNC_ENTER(audio)
    if (audio->client_engine_proc)
    {
        audio->client_engine_proc(
            &ForgeAudio_Internal_GenerateOutput,
            audio,
            output,
            audio->clientEngineUser
        );
    }
    else
    {
        ForgeAudio_Internal_GenerateOutput(audio, output);
    }
    LOG_FUNC_EXIT(audio)
}

void ForgeAudio_Internal_ResizeDecodeCache(ForgeAudioEngine *audio, uint32_t samples)
{
    LOG_FUNC_ENTER(audio)
    ForgeAudio_PlatformLockMutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    if (samples > audio->decodeSamples)
    {
        audio->decodeSamples = samples;
        audio->decodeCache = (float*) audio->realloc_func(
            audio->decodeCache,
            sizeof(float) * audio->decodeSamples
        );
    }
    ForgeAudio_PlatformUnlockMutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)
    LOG_FUNC_EXIT(audio)
}

void ForgeAudio_Internal_AllocEffectChain(
    ForgeVoice *voice,
    const ForgeEffectChain *effect_chain
) {
    LOG_FUNC_ENTER(voice->audio)
    voice->effects.state = FORGE_EFFECT_BUFFER_VALID;
    voice->effects.count = effect_chain->effect_count;
    if (voice->effects.count == 0)
    {
        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    voice->effects.desc = (ForgeEffectDesc*) voice->audio->malloc_func(
        voice->effects.count * sizeof(ForgeEffectDesc)
    );
    ForgeAudio_memcpy(
        voice->effects.desc,
        effect_chain->effects,
        voice->effects.count * sizeof(ForgeEffectDesc)
    );
    #define ALLOC_EFFECT_PROPERTY(prop, type) \
        voice->effects.prop = (type*) voice->audio->malloc_func( \
            voice->effects.count * sizeof(type) \
        ); \
        ForgeAudio_zero( \
            voice->effects.prop, \
            voice->effects.count * sizeof(type) \
        );
    ALLOC_EFFECT_PROPERTY(parameters, void*)
    ALLOC_EFFECT_PROPERTY(parameterSizes, uint32_t)
    ALLOC_EFFECT_PROPERTY(parameterUpdates, uint8_t)
    ALLOC_EFFECT_PROPERTY(inPlaceProcessing, uint8_t)
    #undef ALLOC_EFFECT_PROPERTY
    LOG_FUNC_EXIT(voice->audio)
}

void ForgeAudio_Internal_FreeEffectChain(ForgeVoice *voice)
{
    LOG_FUNC_ENTER(voice->audio)
    if (voice->effects.count == 0)
    {
        LOG_FUNC_EXIT(voice->audio)
        return;
    }

    for (uint32_t i = 0; i < voice->effects.count; i += 1)
    {
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

ForgeResult ForgeAudio_Internal_VoiceOutputFrequency(
    ForgeVoice *voice,
    const ForgeSendList *send_list
) {
    uint32_t outSampleRate;
    uint32_t newResampleSamples;
    uint64_t resampleSanityCheck;

    LOG_FUNC_ENTER(voice->audio)

    if ((send_list == NULL) || (send_list->send_count == 0))
    {
        /* When we're deliberately given no sends, use master rate! */
        outSampleRate = voice->audio->master->master.inputSampleRate;
    }
    else
    {
        outSampleRate = send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_MASTER ?
            send_list->sends[0].output_voice->master.inputSampleRate :
            send_list->sends[0].output_voice->mix.inputSampleRate;
    }
    newResampleSamples = (uint32_t) ForgeAudio_ceil(
        voice->audio->updateSize *
        (double) outSampleRate /
        (double) voice->audio->master->master.inputSampleRate
    );
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
    {
        if (    (voice->src.resampleSamples != 0) &&
            (newResampleSamples != voice->src.resampleSamples) &&
            (voice->effects.count > 0)    )
        {
            LOG_FUNC_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
        voice->src.resampleSamples = newResampleSamples;
    }
    else /* (voice->type == FORGE_AUDIO_VOICE_SUBMIX) */
    {
        if (    (voice->mix.outputSamples != 0) &&
            (newResampleSamples != voice->mix.outputSamples) &&
            (voice->effects.count > 0)    )
        {
            LOG_FUNC_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
        voice->mix.outputSamples = newResampleSamples;

        voice->mix.resampleStep = DOUBLE_TO_FIXED((
            (double) voice->mix.inputSampleRate /
            (double) outSampleRate
        ));

        /* Because we used ceil earlier, there's a chance that
         * downsampling submixes will go past the number of samples
         * available. Sources can do this thanks to padding, but we
         * don't have that luxury for submixes, so unfortunately we
         * just have to undo the ceil and turn it into a floor.
         * -flibit
         */
        resampleSanityCheck = (
            voice->mix.resampleStep * voice->mix.outputSamples
        ) >> FIXED_PRECISION;
        if (resampleSanityCheck > (voice->mix.inputSamples / voice->mix.inputChannels))
        {
            voice->mix.outputSamples -= 1;
        }
    }

    LOG_FUNC_EXIT(voice->audio)
    return 0;
}

const float FORGE_AUDIO_INTERNAL_MATRIX_DEFAULTS[8][8][64] =
{
    #include "matrix_defaults.inl"
};

/* PCM Decoding */

void ForgeAudio_Internal_DecodePCM8(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
) {
    LOG_FUNC_ENTER(voice->audio)
    ForgeAudio_Internal_Convert_U8_To_F32(src, decodeCache, samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

void ForgeAudio_Internal_DecodePCM16(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
) {
    LOG_FUNC_ENTER(voice->audio)
    ForgeAudio_Internal_Convert_S16_To_F32(src, decodeCache, samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

void ForgeAudio_Internal_DecodePCM24(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
) {
    const uint8_t *buf = src;
    LOG_FUNC_ENTER(voice->audio)

    /* FIXME: Uh... is this something that can be SIMD-ified? */
    for (uint32_t i = 0; i < samples; i += 1, buf += voice->src.format->block_align)
    for (uint32_t j = 0; j < voice->src.format->channels; j += 1)
    {
        *decodeCache++ = ((int32_t) (
            ((uint32_t) buf[(j * 3) + 2] << 24) |
            ((uint32_t) buf[(j * 3) + 1] << 16) |
            ((uint32_t) buf[(j * 3) + 0] << 8)
        ) >> 8) / 8388607.0f;
    }

    LOG_FUNC_EXIT(voice->audio)
}

void ForgeAudio_Internal_DecodePCM32(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
) {
    LOG_FUNC_ENTER(voice->audio)
    ForgeAudio_Internal_Convert_S32_To_F32(src, decodeCache, samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

void ForgeAudio_Internal_DecodePCM32F(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
) {
    LOG_FUNC_ENTER(voice->audio)
    ForgeAudio_memcpy(decodeCache, src, sizeof(float) * samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

/* Fallback WMA decoder, get ready for spam! */

void ForgeAudio_Internal_DecodeWMAERROR(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
) {
    LOG_FUNC_ENTER(voice->audio)
    LOG_ERROR(voice->audio, "%s", "WMA IS NOT SUPPORTED IN THIS BUILD!")
    ForgeAudio_zero(decodeCache, samples * voice->src.format->channels * sizeof(float));
    LOG_FUNC_EXIT(voice->audio)
}
