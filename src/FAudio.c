/* ForgeAudioEngine - XAudio Reimplementation for FNA
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

#include "FAudio_internal.h"

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
static void FAudio_DUMPVOICE_Init(const ForgeSourceVoice *voice);
static void FAudio_DUMPVOICE_Finalize(const ForgeSourceVoice *voice);
static void FAudio_DUMPVOICE_WriteBuffer(
    const ForgeSourceVoice *voice,
    const ForgeBuffer *pBuffer,
    const ForgeBufferWMA *pBufferWMA,
    const uint32_t size
);
#endif /* FORGE_AUDIO_DUMP_VOICES */

static uint8_t FAudio_INTERNAL_ValidateUncompressedFormat(
    ForgeAudioEngine *audio,
    const ForgeAudioFormat *format
) {
    const ForgeAudioFormat *base = format;
    uint8_t isPCM = 0;
    uint8_t isFloat = 0;
    uint32_t expectedBlockAlign;

    if (format->wFormatTag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        const ForgeAudioFormatExtensible *ext = (const ForgeAudioFormatExtensible*) format;

        if (FAudio_memcmp(&ext->SubFormat, &FORGE_AUDIO_SUBTYPE_PCM, sizeof(ForgeGuid)) == 0)
        {
            isPCM = 1;
        }
        else if (FAudio_memcmp(&ext->SubFormat, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid)) == 0)
        {
            isFloat = 1;
        }
        else
        {
            return 1;
        }
    }
    else if (format->wFormatTag == FORGE_AUDIO_FORMAT_PCM)
    {
        isPCM = 1;
    }
    else if (format->wFormatTag == FORGE_AUDIO_FORMAT_IEEE_FLOAT)
    {
        isFloat = 1;
    }
    else
    {
        return 1;
    }

    if (base->nChannels == 0 || base->nBlockAlign == 0 || base->wBitsPerSample % 8 != 0)
    {
        LOG_ERROR(audio, "%s", "Invalid PCM source format block alignment");
        return 0;
    }

    if (isPCM &&    base->wBitsPerSample != 8 &&
            base->wBitsPerSample != 16 &&
            base->wBitsPerSample != 24 &&
            base->wBitsPerSample != 32    )
    {
        LOG_ERROR(audio, "Unsupported PCM bit depth: %u", base->wBitsPerSample);
        return 0;
    }

    if (isFloat && base->wBitsPerSample != 32)
    {
        LOG_ERROR(audio, "Unsupported float PCM bit depth: %u", base->wBitsPerSample);
        return 0;
    }

    expectedBlockAlign = base->nChannels * (base->wBitsPerSample / 8);
    if (base->nBlockAlign != expectedBlockAlign)
    {
        LOG_ERROR(
            audio,
            "Invalid PCM block alignment: got %u, expected %u",
            base->nBlockAlign,
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

uint32_t forge_audio_create(
    ForgeAudioEngine **ppFAudio,
    uint32_t Flags
) {
    return forge_audio_create_with_allocator(
        ppFAudio,
        Flags,
        FAudio_malloc,
        FAudio_free,
        FAudio_realloc
    );
}

static uint32_t engine_construct_with_allocator(
    ForgeAudioEngine **ppFAudio,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);

static uint32_t engine_initialize(
    ForgeAudioEngine *audio,
    uint32_t Flags
);

uint32_t forge_audio_create_with_allocator(
    ForgeAudioEngine **ppFAudio,
    uint32_t Flags,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    engine_construct_with_allocator(
        ppFAudio,
        customMalloc,
        customFree,
        customRealloc
    );
    engine_initialize(*ppFAudio, Flags);
    return 0;
}

static uint32_t engine_construct_with_allocator(
    ForgeAudioEngine **ppFAudio,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    ForgeDebugConfiguration debugInit = {0};
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    FAudio_PlatformAddRef();
    *ppFAudio = (ForgeAudioEngine*) customMalloc(sizeof(ForgeAudioEngine));
    FAudio_zero(*ppFAudio, sizeof(ForgeAudioEngine));
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    forge_audio_set_debug_configuration(*ppFAudio, &debugInit, NULL);
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
    (*ppFAudio)->refLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*ppFAudio), (*ppFAudio)->refLock)
    (*ppFAudio)->sourceLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*ppFAudio), (*ppFAudio)->sourceLock)
    (*ppFAudio)->submixLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*ppFAudio), (*ppFAudio)->submixLock)
    (*ppFAudio)->callbackLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*ppFAudio), (*ppFAudio)->callbackLock)
    (*ppFAudio)->operationLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE((*ppFAudio), (*ppFAudio)->operationLock)
    (*ppFAudio)->pMalloc = customMalloc;
    (*ppFAudio)->pFree = customFree;
    (*ppFAudio)->pRealloc = customRealloc;
    (*ppFAudio)->refcount = 1;
    return 0;
}

// Preliminary TODO:
// * Document why this function doesn't increment atomically.
// * Document the thread safety story of this library (XAudio2 is supposed to be thread safe?)
// * Look into removing refcounting if it's related to COM, and even if not, perhaps still.
uint32_t forge_audio_engine_retain(ForgeAudioEngine *audio)
{
    uint32_t refcount;

    LOG_API_ENTER(audio)

    FAudio_PlatformLockMutex(audio->refLock);
    LOG_MUTEX_LOCK(audio, audio->refLock)
    audio->refcount += 1;
    refcount = audio->refcount;
    FAudio_PlatformUnlockMutex(audio->refLock);
    LOG_MUTEX_UNLOCK(audio, audio->refLock)

    LOG_API_EXIT(audio)
    return refcount;
}

static void destroy_voice(ForgeVoice *voice);

uint32_t forge_audio_engine_release(ForgeAudioEngine *audio)
{
	uint32_t refcount;
	ForgeVoice *voice;

	LOG_API_ENTER(audio)

	FAudio_PlatformLockMutex(audio->refLock);
	LOG_MUTEX_LOCK(audio, audio->refLock)
	audio->refcount -= 1;
	refcount = audio->refcount;
	FAudio_PlatformUnlockMutex(audio->refLock);
	LOG_MUTEX_UNLOCK(audio, audio->refLock)

	if (refcount == 0)
	{
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
		FAudio_OPERATIONSET_ClearAll(audio);
		forge_audio_stop_engine(audio);
		audio->pFree(audio->decodeCache);
		audio->pFree(audio->resampleCache);
		audio->pFree(audio->effectChainCache);
		LOG_MUTEX_DESTROY(audio, audio->refLock)
		FAudio_PlatformDestroyMutex(audio->refLock);
		LOG_MUTEX_DESTROY(audio, audio->sourceLock)
		FAudio_PlatformDestroyMutex(audio->sourceLock);
		LOG_MUTEX_DESTROY(audio, audio->submixLock)
		FAudio_PlatformDestroyMutex(audio->submixLock);
		LOG_MUTEX_DESTROY(audio, audio->callbackLock)
		FAudio_PlatformDestroyMutex(audio->callbackLock);
		LOG_MUTEX_DESTROY(audio, audio->operationLock)
		FAudio_PlatformDestroyMutex(audio->operationLock);
		audio->pFree(audio);
		FAudio_PlatformRelease();
	}
	else
	{
		LOG_API_EXIT(audio)
	}
	return refcount;
}

uint32_t forge_audio_get_device_count(ForgeAudioEngine *audio, uint32_t *pCount)
{
    LOG_API_ENTER(audio)
    *pCount = FAudio_PlatformGetDeviceCount();
    LOG_API_EXIT(audio)
    return 0;
}

uint32_t forge_audio_get_device_details(
    ForgeAudioEngine *audio,
    uint32_t Index,
    ForgeDeviceDetails *pDeviceDetails
) {
    uint32_t result;
    LOG_API_ENTER(audio)
    result = FAudio_PlatformGetDeviceDetails(Index, pDeviceDetails);
    LOG_API_EXIT(audio)
    return result;
}

static uint32_t engine_initialize(
    ForgeAudioEngine *audio,
    uint32_t Flags
) {
	LOG_API_ENTER(audio)
	FAudio_assert((Flags & ~(FORGE_AUDIO_DEBUG_ENGINE | FORGE_AUDIO_1024_QUANTUM)) == 0);

    audio->initFlags = Flags;

    /* FIXME: This is lazy... */
    audio->decodeCache = (float*) audio->pMalloc(sizeof(float));
    audio->resampleCache = (float*) audio->pMalloc(sizeof(float));
    audio->decodeSamples = 1;
    audio->resampleSamples = 1;

    forge_audio_start_engine(audio);
    LOG_API_EXIT(audio)
    return 0;
}

