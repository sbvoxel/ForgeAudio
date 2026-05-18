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

#ifndef FORGE_EFFECT_BASE_INTERNAL_H
#define FORGE_EFFECT_BASE_INTERNAL_H

#include "effect_internal.h"

#if defined(__GNUC__) || defined(__clang__)
#define FORGE_INTERNAL_API __attribute__((visibility("hidden")))
#else
#define FORGE_INTERNAL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Constants */

#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG FORGE_AUDIO_FORMAT_IEEE_FLOAT
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS FORGE_EFFECT_MIN_CHANNELS
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS FORGE_EFFECT_MAX_CHANNELS
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE FORGE_EFFECT_MIN_SAMPLE_RATE
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE FORGE_EFFECT_MAX_SAMPLE_RATE
#define FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE 32

#define FORGE_EFFECT_BASE_DEFAULT_FLAG                                                                                 \
    (FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH | FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH |                                \
     FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH | FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH |                        \
     FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED)

#define FORGE_EFFECT_BASE_DEFAULT_BUFFER_COUNT 1

/* ForgeEffectBase Interface */

typedef struct ForgeEffectBase ForgeEffectBase;

typedef void(FORGE_EFFECT_CALL *ForgeEffectBaseSetParametersFunc)(ForgeEffectBase *effect, const void *parameters,
                                                                  uint32_t parametersSize);

#pragma pack(push, 8)
struct ForgeEffectBase {
    /* Base Classes/Interfaces */
    ForgeEffect base;
    void(FORGE_EFFECT_CALL *destructor)(void *);

    /* Public Virtual Functions */
    ForgeEffectBaseSetParametersFunc on_set_parameters;

    /* Private Variables */
    const ForgeEffectInfo *effect_info;
    uint8_t is_locked;
    uint8_t *parameters;
    uint32_t parameter_block_byte_size;
    uint8_t parameters_changed;

    /* Allocator callbacks, NOT part of ForgeEffectBase spec! */
    ForgeMallocFunc malloc_func;
    ForgeFreeFunc free_func;
    ForgeReallocFunc realloc_func;
};
#pragma pack(pop)

FORGE_INTERNAL_API void forge_effect_base_init(ForgeEffectBase *effect, const ForgeEffectInfo *effect_info,
                                               uint8_t *parameters, uint32_t parameter_block_byte_size);

/* See "extensions/custom allocator.txt" for more information. */
FORGE_INTERNAL_API void forge_effect_base_init_with_allocator(ForgeEffectBase *effect,
                                                              const ForgeEffectInfo *effect_info, uint8_t *parameters,
                                                              uint32_t parameter_block_byte_size,
                                                              ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                                              ForgeReallocFunc custom_realloc);

FORGE_INTERNAL_API void forge_effect_base_destroy(ForgeEffectBase *effect);

FORGE_INTERNAL_API void forge_effect_base_get_info(ForgeEffectBase *effect, ForgeEffectInfo *effect_info);

FORGE_INTERNAL_API ForgeResult forge_effect_base_is_input_format_supported(
    ForgeEffectBase *effect, const ForgeAudioFormat *output_format, const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format);

FORGE_INTERNAL_API ForgeResult forge_effect_base_is_output_format_supported(
    ForgeEffectBase *effect, const ForgeAudioFormat *input_format, const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format);

FORGE_INTERNAL_API ForgeResult forge_effect_base_initialize(ForgeEffectBase *effect, const void *data,
                                                            uint32_t data_byte_size);

FORGE_INTERNAL_API void forge_effect_base_reset(ForgeEffectBase *effect);

FORGE_INTERNAL_API ForgeResult forge_effect_base_lock_for_process(
    ForgeEffectBase *effect, uint32_t input_locked_parameter_count,
    const ForgeEffectLockBuffer *input_locked_parameters, uint32_t output_locked_parameter_count,
    const ForgeEffectLockBuffer *output_locked_parameters);

FORGE_INTERNAL_API void forge_effect_base_unlock_for_process(ForgeEffectBase *effect);

FORGE_INTERNAL_API uint32_t forge_effect_base_calc_input_frames(ForgeEffectBase *effect, uint32_t output_frame_count);

FORGE_INTERNAL_API uint32_t forge_effect_base_calc_output_frames(ForgeEffectBase *effect, uint32_t input_frame_count);

FORGE_INTERNAL_API ForgeResult forge_effect_base_validate_default_format(ForgeEffectBase *effect,
                                                                         ForgeAudioFormat *format, uint8_t overwrite);

FORGE_INTERNAL_API ForgeResult forge_effect_base_validate_format_pair(ForgeEffectBase *effect,
                                                                      const ForgeAudioFormat *supported_format,
                                                                      ForgeAudioFormat *requested_format,
                                                                      uint8_t overwrite);

FORGE_INTERNAL_API void forge_effect_base_process_through(ForgeEffectBase *effect, void *input_buffer,
                                                          float *output_buffer, uint32_t frame_count,
                                                          uint16_t input_channel_count, uint16_t output_channel_count,
                                                          uint8_t mix_with_output);

FORGE_INTERNAL_API void forge_effect_base_set_parameters(ForgeEffectBase *effect, const void *parameters,
                                                         uint32_t parameter_byte_size);

FORGE_INTERNAL_API void forge_effect_base_get_parameters(ForgeEffectBase *effect, void *parameters,
                                                         uint32_t parameter_byte_size);

FORGE_INTERNAL_API void forge_effect_base_on_set_parameters(ForgeEffectBase *effect, const void *parameters,
                                                            uint32_t parametersSize);

FORGE_INTERNAL_API uint8_t forge_effect_base_parameters_changed(ForgeEffectBase *effect);

FORGE_INTERNAL_API uint8_t *forge_effect_base_begin_process(ForgeEffectBase *effect);

FORGE_INTERNAL_API void forge_effect_base_end_process(ForgeEffectBase *effect);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef FORGE_INTERNAL_API

#endif /* FORGE_EFFECT_BASE_INTERNAL_H */
