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

#include "forge_audio_fx.h"
#include "FAudio_internal.h"

/* Volume Meter ForgeApo Implementation */

const ForgeGuid FORGE_AUDIO_FX_ID_VOLUME_METER = /* 2.7 */
{
    0xCAC1105F,
    0x619B,
    0x4D04,
    {
        0x83,
        0x1A,
        0x44,
        0xE1,
        0xCB,
        0xF1,
        0x2D,
        0x57
    }
};

static ForgeApoProperties VolumeMeterProperties =
{
    /* .clsid = */ {0},
    /* .FriendlyName = */
    {
        'V', 'o', 'l', 'u', 'm', 'e', 'M', 'e', 't', 'e', 'r', '\0'
    },
    /*.CopyrightInfo = */
    {
        'C', 'o', 'p', 'y', 'r', 'i', 'g', 'h', 't', ' ', '(', 'c', ')',
        'E', 't', 'h', 'a', 'n', ' ', 'L', 'e', 'e', '\0'
    },
    /*.MajorVersion = */ 0,
    /*.MinorVersion = */ 0,
    /*.Flags = */(
        FORGE_APO_FLAG_CHANNELS_MUST_MATCH |
        FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH |
        FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH |
        FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH |
        FORGE_APO_FLAG_IN_PLACE_SUPPORTED |
        FORGE_APO_FLAG_IN_PLACE_REQUIRED
    ),
    /*.MinInputBufferCount = */ 1,
    /*.MaxInputBufferCount = */  1,
    /*.MinOutputBufferCount = */ 1,
    /*.MaxOutputBufferCount =*/ 1
};

typedef struct ForgeAudioFxVolumeMeter
{
    ForgeApoBase base;
    uint16_t channels;
} ForgeAudioFxVolumeMeter;

uint32_t ForgeAudioFxVolumeMeter_LockForProcess(
    ForgeAudioFxVolumeMeter *fapo,
    uint32_t InputLockedParameterCount,
    const ForgeApoLockBuffer *pInputLockedParameters,
    uint32_t OutputLockedParameterCount,
    const ForgeApoLockBuffer *pOutputLockedParameters
) {
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        fapo->base.m_pParameterBlocks;

    /* Verify parameter counts... */
    if (    InputLockedParameterCount < fapo->base.m_pRegistrationProperties->MinInputBufferCount ||
        InputLockedParameterCount > fapo->base.m_pRegistrationProperties->MaxInputBufferCount ||
        OutputLockedParameterCount < fapo->base.m_pRegistrationProperties->MinOutputBufferCount ||
        OutputLockedParameterCount > fapo->base.m_pRegistrationProperties->MaxOutputBufferCount    )
    {
        return FORGE_AUDIO_E_INVALID_ARG;
    }


    /* Validate input/output formats */
    #define VERIFY_FORMAT_FLAG(flag, prop) \
        if (    (fapo->base.m_pRegistrationProperties->Flags & flag) && \
            (pInputLockedParameters->pFormat->prop != pOutputLockedParameters->pFormat->prop)    ) \
        { \
            return FORGE_AUDIO_E_INVALID_ARG; \
        }
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_CHANNELS_MUST_MATCH, nChannels)
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH, nSamplesPerSec)
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH, wBitsPerSample)
    #undef VERIFY_FORMAT_FLAG
    if (    (fapo->base.m_pRegistrationProperties->Flags & FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (InputLockedParameterCount != OutputLockedParameterCount)    )
    {
        return FORGE_AUDIO_E_INVALID_ARG;
    }

    /* Allocate volume meter arrays */
    fapo->channels = pInputLockedParameters->pFormat->nChannels;
    levels[0].pPeakLevels = (float*) fapo->base.pMalloc(
        fapo->channels * sizeof(float) * 6
    );
    FAudio_zero(levels[0].pPeakLevels, fapo->channels * sizeof(float) * 6);
    levels[0].pRMSLevels = levels[0].pPeakLevels + fapo->channels;
    levels[1].pPeakLevels = levels[0].pPeakLevels + (fapo->channels * 2);
    levels[1].pRMSLevels = levels[0].pPeakLevels + (fapo->channels * 3);
    levels[2].pPeakLevels = levels[0].pPeakLevels + (fapo->channels * 4);
    levels[2].pRMSLevels = levels[0].pPeakLevels + (fapo->channels * 5);

    fapo->base.m_fIsLocked = 1;
    return 0;
}

void ForgeAudioFxVolumeMeter_UnlockForProcess(ForgeAudioFxVolumeMeter *fapo)
{
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        fapo->base.m_pParameterBlocks;
    fapo->base.pFree(levels[0].pPeakLevels);
    fapo->base.m_fIsLocked = 0;
}