uint32_t forge_audio_register_callback(
    ForgeAudioEngine *audio,
    ForgeEngineCallback *pCallback
) {
    LOG_API_ENTER(audio)
    LinkedList_AddEntry(
        &audio->callbacks,
        pCallback,
        audio->callbackLock,
        audio->pMalloc
    );
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_unregister_callback(
    ForgeAudioEngine *audio,
    ForgeEngineCallback *pCallback
) {
    LOG_API_ENTER(audio)
    LinkedList_RemoveEntry(
        &audio->callbacks,
        pCallback,
        audio->callbackLock,
        audio->pFree
    );
    LOG_API_EXIT(audio)
}

uint32_t forge_audio_create_source_voice(
    ForgeAudioEngine *audio,
    ForgeSourceVoice **ppSourceVoice,
    const ForgeAudioFormat *pSourceFormat,
    uint32_t Flags,
    float MaxFrequencyRatio,
    ForgeVoiceCallback *pCallback,
    const ForgeSendList *pSendList,
    const ForgeEffectChain *pEffectChain
) {
    LOG_API_ENTER(audio)
    LOG_FORMAT(audio, pSourceFormat)

    if (pSendList == NULL && audio->master == NULL)
    {
        LOG_ERROR(audio, "%s", "CreateSourceVoice called before mastering voice was initialized");
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (!FAudio_INTERNAL_ValidateUncompressedFormat(audio, pSourceFormat))
    {
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    *ppSourceVoice = (ForgeSourceVoice*) audio->pMalloc(sizeof(ForgeVoice));
    FAudio_zero(*ppSourceVoice, sizeof(ForgeSourceVoice));
    (*ppSourceVoice)->audio = audio;
    (*ppSourceVoice)->type = FORGE_AUDIO_VOICE_SOURCE;
    (*ppSourceVoice)->flags = Flags;
    (*ppSourceVoice)->filter.Type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    (*ppSourceVoice)->filter.Frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
    (*ppSourceVoice)->filter.OneOverQ = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
    (*ppSourceVoice)->filter.WetDryMix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    (*ppSourceVoice)->sendLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSourceVoice)->sendLock)
    (*ppSourceVoice)->effectLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSourceVoice)->effectLock)
    (*ppSourceVoice)->filterLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSourceVoice)->filterLock)
    (*ppSourceVoice)->volumeLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSourceVoice)->volumeLock)

    /* Source Properties */
    FAudio_assert(MaxFrequencyRatio <= FORGE_AUDIO_MAX_FREQ_RATIO);
    (*ppSourceVoice)->src.maxFreqRatio = MaxFrequencyRatio;

    if (    pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_PCM ||
        pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_IEEE_FLOAT ||
        pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_WMAUDIO2 ||
        pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_WMAUDIO3    )
    {
        ForgeAudioFormatExtensible *fmtex = (ForgeAudioFormatExtensible*) audio->pMalloc(
            sizeof(ForgeAudioFormatExtensible)
        );
        /* convert PCM to EXTENSIBLE */
        fmtex->Format.wFormatTag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        fmtex->Format.nChannels = pSourceFormat->nChannels;
        fmtex->Format.nSamplesPerSec = pSourceFormat->nSamplesPerSec;
        fmtex->Format.nAvgBytesPerSec = pSourceFormat->nAvgBytesPerSec;
        fmtex->Format.nBlockAlign = pSourceFormat->nBlockAlign;
        fmtex->Format.wBitsPerSample = pSourceFormat->wBitsPerSample;
        fmtex->Format.cbSize = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        fmtex->Samples.wValidBitsPerSample = pSourceFormat->wBitsPerSample;
        fmtex->dwChannelMask = 0;
        if (pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_PCM)
        {
            FAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_PCM, sizeof(ForgeGuid));
        }
        else if (pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_IEEE_FLOAT)
        {
            FAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid));
        }
        else if (pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_WMAUDIO2)
        {
            FAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_WMAUDIO2, sizeof(ForgeGuid));
        }
        else if (pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_WMAUDIO3)
        {
            FAudio_memcpy(&fmtex->SubFormat, &FORGE_AUDIO_SUBTYPE_WMAUDIO3, sizeof(ForgeGuid));
        }
        (*ppSourceVoice)->src.format = &fmtex->Format;
    }
    else if (pSourceFormat->wFormatTag == FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        ForgeXMA2Format *fmtex = (ForgeXMA2Format*) audio->pMalloc(
            sizeof(ForgeXMA2Format)
        );

        /* Copy what we can, ideally the sizes match! */
        size_t cbSize = sizeof(ForgeAudioFormat) + pSourceFormat->cbSize;
        FAudio_memcpy(
            fmtex,
            pSourceFormat,
            FAudio_min(cbSize, sizeof(ForgeXMA2Format))
        );
        if (cbSize < sizeof(ForgeXMA2Format))
        {
            FAudio_zero(
                ((uint8_t*) fmtex) + cbSize,
                sizeof(ForgeXMA2Format) - cbSize
            );
        }

        /* Does XAudio2 validate this input?! */
        fmtex->wfx.cbSize = sizeof(ForgeXMA2Format) - sizeof(ForgeAudioFormat);
        (*ppSourceVoice)->src.format = &fmtex->wfx;
    }
    else
    {
        /* direct copy anything else */
        (*ppSourceVoice)->src.format = (ForgeAudioFormat*) audio->pMalloc(
            sizeof(ForgeAudioFormat) + pSourceFormat->cbSize
        );
        FAudio_memcpy(
            (*ppSourceVoice)->src.format,
            pSourceFormat,
            sizeof(ForgeAudioFormat) + pSourceFormat->cbSize
        );
    }

    (*ppSourceVoice)->src.callback = pCallback;
    (*ppSourceVoice)->src.active = 0;
    (*ppSourceVoice)->src.freqRatio = 1.0f;
    (*ppSourceVoice)->src.totalSamples = 0;
    (*ppSourceVoice)->src.bufferLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSourceVoice)->src.bufferLock)

    if ((*ppSourceVoice)->src.format->wFormatTag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        ForgeAudioFormatExtensible *fmtex = (ForgeAudioFormatExtensible*) (*ppSourceVoice)->src.format;

        #define COMPARE_GUID(type) \
            (FAudio_memcmp( \
                &fmtex->SubFormat, \
                &DATAFORMAT_SUBTYPE_##type, \
                sizeof(ForgeGuid) \
            ) == 0)
        if (COMPARE_GUID(PCM))
        {
            #define DECODER(bit) \
                if (fmtex->Format.wBitsPerSample == bit) \
                { \
                    (*ppSourceVoice)->src.decode = FAudio_INTERNAL_DecodePCM##bit; \
                }
            DECODER(16)
            else DECODER(8)
            else DECODER(24)
            else DECODER(32)
            else
            {
                LOG_ERROR(
                    audio,
                    "Unrecognized wBitsPerSample: %d",
                    fmtex->Format.wBitsPerSample
                )
                FAudio_assert(0 && "Unrecognized wBitsPerSample!");
            }
            #undef DECODER
        }
        else if (COMPARE_GUID(IEEE_FLOAT))
        {
            /* FIXME: Weird behavior!
             * Prototype creates a source with the IEEE_FLOAT tag,
             * but it's actually PCM16. It seems to prioritize
             * wBitsPerSample over the format tag. Not sure if we
             * should fold this section into the section above...?
             * -flibit
             */
            if (fmtex->Format.wBitsPerSample == 16)
            {
                (*ppSourceVoice)->src.decode = FAudio_INTERNAL_DecodePCM16;
            }
            else
            {
                (*ppSourceVoice)->src.decode = FAudio_INTERNAL_DecodePCM32F;
            }
        }
        else if (    COMPARE_GUID(WMAUDIO2) ||
                COMPARE_GUID(WMAUDIO3) ||
                COMPARE_GUID(WMAUDIO_LOSSLESS)    )
        {
        }
        else
        {
            FAudio_assert(0 && "Unsupported WAVEFORMATEXTENSIBLE subtype!");
        }
        #undef COMPARE_GUID
    }
    else if ((*ppSourceVoice)->src.format->wFormatTag == FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        FAudio_assert(0 && "XMA2 is not supported!");
        (*ppSourceVoice)->src.decode = FAudio_INTERNAL_DecodeWMAERROR;
    }
    else
    {
        FAudio_assert(0 && "Unsupported format tag!");
    }

    if ((*ppSourceVoice)->src.format->nChannels == 1)
    {
        (*ppSourceVoice)->src.resample = FAudio_INTERNAL_ResampleMono;
    }
    else if ((*ppSourceVoice)->src.format->nChannels == 2)
    {
        (*ppSourceVoice)->src.resample = FAudio_INTERNAL_ResampleStereo;
    }
    else
    {
        (*ppSourceVoice)->src.resample = FAudio_INTERNAL_ResampleGeneric;
    }

    (*ppSourceVoice)->src.curBufferOffset = 0;

    /* Sends/Effects */
    FAudio_INTERNAL_VoiceOutputFrequency(*ppSourceVoice, pSendList);
    forge_voice_set_effect_chain(*ppSourceVoice, pEffectChain);

    /* Default Levels */
    (*ppSourceVoice)->volume = 1.0f;
    (*ppSourceVoice)->channelVolume = (float*) audio->pMalloc(
        sizeof(float) * (*ppSourceVoice)->outputChannels
    );
    for (uint32_t i = 0; i < (*ppSourceVoice)->outputChannels; i += 1)
    {
        (*ppSourceVoice)->channelVolume[i] = 1.0f;
    }

    forge_voice_set_outputs(*ppSourceVoice, pSendList);

    /* Filters */
    if (Flags & FORGE_AUDIO_VOICE_USEFILTER)
    {
        (*ppSourceVoice)->filterState = (FAudioFilterState*) audio->pMalloc(
            sizeof(FAudioFilterState) * (*ppSourceVoice)->src.format->nChannels
        );
        FAudio_zero(
            (*ppSourceVoice)->filterState,
            sizeof(FAudioFilterState) * (*ppSourceVoice)->src.format->nChannels
        );
    }

    /* Sample Storage */
    (*ppSourceVoice)->src.decodeSamples = (uint32_t) (FAudio_ceil(
        (double) audio->updateSize *
        (double) MaxFrequencyRatio *
        (double) (*ppSourceVoice)->src.format->nSamplesPerSec /
        (double) audio->master->master.inputSampleRate
    )) + EXTRA_DECODE_PADDING * (*ppSourceVoice)->src.format->nChannels;
    FAudio_INTERNAL_ResizeDecodeCache(
        audio,
        ((*ppSourceVoice)->src.decodeSamples + EXTRA_DECODE_PADDING) * (*ppSourceVoice)->src.format->nChannels
    );

    LOG_INFO(audio, "-> %p", (void*) (*ppSourceVoice))

	LOG_INFO(audio, "-> %p", (void*) (*ppSourceVoice))

	/* Add to list, finally. */
	LinkedList_PrependEntry(
		&audio->sources,
		*ppSourceVoice,
		audio->sourceLock,
		audio->pMalloc
	);

#ifdef FORGE_AUDIO_DUMP_VOICES
    FAudio_DUMPVOICE_Init(*ppSourceVoice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

    LOG_API_EXIT(audio)
    return 0;
}

