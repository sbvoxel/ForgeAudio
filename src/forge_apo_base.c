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

#include "forge_apo_base.h"
#include "forge_audio_internal.h"

/* ForgeApoBase Interface */

void forge_apo_base_init(
    ForgeApoBase *fapo,
    const ForgeApoProperties *pRegistrationProperties,
    uint8_t *pParameterBlocks,
    uint32_t uParameterBlockByteSize,
    uint8_t fProducer
) {
    forge_apo_base_init_with_allocator(
        fapo,
        pRegistrationProperties,
        pParameterBlocks,
        uParameterBlockByteSize,
        fProducer,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

void forge_apo_base_init_with_allocator(
    ForgeApoBase *fapo,
    const ForgeApoProperties *pRegistrationProperties,
    uint8_t *pParameterBlocks,
    uint32_t uParameterBlockByteSize,
    uint8_t fProducer,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    /* Base Classes/Interfaces */
    fapo->base.AddRef = (ForgeApoAddRefFunc) forge_apo_base_retain;
    fapo->base.Release = (ForgeApoReleaseFunc) forge_apo_base_release;
    fapo->base.GetRegistrationProperties = (ForgeApoGetPropertiesFunc)
        forge_apo_base_get_properties;
    fapo->base.IsInputFormatSupported = (ForgeApoIsInputFormatSupportedFunc)
        forge_apo_base_is_input_format_supported;
    fapo->base.IsOutputFormatSupported = (ForgeApoIsOutputFormatSupportedFunc)
        forge_apo_base_is_output_format_supported;
    fapo->base.Initialize = (ForgeApoInitializeFunc) forge_apo_base_initialize;
    fapo->base.Reset = (ForgeApoResetFunc) forge_apo_base_reset;
    fapo->base.LockForProcess = (ForgeApoLockForProcessFunc)
        forge_apo_base_lock_for_process;
    fapo->base.UnlockForProcess = (ForgeApoUnlockForProcessFunc)
        forge_apo_base_unlock_for_process;
    fapo->base.CalcInputFrames = (ForgeApoCalcInputFramesFunc)
        forge_apo_base_calc_input_frames;
    fapo->base.CalcOutputFrames = (ForgeApoCalcOutputFramesFunc)
        forge_apo_base_calc_output_frames;
    fapo->base.SetParameters = (ForgeApoSetParametersFunc)
        forge_apo_base_set_parameters;
    fapo->base.GetParameters = (ForgeApoGetParametersFunc)
        forge_apo_base_get_parameters;

    /* Public Virtual Functions */
    fapo->OnSetParameters = (OnSetParametersFunc)
        forge_apo_base_on_set_parameters;

    /* Private Variables */
    fapo->m_pRegistrationProperties = pRegistrationProperties; /* FIXME */
    fapo->m_pfnMatrixMixFunction = NULL; /* FIXME */
    fapo->m_pfl32MatrixCoefficients = NULL; /* FIXME */
    fapo->m_nSrcFormatType = 0; /* FIXME */
    fapo->m_fIsScalarMatrix = 0; /* FIXME: */
    fapo->m_fIsLocked = 0;
    fapo->m_pParameterBlocks = pParameterBlocks;
    fapo->m_pCurrentParameters = pParameterBlocks;
    fapo->m_pCurrentParametersInternal = pParameterBlocks;
    fapo->m_uCurrentParametersIndex = 0;
    fapo->m_uParameterBlockByteSize = uParameterBlockByteSize;
    fapo->m_fNewerResultsReady = 0;
    fapo->m_fProducer = fProducer;

    /* Allocator Callbacks */
    fapo->pMalloc = customMalloc;
    fapo->pFree = customFree;
    fapo->pRealloc = customRealloc;

    /* Protected Variables */
    fapo->m_lReferenceCount = 1;
}

int32_t forge_apo_base_retain(ForgeApoBase *fapo)
{
    fapo->m_lReferenceCount += 1;
    return fapo->m_lReferenceCount;
}

int32_t forge_apo_base_release(ForgeApoBase *fapo)
{
    fapo->m_lReferenceCount -= 1;
    if (fapo->m_lReferenceCount == 0)
    {
        fapo->Destructor(fapo);
        return 0;
    }
    return fapo->m_lReferenceCount;
}

ForgeResult forge_apo_base_get_properties(
    ForgeApoBase *fapo,
    ForgeApoProperties **ppRegistrationProperties
) {
    *ppRegistrationProperties = (ForgeApoProperties*) fapo->pMalloc(
        sizeof(ForgeApoProperties)
    );
    ForgeAudio_memcpy(
        *ppRegistrationProperties,
        fapo->m_pRegistrationProperties,
        sizeof(ForgeApoProperties)
    );
    return 0;
}

ForgeResult forge_apo_base_is_input_format_supported(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *pOutputFormat,
    const ForgeAudioFormat *pRequestedInputFormat,
    ForgeAudioFormat **ppSupportedInputFormat
) {
    if (    pRequestedInputFormat->wFormatTag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        pRequestedInputFormat->nChannels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        pRequestedInputFormat->nChannels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        pRequestedInputFormat->nSamplesPerSec < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        pRequestedInputFormat->nSamplesPerSec > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        pRequestedInputFormat->wBitsPerSample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (ppSupportedInputFormat != NULL)
        {
            (*ppSupportedInputFormat)->wFormatTag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            (*ppSupportedInputFormat)->nChannels = ForgeAudio_clamp(
                pRequestedInputFormat->nChannels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            (*ppSupportedInputFormat)->nSamplesPerSec = ForgeAudio_clamp(
                pRequestedInputFormat->nSamplesPerSec,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            (*ppSupportedInputFormat)->wBitsPerSample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_apo_base_is_output_format_supported(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *pInputFormat,
    const ForgeAudioFormat *pRequestedOutputFormat,
    ForgeAudioFormat **ppSupportedOutputFormat
) {
    if (    pRequestedOutputFormat->wFormatTag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        pRequestedOutputFormat->nChannels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        pRequestedOutputFormat->nChannels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        pRequestedOutputFormat->nSamplesPerSec < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        pRequestedOutputFormat->nSamplesPerSec > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        pRequestedOutputFormat->wBitsPerSample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (ppSupportedOutputFormat != NULL)
        {
            (*ppSupportedOutputFormat)->wFormatTag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            (*ppSupportedOutputFormat)->nChannels = ForgeAudio_clamp(
                pRequestedOutputFormat->nChannels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            (*ppSupportedOutputFormat)->nSamplesPerSec = ForgeAudio_clamp(
                pRequestedOutputFormat->nSamplesPerSec,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            (*ppSupportedOutputFormat)->wBitsPerSample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_apo_base_initialize(
    ForgeApoBase *fapo,
    const void* pData,
    uint32_t DataByteSize
) {
    return 0;
}

void forge_apo_base_reset(ForgeApoBase *fapo)
{
}

ForgeResult forge_apo_base_lock_for_process(
    ForgeApoBase *fapo,
    uint32_t InputLockedParameterCount,
    const ForgeApoLockBuffer *pInputLockedParameters,
    uint32_t OutputLockedParameterCount,
    const ForgeApoLockBuffer *pOutputLockedParameters
) {
    /* Verify parameter counts... */
    if (    InputLockedParameterCount < fapo->m_pRegistrationProperties->MinInputBufferCount ||
        InputLockedParameterCount > fapo->m_pRegistrationProperties->MaxInputBufferCount ||
        OutputLockedParameterCount < fapo->m_pRegistrationProperties->MinOutputBufferCount ||
        OutputLockedParameterCount > fapo->m_pRegistrationProperties->MaxOutputBufferCount    )
    {
        return ForgeResultInvalidArgument;
    }


    /* Validate input/output formats */
    #define VERIFY_FORMAT_FLAG(flag, prop) \
        if (    (fapo->m_pRegistrationProperties->Flags & flag) && \
            (pInputLockedParameters->pFormat->prop != pOutputLockedParameters->pFormat->prop)    ) \
        { \
            return ForgeResultInvalidArgument; \
        }
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_CHANNELS_MUST_MATCH, nChannels)
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH, nSamplesPerSec)
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH, wBitsPerSample)
    #undef VERIFY_FORMAT_FLAG
    if (    (fapo->m_pRegistrationProperties->Flags & FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (InputLockedParameterCount != OutputLockedParameterCount)    )
    {
        return ForgeResultInvalidArgument;
    }
    fapo->m_fIsLocked = 1;
    return 0;
}

void forge_apo_base_unlock_for_process(ForgeApoBase *fapo)
{
    fapo->m_fIsLocked = 0;
}

uint32_t forge_apo_base_calc_input_frames(ForgeApoBase *fapo, uint32_t OutputFrameCount)
{
    return OutputFrameCount;
}

uint32_t forge_apo_base_calc_output_frames(ForgeApoBase *fapo, uint32_t InputFrameCount)
{
    return InputFrameCount;
}

ForgeResult forge_apo_base_validate_default_format(
    ForgeApoBase *fapo,
    ForgeAudioFormat *pFormat,
    uint8_t fOverwrite
) {
    if (    pFormat->wFormatTag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        pFormat->nChannels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        pFormat->nChannels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        pFormat->nSamplesPerSec < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        pFormat->nSamplesPerSec > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        pFormat->wBitsPerSample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (fOverwrite)
        {
            pFormat->wFormatTag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            pFormat->nChannels = ForgeAudio_clamp(
                pFormat->nChannels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            pFormat->nSamplesPerSec = ForgeAudio_clamp(
                pFormat->nSamplesPerSec,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            pFormat->wBitsPerSample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_apo_base_validate_format_pair(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *pSupportedFormat,
    ForgeAudioFormat *pRequestedFormat,
    uint8_t fOverwrite
) {
    if (    pRequestedFormat->wFormatTag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        pRequestedFormat->nChannels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        pRequestedFormat->nChannels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        pRequestedFormat->nSamplesPerSec < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        pRequestedFormat->nSamplesPerSec > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        pRequestedFormat->wBitsPerSample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (fOverwrite)
        {
            pRequestedFormat->wFormatTag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            pRequestedFormat->nChannels = ForgeAudio_clamp(
                pRequestedFormat->nChannels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            pRequestedFormat->nSamplesPerSec = ForgeAudio_clamp(
                pRequestedFormat->nSamplesPerSec,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            pRequestedFormat->wBitsPerSample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

void forge_apo_base_process_through(
    ForgeApoBase *fapo,
    void* pInputBuffer,
    float *pOutputBuffer,
    uint32_t FrameCount,
    uint16_t InputChannelCount,
    uint16_t OutputChannelCount,
    uint8_t MixWithOutput
) {
    uint32_t i, co, ci;
    float *input = (float*) pInputBuffer;

    if (MixWithOutput)
    {
        /* TODO: SSE */
        for (i = 0; i < FrameCount; i += 1)
        for (co = 0; co < OutputChannelCount; co += 1)
        for (ci = 0; ci < InputChannelCount; ci += 1)
        {
            /* Add, don't overwrite! */
            pOutputBuffer[i * OutputChannelCount + co] +=
                input[i * InputChannelCount + ci];
        }
    }
    else
    {
        /* TODO: SSE */
        for (i = 0; i < FrameCount; i += 1)
        for (co = 0; co < OutputChannelCount; co += 1)
        for (ci = 0; ci < InputChannelCount; ci += 1)
        {
            /* Overwrite, don't add! */
            pOutputBuffer[i * OutputChannelCount + co] =
                input[i * InputChannelCount + ci];
        }
    }
}

void forge_apo_base_set_parameters(
    ForgeApoBase *fapo,
    const void* pParameters,
    uint32_t ParameterByteSize
) {
    ForgeAudio_assert(!fapo->m_fProducer);

    /* User callback for validation */
    fapo->OnSetParameters(
        fapo,
        pParameters,
        ParameterByteSize
    );

    /* Increment parameter block index... */
    fapo->m_uCurrentParametersIndex += 1;
    if (fapo->m_uCurrentParametersIndex == 3)
    {
        fapo->m_uCurrentParametersIndex = 0;
    }
    fapo->m_pCurrentParametersInternal = fapo->m_pParameterBlocks + (
        fapo->m_uParameterBlockByteSize *
        fapo->m_uCurrentParametersIndex
    );

    /* Copy to what will eventually be the next parameter update */
    ForgeAudio_memcpy(
        fapo->m_pCurrentParametersInternal,
        pParameters,
        ParameterByteSize
    );
}

void forge_apo_base_get_parameters(
    ForgeApoBase *fapo,
    void* pParameters,
    uint32_t ParameterByteSize
) {
    /* Copy what's current as of the last Process */
    ForgeAudio_memcpy(
        pParameters,
        fapo->m_pCurrentParameters,
        ParameterByteSize
    );
}

void forge_apo_base_on_set_parameters(
    ForgeApoBase *fapo,
    const void* parameters,
    uint32_t parametersSize
) {
}

uint8_t forge_apo_base_parameters_changed(ForgeApoBase *fapo)
{
    /* Internal will get updated when SetParameters is called */
    return fapo->m_pCurrentParametersInternal != fapo->m_pCurrentParameters;
}

uint8_t* forge_apo_base_begin_process(ForgeApoBase *fapo)
{
    /* Set the latest block as "current", this is what Process will use now */
    fapo->m_pCurrentParameters = fapo->m_pCurrentParametersInternal;
    return fapo->m_pCurrentParameters;
}

void forge_apo_base_end_process(ForgeApoBase *fapo)
{
    /* I'm 100% sure my parameter block increment is wrong... */
}
