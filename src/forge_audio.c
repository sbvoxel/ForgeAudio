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

#define MAKE_SUBFORMAT_GUID(guid, fmt) \
    ForgeGuid DATAFORMAT_SUBTYPE_##guid = \
    { \
        (uint16_t) (fmt), \
        0x0000, \
        0x0010, \
        { \
            0x80, \
            0x00, \
            0x00, \
            0xAA, \
            0x00, \
            0x38, \
            0x9B, \
            0x71 \
        } \
    }
MAKE_SUBFORMAT_GUID(PCM, 1);
MAKE_SUBFORMAT_GUID(IEEE_FLOAT, 3);
MAKE_SUBFORMAT_GUID(XMAUDIO2, FORGE_AUDIO_FORMAT_XMAUDIO2);
MAKE_SUBFORMAT_GUID(WMAUDIO2, FORGE_AUDIO_FORMAT_WMAUDIO2);
MAKE_SUBFORMAT_GUID(WMAUDIO3, FORGE_AUDIO_FORMAT_WMAUDIO3);
MAKE_SUBFORMAT_GUID(WMAUDIO_LOSSLESS, FORGE_AUDIO_FORMAT_WMAUDIO_LOSSLESS);
#undef MAKE_SUBFORMAT_GUID

#ifdef FORGE_AUDIO_DUMP_VOICES
static void ForgeAudio_DumpVoice_Init(const ForgeSourceVoice *voice);
static void ForgeAudio_DumpVoice_Finalize(const ForgeSourceVoice *voice);
static void ForgeAudio_DumpVoice_WriteBuffer(
    const ForgeSourceVoice *voice,
    const ForgeBuffer *buffer,
    const ForgeBufferWMA *buffer_wma,
    const uint32_t size
);
#endif /* FORGE_AUDIO_DUMP_VOICES */

static uint8_t ForgeAudio_Internal_ValidateUncompressedFormat(
    ForgeAudioEngine *audio,
    const ForgeAudioFormat *format
) {
    const ForgeAudioFormat *base = format;
    uint8_t isPCM = 0;
    uint8_t isFloat = 0;
    uint32_t expectedBlockAlign;

    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        const ForgeAudioFormatExtensible *ext = (const ForgeAudioFormatExtensible*) format;

        if (ForgeAudio_memcmp(&ext->SubFormat, &FORGE_AUDIO_SUBTYPE_PCM, sizeof(ForgeGuid)) == 0)
        {
            isPCM = 1;
        }
        else if (ForgeAudio_memcmp(&ext->SubFormat, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid)) == 0)
        {
            isFloat = 1;
        }
        else
        {
            return 1;
        }
    }
    else if (format->format_tag == FORGE_AUDIO_FORMAT_PCM)
    {
        isPCM = 1;
    }
    else if (format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT)
    {
        isFloat = 1;
    }
    else
    {
        return 1;
    }

    if (base->channels == 0 || base->block_align == 0 || base->bits_per_sample % 8 != 0)
    {
        LOG_ERROR(audio, "%s", "Invalid PCM source format block alignment");
        return 0;
    }

    if (isPCM &&    base->bits_per_sample != 8 &&
            base->bits_per_sample != 16 &&
            base->bits_per_sample != 24 &&
            base->bits_per_sample != 32    )
    {
        LOG_ERROR(audio, "Unsupported PCM bit depth: %u", base->bits_per_sample);
        return 0;
    }

    if (isFloat && base->bits_per_sample != 32)
    {
        LOG_ERROR(audio, "Unsupported float PCM bit depth: %u", base->bits_per_sample);
        return 0;
    }

    expectedBlockAlign = base->channels * (base->bits_per_sample / 8);
    if (base->block_align != expectedBlockAlign)
    {
        LOG_ERROR(
            audio,
            "Invalid PCM block alignment: got %u, expected %u",
            base->block_align,
            expectedBlockAlign
        )
        return 0;
    }

    return 1;
}

/* ForgeAudioEngine Version */

uint32_t forge_audio_linked_version(void)
{
    return FORGE_AUDIO_COMPILED_VERSION;
}

/* ForgeAudioEngine Interface */

ForgeResult forge_audio_create(
    ForgeAudioEngine **engine,
    uint32_t Flags
) {
    return forge_audio_create_with_allocator(
        engine,
        Flags,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

static ForgeResult engine_construct_with_allocator(
    ForgeAudioEngine **engine,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);

static ForgeResult engine_initialize(
    ForgeAudioEngine *audio,
    uint32_t Flags
);

ForgeResult forge_audio_create_with_allocator(
    ForgeAudioEngine **engine,
    uint32_t Flags,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    engine_construct_with_allocator(
        engine,
        customMalloc,
        customFree,
        customRealloc
    );
    engine_initialize(*engine, Flags);
    return 0;
}

static ForgeResult engine_construct_with_allocator(
    ForgeAudioEngine **engine,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    ForgeDebugConfiguration debugInit = {0};
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    ForgeAudio_PlatformAddRef();
    *engine = (ForgeAudioEngine*) customMalloc(sizeof(ForgeAudioEngine));
    ForgeAudio_zero(*engine, sizeof(ForgeAudioEngine));
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    forge_audio_set_debug_configuration(*engine, &debugInit, NULL);
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    (*engine)->sourceLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->sourceLock)
    (*engine)->submixLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->submixLock)
    (*engine)->callbackLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->callbackLock)
    (*engine)->operationLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*engine), (*engine)->operationLock)
    (*engine)->malloc_func = customMalloc;
    (*engine)->free_func = customFree;
    (*engine)->realloc_func = customRealloc;
    return 0;
}

static void destroy_voice(ForgeVoice *voice);

void forge_audio_destroy(ForgeAudioEngine *audio)
{
	ForgeVoice *voice;

	LOG_API_ENTER(audio)

	while (audio->sources)
	{
		voice = (ForgeSourceVoice*) audio->sources->entry;
		destroy_voice(voice);
	}
	while (audio->submixes)
	{
		voice = (ForgeSourceVoice*) audio->submixes->entry;
		destroy_voice(voice);
	}
	if (audio->master)
		destroy_voice(audio->master);
	ForgeAudio_OperationSet_ClearAll(audio);
	forge_audio_stop_engine(audio);
	audio->free_func(audio->decodeCache);
	audio->free_func(audio->resampleCache);
	audio->free_func(audio->effectChainCache);
	LOG_MUTEX_DESTROY(audio, audio->sourceLock)
	ForgeAudio_PlatformDestroyMutex(audio->sourceLock);
	LOG_MUTEX_DESTROY(audio, audio->submixLock)
	ForgeAudio_PlatformDestroyMutex(audio->submixLock);
	LOG_MUTEX_DESTROY(audio, audio->callbackLock)
	ForgeAudio_PlatformDestroyMutex(audio->callbackLock);
	LOG_MUTEX_DESTROY(audio, audio->operationLock)
	ForgeAudio_PlatformDestroyMutex(audio->operationLock);
	audio->free_func(audio);
	ForgeAudio_PlatformRelease();
}

