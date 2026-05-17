/* ForgeAudio
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

#ifndef FORGE_APO_BASE_H
#define FORGE_APO_BASE_H

#include "forge_apo.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Constants */

#define FORGE_APO_BASE_DEFAULT_FORMAT_TAG        FORGE_AUDIO_FORMAT_IEEE_FLOAT
#define FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS    FORGE_APO_MIN_CHANNELS
#define FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS    FORGE_APO_MAX_CHANNELS
#define FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE    FORGE_APO_MIN_SAMPLE_RATE
#define FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE    FORGE_APO_MAX_SAMPLE_RATE
#define FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    32

#define FORGE_APO_BASE_DEFAULT_FLAG ( \
    FORGE_APO_FLAG_CHANNELS_MUST_MATCH | \
    FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH | \
    FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH | \
    FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH | \
    FORGE_APO_FLAG_IN_PLACE_SUPPORTED \
)

#define FORGE_APO_BASE_DEFAULT_BUFFER_COUNT 1

/* ForgeApoBase Interface */

typedef struct ForgeApoBase ForgeApoBase;

typedef void (FORGE_APO_CALL * OnSetParametersFunc)(
    ForgeApoBase *fapo,
    const void* parameters,
    uint32_t parametersSize
);

#pragma pack(push, 8)
struct ForgeApoBase
{
    /* Base Classes/Interfaces */
    ForgeApo base;
    void (FORGE_APO_CALL *Destructor)(void*);

    /* Public Virtual Functions */
    OnSetParametersFunc OnSetParameters;

    /* Private Variables */
    const ForgeApoProperties *m_pRegistrationProperties;
    void* m_pfnMatrixMixFunction;
    float *m_pfl32MatrixCoefficients;
    uint32_t m_nSrcFormatType;
    uint8_t m_fIsScalarMatrix;
    uint8_t m_fIsLocked;
    uint8_t *m_pParameterBlocks;
    uint8_t *m_pCurrentParameters;
    uint8_t *m_pCurrentParametersInternal;
    uint32_t m_uCurrentParametersIndex;
    uint32_t m_uParameterBlockByteSize;
    uint8_t m_fNewerResultsReady;
    uint8_t m_fProducer;

    /* Protected Variables */
    int32_t m_lReferenceCount; /* LONG */

    /* Allocator callbacks, NOT part of ForgeApoBase spec! */
    ForgeMallocFunc pMalloc;
    ForgeFreeFunc pFree;
    ForgeReallocFunc pRealloc;
};
#pragma pack(pop)

FORGE_APO_API void forge_apo_base_init(
    ForgeApoBase *fapo,
    const ForgeApoProperties *pRegistrationProperties,
    uint8_t *pParameterBlocks,
    uint32_t uParameterBlockByteSize,
    uint8_t fProducer
);

/* See "extensions/custom allocator.txt" for more information. */
FORGE_APO_API void forge_apo_base_init_with_allocator(
    ForgeApoBase *fapo,
    const ForgeApoProperties *pRegistrationProperties,
    uint8_t *pParameterBlocks,
    uint32_t uParameterBlockByteSize,
    uint8_t fProducer,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);

FORGE_APO_API int32_t forge_apo_base_retain(ForgeApoBase *fapo);

FORGE_APO_API int32_t forge_apo_base_release(ForgeApoBase *fapo);

FORGE_APO_API ForgeResult forge_apo_base_get_properties(
    ForgeApoBase *fapo,
    ForgeApoProperties **ppRegistrationProperties
);

FORGE_APO_API ForgeResult forge_apo_base_is_input_format_supported(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *pOutputFormat,
    const ForgeAudioFormat *pRequestedInputFormat,
    ForgeAudioFormat **ppSupportedInputFormat
);

FORGE_APO_API ForgeResult forge_apo_base_is_output_format_supported(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *pInputFormat,
    const ForgeAudioFormat *pRequestedOutputFormat,
    ForgeAudioFormat **ppSupportedOutputFormat
);

FORGE_APO_API ForgeResult forge_apo_base_initialize(
    ForgeApoBase *fapo,
    const void* pData,
    uint32_t DataByteSize
);

FORGE_APO_API void forge_apo_base_reset(ForgeApoBase *fapo);

FORGE_APO_API ForgeResult forge_apo_base_lock_for_process(
    ForgeApoBase *fapo,
    uint32_t InputLockedParameterCount,
    const ForgeApoLockBuffer *pInputLockedParameters,
    uint32_t OutputLockedParameterCount,
    const ForgeApoLockBuffer *pOutputLockedParameters
);

FORGE_APO_API void forge_apo_base_unlock_for_process(ForgeApoBase *fapo);

FORGE_APO_API uint32_t forge_apo_base_calc_input_frames(
    ForgeApoBase *fapo,
    uint32_t OutputFrameCount
);

FORGE_APO_API uint32_t forge_apo_base_calc_output_frames(
    ForgeApoBase *fapo,
    uint32_t InputFrameCount
);

FORGE_APO_API ForgeResult forge_apo_base_validate_default_format(
    ForgeApoBase *fapo,
    ForgeAudioFormat *pFormat,
    uint8_t fOverwrite
);

FORGE_APO_API ForgeResult forge_apo_base_validate_format_pair(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *pSupportedFormat,
    ForgeAudioFormat *pRequestedFormat,
    uint8_t fOverwrite
);

FORGE_APO_API void forge_apo_base_process_through(
    ForgeApoBase *fapo,
    void* pInputBuffer,
    float *pOutputBuffer,
    uint32_t FrameCount,
    uint16_t InputChannelCount,
    uint16_t OutputChannelCount,
    uint8_t MixWithOutput
);

FORGE_APO_API void forge_apo_base_set_parameters(
    ForgeApoBase *fapo,
    const void* pParameters,
    uint32_t ParameterByteSize
);

FORGE_APO_API void forge_apo_base_get_parameters(
    ForgeApoBase *fapo,
    void* pParameters,
    uint32_t ParameterByteSize
);

FORGE_APO_API void forge_apo_base_on_set_parameters(
    ForgeApoBase *fapo,
    const void* parameters,
    uint32_t parametersSize
);

FORGE_APO_API uint8_t forge_apo_base_parameters_changed(ForgeApoBase *fapo);

FORGE_APO_API uint8_t* forge_apo_base_begin_process(ForgeApoBase *fapo);

FORGE_APO_API void forge_apo_base_end_process(ForgeApoBase *fapo);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_APO_BASE_H */
