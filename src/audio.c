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
#include "format_internal.h"
#include "batch_internal.h"
#include "simd_internal.h"

#ifdef FORGE_AUDIO_DUMP_VOICES
#include "dump_internal.h"
#endif /* FORGE_AUDIO_DUMP_VOICES */

#ifdef FORGE_AUDIO_DUMP_VOICES
static void dump_voice_init(const ForgeSourceVoice *voice);
static void dump_voice_finalize(const ForgeSourceVoice *voice);
static void dump_voice_write_buffer(const ForgeSourceVoice *voice, const ForgeBuffer *buffer, const uint32_t size);
#endif /* FORGE_AUDIO_DUMP_VOICES */

#define FA_AUTOMATION_DEFAULT_TARGET_FRAMES 128

static uint8_t validate_pcm_or_float_format(ForgeAudioEngine *audio, const ForgeAudioFormat *format) {
    const ForgeAudioFormat *base = format;
    uint8_t isPCM = 0;
    uint8_t isFloat = 0;
    uint32_t expectedBlockAlign;

    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        const ForgeAudioFormatExtensible *ext = (const ForgeAudioFormatExtensible *)format;

        if (fa_format_id_equals(ext->format_id, fa_format_id_pcm)) {
            isPCM = 1;
        } else if (fa_format_id_equals(ext->format_id, fa_format_id_ieee_float)) {
            isFloat = 1;
        } else {
            LOG_ERROR(audio, "%s", "Unsupported extensible source format identifier");
            return 0;
        }
    } else if (format->format_tag == FORGE_AUDIO_FORMAT_PCM) {
        isPCM = 1;
    } else if (format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
        isFloat = 1;
    } else {
        LOG_ERROR(audio, "Unsupported source format tag: %u", format->format_tag);
        return 0;
    }

    if (base->channels == 0 || base->block_align == 0 || base->bits_per_sample % 8 != 0) {
        LOG_ERROR(audio, "%s", "Invalid PCM source format block alignment");
        return 0;
    }

    if (isPCM && base->bits_per_sample != 8 && base->bits_per_sample != 16 && base->bits_per_sample != 24 &&
        base->bits_per_sample != 32) {
        LOG_ERROR(audio, "Unsupported PCM bit depth: %u", base->bits_per_sample);
        return 0;
    }

    if (isFloat && base->bits_per_sample != 32) {
        LOG_ERROR(audio, "Unsupported float PCM bit depth: %u", base->bits_per_sample);
        return 0;
    }

    expectedBlockAlign = base->channels * (base->bits_per_sample / 8);
    if (base->block_align != expectedBlockAlign) {
        LOG_ERROR(audio, "Invalid PCM block alignment: got %u, expected %u", base->block_align, expectedBlockAlign)
        return 0;
    }

    return 1;
}

static uint8_t is_unsupported_compressed_source_format(const ForgeAudioFormat *format) {
    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        const ForgeAudioFormatExtensible *ext = (const ForgeAudioFormatExtensible *)format;

        return fa_format_id_equals(ext->format_id, fa_format_id_wmaudio2) ||
               fa_format_id_equals(ext->format_id, fa_format_id_wmaudio3) ||
               fa_format_id_equals(ext->format_id, fa_format_id_wmaudio_lossless) ||
               fa_format_id_equals(ext->format_id, fa_format_id_xmaudio2);
    }

    return format->format_tag == FA_AUDIO_FORMAT_WMAUDIO2 ||
           format->format_tag == FA_AUDIO_FORMAT_WMAUDIO3 ||
           format->format_tag == FA_AUDIO_FORMAT_WMAUDIO_LOSSLESS ||
           format->format_tag == FA_AUDIO_FORMAT_XMAUDIO2;
}

static uint32_t send_list_output_rate(ForgeAudioEngine *audio, const ForgeSendList *send_list) {
    ForgeVoice *out;

    if (send_list == NULL || send_list->send_count == 0) {
        return audio->master->master.inputSampleRate;
    }

    out = send_list->sends[0].output_voice;
    return (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputSampleRate : out->mix.inputSampleRate;
}

static ForgeResult validate_voice_output_send_list(const ForgeSendList *send_list) {
    uint32_t sendRate, nextRate;

    if (send_list == NULL || send_list->send_count <= 1) {
        return ForgeResultSuccess;
    }

    if (send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_SOURCE) {
        return ForgeResultInvalidCall;
    }

    sendRate = (send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_MASTER)
                   ? send_list->sends[0].output_voice->master.inputSampleRate
                   : send_list->sends[0].output_voice->mix.inputSampleRate;
    for (uint32_t i = 0; i < send_list->send_count; i += 1) {
        nextRate = (send_list->sends[i].output_voice->type == FORGE_AUDIO_VOICE_MASTER)
                       ? send_list->sends[i].output_voice->master.inputSampleRate
                       : send_list->sends[i].output_voice->mix.inputSampleRate;
        if (nextRate != sendRate) {
            return ForgeResultInvalidCall;
        }
    }

    return ForgeResultSuccess;
}

static uint32_t source_voice_output_rate(const ForgeSourceVoice *voice) {
    ForgeVoice *out;

    if (voice->sends.send_count == 0) {
        return voice->audio->master->master.inputSampleRate;
    }

    out = voice->sends.sends[0].send.output_voice;
    return (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputSampleRate : out->mix.inputSampleRate;
}

static uint32_t voice_filter_sample_rate(const ForgeVoice *voice) {
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
        return source_voice_output_rate(voice);
    }
    if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
        if (voice->sends.send_count == 0) {
            return voice->audio->master->master.inputSampleRate;
        }
        return voice->sends.sends[0].send.output_voice->type == FORGE_AUDIO_VOICE_MASTER
                   ? voice->sends.sends[0].send.output_voice->master.inputSampleRate
                   : voice->sends.sends[0].send.output_voice->mix.inputSampleRate;
    }
    return voice->master.inputSampleRate;
}

static uint32_t output_filter_sample_rate(const ForgeVoiceSendRuntime *send) {
    ForgeVoice *out = send->send.output_voice;

    return (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputSampleRate : out->mix.inputSampleRate;
}

static ForgeResult voice_set_effect_chain_with_sample_rate(ForgeVoice *voice, const ForgeEffectChain *effect_chain,
                                                           uint32_t effect_sample_rate);

static float filter_max_cutoff_hz(uint32_t sample_rate) {
    /* The current Chamberlin state-variable filter is stable through coefficient
     * 1.0, which corresponds to 2 * sin(pi * cutoff / sample_rate) == 1.0,
     * or cutoff == sample_rate / 6. This is intentionally internal policy; the
     * public API exposes the discoverable range instead of the coefficient.
     */
    return (float)sample_rate / 6.0f;
}

static void filter_get_cutoff_range(uint32_t sample_rate, float *min_cutoff_hz, float *max_cutoff_hz) {
    if (min_cutoff_hz != NULL) {
        *min_cutoff_hz = 0.0f;
    }
    if (max_cutoff_hz != NULL) {
        *max_cutoff_hz = filter_max_cutoff_hz(sample_rate);
    }
}

static float filter_coefficient_from_cutoff(float cutoff_hz, uint32_t sample_rate) {
    const double pi = 3.14159265358979323846264338327950288;

    if (sample_rate == 0) {
        return 0.0f;
    }
    return 2.0f * forge_sinf((float)(pi * (double)cutoff_hz / (double)sample_rate));
}

static ForgeFilterTarget clamp_filter_target(const ForgeFilterTarget *target, uint32_t sample_rate) {
    ForgeFilterTarget result;

    forge_zero(&result, sizeof(result));
    result.field_mask = target->field_mask;
    if (target->field_mask & FORGE_FILTER_TARGET_CUTOFF_HZ) {
        result.cutoff_hz = forge_clamp(target->cutoff_hz, 0.0f, filter_max_cutoff_hz(sample_rate));
    }
    if (target->field_mask & FORGE_FILTER_TARGET_Q) {
        result.q = forge_clamp(target->q, FORGE_AUDIO_MIN_FILTER_Q, FORGE_AUDIO_MAX_FILTER_Q);
    }
    if (target->field_mask & FORGE_FILTER_TARGET_WET_DRY_MIX) {
        result.wet_dry_mix = forge_clamp(target->wet_dry_mix, 0.0f, 1.0f);
    }
    return result;
}

static void clear_filter_runtime_automation(ForgeFilterRuntime *filter) {
    filter->automation.cutoff_hz.active = 0;
    filter->automation.cutoff_hz.remainingFrames = 0;
    filter->automation.q.active = 0;
    filter->automation.q.remainingFrames = 0;
    filter->automation.wet_dry_mix.active = 0;
    filter->automation.wet_dry_mix.remainingFrames = 0;
}

static void filter_runtime_refresh_dsp(ForgeFilterRuntime *filter) {
    filter->frequency = filter_coefficient_from_cutoff(filter->cutoff_hz, filter->sample_rate);
    filter->one_over_q = 1.0f / filter->q;
}

static void filter_runtime_set_sample_rate(ForgeFilterRuntime *filter, uint32_t sample_rate) {
    ForgeFilterTarget target;

    filter->sample_rate = sample_rate;
    target.field_mask = FORGE_FILTER_TARGET_ALL;
    target.cutoff_hz = filter->cutoff_hz;
    target.q = filter->q;
    target.wet_dry_mix = filter->wet_dry_mix;
    target = clamp_filter_target(&target, sample_rate);
    filter->cutoff_hz = target.cutoff_hz;
    filter->q = target.q;
    filter->wet_dry_mix = target.wet_dry_mix;
    filter_runtime_refresh_dsp(filter);
}

static void filter_runtime_init(ForgeFilterRuntime *filter, uint32_t sample_rate) {
    ForgeFilterTarget target;

    forge_zero(filter, sizeof(*filter));
    filter->type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    filter->sample_rate = sample_rate;
    target.field_mask = FORGE_FILTER_TARGET_ALL;
    target.cutoff_hz = FORGE_AUDIO_DEFAULT_FILTER_CUTOFF_HZ;
    target.q = FORGE_AUDIO_DEFAULT_FILTER_Q;
    target.wet_dry_mix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    target = clamp_filter_target(&target, sample_rate);
    filter->cutoff_hz = target.cutoff_hz;
    filter->q = target.q;
    filter->wet_dry_mix = target.wet_dry_mix;
    filter_runtime_refresh_dsp(filter);
}

static void filter_runtime_set_parameters(ForgeFilterRuntime *filter, const ForgeFilterParameters *parameters) {
    ForgeFilterTarget target;

    filter->type = parameters->type;
    target.field_mask = FORGE_FILTER_TARGET_ALL;
    target.cutoff_hz = parameters->cutoff_hz;
    target.q = parameters->q;
    target.wet_dry_mix = parameters->wet_dry_mix;
    target = clamp_filter_target(&target, filter->sample_rate);
    filter->cutoff_hz = target.cutoff_hz;
    filter->q = target.q;
    filter->wet_dry_mix = target.wet_dry_mix;
    clear_filter_runtime_automation(filter);
    filter_runtime_refresh_dsp(filter);
}

static void filter_runtime_get_parameters(const ForgeFilterRuntime *filter, ForgeFilterParameters *parameters) {
    parameters->type = filter->type;
    parameters->cutoff_hz = filter->cutoff_hz;
    parameters->q = filter->q;
    parameters->wet_dry_mix = filter->wet_dry_mix;
}

static void filter_runtime_set_field_automation(ForgeFilterFieldAutomation *automation, float current, float target,
                                                uint32_t duration_frames) {
    if (duration_frames == 0) {
        automation->active = 0;
        automation->remainingFrames = 0;
        return;
    }

    automation->target = target;
    automation->step = (target - current) / (float)duration_frames;
    automation->remainingFrames = duration_frames;
    automation->active = 1;
}

static void filter_runtime_set_type(ForgeFilterRuntime *filter, ForgeFilterType type) {
    filter->type = type;
}

static void filter_runtime_install_ramp(ForgeFilterRuntime *filter, const ForgeFilterTarget *target,
                                        uint32_t duration_frames) {
    ForgeFilterTarget clamped = clamp_filter_target(target, filter->sample_rate);

    if (duration_frames == 0) {
        if (clamped.field_mask & FORGE_FILTER_TARGET_CUTOFF_HZ) {
            filter->cutoff_hz = clamped.cutoff_hz;
            filter->automation.cutoff_hz.active = 0;
            filter->automation.cutoff_hz.remainingFrames = 0;
        }
        if (clamped.field_mask & FORGE_FILTER_TARGET_Q) {
            filter->q = clamped.q;
            filter->automation.q.active = 0;
            filter->automation.q.remainingFrames = 0;
        }
        if (clamped.field_mask & FORGE_FILTER_TARGET_WET_DRY_MIX) {
            filter->wet_dry_mix = clamped.wet_dry_mix;
            filter->automation.wet_dry_mix.active = 0;
            filter->automation.wet_dry_mix.remainingFrames = 0;
        }
        filter_runtime_refresh_dsp(filter);
        return;
    }

    if (clamped.field_mask & FORGE_FILTER_TARGET_CUTOFF_HZ) {
        filter_runtime_set_field_automation(&filter->automation.cutoff_hz, filter->cutoff_hz,
                                            clamped.cutoff_hz, duration_frames);
    }
    if (clamped.field_mask & FORGE_FILTER_TARGET_Q) {
        filter_runtime_set_field_automation(&filter->automation.q, filter->q, clamped.q, duration_frames);
    }
    if (clamped.field_mask & FORGE_FILTER_TARGET_WET_DRY_MIX) {
        filter_runtime_set_field_automation(&filter->automation.wet_dry_mix, filter->wet_dry_mix,
                                            clamped.wet_dry_mix, duration_frames);
    }
}

static void free_voice_send_runtime(ForgeVoice *voice) {
    if (voice->sends.sends == NULL) {
        forge_zero(&voice->sends, sizeof(voice->sends));
        return;
    }

    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        voice->audio->free_func(voice->sends.sends[i].sendCoefficients);
        voice->audio->free_func(voice->sends.sends[i].mixCoefficients);
        if (voice->sends.sends[i].matrixAutomation.target != NULL) {
            voice->audio->free_func(voice->sends.sends[i].matrixAutomation.target);
        }
        if (voice->sends.sends[i].matrixAutomation.step != NULL) {
            voice->audio->free_func(voice->sends.sends[i].matrixAutomation.step);
        }
        if (voice->sends.sends[i].filterState != NULL) {
            voice->audio->free_func(voice->sends.sends[i].filterState);
        }
    }
    if (voice->sends.sends != NULL) {
        voice->audio->free_func(voice->sends.sends);
    }
    forge_zero(&voice->sends, sizeof(voice->sends));
}

static void release_failed_create_effect_chain(ForgeVoice *voice) {
    if (voice->effects.count == 0 || voice->effects.desc == NULL) {
        return;
    }

    for (uint32_t i = 0; i < voice->effects.count; i += 1) {
        voice->effects.desc[i].effect->unlock_for_process(voice->effects.desc[i].effect);
    }

    voice->audio->free_func(voice->effects.desc);
    voice->audio->free_func(voice->effects.parameters);
    voice->audio->free_func(voice->effects.parameterSizes);
    voice->audio->free_func(voice->effects.parameterUpdates);
    voice->audio->free_func(voice->effects.inPlaceProcessing);
    forge_zero(&voice->effects, sizeof(voice->effects));
}

