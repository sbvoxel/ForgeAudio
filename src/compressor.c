/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include <forge/effects.h>
#include "effect_base_internal.h"
#include "format_internal.h"

static const ForgeEffectInfo compressor_info = {
    .flags = FORGE_EFFECT_BASE_DEFAULT_FLAG,
    .min_input_buffer_count = 1,
    .max_input_buffer_count = 1,
    .min_output_buffer_count = 1,
    .max_output_buffer_count = 1};

#define COMPRESSOR_SIGNAL_EPSILON 0.0000001f
#define COMPRESSOR_MIN_LEVEL_DB -120.0f

typedef struct ForgeCompressor {
    ForgeEffectBase base;
    ForgeCompressorParameters applied_parameters;
    uint32_t sample_rate;
    uint16_t channels;
    float makeup_gain_linear;
    float wet_mix;
    float dry_mix;
    float attack_coeff;
    float release_coeff;
    float current_reduction_db;
} ForgeCompressor;

static int8_t compressor_is_float_format(const ForgeAudioFormat *format) {
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

static uint8_t compressor_float_is_finite(float value) {
    return value == value && value > -3.4e38f && value < 3.4e38f;
}

static float compressor_sanitize(float value, float fallback) {
    return compressor_float_is_finite(value) ? value : fallback;
}

static float compressor_db_to_linear(float db) {
    return forge_powf(10.0f, db / 20.0f);
}

static float compressor_linear_to_db(float linear) {
    if (linear <= 0.0f) {
        return COMPRESSOR_MIN_LEVEL_DB;
    }
    return (float)(20.0 * forge_log10((double)linear));
}

static float compressor_time_coeff(uint32_t sample_rate, float ms) {
    if (sample_rate == 0 || ms <= 0.0f) {
        return 0.0f;
    }
    return (float)forge_exp(-1.0 / ((double)sample_rate * ((double)ms / 1000.0)));
}

static ForgeCompressorParameters compressor_clamp_parameters(const ForgeCompressorParameters *parameters) {
    ForgeCompressorParameters result = *parameters;

    result.threshold_db = compressor_sanitize(result.threshold_db, FORGE_COMPRESSOR_DEFAULT_THRESHOLD_DB);
    result.threshold_db =
        forge_clamp(result.threshold_db, FORGE_COMPRESSOR_MIN_THRESHOLD_DB, FORGE_COMPRESSOR_MAX_THRESHOLD_DB);
    result.ratio = compressor_sanitize(result.ratio, FORGE_COMPRESSOR_DEFAULT_RATIO);
    result.ratio = forge_clamp(result.ratio, FORGE_COMPRESSOR_MIN_RATIO, FORGE_COMPRESSOR_MAX_RATIO);
    result.knee_db = compressor_sanitize(result.knee_db, FORGE_COMPRESSOR_DEFAULT_KNEE_DB);
    result.knee_db = forge_clamp(result.knee_db, FORGE_COMPRESSOR_MIN_KNEE_DB, FORGE_COMPRESSOR_MAX_KNEE_DB);
    result.attack_ms = compressor_sanitize(result.attack_ms, FORGE_COMPRESSOR_DEFAULT_ATTACK_MS);
    result.attack_ms = forge_clamp(result.attack_ms, FORGE_COMPRESSOR_MIN_ATTACK_MS, FORGE_COMPRESSOR_MAX_ATTACK_MS);
    result.release_ms = compressor_sanitize(result.release_ms, FORGE_COMPRESSOR_DEFAULT_RELEASE_MS);
    result.release_ms =
        forge_clamp(result.release_ms, FORGE_COMPRESSOR_MIN_RELEASE_MS, FORGE_COMPRESSOR_MAX_RELEASE_MS);
    result.makeup_gain_db = compressor_sanitize(result.makeup_gain_db, FORGE_COMPRESSOR_DEFAULT_MAKEUP_GAIN_DB);
    result.makeup_gain_db = forge_clamp(result.makeup_gain_db, FORGE_COMPRESSOR_MIN_MAKEUP_GAIN_DB,
                                        FORGE_COMPRESSOR_MAX_MAKEUP_GAIN_DB);
    result.wet_dry_mix = compressor_sanitize(result.wet_dry_mix, FORGE_COMPRESSOR_DEFAULT_WET_DRY_MIX);
    result.wet_dry_mix =
        forge_clamp(result.wet_dry_mix, FORGE_COMPRESSOR_MIN_WET_DRY_MIX, FORGE_COMPRESSOR_MAX_WET_DRY_MIX);

    return result;
}

static void compressor_apply_parameters(ForgeCompressor *effect, const ForgeCompressorParameters *parameters) {
    effect->applied_parameters = compressor_clamp_parameters(parameters);
    forge_memcpy(effect->base.parameters, &effect->applied_parameters, sizeof(effect->applied_parameters));
    effect->makeup_gain_linear = compressor_db_to_linear(effect->applied_parameters.makeup_gain_db);
    effect->wet_mix = effect->applied_parameters.wet_dry_mix;
    effect->dry_mix = 1.0f - effect->wet_mix;
    effect->attack_coeff = compressor_time_coeff(effect->sample_rate, effect->applied_parameters.attack_ms);
    effect->release_coeff = compressor_time_coeff(effect->sample_rate, effect->applied_parameters.release_ms);
}

static void compressor_reset_state(ForgeCompressor *effect) {
    effect->current_reduction_db = 0.0f;
}

static float compressor_target_reduction_db(const ForgeCompressorParameters *parameters, float input_db) {
    float over_db = input_db - parameters->threshold_db;
    float slope;

    if (parameters->ratio <= 1.0f) {
        return 0.0f;
    }

    slope = 1.0f - (1.0f / parameters->ratio);
    if (parameters->knee_db > 0.0f) {
        float half_knee = parameters->knee_db * 0.5f;

        if (over_db <= -half_knee) {
            return 0.0f;
        }
        if (over_db >= half_knee) {
            return over_db * slope;
        }

        return slope * (over_db + half_knee) * (over_db + half_knee) / (2.0f * parameters->knee_db);
    }

    return (over_db > 0.0f) ? over_db * slope : 0.0f;
}

static float compressor_update_gain(ForgeCompressor *effect, float peak) {
    float peak_db = compressor_linear_to_db(peak);
    float target_reduction_db = compressor_target_reduction_db(&effect->applied_parameters, peak_db);
    float coeff = (target_reduction_db > effect->current_reduction_db) ? effect->attack_coeff : effect->release_coeff;

    effect->current_reduction_db =
        target_reduction_db + ((effect->current_reduction_db - target_reduction_db) * coeff);
    if (effect->current_reduction_db < 0.0f) {
        effect->current_reduction_db = 0.0f;
    }

    return compressor_db_to_linear(-effect->current_reduction_db);
}

static ForgeResult fa_compressor_initialize(ForgeCompressor *effect, const void *data, uint32_t data_byte_size) {
    ForgeCompressorParameters parameters;

    forge_assert(data_byte_size == sizeof(ForgeCompressorParameters));
    if (data == NULL || data_byte_size != sizeof(ForgeCompressorParameters)) {
        return ForgeResultInvalidArgument;
    }

    parameters = compressor_clamp_parameters((const ForgeCompressorParameters *)data);
    compressor_apply_parameters(effect, &parameters);
    effect->base.parameters_changed = 1;
    return ForgeResultSuccess;
}

static ForgeResult fa_compressor_lock_for_process(ForgeCompressor *effect, uint32_t input_locked_parameter_count,
                                                  const ForgeEffectLockBuffer *input_locked_parameters,
                                                  uint32_t output_locked_parameter_count,
                                                  const ForgeEffectLockBuffer *output_locked_parameters) {
    ForgeResult result;

    if (!compressor_is_float_format(input_locked_parameters->format) ||
        !compressor_is_float_format(output_locked_parameters->format)) {
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
    compressor_apply_parameters(effect, (const ForgeCompressorParameters *)effect->base.parameters);
    compressor_reset_state(effect);
    return ForgeResultSuccess;
}

static void fa_compressor_unlock_for_process(ForgeCompressor *effect) {
    effect->channels = 0;
    effect->sample_rate = 0;
    compressor_apply_parameters(effect, (const ForgeCompressorParameters *)effect->base.parameters);
    compressor_reset_state(effect);
    fa_effect_base_unlock_for_process(&effect->base);
}

static void fa_compressor_process(ForgeCompressor *effect, uint32_t input_buffer_count,
                                  const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                                  ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    const float *input = (const float *)input_buffers->buffer;
    float *output = (float *)output_buffers->buffer;
    uint32_t frame_count = input_buffers->valid_frame_count;
    float output_peak = 0.0f;

    (void)input_buffer_count;
    (void)output_buffer_count;

    if (fa_effect_base_parameters_changed(&effect->base)) {
        compressor_apply_parameters(effect, (const ForgeCompressorParameters *)effect->base.parameters);
    }

    output_buffers->valid_frame_count = frame_count;

    if (is_enabled == 0) {
        output_buffers->buffer_flags = input_buffers->buffer_flags;
        if (input != output && input_buffers->buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            forge_memcpy(output, input, frame_count * effect->channels * sizeof(float));
        }
        compressor_reset_state(effect);
        fa_effect_base_end_process(&effect->base);
        return;
    }

    for (uint32_t frame = 0; frame < frame_count; frame += 1) {
        float peak = 0.0f;
        float gain;

        if (input_buffers->buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
                float sample_abs = forge_fabsf(input[frame * effect->channels + channel]);
                if (sample_abs > peak) {
                    peak = sample_abs;
                }
            }
        }

        gain = compressor_update_gain(effect, peak);
        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            uint32_t index = (frame * effect->channels) + channel;
            float dry = (input_buffers->buffer_flags == FORGE_EFFECT_BUFFER_SILENT) ? 0.0f : input[index];
            float compressed = dry * gain * effect->makeup_gain_linear;
            float mixed = (dry * effect->dry_mix) + (compressed * effect->wet_mix);

            output[index] = mixed;
            if (forge_fabsf(mixed) > output_peak) {
                output_peak = forge_fabsf(mixed);
            }
        }
    }

    output_buffers->buffer_flags =
        (output_peak > COMPRESSOR_SIGNAL_EPSILON) ? FORGE_EFFECT_BUFFER_VALID : FORGE_EFFECT_BUFFER_SILENT;

    fa_effect_base_end_process(&effect->base);
}

