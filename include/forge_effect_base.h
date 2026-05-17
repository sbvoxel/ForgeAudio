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

#ifndef FORGE_EFFECT_BASE_H
#define FORGE_EFFECT_BASE_H

#include "forge_effect.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Constants */

#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG        FORGE_AUDIO_FORMAT_IEEE_FLOAT
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS    FORGE_EFFECT_MIN_CHANNELS
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS    FORGE_EFFECT_MAX_CHANNELS
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE    FORGE_EFFECT_MIN_SAMPLE_RATE
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE    FORGE_EFFECT_MAX_SAMPLE_RATE
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    32

#define FORGE_EFFECT_BASE_DEFAULT_FLAG ( \
    FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH | \
    FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH | \
    FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH | \
    FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH | \
    FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED \
)

#define FORGE_EFFECT_BASE_DEFAULT_BUFFER_COUNT 1

/* ForgeEffectBase Interface */

typedef struct ForgeEffectBase ForgeEffectBase;

typedef void (FORGE_EFFECT_CALL * OnSetParametersFunc)(
    ForgeEffectBase *effect,
    const void* parameters,
    uint32_t parametersSize
);

#pragma pack(push, 8)
struct ForgeEffectBase
{
    /* Base Classes/Interfaces */
    ForgeEffect base;
    void (FORGE_EFFECT_CALL *Destructor)(void*);

    /* Public Virtual Functions */
    OnSetParametersFunc OnSetParameters;

    /* Private Variables */
    const ForgeEffectProperties *registration_properties;
    void* matrix_mix_function;
    float *matrix_coefficients;
    uint32_t src_format_type;
    uint8_t is_scalar_matrix;
    uint8_t is_locked;
    uint8_t *parameter_blocks;
    uint8_t *current_parameters;
    uint8_t *current_parameters_internal;
    uint32_t current_parameters_index;
    uint32_t parameter_block_byte_size;
    uint8_t newer_results_ready;
    uint8_t producer;

    /* Allocator callbacks, NOT part of ForgeEffectBase spec! */
    ForgeMallocFunc malloc_func;
    ForgeFreeFunc free_func;
    ForgeReallocFunc realloc_func;
};
#pragma pack(pop)

FORGE_EFFECT_API void forge_effect_base_init(
    ForgeEffectBase *effect,
    const ForgeEffectProperties *registration_properties,
    uint8_t *parameter_blocks,
    uint32_t parameter_block_byte_size,
    uint8_t producer
);

/* See "extensions/custom allocator.txt" for more information. */
FORGE_EFFECT_API void forge_effect_base_init_with_allocator(
    ForgeEffectBase *effect,
    const ForgeEffectProperties *registration_properties,
    uint8_t *parameter_blocks,
    uint32_t parameter_block_byte_size,
    uint8_t producer,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);

FORGE_EFFECT_API void forge_effect_base_destroy(ForgeEffectBase *effect);

FORGE_EFFECT_API ForgeResult forge_effect_base_get_properties(
    ForgeEffectBase *effect,
    ForgeEffectProperties **registration_properties
);

FORGE_EFFECT_API ForgeResult forge_effect_base_is_input_format_supported(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *output_format,
    const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format
);

FORGE_EFFECT_API ForgeResult forge_effect_base_is_output_format_supported(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *input_format,
    const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format
);

FORGE_EFFECT_API ForgeResult forge_effect_base_initialize(
    ForgeEffectBase *effect,
    const void* data,
    uint32_t DataByteSize
);

FORGE_EFFECT_API void forge_effect_base_reset(ForgeEffectBase *effect);

FORGE_EFFECT_API ForgeResult forge_effect_base_lock_for_process(
    ForgeEffectBase *effect,
    uint32_t InputLockedParameterCount,
    const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t OutputLockedParameterCount,
    const ForgeEffectLockBuffer *output_locked_parameters
);

FORGE_EFFECT_API void forge_effect_base_unlock_for_process(ForgeEffectBase *effect);

FORGE_EFFECT_API uint32_t forge_effect_base_calc_input_frames(
    ForgeEffectBase *effect,
    uint32_t OutputFrameCount
);

FORGE_EFFECT_API uint32_t forge_effect_base_calc_output_frames(
    ForgeEffectBase *effect,
    uint32_t InputFrameCount
);

FORGE_EFFECT_API ForgeResult forge_effect_base_validate_default_format(
    ForgeEffectBase *effect,
    ForgeAudioFormat *format,
    uint8_t overwrite
);

FORGE_EFFECT_API ForgeResult forge_effect_base_validate_format_pair(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *supported_format,
    ForgeAudioFormat *requested_format,
    uint8_t overwrite
);

FORGE_EFFECT_API void forge_effect_base_process_through(
    ForgeEffectBase *effect,
    void* input_buffer,
    float *output_buffer,
    uint32_t FrameCount,
    uint16_t InputChannelCount,
    uint16_t OutputChannelCount,
    uint8_t MixWithOutput
);

FORGE_EFFECT_API void forge_effect_base_set_parameters(
    ForgeEffectBase *effect,
    const void* parameters,
    uint32_t ParameterByteSize
);

FORGE_EFFECT_API void forge_effect_base_get_parameters(
    ForgeEffectBase *effect,
    void* parameters,
    uint32_t ParameterByteSize
);

FORGE_EFFECT_API void forge_effect_base_on_set_parameters(
    ForgeEffectBase *effect,
    const void* parameters,
    uint32_t parametersSize
);

FORGE_EFFECT_API uint8_t forge_effect_base_parameters_changed(ForgeEffectBase *effect);

FORGE_EFFECT_API uint8_t* forge_effect_base_begin_process(ForgeEffectBase *effect);

FORGE_EFFECT_API void forge_effect_base_end_process(ForgeEffectBase *effect);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_EFFECT_BASE_H */