static void cleanup_failed_unlinked_voice(ForgeVoice **failed_voice) {
    ForgeVoice *voice = *failed_voice;

    if (voice == NULL) {
        return;
    }

    free_voice_send_runtime(voice);
    release_failed_create_effect_chain(voice);

    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
        voice->audio->free_func(voice->src.queued_buffers);
        voice->audio->free_func(voice->src.flush_buffers);
        if (voice->src.format != NULL) {
            voice->audio->free_func(voice->src.format);
        }
        if (voice->src.bufferLock != NULL) {
            LOG_MUTEX_DESTROY(voice->audio, voice->src.bufferLock)
            fa_platform_destroy_mutex(voice->src.bufferLock);
        }
    } else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
        if (voice->mix.inputCache != NULL) {
            voice->audio->free_func(voice->mix.inputCache);
        }
        if (voice->mix.resampleInputCache != NULL) {
            voice->audio->free_func(voice->mix.resampleInputCache);
        }
        if (voice->mix.resampleHistory != NULL) {
            voice->audio->free_func(voice->mix.resampleHistory);
        }
    } else if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        if (voice->master.effectCache != NULL) {
            voice->audio->free_func(voice->master.effectCache);
        }
        if (voice->audio->master == voice) {
            voice->audio->master = NULL;
        }
    }

    if (voice->sendLock != NULL) {
        LOG_MUTEX_DESTROY(voice->audio, voice->sendLock)
        fa_platform_destroy_mutex(voice->sendLock);
    }
    if (voice->effectLock != NULL) {
        LOG_MUTEX_DESTROY(voice->audio, voice->effectLock)
        fa_platform_destroy_mutex(voice->effectLock);
    }
    if (voice->filterLock != NULL) {
        LOG_MUTEX_DESTROY(voice->audio, voice->filterLock)
        fa_platform_destroy_mutex(voice->filterLock);
    }
    if (voice->filterState != NULL) {
        voice->audio->free_func(voice->filterState);
    }
    if (voice->channelVolume != NULL) {
        voice->audio->free_func(voice->channelVolume);
    }
    if (voice->channelVolumeAutomation.target != NULL) {
        voice->audio->free_func(voice->channelVolumeAutomation.target);
    }
    if (voice->channelVolumeAutomation.step != NULL) {
        voice->audio->free_func(voice->channelVolumeAutomation.step);
    }
    if (voice->volumeLock != NULL) {
        LOG_MUTEX_DESTROY(voice->audio, voice->volumeLock)
        fa_platform_destroy_mutex(voice->volumeLock);
    }

    voice->audio->free_func(voice);
    *failed_voice = NULL;
}

static ForgeResult allocate_default_channel_volumes(ForgeVoice *voice) {
    voice->volume = 1.0f;
    voice->channelVolume = (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels);
    voice->channelVolumeAutomation.target =
        (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels);
    voice->channelVolumeAutomation.step =
        (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels);

    if (voice->channelVolume == NULL || voice->channelVolumeAutomation.target == NULL ||
        voice->channelVolumeAutomation.step == NULL) {
        return ForgeResultOutOfMemory;
    }

    for (uint32_t i = 0; i < voice->outputChannels; i += 1) {
        voice->channelVolume[i] = 1.0f;
        voice->channelVolumeAutomation.target[i] = 1.0f;
        voice->channelVolumeAutomation.step[i] = 0.0f;
    }

    return ForgeResultSuccess;
}

static uint32_t source_decode_frame_count(uint32_t resample_samples, float max_frequency_ratio,
                                                   uint32_t source_sample_rate, uint32_t output_sample_rate) {
    uint64_t max_step;
    uint64_t decode_samples;

    forge_assert(output_sample_rate != 0);

    max_step = DOUBLE_TO_FIXED((double)max_frequency_ratio * (double)source_sample_rate / (double)output_sample_rate);
    decode_samples =
        (((uint64_t)resample_samples * max_step) + FIXED_FRACTION_MASK + FIXED_FRACTION_MASK) >> FIXED_PRECISION;
    forge_assert(decode_samples <= UINT32_MAX);

    return (uint32_t)decode_samples;
}

static ForgeAudioDecodeCallback decoder_for_pcm_bits(uint16_t bits_per_sample) {
    switch (bits_per_sample) {
        case 8:
            return fa_decode_pcm8;
        case 16:
            return fa_decode_pcm16;
        case 24:
            return fa_decode_pcm24;
        case 32:
            return fa_decode_pcm32;
        default:
            forge_assert(0 && "Unsupported validated PCM bit depth!");
            return fa_decode_pcm16;
    }
}

#ifdef FORGE_AUDIO_TESTING
uint32_t forge_audio_test_source_decode_frame_count(uint32_t resample_samples, float max_frequency_ratio,
                                                    uint32_t source_sample_rate, uint32_t output_sample_rate) {
    return source_decode_frame_count(resample_samples, max_frequency_ratio, source_sample_rate,
                                              output_sample_rate);
}
#endif

/* ForgeAudio Version */

uint32_t forge_audio_linked_version(void) {
    return FORGE_AUDIO_COMPILED_VERSION;
}

/* ForgeAudio Interface */

ForgeResult forge_audio_create(ForgeAudioEngine **engine, uint32_t flags) {
    return forge_audio_create_with_allocator(engine, flags, forge_malloc, forge_free, forge_realloc);
}

static ForgeResult engine_construct_with_allocator(ForgeAudioEngine **engine, ForgeMallocFunc custom_malloc,
                                                   ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc);
static ForgeResult engine_construct_offline_with_allocator(ForgeAudioEngine **engine, ForgeMallocFunc custom_malloc,
                                                           ForgeFreeFunc custom_free,
                                                           ForgeReallocFunc custom_realloc);

static ForgeResult engine_initialize(ForgeAudioEngine *audio, uint32_t flags);
static void engine_destroy(ForgeAudioEngine *audio, uint8_t release_platform);

ForgeResult forge_audio_create_with_allocator(ForgeAudioEngine **engine, uint32_t flags, ForgeMallocFunc custom_malloc,
                                              ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    ForgeResult result = engine_construct_with_allocator(engine, custom_malloc, custom_free, custom_realloc);
    if (result != ForgeResultSuccess) {
        return result;
    }
    result = engine_initialize(*engine, flags);
    if (result != ForgeResultSuccess) {
        engine_destroy(*engine, 1);
        *engine = NULL;
        return result;
    }
    return ForgeResultSuccess;
}

static ForgeResult engine_construct_with_allocator(ForgeAudioEngine **engine, ForgeMallocFunc custom_malloc,
                                                   ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    ForgeResult result;

    /* Normal engines hold the global platform lifetime until engine_destroy(). */
    fa_platform_add_ref();
    result = engine_construct_offline_with_allocator(engine, custom_malloc, custom_free, custom_realloc);
    if (result != ForgeResultSuccess) {
        fa_platform_release();
    } else {
        (*engine)->platformLifetimeHeld = 1;
    }
    return result;
}

static ForgeResult engine_construct_offline_with_allocator(ForgeAudioEngine **engine, ForgeMallocFunc custom_malloc,
                                                           ForgeFreeFunc custom_free,
                                                           ForgeReallocFunc custom_realloc) {
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    ForgeDebugConfiguration debugInit = {0};
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    *engine = (ForgeAudioEngine *)custom_malloc(sizeof(ForgeAudioEngine));
    if (*engine == NULL) {
        return ForgeResultOutOfMemory;
    }
    forge_zero(*engine, sizeof(ForgeAudioEngine));
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    forge_audio_set_debug_configuration(*engine, &debugInit, NULL);
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    (*engine)->sourceLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->sourceLock)
    (*engine)->submixLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->submixLock)
    (*engine)->callbackLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->callbackLock)
    (*engine)->batchLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->batchLock)
    (*engine)->malloc_func = custom_malloc;
    (*engine)->free_func = custom_free;
    (*engine)->realloc_func = custom_realloc;
    return ForgeResultSuccess;
}

static void destroy_voice(ForgeVoice *voice);

void forge_audio_destroy(ForgeAudioEngine *audio) {
    engine_destroy(audio, 1);
}

#ifdef FORGE_AUDIO_TESTING
ForgeResult forge_audio_test_create_offline_engine(ForgeAudioEngine **engine) {
    ForgeResult result = engine_construct_offline_with_allocator(engine, forge_malloc, forge_free, forge_realloc);
    if (result != ForgeResultSuccess) {
        return result;
    }
    /* Offline test engines intentionally do not hold the global platform lifetime or a platform device. */
    result = engine_initialize(*engine, 0);
    if (result != ForgeResultSuccess) {
        engine_destroy(*engine, 0);
        *engine = NULL;
        return result;
    }
    return ForgeResultSuccess;
}

ForgeResult forge_audio_test_create_offline_engine_with_allocator(ForgeAudioEngine **engine,
                                                                  ForgeMallocFunc custom_malloc,
                                                                  ForgeFreeFunc custom_free,
                                                                  ForgeReallocFunc custom_realloc) {
    ForgeResult result = engine_construct_offline_with_allocator(engine, custom_malloc, custom_free, custom_realloc);
    if (result != ForgeResultSuccess) {
        return result;
    }
    result = engine_initialize(*engine, 0);
    if (result != ForgeResultSuccess) {
        engine_destroy(*engine, 0);
        *engine = NULL;
        return result;
    }
    return ForgeResultSuccess;
}

void forge_audio_test_destroy_offline_engine(ForgeAudioEngine *audio) {
    engine_destroy(audio, 0);
}
#endif

static void engine_destroy(ForgeAudioEngine *audio, uint8_t release_platform) {
    ForgeVoice *voice;
    uint8_t release_platform_lifetime;

    LOG_API_ENTER(audio)
    forge_assert((release_platform != 0) == (audio->platformLifetimeHeld != 0));
    release_platform_lifetime = release_platform && audio->platformLifetimeHeld;

    while (audio->sources) {
        voice = (ForgeSourceVoice *)audio->sources->entry;
        destroy_voice(voice);
    }
    while (audio->submixes) {
        voice = (ForgeSourceVoice *)audio->submixes->entry;
        destroy_voice(voice);
    }
    if (audio->master)
        destroy_voice(audio->master);
    fa_batch_clear_all(audio);
    forge_audio_stop_engine(audio);
    audio->free_func(audio->decodeCache);
    audio->free_func(audio->resampleCache);
    audio->free_func(audio->effectChainCache);
    audio->free_func(audio->effectChainCache2);
    LOG_MUTEX_DESTROY(audio, audio->sourceLock)
    fa_platform_destroy_mutex(audio->sourceLock);
    LOG_MUTEX_DESTROY(audio, audio->submixLock)
    fa_platform_destroy_mutex(audio->submixLock);
    LOG_MUTEX_DESTROY(audio, audio->callbackLock)
    fa_platform_destroy_mutex(audio->callbackLock);
    LOG_MUTEX_DESTROY(audio, audio->batchLock)
    fa_platform_destroy_mutex(audio->batchLock);
    audio->free_func(audio);
    if (release_platform_lifetime) {
        fa_platform_release();
    }
}