uint32_t forge_audio_create_submix_voice(
    ForgeAudioEngine *audio,
    ForgeSubmixVoice **ppSubmixVoice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint32_t ProcessingStage,
    const ForgeSendList *pSendList,
    const ForgeEffectChain *pEffectChain
) {
    LOG_API_ENTER(audio)

    if (pSendList == NULL && audio->master == NULL)
    {
        LOG_ERROR(audio, "%s", "CreateSubmixVoice called before mastering voice was initialized");
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    *ppSubmixVoice = (ForgeSubmixVoice*) audio->pMalloc(sizeof(ForgeVoice));
    FAudio_zero(*ppSubmixVoice, sizeof(ForgeSubmixVoice));
    (*ppSubmixVoice)->audio = audio;
    (*ppSubmixVoice)->type = FORGE_AUDIO_VOICE_SUBMIX;
    (*ppSubmixVoice)->flags = Flags;
    (*ppSubmixVoice)->filter.Type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
    (*ppSubmixVoice)->filter.Frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
    (*ppSubmixVoice)->filter.OneOverQ = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
    (*ppSubmixVoice)->filter.WetDryMix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
    (*ppSubmixVoice)->sendLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSubmixVoice)->sendLock)
    (*ppSubmixVoice)->effectLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSubmixVoice)->effectLock)
    (*ppSubmixVoice)->filterLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSubmixVoice)->filterLock)
    (*ppSubmixVoice)->volumeLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppSubmixVoice)->volumeLock)

    /* Submix Properties */
    (*ppSubmixVoice)->mix.inputChannels = InputChannels;
    (*ppSubmixVoice)->mix.inputSampleRate = InputSampleRate;
    (*ppSubmixVoice)->mix.processingStage = ProcessingStage;

    /* Resampler */
    if (InputChannels == 1)
    {
        (*ppSubmixVoice)->mix.resample = FAudio_INTERNAL_ResampleMono;
    }
    else if (InputChannels == 2)
    {
        (*ppSubmixVoice)->mix.resample = FAudio_INTERNAL_ResampleStereo;
    }
    else
    {
        (*ppSubmixVoice)->mix.resample = FAudio_INTERNAL_ResampleGeneric;
    }

    /* Sample Storage */
    (*ppSubmixVoice)->mix.inputSamples = ((uint32_t) FAudio_ceil(
        audio->updateSize *
        (double) InputSampleRate /
        (double) audio->master->master.inputSampleRate
    ) + EXTRA_DECODE_PADDING) * InputChannels;
    (*ppSubmixVoice)->mix.inputCache = (float*) audio->pMalloc(
        sizeof(float) * (*ppSubmixVoice)->mix.inputSamples
    );
    FAudio_zero( /* Zero this now, for the first update */
        (*ppSubmixVoice)->mix.inputCache,
        sizeof(float) * (*ppSubmixVoice)->mix.inputSamples
    );

    /* Sends/Effects */
    FAudio_INTERNAL_VoiceOutputFrequency(*ppSubmixVoice, pSendList);
    forge_voice_set_effect_chain(*ppSubmixVoice, pEffectChain);

    /* Default Levels */
    (*ppSubmixVoice)->volume = 1.0f;
    (*ppSubmixVoice)->channelVolume = (float*) audio->pMalloc(
        sizeof(float) * (*ppSubmixVoice)->outputChannels
    );
    for (uint32_t i = 0; i < (*ppSubmixVoice)->outputChannels; i += 1)
    {
        (*ppSubmixVoice)->channelVolume[i] = 1.0f;
    }

    forge_voice_set_outputs(*ppSubmixVoice, pSendList);

    /* Filters */
    if (Flags & FORGE_AUDIO_VOICE_USEFILTER)
    {
        (*ppSubmixVoice)->filterState = (FAudioFilterState*) audio->pMalloc(
            sizeof(FAudioFilterState) * InputChannels
        );
        FAudio_zero(
            (*ppSubmixVoice)->filterState,
            sizeof(FAudioFilterState) * InputChannels
        );
    }

	/* Add to list, finally. */
	FAudio_INTERNAL_InsertSubmixSorted(
		&audio->submixes,
		*ppSubmixVoice,
		audio->submixLock,
		audio->pMalloc
	);

	LOG_API_EXIT(audio)
	return 0;
}

uint32_t forge_audio_create_master_voice(
    ForgeAudioEngine *audio,
    ForgeMasterVoice **ppMasteringVoice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint32_t DeviceIndex,
    const ForgeEffectChain *pEffectChain
) {
    LOG_API_ENTER(audio)

    /* For now we only support one allocated master voice at a time */
    FAudio_assert(audio->master == NULL);

    if (    InputChannels == FORGE_AUDIO_DEFAULT_CHANNELS ||
        InputSampleRate == FORGE_AUDIO_DEFAULT_SAMPLERATE    )
    {
        ForgeDeviceDetails details;
        if (forge_audio_get_device_details(audio, DeviceIndex, &details) != 0)
        {
            return FORGE_AUDIO_E_INVALID_CALL;
        }
        if (InputChannels == FORGE_AUDIO_DEFAULT_CHANNELS)
        {
            InputChannels = details.OutputFormat.Format.nChannels;
        }
        if (InputSampleRate == FORGE_AUDIO_DEFAULT_SAMPLERATE)
        {
            InputSampleRate = details.OutputFormat.Format.nSamplesPerSec;
        }
    }

    *ppMasteringVoice = (ForgeMasterVoice*) audio->pMalloc(sizeof(ForgeVoice));
    FAudio_zero(*ppMasteringVoice, sizeof(ForgeMasterVoice));
    (*ppMasteringVoice)->audio = audio;
    (*ppMasteringVoice)->type = FORGE_AUDIO_VOICE_MASTER;
    (*ppMasteringVoice)->flags = Flags;
    (*ppMasteringVoice)->effectLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppMasteringVoice)->effectLock)
    (*ppMasteringVoice)->volumeLock = FAudio_PlatformCreateMutex();
    LOG_MUTEX_CREATE(audio, (*ppMasteringVoice)->volumeLock)

    /* Default Levels */
    (*ppMasteringVoice)->volume = 1.0f;

    /* Master Properties */
    (*ppMasteringVoice)->master.inputChannels = InputChannels;
    (*ppMasteringVoice)->master.inputSampleRate = InputSampleRate;

    /* Sends/Effects */
    FAudio_zero(&(*ppMasteringVoice)->sends, sizeof(ForgeSendList));
    forge_voice_set_effect_chain(*ppMasteringVoice, pEffectChain);

    /* This is now safe enough to assign */
    audio->master = *ppMasteringVoice;

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
	FAudio_PlatformInit(
		audio,
		audio->initFlags,
		DeviceIndex,
		&audio->mixFormat,
		&audio->updateSize,
		&audio->platform
	);
	if (audio->platform == NULL)
	{
		forge_voice_destroy(*ppMasteringVoice);
		*ppMasteringVoice = NULL;

        /* Not the best code, but it's probably true? */
        return FORGE_AUDIO_E_DEVICE_INVALIDATED;
    }
    audio->master->outputChannels = audio->mixFormat.Format.nChannels;
    audio->master->master.inputSampleRate = audio->mixFormat.Format.nSamplesPerSec;

    /* Effect Chain Cache */
    if ((*ppMasteringVoice)->master.inputChannels != (*ppMasteringVoice)->outputChannels)
    {
        (*ppMasteringVoice)->master.effectCache = (float*) audio->pMalloc(
            sizeof(float) *
            audio->updateSize *
            (*ppMasteringVoice)->master.inputChannels
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
    audio->pClientEngineProc = clientEngineProc;
    audio->clientEngineUser = user;
    LOG_API_EXIT(audio)
}

uint32_t forge_audio_start_engine(ForgeAudioEngine *audio)
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
    FAudio_OPERATIONSET_CommitAll(audio);
    FAudio_OPERATIONSET_Execute(audio);
    LOG_API_EXIT(audio)
}

uint32_t forge_audio_commit_operation_set(ForgeAudioEngine *audio, uint32_t OperationSet)
{
    LOG_API_ENTER(audio)
    if (OperationSet == FORGE_AUDIO_COMMIT_ALL)
    {
        FAudio_OPERATIONSET_CommitAll(audio);
    }
    else
    {
        FAudio_OPERATIONSET_Commit(audio, OperationSet);
    }
    LOG_API_EXIT(audio)
    return 0;
}

void forge_audio_get_performance_data(
    ForgeAudioEngine *audio,
    ForgePerformanceData *pPerfData
) {
    LinkedList *list;
    ForgeSourceVoice *source;

    LOG_API_ENTER(audio)

    FAudio_zero(pPerfData, sizeof(ForgePerformanceData));

    FAudio_PlatformLockMutex(audio->sourceLock);
    LOG_MUTEX_LOCK(audio, audio->sourceLock)
    list = audio->sources;
    while (list != NULL)
    {
        source = (ForgeSourceVoice*) list->entry;
        pPerfData->TotalSourceVoiceCount += 1;
        if (source->src.active)
        {
            pPerfData->ActiveSourceVoiceCount += 1;
        }
        list = list->next;
    }
    FAudio_PlatformUnlockMutex(audio->sourceLock);
    LOG_MUTEX_UNLOCK(audio, audio->sourceLock)

    FAudio_PlatformLockMutex(audio->submixLock);
    LOG_MUTEX_LOCK(audio, audio->submixLock)
    list = audio->submixes;
    while (list != NULL)
    {
        pPerfData->ActiveSubmixVoiceCount += 1;
        list = list->next;
    }
    FAudio_PlatformUnlockMutex(audio->submixLock);
    LOG_MUTEX_UNLOCK(audio, audio->submixLock)

    if (audio->master != NULL)
    {
        /* estimate, should use real latency from platform */
        pPerfData->CurrentLatencyInSamples = 2 * audio->updateSize;
    }

    LOG_API_EXIT(audio)
}

