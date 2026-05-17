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

#include "forge_audio_internal.h"

#define MAKE_FORMAT_ID(name, fmt)                                                                                      \
    const uint8_t forge_audio_format_id_##name[FORGE_AUDIO_FORMAT_ID_SIZE] = {(uint8_t)((fmt) & 0xFF),                 \
                                                                              (uint8_t)(((fmt) >> 8) & 0xFF),          \
                                                                              0x00,                                    \
                                                                              0x00,                                    \
                                                                              0x00,                                    \
                                                                              0x00,                                    \
                                                                              0x10,                                    \
                                                                              0x00,                                    \
                                                                              0x80,                                    \
                                                                              0x00,                                    \
                                                                              0x00,                                    \
                                                                              0xAA,                                    \
                                                                              0x00,                                    \
                                                                              0x38,                                    \
                                                                              0x9B,                                    \
                                                                              0x71}
MAKE_FORMAT_ID(pcm, 1);
MAKE_FORMAT_ID(ieee_float, 3);
MAKE_FORMAT_ID(xmaudio2, FORGE_AUDIO_FORMAT_XMAUDIO2);
MAKE_FORMAT_ID(wmaudio2, FORGE_AUDIO_FORMAT_WMAUDIO2);
MAKE_FORMAT_ID(wmaudio3, FORGE_AUDIO_FORMAT_WMAUDIO3);
MAKE_FORMAT_ID(wmaudio_lossless, FORGE_AUDIO_FORMAT_WMAUDIO_LOSSLESS);
#undef MAKE_FORMAT_ID

#ifdef FORGE_AUDIO_DUMP_VOICES
static void forge_audio_dump_voice_init(const ForgeSourceVoice *voice);
static void forge_audio_dump_voice_finalize(const ForgeSourceVoice *voice);
static void forge_audio_dump_voice_write_buffer(const ForgeSourceVoice *voice, const ForgeBuffer *buffer,
                                                const ForgeBufferWMA *buffer_wma, const uint32_t size);
#endif /* FORGE_AUDIO_DUMP_VOICES */

