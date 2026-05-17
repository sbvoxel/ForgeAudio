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

#include "forge_effects.h"
#include "forge_audio_internal.h"

/* volume Meter ForgeEffect Implementation */

static const ForgeEffectInfo VolumeMeterInfo = {
    .flags = (FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH | FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH |
              FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH | FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH |
              FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED | FORGE_EFFECT_FLAG_IN_PLACE_REQUIRED),
    .min_input_buffer_count = 1,
    .max_input_buffer_count = 1,
    .min_output_buffer_count = 1,
    .max_output_buffer_count = 1};

typedef struct ForgeVolumeMeter {
    ForgeEffectBase base;
    ForgeVolumeMeterLevels levels;
    uint16_t channels;
} ForgeVolumeMeter;

static ForgeResult forge_volume_meter_lock_for_process(ForgeVolumeMeter *effect, uint32_t input_locked_parameter_count,
                                                       const ForgeEffectLockBuffer *input_locked_parameters,
                                                       uint32_t output_locked_parameter_count,
                                                       const ForgeEffectLockBuffer *output_locked_parameters) {
    /* Verify parameter counts... */
    if (input_locked_parameter_count < effect->base.effect_info->min_input_buffer_count ||
        input_locked_parameter_count > effect->base.effect_info->max_input_buffer_count ||
        output_locked_parameter_count < effect->base.effect_info->min_output_buffer_count ||
        output_locked_parameter_count > effect->base.effect_info->max_output_buffer_count) {
        return ForgeResultInvalidArgument;
    }

/* Validate input/output formats */
#define VERIFY_FORMAT_FLAG(flag, prop)                                                                                 \
    if ((effect->base.effect_info->flags & flag) &&                                                                    \
        (input_locked_parameters->format->prop != output_locked_parameters->format->prop)) {                           \
        return ForgeResultInvalidArgument;                                                                             \
    }
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH, channels)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH, sample_rate)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH, bits_per_sample)
#undef VERIFY_FORMAT_FLAG
    if ((effect->base.effect_info->flags & FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (input_locked_parameter_count != output_locked_parameter_count)) {
        return ForgeResultInvalidArgument;
    }

    /* Allocate volume meter arrays */
    effect->channels = input_locked_parameters->format->channels;
    effect->levels.peak_levels = (float *)effect->base.malloc_func(effect->channels * sizeof(float) * 2);
    forge_zero(effect->levels.peak_levels, effect->channels * sizeof(float) * 2);
    effect->levels.rms_levels = effect->levels.peak_levels + effect->channels;
    effect->levels.channel_count = effect->channels;

    effect->base.is_locked = 1;
    return 0;
}

static void forge_volume_meter_unlock_for_process(ForgeVolumeMeter *effect) {
    effect->base.free_func(effect->levels.peak_levels);
    forge_zero(&effect->levels, sizeof(effect->levels));
    effect->base.is_locked = 0;
}

static void forge_volume_meter_process(ForgeVolumeMeter *effect, uint32_t input_buffer_count,
                                       const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                                       ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    float peak;
    float total;
    float *buffer;
    uint32_t i, j;
    ForgeVolumeMeterLevels *levels = &effect->levels;

    /* Potential SIMD optimization: per-channel peak/RMS scan. */
    for (i = 0; i < effect->channels; i += 1) {
        peak = 0.0f;
        total = 0.0f;
        buffer = ((float *)input_buffers->buffer) + i;
        for (j = 0; j < input_buffers->valid_frame_count; j += 1, buffer += effect->channels) {
            const float sampleAbs = forge_fabsf(*buffer);
            if (sampleAbs > peak) {
                peak = sampleAbs;
            }
            total += (*buffer) * (*buffer);
        }
        levels->peak_levels[i] = peak;
        levels->rms_levels[i] = forge_sqrtf(total / input_buffers->valid_frame_count);
    }
}

static void forge_volume_meter_get_parameters(ForgeVolumeMeter *effect, ForgeVolumeMeterLevels *parameters,
                                              uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == sizeof(ForgeVolumeMeterLevels));
    forge_volume_meter_get_levels(&effect->base.base, parameters);
}

static void forge_volume_meter_free(void *effect) {
    ForgeVolumeMeter *volume_meter = (ForgeVolumeMeter *)effect;
    volume_meter->base.free_func(effect);
}

/* Public API */

ForgeResult forge_create_volume_meter(ForgeEffect **effect, uint32_t flags) {
    return forge_create_volume_meter_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

void forge_volume_meter_get_levels(ForgeEffect *effect, ForgeVolumeMeterLevels *levels) {
    ForgeVolumeMeter *volume_meter = (ForgeVolumeMeter *)effect;
    ForgeVolumeMeterLevels *current_levels = &volume_meter->levels;

    if (levels == NULL) {
        return;
    }

    forge_assert(levels->channel_count == volume_meter->channels);

    if (levels->peak_levels != NULL) {
        forge_memcpy(levels->peak_levels, current_levels->peak_levels, volume_meter->channels * sizeof(float));
    }
    if (levels->rms_levels != NULL) {
        forge_memcpy(levels->rms_levels, current_levels->rms_levels, volume_meter->channels * sizeof(float));
    }
}

ForgeResult forge_create_volume_meter_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                     ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                                     ForgeReallocFunc custom_realloc) {
    /* Allocate... */
    ForgeVolumeMeter *result = (ForgeVolumeMeter *)custom_malloc(sizeof(ForgeVolumeMeter));

    forge_effect_base_init_with_allocator(&result->base, &VolumeMeterInfo, NULL, 0, custom_malloc, custom_free,
                                          custom_realloc);
    forge_zero(&result->levels, sizeof(result->levels));

    /* Function table... */
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)forge_volume_meter_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)forge_volume_meter_unlock_for_process;
    result->base.base.process = (ForgeEffectProcessFunc)forge_volume_meter_process;
    result->base.base.get_parameters = (ForgeEffectGetParametersFunc)forge_volume_meter_get_parameters;
    result->base.destructor = forge_volume_meter_free;

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}