void forge_audio_set_debug_configuration(
    ForgeAudioEngine *audio,
    ForgeDebugConfiguration *pDebugConfiguration,
    void* pReserved
) {
#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    char *env;

    LOG_API_ENTER(audio)

    FAudio_memcpy(
        &audio->debug,
        pDebugConfiguration,
        sizeof(ForgeDebugConfiguration)
    );

    env = FAudio_getenv("FORGE_AUDIO_LOG_EVERYTHING");
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
        env = FAudio_getenv("FORGE_AUDIO_LOG_" #type); \
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
        env = FAudio_getenv("FORGE_AUDIO_LOG_LOG" #envvar); \
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
    FAudio_assert(audio->master != NULL);
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

static void FAudio_RecalcMixMatrix(ForgeVoice *voice, uint32_t sendIndex)
{
    uint32_t oChan, s, d;
    ForgeVoice *out = voice->sends.pSends[sendIndex].pOutputVoice;
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
    ForgeVoiceDetails *pVoiceDetails
) {
    LOG_API_ENTER(voice->audio)

    pVoiceDetails->CreationFlags = voice->flags;
    pVoiceDetails->ActiveFlags = voice->flags;
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
    {
        pVoiceDetails->InputChannels = voice->src.format->nChannels;
        pVoiceDetails->InputSampleRate = voice->src.format->nSamplesPerSec;
    }
    else if (voice->type == FORGE_AUDIO_VOICE_SUBMIX)
    {
        pVoiceDetails->InputChannels = voice->mix.inputChannels;
        pVoiceDetails->InputSampleRate = voice->mix.inputSampleRate;
    }
    else if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        pVoiceDetails->InputChannels = voice->master.inputChannels;
        pVoiceDetails->InputSampleRate = voice->master.inputSampleRate;
    }
    else
    {
        FAudio_assert(0 && "Unknown voice type!");
    }

    LOG_API_EXIT(voice->audio)
}

uint32_t forge_voice_set_outputs(
    ForgeVoice *voice,
    const ForgeSendList *pSendList
) {
    uint32_t sendRate, nextRate, outChannels;
    ForgeSendList defaultSends;
    ForgeSend defaultSend;

    LOG_API_ENTER(voice->audio)

    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (pSendList != NULL && pSendList->SendCount > 1)
    {
        if (pSendList->pSends[0].pOutputVoice->type == FORGE_AUDIO_VOICE_SOURCE)
        {
            LOG_API_EXIT(voice->audio)
            return FORGE_AUDIO_E_INVALID_CALL;
        }
        sendRate = (pSendList->pSends[0].pOutputVoice->type == FORGE_AUDIO_VOICE_MASTER) ?
            pSendList->pSends[0].pOutputVoice->master.inputSampleRate :
            pSendList->pSends[0].pOutputVoice->mix.inputSampleRate;
        for (uint32_t i = 0; i < pSendList->SendCount; i += 1)
        {
            nextRate = (pSendList->pSends[i].pOutputVoice->type == FORGE_AUDIO_VOICE_MASTER) ?
                pSendList->pSends[i].pOutputVoice->master.inputSampleRate :
                pSendList->pSends[i].pOutputVoice->mix.inputSampleRate;
            if (nextRate != sendRate)
            {
                LOG_API_EXIT(voice->audio)
                return FORGE_AUDIO_E_INVALID_CALL;
            }
        }
    }

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (FAudio_INTERNAL_VoiceOutputFrequency(voice, pSendList) != 0)
    {
        LOG_ERROR(
            voice->audio,
            "%s",
            "Changing the sample rate while an effect chain is attached is invalid!"
        )
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    FAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    /* FIXME: This is lazy... */
    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        voice->audio->pFree(voice->sendCoefficients[i]);
    }
    if (voice->sendCoefficients != NULL)
    {
        voice->audio->pFree(voice->sendCoefficients);
    }
    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        voice->audio->pFree(voice->mixCoefficients[i]);
    }
    if (voice->mixCoefficients != NULL)
    {
        voice->audio->pFree(voice->mixCoefficients);
    }
    if (voice->sendMix != NULL)
    {
        voice->audio->pFree(voice->sendMix);
    }
    if (voice->sendFilter != NULL)
    {
        voice->audio->pFree(voice->sendFilter);
        voice->sendFilter = NULL;
    }
    if (voice->sendFilterState != NULL)
    {
        for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
        {
            if (voice->sendFilterState[i] != NULL)
            {
                voice->audio->pFree(voice->sendFilterState[i]);
            }
        }
        voice->audio->pFree(voice->sendFilterState);
        voice->sendFilterState = NULL;
    }
    if (voice->sends.pSends != NULL)
    {
        voice->audio->pFree(voice->sends.pSends);
    }

    if (pSendList == NULL)
    {
        /* Default to the mastering voice as output */
        defaultSend.Flags = 0;
        defaultSend.pOutputVoice = voice->audio->master;
        defaultSends.SendCount = 1;
        defaultSends.pSends = &defaultSend;
        pSendList = &defaultSends;
    }
    else if (pSendList->SendCount == 0)
    {
        /* No sends? Nothing to do... */
        voice->sendCoefficients = NULL;
        voice->mixCoefficients = NULL;
        voice->sendMix = NULL;
        FAudio_zero(&voice->sends, sizeof(ForgeSendList));

        FAudio_PlatformUnlockMutex(voice->volumeLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Copy send list */
    voice->sends.SendCount = pSendList->SendCount;
    voice->sends.pSends = (ForgeSend*) voice->audio->pMalloc(
        pSendList->SendCount * sizeof(ForgeSend)
    );
    FAudio_memcpy(
        voice->sends.pSends,
        pSendList->pSends,
        pSendList->SendCount * sizeof(ForgeSend)
    );

    /* Allocate/Reset default output matrix, mixer function, filters */
    voice->sendCoefficients = (float**) voice->audio->pMalloc(
        sizeof(float*) * pSendList->SendCount
    );
    voice->mixCoefficients = (float**) voice->audio->pMalloc(
        sizeof(float*) * pSendList->SendCount
    );
    voice->sendMix = (FAudioMixCallback*) voice->audio->pMalloc(
        sizeof(FAudioMixCallback) * pSendList->SendCount
    );

    for (uint32_t i = 0; i < pSendList->SendCount; i += 1)
    {
        if (pSendList->pSends[i].pOutputVoice->type == FORGE_AUDIO_VOICE_MASTER)
        {
            outChannels = pSendList->pSends[i].pOutputVoice->master.inputChannels;
        }
        else
        {
            outChannels = pSendList->pSends[i].pOutputVoice->mix.inputChannels;
        }
        voice->sendCoefficients[i] = (float*) voice->audio->pMalloc(
            sizeof(float) * voice->outputChannels * outChannels
        );
        voice->mixCoefficients[i] = (float*) voice->audio->pMalloc(
            sizeof(float) * voice->outputChannels * outChannels
        );

        FAudio_assert(voice->outputChannels > 0 && voice->outputChannels < 9);
        FAudio_assert(outChannels > 0 && outChannels < 9);
        FAudio_memcpy(
            voice->sendCoefficients[i],
            FORGE_AUDIO_INTERNAL_MATRIX_DEFAULTS[voice->outputChannels - 1][outChannels - 1],
            voice->outputChannels * outChannels * sizeof(float)
        );
        FAudio_RecalcMixMatrix(voice, i);

        if (voice->outputChannels == 1)
        {
            if (outChannels == 1)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_1in_1out_Scalar;
            }
            else if (outChannels == 2)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_1in_2out_Scalar;
            }
            else if (outChannels == 6)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_1in_6out_Scalar;
            }
            else if (outChannels == 8)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_1in_8out_Scalar;
            }
            else
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_Generic;
            }
        }
        else if (voice->outputChannels == 2)
        {
            if (outChannels == 1)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_2in_1out_Scalar;
            }
            else if (outChannels == 2)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_2in_2out_Scalar;
            }
            else if (outChannels == 6)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_2in_6out_Scalar;
            }
            else if (outChannels == 8)
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_2in_8out_Scalar;
            }
            else
            {
                voice->sendMix[i] = FAudio_INTERNAL_Mix_Generic;
            }
        }
        else
        {
            voice->sendMix[i] = FAudio_INTERNAL_Mix_Generic;
        }

        if (pSendList->pSends[i].Flags & FORGE_AUDIO_SEND_USEFILTER)
        {
            /* Allocate the whole send filter array if needed... */
            if (voice->sendFilter == NULL)
            {
                voice->sendFilter = (ForgeFilterParameters*) voice->audio->pMalloc(
                    sizeof(ForgeFilterParameters) * pSendList->SendCount
                );
            }
            if (voice->sendFilterState == NULL)
            {
                voice->sendFilterState = (FAudioFilterState**) voice->audio->pMalloc(
                    sizeof(FAudioFilterState*) * pSendList->SendCount
                );
                FAudio_zero(
                    voice->sendFilterState,
                    sizeof(FAudioFilterState*) * pSendList->SendCount
                );
            }

            /* ... then fill in this send's filter data */
            voice->sendFilter[i].Type = FORGE_AUDIO_DEFAULT_FILTER_TYPE;
            voice->sendFilter[i].Frequency = FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY;
            voice->sendFilter[i].OneOverQ = FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ;
            voice->sendFilter[i].WetDryMix = FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX;
            voice->sendFilterState[i] = (FAudioFilterState*) voice->audio->pMalloc(
                sizeof(FAudioFilterState) * outChannels
            );
            FAudio_zero(
                voice->sendFilterState[i],
                sizeof(FAudioFilterState) * outChannels
            );
        }
    }

    FAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_voice_set_effect_chain(
    ForgeVoice *voice,
    const ForgeEffectChain *pEffectChain
) {
    FAPO *fapo;
    uint32_t channelCount;
    ForgeVoiceDetails voiceDetails;
    FAPORegistrationProperties *pProps;
    ForgeAudioFormatExtensible srcFmt, dstFmt;
    FAPOLockForProcessBufferParameters srcLockParams, dstLockParams;

    LOG_API_ENTER(voice->audio)

    forge_voice_get_details(voice, &voiceDetails);

    /* SetEffectChain must not change the number of output channels once the voice has been created */
    if (pEffectChain == NULL && voice->outputChannels != 0)
    {
        /* cannot remove an effect chain that changes the number of channels */
        if (voice->outputChannels != voiceDetails.InputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "%s",
                "Cannot remove effect chain that changes the number of channels"
            )
            FAudio_assert(0 && "Cannot remove effect chain that changes the number of channels");
            LOG_API_EXIT(voice->audio)
            return FORGE_AUDIO_E_INVALID_CALL;
        }
    }

    if (pEffectChain != NULL && voice->outputChannels != 0)
    {
        uint32_t lst = pEffectChain->EffectCount - 1;

        /* new effect chain must have same number of output channels */
        if (voice->outputChannels != pEffectChain->pEffectDescriptors[lst].OutputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "%s",
                "New effect chain must have same number of output channels as the old chain"
            )
            FAudio_assert(0 && "New effect chain must have same number of output channels as the old chain");
            LOG_API_EXIT(voice->audio)
            return FORGE_AUDIO_E_INVALID_CALL;
        }
    }

    FAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)

    if (pEffectChain == NULL)
    {
        FAudio_INTERNAL_FreeEffectChain(voice);
        FAudio_zero(&voice->effects, sizeof(voice->effects));
        voice->outputChannels = voiceDetails.InputChannels;
    }
    else
    {
        /* Validate incoming chain before changing the current chain */

        /* These are always the same, so just write them now. */
        srcLockParams.pFormat = &srcFmt.Format;
        dstLockParams.pFormat = &dstFmt.Format;
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
        srcFmt.Format.wBitsPerSample = 32;
        srcFmt.Format.wFormatTag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
        srcFmt.Format.nChannels = voiceDetails.InputChannels;
        srcFmt.Format.nSamplesPerSec = voiceDetails.InputSampleRate;
        srcFmt.Format.nBlockAlign = srcFmt.Format.nChannels * (srcFmt.Format.wBitsPerSample / 8);
        srcFmt.Format.nAvgBytesPerSec = srcFmt.Format.nSamplesPerSec * srcFmt.Format.nBlockAlign;
        srcFmt.Format.cbSize = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        srcFmt.Samples.wValidBitsPerSample = srcFmt.Format.wBitsPerSample;
        srcFmt.dwChannelMask = 0;
        FAudio_memcpy(&srcFmt.SubFormat, &FORGE_AUDIO_SUBTYPE_IEEE_FLOAT, sizeof(ForgeGuid));
        FAudio_memcpy(&dstFmt, &srcFmt, sizeof(srcFmt));

        for (uint32_t i = 0; i < pEffectChain->EffectCount; i += 1)
        {
            fapo = pEffectChain->pEffectDescriptors[i].pEffect;

            /* ... then we get this effect's format... */
            dstFmt.Format.nChannels = pEffectChain->pEffectDescriptors[i].OutputChannels;
            dstFmt.Format.nBlockAlign = dstFmt.Format.nChannels * (dstFmt.Format.wBitsPerSample / 8);
            dstFmt.Format.nAvgBytesPerSec = dstFmt.Format.nSamplesPerSec * dstFmt.Format.nBlockAlign;

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
                FAudio_assert(0 && "Effect output format not supported");
                FAudio_PlatformUnlockMutex(voice->effectLock);
                LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
                LOG_API_EXIT(voice->audio)
                return FORGE_AUDIO_E_UNSUPPORTED_FORMAT;
            }

            /* Okay, now this effect is the source and the next
             * effect will be the destination. Repeat until no
             * effects left.
             */
            FAudio_memcpy(&srcFmt, &dstFmt, sizeof(srcFmt));
        }

        FAudio_INTERNAL_FreeEffectChain(voice);
        FAudio_INTERNAL_AllocEffectChain(
            voice,
            pEffectChain
        );

        /* check if in-place processing is supported */
        channelCount = voiceDetails.InputChannels;
        for (uint32_t i = 0; i < voice->effects.count; i += 1)
        {
            fapo = voice->effects.desc[i].pEffect;
            if (fapo->GetRegistrationProperties(fapo, &pProps) == 0)
            {
                voice->effects.inPlaceProcessing[i] = (pProps->Flags & FAPO_FLAG_INPLACE_SUPPORTED) == FAPO_FLAG_INPLACE_SUPPORTED;
                voice->effects.inPlaceProcessing[i] &= (channelCount == voice->effects.desc[i].OutputChannels);
                channelCount = voice->effects.desc[i].OutputChannels;

                /* Fails if in-place processing is mandatory and
                 * the chain forces us to do otherwise...
                 */
                FAudio_assert(
                    !(pProps->Flags & FAPO_FLAG_INPLACE_REQUIRED) ||
                    voice->effects.inPlaceProcessing[i]
                );

                voice->audio->pFree(pProps);
            }
        }
        voice->outputChannels = channelCount;
    }

    FAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_voice_enable_effect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueEnableEffect(
            voice,
            EffectIndex,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    FAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[EffectIndex].InitialState = 1;
    FAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_voice_disable_effect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueDisableEffect(
            voice,
            EffectIndex,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    FAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    voice->effects.desc[EffectIndex].InitialState = 0;
    FAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_effect_state(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    int32_t *pEnabled
) {
    LOG_API_ENTER(voice->audio)
    FAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    *pEnabled = voice->effects.desc[EffectIndex].InitialState;
    FAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_voice_set_effect_parameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    const void *pParameters,
    uint32_t ParametersByteSize,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetEffectParameters(
            voice,
            EffectIndex,
            pParameters,
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
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (voice->effects.parameters[EffectIndex] == NULL)
    {
        voice->effects.parameters[EffectIndex] = voice->audio->pMalloc(
            ParametersByteSize
        );
        voice->effects.parameterSizes[EffectIndex] = ParametersByteSize;
    }
    FAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    if (voice->effects.parameterSizes[EffectIndex] < ParametersByteSize)
    {
        voice->effects.parameters[EffectIndex] = voice->audio->pRealloc(
            voice->effects.parameters[EffectIndex],
            ParametersByteSize
        );
        voice->effects.parameterSizes[EffectIndex] = ParametersByteSize;
    }
    FAudio_memcpy(
        voice->effects.parameters[EffectIndex],
        pParameters,
        ParametersByteSize
    );
    voice->effects.parameterUpdates[EffectIndex] = 1;
    FAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_voice_get_effect_parameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    void *pParameters,
    uint32_t ParametersByteSize
) {
    FAPO *fapo;
    LOG_API_ENTER(voice->audio)
    FAudio_PlatformLockMutex(voice->effectLock);
    LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
    fapo = voice->effects.desc[EffectIndex].pEffect;
    fapo->GetParameters(fapo, pParameters, ParametersByteSize);
    FAudio_PlatformUnlockMutex(voice->effectLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_voice_set_filter_parameters(
    ForgeVoice *voice,
    const ForgeFilterParameters *pParameters,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetFilterParameters(
            voice,
            pParameters,
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

    FAudio_PlatformLockMutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    FAudio_memcpy(
        &voice->filter,
        pParameters,
        sizeof(ForgeFilterParameters)
    );
    FAudio_PlatformUnlockMutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_filter_parameters(
    ForgeVoice *voice,
    ForgeFilterParameters *pParameters
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

    FAudio_PlatformLockMutex(voice->filterLock);
    LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
    FAudio_memcpy(
        pParameters,
        &voice->filter,
        sizeof(ForgeFilterParameters)
    );
    FAudio_PlatformUnlockMutex(voice->filterLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_voice_set_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    const ForgeFilterParameters *pParameters,
    uint32_t OperationSet
) {
    uint32_t i;
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetOutputFilterParameters(
            voice,
            pDestinationVoice,
            pParameters,
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

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (pDestinationVoice == NULL && voice->sends.SendCount == 1)
    {
        pDestinationVoice = voice->sends.pSends[0].pOutputVoice;
    }
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (pDestinationVoice == voice->sends.pSends[i].pOutputVoice)
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
            (void*) pDestinationVoice
        )
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (!(voice->sends.pSends[i].Flags & FORGE_AUDIO_SEND_USEFILTER))
    {
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    /* Set the filter parameters, finally. */
    FAudio_memcpy(
        &voice->sendFilter[i],
        pParameters,
        sizeof(ForgeFilterParameters)
    );

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    ForgeFilterParameters *pParameters
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

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (pDestinationVoice == NULL && voice->sends.SendCount == 1)
    {
        pDestinationVoice = voice->sends.pSends[0].pOutputVoice;
    }
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (pDestinationVoice == voice->sends.pSends[i].pOutputVoice)
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
            (void*) pDestinationVoice
        )
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    if (!(voice->sends.pSends[i].Flags & FORGE_AUDIO_SEND_USEFILTER))
    {
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Set the filter parameters, finally. */
    FAudio_memcpy(
        pParameters,
        &voice->sendFilter[i],
        sizeof(ForgeFilterParameters)
    );

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_voice_set_volume(
    ForgeVoice *voice,
    float Volume,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetVolume(
            voice,
            Volume,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    FAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    voice->volume = FAudio_clamp(
        Volume,
        -FORGE_AUDIO_MAX_VOLUME_LEVEL,
        FORGE_AUDIO_MAX_VOLUME_LEVEL
    );

    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        FAudio_RecalcMixMatrix(voice, i);
    }

    FAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_volume(
    ForgeVoice *voice,
    float *pVolume
) {
    LOG_API_ENTER(voice->audio)
    *pVolume = voice->volume;
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_voice_set_channel_volumes(
    ForgeVoice *voice,
    uint32_t Channels,
    const float *pVolumes,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetChannelVolumes(
            voice,
            Channels,
            pVolumes,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    if (pVolumes == NULL)
    {
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (voice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (Channels != voice->outputChannels)
    {
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    FAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    FAudio_memcpy(
        voice->channelVolume,
        pVolumes,
        sizeof(float) * Channels
    );

    for (uint32_t i = 0; i < voice->sends.SendCount; i += 1)
    {
        FAudio_RecalcMixMatrix(voice, i);
    }

    FAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_voice_get_channel_volumes(
    ForgeVoice *voice,
    uint32_t Channels,
    float *pVolumes
) {
    LOG_API_ENTER(voice->audio)
    FAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
    FAudio_memcpy(
        pVolumes,
        voice->channelVolume,
        sizeof(float) * Channels
    );
    FAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_voice_set_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    const float *pLevelMatrix,
    uint32_t OperationSet
) {
    uint32_t i, result = 0;
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetOutputMatrix(
            voice,
            pDestinationVoice,
            SourceChannels,
            DestinationChannels,
            pLevelMatrix,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    if (pDestinationVoice == NULL && voice->sends.SendCount == 1)
    {
        pDestinationVoice = voice->sends.pSends[0].pOutputVoice;
    }
    FAudio_assert(pDestinationVoice != NULL);
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (pDestinationVoice == voice->sends.pSends[i].pOutputVoice)
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
            (void*) pDestinationVoice
        )
        result = FORGE_AUDIO_E_INVALID_CALL;
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
        result = FORGE_AUDIO_E_INVALID_CALL;
        goto end;
    }

    if (pDestinationVoice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        if (DestinationChannels != pDestinationVoice->master.inputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "DestinationChannels not equal to master channel count: %p %d %d",
                (void*) pDestinationVoice,
                DestinationChannels,
                pDestinationVoice->master.inputChannels
            )
            result = FORGE_AUDIO_E_INVALID_CALL;
            goto end;
        }
    }
    else
    {
        if (DestinationChannels != pDestinationVoice->mix.inputChannels)
        {
            LOG_ERROR(
                voice->audio,
                "DestinationChannels not equal to submix channel count: %p %d %d",
                (void*) pDestinationVoice,
                DestinationChannels,
                pDestinationVoice->mix.inputChannels
            )
            result = FORGE_AUDIO_E_INVALID_CALL;
            goto end;
        }
    }

    /* Set the matrix values, finally */
    FAudio_PlatformLockMutex(voice->volumeLock);
    LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)

    FAudio_memcpy(
        voice->sendCoefficients[i],
        pLevelMatrix,
        sizeof(float) * SourceChannels * DestinationChannels
    );

    FAudio_RecalcMixMatrix(voice, i);

    FAudio_PlatformUnlockMutex(voice->volumeLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)

end:
    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
    return result;
}

void forge_voice_get_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    float *pLevelMatrix
) {
    uint32_t i;

    LOG_API_ENTER(voice->audio)
    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    /* Find the send index */
    for (i = 0; i < voice->sends.SendCount; i += 1)
    {
        if (pDestinationVoice == voice->sends.pSends[i].pOutputVoice)
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
            (void*) pDestinationVoice
        )
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return;
    }

    /* Verify the Source/Destination channel count */
    if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
    {
        FAudio_assert(SourceChannels == voice->src.format->nChannels);
    }
    else
    {
        FAudio_assert(SourceChannels == voice->mix.inputChannels);
    }
    if (pDestinationVoice->type == FORGE_AUDIO_VOICE_MASTER)
    {
        FAudio_assert(DestinationChannels == pDestinationVoice->master.inputChannels);
    }
    else
    {
        FAudio_assert(DestinationChannels == pDestinationVoice->mix.inputChannels);
    }

    /* Get the matrix values, finally */
    FAudio_memcpy(
        pLevelMatrix,
        voice->sendCoefficients[i],
        sizeof(float) * SourceChannels * DestinationChannels
    );

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
    LOG_API_EXIT(voice->audio)
}

static uint32_t check_for_sends_to_voice(ForgeVoice *voice)
{
	ForgeAudioEngine *audio = voice->audio;
	uint32_t ret = 0;
	ForgeSourceVoice *source;
	ForgeSubmixVoice *submix;
	LinkedList *list;
	uint32_t i;

	FAudio_PlatformLockMutex(audio->sourceLock);
	list = audio->sources;
	while (list != NULL)
	{
		source = (ForgeSourceVoice*) list->entry;
		for (i = 0; i < source->sends.SendCount; i += 1)
			if (source->sends.pSends[i].pOutputVoice == voice)
			{
				ret = 0x80004005; /* E_FAIL */
				break;
			}
		if (ret)
			break;
		list = list->next;
	}
	FAudio_PlatformUnlockMutex(audio->sourceLock);

	if (ret)
		return ret;

	FAudio_PlatformLockMutex(audio->submixLock);
	list = audio->submixes;
	while (list != NULL)
	{
		submix = (ForgeSubmixVoice*) list->entry;
		for (i = 0; i < submix->sends.SendCount; i += 1)
			if (submix->sends.pSends[i].pOutputVoice == voice)
			{
				ret = 0x80004005; /* E_FAIL */
				break;
			}
		if (ret)
			break;
		list = list->next;
	}
	FAudio_PlatformUnlockMutex(audio->submixLock);

	return ret;
}

static void destroy_voice(ForgeVoice *voice)
{
	uint32_t i;

	/* TODO: Check for dependencies and remove from audio graph first! */
	FAudio_OPERATIONSET_ClearAllForVoice(voice);

	if (voice->type == FORGE_AUDIO_VOICE_SOURCE)
	{
#ifdef FORGE_AUDIO_DUMP_VOICES
		FAudio_DUMPVOICE_Finalize((ForgeSourceVoice*) voice);
#endif /* FORGE_AUDIO_DUMP_VOICES */

		FAudio_PlatformLockMutex(voice->audio->sourceLock);
		LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
		while (voice == voice->audio->processingSource)
		{
			FAudio_PlatformUnlockMutex(voice->audio->sourceLock);
			LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)
			FAudio_PlatformLockMutex(voice->audio->sourceLock);
			LOG_MUTEX_LOCK(voice->audio, voice->audio->sourceLock)
		}
		LinkedList_RemoveEntry(
			&voice->audio->sources,
			voice,
			voice->audio->sourceLock,
			voice->audio->pFree
		);
		FAudio_PlatformUnlockMutex(voice->audio->sourceLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->audio->sourceLock)

		voice->audio->pFree(voice->src.queued_buffers);
		voice->audio->pFree(voice->src.flush_buffers);
		voice->audio->pFree(voice->src.format);
		LOG_MUTEX_DESTROY(voice->audio, voice->src.bufferLock)
		FAudio_PlatformDestroyMutex(voice->src.bufferLock);
#ifdef HAVE_WMADEC
		if (voice->src.wmadec)
		{
			FAudio_WMADEC_free(voice);
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
			voice->audio->pFree
		);

		/* Delete submix data */
		voice->audio->pFree(voice->mix.inputCache);
	}
	else if (voice->type == FORGE_AUDIO_VOICE_MASTER)
	{
		if (voice->audio->platform != NULL)
		{
			FAudio_PlatformQuit(voice->audio->platform);
			voice->audio->platform = NULL;
		}
		if (voice->master.effectCache != NULL)
		{
			voice->audio->pFree(voice->master.effectCache);
		}
		voice->audio->master = NULL;
	}

	if (voice->sendLock != NULL)
	{
		FAudio_PlatformLockMutex(voice->sendLock);
		LOG_MUTEX_LOCK(voice->audio, voice->sendLock)
		for (i = 0; i < voice->sends.SendCount; i += 1)
		{
			voice->audio->pFree(voice->sendCoefficients[i]);
		}
		if (voice->sendCoefficients != NULL)
		{
			voice->audio->pFree(voice->sendCoefficients);
		}
		for (i = 0; i < voice->sends.SendCount; i += 1)
		{
			voice->audio->pFree(voice->mixCoefficients[i]);
		}
		if (voice->mixCoefficients != NULL)
		{
			voice->audio->pFree(voice->mixCoefficients);
		}
		if (voice->sendMix != NULL)
		{
			voice->audio->pFree(voice->sendMix);
		}
		if (voice->sendFilter != NULL)
		{
			voice->audio->pFree(voice->sendFilter);
		}
		if (voice->sendFilterState != NULL)
		{
			for (i = 0; i < voice->sends.SendCount; i += 1)
			{
				if (voice->sendFilterState[i] != NULL)
				{
					voice->audio->pFree(voice->sendFilterState[i]);
				}
			}
			voice->audio->pFree(voice->sendFilterState);
		}
		if (voice->sends.pSends != NULL)
		{
			voice->audio->pFree(voice->sends.pSends);
		}
		FAudio_PlatformUnlockMutex(voice->sendLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->sendLock)
		FAudio_PlatformDestroyMutex(voice->sendLock);
	}

	if (voice->effectLock != NULL)
	{
		FAudio_PlatformLockMutex(voice->effectLock);
		LOG_MUTEX_LOCK(voice->audio, voice->effectLock)
		FAudio_INTERNAL_FreeEffectChain(voice);
		FAudio_PlatformUnlockMutex(voice->effectLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->effectLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->effectLock)
		FAudio_PlatformDestroyMutex(voice->effectLock);
	}

	if (voice->filterLock != NULL)
	{
		FAudio_PlatformLockMutex(voice->filterLock);
		LOG_MUTEX_LOCK(voice->audio, voice->filterLock)
		if (voice->filterState != NULL)
		{
			voice->audio->pFree(voice->filterState);
		}
		FAudio_PlatformUnlockMutex(voice->filterLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->filterLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->filterLock)
		FAudio_PlatformDestroyMutex(voice->filterLock);
	}

	if (voice->volumeLock != NULL)
	{
		FAudio_PlatformLockMutex(voice->volumeLock);
		LOG_MUTEX_LOCK(voice->audio, voice->volumeLock)
		if (voice->channelVolume != NULL)
		{
			voice->audio->pFree(voice->channelVolume);
		}
		FAudio_PlatformUnlockMutex(voice->volumeLock);
		LOG_MUTEX_UNLOCK(voice->audio, voice->volumeLock)
		LOG_MUTEX_DESTROY(voice->audio, voice->volumeLock)
		FAudio_PlatformDestroyMutex(voice->volumeLock);
	}

	voice->audio->pFree(voice);
}

uint32_t forge_voice_try_destroy(ForgeVoice *voice)
{
	uint32_t ret;

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

uint32_t forge_source_voice_start(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueStart(
            voice,
            Flags,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }


    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    FAudio_assert(Flags == 0);
    voice->src.active = 1;
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_source_voice_stop(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueStop(
            voice,
            Flags,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

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

uint32_t forge_source_voice_submit_buffer(
    ForgeSourceVoice *voice,
    const ForgeBuffer *pBuffer,
    const ForgeBufferWMA *pBufferWMA
) {
    const uint32_t block_size = voice->src.format->nBlockAlign;
	uint32_t playBegin, playLength, loopBegin, loopLength, bufferLength;
	struct queued_buffer *entry;

    LOG_API_ENTER(voice->audio)
    LOG_INFO(
        voice->audio,
        "%p: {Flags: 0x%x, AudioBytes: %u, pAudioData: %p, Play: %u + %u, Loop: %u + %u x %u}",
        (void*) voice,
        pBuffer->Flags,
        pBuffer->AudioBytes,
        (const void*) pBuffer->pAudioData,
        pBuffer->PlayBegin,
        pBuffer->PlayLength,
        pBuffer->LoopBegin,
        pBuffer->LoopLength,
        pBuffer->LoopCount
    )

    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (block_size == 0)
    {
        LOG_ERROR(voice->audio, "%s", "Source voice has zero block alignment");
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    if (pBufferWMA == NULL &&
        voice->src.format->wFormatTag != FORGE_AUDIO_FORMAT_XMAUDIO2 &&
        pBuffer->AudioBytes % block_size != 0)
    {
        LOG_ERROR(
            voice->audio,
            "PCM source buffer AudioBytes must be a multiple of nBlockAlign: %u %% %u",
            pBuffer->AudioBytes,
            block_size
        )
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

    /* Start off with whatever they just sent us... */
    playBegin = pBuffer->PlayBegin;
    playLength = pBuffer->PlayLength;
    loopBegin = pBuffer->LoopBegin;
    loopLength = pBuffer->LoopLength;

    /* "LoopBegin/LoopLength must be zero if LoopCount is 0" */
    if (pBuffer->LoopCount == 0 && (loopBegin > 0 || loopLength > 0))
    {
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }

	if (voice->src.format->wFormatTag == FORGE_AUDIO_FORMAT_XMAUDIO2)
	{
		ForgeXMA2Format *fmtex = (ForgeXMA2Format*) voice->src.format;
		bufferLength = fmtex->dwSamplesEncoded;
	}
	else if (pBufferWMA != NULL)
	{
		bufferLength =
			pBufferWMA->pDecodedPacketCumulativeBytes[pBufferWMA->PacketCount - 1] /
			(voice->src.format->nChannels * voice->src.format->wBitsPerSample / 8);
	}
	else
	{
		bufferLength =
			pBuffer->AudioBytes /
			voice->src.format->nBlockAlign;
	}

	if (playBegin + playLength > bufferLength || playBegin + playLength < playLength)
	{
		/* Reading past the end of the buffer, or begin + length overflow uint32_t, which
		 * would also read past the end of the buffer. */
		LOG_API_EXIT(voice->audio)
		return FORGE_AUDIO_E_INVALID_CALL;
	}

    if (pBuffer->LoopCount > 0 && pBufferWMA == NULL && voice->src.format->wFormatTag != FORGE_AUDIO_FORMAT_XMAUDIO2)
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
            return FORGE_AUDIO_E_INVALID_CALL;
        }

        /* "The value of LoopBegin + LoopLength must be greater than PlayBegin
         * and less than PlayBegin + PlayLength"
         */
        if ((loopBegin + realLoopLength) <= playBegin ||
           (loopBegin + realLoopLength) > (playBegin + realPlayLength))
        {
            LOG_API_EXIT(voice->audio)
            return FORGE_AUDIO_E_INVALID_CALL;
        }
    }

    if (pBufferWMA != NULL || voice->src.format->wFormatTag == FORGE_AUDIO_FORMAT_XMAUDIO2)
    {
        /* WMA only supports looping the whole buffer */
        loopBegin = 0;
        loopLength = playBegin + playLength;
    }

    FAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    array_reserve(
        voice->audio,
        (void**) &voice->src.queued_buffers,
        &voice->src.queued_buffers_capacity,
        voice->src.queued_buffer_count + 1,
        sizeof(*voice->src.queued_buffers)
    );

    entry = &voice->src.queued_buffers[voice->src.queued_buffer_count++];
    FAudio_memset(entry, 0, sizeof(*entry));
    FAudio_memcpy(&entry->buffer, pBuffer, sizeof(ForgeBuffer));
    entry->buffer.PlayBegin = playBegin;
    entry->buffer.PlayLength = playLength;
    entry->buffer.LoopBegin = loopBegin;
    entry->buffer.LoopLength = loopLength;
    if (pBufferWMA != NULL)
    {
        FAudio_memcpy(&entry->bufferWMA, pBufferWMA, sizeof(ForgeBufferWMA));
    }
    else
    {
        if (playLength != 0)
        {
            entry->play_bytes = playLength * block_size;
        }
        else
        {
            entry->play_bytes = pBuffer->AudioBytes - (playBegin * block_size);
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
    if (pBuffer->pAudioData != NULL && entry->play_bytes > 0)
    {
        FAudio_DUMPVOICE_WriteBuffer(voice, pBuffer, pBufferWMA, entry->play_bytes);
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
    FAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_source_voice_flush_buffers(
    ForgeSourceVoice *voice
) {
    size_t offset = 0;

    LOG_API_ENTER(voice->audio)
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    FAudio_PlatformLockMutex(voice->src.bufferLock);
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
        FAudio_memcpy(
            voice->src.flush_buffers + voice->src.flush_buffer_count,
            voice->src.queued_buffers + offset,
            (voice->src.queued_buffer_count - offset) * sizeof(*voice->src.flush_buffers)
        );
    }

    voice->src.flush_buffer_count += voice->src.queued_buffer_count - offset;
    voice->src.queued_buffer_count = offset;

    FAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_source_voice_end_stream(
    ForgeSourceVoice *voice
) {
    LOG_API_ENTER(voice->audio)
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    FAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0)
    {
        voice->src.queued_buffers[voice->src.queued_buffer_count - 1].buffer.Flags |= FORGE_AUDIO_END_OF_STREAM;
    }

    FAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

uint32_t forge_source_voice_break_loop(
    ForgeSourceVoice *voice,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueExitLoop(
            voice,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    FAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (voice->src.queued_buffer_count != 0)
    {
        voice->src.queued_buffers[0].buffer.LoopCount = 0;
    }

    FAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_source_voice_get_state(
    ForgeSourceVoice *voice,
    ForgeVoiceState *pVoiceState,
    uint32_t Flags
) {
    LOG_API_ENTER(voice->audio)
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    FAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)

    if (!(Flags & FORGE_AUDIO_VOICE_NOSAMPLESPLAYED))
    {
        pVoiceState->SamplesPlayed = voice->src.totalSamples;
    }

    pVoiceState->BuffersQueued = 0;
    pVoiceState->pCurrentBufferContext = NULL;

    if (voice->src.queued_buffer_count != 0)
    {
        pVoiceState->pCurrentBufferContext = voice->src.queued_buffers[0].buffer.pContext;
    }
    pVoiceState->BuffersQueued += (uint32_t) voice->src.queued_buffer_count;

    /* Pending flushed buffers also count */
    pVoiceState->BuffersQueued += (uint32_t) voice->src.flush_buffer_count;

    LOG_INFO(
        voice->audio,
        "-> {pCurrentBufferContext: %p, BuffersQueued: %u, SamplesPlayed: %"FAudio_PRIu64"}",
        pVoiceState->pCurrentBufferContext, pVoiceState->BuffersQueued,
        pVoiceState->SamplesPlayed
    )

    FAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_source_voice_set_rate(
    ForgeSourceVoice *voice,
    float Ratio,
    uint32_t OperationSet
) {
    LOG_API_ENTER(voice->audio)

    if (OperationSet != FORGE_AUDIO_COMMIT_NOW && voice->audio->active)
    {
        FAudio_OPERATIONSET_QueueSetFrequencyRatio(
            voice,
            Ratio,
            OperationSet
        );
        LOG_API_EXIT(voice->audio)
        return 0;
    }
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    if (voice->flags & FORGE_AUDIO_VOICE_NOPITCH)
    {
        LOG_API_EXIT(voice->audio)
        return 0;
    }

    voice->src.freqRatio = FAudio_clamp(
        Ratio,
        FORGE_AUDIO_MIN_FREQ_RATIO,
        voice->src.maxFreqRatio
    );
    LOG_API_EXIT(voice->audio)
    return 0;
}

void forge_source_voice_get_rate(
    ForgeSourceVoice *voice,
    float *pRatio
) {
    LOG_API_ENTER(voice->audio)
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);

    *pRatio = voice->src.freqRatio;
    LOG_API_EXIT(voice->audio)
}

uint32_t forge_source_voice_set_sample_rate(
    ForgeSourceVoice *voice,
    uint32_t NewSourceSampleRate
) {
    uint32_t outSampleRate;
    uint32_t newDecodeSamples, newResampleSamples;

    LOG_API_ENTER(voice->audio)
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_SOURCE);
    FAudio_assert(    NewSourceSampleRate >= FORGE_AUDIO_MIN_SAMPLE_RATE &&
            NewSourceSampleRate <= FORGE_AUDIO_MAX_SAMPLE_RATE    );

    FAudio_PlatformLockMutex(voice->src.bufferLock);
    LOG_MUTEX_LOCK(voice->audio, voice->src.bufferLock)
    if (voice->src.queued_buffer_count != 0)
    {
        FAudio_PlatformUnlockMutex(voice->src.bufferLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)
        LOG_API_EXIT(voice->audio)
        return FORGE_AUDIO_E_INVALID_CALL;
    }
    FAudio_PlatformUnlockMutex(voice->src.bufferLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->src.bufferLock)

    voice->src.format->nSamplesPerSec = NewSourceSampleRate;

    /* Resize decode cache */
    newDecodeSamples = (uint32_t) FAudio_ceil(
        voice->audio->updateSize *
        (double) voice->src.maxFreqRatio *
        (double) NewSourceSampleRate /
        (double) voice->audio->master->master.inputSampleRate
    ) + EXTRA_DECODE_PADDING * voice->src.format->nChannels;
    FAudio_INTERNAL_ResizeDecodeCache(
        voice->audio,
        (newDecodeSamples + EXTRA_DECODE_PADDING) * voice->src.format->nChannels
    );
    voice->src.decodeSamples = newDecodeSamples;

    FAudio_PlatformLockMutex(voice->sendLock);
    LOG_MUTEX_LOCK(voice->audio, voice->sendLock)

    if (voice->sends.SendCount == 0)
    {
        FAudio_PlatformUnlockMutex(voice->sendLock);
        LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)
        LOG_API_EXIT(voice->audio)
        return 0;
    }
    outSampleRate = voice->sends.pSends[0].pOutputVoice->type == FORGE_AUDIO_VOICE_MASTER ?
        voice->sends.pSends[0].pOutputVoice->master.inputSampleRate :
        voice->sends.pSends[0].pOutputVoice->mix.inputSampleRate;

    newResampleSamples = (uint32_t) (FAudio_ceil(
        (double) voice->audio->updateSize *
        (double) outSampleRate /
        (double) voice->audio->master->master.inputSampleRate
    ));
    voice->src.resampleSamples = newResampleSamples;

    FAudio_PlatformUnlockMutex(voice->sendLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->sendLock)

    LOG_API_EXIT(voice->audio)
    return 0;
}

/* ForgeMasterVoice Interface */

FORGE_AUDIO_API uint32_t forge_master_voice_get_channel_mask(
    ForgeMasterVoice *voice,
    uint32_t *pChannelMask
) {
    LOG_API_ENTER(voice->audio)
    FAudio_assert(voice->type == FORGE_AUDIO_VOICE_MASTER);
    FAudio_assert(pChannelMask != NULL);

    *pChannelMask = voice->audio->mixFormat.dwChannelMask;
    LOG_API_EXIT(voice->audio)
    return 0;
}

#ifdef FORGE_AUDIO_DUMP_VOICES

static inline FAudioIOStreamOut *DumpVoices_fopen(
    const ForgeSourceVoice *voice,
    const ForgeAudioFormat *format,
    const char *mode,
    const char *ext
) {
    char loc[64];
    uint16_t format_tag = format->wFormatTag;
    uint16_t format_ex_tag = 0;
    if (format->wFormatTag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        /* get the GUID of the extended subformat */
        const ForgeAudioFormatExtensible *format_ex =
                (const ForgeAudioFormatExtensible*) format;
        format_ex_tag = (uint16_t) (format_ex->SubFormat.Data1);
    }
    FAudio_snprintf(
        loc,
        sizeof(loc),
        "FA_fmt_0x%04X_0x%04X_0x%016lX%s.wav",
        format_tag,
        format_ex_tag,
        (uint64_t) voice,
        ext
    );
    FAudioIOStreamOut *fileOut = FAudio_fopen_out(loc, mode);
    return fileOut;
}

static inline void DumpVoices_finalize_section(
    const ForgeSourceVoice *voice,
    const ForgeAudioFormat *format,
    const char *section /* one of "data" or "dpds" */
) {
    /* data file only contains the real data bytes */
    FAudioIOStreamOut *io_data = DumpVoices_fopen(voice, format, "rb", section);
    if (!io_data)
    {
        return;
    }
    FAudio_PlatformLockMutex((FAudioMutex) io_data->lock);
    size_t file_size_data = io_data->size(io_data->data);
    if (file_size_data == 0)
    {
        /* nothing to do */
        /* close data file */
        FAudio_PlatformUnlockMutex((FAudioMutex) io_data->lock);
        FAudio_close_out(io_data);
        return;
    }

    /* we got some data: append data section to main file */
    FAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "ab", "");
    if (!io)
    {
        /* close data file */
        FAudio_PlatformUnlockMutex((FAudioMutex) io_data->lock);
        FAudio_close_out(io_data);
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
    FAudio_PlatformUnlockMutex((FAudioMutex) io_data->lock);
    FAudio_close_out(io_data);
    /* close main file */
    FAudio_PlatformUnlockMutex((FAudioMutex) io->lock);
    FAudio_close_out(io);
}

static void FAudio_DUMPVOICE_Init(const ForgeSourceVoice *voice)
{
    const ForgeAudioFormat *format = voice->src.format;

    FAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "wb", "");
    if (!io)
    {
        return;
    }
    FAudio_PlatformLockMutex((FAudioMutex) io->lock);
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

     * | subchnk size|fmt  |nChan |samplerate  |byte rate   |
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

    uint16_t cbSize = format->cbSize;
    const char *formatFourcc = "WAVE";
    uint16_t wFormatTag = format->wFormatTag;
    /* special handling for WMAUDIO2 */
    if (wFormatTag == FORGE_AUDIO_FORMAT_EXTENSIBLE && cbSize >= 22)
    {
        const ForgeAudioFormatExtensible *format_ex =
                (const ForgeAudioFormatExtensible*) format;
        uint16_t format_ex_tag = (uint16_t) (format_ex->SubFormat.Data1);
        if (format_ex_tag == FORGE_AUDIO_FORMAT_WMAUDIO2)
        {
            cbSize = 0;
            formatFourcc = "XWMA";
            wFormatTag = FORGE_AUDIO_FORMAT_WMAUDIO2;
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
        /* 18 byte for WAVEFORMATEX and cbSize for WAVEFORMATEXTENDED */
        uint32_t chunk_data_size = 18 + (uint32_t) cbSize;
        io->write(io->data, &chunk_data_size, 4, 1);
        /* AudioFormat - 2 */
        io->write(io->data, &wFormatTag, 2, 1);
        /* NumChannels - 2 */
        io->write(io->data, &format->nChannels, 2, 1);
        /* SampleRate - 4 */
        io->write(io->data, &format->nSamplesPerSec, 4, 1);
        /* ByteRate - 4 */
        /* SampleRate * NumChannels * BitsPerSample/8 */
        io->write(io->data, &format->nAvgBytesPerSec, 4, 1);
        /* BlockAlign - 2 */
        /* NumChannels * BitsPerSample/8 */
        io->write(io->data, &format->nBlockAlign, 2, 1);
        /* BitsPerSample - 2 */
        io->write(io->data, &format->wBitsPerSample, 2, 1);
    }
    /* in case of extensible audio format write the additional data to the file */
    {
        /* always write the cbSize */
        io->write(io->data, &cbSize, 2, 1);

        if (cbSize >= 22)
        {
            /* we have a WAVEFORMATEXTENSIBLE struct to write */
            const ForgeAudioFormatExtensible *format_ex =
                    (const ForgeAudioFormatExtensible*) format;
            io->write(io->data, &format_ex->Samples.wValidBitsPerSample, 2, 1);
            io->write(io->data, &format_ex->dwChannelMask,   4, 1);
            /* write ForgeGuid */
            io->write(io->data, &format_ex->SubFormat.Data1, 4, 1);
            io->write(io->data, &format_ex->SubFormat.Data2, 2, 1);
            io->write(io->data, &format_ex->SubFormat.Data3, 2, 1);
            io->write(io->data, &format_ex->SubFormat.Data4, 1, 8);
        }
        if (format->cbSize > 22)
        {
            /* fill up the remaining cbSize bytes with zeros */
            uint8_t zero = 0;
            for (uint16_t i=23; i<=format->cbSize; i++)
            {
                io->write(io->data, &zero, 1, 1);
            }
        }
    }
    { /* dpds sub-chunk - optional - 8 bytes + bufferWMA uint32_t samples */
        /* create file to hold the bufferWMA samples */
        FAudioIOStreamOut *io_dpds = DumpVoices_fopen(voice, format, "wb", "dpds");
        FAudio_close_out(io_dpds);
        /* io_dpds file will be filled by SubmitBuffer */
    }
    { /* data sub-chunk - 8 bytes + data */
        /* create file to hold the data samples */
        FAudioIOStreamOut *io_data = DumpVoices_fopen(voice, format, "wb", "data");
        FAudio_close_out(io_data);
        /* io_data file will be filled by SubmitBuffer */
    }
    FAudio_PlatformUnlockMutex((FAudioMutex) io->lock);
    FAudio_close_out(io);
}

static void FAudio_DUMPVOICE_Finalize(const ForgeSourceVoice *voice)
{
    const ForgeAudioFormat *format = voice->src.format;

    /* add dpds subchunk - optional */
    DumpVoices_finalize_section(voice, format, "dpds");
    /* add data subchunk */
    DumpVoices_finalize_section(voice, format, "data");

    /* open main file to update filesize */
    FAudioIOStreamOut *io = DumpVoices_fopen(voice, format, "r+b", "");
    if (!io)
    {
        return;
    }
    FAudio_PlatformLockMutex((FAudioMutex) io->lock);
    size_t file_size = io->size(io->data);
    if (file_size >= 44)
    {
        /* update filesize */
        uint32_t chunk_size = (uint32_t)(file_size - 8);
        io->seek(io->data, 4, FORGE_AUDIO_SEEK_SET);
        io->write(io->data, &chunk_size, 4, 1);
    }
    FAudio_PlatformUnlockMutex((FAudioMutex) io->lock);
    FAudio_close_out(io);
}

static void FAudio_DUMPVOICE_WriteBuffer(
    const ForgeSourceVoice *voice,
    const ForgeBuffer *pBuffer,
    const ForgeBufferWMA *pBufferWMA,
    const uint32_t size
) {
    FAudioIOStreamOut *io_data = DumpVoices_fopen(voice, voice->src.format, "ab", "data");
    if (io_data == NULL)
    {
        return;
    }

    FAudio_PlatformLockMutex((FAudioMutex) io_data->lock);
    if (pBufferWMA != NULL)
    {
        /* dump encoded buffer contents */
        if (pBufferWMA->PacketCount > 0)
        {
            FAudioIOStreamOut *io_dpds = DumpVoices_fopen(voice, voice->src.format, "ab", "dpds");
            if (io_dpds)
            {
                FAudio_PlatformLockMutex((FAudioMutex) io_dpds->lock);
                /* write to dpds file */
                io_dpds->write(io_dpds->data, pBufferWMA->pDecodedPacketCumulativeBytes, sizeof(uint32_t), pBufferWMA->PacketCount);
                FAudio_PlatformUnlockMutex((FAudioMutex) io_dpds->lock);
                FAudio_close_out(io_dpds);
            }
            /* write buffer contents to data file */
            io_data->write(io_data->data, pBuffer->pAudioData, sizeof(uint8_t), pBuffer->AudioBytes);
        }
    }
    else
    {
        /* dump unencoded buffer contents */
        uint16_t bytesPerFrame = (voice->src.format->nChannels * voice->src.format->wBitsPerSample / 8);
        FAudio_assert(bytesPerFrame > 0);
        const void *pAudioDataBegin = pBuffer->pAudioData + pBuffer->PlayBegin * bytesPerFrame;
        io_data->write(io_data->data, pAudioDataBegin, 1, size);
    }
    FAudio_PlatformUnlockMutex((FAudioMutex) io_data->lock);
    FAudio_close_out(io_data);
}

#endif /* FORGE_AUDIO_DUMP_VOICES */