ForgeResult forge_audio_get_device_count(ForgeAudioEngine *audio, uint32_t *count) {
    LOG_API_ENTER(audio)
    *count = fa_platform_get_device_count();
    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_get_device_details(ForgeAudioEngine *audio, uint32_t index,
                                           ForgeDeviceDetails *device_details) {
    uint32_t result;
    LOG_API_ENTER(audio)
    result = fa_platform_get_device_details(index, device_details);
    LOG_API_EXIT(audio)
    return result;
}

static ForgeResult engine_initialize(ForgeAudioEngine *audio, uint32_t flags) {
    LOG_API_ENTER(audio)
    forge_assert((flags & ~(FORGE_AUDIO_DEBUG_ENGINE | FORGE_AUDIO_1024_QUANTUM)) == 0);

    audio->initFlags = flags;

    /* Seed shared decode/resample caches; they grow on demand before use. */
    audio->decodeCache = (float *)audio->malloc_func(sizeof(float));
    audio->resampleCache = (float *)audio->malloc_func(sizeof(float));
    if (audio->decodeCache == NULL || audio->resampleCache == NULL) {
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    audio->decodeSamples = 1;
    audio->resampleSamples = 1;

    forge_audio_start_engine(audio);
    LOG_API_EXIT(audio)
    return ForgeResultSuccess;
}

ForgeResult forge_audio_register_callback(ForgeAudioEngine *audio, ForgeEngineCallback *callback) {
    LOG_API_ENTER(audio)
    if (!fa_linked_list_add_entry(&audio->callbacks, callback, audio->callbackLock, audio->malloc_func)) {
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    LOG_API_EXIT(audio)
    return ForgeResultSuccess;
}

void forge_audio_unregister_callback(ForgeAudioEngine *audio, ForgeEngineCallback *callback) {
    LOG_API_ENTER(audio)
    fa_linked_list_remove_entry(&audio->callbacks, callback, audio->callbackLock, audio->free_func);
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_create_source_voice(ForgeAudioEngine *audio, ForgeSourceVoice **source_voice,
                                            const ForgeAudioFormat *source_format, uint32_t flags,
                                            float max_frequency_ratio, ForgeVoiceCallback *callback,
                                            const ForgeSendList *send_list, const ForgeEffectChain *effect_chain) {
    ForgeResult result;
    uint32_t outputRate;

    LOG_API_ENTER(audio)
    LOG_FORMAT(audio, source_format)

    if ((send_list == NULL || send_list->send_count == 0) && audio->master == NULL) {
        LOG_ERROR(audio, "%s", "CreateSourceVoice called before mastering voice was initialized");
        return ForgeResultInvalidCall;
    }

    if (is_unsupported_compressed_source_format(source_format)) {
        LOG_ERROR(audio, "%s", "Compressed source voices are not supported");
        return ForgeResultUnsupportedFormat;
    }

    if (!validate_pcm_or_float_format(audio, source_format)) {
        return ForgeResultInvalidCall;
    }

    result = validate_voice_output_send_list(send_list);
    if (result != ForgeResultSuccess) {
        return result;
    }

    *source_voice = (ForgeSourceVoice *)audio->malloc_func(sizeof(ForgeVoice));
    if (*source_voice == NULL) {
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    forge_zero(*source_voice, sizeof(ForgeSourceVoice));
    (*source_voice)->audio = audio;
    (*source_voice)->type = FORGE_AUDIO_VOICE_SOURCE;
    (*source_voice)->flags = flags;
    (*source_voice)->sendLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->sendLock)
    (*source_voice)->effectLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->effectLock)
    (*source_voice)->filterLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->filterLock)
    (*source_voice)->volumeLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->volumeLock)

    /* Source Properties */
    forge_assert(max_frequency_ratio <= FORGE_AUDIO_MAX_FREQ_RATIO);
    (*source_voice)->src.maxFreqRatio = max_frequency_ratio;

    if (source_format->format_tag == FORGE_AUDIO_FORMAT_PCM ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
        ForgeAudioFormatExtensible *fmtex =
            (ForgeAudioFormatExtensible *)audio->malloc_func(sizeof(ForgeAudioFormatExtensible));
        if (fmtex == NULL) {
            cleanup_failed_unlinked_voice(source_voice);
            LOG_API_EXIT(audio)
            return ForgeResultOutOfMemory;
        }
        /* convert PCM to EXTENSIBLE */
        fmtex->format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        fmtex->format.channels = source_format->channels;
        fmtex->format.sample_rate = source_format->sample_rate;
        fmtex->format.average_bytes_per_second = source_format->average_bytes_per_second;
        fmtex->format.block_align = source_format->block_align;
        fmtex->format.bits_per_sample = source_format->bits_per_sample;
        fmtex->format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        fmtex->samples.valid_bits_per_sample = source_format->bits_per_sample;
        fmtex->channel_mask = 0;
        if (source_format->format_tag == FORGE_AUDIO_FORMAT_PCM) {
            forge_memcpy(fmtex->format_id, fa_format_id_pcm, FORGE_AUDIO_FORMAT_ID_SIZE);
        } else if (source_format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
            forge_memcpy(fmtex->format_id, fa_format_id_ieee_float, FORGE_AUDIO_FORMAT_ID_SIZE);
        }
        (*source_voice)->src.format = &fmtex->format;
    } else {
        /* direct copy anything else */
        (*source_voice)->src.format =
            (ForgeAudioFormat *)audio->malloc_func(sizeof(ForgeAudioFormat) + source_format->extra_size);
        if ((*source_voice)->src.format == NULL) {
            cleanup_failed_unlinked_voice(source_voice);
            LOG_API_EXIT(audio)
            return ForgeResultOutOfMemory;
        }
        forge_memcpy((*source_voice)->src.format, source_format, sizeof(ForgeAudioFormat) + source_format->extra_size);
    }

    (*source_voice)->src.callback = callback;
    (*source_voice)->src.active = 0;
    (*source_voice)->src.freqRatio = 1.0f;
    (*source_voice)->src.totalSamples = 0;
    (*source_voice)->src.bufferLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->src.bufferLock)

    if ((*source_voice)->src.format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        ForgeAudioFormatExtensible *fmtex = (ForgeAudioFormatExtensible *)(*source_voice)->src.format;

#define COMPARE_FORMAT_ID(type) fa_format_id_equals(fmtex->format_id, fa_format_id_##type)
        if (COMPARE_FORMAT_ID(pcm)) {
            (*source_voice)->src.decode = decoder_for_pcm_bits(fmtex->format.bits_per_sample);
        } else if (COMPARE_FORMAT_ID(ieee_float)) {
            (*source_voice)->src.decode = fa_decode_pcm32f;
        } else {
            forge_assert(0 && "Unsupported extensible audio format identifier!");
        }
#undef COMPARE_FORMAT_ID
    } else {
        forge_assert(0 && "Unsupported format tag!");
    }

    if ((*source_voice)->src.format->channels == 1) {
        (*source_voice)->src.resample = fa_resample_mono;
    } else if ((*source_voice)->src.format->channels == 2) {
        (*source_voice)->src.resample = fa_resample_stereo;
    } else {
        (*source_voice)->src.resample = fa_resample_generic;
    }

    (*source_voice)->src.curBufferOffset = 0;

    /* Sends/Effects */
    outputRate = send_list_output_rate(audio, send_list);
    fa_audio_voice_output_frequency(*source_voice, send_list);
    filter_runtime_init(&(*source_voice)->filter, outputRate);
    result = voice_set_effect_chain_with_sample_rate(*source_voice, effect_chain, outputRate);
    if (result != 0) {
        cleanup_failed_unlinked_voice(source_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    /* Default Levels */
    result = allocate_default_channel_volumes(*source_voice);
    if (result != ForgeResultSuccess) {
        cleanup_failed_unlinked_voice(source_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    result = forge_voice_set_outputs(*source_voice, send_list);
    if (result != ForgeResultSuccess) {
        cleanup_failed_unlinked_voice(source_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    /* Filters */
    if (flags & FORGE_AUDIO_VOICE_USEFILTER) {
        (*source_voice)->filterState = (ForgeAudioFilterState *)audio->malloc_func(
            sizeof(ForgeAudioFilterState) * (*source_voice)->src.format->channels);
        if ((*source_voice)->filterState == NULL) {
            cleanup_failed_unlinked_voice(source_voice);
            LOG_API_EXIT(audio)
            return ForgeResultOutOfMemory;
        }
        forge_zero((*source_voice)->filterState, sizeof(ForgeAudioFilterState) * (*source_voice)->src.format->channels);
    }

    /* Sample Storage */
    (*source_voice)->src.decodeSamples =
        source_decode_frame_count((*source_voice)->src.resampleSamples, max_frequency_ratio,
                                           (*source_voice)->src.format->sample_rate, outputRate);
    if (!fa_audio_resize_decode_cache(audio, ((*source_voice)->src.decodeSamples + EXTRA_DECODE_PADDING) *
                                                 (*source_voice)->src.format->channels)) {
        cleanup_failed_unlinked_voice(source_voice);
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }

    LOG_INFO(audio, "-> %p", (void *)(*source_voice))

    LOG_INFO(audio, "-> %p", (void *)(*source_voice))

    /* Add to list, finally. */
    if (!fa_linked_list_prepend_entry(&audio->sources, *source_voice, audio->sourceLock, audio->malloc_func)) {
        cleanup_failed_unlinked_voice(source_voice);
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }

#ifdef FORGE_AUDIO_DUMP_VOICES
    dump_voice_init(*source_voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_create_submix_voice(ForgeAudioEngine *audio, ForgeSubmixVoice **submix_voice,
                                            uint32_t input_channels, uint32_t input_sample_rate, uint32_t flags,
                                            uint32_t processing_stage, const ForgeSendList *send_list,
                                            const ForgeEffectChain *effect_chain) {
    ForgeResult result;
    uint32_t outputRate;

    LOG_API_ENTER(audio)

    if ((send_list == NULL || send_list->send_count == 0) && audio->master == NULL) {
        LOG_ERROR(audio, "%s", "CreateSubmixVoice called before mastering voice was initialized");
        return ForgeResultInvalidCall;
    }

    result = validate_voice_output_send_list(send_list);
    if (result != ForgeResultSuccess) {
        return result;
    }

    *submix_voice = (ForgeSubmixVoice *)audio->malloc_func(sizeof(ForgeVoice));
    if (*submix_voice == NULL) {
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    forge_zero(*submix_voice, sizeof(ForgeSubmixVoice));
    (*submix_voice)->audio = audio;
    (*submix_voice)->type = FORGE_AUDIO_VOICE_SUBMIX;
    (*submix_voice)->flags = flags;
    (*submix_voice)->sendLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->sendLock)
    (*submix_voice)->effectLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->effectLock)
    (*submix_voice)->filterLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->filterLock)
    (*submix_voice)->volumeLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->volumeLock)

    /* Submix Properties */
    (*submix_voice)->mix.inputChannels = input_channels;
    (*submix_voice)->mix.inputSampleRate = input_sample_rate;
    (*submix_voice)->mix.processingStage = processing_stage;

    /* Resampler */
    if (input_channels == 1) {
        (*submix_voice)->mix.resample = fa_resample_mono;
    } else if (input_channels == 2) {
        (*submix_voice)->mix.resample = fa_resample_stereo;
    } else {
        (*submix_voice)->mix.resample = fa_resample_generic;
    }

    /* Sample Storage */
    (*submix_voice)->mix.inputFrames = (uint32_t)forge_ceil(audio->updateSize * (double)input_sample_rate /
                                                            (double)audio->master->master.inputSampleRate);
    (*submix_voice)->mix.inputSamples = ((*submix_voice)->mix.inputFrames +
                                         SUBMIX_RESAMPLE_INPUT_PADDING_FRAMES) *
                                        input_channels;
    (*submix_voice)->mix.inputCache = (float *)audio->malloc_func(sizeof(float) * (*submix_voice)->mix.inputSamples);
    if ((*submix_voice)->mix.inputCache == NULL) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    (*submix_voice)->mix.resampleInputCache = (float *)audio->malloc_func(
        sizeof(float) * ((*submix_voice)->mix.inputFrames + SUBMIX_RESAMPLE_HISTORY_FRAMES +
                         SUBMIX_RESAMPLE_EDGE_FRAMES) *
        input_channels);
    if ((*submix_voice)->mix.resampleInputCache == NULL) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    (*submix_voice)->mix.resampleHistory = (float *)audio->malloc_func(sizeof(float) * input_channels);
    if ((*submix_voice)->mix.resampleHistory == NULL) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    forge_zero(/* Zero this now, for the first update */
               (*submix_voice)->mix.inputCache, sizeof(float) * (*submix_voice)->mix.inputSamples);
    forge_zero((*submix_voice)->mix.resampleInputCache,
               sizeof(float) * ((*submix_voice)->mix.inputFrames + SUBMIX_RESAMPLE_HISTORY_FRAMES +
                                SUBMIX_RESAMPLE_EDGE_FRAMES) *
                   input_channels);
    forge_zero((*submix_voice)->mix.resampleHistory, sizeof(float) * input_channels);

    /* Sends/Effects */
    outputRate = send_list_output_rate(audio, send_list);
    fa_audio_voice_output_frequency(*submix_voice, send_list);
    filter_runtime_init(&(*submix_voice)->filter, outputRate);
    result = voice_set_effect_chain_with_sample_rate(*submix_voice, effect_chain, outputRate);
    if (result != 0) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    /* Default Levels */
    result = allocate_default_channel_volumes(*submix_voice);
    if (result != ForgeResultSuccess) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    result = forge_voice_set_outputs(*submix_voice, send_list);
    if (result != ForgeResultSuccess) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    /* Filters */
    if (flags & FORGE_AUDIO_VOICE_USEFILTER) {
        (*submix_voice)->filterState =
            (ForgeAudioFilterState *)audio->malloc_func(sizeof(ForgeAudioFilterState) * input_channels);
        if ((*submix_voice)->filterState == NULL) {
            cleanup_failed_unlinked_voice(submix_voice);
            LOG_API_EXIT(audio)
            return ForgeResultOutOfMemory;
        }
        forge_zero((*submix_voice)->filterState, sizeof(ForgeAudioFilterState) * input_channels);
    }

    /* Add to list, finally. */
    if (!fa_audio_insert_submix_sorted(&audio->submixes, *submix_voice, audio->submixLock, audio->malloc_func)) {
        cleanup_failed_unlinked_voice(submix_voice);
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }

    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_create_master_voice(ForgeAudioEngine *audio, ForgeMasterVoice **mastering_voice,
                                            uint32_t input_channels, uint32_t input_sample_rate, uint32_t flags,
                                            uint32_t device_index, const ForgeEffectChain *effect_chain) {
    ForgeResult result;

    LOG_API_ENTER(audio)

    /* For now we only support one allocated master voice at a time */
    forge_assert(audio->master == NULL);

    if (!audio->platformLifetimeHeld) {
        LOG_API_EXIT(audio)
        return ForgeResultInvalidCall;
    }

    if (input_channels == FORGE_AUDIO_DEFAULT_CHANNELS || input_sample_rate == FORGE_AUDIO_DEFAULT_SAMPLERATE) {
        ForgeDeviceDetails details;
        if (forge_audio_get_device_details(audio, device_index, &details) != 0) {
            return ForgeResultInvalidCall;
        }
        if (input_channels == FORGE_AUDIO_DEFAULT_CHANNELS) {
            input_channels = details.output_format.format.channels;
        }
        if (input_sample_rate == FORGE_AUDIO_DEFAULT_SAMPLERATE) {
            input_sample_rate = details.output_format.format.sample_rate;
        }
    }

    *mastering_voice = (ForgeMasterVoice *)audio->malloc_func(sizeof(ForgeVoice));
    if (*mastering_voice == NULL) {
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    forge_zero(*mastering_voice, sizeof(ForgeMasterVoice));
    (*mastering_voice)->audio = audio;
    (*mastering_voice)->type = FORGE_AUDIO_VOICE_MASTER;
    (*mastering_voice)->flags = flags;
    (*mastering_voice)->effectLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->effectLock)
    (*mastering_voice)->volumeLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->volumeLock)

    /* Default Levels */
    (*mastering_voice)->volume = 1.0f;

    /* Master Properties */
    (*mastering_voice)->master.inputChannels = input_channels;
    (*mastering_voice)->master.inputSampleRate = input_sample_rate;

    /* Sends/Effects */
    forge_zero(&(*mastering_voice)->sends, sizeof((*mastering_voice)->sends));
    result = forge_voice_set_effect_chain(*mastering_voice, effect_chain);
    if (result != 0) {
        cleanup_failed_unlinked_voice(mastering_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    /* This is now safe enough to assign */
    audio->master = *mastering_voice;

    /* Build the device format.
     * The most unintuitive part of this is the use of outputChannels
     * instead of master.inputChannels. Bizarrely, the effect chain can
     * dictate the _actual_ output channel count, and when the channel count
     * mismatches, we have to add a staging buffer for effects to process on
     * before ultimately copying the final result to the device. ARGH.
     */
    fa_format_write_extensible(&audio->mixFormat, audio->master->outputChannels, audio->master->master.inputSampleRate,
                               fa_format_id_ieee_float);

    /* Platform Device */
    fa_platform_init(audio, audio->initFlags, device_index, &audio->mixFormat, &audio->updateSize, &audio->platform);
    if (audio->platform == NULL) {
        forge_voice_destroy(*mastering_voice);
        *mastering_voice = NULL;

        /* Not the best code, but it's probably true? */
        return ForgeResultDeviceInvalidated;
    }
    audio->master->outputChannels = audio->mixFormat.format.channels;
    audio->master->master.inputSampleRate = audio->mixFormat.format.sample_rate;

    /* Effect Chain Cache */
    if ((*mastering_voice)->master.inputChannels != (*mastering_voice)->outputChannels) {
        (*mastering_voice)->master.effectCache =
            (float *)audio->malloc_func(sizeof(float) * audio->updateSize * (*mastering_voice)->master.inputChannels);
        if ((*mastering_voice)->master.effectCache == NULL) {
            forge_voice_destroy(*mastering_voice);
            *mastering_voice = NULL;
            LOG_API_EXIT(audio)
            return ForgeResultOutOfMemory;
        }
    }

    LOG_API_EXIT(audio)
    return 0;
}

#ifdef FORGE_AUDIO_TESTING
ForgeResult forge_audio_test_create_virtual_master_voice(ForgeAudioEngine *audio, ForgeMasterVoice **mastering_voice,
                                                         uint32_t input_channels, uint32_t input_sample_rate,
                                                         uint32_t update_size,
                                                         const ForgeEffectChain *effect_chain) {
    ForgeResult result;

    if (audio == NULL || mastering_voice == NULL) {
        return ForgeResultInvalidArgument;
    }

    LOG_API_ENTER(audio)

    if (audio->master != NULL || input_channels == 0 || input_sample_rate == 0 || update_size == 0) {
        LOG_API_EXIT(audio)
        return ForgeResultInvalidArgument;
    }

    *mastering_voice = (ForgeMasterVoice *)audio->malloc_func(sizeof(ForgeVoice));
    if (*mastering_voice == NULL) {
        LOG_API_EXIT(audio)
        return ForgeResultOutOfMemory;
    }
    forge_zero(*mastering_voice, sizeof(ForgeMasterVoice));
    (*mastering_voice)->audio = audio;
    (*mastering_voice)->type = FORGE_AUDIO_VOICE_MASTER;
    (*mastering_voice)->effectLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->effectLock)
    (*mastering_voice)->volumeLock = fa_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->volumeLock)
    (*mastering_voice)->volume = 1.0f;
    (*mastering_voice)->master.inputChannels = input_channels;
    (*mastering_voice)->master.inputSampleRate = input_sample_rate;

    audio->updateSize = update_size;

    result = forge_voice_set_effect_chain(*mastering_voice, effect_chain);
    if (result != 0) {
        cleanup_failed_unlinked_voice(mastering_voice);
        LOG_API_EXIT(audio)
        return result;
    }

    audio->master = *mastering_voice;

    fa_format_write_extensible(&audio->mixFormat, audio->master->outputChannels,
                               audio->master->master.inputSampleRate, fa_format_id_ieee_float);
    audio->master->outputChannels = audio->mixFormat.format.channels;
    audio->master->master.inputSampleRate = audio->mixFormat.format.sample_rate;

    if ((*mastering_voice)->master.inputChannels != (*mastering_voice)->outputChannels) {
        (*mastering_voice)->master.effectCache =
            (float *)audio->malloc_func(sizeof(float) * audio->updateSize * (*mastering_voice)->master.inputChannels);
        if ((*mastering_voice)->master.effectCache == NULL) {
            cleanup_failed_unlinked_voice(mastering_voice);
            LOG_API_EXIT(audio)
            return ForgeResultOutOfMemory;
        }
    }

    LOG_API_EXIT(audio)
    return 0;
}
#endif

void forge_audio_set_engine_procedure(ForgeAudioEngine *audio, ForgeEngineProcedure client_engine_proc, void *user) {
    LOG_API_ENTER(audio)
    audio->client_engine_proc = client_engine_proc;
    audio->clientEngineUser = user;
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_start_engine(ForgeAudioEngine *audio) {
    LOG_API_ENTER(audio)
    audio->active = 1;
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_stop_engine(ForgeAudioEngine *audio) {
    LOG_API_ENTER(audio)
    audio->active = 0;
    fa_batch_apply_all(audio);
    fa_batch_execute(audio);
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_apply_batch(ForgeAudioEngine *audio, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(audio)
    if (batch_id == FORGE_AUDIO_BATCH_IMMEDIATE) {
        LOG_API_EXIT(audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        fa_batch_apply_all(audio);
    } else {
        fa_batch_apply(audio, batch_id);
    }
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_get_performance_data(ForgeAudioEngine *audio, ForgePerformanceData *perf_data) {
    ForgeLinkedList *list;
    ForgeSourceVoice *source;

    LOG_API_ENTER(audio)

    forge_zero(perf_data, sizeof(ForgePerformanceData));

    fa_platform_lock_mutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    list = audio->sources;
    while (list != NULL) {
        source = (ForgeSourceVoice *)list->entry;
        perf_data->total_source_voice_count += 1;
        if (source->src.active) {
            perf_data->active_source_voice_count += 1;
        }
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)

    fa_platform_lock_mutex(audio->submixLock);
    LOG_MUTEX_LOCK(audio, audio->submixLock)
    list = audio->submixes;
    while (list != NULL) {
        perf_data->active_submix_voice_count += 1;
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->submixLock);
    LOG_MUTEX_UNLOCK(audio, audio->submixLock)

    if (audio->master != NULL) {
        /* estimate, should use real latency from platform */
        perf_data->current_latency_in_samples = 2 * audio->updateSize;
    }

    LOG_API_EXIT(audio)
}

void forge_audio_set_debug_configuration(ForgeAudioEngine *audio, ForgeDebugConfiguration *debug_configuration,
                                         void *reserved) {
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    const char *env;

    LOG_API_ENTER(audio)

    forge_memcpy(&audio->debug, debug_configuration, sizeof(ForgeDebugConfiguration));

    env = forge_getenv("FORGE_AUDIO_LOG_EVERYTHING");
    if (env != NULL && *env == '1') {
        audio->debug.trace_mask =
            (FORGE_AUDIO_LOG_ERRORS | FORGE_AUDIO_LOG_WARNINGS | FORGE_AUDIO_LOG_INFO | FORGE_AUDIO_LOG_DETAIL |
             FORGE_AUDIO_LOG_API_CALLS | FORGE_AUDIO_LOG_FUNC_CALLS | FORGE_AUDIO_LOG_TIMING | FORGE_AUDIO_LOG_LOCKS |
             FORGE_AUDIO_LOG_MEMORY | FORGE_AUDIO_LOG_STREAMING);
        audio->debug.log_thread_id = 1;
        audio->debug.log_function_name = 1;
        audio->debug.log_timing = 1;
    }

#define CHECK_ENV(type)                                                                                                \
    env = forge_getenv("FORGE_AUDIO_LOG_" #type);                                                                      \
    if (env != NULL) {                                                                                                 \
        if (*env == '1') {                                                                                             \
            audio->debug.trace_mask |= FORGE_AUDIO_LOG_##type;                                                         \
        } else {                                                                                                       \
            audio->debug.trace_mask &= ~FORGE_AUDIO_LOG_##type;                                                        \
        }                                                                                                              \
    }
    CHECK_ENV(ERRORS)
    CHECK_ENV(WARNINGS)
    CHECK_ENV(INFO)
    CHECK_ENV(DETAIL)
    CHECK_ENV(API_CALLS)
    CHECK_ENV(FUNC_CALLS)
    CHECK_ENV(TIMING)
    CHECK_ENV(LOCKS)
    CHECK_ENV(MEMORY)
    CHECK_ENV(STREAMING)
#undef CHECK_ENV
#define CHECK_ENV(envvar, field)                                                                                       \
    env = forge_getenv("FORGE_AUDIO_LOG_LOG" #envvar);                                                                 \
    if (env != NULL) {                                                                                                 \
        audio->debug.field = (*env == '1');                                                                            \
    }
    CHECK_ENV(THREADID, log_thread_id)
    CHECK_ENV(FILELINE, log_fileline)
    CHECK_ENV(FUNCTIONNAME, log_function_name)
    CHECK_ENV(TIMING, log_timing)
#undef CHECK_ENV

    LOG_API_EXIT(audio)
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
}

void forge_audio_get_processing_quantum(ForgeAudioEngine *audio, uint32_t *quantumNumerator,
                                        uint32_t *quantumDenominator) {
    forge_assert(audio->master != NULL);
    if (quantumNumerator != NULL) {
        *quantumNumerator = audio->updateSize;
    }
    if (quantumDenominator != NULL) {
        *quantumDenominator = audio->master->master.inputSampleRate;
    }
}

static ForgeResult ms_to_frames_with_rate(uint32_t sample_rate, double duration_ms, uint32_t *frames) {
    double frame_count;
    double rounded_frames;

    if (frames == NULL || sample_rate == 0) {
        return ForgeResultInvalidCall;
    }

    if (!(duration_ms >= 0.0)) {
        return ForgeResultInvalidCall;
    }

    if (duration_ms == 0.0) {
        *frames = 0;
        return ForgeResultSuccess;
    }

    frame_count = (duration_ms * (double)sample_rate) / 1000.0;
    if (!(frame_count <= (double)UINT32_MAX + 0.499999999)) {
        return ForgeResultInvalidCall;
    }

    rounded_frames = forge_floor(frame_count + 0.5);
    if (rounded_frames < 1.0) {
        rounded_frames = 1.0;
    }
    if (!(rounded_frames <= (double)UINT32_MAX)) {
        return ForgeResultInvalidCall;
    }

    *frames = (uint32_t)rounded_frames;
    return ForgeResultSuccess;
}

ForgeResult forge_audio_ms_to_frames(ForgeAudioEngine *audio, double duration_ms, uint32_t *frames) {
    if (audio == NULL || audio->master == NULL) {
        return ForgeResultInvalidCall;
    }

    return ms_to_frames_with_rate(audio->master->master.inputSampleRate, duration_ms, frames);
}

/* ForgeVoice Interface */

void fa_voice_recalc_mix_matrix(ForgeVoice *voice, uint32_t sendIndex) {
    uint32_t oChan, s, d;
    ForgeVoiceSendRuntime *send = &voice->sends.sends[sendIndex];
    ForgeVoice *out = send->send.output_voice;
    float *matrix = send->mixCoefficients;

    if (out->type == FORGE_AUDIO_VOICE_MASTER) {
        oChan = out->master.inputChannels;
    } else {
        oChan = out->mix.inputChannels;
    }

    for (d = 0; d < oChan; d += 1) {
        for (s = 0; s < voice->outputChannels; s += 1) {
            matrix[d * voice->outputChannels + s] = send->sendCoefficients[d * voice->outputChannels + s];
        }
    }
}

void forge_voice_get_details(ForgeVoice *voice, ForgeVoiceDetails *voice_details) {
    LOG_API_ENTER(voice->audio)

    voice_details->creation_flags = voice->flags;
    voice_details->active_flags = voice->flags;
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
        voice_details->input_channels = voice->src.format->channels;
        voice_details->input_sample_rate = voice->src.format->sample_rate;
    } else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
        voice_details->input_channels = voice->mix.inputChannels;
        voice_details->input_sample_rate = voice->mix.inputSampleRate;
    } else if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        voice_details->input_channels = voice->master.inputChannels;
        voice_details->input_sample_rate = voice->master.inputSampleRate;
    } else {
        forge_assert(0 && "Unknown voice type!");
    }

    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_outputs(ForgeVoice *voice, const ForgeSendList *send_list) {
    uint32_t outChannels;
    uint32_t sourceDecodeSamples = 0;
    ForgeSendList defaultSends;
    ForgeSend defaultSend;

    LOG_API_ENTER(voice->audio)

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (validate_voice_output_send_list(send_list) != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (voice->type == FORGE_AUDIO_VOICE_SOURCE && voice->src.decodeSamples != 0) {
        uint32_t outputRate = send_list_output_rate(voice->audio, send_list);
        uint32_t sourceResampleSamples = (uint32_t)forge_ceil((double)voice->audio->updateSize * (double)outputRate /
                                                              (double)voice->audio->master->master.inputSampleRate);

        sourceDecodeSamples = source_decode_frame_count(sourceResampleSamples, voice->src.maxFreqRatio,
                                                                 voice->src.format->sample_rate, outputRate);
        if (!fa_audio_resize_decode_cache(voice->audio,
                                          (sourceDecodeSamples + EXTRA_DECODE_PADDING) * voice->src.format->channels)) {
            LOG_API_EXIT(voice->audio)
            return ForgeResultOutOfMemory;
        }
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (fa_audio_voice_output_frequency(voice, send_list) != 0) {
        LOG_ERROR(voice->audio, "%s", "Changing the sample rate while an effect chain is attached is invalid!")
        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE && sourceDecodeSamples != 0) {
        voice->src.decodeSamples = sourceDecodeSamples;
    }
    filter_runtime_set_sample_rate(&voice->filter, send_list_output_rate(voice->audio, send_list));

    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    /* Rebuild send-dependent matrices, mixers, and filter state for the new output list. */
    free_voice_send_runtime(voice);

    if (send_list == NULL) {
        /* Default to the mastering voice as output */
        defaultSend.flags = 0;
        defaultSend.output_voice = voice->audio->master;
        defaultSends.send_count = 1;
        defaultSends.sends = &defaultSend;
        send_list = &defaultSends;
    } else if (send_list->send_count == 0) {
        /* No sends? Nothing to do... */
        fa_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Copy send list and allocate/reset default output matrix, mixer function, filters */
    voice->sends.sends =
        (ForgeVoiceSendRuntime *)voice->audio->malloc_func(send_list->send_count * sizeof(ForgeVoiceSendRuntime));
    if (voice->sends.sends == NULL) {
        fa_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultOutOfMemory;
    }
    voice->sends.send_count = send_list->send_count;
    forge_zero(voice->sends.sends, send_list->send_count * sizeof(ForgeVoiceSendRuntime));

    for (uint32_t i = 0; i < send_list->send_count; i += 1) {
        ForgeVoiceSendRuntime *send = &voice->sends.sends[i];

        send->send = send_list->sends[i];
        if (send->send.output_voice->type == FORGE_AUDIO_VOICE_MASTER) {
            outChannels = send->send.output_voice->master.inputChannels;
        } else {
            outChannels = send->send.output_voice->mix.inputChannels;
        }
        send->sendCoefficients =
            (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels * outChannels);
        send->mixCoefficients =
            (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels * outChannels);
        send->matrixAutomation.target =
            (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels * outChannels);
        send->matrixAutomation.step =
            (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels * outChannels);

        if (send->sendCoefficients == NULL || send->mixCoefficients == NULL ||
            send->matrixAutomation.target == NULL || send->matrixAutomation.step == NULL) {
            free_voice_send_runtime(voice);
            fa_platform_unlock_mutex(voice->volumeLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
            fa_platform_unlock_mutex(voice->sendLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
            LOG_API_EXIT(voice->audio)
            return ForgeResultOutOfMemory;
        }

        forge_assert(voice->outputChannels > 0 && voice->outputChannels < 9);
        forge_assert(outChannels > 0 && outChannels < 9);
        forge_memcpy(send->sendCoefficients, fa_audio_matrix_defaults[voice->outputChannels - 1][outChannels - 1],
                     voice->outputChannels * outChannels * sizeof(float));
        forge_memcpy(send->matrixAutomation.target, send->sendCoefficients,
                     voice->outputChannels * outChannels * sizeof(float));
        forge_zero(send->matrixAutomation.step, voice->outputChannels * outChannels * sizeof(float));
        fa_voice_recalc_mix_matrix(voice, i);

        if (voice->outputChannels == 1) {
            if (outChannels == 1) {
                send->mix = fa_mix_1in_1out_scalar;
            } else if (outChannels == 2) {
                send->mix = fa_mix_1in_2out_scalar;
            } else if (outChannels == 6) {
                send->mix = fa_mix_1in_6out_scalar;
            } else if (outChannels == 8) {
                send->mix = fa_mix_1in_8out_scalar;
            } else {
                send->mix = fa_mix_generic;
            }
        } else if (voice->outputChannels == 2) {
            if (outChannels == 1) {
                send->mix = fa_mix_2in_1out_scalar;
            } else if (outChannels == 2) {
                send->mix = fa_mix_2in_2out_scalar;
            } else if (outChannels == 6) {
                send->mix = fa_mix_2in_6out_scalar;
            } else if (outChannels == 8) {
                send->mix = fa_mix_2in_8out_scalar;
            } else {
                send->mix = fa_mix_generic;
            }
        } else {
            send->mix = fa_mix_generic;
        }

        if (send->send.flags & FORGE_AUDIO_SEND_USEFILTER) {
            filter_runtime_init(&send->filter, output_filter_sample_rate(send));
            send->filterState =
                (ForgeAudioFilterState *)voice->audio->malloc_func(sizeof(ForgeAudioFilterState) * outChannels);
            if (send->filterState == NULL) {
                free_voice_send_runtime(voice);
                fa_platform_unlock_mutex(voice->volumeLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
                fa_platform_unlock_mutex(voice->sendLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
                LOG_API_EXIT(voice->audio)
                return ForgeResultOutOfMemory;
            }
            forge_zero(send->filterState, sizeof(ForgeAudioFilterState) * outChannels);
        }
    }

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

static ForgeResult voice_set_effect_chain_with_sample_rate(ForgeVoice *voice, const ForgeEffectChain *effect_chain,
                                                           uint32_t effect_sample_rate) {
    ForgeEffect *effect;
    ForgeResult result;
    uint32_t lockedEffects;
    uint32_t channelCount;
    ForgeVoiceDetails voiceDetails;
    ForgeEffectInfo info;
    ForgeAudioFormatExtensible srcFmt, dstFmt;
    ForgeEffectLockBuffer srcLockParams, dstLockParams;
    uint8_t hasEffectChain;

    forge_voice_get_details(voice, &voiceDetails);
    hasEffectChain = (effect_chain != NULL && effect_chain->effect_count > 0);

    /* SetEffectChain must not change the number of output channels once the voice has been created */
    if (!hasEffectChain && voice->outputChannels != 0) {
        /* cannot remove an effect chain that changes the number of channels */
        if (voice->outputChannels != voiceDetails.input_channels) {
            LOG_ERROR(voice->audio, "%s", "Cannot remove effect chain that changes the number of channels")
            forge_assert(0 && "Cannot remove effect chain that changes the number of channels");
            return ForgeResultInvalidCall;
        }
    }

    if (hasEffectChain && voice->outputChannels != 0) {
        uint32_t lst = effect_chain->effect_count - 1;

        /* new effect chain must have same number of output channels */
        if (voice->outputChannels != effect_chain->effects[lst].output_channels) {
            LOG_ERROR(voice->audio, "%s", "New effect chain must have same number of output channels as the old chain")
            forge_assert(0 && "New effect chain must have same number of output channels as the old chain");
            return ForgeResultInvalidCall;
        }
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)

    if (!hasEffectChain) {
        fa_audio_free_effect_chain(voice);
        forge_zero(&voice->effects, sizeof(voice->effects));
        voice->outputChannels = voiceDetails.input_channels;
    } else {
        /* Validate incoming chain before changing the current chain */

        /* These are always the same, so just write them now. */
        srcLockParams.format = &srcFmt.format;
        dstLockParams.format = &dstFmt.format;
        if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
            srcLockParams.max_frame_count = voice->src.resampleSamples;
            dstLockParams.max_frame_count = voice->src.resampleSamples;
        } else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
            srcLockParams.max_frame_count = voice->mix.outputSamples;
            dstLockParams.max_frame_count = voice->mix.outputSamples;
        } else if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
            srcLockParams.max_frame_count = voice->audio->updateSize;
            dstLockParams.max_frame_count = voice->audio->updateSize;
        }

        /* The effect chain processes post-resample PCM32F buffers. */
        srcFmt.format.bits_per_sample = 32;
        srcFmt.format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        srcFmt.format.channels = voiceDetails.input_channels;
        srcFmt.format.sample_rate = effect_sample_rate;
        srcFmt.format.block_align = srcFmt.format.channels * (srcFmt.format.bits_per_sample / 8);
        srcFmt.format.average_bytes_per_second = srcFmt.format.sample_rate * srcFmt.format.block_align;
        srcFmt.format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        srcFmt.samples.valid_bits_per_sample = srcFmt.format.bits_per_sample;
        srcFmt.channel_mask = 0;
        forge_memcpy(srcFmt.format_id, fa_format_id_ieee_float, FORGE_AUDIO_FORMAT_ID_SIZE);
        forge_memcpy(&dstFmt, &srcFmt, sizeof(srcFmt));

        lockedEffects = 0;
        for (uint32_t i = 0; i < effect_chain->effect_count; i += 1) {
            effect = effect_chain->effects[i].effect;

            /* ... then we get this effect's format... */
            dstFmt.format.channels = effect_chain->effects[i].output_channels;
            dstFmt.format.block_align = dstFmt.format.channels * (dstFmt.format.bits_per_sample / 8);
            dstFmt.format.average_bytes_per_second = dstFmt.format.sample_rate * dstFmt.format.block_align;

            result = effect->lock_for_process(effect, 1, &srcLockParams, 1, &dstLockParams);
            if (result != 0) {
                for (uint32_t j = 0; j < lockedEffects; j += 1) {
                    effect_chain->effects[j].effect->unlock_for_process(effect_chain->effects[j].effect);
                }
                LOG_ERROR(voice->audio, "%s", "Effect output format not supported")
                forge_assert(0 && "Effect output format not supported");
                fa_platform_unlock_mutex(voice->effectLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
                return result;
            }
            lockedEffects += 1;

            /* Okay, now this effect is the source and the next
             * effect will be the destination. Repeat until no
             * effects left.
             */
            forge_memcpy(&srcFmt, &dstFmt, sizeof(srcFmt));
        }

        {
            ForgeEffectChainRuntime oldEffects = voice->effects;
            uint32_t oldOutputChannels = voice->outputChannels;

            forge_zero(&voice->effects, sizeof(voice->effects));
            result = fa_audio_alloc_effect_chain(voice, effect_chain);
            if (result != ForgeResultSuccess) {
                voice->effects = oldEffects;
                voice->outputChannels = oldOutputChannels;
                for (uint32_t j = 0; j < lockedEffects; j += 1) {
                    effect_chain->effects[j].effect->unlock_for_process(effect_chain->effects[j].effect);
                }
                fa_platform_unlock_mutex(voice->effectLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
                return result;
            }

            {
                ForgeEffectChainRuntime newEffects = voice->effects;

                voice->effects = oldEffects;
                fa_audio_free_effect_chain(voice);
                voice->effects = newEffects;
            }
        }

        /* check if in-place processing is supported */
        channelCount = voiceDetails.input_channels;
        for (uint32_t i = 0; i < voice->effects.count; i += 1) {
            effect = voice->effects.desc[i].effect;
            effect->get_info(effect, &info);
            voice->effects.inPlaceProcessing[i] =
                (info.flags & FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED) == FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED;
            voice->effects.inPlaceProcessing[i] &= (channelCount == voice->effects.desc[i].output_channels);
            channelCount = voice->effects.desc[i].output_channels;

            /* Fails if in-place processing is mandatory and
             * the chain forces us to do otherwise...
             */
            forge_assert(!(info.flags & FORGE_EFFECT_FLAG_IN_PLACE_REQUIRED) || voice->effects.inPlaceProcessing[i]);
        }
        voice->outputChannels = channelCount;
    }

    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    return 0;
}

ForgeResult forge_voice_set_effect_chain(ForgeVoice *voice, const ForgeEffectChain *effect_chain) {
    ForgeResult result;

    LOG_API_ENTER(voice->audio)
    result = voice_set_effect_chain_with_sample_rate(voice, effect_chain, voice_filter_sample_rate(voice));
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_enable_effect(ForgeVoice *voice, uint32_t effect_index, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_enable_effect(voice, effect_index, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[effect_index].initial_state = 1;
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_disable_effect(ForgeVoice *voice, uint32_t effect_index, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_disable_effect(voice, effect_index, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[effect_index].initial_state = 0;
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_effect_state(ForgeVoice *voice, uint32_t effect_index, int32_t *enabled) {
    LOG_API_ENTER(voice->audio)
    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    *enabled = voice->effects.desc[effect_index].initial_state;
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_effect_parameters(ForgeVoice *voice, uint32_t effect_index, const void *parameters,
                                              uint32_t parameters_byte_size, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL || parameters == NULL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (voice->audio->active) {
        fa_batch_queue_set_effect_parameters(voice, effect_index, parameters, parameters_byte_size, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeResult result = fa_voice_install_set_effect_parameters(voice, effect_index, parameters, parameters_byte_size);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult fa_voice_install_set_effect_parameters(ForgeVoice *voice, uint32_t effect_index, const void *parameters,
                                                   uint32_t parameters_byte_size) {
    if (parameters == NULL || voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        return ForgeResultInvalidCall;
    }

    if (voice->effects.parameters == NULL) {
        LOG_ERROR(voice->audio, "Setting effect parameters on voice with no effect chain: %p", (void *)voice);
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)

    if (voice->effects.parameters[effect_index] == NULL ||
        voice->effects.parameterSizes[effect_index] < parameters_byte_size) {
        voice->effects.parameters[effect_index] =
            voice->audio->realloc_func(voice->effects.parameters[effect_index], parameters_byte_size);
        if (voice->effects.parameters[effect_index] == NULL) {
            fa_platform_unlock_mutex(voice->effectLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
            return ForgeResultOutOfMemory;
        }
    }

    voice->effects.parameterSizes[effect_index] = parameters_byte_size;
    forge_memcpy(voice->effects.parameters[effect_index], parameters, parameters_byte_size);
    voice->effects.parameterUpdates[effect_index] = 0;
    voice->effects.desc[effect_index].effect->set_parameters(voice->effects.desc[effect_index].effect, parameters,
                                                             parameters_byte_size);
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    return 0;
}

ForgeResult forge_voice_get_effect_parameters(ForgeVoice *voice, uint32_t effect_index, void *parameters,
                                              uint32_t parameters_byte_size) {
    ForgeEffect *effect;
    LOG_API_ENTER(voice->audio)
    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    effect = voice->effects.desc[effect_index].effect;
    effect->get_parameters(effect, parameters, parameters_byte_size);
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

static ForgeResult validate_reverb_target_arg(const ForgeReverbTarget *target) {
    if (target == NULL) {
        return ForgeResultInvalidCall;
    }
    if (target->field_mask == 0 || (target->field_mask & ~FORGE_REVERB_TARGET_ALL) != 0) {
        return ForgeResultInvalidArgument;
    }
    return ForgeResultSuccess;
}

static ForgeResult validate_reverb_effect_slot(ForgeVoice *voice, uint32_t effect_index, uint8_t allow_7point1,
                                               ForgeEffect **effect) {
    ForgeEffect *slot_effect;
    ForgeResult result = ForgeResultSuccess;

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        slot_effect = voice->effects.desc[effect_index].effect;
        if (slot_effect == NULL || slot_effect->set_reverb_target == NULL) {
            result = ForgeResultInvalidCall;
        } else if (slot_effect->kind != ForgeEffectKindReverb &&
                   (!allow_7point1 || slot_effect->kind != ForgeEffectKindReverb7Point1)) {
            result = ForgeResultInvalidCall;
        } else if (effect != NULL) {
            *effect = slot_effect;
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    return result;
}

ForgeResult fa_voice_install_ramp_reverb_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                    const ForgeReverbTarget *target, uint32_t duration_frames) {
    ForgeEffect *effect;
    ForgeResult result = validate_reverb_target_arg(target);

    if (result != ForgeResultSuccess) {
        return result;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->set_reverb_target == NULL ||
            (effect->kind != ForgeEffectKindReverb && effect->kind != ForgeEffectKindReverb7Point1)) {
            result = ForgeResultInvalidCall;
        } else {
            result = effect->set_reverb_target(effect, target, duration_frames);
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    return result;
}

static ForgeResult queue_or_install_ramp_reverb_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                           const ForgeReverbTarget *target,
                                                           uint32_t duration_frames,
                                                           ForgeAudioBatchId batch_id) {
    ForgeResult result = validate_reverb_target_arg(target);

    if (result != ForgeResultSuccess) {
        return result;
    }
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }
    result = validate_reverb_effect_slot(voice, effect_index, 1, NULL);
    if (result != ForgeResultSuccess) {
        return result;
    }
    if (voice->audio->active) {
        fa_batch_queue_ramp_reverb_parameters(voice, effect_index, target, duration_frames, batch_id);
        return ForgeResultSuccess;
    }
    return fa_voice_install_ramp_reverb_parameters(voice, effect_index, target, duration_frames);
}

ForgeResult forge_voice_set_reverb_parameters_target(ForgeVoice *voice, uint32_t effect_index,
                                                     const ForgeReverbTarget *target,
                                                     ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_reverb_parameters(voice, effect_index, target,
                                                     FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_reverb_parameters_frames(ForgeVoice *voice, uint32_t effect_index,
                                                      const ForgeReverbTarget *target,
                                                      uint32_t duration_frames,
                                                      ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_reverb_parameters(voice, effect_index, target, duration_frames, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_reverb_parameters_ms(ForgeVoice *voice, uint32_t effect_index,
                                                  const ForgeReverbTarget *target, double duration_ms,
                                                  ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;

    LOG_API_ENTER(voice->audio)
    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_reverb_parameters(voice, effect_index, target, duration_frames, batch_id);
    }
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_get_reverb_parameters(ForgeVoice *voice, uint32_t effect_index,
                                              ForgeReverbParameters *parameters) {
    ForgeEffect *effect;
    ForgeResult result = ForgeResultSuccess;

    if (parameters == NULL) {
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->kind != ForgeEffectKindReverb) {
            result = ForgeResultInvalidCall;
        } else {
            effect->get_parameters(effect, parameters, sizeof(*parameters));
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    return result;
}

ForgeResult forge_voice_get_reverb_7point1_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                      ForgeReverbParameters7Point1 *parameters) {
    ForgeEffect *effect;
    ForgeResult result = ForgeResultSuccess;

    if (parameters == NULL) {
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->kind != ForgeEffectKindReverb7Point1) {
            result = ForgeResultInvalidCall;
        } else {
            effect->get_parameters(effect, parameters, sizeof(*parameters));
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    return result;
}

static ForgeResult validate_delay_target_arg(const ForgeDelayTarget *target) {
    if (target == NULL) {
        return ForgeResultInvalidCall;
    }
    if (target->field_mask == 0 || (target->field_mask & ~FORGE_DELAY_TARGET_ALL) != 0) {
        return ForgeResultInvalidArgument;
    }
    return ForgeResultSuccess;
}

static ForgeResult validate_delay_effect_slot(ForgeVoice *voice, uint32_t effect_index, ForgeEffect **effect) {
    ForgeEffect *slot_effect;
    ForgeResult result = ForgeResultSuccess;

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        slot_effect = voice->effects.desc[effect_index].effect;
        if (slot_effect == NULL || slot_effect->set_delay_target == NULL || slot_effect->kind != ForgeEffectKindDelay) {
            result = ForgeResultInvalidCall;
        } else if (effect != NULL) {
            *effect = slot_effect;
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    return result;
}

ForgeResult fa_voice_install_ramp_delay_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                   const ForgeDelayTarget *target, uint32_t duration_frames) {
    ForgeEffect *effect;
    ForgeResult result = validate_delay_target_arg(target);

    if (result != ForgeResultSuccess) {
        return result;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->set_delay_target == NULL || effect->kind != ForgeEffectKindDelay) {
            result = ForgeResultInvalidCall;
        } else {
            result = effect->set_delay_target(effect, target, duration_frames);
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    return result;
}

static ForgeResult queue_or_install_ramp_delay_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                          const ForgeDelayTarget *target,
                                                          uint32_t duration_frames,
                                                          ForgeAudioBatchId batch_id) {
    ForgeResult result = validate_delay_target_arg(target);

    if (result != ForgeResultSuccess) {
        return result;
    }
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }
    result = validate_delay_effect_slot(voice, effect_index, NULL);
    if (result != ForgeResultSuccess) {
        return result;
    }
    if (voice->audio->active) {
        fa_batch_queue_ramp_delay_parameters(voice, effect_index, target, duration_frames, batch_id);
        return ForgeResultSuccess;
    }
    return fa_voice_install_ramp_delay_parameters(voice, effect_index, target, duration_frames);
}

ForgeResult forge_voice_set_delay_parameters_target(ForgeVoice *voice, uint32_t effect_index,
                                                    const ForgeDelayTarget *target,
                                                    ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_delay_parameters(voice, effect_index, target,
                                                    FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_delay_parameters_frames(ForgeVoice *voice, uint32_t effect_index,
                                                     const ForgeDelayTarget *target,
                                                     uint32_t duration_frames,
                                                     ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_delay_parameters(voice, effect_index, target, duration_frames, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_delay_parameters_ms(ForgeVoice *voice, uint32_t effect_index,
                                                 const ForgeDelayTarget *target, double duration_ms,
                                                 ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;

    LOG_API_ENTER(voice->audio)
    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_delay_parameters(voice, effect_index, target, duration_frames, batch_id);
    }
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_get_delay_parameters(ForgeVoice *voice, uint32_t effect_index,
                                             ForgeDelayParameters *parameters) {
    ForgeEffect *effect;
    ForgeResult result = ForgeResultSuccess;

    if (parameters == NULL) {
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->kind != ForgeEffectKindDelay) {
            result = ForgeResultInvalidCall;
        } else {
            effect->get_parameters(effect, parameters, sizeof(*parameters));
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    return result;
}

static ForgeResult validate_biquad_target_arg(const ForgeBiquadTarget *target) {
    if (target == NULL) {
        return ForgeResultInvalidCall;
    }
    if (target->field_mask == 0 || (target->field_mask & ~FORGE_BIQUAD_TARGET_ALL) != 0) {
        return ForgeResultInvalidArgument;
    }
    return ForgeResultSuccess;
}

static ForgeResult validate_biquad_effect_slot(ForgeVoice *voice, uint32_t effect_index, ForgeEffect **effect) {
    ForgeEffect *slot_effect;
    ForgeResult result = ForgeResultSuccess;

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        slot_effect = voice->effects.desc[effect_index].effect;
        if (slot_effect == NULL || slot_effect->set_biquad_target == NULL ||
            slot_effect->kind != ForgeEffectKindBiquad) {
            result = ForgeResultInvalidCall;
        } else if (effect != NULL) {
            *effect = slot_effect;
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    return result;
}

ForgeResult fa_voice_install_ramp_biquad_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                    const ForgeBiquadTarget *target, uint32_t duration_frames) {
    ForgeEffect *effect;
    ForgeResult result = validate_biquad_target_arg(target);

    if (result != ForgeResultSuccess) {
        return result;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->set_biquad_target == NULL || effect->kind != ForgeEffectKindBiquad) {
            result = ForgeResultInvalidCall;
        } else {
            result = effect->set_biquad_target(effect, target, duration_frames);
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)

    return result;
}

static ForgeResult queue_or_install_ramp_biquad_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                           const ForgeBiquadTarget *target,
                                                           uint32_t duration_frames,
                                                           ForgeAudioBatchId batch_id) {
    ForgeResult result = validate_biquad_target_arg(target);

    if (result != ForgeResultSuccess) {
        return result;
    }
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }
    result = validate_biquad_effect_slot(voice, effect_index, NULL);
    if (result != ForgeResultSuccess) {
        return result;
    }
    if (voice->audio->active) {
        fa_batch_queue_ramp_biquad_parameters(voice, effect_index, target, duration_frames, batch_id);
        return ForgeResultSuccess;
    }
    return fa_voice_install_ramp_biquad_parameters(voice, effect_index, target, duration_frames);
}

ForgeResult forge_voice_set_biquad_parameters_target(ForgeVoice *voice, uint32_t effect_index,
                                                     const ForgeBiquadTarget *target,
                                                     ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_biquad_parameters(voice, effect_index, target,
                                                     FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_biquad_parameters_frames(ForgeVoice *voice, uint32_t effect_index,
                                                      const ForgeBiquadTarget *target,
                                                      uint32_t duration_frames,
                                                      ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_biquad_parameters(voice, effect_index, target, duration_frames, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_biquad_parameters_ms(ForgeVoice *voice, uint32_t effect_index,
                                                  const ForgeBiquadTarget *target, double duration_ms,
                                                  ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;

    LOG_API_ENTER(voice->audio)
    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_biquad_parameters(voice, effect_index, target, duration_frames, batch_id);
    }
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_get_biquad_parameters(ForgeVoice *voice, uint32_t effect_index,
                                              ForgeBiquadParameters *parameters) {
    ForgeEffect *effect;
    ForgeResult result = ForgeResultSuccess;

    if (parameters == NULL) {
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.desc == NULL || effect_index >= voice->effects.count) {
        result = ForgeResultInvalidCall;
    } else {
        effect = voice->effects.desc[effect_index].effect;
        if (effect == NULL || effect->kind != ForgeEffectKindBiquad) {
            result = ForgeResultInvalidCall;
        } else {
            effect->get_parameters(effect, parameters, sizeof(*parameters));
        }
    }
    fa_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    return result;
}

static ForgeResult validate_filter_target_arg(const ForgeFilterTarget *target) {
    if (target == NULL) {
        return ForgeResultInvalidCall;
    }
    if ((target->field_mask & ~FORGE_FILTER_TARGET_ALL) != 0 || target->field_mask == 0) {
        return ForgeResultInvalidArgument;
    }
    return ForgeResultSuccess;
}

static ForgeResult validate_filter_type(ForgeFilterType type) {
    switch (type) {
    case ForgeFilterLowPass:
    case ForgeFilterBandPass:
    case ForgeFilterHighPass:
    case ForgeFilterNotch:
        return ForgeResultSuccess;
    default:
        return ForgeResultInvalidArgument;
    }
}

static ForgeResult validate_filter_parameters_arg(const ForgeFilterParameters *parameters) {
    if (parameters == NULL) {
        return ForgeResultInvalidCall;
    }
    return validate_filter_type(parameters->type);
}

static ForgeResult find_output_filter_locked(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t *send_index) {
    uint32_t i;

    if (destination_voice == NULL && voice->sends.send_count == 1) {
        destination_voice = voice->sends.sends[0].send.output_voice;
    }
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].send.output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        return ForgeResultInvalidCall;
    }
    if (!(voice->sends.sends[i].send.flags & FORGE_AUDIO_SEND_USEFILTER)) {
        return ForgeResultFormatSuggested;
    }
    *send_index = i;
    return ForgeResultSuccess;
}

ForgeResult fa_voice_install_set_filter_parameters(ForgeVoice *voice, const ForgeFilterParameters *parameters) {
    ForgeResult result = validate_filter_parameters_arg(parameters);
    if (result != ForgeResultSuccess) {
        return result;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER || !(voice->flags & FORGE_AUDIO_VOICE_USEFILTER)) {
        return ForgeResultSuccess;
    }

    fa_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    filter_runtime_set_parameters(&voice->filter, parameters);
    fa_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    return ForgeResultSuccess;
}

ForgeResult fa_voice_install_set_filter_type(ForgeVoice *voice, ForgeFilterType type) {
    ForgeResult result = validate_filter_type(type);
    if (result != ForgeResultSuccess) {
        return result;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER || !(voice->flags & FORGE_AUDIO_VOICE_USEFILTER)) {
        return ForgeResultSuccess;
    }

    fa_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    filter_runtime_set_type(&voice->filter, type);
    fa_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    return ForgeResultSuccess;
}

ForgeResult fa_voice_install_ramp_filter(ForgeVoice *voice, const ForgeFilterTarget *target,
                                         uint32_t duration_frames) {
    if (voice->type == FORGE_AUDIO_VOICE_MASTER || !(voice->flags & FORGE_AUDIO_VOICE_USEFILTER)) {
        return ForgeResultSuccess;
    }

    fa_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    filter_runtime_install_ramp(&voice->filter, target, duration_frames);
    fa_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    return ForgeResultSuccess;
}

ForgeResult fa_voice_install_set_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                          const ForgeFilterParameters *parameters) {
    uint32_t i = 0;
    ForgeResult result = validate_filter_parameters_arg(parameters);

    if (result != ForgeResultSuccess) {
        return result;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        return ForgeResultSuccess;
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    result = find_output_filter_locked(voice, destination_voice, &i);
    if (result == ForgeResultSuccess) {
        filter_runtime_set_parameters(&voice->sends.sends[i].filter, parameters);
    } else if (result == ForgeResultFormatSuggested) {
        result = ForgeResultSuccess;
    }
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    return result;
}

ForgeResult fa_voice_install_set_output_filter_type(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                    ForgeFilterType type) {
    uint32_t i = 0;
    ForgeResult result = validate_filter_type(type);

    if (result != ForgeResultSuccess) {
        return result;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        return ForgeResultSuccess;
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    result = find_output_filter_locked(voice, destination_voice, &i);
    if (result == ForgeResultSuccess) {
        filter_runtime_set_type(&voice->sends.sends[i].filter, type);
    } else if (result == ForgeResultFormatSuggested) {
        result = ForgeResultSuccess;
    }
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    return result;
}

ForgeResult fa_voice_install_ramp_output_filter(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                const ForgeFilterTarget *target, uint32_t duration_frames) {
    uint32_t i = 0;
    ForgeResult result;

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        return ForgeResultSuccess;
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    result = find_output_filter_locked(voice, destination_voice, &i);
    if (result == ForgeResultSuccess) {
        filter_runtime_install_ramp(&voice->sends.sends[i].filter, target, duration_frames);
    } else if (result == ForgeResultFormatSuggested) {
        result = ForgeResultSuccess;
    }
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    return result;
}

ForgeResult forge_voice_set_filter_parameters(ForgeVoice *voice, const ForgeFilterParameters *parameters,
                                              ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    ForgeResult result = validate_filter_parameters_arg(parameters);
    if (batch_id == FORGE_AUDIO_BATCH_ALL || result != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return batch_id == FORGE_AUDIO_BATCH_ALL ? ForgeResultInvalidCall : result;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_filter_parameters(voice, parameters, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (voice->audio->active) {
        fa_batch_clear_ready_filter_automation(voice);
    }

    result = fa_voice_install_set_filter_parameters(voice, parameters);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_set_filter_type(ForgeVoice *voice, ForgeFilterType type, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    ForgeResult result = validate_filter_type(type);
    if (batch_id == FORGE_AUDIO_BATCH_ALL || result != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return batch_id == FORGE_AUDIO_BATCH_ALL ? ForgeResultInvalidCall : result;
    }
    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_filter_type(voice, type, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    result = fa_voice_install_set_filter_type(voice, type);
    LOG_API_EXIT(voice->audio)
    return result;
}

static ForgeResult queue_or_install_ramp_filter(ForgeVoice *voice, const ForgeFilterTarget *target,
                                                uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeResult result = validate_filter_target_arg(target);
    if (result != ForgeResultSuccess) {
        return result;
    }
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }
    if (voice->audio->active) {
        fa_batch_queue_ramp_filter(voice, target, duration_frames, batch_id);
        return ForgeResultSuccess;
    }
    return fa_voice_install_ramp_filter(voice, target, duration_frames);
}

ForgeResult forge_voice_set_filter_target(ForgeVoice *voice, const ForgeFilterTarget *target,
                                          ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_filter(voice, target, FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_filter_frames(ForgeVoice *voice, const ForgeFilterTarget *target,
                                           uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_filter(voice, target, duration_frames, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_filter_ms(ForgeVoice *voice, const ForgeFilterTarget *target, double duration_ms,
                                       ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)
    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_filter(voice, target, duration_frames, batch_id);
    }
    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_filter_parameters(ForgeVoice *voice, ForgeFilterParameters *parameters) {
    LOG_API_ENTER(voice->audio)

    if (voice->type == FORGE_AUDIO_VOICE_MASTER || !(voice->flags & FORGE_AUDIO_VOICE_USEFILTER) ||
        parameters == NULL) {
        LOG_API_EXIT(voice->audio)
        return;
    }

    fa_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    filter_runtime_get_parameters(&voice->filter, parameters);
    fa_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_get_filter_cutoff_range(ForgeVoice *voice, float *min_cutoff_hz, float *max_cutoff_hz) {
    if (voice == NULL || voice->type == FORGE_AUDIO_VOICE_MASTER || !(voice->flags & FORGE_AUDIO_VOICE_USEFILTER)) {
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    filter_get_cutoff_range(voice->filter.sample_rate, min_cutoff_hz, max_cutoff_hz);
    fa_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    return ForgeResultSuccess;
}

ForgeResult forge_voice_set_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                     const ForgeFilterParameters *parameters,
                                                     ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    ForgeResult result = validate_filter_parameters_arg(parameters);
    if (batch_id == FORGE_AUDIO_BATCH_ALL || result != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return batch_id == FORGE_AUDIO_BATCH_ALL ? ForgeResultInvalidCall : result;
    }
    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_output_filter_parameters(voice, destination_voice, parameters, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }
    if (voice->audio->active) {
        fa_batch_clear_ready_output_filter_automation(voice, destination_voice);
    }

    result = fa_voice_install_set_output_filter_parameters(voice, destination_voice, parameters);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_set_output_filter_type(ForgeVoice *voice, ForgeVoice *destination_voice,
                                               ForgeFilterType type, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    ForgeResult result = validate_filter_type(type);
    if (batch_id == FORGE_AUDIO_BATCH_ALL || result != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return batch_id == FORGE_AUDIO_BATCH_ALL ? ForgeResultInvalidCall : result;
    }
    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_output_filter_type(voice, destination_voice, type, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    result = fa_voice_install_set_output_filter_type(voice, destination_voice, type);
    LOG_API_EXIT(voice->audio)
    return result;
}

static ForgeResult queue_or_install_ramp_output_filter(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                       const ForgeFilterTarget *target, uint32_t duration_frames,
                                                       ForgeAudioBatchId batch_id) {
    ForgeResult result = validate_filter_target_arg(target);
    if (result != ForgeResultSuccess) {
        return result;
    }
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }
    if (voice->audio->active) {
        fa_batch_queue_ramp_output_filter(voice, destination_voice, target, duration_frames, batch_id);
        return ForgeResultSuccess;
    }
    return fa_voice_install_ramp_output_filter(voice, destination_voice, target, duration_frames);
}

ForgeResult forge_voice_set_output_filter_target(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                 const ForgeFilterTarget *target, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_output_filter(voice, destination_voice, target,
                                                 FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_output_filter_frames(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                  const ForgeFilterTarget *target, uint32_t duration_frames,
                                                  ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)
    result = queue_or_install_ramp_output_filter(voice, destination_voice, target, duration_frames, batch_id);
    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_output_filter_ms(ForgeVoice *voice, ForgeVoice *destination_voice,
                                              const ForgeFilterTarget *target, double duration_ms,
                                              ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)
    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_output_filter(voice, destination_voice, target, duration_frames, batch_id);
    }
    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                              ForgeFilterParameters *parameters) {
    uint32_t i = 0;
    ForgeResult result;

    LOG_API_ENTER(voice->audio)

    if (voice->type == FORGE_AUDIO_VOICE_MASTER || parameters == NULL) {
        LOG_API_EXIT(voice->audio)
        return;
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    result = find_output_filter_locked(voice, destination_voice, &i);
    if (result == ForgeResultSuccess) {
        filter_runtime_get_parameters(&voice->sends.sends[i].filter, parameters);
    }
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_get_output_filter_cutoff_range(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                       float *min_cutoff_hz, float *max_cutoff_hz) {
    uint32_t i = 0;
    ForgeResult result;

    if (voice == NULL || voice->type == FORGE_AUDIO_VOICE_MASTER) {
        return ForgeResultInvalidCall;
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
    result = find_output_filter_locked(voice, destination_voice, &i);
    if (result == ForgeResultSuccess) {
        filter_get_cutoff_range(voice->sends.sends[i].filter.sample_rate, min_cutoff_hz, max_cutoff_hz);
    } else if (result == ForgeResultFormatSuggested) {
        result = ForgeResultInvalidCall;
    }
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    return result;
}

ForgeResult fa_voice_install_set_volume(ForgeVoice *voice, float volume) {
    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    voice->volume = forge_clamp(volume, -FORGE_AUDIO_MAX_VOLUME_LEVEL, FORGE_AUDIO_MAX_VOLUME_LEVEL);
    voice->volumeAutomation.active = 0;
    voice->volumeAutomation.remainingFrames = 0;
    voice->volumeAutomation.stopSourceOnComplete = 0;

    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        fa_voice_recalc_mix_matrix(voice, i);
    }

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    return ForgeResultSuccess;
}

ForgeResult fa_voice_install_set_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes) {
    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    forge_memcpy(voice->channelVolume, volumes, sizeof(float) * channels);
    voice->channelVolumeAutomation.active = 0;
    voice->channelVolumeAutomation.remainingFrames = 0;

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    return ForgeResultSuccess;
}

static ForgeResult install_volume_automation(ForgeVoice *voice, float volume, uint32_t duration_frames,
                                             uint8_t stop_source_on_complete) {
    float target = forge_clamp(volume, -FORGE_AUDIO_MAX_VOLUME_LEVEL, FORGE_AUDIO_MAX_VOLUME_LEVEL);

    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    if (duration_frames == 0) {
        voice->volume = target;
        voice->volumeAutomation.active = 0;
        voice->volumeAutomation.remainingFrames = 0;
        voice->volumeAutomation.stopSourceOnComplete = 0;
        if (stop_source_on_complete) {
            voice->src.active = 0;
        }
    } else {
        voice->volumeAutomation.target = target;
        voice->volumeAutomation.remainingFrames = duration_frames;
        voice->volumeAutomation.step = (target - voice->volume) / (float)duration_frames;
        voice->volumeAutomation.stopSourceOnComplete = stop_source_on_complete;
        voice->volumeAutomation.active = 1;
    }

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    return ForgeResultSuccess;
}

ForgeResult fa_voice_install_ramp_volume(ForgeVoice *voice, float volume, uint32_t duration_frames) {
    return install_volume_automation(voice, volume, duration_frames, 0);
}

ForgeResult fa_voice_install_ramp_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                  uint32_t duration_frames) {
    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    if (duration_frames == 0) {
        forge_memcpy(voice->channelVolume, volumes, sizeof(float) * channels);
        voice->channelVolumeAutomation.active = 0;
        voice->channelVolumeAutomation.remainingFrames = 0;
    } else {
        for (uint32_t i = 0; i < channels; i += 1) {
            voice->channelVolumeAutomation.target[i] = volumes[i];
            voice->channelVolumeAutomation.step[i] = (volumes[i] - voice->channelVolume[i]) / (float)duration_frames;
        }
        voice->channelVolumeAutomation.remainingFrames = duration_frames;
        voice->channelVolumeAutomation.active = 1;
    }

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    return ForgeResultSuccess;
}

static ForgeResult validate_output_matrix_locked(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                 uint32_t source_channels, uint32_t destination_channels,
                                                 uint32_t *send_index) {
    uint32_t i;

    if (destination_voice == NULL && voice->sends.send_count == 1) {
        destination_voice = voice->sends.sends[0].send.output_voice;
    }
    if (destination_voice == NULL) {
        return ForgeResultInvalidCall;
    }
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].send.output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        return ForgeResultInvalidCall;
    }

    if (source_channels != voice->outputChannels) {
        LOG_ERROR(voice->audio, "source_channels not equal to voice channel count: %p %d %d", (void *)voice,
                  source_channels, voice->outputChannels)
        return ForgeResultInvalidCall;
    }

    if (destination_voice->type == FORGE_AUDIO_VOICE_MASTER) {
        if (destination_channels != destination_voice->master.inputChannels) {
            LOG_ERROR(voice->audio, "destination_channels not equal to master channel count: %p %d %d",
                      (void *)destination_voice, destination_channels, destination_voice->master.inputChannels)
            return ForgeResultInvalidCall;
        }
    } else {
        if (destination_channels != destination_voice->mix.inputChannels) {
            LOG_ERROR(voice->audio, "destination_channels not equal to submix channel count: %p %d %d",
                      (void *)destination_voice, destination_channels, destination_voice->mix.inputChannels)
            return ForgeResultInvalidCall;
        }
    }

    if (send_index != NULL) {
        *send_index = i;
    }
    return ForgeResultSuccess;
}

static ForgeResult validate_output_matrix_args(ForgeVoice *voice, ForgeVoice **destination_voice,
                                               uint32_t source_channels, uint32_t destination_channels) {
    ForgeResult result;

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (*destination_voice == NULL && voice->sends.send_count == 1) {
        *destination_voice = voice->sends.sends[0].send.output_voice;
    }
    result = validate_output_matrix_locked(voice, *destination_voice, source_channels, destination_channels, NULL);

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    return result;
}

static void install_set_output_matrix_locked(ForgeVoice *voice, uint32_t send_index, uint32_t source_channels,
                                             uint32_t destination_channels, const float *level_matrix) {
    forge_memcpy(voice->sends.sends[send_index].sendCoefficients, level_matrix,
                 sizeof(float) * source_channels * destination_channels);
    voice->sends.sends[send_index].matrixAutomation.active = 0;
    voice->sends.sends[send_index].matrixAutomation.remainingFrames = 0;

    fa_voice_recalc_mix_matrix(voice, send_index);
}

static void install_ramp_output_matrix_locked(ForgeVoice *voice, uint32_t send_index, uint32_t source_channels,
                                              uint32_t destination_channels, const float *level_matrix,
                                              uint32_t duration_frames) {
    ForgeVoiceSendRuntime *send = &voice->sends.sends[send_index];
    uint32_t coefficientCount = source_channels * destination_channels;

    if (duration_frames == 0) {
        install_set_output_matrix_locked(voice, send_index, source_channels, destination_channels, level_matrix);
        return;
    }

    for (uint32_t coefficient = 0; coefficient < coefficientCount; coefficient += 1) {
        send->matrixAutomation.target[coefficient] = level_matrix[coefficient];
        send->matrixAutomation.step[coefficient] =
            (level_matrix[coefficient] - send->sendCoefficients[coefficient]) / (float)duration_frames;
    }
    send->matrixAutomation.remainingFrames = duration_frames;
    send->matrixAutomation.active = 1;
}

ForgeResult fa_voice_install_set_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice,
                                               uint32_t source_channels, uint32_t destination_channels,
                                               const float *level_matrix) {
    uint32_t send_index = 0;
    ForgeResult result = ForgeResultSuccess;

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    result = validate_output_matrix_locked(voice, destination_voice, source_channels, destination_channels,
                                           &send_index);
    if (result != ForgeResultSuccess) {
        goto end;
    }

    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    install_set_output_matrix_locked(voice, send_index, source_channels, destination_channels, level_matrix);

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

end:
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    return result;
}

ForgeResult fa_voice_install_ramp_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                uint32_t source_channels, uint32_t destination_channels,
                                                const float *level_matrix, uint32_t duration_frames) {
    uint32_t send_index = 0;
    ForgeResult result = ForgeResultSuccess;

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    result = validate_output_matrix_locked(voice, destination_voice, source_channels, destination_channels,
                                           &send_index);
    if (result != ForgeResultSuccess) {
        goto end;
    }

    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    install_ramp_output_matrix_locked(voice, send_index, source_channels, destination_channels, level_matrix,
                                      duration_frames);

    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

end:
    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    return result;
}

ForgeResult fa_source_voice_install_fade_stop(ForgeSourceVoice *voice, float volume, uint32_t duration_frames) {
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);
    return install_volume_automation(voice, volume, duration_frames, 1);
}

ForgeResult forge_voice_set_volume(ForgeVoice *voice, float volume, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_volume(voice, volume, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (voice->audio->active) {
        fa_batch_clear_ready_volume_automation(voice);
    }

    fa_voice_install_set_volume(voice, volume);

    LOG_API_EXIT(voice->audio)
    return 0;
}

static ForgeResult queue_or_install_ramp_volume(ForgeVoice *voice, float volume, uint32_t duration_frames,
                                                ForgeAudioBatchId batch_id) {
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }

    if (voice->audio->active) {
        fa_batch_queue_ramp_volume(voice, volume, duration_frames, batch_id);
        return 0;
    }

    fa_voice_install_ramp_volume(voice, volume, duration_frames);

    return 0;
}

ForgeResult forge_voice_set_volume_target(ForgeVoice *voice, float volume, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_volume(voice, volume, FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_volume_frames(ForgeVoice *voice, float volume, uint32_t duration_frames,
                                           ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_volume(voice, volume, duration_frames, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_volume_ms(ForgeVoice *voice, float volume, double duration_ms,
                                       ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)

    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_volume(voice, volume, duration_frames, batch_id);
    }

    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_volume(ForgeVoice *voice, float *volume) {
    LOG_API_ENTER(voice->audio)
    *volume = voice->volume;
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                            ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (volumes == NULL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (channels != voice->outputChannels) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_channel_volumes(voice, channels, volumes, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (voice->audio->active) {
        fa_batch_clear_ready_channel_volume_automation(voice);
    }

    fa_voice_install_set_channel_volumes(voice, channels, volumes);

    LOG_API_EXIT(voice->audio)
    return 0;
}

static ForgeResult queue_or_install_ramp_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                         uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }

    if (volumes == NULL) {
        return ForgeResultInvalidCall;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        return ForgeResultInvalidCall;
    }

    if (channels != voice->outputChannels) {
        return ForgeResultInvalidCall;
    }

    if (voice->audio->active) {
        fa_batch_queue_ramp_channel_volumes(voice, channels, volumes, duration_frames, batch_id);
        return 0;
    }

    fa_voice_install_ramp_channel_volumes(voice, channels, volumes, duration_frames);

    return 0;
}

ForgeResult forge_voice_set_channel_volumes_target(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                   ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_channel_volumes(voice, channels, volumes, FA_AUTOMATION_DEFAULT_TARGET_FRAMES,
                                                  batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_channel_volumes_frames(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                    uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_channel_volumes(voice, channels, volumes, duration_frames, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_channel_volumes_ms(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                double duration_ms, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)

    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_channel_volumes(voice, channels, volumes, duration_frames, batch_id);
    }

    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_channel_volumes(ForgeVoice *voice, uint32_t channels, float *volumes) {
    LOG_API_ENTER(voice->audio)
    fa_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    forge_memcpy(volumes, voice->channelVolume, sizeof(float) * channels);
    fa_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t source_channels,
                                          uint32_t destination_channels, const float *level_matrix,
                                          ForgeAudioBatchId batch_id) {
    ForgeResult result = ForgeResultSuccess;
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (level_matrix == NULL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (voice->audio->active) {
        result = validate_output_matrix_args(voice, &destination_voice, source_channels, destination_channels);
        if (result != ForgeResultSuccess) {
            LOG_API_EXIT(voice->audio)
            return result;
        }

        if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE) {
            fa_batch_queue_set_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                             level_matrix, batch_id);
            LOG_API_EXIT(voice->audio)
            return 0;
        }

        fa_batch_clear_ready_output_matrix_automation(voice, destination_voice);
    }

    result = fa_voice_install_set_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                                level_matrix);
    LOG_API_EXIT(voice->audio)
    return result;
}

static ForgeResult queue_or_install_ramp_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                       uint32_t source_channels, uint32_t destination_channels,
                                                       const float *level_matrix, uint32_t duration_frames,
                                                       ForgeAudioBatchId batch_id) {
    ForgeResult result = ForgeResultSuccess;

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }

    if (level_matrix == NULL) {
        return ForgeResultInvalidCall;
    }

    if (voice->audio->active) {
        result = validate_output_matrix_args(voice, &destination_voice, source_channels, destination_channels);
        if (result != ForgeResultSuccess) {
            return result;
        }

        fa_batch_queue_ramp_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                          level_matrix, duration_frames, batch_id);
        return 0;
    }

    result = fa_voice_install_ramp_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                                 level_matrix, duration_frames);
    return result;
}

ForgeResult forge_voice_set_output_matrix_target(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                 uint32_t source_channels, uint32_t destination_channels,
                                                 const float *level_matrix, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                                 level_matrix, FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_output_matrix_frames(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                  uint32_t source_channels, uint32_t destination_channels,
                                                  const float *level_matrix, uint32_t duration_frames,
                                                  ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                                 level_matrix, duration_frames, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_voice_ramp_output_matrix_ms(ForgeVoice *voice, ForgeVoice *destination_voice,
                                              uint32_t source_channels, uint32_t destination_channels,
                                              const float *level_matrix, double duration_ms,
                                              ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)

    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                                     level_matrix, duration_frames, batch_id);
    }

    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t source_channels,
                                   uint32_t destination_channels, float *level_matrix) {
    uint32_t i;

    LOG_API_ENTER(voice->audio)
    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].send.output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Verify the Source/Destination channel count */
    forge_assert(source_channels == voice->outputChannels);
    if (destination_voice->type == FORGE_AUDIO_VOICE_MASTER) {
        forge_assert(destination_channels == destination_voice->master.inputChannels);
    } else {
        forge_assert(destination_channels == destination_voice->mix.inputChannels);
    }

    /* Get the matrix values, finally */
    forge_memcpy(level_matrix, voice->sends.sends[i].sendCoefficients,
                 sizeof(float) * source_channels * destination_channels);

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

static ForgeResult check_for_sends_to_voice(ForgeVoice *voice) {
    ForgeAudioEngine *audio = voice->audio;
    ForgeResult ret = ForgeResultSuccess;
    ForgeSourceVoice *source;
    ForgeSubmixVoice *submix;
    ForgeLinkedList *list;
    uint32_t i;

    fa_platform_lock_mutex(audio->sourceLock);
    list = audio->sources;
    while (list != NULL) {
        source = (ForgeSourceVoice *)list->entry;
        for (i = 0; i < source->sends.send_count; i += 1)
            if (source->sends.sends[i].send.output_voice == voice) {
                ret = ForgeResultFailed;
                break;
            }
        if (ret)
            break;
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->sourceLock);

    if (ret)
        return ret;

    fa_platform_lock_mutex(audio->submixLock);
    list = audio->submixes;
    while (list != NULL) {
        submix = (ForgeSubmixVoice *)list->entry;
        for (i = 0; i < submix->sends.send_count; i += 1)
            if (submix->sends.sends[i].send.output_voice == voice) {
                ret = ForgeResultFailed;
                break;
            }
        if (ret)
            break;
        list = list->next;
    }
    fa_platform_unlock_mutex(audio->submixLock);

    return ret;
}

static void destroy_voice(ForgeVoice *voice) {
    /* Callers must reject incoming sends before destroying a voice; clear queued commands here. */
    fa_batch_clear_all_for_voice(voice);

    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
#ifdef FORGE_AUDIO_DUMP_VOICES
        dump_voice_finalize((ForgeSourceVoice *)voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

        fa_platform_lock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
        while (voice == voice->audio->processingSource) {
            fa_platform_unlock_mutex(voice->audio->sourceLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)
            fa_platform_lock_mutex(voice->audio->sourceLock);
            LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
        }
        fa_linked_list_remove_entry(&voice->audio->sources, voice, voice->audio->sourceLock, voice->audio->free_func);
        fa_platform_unlock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        voice->audio->free_func(voice->src.queued_buffers);
        voice->audio->free_func(voice->src.flush_buffers);
        voice->audio->free_func(voice->src.format);
        LOG_MUTEX_DESTROY(voice->audio, voice->src.bufferLock)
        fa_platform_destroy_mutex(voice->src.bufferLock);
    } else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
        /* Remove submix from list */
        fa_linked_list_remove_entry(&voice->audio->submixes, voice, voice->audio->submixLock, voice->audio->free_func);

        /* Delete submix data */
        voice->audio->free_func(voice->mix.inputCache);
        voice->audio->free_func(voice->mix.resampleInputCache);
        voice->audio->free_func(voice->mix.resampleHistory);
    } else if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        if (voice->audio->platform != NULL) {
            fa_platform_quit(voice->audio->platform);
            voice->audio->platform = NULL;
        }
        if (voice->master.effectCache != NULL) {
            voice->audio->free_func(voice->master.effectCache);
        }
        voice->audio->master = NULL;
    }

    if (voice->sendLock != NULL) {
        fa_platform_lock_mutex(voice->sendLock);
        LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
        free_voice_send_runtime(voice);
        fa_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->sendLock)
        fa_platform_destroy_mutex(voice->sendLock);
    }

    if (voice->effectLock != NULL) {
        fa_platform_lock_mutex(voice->effectLock);
        LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
        fa_audio_free_effect_chain(voice);
        fa_platform_unlock_mutex(voice->effectLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->effectLock)
        fa_platform_destroy_mutex(voice->effectLock);
    }

    if (voice->filterLock != NULL) {
        fa_platform_lock_mutex(voice->filterLock);
        LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
        if (voice->filterState != NULL) {
            voice->audio->free_func(voice->filterState);
        }
        fa_platform_unlock_mutex(voice->filterLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->filterLock)
        fa_platform_destroy_mutex(voice->filterLock);
    }

    if (voice->volumeLock != NULL) {
        fa_platform_lock_mutex(voice->volumeLock);
        LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
        if (voice->channelVolume != NULL) {
            voice->audio->free_func(voice->channelVolume);
        }
        if (voice->channelVolumeAutomation.target != NULL) {
            voice->audio->free_func(voice->channelVolumeAutomation.target);
        }
        if (voice->channelVolumeAutomation.step != NULL) {
            voice->audio->free_func(voice->channelVolumeAutomation.step);
        }
        fa_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->volumeLock)
        fa_platform_destroy_mutex(voice->volumeLock);
    }

    voice->audio->free_func(voice);
}

ForgeResult forge_voice_try_destroy(ForgeVoice *voice) {
    ForgeResult ret;

    ForgeAudioEngine *audio = voice->audio;

    LOG_API_ENTER(audio)

    if ((ret = check_for_sends_to_voice(voice))) {
        LOG_ERROR(audio, "Voice %p is an output for other voice(s)", voice)
        LOG_API_EXIT(audio)
        return ret;
    }
    destroy_voice(voice);
    LOG_API_EXIT(audio)
    return 0;
}

void forge_voice_destroy(ForgeVoice *voice) {
    forge_voice_try_destroy(voice);
}

/* ForgeSourceVoice Interface */

ForgeResult forge_source_voice_start(ForgeSourceVoice *voice, uint32_t flags, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_start(voice, flags, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    forge_assert(flags == 0);
    voice->src.active = 1;
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_stop(ForgeSourceVoice *voice, uint32_t flags, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_stop(voice, flags, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (flags & FORGE_AUDIO_PLAY_TAILS) {
        voice->src.active = 2;
    } else {
        voice->src.active = 0;
    }
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_fade_stop_frames(ForgeSourceVoice *voice, float volume, uint32_t duration_frames,
                                                ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->audio->active) {
        fa_batch_queue_fade_stop(voice, volume, duration_frames, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    fa_source_voice_install_fade_stop(voice, volume, duration_frames);

    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_fade_stop_ms(ForgeSourceVoice *voice, float volume, double duration_ms,
                                            ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)

    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return result;
    }

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->audio->active) {
        fa_batch_queue_fade_stop(voice, volume, duration_frames, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    fa_source_voice_install_fade_stop(voice, volume, duration_frames);

    LOG_API_EXIT(voice->audio)
    return 0;
}

static ForgeResult validate_source_buffer_submission(ForgeSourceVoice *voice, const ForgeBuffer *buffer,
                                                     struct queued_buffer *validated) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t playBegin, playLength, loopBegin, loopLength, bufferLength;
    uint32_t playEnd, realPlayEnd;

    if (buffer == NULL) {
        LOG_ERROR(voice->audio, "%s", "Source buffer must not be NULL");
        return ForgeResultInvalidCall;
    }

    if (block_size == 0) {
        LOG_ERROR(voice->audio, "%s", "Source voice has zero block alignment");
        return ForgeResultInvalidCall;
    }

    if (buffer->audio_bytes % block_size != 0) {
        LOG_ERROR(voice->audio, "PCM source buffer audio_bytes must be a multiple of block_align: %u %% %u",
                  buffer->audio_bytes, block_size)
        return ForgeResultInvalidCall;
    }

    playBegin = buffer->play_begin;
    playLength = buffer->play_length;
    loopBegin = buffer->loop_begin;
    loopLength = buffer->loop_length;
    bufferLength = buffer->audio_bytes / block_size;
    playEnd = playBegin + playLength;

    if (buffer->loop_count == 0 && (loopBegin > 0 || loopLength > 0)) {
        return ForgeResultInvalidCall;
    }

    if (playEnd > bufferLength || playEnd < playLength) {
        return ForgeResultInvalidCall;
    }

    realPlayEnd = (playLength == 0) ? bufferLength : playEnd;
    if (buffer->loop_count > 0) {
        uint32_t realLoopEnd;

        if (loopBegin >= realPlayEnd) {
            return ForgeResultInvalidCall;
        }

        if (loopLength == 0) {
            realLoopEnd = realPlayEnd;
        } else {
            realLoopEnd = loopBegin + loopLength;
            if (realLoopEnd < loopLength) {
                return ForgeResultInvalidCall;
            }
        }

        if (realLoopEnd <= playBegin || realLoopEnd > realPlayEnd) {
            return ForgeResultInvalidCall;
        }
    }

    forge_memset(validated, 0, sizeof(*validated));
    forge_memcpy(&validated->buffer, buffer, sizeof(ForgeBuffer));
    validated->buffer.play_begin = playBegin;
    validated->buffer.play_length = playLength;
    validated->buffer.loop_begin = loopBegin;
    validated->buffer.loop_length = loopLength;
    if (playLength != 0) {
        validated->play_bytes = playLength * block_size;
    } else {
        validated->play_bytes = buffer->audio_bytes - (playBegin * block_size);
    }

    if (loopLength != 0) {
        validated->loop_bytes = loopLength * block_size;
    } else {
        validated->loop_bytes = validated->play_bytes + (playBegin * block_size) - (loopBegin * block_size);
    }

    return ForgeResultSuccess;
}

ForgeResult forge_source_voice_submit_buffer(ForgeSourceVoice *voice, const ForgeBuffer *buffer) {
    struct queued_buffer validated;
    struct queued_buffer *entry;
    ForgeResult result;

    LOG_API_ENTER(voice->audio)

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    result = validate_source_buffer_submission(voice, buffer, &validated);
    if (result != ForgeResultSuccess) {
        LOG_API_EXIT(voice->audio)
        return result;
    }

    LOG_INFO(voice->audio, "%p: {flags: 0x%x, audio_bytes: %u, audio_data: %p, Play: %u + %u, Loop: %u + %u x %u}",
             (void *)voice, validated.buffer.flags, validated.buffer.audio_bytes,
             (const void *)validated.buffer.audio_data, validated.buffer.play_begin, validated.buffer.play_length,
             validated.buffer.loop_begin, validated.buffer.loop_length, validated.buffer.loop_count)

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    fa_array_reserve(voice->audio, (void **)&voice->src.queued_buffers, &voice->src.queued_buffers_capacity,
                     voice->src.queued_buffer_count + 1, sizeof(*voice->src.queued_buffers));

    entry = &voice->src.queued_buffers[voice->src.queued_buffer_count++];
    forge_memcpy(entry, &validated, sizeof(*entry));

#ifdef FORGE_AUDIO_DUMP_VOICES
    /* dumping current buffer, append into "data" section */
    if (entry->buffer.audio_data != NULL && entry->play_bytes > 0) {
        dump_voice_write_buffer(voice, &entry->buffer, entry->play_bytes);
    }
#endif /* FORGE_AUDIO_DUMP_VOICES */

    if (voice->src.queued_buffer_count == 1) {
        voice->src.curBufferOffset = entry->buffer.play_begin;
    }

    LOG_INFO(voice->audio, "%p: appended buffer %p", (void *)voice, (void *)&entry->buffer)
    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_flush_buffers(ForgeSourceVoice *voice) {
    size_t offset = 0;

    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    fa_array_reserve(voice->audio, (void **)&voice->src.flush_buffers, &voice->src.flush_buffers_capacity,
                     voice->src.flush_buffer_count + voice->src.queued_buffer_count, sizeof(*voice->src.flush_buffers));

    if (voice->src.active == 1 && voice->src.queued_buffer_count > 0 &&
        voice->src.queued_buffers[0].sent_OnStartBuffer) {
        offset = 1;
    } else {
        voice->src.curBufferOffset = 0;
    }

    if (voice->src.queued_buffer_count > offset) {
        forge_memcpy(voice->src.flush_buffers + voice->src.flush_buffer_count, voice->src.queued_buffers + offset,
                     (voice->src.queued_buffer_count - offset) * sizeof(*voice->src.flush_buffers));
    }

    voice->src.flush_buffer_count += voice->src.queued_buffer_count - offset;
    voice->src.queued_buffer_count = offset;

    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_end_stream(ForgeSourceVoice *voice) {
    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0) {
        voice->src.queued_buffers[voice->src.queued_buffer_count - 1].buffer.flags |= FORGE_AUDIO_END_OF_STREAM;
    }

    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_break_loop(ForgeSourceVoice *voice, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_exit_loop(voice, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0) {
        voice->src.queued_buffers[0].buffer.loop_count = 0;
    }

    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_source_voice_get_state(ForgeSourceVoice *voice, ForgeVoiceState *voice_state, uint32_t flags) {
    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (!(flags & FORGE_AUDIO_VOICE_NOSAMPLESPLAYED)) {
        voice_state->samples_played = voice->src.totalSamples;
    }

    voice_state->buffers_queued = 0;
    voice_state->current_buffer_context = NULL;

    if (voice->src.queued_buffer_count != 0) {
        voice_state->current_buffer_context = voice->src.queued_buffers[0].buffer.context;
    }
    voice_state->buffers_queued += (uint32_t)voice->src.queued_buffer_count;

    /* Pending flushed buffers also count */
    voice_state->buffers_queued += (uint32_t)voice->src.flush_buffer_count;

    LOG_INFO(voice->audio, "-> {current_buffer_context: %p, buffers_queued: %u, samples_played: %" FORGE_PRIu64 "}",
             voice_state->current_buffer_context, voice_state->buffers_queued, voice_state->samples_played)

    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_source_voice_set_rate(ForgeSourceVoice *voice, float ratio, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        fa_batch_queue_set_frequency_ratio(voice, ratio, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (voice->audio->active) {
        fa_batch_clear_ready_rate_automation(voice);
    }

    fa_source_voice_install_set_rate(voice, ratio);

    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult fa_source_voice_install_set_rate(ForgeSourceVoice *voice, float ratio) {
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->flags & FORGE_AUDIO_VOICE_NOPITCH) {
        return 0;
    }

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    voice->src.freqRatio = forge_clamp(ratio, FORGE_AUDIO_MIN_FREQ_RATIO, voice->src.maxFreqRatio);
    voice->src.rateAutomation.active = 0;
    voice->src.rateAutomation.remainingFrames = 0;

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    return 0;
}

ForgeResult fa_source_voice_install_ramp_rate(ForgeSourceVoice *voice, float ratio, uint32_t duration_frames) {
    float target;

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->flags & FORGE_AUDIO_VOICE_NOPITCH) {
        return 0;
    }

    target = forge_clamp(ratio, FORGE_AUDIO_MIN_FREQ_RATIO, voice->src.maxFreqRatio);

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (duration_frames == 0) {
        voice->src.freqRatio = target;
        voice->src.rateAutomation.active = 0;
        voice->src.rateAutomation.remainingFrames = 0;
    } else {
        voice->src.rateAutomation.target = target;
        voice->src.rateAutomation.remainingFrames = duration_frames;
        voice->src.rateAutomation.step = (target - voice->src.freqRatio) / (float)duration_frames;
        voice->src.rateAutomation.active = 1;
    }

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    return 0;
}

static ForgeResult queue_or_install_ramp_rate(ForgeSourceVoice *voice, float ratio, uint32_t duration_frames,
                                              ForgeAudioBatchId batch_id) {
    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        return ForgeResultInvalidCall;
    }

    if (voice->audio->active) {
        fa_batch_queue_ramp_frequency_ratio(voice, ratio, duration_frames, batch_id);
        return 0;
    }

    return fa_source_voice_install_ramp_rate(voice, ratio, duration_frames);
}

ForgeResult forge_source_voice_set_rate_target(ForgeSourceVoice *voice, float ratio, ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_rate(voice, ratio, FA_AUTOMATION_DEFAULT_TARGET_FRAMES, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_source_voice_ramp_rate_frames(ForgeSourceVoice *voice, float ratio, uint32_t duration_frames,
                                                ForgeAudioBatchId batch_id) {
    ForgeResult result;
    LOG_API_ENTER(voice->audio)

    result = queue_or_install_ramp_rate(voice, ratio, duration_frames, batch_id);

    LOG_API_EXIT(voice->audio)
    return result;
}

ForgeResult forge_source_voice_ramp_rate_ms(ForgeSourceVoice *voice, float ratio, double duration_ms,
                                            ForgeAudioBatchId batch_id) {
    ForgeResult result;
    uint32_t duration_frames = 0;
    LOG_API_ENTER(voice->audio)

    result = forge_audio_ms_to_frames(voice->audio, duration_ms, &duration_frames);
    if (result == ForgeResultSuccess) {
        result = queue_or_install_ramp_rate(voice, ratio, duration_frames, batch_id);
    }

    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_source_voice_get_rate(ForgeSourceVoice *voice, float *ratio) {
    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    *ratio = voice->src.freqRatio;
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_source_voice_set_sample_rate(ForgeSourceVoice *voice, uint32_t new_source_sample_rate) {
    uint32_t outSampleRate;
    uint32_t newDecodeSamples, newResampleSamples;

    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);
    forge_assert(new_source_sample_rate >= FORGE_AUDIO_MIN_SAMPLE_RATE &&
                 new_source_sample_rate <= FORGE_AUDIO_MAX_SAMPLE_RATE);

    fa_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
    if (voice->src.queued_buffer_count != 0) {
        fa_platform_unlock_mutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }
    fa_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

    fa_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    outSampleRate = source_voice_output_rate(voice);

    newResampleSamples = (uint32_t)(forge_ceil((double)voice->audio->updateSize * (double)outSampleRate /
                                               (double)voice->audio->master->master.inputSampleRate));

    fa_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    newDecodeSamples = source_decode_frame_count(newResampleSamples, voice->src.maxFreqRatio,
                                                          new_source_sample_rate, outSampleRate);
    if (!fa_audio_resize_decode_cache(voice->audio,
                                      (newDecodeSamples + EXTRA_DECODE_PADDING) * voice->src.format->channels)) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultOutOfMemory;
    }

    voice->src.format->sample_rate = new_source_sample_rate;
    voice->src.resampleSamples = newResampleSamples;
    voice->src.decodeSamples = newDecodeSamples;

    LOG_API_EXIT(voice->audio)
    return 0;
}

/* ForgeMasterVoice Interface */

FORGE_AUDIO_API ForgeResult forge_master_voice_get_channel_mask(ForgeMasterVoice *voice, uint32_t *channel_mask) {
    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_MASTER);
    forge_assert(channel_mask != NULL);

    *channel_mask = voice->audio->mixFormat.channel_mask;
    LOG_API_EXIT(voice->audio)
    return 0;
}

#ifdef FORGE_AUDIO_DUMP_VOICES

static inline ForgeAudioIOStreamOut *dump_voices_fopen(const ForgeSourceVoice *voice, const ForgeAudioFormat *format,
                                                      const char *mode, const char *ext) {
    char loc[64];
    uint16_t format_tag = format->format_tag;
    uint16_t format_ex_tag = 0;
    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        const ForgeAudioFormatExtensible *format_ex = (const ForgeAudioFormatExtensible *)format;
        format_ex_tag = fa_format_id_tag(format_ex->format_id);
    }
    forge_snprintf(loc, sizeof(loc), "FA_fmt_0x%04X_0x%04X_0x%016lX%s.wav", format_tag, format_ex_tag, (uint64_t)voice,
                   ext);
    ForgeAudioIOStreamOut *file_out = fa_dump_fopen_out(loc, mode);
    return file_out;
}

static inline void dump_voices_finalize_section(const ForgeSourceVoice *voice, const ForgeAudioFormat *format,
                                               const char *section /* one of "data" or "dpds" */
) {
    /* data file only contains the real data bytes */
    ForgeAudioIOStreamOut *io_data = dump_voices_fopen(voice, format, "rb", section);
    if (!io_data) {
        return;
    }
    fa_platform_lock_mutex((ForgeAudioMutex)io_data->lock);
    size_t file_size_data = io_data->size(io_data->data);
    if (file_size_data == 0) {
        /* nothing to do */
        /* close data file */
        fa_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
        fa_dump_close_out(io_data);
        return;
    }

    /* we got some data: append data section to main file */
    ForgeAudioIOStreamOut *io = dump_voices_fopen(voice, format, "ab", "");
    if (!io) {
        /* close data file */
        fa_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
        fa_dump_close_out(io_data);
        return;
    }

    /* data sub-chunk - 8 bytes + data */
    /* SubChunk2ID - 4 --> "data" or "dpds" */
    io->write(io->data, section, 4, 1);
    /* Subchunk2Size - 4 */
    uint32_t chunk_size = (uint32_t)file_size_data;
    io->write(io->data, &chunk_size, 4, 1);
    /* data */
    /* fill in data bytes */
    uint8_t buffer[1024 * 1024];
    size_t count;
    while ((count = io_data->read(io_data->data, (void *)buffer, 1, 1024 * 1024)) > 0) {
        io->write(io->data, (void *)buffer, 1, count);
    }

    /* close data file */
    fa_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
    fa_dump_close_out(io_data);
    /* close main file */
    fa_platform_unlock_mutex((ForgeAudioMutex)io->lock);
    fa_dump_close_out(io);
}

static void dump_voice_init(const ForgeSourceVoice *voice) {
    const ForgeAudioFormat *format = voice->src.format;

    ForgeAudioIOStreamOut *io = dump_voices_fopen(voice, format, "wb", "");
    if (!io) {
        return;
    }
    fa_platform_lock_mutex((ForgeAudioMutex)io->lock);
    /* wave file format taken from
     * http://soundfile.sapp.org/doc/WaveFormat
     * https://sites.google.com/site/musicgapi/technical-documents/wav-file-format
     * |52 49|46 46|52 4A|02 00|
     * |c1 sz|af|nc|sp rt|bt rt|
     * |ba|bs|da ta|c2 sz|

     * | R  I  F  F  |chunk size  |W  A  V  E  |f  m  t     |
     *                19026
     * | 52 49 46 46  52 4A 02 00  57 41 56 45  66 6D 74 20 | RIFFRJ..WAVEfmt

     * | subchnk size|fmt  |channel_count |samplerate  |byte rate   |
     * | 50          | 2   |2     |11025       |11289       |
     * | 32 00 00 00  02 00 02 00  11 2B 00 00  19 2C 00 00 | 2........+...,..

     * |blkaln|bps   |efmt |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
     * | 512  |4     |32   |500   |7    |256   |0    |512   |
     * | 512  |4     |32   |459252      |256         |
     * | 00 02|04 00  20 00 F4 01  07 00 00 01  00 00 00 02 | .... .ô.........

     * | XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX |
     * |
     * | 00 FF 00 00  00 00 C0 00  40 00 F0 00  00 00 CC 01 | .ÿ....À.@.ð...Ì.

     * | XXXXXXXXXXXXXXXXXX|d  a   t  a |chunk size  |XXXXX |
     * |                   |            |18944       |      |
     * | 30 FF 88 01  18 FF 64 61  74 61 00 4A  02 00 00 00 | 0ÿ...ÿdata.J....
     */

    uint16_t extra_size = format->extra_size;
    uint16_t format_tag = format->format_tag;

    { /* RIFF chunk descriptor - 12 byte */
        /* ChunkID - 4 */
        io->write(io->data, "RIFF", 4, 1);
        /* ChunkSize - 4 */
        uint32_t filesize = 0; /* the real file size is written in finalize step */
        io->write(io->data, &filesize, 4, 1);
        /* format - 4 */
        io->write(io->data, "WAVE", 4, 1);
    }
    { /* fmt sub-chunk 24 */
        /* Subchunk1ID - 4 */
        io->write(io->data, "fmt ", 4, 1);
        /* Subchunk1Size - 4 */
        /* 18 byte for WAVEFORMATEX and extra_size for WAVEFORMATEXTENDED */
        uint32_t chunk_data_size = 18 + (uint32_t)extra_size;
        io->write(io->data, &chunk_data_size, 4, 1);
        /* AudioFormat - 2 */
        io->write(io->data, &format_tag, 2, 1);
        /* NumChannels - 2 */
        io->write(io->data, &format->channels, 2, 1);
        /* SampleRate - 4 */
        io->write(io->data, &format->sample_rate, 4, 1);
        /* ByteRate - 4 */
        /* SampleRate * NumChannels * BitsPerSample/8 */
        io->write(io->data, &format->average_bytes_per_second, 4, 1);
        /* BlockAlign - 2 */
        /* NumChannels * BitsPerSample/8 */
        io->write(io->data, &format->block_align, 2, 1);
        /* BitsPerSample - 2 */
        io->write(io->data, &format->bits_per_sample, 2, 1);
    }
    /* in case of extensible audio format write the additional data to the file */
    {
        /* always write the extra_size */
        io->write(io->data, &extra_size, 2, 1);

        if (extra_size >= 22) {
            /* we have a WAVEFORMATEXTENSIBLE struct to write */
            const ForgeAudioFormatExtensible *format_ex = (const ForgeAudioFormatExtensible *)format;
            io->write(io->data, &format_ex->samples.valid_bits_per_sample, 2, 1);
            io->write(io->data, &format_ex->channel_mask, 4, 1);
            io->write(io->data, format_ex->format_id, 1, FORGE_AUDIO_FORMAT_ID_SIZE);
        }
        if (format->extra_size > 22) {
            /* fill up the remaining extra_size bytes with zeros */
            uint8_t zero = 0;
            for (uint16_t i = 23; i <= format->extra_size; i++) {
                io->write(io->data, &zero, 1, 1);
            }
        }
    }
    { /* data sub-chunk - 8 bytes + data */
        /* create file to hold the data samples */
        ForgeAudioIOStreamOut *io_data = dump_voices_fopen(voice, format, "wb", "data");
        fa_dump_close_out(io_data);
        /* io_data file will be filled by SubmitBuffer */
    }
    fa_platform_unlock_mutex((ForgeAudioMutex)io->lock);
    fa_dump_close_out(io);
}

static void dump_voice_finalize(const ForgeSourceVoice *voice) {
    const ForgeAudioFormat *format = voice->src.format;

    /* add data subchunk */
    dump_voices_finalize_section(voice, format, "data");

    /* open main file to update filesize */
    ForgeAudioIOStreamOut *io = dump_voices_fopen(voice, format, "r+b", "");
    if (!io) {
        return;
    }
    fa_platform_lock_mutex((ForgeAudioMutex)io->lock);
    size_t file_size = io->size(io->data);
    if (file_size >= 44) {
        /* update filesize */
        uint32_t chunk_size = (uint32_t)(file_size - 8);
        io->seek(io->data, 4, FORGE_AUDIO_SEEK_SET);
        io->write(io->data, &chunk_size, 4, 1);
    }
    fa_platform_unlock_mutex((ForgeAudioMutex)io->lock);
    fa_dump_close_out(io);
}

static void dump_voice_write_buffer(const ForgeSourceVoice *voice, const ForgeBuffer *buffer, const uint32_t size) {
    ForgeAudioIOStreamOut *io_data = dump_voices_fopen(voice, voice->src.format, "ab", "data");
    if (io_data == NULL) {
        return;
    }

    fa_platform_lock_mutex((ForgeAudioMutex)io_data->lock);
    uint16_t bytesPerFrame = (voice->src.format->channels * voice->src.format->bits_per_sample / 8);
    forge_assert(bytesPerFrame > 0);
    const void *audio_data_begin = buffer->audio_data + buffer->play_begin * bytesPerFrame;
    io_data->write(io_data->data, audio_data_begin, 1, size);
    fa_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
    fa_dump_close_out(io_data);
}

#endif /* FORGE_AUDIO_DUMP_VOICES */