static uint8_t forge_audio_validate_uncompressed_format(ForgeAudioEngine *audio, const ForgeAudioFormat *format) {
    const ForgeAudioFormat *base = format;
    uint8_t isPCM = 0;
    uint8_t isFloat = 0;
    uint32_t expectedBlockAlign;

    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        const ForgeAudioFormatExtensible *ext = (const ForgeAudioFormatExtensible *)format;

        if (forge_audio_format_id_equals(ext->format_id, forge_audio_format_id_pcm)) {
            isPCM = 1;
        } else if (forge_audio_format_id_equals(ext->format_id, forge_audio_format_id_ieee_float)) {
            isFloat = 1;
        } else {
            return 1;
        }
    } else if (format->format_tag == FORGE_AUDIO_FORMAT_PCM) {
        isPCM = 1;
    } else if (format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
        isFloat = 1;
    } else {
        return 1;
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

static uint32_t forge_audio_send_list_output_rate(ForgeAudioEngine *audio, const ForgeSendList *send_list) {
    ForgeVoice *out;

    if (send_list == NULL || send_list->send_count == 0) {
        return audio->master->master.inputSampleRate;
    }

    out = send_list->sends[0].output_voice;
    return (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputSampleRate : out->mix.inputSampleRate;
}

static uint32_t forge_audio_source_voice_output_rate(const ForgeSourceVoice *voice) {
    ForgeVoice *out;

    if (voice->sends.send_count == 0) {
        return voice->audio->master->master.inputSampleRate;
    }

    out = voice->sends.sends[0].output_voice;
    return (out->type == FORGE_AUDIO_VOICE_MASTER) ? out->master.inputSampleRate : out->mix.inputSampleRate;
}

static uint32_t forge_audio_source_decode_frame_count(uint32_t resample_samples, float max_frequency_ratio,
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

#ifdef FORGE_AUDIO_TESTING
uint32_t forge_audio_test_source_decode_frame_count(uint32_t resample_samples, float max_frequency_ratio,
                                                    uint32_t source_sample_rate, uint32_t output_sample_rate) {
    return forge_audio_source_decode_frame_count(resample_samples, max_frequency_ratio, source_sample_rate,
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

static ForgeResult engine_initialize(ForgeAudioEngine *audio, uint32_t flags);

ForgeResult forge_audio_create_with_allocator(ForgeAudioEngine **engine, uint32_t flags, ForgeMallocFunc custom_malloc,
                                              ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    engine_construct_with_allocator(engine, custom_malloc, custom_free, custom_realloc);
    engine_initialize(*engine, flags);
    return 0;
}

static ForgeResult engine_construct_with_allocator(ForgeAudioEngine **engine, ForgeMallocFunc custom_malloc,
                                                   ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    ForgeDebugConfiguration debugInit = {0};
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    forge_platform_add_ref();
    *engine = (ForgeAudioEngine *)custom_malloc(sizeof(ForgeAudioEngine));
    forge_zero(*engine, sizeof(ForgeAudioEngine));
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    forge_audio_set_debug_configuration(*engine, &debugInit, NULL);
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    (*engine)->sourceLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->sourceLock)
    (*engine)->submixLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->submixLock)
    (*engine)->callbackLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->callbackLock)
    (*engine)->batchLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->batchLock)
    (*engine)->malloc_func = custom_malloc;
    (*engine)->free_func = custom_free;
    (*engine)->realloc_func = custom_realloc;
    return 0;
}

static void destroy_voice(ForgeVoice *voice);

void forge_audio_destroy(ForgeAudioEngine *audio) {
    ForgeVoice *voice;

    LOG_API_ENTER(audio)

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
    forge_audio_batch_clear_all(audio);
    forge_audio_stop_engine(audio);
    audio->free_func(audio->decodeCache);
    audio->free_func(audio->resampleCache);
    audio->free_func(audio->effectChainCache);
    audio->free_func(audio->effectChainCache2);
    LOG_MUTEX_DESTROY(audio, audio->sourceLock)
    forge_platform_destroy_mutex(audio->sourceLock);
    LOG_MUTEX_DESTROY(audio, audio->submixLock)
    forge_platform_destroy_mutex(audio->submixLock);
    LOG_MUTEX_DESTROY(audio, audio->callbackLock)
    forge_platform_destroy_mutex(audio->callbackLock);
    LOG_MUTEX_DESTROY(audio, audio->batchLock)
    forge_platform_destroy_mutex(audio->batchLock);
    audio->free_func(audio);
    forge_platform_release();
}

ForgeResult forge_audio_get_device_count(ForgeAudioEngine *audio, uint32_t *count) {
    LOG_API_ENTER(audio)
    *count = forge_platform_get_device_count();
    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_get_device_details(ForgeAudioEngine *audio, uint32_t index,
                                           ForgeDeviceDetails *device_details) {
    uint32_t result;
    LOG_API_ENTER(audio)
    result = forge_platform_get_device_details(index, device_details);
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
    audio->decodeSamples = 1;
    audio->resampleSamples = 1;

    forge_audio_start_engine(audio);
    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_register_callback(ForgeAudioEngine *audio, ForgeEngineCallback *callback) {
    LOG_API_ENTER(audio)
    forge_linked_list_add_entry(&audio->callbacks, callback, audio->callbackLock, audio->malloc_func);
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_unregister_callback(ForgeAudioEngine *audio, ForgeEngineCallback *callback) {
    LOG_API_ENTER(audio)
    forge_linked_list_remove_entry(&audio->callbacks, callback, audio->callbackLock, audio->free_func);
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

    if (send_list == NULL && audio->master == NULL) {
        LOG_ERROR(audio, "%s", "CreateSourceVoice called before mastering voice was initialized");
        return ForgeResultInvalidCall;
    }

    if (!forge_audio_validate_uncompressed_format(audio, source_format)) {
        return ForgeResultInvalidCall;
    }

    *source_voice = (ForgeSourceVoice *)audio->malloc_func(sizeof(ForgeVoice));
    forge_zero(*source_voice, sizeof(ForgeSourceVoice));
    (*source_voice)->audio = audio;
    (*source_voice)->type = FORGE_AUDIO_VOICE_SOURCE;
    (*source_voice)->flags = flags;
    (*source_voice)->filter.type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    (*source_voice)->filter.frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
    (*source_voice)->filter.one_over_q = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
    (*source_voice)->filter.wet_dry_mix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    (*source_voice)->sendLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->sendLock)
    (*source_voice)->effectLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->effectLock)
    (*source_voice)->filterLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->filterLock)
    (*source_voice)->volumeLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->volumeLock)

    /* Source Properties */
    forge_assert(max_frequency_ratio <= FORGE_AUDIO_MAX_FREQ_RATIO);
    (*source_voice)->src.maxFreqRatio = max_frequency_ratio;

    if (source_format->format_tag == FORGE_AUDIO_FORMAT_PCM ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO2 ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO3) {
        ForgeAudioFormatExtensible *fmtex =
            (ForgeAudioFormatExtensible *)audio->malloc_func(sizeof(ForgeAudioFormatExtensible));
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
            forge_memcpy(fmtex->format_id, forge_audio_format_id_pcm, FORGE_AUDIO_FORMAT_ID_SIZE);
        } else if (source_format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
            forge_memcpy(fmtex->format_id, forge_audio_format_id_ieee_float, FORGE_AUDIO_FORMAT_ID_SIZE);
        } else if (source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO2) {
            forge_memcpy(fmtex->format_id, forge_audio_format_id_wmaudio2, FORGE_AUDIO_FORMAT_ID_SIZE);
        } else if (source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO3) {
            forge_memcpy(fmtex->format_id, forge_audio_format_id_wmaudio3, FORGE_AUDIO_FORMAT_ID_SIZE);
        }
        (*source_voice)->src.format = &fmtex->format;
    } else if (source_format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2) {
        ForgeXMA2Format *fmtex = (ForgeXMA2Format *)audio->malloc_func(sizeof(ForgeXMA2Format));

        /* Copy what we can, ideally the sizes match! */
        size_t extra_size = sizeof(ForgeAudioFormat) + source_format->extra_size;
        forge_memcpy(fmtex, source_format, forge_min(extra_size, sizeof(ForgeXMA2Format)));
        if (extra_size < sizeof(ForgeXMA2Format)) {
            forge_zero(((uint8_t *)fmtex) + extra_size, sizeof(ForgeXMA2Format) - extra_size);
        }

        /* Preserve existing input-validation behavior. */
        fmtex->wfx.extra_size = sizeof(ForgeXMA2Format) - sizeof(ForgeAudioFormat);
        (*source_voice)->src.format = &fmtex->wfx;
    } else {
        /* direct copy anything else */
        (*source_voice)->src.format =
            (ForgeAudioFormat *)audio->malloc_func(sizeof(ForgeAudioFormat) + source_format->extra_size);
        forge_memcpy((*source_voice)->src.format, source_format, sizeof(ForgeAudioFormat) + source_format->extra_size);
    }

    (*source_voice)->src.callback = callback;
    (*source_voice)->src.active = 0;
    (*source_voice)->src.freqRatio = 1.0f;
    (*source_voice)->src.totalSamples = 0;
    (*source_voice)->src.bufferLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->src.bufferLock)

    if ((*source_voice)->src.format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        ForgeAudioFormatExtensible *fmtex = (ForgeAudioFormatExtensible *)(*source_voice)->src.format;

#define COMPARE_FORMAT_ID(type) forge_audio_format_id_equals(fmtex->format_id, forge_audio_format_id_##type)
        if (COMPARE_FORMAT_ID(pcm)) {
#define DECODER(bit)                                                                                                   \
    if (fmtex->format.bits_per_sample == bit) {                                                                        \
        (*source_voice)->src.decode = forge_audio_decode_pcm##bit;                                                     \
    }
            DECODER(16)
            else DECODER(8) else DECODER(24) else DECODER(32) else {
                LOG_ERROR(audio, "Unrecognized bits_per_sample: %d", fmtex->format.bits_per_sample)
                forge_assert(0 && "Unrecognized bits_per_sample!");
            }
#undef DECODER
        } else if (COMPARE_FORMAT_ID(ieee_float)) {
            /* FIXME: Decide whether ForgeAudio should keep accepting
             * IEEE_FLOAT format IDs with 16-bit PCM payloads.
             */
            if (fmtex->format.bits_per_sample == 16) {
                (*source_voice)->src.decode = forge_audio_decode_pcm16;
            } else {
                (*source_voice)->src.decode = forge_audio_decode_pcm32f;
            }
        } else if (COMPARE_FORMAT_ID(wmaudio2) || COMPARE_FORMAT_ID(wmaudio3) || COMPARE_FORMAT_ID(wmaudio_lossless)) {
        } else {
            forge_assert(0 && "Unsupported extensible audio format identifier!");
        }
#undef COMPARE_FORMAT_ID
    } else if ((*source_voice)->src.format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2) {
        forge_assert(0 && "XMA2 is not supported!");
        (*source_voice)->src.decode = forge_audio_decode_wma_error;
    } else {
        forge_assert(0 && "Unsupported format tag!");
    }

    if ((*source_voice)->src.format->channels == 1) {
        (*source_voice)->src.resample = forge_audio_resample_mono;
    } else if ((*source_voice)->src.format->channels == 2) {
        (*source_voice)->src.resample = forge_audio_resample_stereo;
    } else {
        (*source_voice)->src.resample = forge_audio_resample_generic;
    }

    (*source_voice)->src.curBufferOffset = 0;

    /* Sends/Effects */
    forge_audio_voice_output_frequency(*source_voice, send_list);
    result = forge_voice_set_effect_chain(*source_voice, effect_chain);
    if (result != 0) {
        audio->free_func((*source_voice)->src.format);
        LOG_MUTEX_DESTROY(audio, (*source_voice)->src.bufferLock)
        forge_platform_destroy_mutex((*source_voice)->src.bufferLock);
        LOG_MUTEX_DESTROY(audio, (*source_voice)->sendLock)
        forge_platform_destroy_mutex((*source_voice)->sendLock);
        LOG_MUTEX_DESTROY(audio, (*source_voice)->effectLock)
        forge_platform_destroy_mutex((*source_voice)->effectLock);
        LOG_MUTEX_DESTROY(audio, (*source_voice)->filterLock)
        forge_platform_destroy_mutex((*source_voice)->filterLock);
        LOG_MUTEX_DESTROY(audio, (*source_voice)->volumeLock)
        forge_platform_destroy_mutex((*source_voice)->volumeLock);
        audio->free_func(*source_voice);
        *source_voice = NULL;
        LOG_API_EXIT(audio)
        return result;
    }

    /* Default Levels */
    (*source_voice)->volume = 1.0f;
    (*source_voice)->channelVolume = (float *)audio->malloc_func(sizeof(float) * (*source_voice)->outputChannels);
    for (uint32_t i = 0; i < (*source_voice)->outputChannels; i += 1) {
        (*source_voice)->channelVolume[i] = 1.0f;
    }

    forge_voice_set_outputs(*source_voice, send_list);

    /* Filters */
    if (flags & FORGE_AUDIO_VOICE_USEFILTER) {
        (*source_voice)->filterState = (ForgeAudioFilterState *)audio->malloc_func(
            sizeof(ForgeAudioFilterState) * (*source_voice)->src.format->channels);
        forge_zero((*source_voice)->filterState, sizeof(ForgeAudioFilterState) * (*source_voice)->src.format->channels);
    }

    /* Sample Storage */
    outputRate = forge_audio_send_list_output_rate(audio, send_list);
    (*source_voice)->src.decodeSamples =
        forge_audio_source_decode_frame_count((*source_voice)->src.resampleSamples, max_frequency_ratio,
                                              (*source_voice)->src.format->sample_rate, outputRate);
    forge_audio_resize_decode_cache(audio, ((*source_voice)->src.decodeSamples + EXTRA_DECODE_PADDING) *
                                               (*source_voice)->src.format->channels);

    LOG_INFO(audio, "-> %p", (void *)(*source_voice))

    LOG_INFO(audio, "-> %p", (void *)(*source_voice))

    /* Add to list, finally. */
    forge_linked_list_prepend_entry(&audio->sources, *source_voice, audio->sourceLock, audio->malloc_func);