void ForgeAudioFxVolumeMeter_Process(
    ForgeAudioFxVolumeMeter *fapo,
    uint32_t InputProcessParameterCount,
    const ForgeApoProcessBuffer* pInputProcessParameters,
    uint32_t OutputProcessParameterCount,
    ForgeApoProcessBuffer* pOutputProcessParameters,
    int32_t IsEnabled
) {
    float peak;
    float total;
    float *buffer;
    uint32_t i, j;
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        forge_apo_base_begin_process(&fapo->base);

    /* TODO: This could probably be SIMD-ified... */
    for (i = 0; i < fapo->channels; i += 1)
    {
        peak = 0.0f;
        total = 0.0f;
        buffer = ((float*) pInputProcessParameters->pBuffer) + i;
        for (j = 0; j < pInputProcessParameters->ValidFrameCount; j += 1, buffer += fapo->channels)
        {
            const float sampleAbs = FAudio_fabsf(*buffer);
            if (sampleAbs > peak)
            {
                peak = sampleAbs;
            }
            total += (*buffer) * (*buffer);
        }
        levels->pPeakLevels[i] = peak;
        levels->pRMSLevels[i] = FAudio_sqrtf(
            total / pInputProcessParameters->ValidFrameCount
        );
    }

    forge_apo_base_end_process(&fapo->base);
}

void ForgeAudioFxVolumeMeter_GetParameters(
    ForgeAudioFxVolumeMeter *fapo,
    ForgeAudioFxVolumeMeterLevels *pParameters,
    uint32_t ParameterByteSize
) {
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        fapo->base.m_pCurrentParameters;
    FAudio_assert(ParameterByteSize == sizeof(ForgeAudioFxVolumeMeterLevels));
    FAudio_assert(pParameters->ChannelCount == fapo->channels);

    /* Copy what's current as of the last Process */
    if (pParameters->pPeakLevels != NULL)
    {
        FAudio_memcpy(
            pParameters->pPeakLevels,
            levels->pPeakLevels,
            fapo->channels * sizeof(float)
        );
    }
    if (pParameters->pRMSLevels != NULL)
    {
        FAudio_memcpy(
            pParameters->pRMSLevels,
            levels->pRMSLevels,
            fapo->channels * sizeof(float)
        );
    }
}

void ForgeAudioFxVolumeMeter_Free(void* fapo)
{
    ForgeAudioFxVolumeMeter *volumemeter = (ForgeAudioFxVolumeMeter*) fapo;
    volumemeter->base.pFree(volumemeter->base.m_pParameterBlocks);
    volumemeter->base.pFree(fapo);
}

/* Public API */

uint32_t forge_audio_create_volume_meter(ForgeApo** ppApo, uint32_t Flags)
{
    return forge_audio_create_volume_meter_with_allocator(
        ppApo,
        Flags,
        FAudio_malloc,
        FAudio_free,
        FAudio_realloc
    );
}

uint32_t forge_audio_create_volume_meter_with_allocator(
    ForgeApo** ppApo,
    uint32_t Flags,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    /* Allocate... */
    ForgeAudioFxVolumeMeter *result = (ForgeAudioFxVolumeMeter*) customMalloc(
        sizeof(ForgeAudioFxVolumeMeter)
    );
    uint8_t *params = (uint8_t*) customMalloc(
        sizeof(ForgeAudioFxVolumeMeterLevels) * 3
    );
    FAudio_zero(params, sizeof(ForgeAudioFxVolumeMeterLevels) * 3);

    /* Initialize... */
    FAudio_memcpy(
        &VolumeMeterProperties.clsid,
        &FORGE_AUDIO_FX_ID_VOLUME_METER,
        sizeof(ForgeGuid)
    );
    forge_apo_base_init_with_allocator(
        &result->base,
        &VolumeMeterProperties,
        params,
        sizeof(ForgeAudioFxVolumeMeterLevels),
        1,
        customMalloc,
        customFree,
        customRealloc
    );

    /* Function table... */
    result->base.base.LockForProcess = (ForgeApoLockForProcessFunc)
        ForgeAudioFxVolumeMeter_LockForProcess;
    result->base.base.UnlockForProcess = (ForgeApoUnlockForProcessFunc)
        ForgeAudioFxVolumeMeter_UnlockForProcess;
    result->base.base.Process = (ForgeApoProcessFunc)
        ForgeAudioFxVolumeMeter_Process;
    result->base.base.GetParameters = (ForgeApoGetParametersFunc)
        ForgeAudioFxVolumeMeter_GetParameters;
    result->base.Destructor = ForgeAudioFxVolumeMeter_Free;

    /* Finally. */
    *ppApo = &result->base.base;
    return 0;
}