ForgeResult forge_audio_get_device_count(ForgeAudioEngine *audio, uint32_t *count)
{
    LOG_API_ENTER(audio)
    *count = ForgeAudio_PlatformGetDeviceCount();
    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_get_device_details(
    ForgeAudioEngine *audio,
    uint32_t Index,
    ForgeDeviceDetails *device_details
) {
    uint32_t result;
    LOG_API_ENTER(audio)
    result = ForgeAudio_PlatformGetDeviceDetails(Index, device_details);
    LOG_API_EXIT(audio)
    return result;
}

static ForgeResult engine_initialize(
    ForgeAudioEngine *audio,
    uint32_t Flags
) {
	LOG_API_ENTER(audio)
	ForgeAudio_assert((Flags & ~(FORGE_AUDIO_DEBUG_ENGINE | FORGE_AUDIO_1024_QUANTUM)) == 0);

    audio->initFlags = Flags;

    /* FIXME: This is lazy... */
    audio->decodeCache = (float*) audio->malloc_func(sizeof(float));
    audio->resampleCache = (float*) audio->malloc_func(sizeof(float));
    audio->decodeSamples = 1;
    audio->resampleSamples = 1;

    forge_audio_start_engine(audio);
    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_register_callback(
    ForgeAudioEngine *audio,
    ForgeEngineCallback *callback
) {
    LOG_API_ENTER(audio)
    LinkedList_AddEntry(
        &audio->callbacks,
        callback,
        audio->callbackLock,
        audio->malloc_func
    );
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_unregister_callback(
    ForgeAudioEngine *audio,
    ForgeEngineCallback *callback
) {
    LOG_API_ENTER(audio)
    LinkedList_RemoveEntry(
        &audio->callbacks,
        callback,
        audio->callbackLock,
        audio->free_func
    );
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_create_source_voice(
    ForgeAudioEngine *audio,
    ForgeSourceVoice **source_voice,
    const ForgeAudioFormat *source_format,
    uint32_t Flags,
    float MaxFrequencyRatio,
    ForgeVoiceCallback *callback,
    const ForgeSendList *send_list,
    const ForgeEffectChain *effect_chain
) {
    LOG_API_ENTER(audio)
    LOG_FORMAT(audio, source_format)

    if (send_list == NULL && audio->master == NULL)
    {
        LOG_ERROR(audio, "%s", "CreateSourceVoice called before mastering voice was initialized");
        return ForgeResultInvalidCall;
    }

    if (!ForgeAudio_Internal_ValidateUncompressedFormat(audio, source_format))
    {
        return ForgeResultInvalidCall;
    }

    *source_voice = (ForgeSourceVoice*) audio->malloc_func(sizeof(ForgeVoice));
    ForgeAudio_zero(*source_voice, sizeof(ForgeSourceVoice));
    (*source_voice)->audio = audio;
    (*source_voice)->type = FORGE_AUDIO_VOICE_SOURCE;
    (*source_voice)->flags = Flags;
    (*source_voice)->filter.Type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    (*source_voice)->filter.Frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
    (*source_voice)->filter.OneOverQ = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
    (*source_voice)->filter.WetDryMix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    (*source_voice)->sendLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->sendLock)
    (*source_voice)->effectLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->effectLock)
    (*source_voice)->filterLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->filterLock)
    (*source_voice)->volumeLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->volumeLock)

    /* Source Properties */
    ForgeAudio_assert(MaxFrequencyRatio <= FORGE_AUDIO_MAX_FREQ_RATIO);
    (*source_voice)->src.maxFreqRatio = MaxFrequencyRatio;

    if (    source_format->format_tag == FORGE_AUDIO_FORMAT_PCM ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO2 ||
        source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO3    )
    {
        ForgeAudioFormatExtensible *fmtex = (ForgeAudioFormatExtensible*) audio->malloc_func(
            sizeof(ForgeAudioFormatExtensible)
        );
        /* convert PCM to EXTENSIBLE */
        fmtex->Format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        fmtex->Format.channels = source_format->channels;
        fmtex->Format.sample_rate = source_format->sample_rate;
        fmtex->Format.average_bytes_per_second = source_format->average_bytes_per_second;
        fmtex->Format.block_align = source_format->block_align;
        fmtex->Format.bits_per_sample = source_format->bits_per_sample;
        fmtex->Format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        fmtex->Samples.valid_bits_per_sample = source_format->bits_per_sample;
        fmtex->channel_mask = 0;
        if (source_format->format_tag == FORGE_AUDIO_FORMAT_PCM)
        {
            ForgeAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_PCM, sizeof(ForgeGuid));
        }
        else if (source_format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT)
        {
            ForgeAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid));
        }
        else if (source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO2)
        {
            ForgeAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_WMAUDIO2, sizeof(ForgeGuid));
        }
        else if (source_format->format_tag == FORGE_AUDIO_FORMAT_WMAUDIO3)
        {
            ForgeAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_WMAUDIO3, sizeof(ForgeGuid));
        }
        (*source_voice)->src.format = &fmtex->Format;
    }
    else if (source_format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        ForgeXMA2Format *fmtex = (ForgeXMA2Format*) audio->malloc_func(
            sizeof(ForgeXMA2Format)
        );

        /* Copy what we can, ideally the sizes match! */
        size_t extra_size = sizeof(ForgeAudioFormat) + source_format->extra_size;
        ForgeAudio_memcpy(
            fmtex,
            source_format,
            ForgeAudio_min(extra_size, sizeof(ForgeXMA2Format))
        );
        if (extra_size < sizeof(ForgeXMA2Format))
        {
            ForgeAudio_zero(
                ((uint8_t*) fmtex) + extra_size,
                sizeof(ForgeXMA2Format) - extra_size
            );
        }

        /* Preserve existing input-validation behavior. */
        fmtex->wfx.extra_size = sizeof(ForgeXMA2Format) - sizeof(ForgeAudioFormat);
        (*source_voice)->src.format = &fmtex->wfx;
    }
    else
    {
        /* direct copy anything else */
        (*source_voice)->src.format = (ForgeAudioFormat*) audio->malloc_func(
            sizeof(ForgeAudioFormat) + source_format->extra_size
        );
        ForgeAudio_memcpy(
            (*source_voice)->src.format,
            source_format,
            sizeof(ForgeAudioFormat) + source_format->extra_size
        );
    }

    (*source_voice)->src.callback = callback;
    (*source_voice)->src.active = 0;
    (*source_voice)->src.freqRatio = 1.0f;
    (*source_voice)->src.totalSamples = 0;
    (*source_voice)->src.bufferLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*source_voice)->src.bufferLock)

    if ((*source_voice)->src.format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        ForgeAudioFormatExtensible *fmtex = (ForgeAudioFormatExtensible*) (*source_voice)->src.format;

        #define COMPARE_GUID(type) \
            (ForgeAudio_memcmp( \
                &fmtex->SubFormat, \
                &DATAFORMAT_SUBTYPE_##type, \
                sizeof(ForgeGuid) \
            ) == 0)
        if (COMPARE_GUID(PCM))
        {
            #define DECODER(bit) \
                if (fmtex->Format.bits_per_sample == bit) \
                { \
                    (*source_voice)->src.decode = ForgeAudio_Internal_DecodePCM##bit; \
                }
            DECODER(16)
            else DECODER(8)
            else DECODER(24)
            else DECODER(32)
            else
            {
                LOG_ERROR(
                    audio,
                    "Unrecognized bits_per_sample: %d",
                    fmtex->Format.bits_per_sample
                )
                ForgeAudio_assert(0 && "Unrecognized bits_per_sample!");
            }
            #undef DECODER
        }
        else if (COMPARE_GUID(IEEE_FLOAT))
        {
            /* FIXME: Weird behavior!
             * Prototype creates a source with the IEEE_FLOAT tag,
             * but it's actually PCM16. It seems to prioritize
             * bits_per_sample over the format tag. Not sure if we
             * should fold this section into the section above...?
             * -flibit
             */
            if (fmtex->Format.bits_per_sample == 16)
            {
                (*source_voice)->src.decode = ForgeAudio_Internal_DecodePCM16;
            }
            else
            {
                (*source_voice)->src.decode = ForgeAudio_Internal_DecodePCM32F;
            }
        }
        else if (    COMPARE_GUID(WMAUDIO2) ||
                COMPARE_GUID(WMAUDIO3) ||
                COMPARE_GUID(WMAUDIO_LOSSLESS)    )
        {
        }
        else
        {
            ForgeAudio_assert(0 && "Unsupported WAVEFORMATEXTENSIBLE subtype!");
        }
        #undef COMPARE_GUID
    }
    else if ((*source_voice)->src.format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        ForgeAudio_assert(0 && "XMA2 is not supported!");
        (*source_voice)->src.decode = ForgeAudio_Internal_DecodeWMAERROR;
    }
    else
    {
        ForgeAudio_assert(0 && "Unsupported format tag!");
    }

    if ((*source_voice)->src.format->channels == 1)
    {
        (*source_voice)->src.resample = ForgeAudio_Internal_ResampleMono;
    }
    else if ((*source_voice)->src.format->channels == 2)
    {
        (*source_voice)->src.resample = ForgeAudio_Internal_ResampleStereo;
    }
    else
    {
        (*source_voice)->src.resample = ForgeAudio_Internal_ResampleGeneric;
    }

    (*source_voice)->src.curBufferOffset = 0;

    /* Sends/Effects */
    ForgeAudio_Internal_VoiceOutputFrequency(*source_voice, send_list);
    forge_voice_set_effect_chain(*source_voice, effect_chain);

    /* Default Levels */
    (*source_voice)->volume = 1.0f;
    (*source_voice)->channelVolume = (float*) audio->malloc_func(
        sizeof(float) * (*source_voice)->outputChannels
    );
    for (uint32_t i = 0; i < (*source_voice)->outputChannels; i += 1)
    {
        (*source_voice)->channelVolume[i] = 1.0f;
    }

    forge_voice_set_outputs(*source_voice, send_list);

    /* Filters */
    if (Flags & FORGE_AUDIO_VOICE_USEFILTER)
    {
        (*source_voice)->filterState = (ForgeAudioFilterState*) audio->malloc_func(
            sizeof(ForgeAudioFilterState) * (*source_voice)->src.format->channels
        );
        ForgeAudio_zero(
            (*source_voice)->filterState,
            sizeof(ForgeAudioFilterState) * (*source_voice)->src.format->channels
        );
    }

    /* Sample Storage */
    (*source_voice)->src.decodeSamples = (uint32_t) (ForgeAudio_ceil(
        (double) audio->updateSize *
        (double) MaxFrequencyRatio *
        (double) (*source_voice)->src.format->sample_rate /
        (double) audio->master->master.inputSampleRate
    )) + EXTRA_DECODE_PADDING * (*source_voice)->src.format->channels;
    ForgeAudio_Internal_ResizeDecodeCache(
        audio,
        ((*source_voice)->src.decodeSamples + EXTRA_DECODE_PADDING) * (*source_voice)->src.format->channels
    );

    LOG_INFO(audio, "-> %p", (void*) (*source_voice))

	LOG_INFO(audio, "-> %p", (void*) (*source_voice))

	/* Add to list, finally. */
	LinkedList_PrependEntry(
		&audio->sources,
		*source_voice,
		audio->sourceLock,
		audio->malloc_func
	);

#ifdef FORGE_AUDIO_DUMP_VOICES
    ForgeAudio_DumpVoice_Init(*source_voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

    LOG_API_EXIT(audio)
    return 0;
}

ForgeResult forge_audio_create_submix_voice(
    ForgeAudioEngine *audio,
    ForgeSubmixVoice **submix_voice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint32_t ProcessingStage,
    const ForgeSendList *send_list,
    const ForgeEffectChain *effect_chain
) {
    LOG_API_ENTER(audio)

    if (send_list == NULL && audio->master == NULL)
    {
        LOG_ERROR(audio, "%s", "CreateSubmixVoice called before mastering voice was initialized");
        return ForgeResultInvalidCall;
    }

    *submix_voice = (ForgeSubmixVoice*) audio->malloc_func(sizeof(ForgeVoice));
    ForgeAudio_zero(*submix_voice, sizeof(ForgeSubmixVoice));
    (*submix_voice)->audio = audio;
    (*submix_voice)->type = FORGE_AUDIO_VOICE_SUBMIX;
    (*submix_voice)->flags = Flags;
    (*submix_voice)->filter.Type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    (*submix_voice)->filter.Frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
    (*submix_voice)->filter.OneOverQ = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
    (*submix_voice)->filter.WetDryMix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    (*submix_voice)->sendLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->sendLock)
    (*submix_voice)->effectLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->effectLock)
    (*submix_voice)->filterLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->filterLock)
    (*submix_voice)->volumeLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*submix_voice)->volumeLock)

    /* Submix Properties */
    (*submix_voice)->mix.inputChannels = InputChannels;
    (*submix_voice)->mix.inputSampleRate = InputSampleRate;
    (*submix_voice)->mix.processingStage = ProcessingStage;

    /* Resampler */
    if (InputChannels == 1)
    {
        (*submix_voice)->mix.resample = ForgeAudio_Internal_ResampleMono;
    }
    else if (InputChannels == 2)
    {
        (*submix_voice)->mix.resample = ForgeAudio_Internal_ResampleStereo;
    }
    else
    {
        (*submix_voice)->mix.resample = ForgeAudio_Internal_ResampleGeneric;
    }

    /* Sample Storage */
    (*submix_voice)->mix.inputSamples = ((uint32_t) ForgeAudio_ceil(
        audio->updateSize *
        (double) InputSampleRate /
        (double) audio->master->master.inputSampleRate
    ) + EXTRA_DECODE_PADDING) * InputChannels;
    (*submix_voice)->mix.inputCache = (float*) audio->malloc_func(
        sizeof(float) * (*submix_voice)->mix.inputSamples
    );
    ForgeAudio_zero( /* Zero this now, for the first update */
        (*submix_voice)->mix.inputCache,
        sizeof(float) * (*submix_voice)->mix.inputSamples
    );

    /* Sends/Effects */
    ForgeAudio_Internal_VoiceOutputFrequency(*submix_voice, send_list);
    forge_voice_set_effect_chain(*submix_voice, effect_chain);

    /* Default Levels */
    (*submix_voice)->volume = 1.0f;
    (*submix_voice)->channelVolume = (float*) audio->malloc_func(
        sizeof(float) * (*submix_voice)->outputChannels
    );
    for (uint32_t i = 0; i < (*submix_voice)->outputChannels; i += 1)
    {
        (*submix_voice)->channelVolume[i] = 1.0f;
    }

    forge_voice_set_outputs(*submix_voice, send_list);

    /* Filters */
    if (Flags & FORGE_AUDIO_VOICE_USEFILTER)
    {
        (*submix_voice)->filterState = (ForgeAudioFilterState*) audio->malloc_func(
            sizeof(ForgeAudioFilterState) * InputChannels
        );
        ForgeAudio_zero(
            (*submix_voice)->filterState,
            sizeof(ForgeAudioFilterState) * InputChannels
        );
    }

	/* Add to list, finally. */
	ForgeAudio_Internal_InsertSubmixSorted(
		&audio->submixes,
		*submix_voice,
		audio->submixLock,
		audio->malloc_func
	);

	LOG_API_EXIT(audio)
	return 0;
}