#ifdef FORGE_AUDIO_DUMP_VOICES
    forge_audio_dump_voice_init(*source_voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_create_submix_voice(ForgeAudioEngine *audio, ForgeSubmixVoice **submix_voice,
                                            uint32_t input_channels, uint32_t input_sample_rate, uint32_t flags,
                                            uint32_t processing_stage, const ForgeSendList *send_list,
                                            const ForgeEffectChain *effect_chain) {
    ForgeResult result;

    LOG_API_ENTER(audio)

    if (send_list == NULL && audio->master == NULL) {
        LOG_ERROR(audio, "%s", "CreateSubmixVoice called before mastering voice was initialized");
        return ForgeResultInvalidCall;
    }

    *submix_voice = (ForgeSubmixVoice *)audio->malloc_func(sizeof(ForgeVoice));
    forge_zero(*submix_voice, sizeof(ForgeSubmixVoice));
    (*submix_voice)->audio = audio;
    (*submix_voice)->type = FORGE_AUDIO_VOICE_SUBMIX;
    (*submix_voice)->flags = flags;
    (*submix_voice)->filter.type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    (*submix_voice)->filter.frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
    (*submix_voice)->filter.one_over_q = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
    (*submix_voice)->filter.wet_dry_mix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    (*submix_voice)->sendLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->sendLock)
    (*submix_voice)->effectLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->effectLock)
    (*submix_voice)->filterLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->filterLock)
    (*submix_voice)->volumeLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->volumeLock)

    /* Submix Properties */
    (*submix_voice)->mix.inputChannels = input_channels;
    (*submix_voice)->mix.inputSampleRate = input_sample_rate;
    (*submix_voice)->mix.processingStage = processing_stage;

    /* Resampler */
    if (input_channels == 1) {
        (*submix_voice)->mix.resample = forge_audio_resample_mono;
    } else if (input_channels == 2) {
        (*submix_voice)->mix.resample = forge_audio_resample_stereo;
    } else {
        (*submix_voice)->mix.resample = forge_audio_resample_generic;
    }

    /* Sample Storage */
    (*submix_voice)->mix.inputSamples = ((uint32_t)forge_ceil(audio->updateSize * (double)input_sample_rate /
                                                              (double)audio->master->master.inputSampleRate) +
                                         EXTRA_DECODE_PADDING) *
                                        input_channels;
    (*submix_voice)->mix.inputCache = (float *)audio->malloc_func(sizeof(float) * (*submix_voice)->mix.inputSamples);
    forge_zero(/* Zero this now, for the first update */
               (*submix_voice)->mix.inputCache, sizeof(float) * (*submix_voice)->mix.inputSamples);

    /* Sends/Effects */
    forge_audio_voice_output_frequency(*submix_voice, send_list);
    result = forge_voice_set_effect_chain(*submix_voice, effect_chain);
    if (result != 0) {
        audio->free_func((*submix_voice)->mix.inputCache);
        LOG_MUTEX_DESTROY(audio, (*submix_voice)->sendLock)
        forge_platform_destroy_mutex((*submix_voice)->sendLock);
        LOG_MUTEX_DESTROY(audio, (*submix_voice)->effectLock)
        forge_platform_destroy_mutex((*submix_voice)->effectLock);
        LOG_MUTEX_DESTROY(audio, (*submix_voice)->filterLock)
        forge_platform_destroy_mutex((*submix_voice)->filterLock);
        LOG_MUTEX_DESTROY(audio, (*submix_voice)->volumeLock)
        forge_platform_destroy_mutex((*submix_voice)->volumeLock);
        audio->free_func(*submix_voice);
        *submix_voice = NULL;
        LOG_API_EXIT(audio)
        return result;
    }

    /* Default Levels */
    (*submix_voice)->volume = 1.0f;
    (*submix_voice)->channelVolume = (float *)audio->malloc_func(sizeof(float) * (*submix_voice)->outputChannels);
    for (uint32_t i = 0; i < (*submix_voice)->outputChannels; i += 1) {
        (*submix_voice)->channelVolume[i] = 1.0f;
    }

    forge_voice_set_outputs(*submix_voice, send_list);

    /* Filters */
    if (flags & FORGE_AUDIO_VOICE_USEFILTER) {
        (*submix_voice)->filterState =
            (ForgeAudioFilterState *)audio->malloc_func(sizeof(ForgeAudioFilterState) * input_channels);
        forge_zero((*submix_voice)->filterState, sizeof(ForgeAudioFilterState) * input_channels);
    }

    /* Add to list, finally. */
    forge_audio_insert_submix_sorted(&audio->submixes, *submix_voice, audio->submixLock, audio->malloc_func);

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
    forge_zero(*mastering_voice, sizeof(ForgeMasterVoice));
    (*mastering_voice)->audio = audio;
    (*mastering_voice)->type = FORGE_AUDIO_VOICE_MASTER;
    (*mastering_voice)->flags = flags;
    (*mastering_voice)->effectLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->effectLock)
    (*mastering_voice)->volumeLock = forge_platform_create_mutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->volumeLock)

    /* Default Levels */
    (*mastering_voice)->volume = 1.0f;

    /* Master Properties */
    (*mastering_voice)->master.inputChannels = input_channels;
    (*mastering_voice)->master.inputSampleRate = input_sample_rate;

    /* Sends/Effects */
    forge_zero(&(*mastering_voice)->sends, sizeof(ForgeSendList));
    result = forge_voice_set_effect_chain(*mastering_voice, effect_chain);
    if (result != 0) {
        LOG_MUTEX_DESTROY(audio, (*mastering_voice)->effectLock)
        forge_platform_destroy_mutex((*mastering_voice)->effectLock);
        LOG_MUTEX_DESTROY(audio, (*mastering_voice)->volumeLock)
        forge_platform_destroy_mutex((*mastering_voice)->volumeLock);
        audio->free_func(*mastering_voice);
        *mastering_voice = NULL;
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
    WriteWaveFormatExtensible(&audio->mixFormat, audio->master->outputChannels, audio->master->master.inputSampleRate,
                              forge_audio_format_id_ieee_float);

    /* Platform Device */
    forge_platform_init(audio, audio->initFlags, device_index, &audio->mixFormat, &audio->updateSize, &audio->platform);
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
    }

    LOG_API_EXIT(audio)
    return 0;
}

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
    forge_audio_batch_apply_all(audio);
    forge_audio_batch_execute(audio);
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_apply_batch(ForgeAudioEngine *audio, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(audio)
    if (batch_id == FORGE_AUDIO_BATCH_IMMEDIATE) {
        LOG_API_EXIT(audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        forge_audio_batch_apply_all(audio);
    } else {
        forge_audio_batch_apply(audio, batch_id);
    }
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_get_performance_data(ForgeAudioEngine *audio, ForgePerformanceData *perf_data) {
    ForgeLinkedList *list;
    ForgeSourceVoice *source;

    LOG_API_ENTER(audio)

    forge_zero(perf_data, sizeof(ForgePerformanceData));

    forge_platform_lock_mutex(audio->sourceLock);
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
    forge_platform_unlock_mutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)

    forge_platform_lock_mutex(audio->submixLock);
    LOG_MUTEX_LOCK(audio, audio->submixLock)
    list = audio->submixes;
    while (list != NULL) {
        perf_data->active_submix_voice_count += 1;
        list = list->next;
    }
    forge_platform_unlock_mutex(audio->submixLock);
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
    char *env;

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

    #define CHECK_ENV(type)                                                                                            \
        env = forge_getenv("FORGE_AUDIO_LOG_" #type);                                                                  \
        if (env != NULL) {                                                                                             \
            if (*env == '1') {                                                                                         \
                audio->debug.trace_mask |= FORGE_AUDIO_LOG_##type;                                                     \
            } else {                                                                                                   \
                audio->debug.trace_mask &= ~FORGE_AUDIO_LOG_##type;                                                    \
            }                                                                                                          \
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
    #define CHECK_ENV(envvar, boolvar)                                                                                 \
        env = forge_getenv("FORGE_AUDIO_LOG_LOG" #envvar);                                                             \
        if (env != NULL) {                                                                                             \
            audio->debug.Log##boolvar = (*env == '1');                                                                 \
        }
    CHECK_ENV(THREADID, ThreadID)
    CHECK_ENV(FILELINE, Fileline)
    CHECK_ENV(FUNCTIONNAME, FunctionName)
    CHECK_ENV(TIMING, Timing)
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

/* ForgeVoice Interface */

static void forge_audio_recalc_mix_matrix(ForgeVoice *voice, uint32_t sendIndex) {
    uint32_t oChan, s, d;
    ForgeVoice *out = voice->sends.sends[sendIndex].output_voice;
    float volume, *matrix = voice->mixCoefficients[sendIndex];

    if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
        volume = 1.f;
    } else {
        volume = voice->volume;
    }

    if (out->type == FORGE_AUDIO_VOICE_MASTER) {
        oChan = out->master.inputChannels;
    } else {
        oChan = out->mix.inputChannels;
    }

    for (d = 0; d < oChan; d += 1) {
        for (s = 0; s < voice->outputChannels; s += 1) {
            matrix[d * voice->outputChannels + s] =
                volume * voice->channelVolume[s] * voice->sendCoefficients[sendIndex][d * voice->outputChannels + s];
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
    uint32_t sendRate, nextRate, outChannels;
    uint32_t sourceDecodeSamples = 0;
    ForgeSendList defaultSends;
    ForgeSend defaultSend;

    LOG_API_ENTER(voice->audio)

    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (send_list != NULL && send_list->send_count > 1) {
        if (send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_SOURCE) {
            LOG_API_EXIT(voice->audio)
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
                LOG_API_EXIT(voice->audio)
                return ForgeResultInvalidCall;
            }
        }
    }

    if (voice->type == FORGE_AUDIO_VOICE_SOURCE && voice->src.decodeSamples != 0) {
        uint32_t outputRate = forge_audio_send_list_output_rate(voice->audio, send_list);
        uint32_t sourceResampleSamples =
            (uint32_t)forge_ceil((double)voice->audio->updateSize * (double)outputRate /
                                 (double)voice->audio->master->master.inputSampleRate);

        sourceDecodeSamples = forge_audio_source_decode_frame_count(
            sourceResampleSamples, voice->src.maxFreqRatio, voice->src.format->sample_rate, outputRate);
        forge_audio_resize_decode_cache(voice->audio,
                                        (sourceDecodeSamples + EXTRA_DECODE_PADDING) * voice->src.format->channels);
    }

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (forge_audio_voice_output_frequency(voice, send_list) != 0) {
        LOG_ERROR(voice->audio, "%s", "Changing the sample rate while an effect chain is attached is invalid!")
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE && sourceDecodeSamples != 0) {
        voice->src.decodeSamples = sourceDecodeSamples;
    }

    forge_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    /* Rebuild send-dependent matrices, mixers, and filter state for the new output list. */
    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        voice->audio->free_func(voice->sendCoefficients[i]);
    }
    if (voice->sendCoefficients != NULL) {
        voice->audio->free_func(voice->sendCoefficients);
    }
    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        voice->audio->free_func(voice->mixCoefficients[i]);
    }
    if (voice->mixCoefficients != NULL) {
        voice->audio->free_func(voice->mixCoefficients);
    }
    if (voice->sendMix != NULL) {
        voice->audio->free_func(voice->sendMix);
    }
    if (voice->sendFilter != NULL) {
        voice->audio->free_func(voice->sendFilter);
        voice->sendFilter = NULL;
    }
    if (voice->sendFilterState != NULL) {
        for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
            if (voice->sendFilterState[i] != NULL) {
                voice->audio->free_func(voice->sendFilterState[i]);
            }
        }
        voice->audio->free_func(voice->sendFilterState);
        voice->sendFilterState = NULL;
    }
    if (voice->sends.sends != NULL) {
        voice->audio->free_func(voice->sends.sends);
    }

    if (send_list == NULL) {
        /* Default to the mastering voice as output */
        defaultSend.flags = 0;
        defaultSend.output_voice = voice->audio->master;
        defaultSends.send_count = 1;
        defaultSends.sends = &defaultSend;
        send_list = &defaultSends;
    } else if (send_list->send_count == 0) {
        /* No sends? Nothing to do... */
        voice->sendCoefficients = NULL;
        voice->mixCoefficients = NULL;
        voice->sendMix = NULL;
        forge_zero(&voice->sends, sizeof(ForgeSendList));

        forge_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Copy send list */
    voice->sends.send_count = send_list->send_count;
    voice->sends.sends = (ForgeSend *)voice->audio->malloc_func(send_list->send_count * sizeof(ForgeSend));
    forge_memcpy(voice->sends.sends, send_list->sends, send_list->send_count * sizeof(ForgeSend));

    /* Allocate/reset default output matrix, mixer function, filters */
    voice->sendCoefficients = (float **)voice->audio->malloc_func(sizeof(float *) * send_list->send_count);
    voice->mixCoefficients = (float **)voice->audio->malloc_func(sizeof(float *) * send_list->send_count);
    voice->sendMix =
        (ForgeAudioMixCallback *)voice->audio->malloc_func(sizeof(ForgeAudioMixCallback) * send_list->send_count);

    for (uint32_t i = 0; i < send_list->send_count; i += 1) {
        if (send_list->sends[i].output_voice->type == FORGE_AUDIO_VOICE_MASTER) {
            outChannels = send_list->sends[i].output_voice->master.inputChannels;
        } else {
            outChannels = send_list->sends[i].output_voice->mix.inputChannels;
        }
        voice->sendCoefficients[i] =
            (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels * outChannels);
        voice->mixCoefficients[i] =
            (float *)voice->audio->malloc_func(sizeof(float) * voice->outputChannels * outChannels);

        forge_assert(voice->outputChannels > 0 && voice->outputChannels < 9);
        forge_assert(outChannels > 0 && outChannels < 9);
        forge_memcpy(voice->sendCoefficients[i],
                     forge_audio_internal_matrix_defaults[voice->outputChannels - 1][outChannels - 1],
                     voice->outputChannels * outChannels * sizeof(float));
        forge_audio_recalc_mix_matrix(voice, i);

        if (voice->outputChannels == 1) {
            if (outChannels == 1) {
                voice->sendMix[i] = forge_audio_mix_1in_1out_scalar;
            } else if (outChannels == 2) {
                voice->sendMix[i] = forge_audio_mix_1in_2out_scalar;
            } else if (outChannels == 6) {
                voice->sendMix[i] = forge_audio_mix_1in_6out_scalar;
            } else if (outChannels == 8) {
                voice->sendMix[i] = forge_audio_mix_1in_8out_scalar;
            } else {
                voice->sendMix[i] = forge_audio_mix_generic;
            }
        } else if (voice->outputChannels == 2) {
            if (outChannels == 1) {
                voice->sendMix[i] = forge_audio_mix_2in_1out_scalar;
            } else if (outChannels == 2) {
                voice->sendMix[i] = forge_audio_mix_2in_2out_scalar;
            } else if (outChannels == 6) {
                voice->sendMix[i] = forge_audio_mix_2in_6out_scalar;
            } else if (outChannels == 8) {
                voice->sendMix[i] = forge_audio_mix_2in_8out_scalar;
            } else {
                voice->sendMix[i] = forge_audio_mix_generic;
            }
        } else {
            voice->sendMix[i] = forge_audio_mix_generic;
        }

        if (send_list->sends[i].flags & FORGE_AUDIO_SEND_USEFILTER) {
            /* Allocate the whole send filter array if needed... */
            if (voice->sendFilter == NULL) {
                voice->sendFilter = (ForgeFilterParameters *)voice->audio->malloc_func(sizeof(ForgeFilterParameters) *
                                                                                       send_list->send_count);
            }
            if (voice->sendFilterState == NULL) {
                voice->sendFilterState = (ForgeAudioFilterState **)voice->audio->malloc_func(
                    sizeof(ForgeAudioFilterState *) * send_list->send_count);
                forge_zero(voice->sendFilterState, sizeof(ForgeAudioFilterState *) * send_list->send_count);
            }

            /* ... then fill in this send's filter data */
            voice->sendFilter[i].type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
            voice->sendFilter[i].frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
            voice->sendFilter[i].one_over_q = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
            voice->sendFilter[i].wet_dry_mix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
            voice->sendFilterState[i] =
                (ForgeAudioFilterState *)voice->audio->malloc_func(sizeof(ForgeAudioFilterState) * outChannels);
            forge_zero(voice->sendFilterState[i], sizeof(ForgeAudioFilterState) * outChannels);
        }
    }

    forge_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_set_effect_chain(ForgeVoice *voice, const ForgeEffectChain *effect_chain) {
    ForgeEffect *effect;
    ForgeResult result;
    uint32_t lockedEffects;
    uint32_t channelCount;
    ForgeVoiceDetails voiceDetails;
    ForgeEffectInfo info;
    ForgeAudioFormatExtensible srcFmt, dstFmt;
    ForgeEffectLockBuffer srcLockParams, dstLockParams;
    uint8_t hasEffectChain;

    LOG_API_ENTER(voice->audio)

    forge_voice_get_details(voice, &voiceDetails);
    hasEffectChain = (effect_chain != NULL && effect_chain->effect_count > 0);

    /* SetEffectChain must not change the number of output channels once the voice has been created */
    if (!hasEffectChain && voice->outputChannels != 0) {
        /* cannot remove an effect chain that changes the number of channels */
        if (voice->outputChannels != voiceDetails.input_channels) {
            LOG_ERROR(voice->audio, "%s", "Cannot remove effect chain that changes the number of channels")
            forge_assert(0 && "Cannot remove effect chain that changes the number of channels");
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
    }

    if (hasEffectChain && voice->outputChannels != 0) {
        uint32_t lst = effect_chain->effect_count - 1;

        /* new effect chain must have same number of output channels */
        if (voice->outputChannels != effect_chain->effects[lst].output_channels) {
            LOG_ERROR(voice->audio, "%s", "New effect chain must have same number of output channels as the old chain")
            forge_assert(0 && "New effect chain must have same number of output channels as the old chain");
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
    }

    forge_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)

    if (!hasEffectChain) {
        forge_audio_free_effect_chain(voice);
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

        /* The first source is the voice input data... */
        srcFmt.format.bits_per_sample = 32;
        srcFmt.format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        srcFmt.format.channels = voiceDetails.input_channels;
        srcFmt.format.sample_rate = voiceDetails.input_sample_rate;
        srcFmt.format.block_align = srcFmt.format.channels * (srcFmt.format.bits_per_sample / 8);
        srcFmt.format.average_bytes_per_second = srcFmt.format.sample_rate * srcFmt.format.block_align;
        srcFmt.format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        srcFmt.samples.valid_bits_per_sample = srcFmt.format.bits_per_sample;
        srcFmt.channel_mask = 0;
        forge_memcpy(srcFmt.format_id, forge_audio_format_id_ieee_float, FORGE_AUDIO_FORMAT_ID_SIZE);
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
                forge_platform_unlock_mutex(voice->effectLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
                LOG_API_EXIT(voice->audio)
                return result;
            }
            lockedEffects += 1;

            /* Okay, now this effect is the source and the next
             * effect will be the destination. Repeat until no
             * effects left.
             */
            forge_memcpy(&srcFmt, &dstFmt, sizeof(srcFmt));
        }

        forge_audio_free_effect_chain(voice);
        forge_audio_alloc_effect_chain(voice, effect_chain);

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

    forge_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_enable_effect(ForgeVoice *voice, uint32_t effect_index, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_enable_effect(voice, effect_index, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[effect_index].initial_state = 1;
    forge_platform_unlock_mutex(voice->effectLock);
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
        forge_audio_batch_queue_disable_effect(voice, effect_index, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[effect_index].initial_state = 0;
    forge_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_effect_state(ForgeVoice *voice, uint32_t effect_index, int32_t *enabled) {
    LOG_API_ENTER(voice->audio)
    forge_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    *enabled = voice->effects.desc[effect_index].initial_state;
    forge_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_effect_parameters(ForgeVoice *voice, uint32_t effect_index, const void *parameters,
                                              uint32_t parameters_byte_size, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_set_effect_parameters(voice, effect_index, parameters, parameters_byte_size, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (voice->effects.parameters == NULL) {
        LOG_ERROR(voice->audio, "Setting effect parameters on voice with no effect chain: %p", (void *)voice);
        return ForgeResultInvalidCall;
    }

    if (voice->effects.parameters[effect_index] == NULL) {
        voice->effects.parameters[effect_index] = voice->audio->malloc_func(parameters_byte_size);
        voice->effects.parameterSizes[effect_index] = parameters_byte_size;
    }
    forge_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.parameterSizes[effect_index] < parameters_byte_size) {
        voice->effects.parameters[effect_index] =
            voice->audio->realloc_func(voice->effects.parameters[effect_index], parameters_byte_size);
        voice->effects.parameterSizes[effect_index] = parameters_byte_size;
    }
    forge_memcpy(voice->effects.parameters[effect_index], parameters, parameters_byte_size);
    voice->effects.parameterUpdates[effect_index] = 1;
    forge_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_get_effect_parameters(ForgeVoice *voice, uint32_t effect_index, void *parameters,
                                              uint32_t parameters_byte_size) {
    ForgeEffect *effect;
    LOG_API_ENTER(voice->audio)
    forge_platform_lock_mutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    effect = voice->effects.desc[effect_index].effect;
    effect->get_parameters(effect, parameters, parameters_byte_size);
    forge_platform_unlock_mutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_set_filter_parameters(ForgeVoice *voice, const ForgeFilterParameters *parameters,
                                              ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_set_filter_parameters(voice, parameters, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (!(voice->flags & FORGE_AUDIO_VOICE_USEFILTER)) {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    forge_memcpy(&voice->filter, parameters, sizeof(ForgeFilterParameters));
    forge_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_filter_parameters(ForgeVoice *voice, ForgeFilterParameters *parameters) {
    LOG_API_ENTER(voice->audio)

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return;
    }

    if (!(voice->flags & FORGE_AUDIO_VOICE_USEFILTER)) {
        LOG_API_EXIT(voice->audio)
        return;
    }

    forge_platform_lock_mutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    forge_memcpy(parameters, &voice->filter, sizeof(ForgeFilterParameters));
    forge_platform_unlock_mutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                     const ForgeFilterParameters *parameters,
                                                     ForgeAudioBatchId batch_id) {
    uint32_t i;
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_set_output_filter_parameters(voice, destination_voice, parameters, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (destination_voice == NULL && voice->sends.send_count == 1) {
        destination_voice = voice->sends.sends[0].output_voice;
    }
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (!(voice->sends.sends[i].flags & FORGE_AUDIO_SEND_USEFILTER)) {
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Set the filter parameters, finally. */
    forge_memcpy(&voice->sendFilter[i], parameters, sizeof(ForgeFilterParameters));

    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                              ForgeFilterParameters *parameters) {
    uint32_t i;

    LOG_API_ENTER(voice->audio)

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        LOG_API_EXIT(voice->audio)
        return;
    }

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (destination_voice == NULL && voice->sends.send_count == 1) {
        destination_voice = voice->sends.sends[0].output_voice;
    }
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    if (!(voice->sends.sends[i].flags & FORGE_AUDIO_SEND_USEFILTER)) {
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Set the filter parameters, finally. */
    forge_memcpy(parameters, &voice->sendFilter[i], sizeof(ForgeFilterParameters));

    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_volume(ForgeVoice *voice, float volume, ForgeAudioBatchId batch_id) {
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_set_volume(voice, volume, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    forge_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    voice->volume = forge_clamp(volume, -FORGE_AUDIO_MAX_VOLUME_LEVEL, FORGE_AUDIO_MAX_VOLUME_LEVEL);

    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        forge_audio_recalc_mix_matrix(voice, i);
    }

    forge_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
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

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_set_channel_volumes(voice, channels, volumes, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
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

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    forge_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    forge_memcpy(voice->channelVolume, volumes, sizeof(float) * channels);

    for (uint32_t i = 0; i < voice->sends.send_count; i += 1) {
        forge_audio_recalc_mix_matrix(voice, i);
    }

    forge_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_channel_volumes(ForgeVoice *voice, uint32_t channels, float *volumes) {
    LOG_API_ENTER(voice->audio)
    forge_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    forge_memcpy(volumes, voice->channelVolume, sizeof(float) * channels);
    forge_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t source_channels,
                                          uint32_t destination_channels, const float *level_matrix,
                                          ForgeAudioBatchId batch_id) {
    uint32_t i;
    ForgeResult result = ForgeResultSuccess;
    LOG_API_ENTER(voice->audio)

    if (batch_id == FORGE_AUDIO_BATCH_ALL) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (batch_id != FORGE_AUDIO_BATCH_IMMEDIATE && voice->audio->active) {
        forge_audio_batch_queue_set_output_matrix(voice, destination_voice, source_channels, destination_channels,
                                                  level_matrix, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (destination_voice == NULL && voice->sends.send_count == 1) {
        destination_voice = voice->sends.sends[0].output_voice;
    }
    forge_assert(destination_voice != NULL);
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        result = ForgeResultInvalidCall;
        goto end;
    }

    /* Verify the Source/Destination channel count */
    if (source_channels != voice->outputChannels) {
        LOG_ERROR(voice->audio, "source_channels not equal to voice channel count: %p %d %d", (void *)voice,
                  source_channels, voice->outputChannels)
        result = ForgeResultInvalidCall;
        goto end;
    }

    if (destination_voice->type == FORGE_AUDIO_VOICE_MASTER) {
        if (destination_channels != destination_voice->master.inputChannels) {
            LOG_ERROR(voice->audio, "destination_channels not equal to master channel count: %p %d %d",
                      (void *)destination_voice, destination_channels, destination_voice->master.inputChannels)
            result = ForgeResultInvalidCall;
            goto end;
        }
    } else {
        if (destination_channels != destination_voice->mix.inputChannels) {
            LOG_ERROR(voice->audio, "destination_channels not equal to submix channel count: %p %d %d",
                      (void *)destination_voice, destination_channels, destination_voice->mix.inputChannels)
            result = ForgeResultInvalidCall;
            goto end;
        }
    }

    /* Set the matrix values, finally */
    forge_platform_lock_mutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    forge_memcpy(voice->sendCoefficients[i], level_matrix, sizeof(float) * source_channels * destination_channels);

    forge_audio_recalc_mix_matrix(voice, i);

    forge_platform_unlock_mutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

end:
    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t source_channels,
                                   uint32_t destination_channels, float *level_matrix) {
    uint32_t i;

    LOG_API_ENTER(voice->audio)
    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    for (i = 0; i < voice->sends.send_count; i += 1) {
        if (destination_voice == voice->sends.sends[i].output_voice) {
            break;
        }
    }
    if (i >= voice->sends.send_count) {
        LOG_ERROR(voice->audio, "Destination not attached to source: %p %p", (void *)voice, (void *)destination_voice)
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Verify the Source/Destination channel count */
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
        forge_assert(source_channels == voice->src.format->channels);
    } else {
        forge_assert(source_channels == voice->mix.inputChannels);
    }
    if (destination_voice->type == FORGE_AUDIO_VOICE_MASTER) {
        forge_assert(destination_channels == destination_voice->master.inputChannels);
    } else {
        forge_assert(destination_channels == destination_voice->mix.inputChannels);
    }

    /* Get the matrix values, finally */
    forge_memcpy(level_matrix, voice->sendCoefficients[i], sizeof(float) * source_channels * destination_channels);

    forge_platform_unlock_mutex(voice->sendLock);
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

    forge_platform_lock_mutex(audio->sourceLock);
    list = audio->sources;
    while (list != NULL) {
        source = (ForgeSourceVoice *)list->entry;
        for (i = 0; i < source->sends.send_count; i += 1)
            if (source->sends.sends[i].output_voice == voice) {
                ret = ForgeResultFailed;
                break;
            }
        if (ret)
            break;
        list = list->next;
    }
    forge_platform_unlock_mutex(audio->sourceLock);

    if (ret)
        return ret;

    forge_platform_lock_mutex(audio->submixLock);
    list = audio->submixes;
    while (list != NULL) {
        submix = (ForgeSubmixVoice *)list->entry;
        for (i = 0; i < submix->sends.send_count; i += 1)
            if (submix->sends.sends[i].output_voice == voice) {
                ret = ForgeResultFailed;
                break;
            }
        if (ret)
            break;
        list = list->next;
    }
    forge_platform_unlock_mutex(audio->submixLock);

    return ret;
}

static void destroy_voice(ForgeVoice *voice) {
    uint32_t i;

    /* Callers must reject incoming sends before destroying a voice; clear deferred commands here. */
    forge_audio_batch_clear_all_for_voice(voice);

    if (voice->type == FORGE_AUDIO_VOICE_SOURCE) {
#ifdef FORGE_AUDIO_DUMP_VOICES
        forge_audio_dump_voice_finalize((ForgeSourceVoice *)voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

        forge_platform_lock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
        while (voice == voice->audio->processingSource) {
            forge_platform_unlock_mutex(voice->audio->sourceLock);
            LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)
            forge_platform_lock_mutex(voice->audio->sourceLock);
            LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
        }
        forge_linked_list_remove_entry(&voice->audio->sources, voice, voice->audio->sourceLock,
                                       voice->audio->free_func);
        forge_platform_unlock_mutex(voice->audio->sourceLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

        voice->audio->free_func(voice->src.queued_buffers);
        voice->audio->free_func(voice->src.flush_buffers);
        voice->audio->free_func(voice->src.format);
        LOG_MUTEX_DESTROY(voice->audio, voice->src.bufferLock)
        forge_platform_destroy_mutex(voice->src.bufferLock);
#ifdef HAVE_WMADEC
        if (voice->src.wmadec) {
            ForgeAudio_WMADEC_free(voice);
        }
#endif /* HAVE_WMADEC */
    } else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX) {
        /* Remove submix from list */
        forge_linked_list_remove_entry(&voice->audio->submixes, voice, voice->audio->submixLock,
                                       voice->audio->free_func);

        /* Delete submix data */
        voice->audio->free_func(voice->mix.inputCache);
    } else if (voice->type == FORGE_AUDIO_VOICE_MASTER) {
        if (voice->audio->platform != NULL) {
            forge_platform_quit(voice->audio->platform);
            voice->audio->platform = NULL;
        }
        if (voice->master.effectCache != NULL) {
            voice->audio->free_func(voice->master.effectCache);
        }
        voice->audio->master = NULL;
    }

    if (voice->sendLock != NULL) {
        forge_platform_lock_mutex(voice->sendLock);
        LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
        for (i = 0; i < voice->sends.send_count; i += 1) {
            voice->audio->free_func(voice->sendCoefficients[i]);
        }
        if (voice->sendCoefficients != NULL) {
            voice->audio->free_func(voice->sendCoefficients);
        }
        for (i = 0; i < voice->sends.send_count; i += 1) {
            voice->audio->free_func(voice->mixCoefficients[i]);
        }
        if (voice->mixCoefficients != NULL) {
            voice->audio->free_func(voice->mixCoefficients);
        }
        if (voice->sendMix != NULL) {
            voice->audio->free_func(voice->sendMix);
        }
        if (voice->sendFilter != NULL) {
            voice->audio->free_func(voice->sendFilter);
        }
        if (voice->sendFilterState != NULL) {
            for (i = 0; i < voice->sends.send_count; i += 1) {
                if (voice->sendFilterState[i] != NULL) {
                    voice->audio->free_func(voice->sendFilterState[i]);
                }
            }
            voice->audio->free_func(voice->sendFilterState);
        }
        if (voice->sends.sends != NULL) {
            voice->audio->free_func(voice->sends.sends);
        }
        forge_platform_unlock_mutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->sendLock)
        forge_platform_destroy_mutex(voice->sendLock);
    }

    if (voice->effectLock != NULL) {
        forge_platform_lock_mutex(voice->effectLock);
        LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
        forge_audio_free_effect_chain(voice);
        forge_platform_unlock_mutex(voice->effectLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->effectLock)
        forge_platform_destroy_mutex(voice->effectLock);
    }

    if (voice->filterLock != NULL) {
        forge_platform_lock_mutex(voice->filterLock);
        LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
        if (voice->filterState != NULL) {
            voice->audio->free_func(voice->filterState);
        }
        forge_platform_unlock_mutex(voice->filterLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->filterLock)
        forge_platform_destroy_mutex(voice->filterLock);
    }

    if (voice->volumeLock != NULL) {
        forge_platform_lock_mutex(voice->volumeLock);
        LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
        if (voice->channelVolume != NULL) {
            voice->audio->free_func(voice->channelVolume);
        }
        forge_platform_unlock_mutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        LOG_MUTEX_DESTROY(voice->audio, voice->volumeLock)
        forge_platform_destroy_mutex(voice->volumeLock);
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
        forge_audio_batch_queue_start(voice, flags, batch_id);
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
        forge_audio_batch_queue_stop(voice, flags, batch_id);
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

ForgeResult forge_source_voice_submit_buffer(ForgeSourceVoice *voice, const ForgeBuffer *buffer,
                                             const ForgeBufferWMA *buffer_wma) {
    const uint32_t block_size = voice->src.format->block_align;
    uint32_t playBegin, playLength, loopBegin, loopLength, bufferLength;
    struct queued_buffer *entry;

    LOG_API_ENTER(voice->audio)
    LOG_INFO(voice->audio, "%p: {flags: 0x%x, audio_bytes: %u, audio_data: %p, Play: %u + %u, Loop: %u + %u x %u}",
             (void *)voice, buffer->flags, buffer->audio_bytes, (const void *)buffer->audio_data, buffer->play_begin,
             buffer->play_length, buffer->loop_begin, buffer->loop_length, buffer->loop_count)

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (block_size == 0) {
        LOG_ERROR(voice->audio, "%s", "Source voice has zero block alignment");
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (buffer_wma == NULL && voice->src.format->format_tag != FORGE_AUDIO_FORMAT_XMAUDIO2 &&
        buffer->audio_bytes % block_size != 0) {
        LOG_ERROR(voice->audio, "PCM source buffer audio_bytes must be a multiple of block_align: %u %% %u",
                  buffer->audio_bytes, block_size)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    /* Start off with whatever they just sent us... */
    playBegin = buffer->play_begin;
    playLength = buffer->play_length;
    loopBegin = buffer->loop_begin;
    loopLength = buffer->loop_length;

    /* "loop_begin/loop_length must be zero if loop_count is 0" */
    if (buffer->loop_count == 0 && (loopBegin > 0 || loopLength > 0)) {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (voice->src.format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2) {
        ForgeXMA2Format *fmtex = (ForgeXMA2Format *)voice->src.format;
        bufferLength = fmtex->dwSamplesEncoded;
    } else if (buffer_wma != NULL) {
        bufferLength = buffer_wma->decoded_packet_cumulative_bytes[buffer_wma->packet_count - 1] /
                       (voice->src.format->channels * voice->src.format->bits_per_sample / 8);
    } else {
        bufferLength = buffer->audio_bytes / voice->src.format->block_align;
    }

    if (playBegin + playLength > bufferLength || playBegin + playLength < playLength) {
        /* Reading past the end of the buffer, or begin + length overflow uint32_t, which
         * would also read past the end of the buffer. */
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (buffer->loop_count > 0 && buffer_wma == NULL && voice->src.format->format_tag != FORGE_AUDIO_FORMAT_XMAUDIO2) {
        uint32_t realPlayLength = playLength;
        uint32_t realLoopLength = loopLength;

        /* play_length Default */
        if (realPlayLength == 0) {
            realPlayLength = bufferLength - playBegin;
        }

        /* loop_length Default */
        if (realLoopLength == 0) {
            realLoopLength = playBegin + realPlayLength - loopBegin;
        }

        /* "The value of loop_begin must be less than play_begin + play_length" */
        if (loopBegin >= (playBegin + realPlayLength)) {
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }

        /* "The value of loop_begin + loop_length must be greater than play_begin
         * and less than play_begin + play_length"
         */
        if ((loopBegin + realLoopLength) <= playBegin || (loopBegin + realLoopLength) > (playBegin + realPlayLength)) {
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
    }

    if (buffer_wma != NULL || voice->src.format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2) {
        /* WMA only supports looping the whole buffer */
        loopBegin = 0;
        loopLength = playBegin + playLength;
    }

    forge_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    forge_array_reserve(voice->audio, (void **)&voice->src.queued_buffers, &voice->src.queued_buffers_capacity,
                        voice->src.queued_buffer_count + 1, sizeof(*voice->src.queued_buffers));

    entry = &voice->src.queued_buffers[voice->src.queued_buffer_count++];
    forge_memset(entry, 0, sizeof(*entry));
    forge_memcpy(&entry->buffer, buffer, sizeof(ForgeBuffer));
    entry->buffer.play_begin = playBegin;
    entry->buffer.play_length = playLength;
    entry->buffer.loop_begin = loopBegin;
    entry->buffer.loop_length = loopLength;
    if (buffer_wma != NULL) {
        forge_memcpy(&entry->bufferWMA, buffer_wma, sizeof(ForgeBufferWMA));
    } else {
        if (playLength != 0) {
            entry->play_bytes = playLength * block_size;
        } else {
            entry->play_bytes = buffer->audio_bytes - (playBegin * block_size);
        }

        if (loopLength != 0) {
            entry->loop_bytes = loopLength * block_size;
        } else {
            entry->loop_bytes = entry->play_bytes + (playBegin * block_size) - (loopBegin * block_size);
        }
    }

#ifdef FORGE_AUDIO_DUMP_VOICES
    /* dumping current buffer, append into "data" section */
    if (buffer->audio_data != NULL && entry->play_bytes > 0) {
        forge_audio_dump_voice_write_buffer(voice, buffer, buffer_wma, entry->play_bytes);
    }
#endif /* FORGE_AUDIO_DUMP_VOICES */

    if (voice->src.queued_buffer_count == 1) {
        voice->src.curBufferOffset = entry->buffer.play_begin;
    }

    LOG_INFO(voice->audio, "%p: appended buffer %p", (void *)voice, (void *)&entry->buffer)
    forge_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_flush_buffers(ForgeSourceVoice *voice) {
    size_t offset = 0;

    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    forge_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    forge_array_reserve(voice->audio, (void **)&voice->src.flush_buffers, &voice->src.flush_buffers_capacity,
                        voice->src.flush_buffer_count + voice->src.queued_buffer_count,
                        sizeof(*voice->src.flush_buffers));

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

    forge_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_end_stream(ForgeSourceVoice *voice) {
    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    forge_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0) {
        voice->src.queued_buffers[voice->src.queued_buffer_count - 1].buffer.flags |= FORGE_AUDIO_END_OF_STREAM;
    }

    forge_platform_unlock_mutex(voice->src.bufferLock);
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
        forge_audio_batch_queue_exit_loop(voice, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    forge_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0) {
        voice->src.queued_buffers[0].buffer.loop_count = 0;
    }

    forge_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_source_voice_get_state(ForgeSourceVoice *voice, ForgeVoiceState *voice_state, uint32_t flags) {
    LOG_API_ENTER(voice->audio)
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    forge_platform_lock_mutex(voice->src.bufferLock);
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

    forge_platform_unlock_mutex(voice->src.bufferLock);
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
        forge_audio_batch_queue_set_frequency_ratio(voice, ratio, batch_id);
        LOG_API_EXIT(voice->audio)
        return 0;
    }
    forge_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->flags & FORGE_AUDIO_VOICE_NOPITCH) {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    voice->src.freqRatio = forge_clamp(ratio, FORGE_AUDIO_MIN_FREQ_RATIO, voice->src.maxFreqRatio);
    LOG_API_EXIT(voice->audio)
    return 0;
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

    forge_platform_lock_mutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
    if (voice->src.queued_buffer_count != 0) {
        forge_platform_unlock_mutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }
    forge_platform_unlock_mutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

    voice->src.format->sample_rate = new_source_sample_rate;

    forge_platform_lock_mutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    outSampleRate = forge_audio_source_voice_output_rate(voice);

    newResampleSamples = (uint32_t)(forge_ceil((double)voice->audio->updateSize * (double)outSampleRate /
                                               (double)voice->audio->master->master.inputSampleRate));
    voice->src.resampleSamples = newResampleSamples;

    forge_platform_unlock_mutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    newDecodeSamples = forge_audio_source_decode_frame_count(newResampleSamples, voice->src.maxFreqRatio,
                                                             new_source_sample_rate, outSampleRate);
    forge_audio_resize_decode_cache(voice->audio,
                                    (newDecodeSamples + EXTRA_DECODE_PADDING) * voice->src.format->channels);
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

static inline ForgeAudioIOStreamOut *DumpVoices_fopen(const ForgeSourceVoice *voice, const ForgeAudioFormat *format,
                                                      const char *mode, const char *ext) {
    char loc[64];
    uint16_t format_tag = format->format_tag;
    uint16_t format_ex_tag = 0;
    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        const ForgeAudioFormatExtensible *format_ex = (const ForgeAudioFormatExtensible *)format;
        format_ex_tag = forge_audio_format_id_tag(format_ex->format_id);
    }
    forge_snprintf(loc, sizeof(loc), "FA_fmt_0x%04X_0x%04X_0x%016lX%s.wav", format_tag, format_ex_tag, (uint64_t)voice,
                   ext);
    ForgeAudioIOStreamOut *fileOut = forge_audio_fopen_out(loc, mode);
    return fileOut;
}

static inline void DumpVoices_finalize_section(const ForgeSourceVoice *voice, const ForgeAudioFormat *format,
                                               const char *section /* one of "data" or "dpds" */
) {
    /* data file only contains the real data bytes */
    ForgeAudioIOStreamOut *io_data = DumpVoices_fopen(voice, format, "rb", section);
    if (!io_data) {
        return;
    }
    forge_platform_lock_mutex((ForgeAudioMutex)io_data->lock);
    size_t file_size_data = io_data->size(io_data->data);
    if (file_size_data == 0) {
        /* nothing to do */
        /* close data file */
        forge_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
        forge_audio_close_out(io_data);
        return;
    }

    /* we got some data: append data section to main file */
    ForgeAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "ab", "");
    if (!io) {
        /* close data file */
        forge_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
        forge_audio_close_out(io_data);
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
    forge_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
    forge_audio_close_out(io_data);
    /* close main file */
    forge_platform_unlock_mutex((ForgeAudioMutex)io->lock);
    forge_audio_close_out(io);
}

static void forge_audio_dump_voice_init(const ForgeSourceVoice *voice) {
    const ForgeAudioFormat *format = voice->src.format;

    ForgeAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "wb", "");
    if (!io) {
        return;
    }
    forge_platform_lock_mutex((ForgeAudioMutex)io->lock);
    /* another GREAT ressource
     * https://wiki.multimedia.cx/index.php/Microsoft_xWMA
     */

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
    const char *formatFourcc = "WAVE";
    uint16_t format_tag = format->format_tag;
    /* special handling for WMAUDIO2 */
    if (format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE && extra_size >= 22) {
        const ForgeAudioFormatExtensible *format_ex = (const ForgeAudioFormatExtensible *)format;
        uint16_t format_ex_tag = forge_audio_format_id_tag(format_ex->format_id);
        if (format_ex_tag == FORGE_AUDIO_FORMAT_WMAUDIO2) {
            extra_size = 0;
            formatFourcc = "XWMA";
            format_tag = FORGE_AUDIO_FORMAT_WMAUDIO2;
        }
    }

    { /* RIFF chunk descriptor - 12 byte */
        /* ChunkID - 4 */
        io->write(io->data, "RIFF", 4, 1);
        /* ChunkSize - 4 */
        uint32_t filesize = 0; /* the real file size is written in finalize step */
        io->write(io->data, &filesize, 4, 1);
        /* format - 4 */
        io->write(io->data, formatFourcc, 4, 1);
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
    { /* dpds sub-chunk - optional - 8 bytes + bufferWMA uint32_t samples */
        /* create file to hold the bufferWMA samples */
        ForgeAudioIOStreamOut *io_dpds = DumpVoices_fopen(voice, format, "wb", "dpds");
        forge_audio_close_out(io_dpds);
        /* io_dpds file will be filled by SubmitBuffer */
    }
    { /* data sub-chunk - 8 bytes + data */
        /* create file to hold the data samples */
        ForgeAudioIOStreamOut *io_data = DumpVoices_fopen(voice, format, "wb", "data");
        forge_audio_close_out(io_data);
        /* io_data file will be filled by SubmitBuffer */
    }
    forge_platform_unlock_mutex((ForgeAudioMutex)io->lock);
    forge_audio_close_out(io);
}

static void forge_audio_dump_voice_finalize(const ForgeSourceVoice *voice) {
    const ForgeAudioFormat *format = voice->src.format;

    /* add dpds subchunk - optional */
    DumpVoices_finalize_section(voice, format, "dpds");
    /* add data subchunk */
    DumpVoices_finalize_section(voice, format, "data");

    /* open main file to update filesize */
    ForgeAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "r+b", "");
    if (!io) {
        return;
    }
    forge_platform_lock_mutex((ForgeAudioMutex)io->lock);
    size_t file_size = io->size(io->data);
    if (file_size >= 44) {
        /* update filesize */
        uint32_t chunk_size = (uint32_t)(file_size - 8);
        io->seek(io->data, 4, FORGE_AUDIO_SEEK_SET);
        io->write(io->data, &chunk_size, 4, 1);
    }
    forge_platform_unlock_mutex((ForgeAudioMutex)io->lock);
    forge_audio_close_out(io);
}

static void forge_audio_dump_voice_write_buffer(const ForgeSourceVoice *voice, const ForgeBuffer *buffer,
                                                const ForgeBufferWMA *buffer_wma, const uint32_t size) {
    ForgeAudioIOStreamOut *io_data = DumpVoices_fopen(voice, voice->src.format, "ab", "data");
    if (io_data == NULL) {
        return;
    }

    forge_platform_lock_mutex((ForgeAudioMutex)io_data->lock);
    if (buffer_wma != NULL) {
        /* dump encoded buffer contents */
        if (buffer_wma->packet_count > 0) {
            ForgeAudioIOStreamOut *io_dpds = DumpVoices_fopen(voice, voice->src.format, "ab", "dpds");
            if (io_dpds) {
                forge_platform_lock_mutex((ForgeAudioMutex)io_dpds->lock);
                /* write to dpds file */
                io_dpds->write(io_dpds->data, buffer_wma->decoded_packet_cumulative_bytes, sizeof(uint32_t),
                               buffer_wma->packet_count);
                forge_platform_unlock_mutex((ForgeAudioMutex)io_dpds->lock);
                forge_audio_close_out(io_dpds);
            }
            /* write buffer contents to data file */
            io_data->write(io_data->data, buffer->audio_data, sizeof(uint8_t), buffer->audio_bytes);
        }
    } else {
        /* dump unencoded buffer contents */
        uint16_t bytesPerFrame = (voice->src.format->channels * voice->src.format->bits_per_sample / 8);
        forge_assert(bytesPerFrame > 0);
        const void *audio_data_begin = buffer->audio_data + buffer->play_begin * bytesPerFrame;
        io_data->write(io_data->data, audio_data_begin, 1, size);
    }
    forge_platform_unlock_mutex((ForgeAudioMutex)io_data->lock);
    forge_audio_close_out(io_data);
}

#endif /* FORGE_AUDIO_DUMP_VOICES */
