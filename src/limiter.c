/*
 * ForgeAudio
 * Forked from FAudio.
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include <forge/effects.h>
#include "effect_base_internal.h"
#include "format_internal.h"

static const ForgeEffectInfo limiter_info = {
    .flags = FORGE_EFFECT_BASE_DEFAULT_FLAG,
    .min_input_buffer_count = 1,
    .max_input_buffer_count = 1,
    .min_output_buffer_count = 1,
    .max_output_buffer_count = 1};

typedef struct ForgeLimiter {
    ForgeEffectBase base;
    ForgeLimiterParameters applied_parameters;
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t max_lookahead_frames;
    uint32_t lookahead_frames;
    uint32_t ring_capacity_frames;
    uint32_t read_frame;
    uint32_t write_frame;
    float *delay_buffer;
    float input_gain_linear;
    float ceiling_linear;
    float release_coeff;
    float current_gain;
} ForgeLimiter;

static int8_t limiter_is_float_format(const ForgeAudioFormat *format) {
    if (format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
        return 1;
    }

    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        if (fa_format_id_equals(((ForgeAudioFormatExtensible *)format)->format_id, fa_format_id_ieee_float)) {
            return 1;
        }
    }

    return 0;
}

static float limiter_db_to_linear(float db) {
    return forge_powf(10.0f, db / 20.0f);
}

static uint32_t limiter_ms_to_frames(uint32_t sample_rate, float ms) {
    float frames;

    if (ms <= 0.0f) {
        return 0;
    }

    frames = (ms * (float)sample_rate) / 1000.0f;
    frames = (float)forge_floor(frames + 0.5f);
    if (frames < 1.0f) {
        frames = 1.0f;
    }
    return (uint32_t)frames;
}

static ForgeLimiterParameters limiter_clamp_parameters(const ForgeLimiterParameters *parameters) {
    ForgeLimiterParameters result = *parameters;

    result.input_gain_db =
        forge_clamp(result.input_gain_db, FORGE_LIMITER_MIN_INPUT_GAIN_DB, FORGE_LIMITER_MAX_INPUT_GAIN_DB);
    result.ceiling_db = forge_clamp(result.ceiling_db, FORGE_LIMITER_MIN_CEILING_DB, FORGE_LIMITER_MAX_CEILING_DB);
    result.lookahead_ms =
        forge_clamp(result.lookahead_ms, FORGE_LIMITER_MIN_LOOKAHEAD_MS, FORGE_LIMITER_MAX_LOOKAHEAD_MS);
    result.release_ms = forge_clamp(result.release_ms, FORGE_LIMITER_MIN_RELEASE_MS, FORGE_LIMITER_MAX_RELEASE_MS);

    return result;
}

static void limiter_apply_parameters(ForgeLimiter *effect, const ForgeLimiterParameters *parameters) {
    uint32_t old_lookahead_frames = effect->lookahead_frames;

    effect->applied_parameters = limiter_clamp_parameters(parameters);
    effect->input_gain_linear = limiter_db_to_linear(effect->applied_parameters.input_gain_db);
    effect->ceiling_linear = limiter_db_to_linear(effect->applied_parameters.ceiling_db);
    if (effect->sample_rate == 0) {
        effect->lookahead_frames = 0;
        effect->release_coeff = 0.0f;
        return;
    }
    effect->lookahead_frames = limiter_ms_to_frames(effect->sample_rate, effect->applied_parameters.lookahead_ms);
    if (effect->lookahead_frames > effect->max_lookahead_frames) {
        effect->lookahead_frames = effect->max_lookahead_frames;
    }
    effect->release_coeff = (float)forge_exp(-1.0 / ((double)effect->sample_rate *
                                                     ((double)effect->applied_parameters.release_ms / 1000.0)));

    if (effect->delay_buffer != NULL && old_lookahead_frames != effect->lookahead_frames) {
        forge_zero(effect->delay_buffer, effect->ring_capacity_frames * effect->channels * sizeof(float));
        effect->read_frame = 0;
        effect->write_frame = effect->lookahead_frames;
        effect->current_gain = 1.0f;
    }
}

static void limiter_reset_ring(ForgeLimiter *effect) {
    if (effect->delay_buffer != NULL) {
        forge_zero(effect->delay_buffer, effect->ring_capacity_frames * effect->channels * sizeof(float));
    }
    effect->read_frame = 0;
    effect->write_frame = effect->lookahead_frames;
    effect->current_gain = 1.0f;
}

static float limiter_window_peak(const ForgeLimiter *effect) {
    float peak = 0.0f;
    uint32_t frame = effect->read_frame;

    for (uint32_t i = 0; i <= effect->lookahead_frames; i += 1) {
        const float *samples = effect->delay_buffer + (frame * effect->channels);
        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            float sample_abs = forge_fabsf(samples[channel]);
            if (sample_abs > peak) {
                peak = sample_abs;
            }
        }
        frame = (frame + 1) % effect->ring_capacity_frames;
    }

    return peak;
}

static uint8_t limiter_has_pending_signal(const ForgeLimiter *effect) {
    uint32_t frame = effect->read_frame;

    for (uint32_t i = 0; i < effect->lookahead_frames; i += 1) {
        const float *samples = effect->delay_buffer + (frame * effect->channels);
        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            if (forge_fabsf(samples[channel]) > 0.0000001f) {
                return 1;
            }
        }
        frame = (frame + 1) % effect->ring_capacity_frames;
    }

    return 0;
}

static ForgeResult fa_limiter_initialize(ForgeLimiter *effect, const void *data, uint32_t data_byte_size) {
    ForgeLimiterParameters parameters;

    forge_assert(data_byte_size == sizeof(ForgeLimiterParameters));
    if (data == NULL || data_byte_size != sizeof(ForgeLimiterParameters)) {
        return ForgeResultInvalidArgument;
    }

    parameters = limiter_clamp_parameters((const ForgeLimiterParameters *)data);
    forge_memcpy(effect->base.parameters, &parameters, sizeof(parameters));
    effect->base.parameters_changed = 1;
    limiter_apply_parameters(effect, &parameters);
    return ForgeResultSuccess;
}

static ForgeResult fa_limiter_lock_for_process(ForgeLimiter *effect, uint32_t input_locked_parameter_count,
                                               const ForgeEffectLockBuffer *input_locked_parameters,
                                               uint32_t output_locked_parameter_count,
                                               const ForgeEffectLockBuffer *output_locked_parameters) {
    ForgeResult result;

    if (!limiter_is_float_format(input_locked_parameters->format) ||
        !limiter_is_float_format(output_locked_parameters->format)) {
        return ForgeResultEffectFormatUnsupported;
    }

    if (input_locked_parameters->format->channels != output_locked_parameters->format->channels) {
        return ForgeResultEffectFormatUnsupported;
    }

    result = fa_effect_base_lock_for_process(&effect->base, input_locked_parameter_count, input_locked_parameters,
                                             output_locked_parameter_count, output_locked_parameters);
    if (result != ForgeResultSuccess) {
        return result;
    }

    effect->channels = input_locked_parameters->format->channels;
    effect->sample_rate = output_locked_parameters->format->sample_rate;
    effect->max_lookahead_frames =
        limiter_ms_to_frames(effect->sample_rate, FORGE_LIMITER_MAX_LOOKAHEAD_MS);
    effect->ring_capacity_frames = effect->max_lookahead_frames + 1;
    effect->delay_buffer =
        (float *)effect->base.malloc_func(effect->ring_capacity_frames * effect->channels * sizeof(float));
    if (effect->delay_buffer == NULL) {
        fa_effect_base_unlock_for_process(&effect->base);
        return ForgeResultOutOfMemory;
    }

    limiter_apply_parameters(effect, (const ForgeLimiterParameters *)effect->base.parameters);
    limiter_reset_ring(effect);
    return ForgeResultSuccess;
}

static void fa_limiter_unlock_for_process(ForgeLimiter *effect) {
    effect->base.free_func(effect->delay_buffer);
    effect->delay_buffer = NULL;
    effect->channels = 0;
    effect->sample_rate = 0;
    effect->max_lookahead_frames = 0;
    effect->lookahead_frames = 0;
    effect->ring_capacity_frames = 0;
    fa_effect_base_unlock_for_process(&effect->base);
}

static void fa_limiter_process(ForgeLimiter *effect, uint32_t input_buffer_count,
                               const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                               ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    const float *input = (const float *)input_buffers->buffer;
    float *output = (float *)output_buffers->buffer;
    uint32_t frame_count = input_buffers->valid_frame_count;
    float output_peak = 0.0f;

    (void)input_buffer_count;
    (void)output_buffer_count;

    if (fa_effect_base_parameters_changed(&effect->base)) {
        limiter_apply_parameters(effect, (const ForgeLimiterParameters *)effect->base.parameters);
    }

    output_buffers->valid_frame_count = frame_count;

    if (is_enabled == 0) {
        output_buffers->buffer_flags = input_buffers->buffer_flags;
        if (input != output && input_buffers->buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            forge_memcpy(output, input, frame_count * effect->channels * sizeof(float));
        }
        fa_effect_base_end_process(&effect->base);
        return;
    }

    for (uint32_t frame = 0; frame < frame_count; frame += 1) {
        float *write = effect->delay_buffer + (effect->write_frame * effect->channels);
        const float *read;
        float peak;
        float required_gain;

        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            float sample = (input_buffers->buffer_flags == FORGE_EFFECT_BUFFER_SILENT)
                               ? 0.0f
                               : input[frame * effect->channels + channel];
            write[channel] = sample * effect->input_gain_linear;
        }

        peak = limiter_window_peak(effect);
        required_gain = (peak > effect->ceiling_linear) ? (effect->ceiling_linear / peak) : 1.0f;
        if (required_gain < effect->current_gain) {
            effect->current_gain = required_gain;
        } else if (required_gain > effect->current_gain) {
            effect->current_gain = required_gain + ((effect->current_gain - required_gain) * effect->release_coeff);
            if (effect->current_gain > required_gain) {
                effect->current_gain = required_gain;
            }
        }

        read = effect->delay_buffer + (effect->read_frame * effect->channels);
        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            float sample = read[channel] * effect->current_gain;
            output[frame * effect->channels + channel] = sample;
            if (forge_fabsf(sample) > output_peak) {
                output_peak = forge_fabsf(sample);
            }
        }

        effect->read_frame = (effect->read_frame + 1) % effect->ring_capacity_frames;
        effect->write_frame = (effect->write_frame + 1) % effect->ring_capacity_frames;
    }

    output_buffers->buffer_flags = (output_peak > 0.0000001f || limiter_has_pending_signal(effect))
                                       ? FORGE_EFFECT_BUFFER_VALID
                                       : FORGE_EFFECT_BUFFER_SILENT;

    fa_effect_base_end_process(&effect->base);
}

static void fa_limiter_reset(ForgeLimiter *effect) {
    fa_effect_base_reset(&effect->base);
    limiter_reset_ring(effect);
}

static void fa_limiter_set_parameters(ForgeLimiter *effect, const ForgeLimiterParameters *parameters,
                                      uint32_t parameter_byte_size) {
    ForgeLimiterParameters clamped_parameters;

    forge_assert(parameter_byte_size == sizeof(ForgeLimiterParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeLimiterParameters)) {
        return;
    }

    clamped_parameters = limiter_clamp_parameters(parameters);
    forge_memcpy(effect->base.parameters, &clamped_parameters, sizeof(clamped_parameters));
    effect->base.parameters_changed = 1;
}

static void fa_limiter_get_parameters(ForgeLimiter *effect, ForgeLimiterParameters *parameters,
                                      uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == sizeof(ForgeLimiterParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeLimiterParameters)) {
        return;
    }

    forge_memcpy(parameters, effect->base.parameters, sizeof(ForgeLimiterParameters));
}

static void fa_limiter_free(void *effect) {
    ForgeLimiter *limiter = (ForgeLimiter *)effect;

    limiter->base.free_func(limiter->delay_buffer);
    limiter->base.free_func(limiter->base.parameters);
    limiter->base.free_func(effect);
}

ForgeResult forge_create_limiter(ForgeEffect **effect, uint32_t flags) {
    return forge_create_limiter_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

ForgeResult forge_create_limiter_with_allocator(ForgeEffect **effect, uint32_t flags, ForgeMallocFunc custom_malloc,
                                                ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    const ForgeLimiterParameters default_parameters = {
        FORGE_LIMITER_DEFAULT_INPUT_GAIN_DB,
        FORGE_LIMITER_DEFAULT_CEILING_DB,
        FORGE_LIMITER_DEFAULT_LOOKAHEAD_MS,
        FORGE_LIMITER_DEFAULT_RELEASE_MS};
    ForgeLimiter *result;
    uint8_t *params;

    (void)flags;

    if (effect == NULL) {
        return ForgeResultInvalidCall;
    }

    result = (ForgeLimiter *)custom_malloc(sizeof(ForgeLimiter));
    if (result == NULL) {
        return ForgeResultOutOfMemory;
    }
    params = (uint8_t *)custom_malloc(sizeof(ForgeLimiterParameters));
    if (params == NULL) {
        custom_free(result);
        return ForgeResultOutOfMemory;
    }

    fa_effect_base_init_with_allocator(&result->base, &limiter_info, params, sizeof(ForgeLimiterParameters),
                                       custom_malloc, custom_free, custom_realloc);
    forge_zero(&result->applied_parameters, sizeof(result->applied_parameters));
    result->sample_rate = 0;
    result->channels = 0;
    result->max_lookahead_frames = 0;
    result->lookahead_frames = 0;
    result->ring_capacity_frames = 0;
    result->read_frame = 0;
    result->write_frame = 0;
    result->delay_buffer = NULL;
    result->input_gain_linear = 1.0f;
    result->ceiling_linear = limiter_db_to_linear(FORGE_LIMITER_DEFAULT_CEILING_DB);
    result->release_coeff = 0.0f;
    result->current_gain = 1.0f;

    result->base.base.initialize = (ForgeEffectInitializeFunc)fa_limiter_initialize;
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)fa_limiter_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)fa_limiter_unlock_for_process;
    result->base.base.reset = (ForgeEffectResetFunc)fa_limiter_reset;
    result->base.base.process = (ForgeEffectProcessFunc)fa_limiter_process;
    result->base.base.set_parameters = (ForgeEffectSetParametersFunc)fa_limiter_set_parameters;
    result->base.base.get_parameters = (ForgeEffectGetParametersFunc)fa_limiter_get_parameters;
    result->base.base.kind = ForgeEffectKindLimiter;
    result->base.destructor = fa_limiter_free;

    result->base.base.initialize(result, &default_parameters, sizeof(default_parameters));

    *effect = &result->base.base;
    return ForgeResultSuccess;
}