ForgeResult forge_audio_create_master_voice(
    ForgeAudioEngine *audio,
    ForgeMasterVoice **mastering_voice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint32_t DeviceIndex,
    const ForgeEffectChain *effect_chain
) {
    LOG_API_ENTER(audio)

    /* For now we only support one allocated master voice at a time */
    ForgeAudio_assert(audio->master == NULL);

    if (    InputChannels == FORGE_AUDIO_DEFAULT_CHANNELS ||
        InputSampleRate == FORGE_AUDIO_DEFAULT_SAMPLERATE    )
    {
        ForgeDeviceDetails details;
        if (forge_audio_get_device_details(audio, DeviceIndex, &details) != 0)
        {
            return ForgeResultInvalidCall;
        }
        if (InputChannels == FORGE_AUDIO_DEFAULT_CHANNELS)
        {
            InputChannels = details.OutputFormat.Format.channels;
        }
        if (InputSampleRate == FORGE_AUDIO_DEFAULT_SAMPLERATE)
        {
            InputSampleRate = details.OutputFormat.Format.sample_rate;
        }
    }

    *mastering_voice = (ForgeMasterVoice*) audio->malloc_func(sizeof(ForgeVoice));
    ForgeAudio_zero(*mastering_voice, sizeof(ForgeMasterVoice));
    (*mastering_voice)->audio = audio;
    (*mastering_voice)->type = FORGE_AUDIO_VOICE_MASTER;
    (*mastering_voice)->flags = Flags;
    (*mastering_voice)->effectLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->effectLock)
    (*mastering_voice)->volumeLock = ForgeAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*mastering_voice)->volumeLock)

    /* Default Levels */
    (*mastering_voice)->volume = 1.0f;

    /* Master Properties */
    (*mastering_voice)->master.inputChannels = InputChannels;
    (*mastering_voice)->master.inputSampleRate = InputSampleRate;

    /* Sends/Effects */
    ForgeAudio_zero(&(*mastering_voice)->sends, sizeof(ForgeSendList));
    forge_voice_set_effect_chain(*mastering_voice, effect_chain);

    /* This is now safe enough to assign */
    audio->master = *mastering_voice;

    /* Build the device format.
     * The most unintuitive part of this is the use of outputChannels
     * instead of master.inputChannels. Bizarrely, the effect chain can
     * dictate the _actual_ output channel count, and when the channel count
     * mismatches, we have to add a staging buffer for effects to process on
     * before ultimately copying the final result to the device. ARGH.
     */
    WriteWaveFormatExtensible(
        &audio->mixFormat,
        audio->master->outputChannels,
        audio->master->master.inputSampleRate,
        &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT
    );

	/* Platform Device */
	ForgeAudio_PlatformInit(
		audio,
		audio->initFlags,
		DeviceIndex,
		&audio->mixFormat,
		&audio->updateSize,
		&audio->platform
	);
	if (audio->platform == NULL)
	{
		forge_voice_destroy(*mastering_voice);
		*mastering_voice = NULL;

        /* Not the best code, but it's probably true? */
        return ForgeResultDeviceInvalidated;
    }
    audio->master->outputChannels = audio->mixFormat.Format.channels;
    audio->master->master.inputSampleRate = audio->mixFormat.Format.sample_rate;

    /* Effect Chain Cache */
    if ((*mastering_voice)->master.inputChannels != (*mastering_voice)->outputChannels)
    {
        (*mastering_voice)->master.effectCache = (float*) audio->malloc_func(
            sizeof(float) *
            audio->updateSize *
            (*mastering_voice)->master.inputChannels
        );
    }

    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_set_engine_procedure(
    ForgeAudioEngine *audio,
    ForgeEngineProcedure clientEngineProc,
    void *user
) {
    LOG_API_ENTER(audio)
    audio->client_engine_proc = clientEngineProc;
    audio->clientEngineUser = user;
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_start_engine(ForgeAudioEngine *audio)
{
    LOG_API_ENTER(audio)
    audio->active = 1;
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_stop_engine(ForgeAudioEngine *audio)
{
    LOG_API_ENTER(audio)
    audio->active = 0;
    ForgeAudio_OperationSet_CommitAll(audio);
    ForgeAudio_OperationSet_Execute(audio);
    LOG_API_EXIT(audio)
}

ForgeResult forge_audio_commit_operation_set(ForgeAudioEngine *audio, uint32_t OperationSet)
{
    LOG_API_ENTER(audio)
    if (OperationSet == FORGE_AUDIO_COMMIT_ALL)
    {
        ForgeAudio_OperationSet_CommitAll(audio);
    }
    else
    {
        ForgeAudio_OperationSet_Commit(audio, OperationSet);
    }
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_get_performance_data(
    ForgeAudioEngine *audio,
    ForgePerformanceData *perf_data
) {
    LinkedList *list;
    ForgeSourceVoice *source;

    LOG_API_ENTER(audio)

    ForgeAudio_zero(perf_data, sizeof(ForgePerformanceData));

    ForgeAudio_PlatformLockMutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    list = audio->sources;
    while (list != NULL)
    {
        source = (ForgeSourceVoice*) list->entry;
        perf_data->TotalSourceVoiceCount += 1;
        if (source->src.active)
        {
            perf_data->ActiveSourceVoiceCount += 1;
        }
        list = list->next;
    }
    ForgeAudio_PlatformUnlockMutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)

    ForgeAudio_PlatformLockMutex(audio->submixLock);
    LOG_MUTEX_LOCK(audio, audio->submixLock)
    list = audio->submixes;
    while (list != NULL)
    {
        perf_data->ActiveSubmixVoiceCount += 1;
        list = list->next;
    }
    ForgeAudio_PlatformUnlockMutex(audio->submixLock);
    LOG_MUTEX_UNLOCK(audio, audio->submixLock)

    if (audio->master != NULL)
    {
        /* estimate, should use real latency from platform */
        perf_data->CurrentLatencyInSamples = 2 * audio->updateSize;
    }

    LOG_API_EXIT(audio)
}

void forge_audio_set_debug_configuration(
    ForgeAudioEngine *audio,
    ForgeDebugConfiguration *debug_configuration,
    void* reserved
) {
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    char *env;

    LOG_API_ENTER(audio)

    ForgeAudio_memcpy(
        &audio->debug,
        debug_configuration,
        sizeof(ForgeDebugConfiguration)
    );

    env = ForgeAudio_getenv("FORGE_AUDIO_LOG_EVERYTHING");
    if (env != NULL && *env == '1')
    {
        audio->debug.TraceMask = (
            FORGE_AUDIO_LOG_ERRORS |
            FORGE_AUDIO_LOG_WARNINGS |
            FORGE_AUDIO_LOG_INFO |
            FORGE_AUDIO_LOG_DETAIL |
            FORGE_AUDIO_LOG_API_CALLS |
            FORGE_AUDIO_LOG_FUNC_CALLS |
            FORGE_AUDIO_LOG_TIMING |
            FORGE_AUDIO_LOG_LOCKS |
            FORGE_AUDIO_LOG_MEMORY |
            FORGE_AUDIO_LOG_STREAMING
        );
        audio->debug.LogThreadID = 1;
        audio->debug.LogFunctionName = 1;
        audio->debug.LogTiming = 1;
    }

    #define CHECK_ENV(type) \
        env = ForgeAudio_getenv("FORGE_AUDIO_LOG_" #type); \
        if (env != NULL) \
        { \
            if (*env == '1') \
            { \
                audio->debug.TraceMask |= FORGE_AUDIO_LOG_##type; \
            } \
            else \
            { \
                audio->debug.TraceMask &= ~FORGE_AUDIO_LOG_##type; \
            } \
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
    #define CHECK_ENV(envvar, boolvar) \
        env = ForgeAudio_getenv("FORGE_AUDIO_LOG_LOG" #envvar); \
        if (env != NULL) \
        { \
            audio->debug.Log##boolvar = (*env == '1'); \
        }
    CHECK_ENV(THREADID, ThreadID)
    CHECK_ENV(FILELINE, Fileline)
    CHECK_ENV(FUNCTIONNAME, FunctionName)
    CHECK_ENV(TIMING, Timing)
    #undef CHECK_ENV

    LOG_API_EXIT(audio)
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
}

void forge_audio_get_processing_quantum(
    ForgeAudioEngine *audio,
    uint32_t *quantumNumerator,
    uint32_t *quantumDenominator
) {
    ForgeAudio_assert(audio->master != NULL);
    if (quantumNumerator != NULL)
    {
        *quantumNumerator = audio->updateSize;
    }
    if (quantumDenominator != NULL)
    {
        *quantumDenominator = audio->master->master.inputSampleRate;
    }
}

/* ForgeVoice Interface */

static void ForgeAudio_RecalcMixMatrix(ForgeVoice *voice, uint32_t sendIndex)
{
    uint32_t oChan, s, d;
    ForgeVoice *out = voice->sends.sends[sendIndex].output_voice;
    float volume, *matrix = voice->mixCoefficients[sendIndex];

    if (voice->type == FORGE_AUDIO_VOICE_SUBMIX)
    {
        volume = 1.f;
    }
    else
    {
        volume = voice->volume;
    }

    if (out->type == FORGE_AUDIO_VOICE_MASTER)
    {
        oChan = out->master.inputChannels;
    }
    else
    {
        oChan = out->mix.inputChannels;
    }

    for (d = 0; d < oChan; d += 1)
    {
        for (s = 0; s < voice->outputChannels; s += 1)
        {
            matrix[d * voice->outputChannels + s] = volume *
                voice->channelVolume[s] *
                voice->sendCoefficients[sendIndex][d * voice->outputChannels + s];
        }
    }
}

void forge_voice_get_details(
    ForgeVoice *voice,
    ForgeVoiceDetails *voice_details
) {
    LOG_API_ENTER(voice->audio)

    voice_details->CreationFlags = voice->flags;
    voice_details->ActiveFlags = voice->flags;
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
    {
        voice_details->InputChannels = voice->src.format->channels;
        voice_details->InputSampleRate = voice->src.format->sample_rate;
    }
    else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX)
    {
        voice_details->InputChannels = voice->mix.inputChannels;
        voice_details->InputSampleRate = voice->mix.inputSampleRate;
    }
    else if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        voice_details->InputChannels = voice->master.inputChannels;
        voice_details->InputSampleRate = voice->master.inputSampleRate;
    }
    else
    {
        ForgeAudio_assert(0 && "Unknown voice type!");
    }

    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_outputs(
    ForgeVoice *voice,
    const ForgeSendList *send_list
) {
    uint32_t sendRate, nextRate, outChannels;
    ForgeSendList defaultSends;
    ForgeSend defaultSend;

    LOG_API_ENTER(voice->audio)

    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (send_list != NULL && send_list->SendCount > 1)
    {
        if (send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_SOURCE)
        {
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
        sendRate = (send_list->sends[0].output_voice->type == FORGE_AUDIO_VOICE_MASTER) ?
            send_list->sends[0].output_voice->master.inputSampleRate :
            send_list->sends[0].output_voice->mix.inputSampleRate;
        for (uint32_t i = 0; i < send_list->SendCount; i += 1)
        {
            nextRate = (send_list->sends[i].output_voice->type == FORGE_AUDIO_VOICE_MASTER) ?
                send_list->sends[i].output_voice->master.inputSampleRate :
                send_list->sends[i].output_voice->mix.inputSampleRate;
            if (nextRate != sendRate)
            {
                LOG_API_EXIT(voice->audio)
                return ForgeResultInvalidCall;
            }
        }
    }

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (ForgeAudio_Internal_VoiceOutputFrequency(voice, send_list) != 0)
    {
        LOG_ERROR(
            voice->audio,
            "%s",
            "Changing the sample rate while an effect chain is attached is invalid!"
        )
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    /* FIXME: This is lazy... */
    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        voice->audio->free_func(voice->sendCoefficients[i]);
    }
    if (voice->sendCoefficients != NULL)
    {
        voice->audio->free_func(voice->sendCoefficients);
    }
    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        voice->audio->free_func(voice->mixCoefficients[i]);
    }
    if (voice->mixCoefficients != NULL)
    {
        voice->audio->free_func(voice->mixCoefficients);
    }
    if (voice->sendMix != NULL)
    {
        voice->audio->free_func(voice->sendMix);
    }
    if (voice->sendFilter != NULL)
    {
        voice->audio->free_func(voice->sendFilter);
        voice->sendFilter = NULL;
    }
    if (voice->sendFilterState != NULL)
    {
        for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
        {
            if (voice->sendFilterState[i] != NULL)
            {
                voice->audio->free_func(voice->sendFilterState[i]);
            }
        }
        voice->audio->free_func(voice->sendFilterState);
        voice->sendFilterState = NULL;
    }
    if (voice->sends.sends != NULL)
    {
        voice->audio->free_func(voice->sends.sends);
    }

    if (send_list == NULL)
    {
        /* Default to the mastering voice as output */
        defaultSend.Flags = 0;
        defaultSend.output_voice = voice->audio->master;
        defaultSends.SendCount = 1;
        defaultSends.sends = &defaultSend;
        send_list = &defaultSends;
    }
    else if (send_list->SendCount == 0)
    {
        /* No sends? Nothing to do... */
        voice->sendCoefficients = NULL;
        voice->mixCoefficients = NULL;
        voice->sendMix = NULL;
        ForgeAudio_zero(&voice->sends, sizeof(ForgeSendList));

        ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Copy send list */
    voice->sends.SendCount = send_list->SendCount;
    voice->sends.sends = (ForgeSend*) voice->audio->malloc_func(
        send_list->SendCount * sizeof(ForgeSend)
    );
    ForgeAudio_memcpy(
        voice->sends.sends,
        send_list->sends,
        send_list->SendCount * sizeof(ForgeSend)
    );

    /* Allocate/Reset default output matrix, mixer function, filters */
    voice->sendCoefficients = (float**) voice->audio->malloc_func(
        sizeof(float*) * send_list->SendCount
    );
    voice->mixCoefficients = (float**) voice->audio->malloc_func(
        sizeof(float*) * send_list->SendCount
    );
    voice->sendMix = (ForgeAudioMixCallback*) voice->audio->malloc_func(
        sizeof(ForgeAudioMixCallback) * send_list->SendCount
    );

    for (uint32_t i = 0; i < send_list->SendCount; i += 1)
    {
        if (send_list->sends[i].output_voice->type == FORGE_AUDIO_VOICE_MASTER)
        {
            outChannels = send_list->sends[i].output_voice->master.inputChannels;
        }
        else
        {
            outChannels = send_list->sends[i].output_voice->mix.inputChannels;
        }
        voice->sendCoefficients[i] = (float*) voice->audio->malloc_func(
            sizeof(float) * voice->outputChannels * outChannels
        );
        voice->mixCoefficients[i] = (float*) voice->audio->malloc_func(
            sizeof(float) * voice->outputChannels * outChannels
        );

        ForgeAudio_assert(voice->outputChannels > 0 && voice->outputChannels < 9);
        ForgeAudio_assert(outChannels > 0 && outChannels < 9);
        ForgeAudio_memcpy(
            voice->sendCoefficients[i],
            FORGE_AUDIO_INTERNAL_MATRIX_DEFAULTS[voice->outputChannels - 1][outChannels - 1],
            voice->outputChannels * outChannels * sizeof(float)
        );
        ForgeAudio_RecalcMixMatrix(voice, i);

        if (voice->outputChannels == 1)
        {
            if (outChannels == 1)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_1in_1out_Scalar;
            }
            else if (outChannels == 2)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_1in_2out_Scalar;
            }
            else if (outChannels == 6)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_1in_6out_Scalar;
            }
            else if (outChannels == 8)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_1in_8out_Scalar;
            }
            else
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_Generic;
            }
        }
        else if (voice->outputChannels == 2)
        {
            if (outChannels == 1)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_2in_1out_Scalar;
            }
            else if (outChannels == 2)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_2in_2out_Scalar;
            }
            else if (outChannels == 6)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_2in_6out_Scalar;
            }
            else if (outChannels == 8)
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_2in_8out_Scalar;
            }
            else
            {
                voice->sendMix[i] = ForgeAudio_Internal_Mix_Generic;
            }
        }
        else
        {
            voice->sendMix[i] = ForgeAudio_Internal_Mix_Generic;
        }

        if (send_list->sends[i].Flags & FORGE_AUDIO_SEND_USEFILTER)
        {
            /* Allocate the whole send filter array if needed... */
            if (voice->sendFilter == NULL)
            {
                voice->sendFilter = (ForgeFilterParameters*) voice->audio->malloc_func(
                    sizeof(ForgeFilterParameters) * send_list->SendCount
                );
            }
            if (voice->sendFilterState == NULL)
            {
                voice->sendFilterState = (ForgeAudioFilterState**) voice->audio->malloc_func(
                    sizeof(ForgeAudioFilterState*) * send_list->SendCount
                );
                ForgeAudio_zero(
                    voice->sendFilterState,
                    sizeof(ForgeAudioFilterState*) * send_list->SendCount
                );
            }

            /* ... then fill in this send's filter data */
            voice->sendFilter[i].Type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
            voice->sendFilter[i].Frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
            voice->sendFilter[i].OneOverQ = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
            voice->sendFilter[i].WetDryMix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
            voice->sendFilterState[i] = (ForgeAudioFilterState*) voice->audio->malloc_func(
                sizeof(ForgeAudioFilterState) * outChannels
            );
            ForgeAudio_zero(
                voice->sendFilterState[i],
                sizeof(ForgeAudioFilterState) * outChannels
            );
        }
    }

    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_set_effect_chain(
    ForgeVoice *voice,
    const ForgeEffectChain *effect_chain
) {
    ForgeApo *fapo;
    uint32_t channelCount;
    ForgeVoiceDetails voiceDetails;
    ForgeApoProperties *props;
    ForgeAudioFormatExtensible srcFmt, dstFmt;
    ForgeApoLockBuffer srcLockParams, dstLockParams;

    LOG_API_ENTER(voice->audio)

    forge_voice_get_details(voice, &voiceDetails);

    /* SetEffectChain must not change the number of output channels once the voice has been created */
    if (effect_chain == NULL && voice->outputChannels != 0)
    {
        /* cannot remove an effect chain that changes the number of channels */
        if (voice->outputChannels != voiceDetails.InputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "%s",
                "Cannot remove effect chain that changes the number of channels"
            )
            ForgeAudio_assert(0 && "Cannot remove effect chain that changes the number of channels");
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
    }

    if (effect_chain != NULL && voice->outputChannels != 0)
    {
        uint32_t lst = effect_chain->EffectCount - 1;

        /* new effect chain must have same number of output channels */
        if (voice->outputChannels != effect_chain->effects[lst].OutputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "%s",
                "New effect chain must have same number of output channels as the old chain"
            )
            ForgeAudio_assert(0 && "New effect chain must have same number of output channels as the old chain");
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
    }

    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)

    if (effect_chain == NULL)
    {
        ForgeAudio_Internal_FreeEffectChain(voice);
        ForgeAudio_zero(&voice->effects, sizeof(voice->effects));
        voice->outputChannels = voiceDetails.InputChannels;
    }
    else
    {
        /* Validate incoming chain before changing the current chain */

        /* These are always the same, so just write them now. */
        srcLockParams.format = &srcFmt.Format;
        dstLockParams.format = &dstFmt.Format;
        if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
        {
            srcLockParams.MaxFrameCount = voice->src.resampleSamples;
            dstLockParams.MaxFrameCount = voice->src.resampleSamples;
        }
        else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX)
        {
            srcLockParams.MaxFrameCount = voice->mix.outputSamples;
            dstLockParams.MaxFrameCount = voice->mix.outputSamples;
        }
        else if (voice->type == FORGE_AUDIO_VOICE_MASTER)
        {
            srcLockParams.MaxFrameCount = voice->audio->updateSize;
            dstLockParams.MaxFrameCount = voice->audio->updateSize;
        }

        /* The first source is the voice input data... */
        srcFmt.Format.bits_per_sample = 32;
        srcFmt.Format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        srcFmt.Format.channels = voiceDetails.InputChannels;
        srcFmt.Format.sample_rate = voiceDetails.InputSampleRate;
        srcFmt.Format.block_align = srcFmt.Format.channels * (srcFmt.Format.bits_per_sample / 8);
        srcFmt.Format.average_bytes_per_second = srcFmt.Format.sample_rate * srcFmt.Format.block_align;
        srcFmt.Format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        srcFmt.Samples.valid_bits_per_sample = srcFmt.Format.bits_per_sample;
        srcFmt.channel_mask = 0;
        ForgeAudio_memcpy(&srcFmt.SubFormat, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid));
        ForgeAudio_memcpy(&dstFmt, &srcFmt, sizeof(srcFmt));

        for (uint32_t i = 0; i < effect_chain->EffectCount; i += 1)
        {
            fapo = effect_chain->effects[i].effect;

            /* ... then we get this effect's format... */
            dstFmt.Format.channels = effect_chain->effects[i].OutputChannels;
            dstFmt.Format.block_align = dstFmt.Format.channels * (dstFmt.Format.bits_per_sample / 8);
            dstFmt.Format.average_bytes_per_second = dstFmt.Format.sample_rate * dstFmt.Format.block_align;

            /* FIXME: This error needs to be found _before_ we start
             * shredding the voice's state. This function is highly
             * destructive so any errors need to be found at the
             * beginning, not in the middle! We can't undo this!
             * -flibit
             */
            if (fapo->LockForProcess(fapo, 1, &srcLockParams, 1, &dstLockParams))
            {
                LOG_ERROR(
                    voice->audio,
                    "%s",
                    "Effect output format not supported"
                )
                ForgeAudio_assert(0 && "Effect output format not supported");
                ForgeAudio_PlatformUnlockMutex(voice->effectLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
                LOG_API_EXIT(voice->audio)
                return ForgeResultUnsupportedFormat;
            }

            /* Okay, now this effect is the source and the next
             * effect will be the destination. Repeat until no
             * effects left.
             */
            ForgeAudio_memcpy(&srcFmt, &dstFmt, sizeof(srcFmt));
        }

        ForgeAudio_Internal_FreeEffectChain(voice);
        ForgeAudio_Internal_AllocEffectChain(
            voice,
            effect_chain
        );

        /* check if in-place processing is supported */
        channelCount = voiceDetails.InputChannels;
        for (uint32_t i = 0; i < voice->effects.count; i += 1)
        {
            fapo = voice->effects.desc[i].effect;
            if (fapo->GetRegistrationProperties(fapo, &props) == 0)
            {
                voice->effects.inPlaceProcessing[i] = (props->Flags & FORGE_APO_FLAG_IN_PLACE_SUPPORTED) == FORGE_APO_FLAG_IN_PLACE_SUPPORTED;
                voice->effects.inPlaceProcessing[i] &= (channelCount == voice->effects.desc[i].OutputChannels);
                channelCount = voice->effects.desc[i].OutputChannels;

                /* Fails if in-place processing is mandatory and
                 * the chain forces us to do otherwise...
                 */
                ForgeAudio_assert(
                    !(props->Flags & FORGE_APO_FLAG_IN_PLACE_REQUIRED) ||
                    voice->effects.inPlaceProcessing[i]
                );

                voice->audio->free_func(props);
            }
        }
        voice->outputChannels = channelCount;
    }

    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_enable_effect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueEnableEffect(
            voice,
            EffectIndex,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[EffectIndex].InitialState = 1;
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_disable_effect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueDisableEffect(
            voice,
            EffectIndex,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[EffectIndex].InitialState = 0;
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_effect_state(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    int32_t *enabled
) {
    LOG_API_ENTER(voice->audio)
    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    *enabled = voice->effects.desc[EffectIndex].InitialState;
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_effect_parameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    const void *parameters,
    uint32_t ParametersByteSize,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetEffectParameters(
            voice,
            EffectIndex,
            parameters,
            ParametersByteSize,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (voice->effects.parameters == NULL)
    {
        LOG_ERROR(
            voice->audio,
            "Setting effect parameters on voice with no effect chain: %p",
            (void*) voice
        );
        return ForgeResultInvalidCall;
    }

    if (voice->effects.parameters[EffectIndex] == NULL)
    {
        voice->effects.parameters[EffectIndex] = voice->audio->malloc_func(
            ParametersByteSize
        );
        voice->effects.parameterSizes[EffectIndex] = ParametersByteSize;
    }
    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.parameterSizes[EffectIndex] < ParametersByteSize)
    {
        voice->effects.parameters[EffectIndex] = voice->audio->realloc_func(
            voice->effects.parameters[EffectIndex],
            ParametersByteSize
        );
        voice->effects.parameterSizes[EffectIndex] = ParametersByteSize;
    }
    ForgeAudio_memcpy(
        voice->effects.parameters[EffectIndex],
        parameters,
        ParametersByteSize
    );
    voice->effects.parameterUpdates[EffectIndex] = 1;
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_get_effect_parameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    void *parameters,
    uint32_t ParametersByteSize
) {
    ForgeApo *fapo;
    LOG_API_ENTER(voice->audio)
    ForgeAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    fapo = voice->effects.desc[EffectIndex].effect;
    fapo->GetParameters(fapo, parameters, ParametersByteSize);
    ForgeAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_voice_set_filter_parameters(
    ForgeVoice *voice,
    const ForgeFilterParameters *parameters,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetFilterParameters(
            voice,
            parameters,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (!(voice->flags & FORGE_AUDIO_VOICE_USEFILTER))
    {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_PlatformLockMutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    ForgeAudio_memcpy(
        &voice->filter,
        parameters,
        sizeof(ForgeFilterParameters)
    );
    ForgeAudio_PlatformUnlockMutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_filter_parameters(
    ForgeVoice *voice,
    ForgeFilterParameters *parameters
) {
    LOG_API_ENTER(voice->audio)

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return;
    }

    if (!(voice->flags & FORGE_AUDIO_VOICE_USEFILTER))
    {
        LOG_API_EXIT(voice->audio)
        return;
    }

    ForgeAudio_PlatformLockMutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    ForgeAudio_memcpy(
        parameters,
        &voice->filter,
        sizeof(ForgeFilterParameters)
    );
    ForgeAudio_PlatformUnlockMutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    const ForgeFilterParameters *parameters,
    uint32_t OperationSet
) {
    uint32_t i;
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetOutputFilterParameters(
            voice,
            destination_voice,
            parameters,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (destination_voice == NULL && voice->sends.SendCount == 1)
    {
        destination_voice = voice->sends.sends[0].output_voice;
    }
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (destination_voice == voice->sends.sends[i].output_voice)
        {
            break;
        }
    }
    if (i >= voice->sends.SendCount)
    {
        LOG_ERROR(
            voice->audio,
            "Destination not attached to source: %p %p",
            (void*) voice,
            (void*) destination_voice
        )
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (!(voice->sends.sends[i].Flags & FORGE_AUDIO_SEND_USEFILTER))
    {
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Set the filter parameters, finally. */
    ForgeAudio_memcpy(
        &voice->sendFilter[i],
        parameters,
        sizeof(ForgeFilterParameters)
    );

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    ForgeFilterParameters *parameters
) {
    uint32_t i;

    LOG_API_ENTER(voice->audio)

    /* MSDN: "This method is usable only on source and submix voices and
     * has no effect on mastering voices."
     */
    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return;
    }

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (destination_voice == NULL && voice->sends.SendCount == 1)
    {
        destination_voice = voice->sends.sends[0].output_voice;
    }
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (destination_voice == voice->sends.sends[i].output_voice)
        {
            break;
        }
    }
    if (i >= voice->sends.SendCount)
    {
        LOG_ERROR(
            voice->audio,
            "Destination not attached to source: %p %p",
            (void*) voice,
            (void*) destination_voice
        )
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    if (!(voice->sends.sends[i].Flags & FORGE_AUDIO_SEND_USEFILTER))
    {
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Set the filter parameters, finally. */
    ForgeAudio_memcpy(
        parameters,
        &voice->sendFilter[i],
        sizeof(ForgeFilterParameters)
    );

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_volume(
    ForgeVoice *voice,
    float Volume,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetVolume(
            voice,
            Volume,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    voice->volume = ForgeAudio_clamp(
        Volume,
        -FORGE_AUDIO_MAX_VOLUME_LEVEL,
        FORGE_AUDIO_MAX_VOLUME_LEVEL
    );

    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        ForgeAudio_RecalcMixMatrix(voice, i);
    }

    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_volume(
    ForgeVoice *voice,
    float *volume
) {
    LOG_API_ENTER(voice->audio)
    *volume = voice->volume;
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_channel_volumes(
    ForgeVoice *voice,
    uint32_t Channels,
    const float *volumes,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetChannelVolumes(
            voice,
            Channels,
            volumes,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (volumes == NULL)
    {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (Channels != voice->outputChannels)
    {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    ForgeAudio_memcpy(
        voice->channelVolume,
        volumes,
        sizeof(float) * Channels
    );

    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        ForgeAudio_RecalcMixMatrix(voice, i);
    }

    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_channel_volumes(
    ForgeVoice *voice,
    uint32_t Channels,
    float *volumes
) {
    LOG_API_ENTER(voice->audio)
    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    ForgeAudio_memcpy(
        volumes,
        voice->channelVolume,
        sizeof(float) * Channels
    );
    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_voice_set_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    const float *level_matrix,
    uint32_t OperationSet
) {
    uint32_t i;
    ForgeResult result = ForgeResultSuccess;
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetOutputMatrix(
            voice,
            destination_voice,
            SourceChannels,
            DestinationChannels,
            level_matrix,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (destination_voice == NULL && voice->sends.SendCount == 1)
    {
        destination_voice = voice->sends.sends[0].output_voice;
    }
    ForgeAudio_assert(destination_voice != NULL);
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (destination_voice == voice->sends.sends[i].output_voice)
        {
            break;
        }
    }
    if (i >= voice->sends.SendCount)
    {
        LOG_ERROR(
            voice->audio,
            "Destination not attached to source: %p %p",
            (void*) voice,
            (void*) destination_voice
        )
        result = ForgeResultInvalidCall;
        goto end;
    }

    /* Verify the Source/Destination channel count */
    if (SourceChannels != voice->outputChannels)
    {
        LOG_ERROR(
            voice->audio,
            "SourceChannels not equal to voice channel count: %p %d %d",
            (void*) voice,
            SourceChannels,
            voice->outputChannels
        )
        result = ForgeResultInvalidCall;
        goto end;
    }

    if (destination_voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        if (DestinationChannels != destination_voice->master.inputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "DestinationChannels not equal to master channel count: %p %d %d",
                (void*) destination_voice,
                DestinationChannels,
                destination_voice->master.inputChannels
            )
            result = ForgeResultInvalidCall;
            goto end;
        }
    }
    else
    {
        if (DestinationChannels != destination_voice->mix.inputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "DestinationChannels not equal to submix channel count: %p %d %d",
                (void*) destination_voice,
                DestinationChannels,
                destination_voice->mix.inputChannels
            )
            result = ForgeResultInvalidCall;
            goto end;
        }
    }

    /* Set the matrix values, finally */
    ForgeAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    ForgeAudio_memcpy(
        voice->sendCoefficients[i],
        level_matrix,
        sizeof(float) * SourceChannels * DestinationChannels
    );

    ForgeAudio_RecalcMixMatrix(voice, i);

    ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

end:
    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    float *level_matrix
) {
    uint32_t i;

    LOG_API_ENTER(voice->audio)
    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (destination_voice == voice->sends.sends[i].output_voice)
        {
            break;
        }
    }
    if (i >= voice->sends.SendCount)
    {
        LOG_ERROR(
            voice->audio,
            "Destination not attached to source: %p %p",
            (void*) voice,
            (void*) destination_voice
        )
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Verify the Source/Destination channel count */
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
    {
        ForgeAudio_assert(SourceChannels == voice->src.format->channels);
    }
    else
    {
        ForgeAudio_assert(SourceChannels == voice->mix.inputChannels);
    }
    if (destination_voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        ForgeAudio_assert(DestinationChannels == destination_voice->master.inputChannels);
    }
    else
    {
        ForgeAudio_assert(DestinationChannels == destination_voice->mix.inputChannels);
    }

    /* Get the matrix values, finally */
    ForgeAudio_memcpy(
        level_matrix,
        voice->sendCoefficients[i],
        sizeof(float) * SourceChannels * DestinationChannels
    );

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

static ForgeResult check_for_sends_to_voice(ForgeVoice *voice)
{
	ForgeAudioEngine *audio = voice->audio;
	ForgeResult ret = ForgeResultSuccess;
	ForgeSourceVoice *source;
	ForgeSubmixVoice *submix;
	LinkedList *list;
	uint32_t i;

	ForgeAudio_PlatformLockMutex(audio->sourceLock);
	list = audio->sources;
	while (list != NULL)
	{
		source = (ForgeSourceVoice*) list->entry;
		for (i = 0; i < source->sends.SendCount; i += 1)
			if (source->sends.sends[i].output_voice == voice)
			{
				ret = ForgeResultFailed;
				break;
			}
		if (ret)
			break;
		list = list->next;
	}
	ForgeAudio_PlatformUnlockMutex(audio->sourceLock);

	if (ret)
		return ret;

	ForgeAudio_PlatformLockMutex(audio->submixLock);
	list = audio->submixes;
	while (list != NULL)
	{
		submix = (ForgeSubmixVoice*) list->entry;
		for (i = 0; i < submix->sends.SendCount; i += 1)
			if (submix->sends.sends[i].output_voice == voice)
			{
				ret = ForgeResultFailed;
				break;
			}
		if (ret)
			break;
		list = list->next;
	}
	ForgeAudio_PlatformUnlockMutex(audio->submixLock);

	return ret;
}

static void destroy_voice(ForgeVoice *voice)
{
	uint32_t i;

	/* TODO: Check for dependencies and remove from audio graph first! */
	ForgeAudio_OperationSet_ClearAllForVoice(voice);

	if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
	{
#ifdef FORGE_AUDIO_DUMP_VOICES
		ForgeAudio_DumpVoice_Finalize((ForgeSourceVoice*) voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

		ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
		LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
		while (voice == voice->audio->processingSource)
		{
			ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
			LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)
			ForgeAudio_PlatformLockMutex(voice->audio->sourceLock);
			LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
		}
		LinkedList_RemoveEntry(
			&voice->audio->sources,
			voice,
			voice->audio->sourceLock,
			voice->audio->free_func
		);
		ForgeAudio_PlatformUnlockMutex(voice->audio->sourceLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

		voice->audio->free_func(voice->src.queued_buffers);
		voice->audio->free_func(voice->src.flush_buffers);
		voice->audio->free_func(voice->src.format);
		LOG_MUTEX_DESTROY(voice->audio, voice->src.bufferLock)
		ForgeAudio_PlatformDestroyMutex(voice->src.bufferLock);
#ifdef HAVE_WMADEC
		if (voice->src.wmadec)
		{
			ForgeAudio_WMADEC_free(voice);
		}
#endif /* HAVE_WMADEC */
	}
	else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX)
	{
		/* Remove submix from list */
		LinkedList_RemoveEntry(
			&voice->audio->submixes,
			voice,
			voice->audio->submixLock,
			voice->audio->free_func
		);

		/* Delete submix data */
		voice->audio->free_func(voice->mix.inputCache);
	}
	else if (voice->type == FORGE_AUDIO_VOICE_MASTER)
	{
		if (voice->audio->platform != NULL)
		{
			ForgeAudio_PlatformQuit(voice->audio->platform);
			voice->audio->platform = NULL;
		}
		if (voice->master.effectCache != NULL)
		{
			voice->audio->free_func(voice->master.effectCache);
		}
		voice->audio->master = NULL;
	}

	if (voice->sendLock != NULL)
	{
		ForgeAudio_PlatformLockMutex(voice->sendLock);
		LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
		for (i = 0; i < voice->sends.SendCount; i += 1)
		{
			voice->audio->free_func(voice->sendCoefficients[i]);
		}
		if (voice->sendCoefficients != NULL)
		{
			voice->audio->free_func(voice->sendCoefficients);
		}
		for (i = 0; i < voice->sends.SendCount; i += 1)
		{
			voice->audio->free_func(voice->mixCoefficients[i]);
		}
		if (voice->mixCoefficients != NULL)
		{
			voice->audio->free_func(voice->mixCoefficients);
		}
		if (voice->sendMix != NULL)
		{
			voice->audio->free_func(voice->sendMix);
		}
		if (voice->sendFilter != NULL)
		{
			voice->audio->free_func(voice->sendFilter);
		}
		if (voice->sendFilterState != NULL)
		{
			for (i = 0; i < voice->sends.SendCount; i += 1)
			{
				if (voice->sendFilterState[i] != NULL)
				{
					voice->audio->free_func(voice->sendFilterState[i]);
				}
			}
			voice->audio->free_func(voice->sendFilterState);
		}
		if (voice->sends.sends != NULL)
		{
			voice->audio->free_func(voice->sends.sends);
		}
		ForgeAudio_PlatformUnlockMutex(voice->sendLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->sendLock)
		ForgeAudio_PlatformDestroyMutex(voice->sendLock);
	}

	if (voice->effectLock != NULL)
	{
		ForgeAudio_PlatformLockMutex(voice->effectLock);
		LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
		ForgeAudio_Internal_FreeEffectChain(voice);
		ForgeAudio_PlatformUnlockMutex(voice->effectLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->effectLock)
		ForgeAudio_PlatformDestroyMutex(voice->effectLock);
	}

	if (voice->filterLock != NULL)
	{
		ForgeAudio_PlatformLockMutex(voice->filterLock);
		LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
		if (voice->filterState != NULL)
		{
			voice->audio->free_func(voice->filterState);
		}
		ForgeAudio_PlatformUnlockMutex(voice->filterLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->filterLock)
		ForgeAudio_PlatformDestroyMutex(voice->filterLock);
	}

	if (voice->volumeLock != NULL)
	{
		ForgeAudio_PlatformLockMutex(voice->volumeLock);
		LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
		if (voice->channelVolume != NULL)
		{
			voice->audio->free_func(voice->channelVolume);
		}
		ForgeAudio_PlatformUnlockMutex(voice->volumeLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->volumeLock)
		ForgeAudio_PlatformDestroyMutex(voice->volumeLock);
	}

	voice->audio->free_func(voice);
}

ForgeResult forge_voice_try_destroy(ForgeVoice *voice)
{
	ForgeResult ret;

	ForgeAudioEngine* audio = voice->audio;

	LOG_API_ENTER(audio)

	if ((ret = check_for_sends_to_voice(voice)))
	{
		LOG_ERROR(
			audio,
			"Voice %p is an output for other voice(s)",
			voice
		)
		LOG_API_EXIT(audio)
		return ret;
	}
	destroy_voice(voice);
	LOG_API_EXIT(audio)
	return 0;
}

void forge_voice_destroy(ForgeVoice *voice)
{
    forge_voice_try_destroy(voice);
}

/* ForgeSourceVoice Interface */

ForgeResult forge_source_voice_start(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueStart(
            voice,
            Flags,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }


    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    ForgeAudio_assert(Flags == 0);
    voice->src.active = 1;
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_stop(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueStop(
            voice,
            Flags,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (Flags & FORGE_AUDIO_PLAY_TAILS)
    {
        voice->src.active = 2;
    }
    else
    {
        voice->src.active = 0;
    }
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_submit_buffer(
    ForgeSourceVoice *voice,
    const ForgeBuffer *buffer,
    const ForgeBufferWMA *buffer_wma
) {
    const uint32_t block_size = voice->src.format->block_align;
	uint32_t playBegin, playLength, loopBegin, loopLength, bufferLength;
	struct queued_buffer *entry;

    LOG_API_ENTER(voice->audio)
    LOG_INFO(
        voice->audio,
        "%p: {Flags: 0x%x, AudioBytes: %u, audio_data: %p, Play: %u + %u, Loop: %u + %u x %u}",
        (void*) voice,
        buffer->Flags,
        buffer->AudioBytes,
        (const void*) buffer->audio_data,
        buffer->PlayBegin,
        buffer->PlayLength,
        buffer->LoopBegin,
        buffer->LoopLength,
        buffer->LoopCount
    )

    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (block_size == 0)
    {
        LOG_ERROR(voice->audio, "%s", "Source voice has zero block alignment");
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    if (buffer_wma == NULL &&
        voice->src.format->format_tag != FORGE_AUDIO_FORMAT_XMAUDIO2 &&
        buffer->AudioBytes % block_size != 0)
    {
        LOG_ERROR(
            voice->audio,
            "PCM source buffer AudioBytes must be a multiple of block_align: %u %% %u",
            buffer->AudioBytes,
            block_size
        )
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

    /* Start off with whatever they just sent us... */
    playBegin = buffer->PlayBegin;
    playLength = buffer->PlayLength;
    loopBegin = buffer->LoopBegin;
    loopLength = buffer->LoopLength;

    /* "LoopBegin/LoopLength must be zero if LoopCount is 0" */
    if (buffer->LoopCount == 0 && (loopBegin > 0 || loopLength > 0))
    {
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }

	if (voice->src.format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2)
	{
		ForgeXMA2Format *fmtex = (ForgeXMA2Format*) voice->src.format;
		bufferLength = fmtex->dwSamplesEncoded;
	}
	else if (buffer_wma != NULL)
	{
		bufferLength =
			buffer_wma->decoded_packet_cumulative_bytes[buffer_wma->PacketCount - 1] /
			(voice->src.format->channels * voice->src.format->bits_per_sample / 8);
	}
	else
	{
		bufferLength =
			buffer->AudioBytes /
			voice->src.format->block_align;
	}

	if (playBegin + playLength > bufferLength || playBegin + playLength < playLength)
	{
		/* Reading past the end of the buffer, or begin + length overflow uint32_t, which
		 * would also read past the end of the buffer. */
		LOG_API_EXIT(voice->audio)
		return ForgeResultInvalidCall;
	}

    if (buffer->LoopCount > 0 && buffer_wma == NULL && voice->src.format->format_tag != FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        uint32_t realPlayLength = playLength;
        uint32_t realLoopLength = loopLength;

        /* PlayLength Default */
        if (realPlayLength == 0)
        {
            realPlayLength = bufferLength - playBegin;
        }

        /* LoopLength Default */
        if (realLoopLength == 0)
        {
            realLoopLength = playBegin + realPlayLength - loopBegin;
        }

        /* "The value of LoopBegin must be less than PlayBegin + PlayLength" */
        if (loopBegin >= (playBegin + realPlayLength))
        {
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }

        /* "The value of LoopBegin + LoopLength must be greater than PlayBegin
         * and less than PlayBegin + PlayLength"
         */
        if ((loopBegin + realLoopLength) <= playBegin ||
           (loopBegin + realLoopLength) > (playBegin + realPlayLength))
        {
            LOG_API_EXIT(voice->audio)
            return ForgeResultInvalidCall;
        }
    }

    if (buffer_wma != NULL || voice->src.format->format_tag == FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        /* WMA only supports looping the whole buffer */
        loopBegin = 0;
        loopLength = playBegin + playLength;
    }

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    array_reserve(
        voice->audio,
        (void**) &voice->src.queued_buffers,
        &voice->src.queued_buffers_capacity,
        voice->src.queued_buffer_count + 1,
        sizeof(*voice->src.queued_buffers)
    );

    entry = &voice->src.queued_buffers[voice->src.queued_buffer_count++];
    ForgeAudio_memset(entry, 0, sizeof(*entry));
    ForgeAudio_memcpy(&entry->buffer, buffer, sizeof(ForgeBuffer));
    entry->buffer.PlayBegin = playBegin;
    entry->buffer.PlayLength = playLength;
    entry->buffer.LoopBegin = loopBegin;
    entry->buffer.LoopLength = loopLength;
    if (buffer_wma != NULL)
    {
        ForgeAudio_memcpy(&entry->bufferWMA, buffer_wma, sizeof(ForgeBufferWMA));
    }
    else
    {
        if (playLength != 0)
        {
            entry->play_bytes = playLength * block_size;
        }
        else
        {
            entry->play_bytes = buffer->AudioBytes - (playBegin * block_size);
        }

        if (loopLength != 0)
        {
            entry->loop_bytes = loopLength * block_size;
        }
        else
        {
            entry->loop_bytes = entry->play_bytes
                + (playBegin * block_size)
                - (loopBegin * block_size);
        }
    }

#ifdef FORGE_AUDIO_DUMP_VOICES
    /* dumping current buffer, append into "data" section */
    if (buffer->audio_data != NULL && entry->play_bytes > 0)
    {
        ForgeAudio_DumpVoice_WriteBuffer(voice, buffer, buffer_wma, entry->play_bytes);
    }
#endif /* FORGE_AUDIO_DUMP_VOICES */

    if (voice->src.queued_buffer_count == 1)
    {
        voice->src.curBufferOffset = entry->buffer.PlayBegin;
    }

    LOG_INFO(
        voice->audio,
        "%p: appended buffer %p",
        (void*) voice,
        (void*) &entry->buffer
    )
    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_flush_buffers(
    ForgeSourceVoice *voice
) {
    size_t offset = 0;

    LOG_API_ENTER(voice->audio)
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    array_reserve(
        voice->audio,
        (void**) &voice->src.flush_buffers,
        &voice->src.flush_buffers_capacity,
        voice->src.flush_buffer_count + voice->src.queued_buffer_count,
        sizeof(*voice->src.flush_buffers)
    );

    if (    voice->src.active == 1 &&
        voice->src.queued_buffer_count > 0 &&
        voice->src.queued_buffers[0].sent_OnStartBuffer    )
    {
        offset = 1;
    }
    else
    {
        voice->src.curBufferOffset = 0;
    }

    if (voice->src.queued_buffer_count > offset)
    {
        ForgeAudio_memcpy(
            voice->src.flush_buffers + voice->src.flush_buffer_count,
            voice->src.queued_buffers + offset,
            (voice->src.queued_buffer_count - offset) * sizeof(*voice->src.flush_buffers)
        );
    }

    voice->src.flush_buffer_count += voice->src.queued_buffer_count - offset;
    voice->src.queued_buffer_count = offset;

    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_end_stream(
    ForgeSourceVoice *voice
) {
    LOG_API_ENTER(voice->audio)
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0)
    {
        voice->src.queued_buffers[voice->src.queued_buffer_count - 1].buffer.Flags |= FORGE_AUDIO_END_OF_STREAM;
    }

    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

ForgeResult forge_source_voice_break_loop(
    ForgeSourceVoice *voice,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueExitLoop(
            voice,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0)
    {
        voice->src.queued_buffers[0].buffer.LoopCount = 0;
    }

    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_source_voice_get_state(
    ForgeSourceVoice *voice,
    ForgeVoiceState *voice_state,
    uint32_t Flags
) {
    LOG_API_ENTER(voice->audio)
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (!(Flags & FORGE_AUDIO_VOICE_NOSAMPLESPLAYED))
    {
        voice_state->SamplesPlayed = voice->src.totalSamples;
    }

    voice_state->BuffersQueued = 0;
    voice_state->current_buffer_context = NULL;

    if (voice->src.queued_buffer_count != 0)
    {
        voice_state->current_buffer_context = voice->src.queued_buffers[0].buffer.context;
    }
    voice_state->BuffersQueued += (uint32_t) voice->src.queued_buffer_count;

    /* Pending flushed buffers also count */
    voice_state->BuffersQueued += (uint32_t) voice->src.flush_buffer_count;

    LOG_INFO(
        voice->audio,
        "-> {current_buffer_context: %p, BuffersQueued: %u, SamplesPlayed: %" ForgeAudio_PRIu64 "}",
        voice_state->current_buffer_context, voice_state->BuffersQueued,
        voice_state->SamplesPlayed
    )

    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_source_voice_set_rate(
    ForgeSourceVoice *voice,
    float Ratio,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        ForgeAudio_OperationSet_QueueSetFrequencyRatio(
            voice,
            Ratio,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->flags & FORGE_AUDIO_VOICE_NOPITCH)
    {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    voice->src.freqRatio = ForgeAudio_clamp(
        Ratio,
        FORGE_AUDIO_MIN_FREQ_RATIO,
        voice->src.maxFreqRatio
    );
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_source_voice_get_rate(
    ForgeSourceVoice *voice,
    float *ratio
) {
    LOG_API_ENTER(voice->audio)
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    *ratio = voice->src.freqRatio;
    LOG_API_EXIT(voice->audio)
}

ForgeResult forge_source_voice_set_sample_rate(
    ForgeSourceVoice *voice,
    uint32_t NewSourceSampleRate
) {
    uint32_t outSampleRate;
    uint32_t newDecodeSamples, newResampleSamples;

    LOG_API_ENTER(voice->audio)
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);
    ForgeAudio_assert(    NewSourceSampleRate >= FORGE_AUDIO_MIN_SAMPLE_RATE &&
            NewSourceSampleRate <= FORGE_AUDIO_MAX_SAMPLE_RATE    );

    ForgeAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
    if (voice->src.queued_buffer_count != 0)
    {
        ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
        LOG_API_EXIT(voice->audio)
        return ForgeResultInvalidCall;
    }
    ForgeAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

    voice->src.format->sample_rate = NewSourceSampleRate;

    /* Resize decode cache */
    newDecodeSamples = (uint32_t) ForgeAudio_ceil(
        voice->audio->updateSize *
        (double) voice->src.maxFreqRatio *
        (double) NewSourceSampleRate /
        (double) voice->audio->master->master.inputSampleRate
    ) + EXTRA_DECODE_PADDING * voice->src.format->channels;
    ForgeAudio_Internal_ResizeDecodeCache(
        voice->audio,
        (newDecodeSamples + EXTRA_DECODE_PADDING) * voice->src.format->channels
    );
    voice->src.decodeSamples = newDecodeSamples;

    ForgeAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (voice->sends.SendCount == 0)
    {
        ForgeAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }
    outSampleRate = voice->sends.sends[0].output_voice->type == FORGE_AUDIO_VOICE_MASTER ?
        voice->sends.sends[0].output_voice->master.inputSampleRate :
        voice->sends.sends[0].output_voice->mix.inputSampleRate;

    newResampleSamples = (uint32_t) (ForgeAudio_ceil(
        (double) voice->audio->updateSize *
        (double) outSampleRate /
        (double) voice->audio->master->master.inputSampleRate
    ));
    voice->src.resampleSamples = newResampleSamples;

    ForgeAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

/* ForgeMasterVoice Interface */

FORGE_AUDIO_API ForgeResult forge_master_voice_get_channel_mask(
    ForgeMasterVoice *voice,
    uint32_t *channel_mask
) {
    LOG_API_ENTER(voice->audio)
    ForgeAudio_assert(voice->type == FORGE_AUDIO_VOICE_MASTER);
    ForgeAudio_assert(channel_mask != NULL);

    *channel_mask = voice->audio->mixFormat.channel_mask;
    LOG_API_EXIT(voice->audio)
    return 0;
}

#ifdef FORGE_AUDIO_DUMP_VOICES

static inline ForgeAudioIOStreamOut *DumpVoices_fopen(
    const ForgeSourceVoice *voice,
    const ForgeAudioFormat *format,
    const char *mode,
    const char *ext
) {
    char loc[64];
    uint16_t format_tag = format->format_tag;
    uint16_t format_ex_tag = 0;
    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        /* get the GUID of the extended subformat */
        const ForgeAudioFormatExtensible *format_ex =
                (const ForgeAudioFormatExtensible*) format;
        format_ex_tag = (uint16_t) (format_ex->SubFormat.Data1);
    }
    ForgeAudio_snprintf(
        loc,
        sizeof(loc),
        "FA_fmt_0x%04X_0x%04X_0x%016lX%s.wav",
        format_tag,
        format_ex_tag,
        (uint64_t) voice,
        ext
    );
    ForgeAudioIOStreamOut *fileOut = ForgeAudio_fopen_out(loc, mode);
    return fileOut;
}

static inline void DumpVoices_finalize_section(
    const ForgeSourceVoice *voice,
    const ForgeAudioFormat *format,
    const char *section /* one of "data" or "dpds" */
) {
    /* data file only contains the real data bytes */
    ForgeAudioIOStreamOut *io_data = DumpVoices_fopen(voice, format, "rb", section);
    if (!io_data)
    {
        return;
    }
    ForgeAudio_PlatformLockMutex((ForgeAudioMutex) io_data->lock);
    size_t file_size_data = io_data->size(io_data->data);
    if (file_size_data == 0)
    {
        /* nothing to do */
        /* close data file */
        ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io_data->lock);
        ForgeAudio_close_out(io_data);
        return;
    }

    /* we got some data: append data section to main file */
    ForgeAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "ab", "");
    if (!io)
    {
        /* close data file */
        ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io_data->lock);
        ForgeAudio_close_out(io_data);
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
    uint8_t buffer[1024*1024];
    size_t count;
    while((count = io_data->read(io_data->data, (void*) buffer, 1, 1024*1024)) > 0)
    {
        io->write(io->data, (void*) buffer, 1, count);
    }

    /* close data file */
    ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io_data->lock);
    ForgeAudio_close_out(io_data);
    /* close main file */
    ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io->lock);
    ForgeAudio_close_out(io);
}

static void ForgeAudio_DumpVoice_Init(const ForgeSourceVoice *voice)
{
    const ForgeAudioFormat *format = voice->src.format;

    ForgeAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "wb", "");
    if (!io)
    {
        return;
    }
    ForgeAudio_PlatformLockMutex((ForgeAudioMutex) io->lock);
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
    if (format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE && extra_size >= 22)
    {
        const ForgeAudioFormatExtensible *format_ex =
                (const ForgeAudioFormatExtensible*) format;
        uint16_t format_ex_tag = (uint16_t) (format_ex->SubFormat.Data1);
        if (format_ex_tag == FORGE_AUDIO_FORMAT_WMAUDIO2)
        {
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
        /* Format - 4 */
        io->write(io->data, formatFourcc, 4, 1);
    }
    { /* fmt sub-chunk 24 */
        /* Subchunk1ID - 4 */
        io->write(io->data, "fmt ", 4, 1);
        /* Subchunk1Size - 4 */
        /* 18 byte for WAVEFORMATEX and extra_size for WAVEFORMATEXTENDED */
        uint32_t chunk_data_size = 18 + (uint32_t) extra_size;
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

        if (extra_size >= 22)
        {
            /* we have a WAVEFORMATEXTENSIBLE struct to write */
            const ForgeAudioFormatExtensible *format_ex =
                    (const ForgeAudioFormatExtensible*) format;
            io->write(io->data, &format_ex->Samples.valid_bits_per_sample, 2, 1);
            io->write(io->data, &format_ex->channel_mask,   4, 1);
            /* write ForgeGuid */
            io->write(io->data, &format_ex->SubFormat.Data1, 4, 1);
            io->write(io->data, &format_ex->SubFormat.Data2, 2, 1);
            io->write(io->data, &format_ex->SubFormat.Data3, 2, 1);
            io->write(io->data, &format_ex->SubFormat.Data4, 1, 8);
        }
        if (format->extra_size > 22)
        {
            /* fill up the remaining extra_size bytes with zeros */
            uint8_t zero = 0;
            for (uint16_t i=23; i<=format->extra_size; i++)
            {
                io->write(io->data, &zero, 1, 1);
            }
        }
    }
    { /* dpds sub-chunk - optional - 8 bytes + bufferWMA uint32_t samples */
        /* create file to hold the bufferWMA samples */
        ForgeAudioIOStreamOut *io_dpds = DumpVoices_fopen(voice, format, "wb", "dpds");
        ForgeAudio_close_out(io_dpds);
        /* io_dpds file will be filled by SubmitBuffer */
    }
    { /* data sub-chunk - 8 bytes + data */
        /* create file to hold the data samples */
        ForgeAudioIOStreamOut *io_data = DumpVoices_fopen(voice, format, "wb", "data");
        ForgeAudio_close_out(io_data);
        /* io_data file will be filled by SubmitBuffer */
    }
    ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io->lock);
    ForgeAudio_close_out(io);
}

static void ForgeAudio_DumpVoice_Finalize(const ForgeSourceVoice *voice)
{
    const ForgeAudioFormat *format = voice->src.format;

    /* add dpds subchunk - optional */
    DumpVoices_finalize_section(voice, format, "dpds");
    /* add data subchunk */
    DumpVoices_finalize_section(voice, format, "data");

    /* open main file to update filesize */
    ForgeAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "r+b", "");
    if (!io)
    {
        return;
    }
    ForgeAudio_PlatformLockMutex((ForgeAudioMutex) io->lock);
    size_t file_size = io->size(io->data);
    if (file_size >= 44)
    {
        /* update filesize */
        uint32_t chunk_size = (uint32_t)(file_size - 8);
        io->seek(io->data, 4, FORGE_AUDIO_SEEK_SET);
        io->write(io->data, &chunk_size, 4, 1);
    }
    ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io->lock);
    ForgeAudio_close_out(io);
}

static void ForgeAudio_DumpVoice_WriteBuffer(
    const ForgeSourceVoice *voice,
    const ForgeBuffer *buffer,
    const ForgeBufferWMA *buffer_wma,
    const uint32_t size
) {
    ForgeAudioIOStreamOut *io_data = DumpVoices_fopen(voice, voice->src.format, "ab", "data");
    if (io_data == NULL)
    {
        return;
    }

    ForgeAudio_PlatformLockMutex((ForgeAudioMutex) io_data->lock);
    if (buffer_wma != NULL)
    {
        /* dump encoded buffer contents */
        if (buffer_wma->PacketCount > 0)
        {
            ForgeAudioIOStreamOut *io_dpds = DumpVoices_fopen(voice, voice->src.format, "ab", "dpds");
            if (io_dpds)
            {
                ForgeAudio_PlatformLockMutex((ForgeAudioMutex) io_dpds->lock);
                /* write to dpds file */
                io_dpds->write(io_dpds->data, buffer_wma->decoded_packet_cumulative_bytes, sizeof(uint32_t), buffer_wma->PacketCount);
                ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io_dpds->lock);
                ForgeAudio_close_out(io_dpds);
            }
            /* write buffer contents to data file */
            io_data->write(io_data->data, buffer->audio_data, sizeof(uint8_t), buffer->AudioBytes);
        }
    }
    else
    {
        /* dump unencoded buffer contents */
        uint16_t bytesPerFrame = (voice->src.format->channels * voice->src.format->bits_per_sample / 8);
        ForgeAudio_assert(bytesPerFrame > 0);
        const void *audio_data_begin = buffer->audio_data + buffer->PlayBegin * bytesPerFrame;
        io_data->write(io_data->data, audio_data_begin, 1, size);
    }
    ForgeAudio_PlatformUnlockMutex((ForgeAudioMutex) io_data->lock);
    ForgeAudio_close_out(io_data);
}

#endif /* FORGE_AUDIO_DUMP_VOICES */