static void fa_compressor_reset(ForgeCompressor *effect) {
    fa_effect_base_reset(&effect->base);
    compressor_reset_state(effect);
}

static void fa_compressor_set_parameters(ForgeCompressor *effect, const ForgeCompressorParameters *parameters,
                                         uint32_t parameter_byte_size) {
    ForgeCompressorParameters clamped_parameters;

    forge_assert(parameter_byte_size == sizeof(ForgeCompressorParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeCompressorParameters)) {
        return;
    }

    clamped_parameters = compressor_clamp_parameters(parameters);
    forge_memcpy(effect->base.parameters, &clamped_parameters, sizeof(clamped_parameters));
    effect->base.parameters_changed = 1;
}

static void fa_compressor_get_parameters(ForgeCompressor *effect, ForgeCompressorParameters *parameters,
                                         uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == sizeof(ForgeCompressorParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeCompressorParameters)) {
        return;
    }

    forge_memcpy(parameters, effect->base.parameters, sizeof(ForgeCompressorParameters));
}

static void fa_compressor_free(void *effect) {
    ForgeCompressor *compressor = (ForgeCompressor *)effect;

    compressor->base.free_func(compressor->base.parameters);
    compressor->base.free_func(effect);
}

ForgeResult forge_create_compressor(ForgeEffect **effect, uint32_t flags) {
    return forge_create_compressor_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

ForgeResult forge_create_compressor_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                   ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                                   ForgeReallocFunc custom_realloc) {
    const ForgeCompressorParameters default_parameters = {
        FORGE_COMPRESSOR_DEFAULT_THRESHOLD_DB,
        FORGE_COMPRESSOR_DEFAULT_RATIO,
        FORGE_COMPRESSOR_DEFAULT_KNEE_DB,
        FORGE_COMPRESSOR_DEFAULT_ATTACK_MS,
        FORGE_COMPRESSOR_DEFAULT_RELEASE_MS,
        FORGE_COMPRESSOR_DEFAULT_MAKEUP_GAIN_DB,
        FORGE_COMPRESSOR_DEFAULT_WET_DRY_MIX};
    ForgeCompressor *result;
    uint8_t *params;

    (void)flags;

    if (effect == NULL) {
        return ForgeResultInvalidCall;
    }

    result = (ForgeCompressor *)custom_malloc(sizeof(ForgeCompressor));
    if (result == NULL) {
        return ForgeResultOutOfMemory;
    }
    params = (uint8_t *)custom_malloc(sizeof(ForgeCompressorParameters));
    if (params == NULL) {
        custom_free(result);
        return ForgeResultOutOfMemory;
    }

    fa_effect_base_init_with_allocator(&result->base, &compressor_info, params, sizeof(ForgeCompressorParameters),
                                       custom_malloc, custom_free, custom_realloc);
    forge_zero(&result->applied_parameters, sizeof(result->applied_parameters));
    result->sample_rate = 0;
    result->channels = 0;
    result->makeup_gain_linear = 1.0f;
    result->wet_mix = FORGE_COMPRESSOR_DEFAULT_WET_DRY_MIX;
    result->dry_mix = 1.0f - result->wet_mix;
    result->attack_coeff = 0.0f;
    result->release_coeff = 0.0f;
    result->current_reduction_db = 0.0f;

    result->base.base.initialize = (ForgeEffectInitializeFunc)fa_compressor_initialize;
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)fa_compressor_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)fa_compressor_unlock_for_process;
    result->base.base.reset = (ForgeEffectResetFunc)fa_compressor_reset;
    result->base.base.process = (ForgeEffectProcessFunc)fa_compressor_process;
    result->base.base.set_parameters = (ForgeEffectSetParametersFunc)fa_compressor_set_parameters;
    result->base.base.get_parameters = (ForgeEffectGetParametersFunc)fa_compressor_get_parameters;
    result->base.base.kind = ForgeEffectKindCompressor;
    result->base.destructor = fa_compressor_free;

    result->base.base.initialize(result, &default_parameters, sizeof(default_parameters));

    *effect = &result->base.base;
    return ForgeResultSuccess;
}
