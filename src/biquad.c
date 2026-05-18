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

static const ForgeEffectInfo biquad_info = {
    .flags = FORGE_EFFECT_BASE_DEFAULT_FLAG,
    .min_input_buffer_count = 1,
    .max_input_buffer_count = 1,
    .min_output_buffer_count = 1,
    .max_output_buffer_count = 1};

#define BIQUAD_SIGNAL_EPSILON 0.0000001f
#define BIQUAD_PI 3.14159265358979323846f

typedef struct BiquadCoefficients {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} BiquadCoefficients;

typedef struct BiquadState {
    float z1;
    float z2;
} BiquadState;

typedef struct BiquadFieldAutomation {
    float target;
    float step;
    uint32_t remaining_frames;
    uint8_t active;
} BiquadFieldAutomation;

typedef struct BiquadAutomation {
    BiquadFieldAutomation frequency_hz;
    BiquadFieldAutomation q;
    BiquadFieldAutomation gain_db;
    BiquadFieldAutomation wet_dry_mix;
} BiquadAutomation;

typedef struct ForgeBiquad {
    ForgeEffectBase base;
    ForgeBiquadParameters applied_parameters;
    BiquadCoefficients coefficients;
    BiquadAutomation automation;
    BiquadState *state;
    uint32_t sample_rate;
    uint16_t channels;
    float wet_mix;
    float dry_mix;
} ForgeBiquad;

static int8_t biquad_is_float_format(const ForgeAudioFormat *format) {
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

static uint8_t biquad_float_is_finite(float value) {
    return value == value && value > -3.4e38f && value < 3.4e38f;
}

static float biquad_sanitize(float value, float fallback) {
    return biquad_float_is_finite(value) ? value : fallback;
}

static ForgeResult biquad_validate_type(ForgeBiquadType type) {
    switch (type) {
    case ForgeBiquadLowPass:
    case ForgeBiquadHighPass:
    case ForgeBiquadBandPass:
    case ForgeBiquadNotch:
    case ForgeBiquadLowShelf:
    case ForgeBiquadHighShelf:
    case ForgeBiquadPeaking:
    case ForgeBiquadAllPass:
        return ForgeResultSuccess;
    }

    return ForgeResultInvalidArgument;
}

static float biquad_max_frequency(uint32_t sample_rate) {
    float max_hz = FORGE_BIQUAD_MAX_FREQUENCY_HZ;

    if (sample_rate > 0) {
        float nyquist_limit = ((float)sample_rate * 0.5f) - 0.001f;
        if (nyquist_limit < max_hz) {
            max_hz = nyquist_limit;
        }
    }
    if (max_hz < FORGE_BIQUAD_MIN_FREQUENCY_HZ) {
        max_hz = FORGE_BIQUAD_MIN_FREQUENCY_HZ;
    }
    return max_hz;
}

static ForgeBiquadParameters biquad_clamp_parameters(const ForgeBiquadParameters *parameters, uint32_t sample_rate) {
    ForgeBiquadParameters result = *parameters;
    float max_frequency = biquad_max_frequency(sample_rate);

    if (biquad_validate_type(result.type) != ForgeResultSuccess) {
        result.type = FORGE_BIQUAD_DEFAULT_TYPE;
    }
    result.frequency_hz = biquad_sanitize(result.frequency_hz, FORGE_BIQUAD_DEFAULT_FREQUENCY_HZ);
    result.frequency_hz = forge_clamp(result.frequency_hz, FORGE_BIQUAD_MIN_FREQUENCY_HZ, max_frequency);
    result.q = biquad_sanitize(result.q, FORGE_BIQUAD_DEFAULT_Q);
    result.q = forge_clamp(result.q, FORGE_BIQUAD_MIN_Q, FORGE_BIQUAD_MAX_Q);
    result.gain_db = biquad_sanitize(result.gain_db, FORGE_BIQUAD_DEFAULT_GAIN_DB);
    result.gain_db = forge_clamp(result.gain_db, FORGE_BIQUAD_MIN_GAIN_DB, FORGE_BIQUAD_MAX_GAIN_DB);
    result.wet_dry_mix = biquad_sanitize(result.wet_dry_mix, FORGE_BIQUAD_DEFAULT_WET_DRY_MIX);
    result.wet_dry_mix =
        forge_clamp(result.wet_dry_mix, FORGE_BIQUAD_MIN_WET_DRY_MIX, FORGE_BIQUAD_MAX_WET_DRY_MIX);

    return result;
}

static void biquad_set_bypass(BiquadCoefficients *coefficients) {
    coefficients->b0 = 1.0f;
    coefficients->b1 = 0.0f;
    coefficients->b2 = 0.0f;
    coefficients->a1 = 0.0f;
    coefficients->a2 = 0.0f;
}

static void biquad_normalize(BiquadCoefficients *coefficients, float b0, float b1, float b2, float a0, float a1,
                             float a2) {
    float inv_a0;

    if (!biquad_float_is_finite(a0) || forge_fabsf(a0) < 0.000000001f) {
        biquad_set_bypass(coefficients);
        return;
    }

    inv_a0 = 1.0f / a0;
    coefficients->b0 = b0 * inv_a0;
    coefficients->b1 = b1 * inv_a0;
    coefficients->b2 = b2 * inv_a0;
    coefficients->a1 = a1 * inv_a0;
    coefficients->a2 = a2 * inv_a0;

    if (!biquad_float_is_finite(coefficients->b0) || !biquad_float_is_finite(coefficients->b1) ||
        !biquad_float_is_finite(coefficients->b2) || !biquad_float_is_finite(coefficients->a1) ||
        !biquad_float_is_finite(coefficients->a2)) {
        biquad_set_bypass(coefficients);
    }
}

static void biquad_compute_coefficients(BiquadCoefficients *coefficients, const ForgeBiquadParameters *parameters,
                                        uint32_t sample_rate) {
    ForgeBiquadParameters params = biquad_clamp_parameters(parameters, sample_rate);
    float omega;
    float sn;
    float cs;
    float alpha;
    float a0;
    float a1;
    float a2;
    float b0;
    float b1;
    float b2;
    float a_gain;

    if (sample_rate == 0) {
        biquad_set_bypass(coefficients);
        return;
    }

    omega = 2.0f * BIQUAD_PI * params.frequency_hz / (float)sample_rate;
    sn = forge_sinf(omega);
    cs = forge_cosf(omega);
    alpha = sn / (2.0f * params.q);
    a_gain = forge_powf(10.0f, params.gain_db / 40.0f);

    switch (params.type) {
    case ForgeBiquadLowPass:
        b0 = (1.0f - cs) * 0.5f;
        b1 = 1.0f - cs;
        b2 = (1.0f - cs) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha;
        break;
    case ForgeBiquadHighPass:
        b0 = (1.0f + cs) * 0.5f;
        b1 = -(1.0f + cs);
        b2 = (1.0f + cs) * 0.5f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha;
        break;
    case ForgeBiquadBandPass:
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha;
        break;
    case ForgeBiquadNotch:
        b0 = 1.0f;
        b1 = -2.0f * cs;
        b2 = 1.0f;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha;
        break;
    case ForgeBiquadLowShelf: {
        float beta = 2.0f * forge_sqrtf(a_gain) * alpha;

        b0 = a_gain * ((a_gain + 1.0f) - ((a_gain - 1.0f) * cs) + beta);
        b1 = 2.0f * a_gain * ((a_gain - 1.0f) - ((a_gain + 1.0f) * cs));
        b2 = a_gain * ((a_gain + 1.0f) - ((a_gain - 1.0f) * cs) - beta);
        a0 = (a_gain + 1.0f) + ((a_gain - 1.0f) * cs) + beta;
        a1 = -2.0f * ((a_gain - 1.0f) + ((a_gain + 1.0f) * cs));
        a2 = (a_gain + 1.0f) + ((a_gain - 1.0f) * cs) - beta;
        break;
    }
    case ForgeBiquadHighShelf: {
        float beta = 2.0f * forge_sqrtf(a_gain) * alpha;

        b0 = a_gain * ((a_gain + 1.0f) + ((a_gain - 1.0f) * cs) + beta);
        b1 = -2.0f * a_gain * ((a_gain - 1.0f) + ((a_gain + 1.0f) * cs));
        b2 = a_gain * ((a_gain + 1.0f) + ((a_gain - 1.0f) * cs) - beta);
        a0 = (a_gain + 1.0f) - ((a_gain - 1.0f) * cs) + beta;
        a1 = 2.0f * ((a_gain - 1.0f) - ((a_gain + 1.0f) * cs));
        a2 = (a_gain + 1.0f) - ((a_gain - 1.0f) * cs) - beta;
        break;
    }
    case ForgeBiquadPeaking:
        b0 = 1.0f + (alpha * a_gain);
        b1 = -2.0f * cs;
        b2 = 1.0f - (alpha * a_gain);
        a0 = 1.0f + (alpha / a_gain);
        a1 = -2.0f * cs;
        a2 = 1.0f - (alpha / a_gain);
        break;
    case ForgeBiquadAllPass:
        b0 = 1.0f - alpha;
        b1 = -2.0f * cs;
        b2 = 1.0f + alpha;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha;
        break;
    default:
        biquad_set_bypass(coefficients);
        return;
    }

    biquad_normalize(coefficients, b0, b1, b2, a0, a1, a2);
}

static uint8_t biquad_automation_active(ForgeBiquad *effect) {
    return effect->automation.frequency_hz.active || effect->automation.q.active || effect->automation.gain_db.active ||
           effect->automation.wet_dry_mix.active;
}

static void biquad_clear_automation(ForgeBiquad *effect) {
    forge_zero(&effect->automation, sizeof(effect->automation));
}

static void biquad_apply_parameters(ForgeBiquad *effect, const ForgeBiquadParameters *parameters) {
    effect->applied_parameters = biquad_clamp_parameters(parameters, effect->sample_rate);
    forge_memcpy(effect->base.parameters, &effect->applied_parameters, sizeof(effect->applied_parameters));
    effect->wet_mix = effect->applied_parameters.wet_dry_mix;
    effect->dry_mix = 1.0f - effect->wet_mix;
    biquad_compute_coefficients(&effect->coefficients, &effect->applied_parameters, effect->sample_rate);
}

static void biquad_reset_state(ForgeBiquad *effect) {
    if (effect->state != NULL) {
        forge_zero(effect->state, effect->channels * sizeof(BiquadState));
    }
}

static float *biquad_parameter_ptr(ForgeBiquad *effect, uint32_t field) {
    ForgeBiquadParameters *params = (ForgeBiquadParameters *)effect->base.parameters;

    switch (field) {
    case FORGE_BIQUAD_TARGET_FREQUENCY_HZ:
        return &params->frequency_hz;
    case FORGE_BIQUAD_TARGET_Q:
        return &params->q;
    case FORGE_BIQUAD_TARGET_GAIN_DB:
        return &params->gain_db;
    case FORGE_BIQUAD_TARGET_WET_DRY_MIX:
        return &params->wet_dry_mix;
    }

    return NULL;
}

static BiquadFieldAutomation *biquad_automation_ptr(ForgeBiquad *effect, uint32_t field) {
    switch (field) {
    case FORGE_BIQUAD_TARGET_FREQUENCY_HZ:
        return &effect->automation.frequency_hz;
    case FORGE_BIQUAD_TARGET_Q:
        return &effect->automation.q;
    case FORGE_BIQUAD_TARGET_GAIN_DB:
        return &effect->automation.gain_db;
    case FORGE_BIQUAD_TARGET_WET_DRY_MIX:
        return &effect->automation.wet_dry_mix;
    }

    return NULL;
}

static float biquad_target_value(const ForgeBiquadTarget *target, uint32_t field) {
    switch (field) {
    case FORGE_BIQUAD_TARGET_FREQUENCY_HZ:
        return target->frequency_hz;
    case FORGE_BIQUAD_TARGET_Q:
        return target->q;
    case FORGE_BIQUAD_TARGET_GAIN_DB:
        return target->gain_db;
    case FORGE_BIQUAD_TARGET_WET_DRY_MIX:
        return target->wet_dry_mix;
    }

    return 0.0f;
}

static float biquad_clamp_target_field(ForgeBiquad *effect, uint32_t field, float value) {
    switch (field) {
    case FORGE_BIQUAD_TARGET_FREQUENCY_HZ:
        value = biquad_sanitize(value, FORGE_BIQUAD_DEFAULT_FREQUENCY_HZ);
        return forge_clamp(value, FORGE_BIQUAD_MIN_FREQUENCY_HZ, biquad_max_frequency(effect->sample_rate));
    case FORGE_BIQUAD_TARGET_Q:
        value = biquad_sanitize(value, FORGE_BIQUAD_DEFAULT_Q);
        return forge_clamp(value, FORGE_BIQUAD_MIN_Q, FORGE_BIQUAD_MAX_Q);
    case FORGE_BIQUAD_TARGET_GAIN_DB:
        value = biquad_sanitize(value, FORGE_BIQUAD_DEFAULT_GAIN_DB);
        return forge_clamp(value, FORGE_BIQUAD_MIN_GAIN_DB, FORGE_BIQUAD_MAX_GAIN_DB);
    case FORGE_BIQUAD_TARGET_WET_DRY_MIX:
        value = biquad_sanitize(value, FORGE_BIQUAD_DEFAULT_WET_DRY_MIX);
        return forge_clamp(value, FORGE_BIQUAD_MIN_WET_DRY_MIX, FORGE_BIQUAD_MAX_WET_DRY_MIX);
    }

    return value;
}

static uint8_t biquad_advance_field_one_frame(BiquadFieldAutomation *automation, float *value) {
    if (!automation->active) {
        return 0;
    }

    automation->remaining_frames -= 1;
    if (automation->remaining_frames == 0) {
        *value = automation->target;
        automation->active = 0;
    } else {
        *value += automation->step;
    }
    return 1;
}

static void biquad_advance_automation_one_frame(ForgeBiquad *effect) {
    uint8_t advanced = 0;

    advanced |= biquad_advance_field_one_frame(&effect->automation.frequency_hz,
                                               biquad_parameter_ptr(effect, FORGE_BIQUAD_TARGET_FREQUENCY_HZ));
    advanced |= biquad_advance_field_one_frame(&effect->automation.q,
                                               biquad_parameter_ptr(effect, FORGE_BIQUAD_TARGET_Q));
    advanced |= biquad_advance_field_one_frame(&effect->automation.gain_db,
                                               biquad_parameter_ptr(effect, FORGE_BIQUAD_TARGET_GAIN_DB));
    advanced |= biquad_advance_field_one_frame(&effect->automation.wet_dry_mix,
                                               biquad_parameter_ptr(effect, FORGE_BIQUAD_TARGET_WET_DRY_MIX));

    if (advanced) {
        biquad_apply_parameters(effect, (const ForgeBiquadParameters *)effect->base.parameters);
    }
}

static void fa_biquad_advance_automation(ForgeBiquad *effect, uint32_t frame_count) {
    while (frame_count > 0 && biquad_automation_active(effect)) {
        biquad_advance_automation_one_frame(effect);
        frame_count -= 1;
    }
}

static void biquad_set_field_automation(ForgeBiquad *effect, uint32_t field, float target, uint32_t duration_frames) {
    BiquadFieldAutomation *automation = biquad_automation_ptr(effect, field);
    float *value = biquad_parameter_ptr(effect, field);

    if (duration_frames == 0) {
        *value = target;
        automation->active = 0;
        automation->remaining_frames = 0;
        return;
    }

    automation->target = target;
    automation->step = (target - *value) / (float)duration_frames;
    automation->remaining_frames = duration_frames;
    automation->active = 1;
}

static ForgeResult fa_biquad_set_target(ForgeBiquad *effect, const ForgeBiquadTarget *target,
                                        uint32_t duration_frames) {
    static const uint32_t fields[] = {FORGE_BIQUAD_TARGET_FREQUENCY_HZ, FORGE_BIQUAD_TARGET_Q,
                                      FORGE_BIQUAD_TARGET_GAIN_DB, FORGE_BIQUAD_TARGET_WET_DRY_MIX};
    uint32_t unknown;

    if (target == NULL) {
        return ForgeResultInvalidCall;
    }
    unknown = target->field_mask & ~FORGE_BIQUAD_TARGET_ALL;
    if (target->field_mask == 0 || unknown != 0) {
        return ForgeResultInvalidArgument;
    }

    for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i += 1) {
        uint32_t field = fields[i];

        if (target->field_mask & field) {
            float value = biquad_clamp_target_field(effect, field, biquad_target_value(target, field));
            biquad_set_field_automation(effect, field, value, duration_frames);
        }
    }

    if (duration_frames == 0) {
        biquad_apply_parameters(effect, (const ForgeBiquadParameters *)effect->base.parameters);
    }
    return ForgeResultSuccess;
}

static ForgeResult fa_biquad_initialize(ForgeBiquad *effect, const void *data, uint32_t data_byte_size) {
    ForgeBiquadParameters parameters;

    forge_assert(data_byte_size == sizeof(ForgeBiquadParameters));
    if (data == NULL || data_byte_size != sizeof(ForgeBiquadParameters)) {
        return ForgeResultInvalidArgument;
    }

    parameters = biquad_clamp_parameters((const ForgeBiquadParameters *)data, effect->sample_rate);
    biquad_apply_parameters(effect, &parameters);
    effect->base.parameters_changed = 1;
    return ForgeResultSuccess;
}

static ForgeResult fa_biquad_lock_for_process(ForgeBiquad *effect, uint32_t input_locked_parameter_count,
                                              const ForgeEffectLockBuffer *input_locked_parameters,
                                              uint32_t output_locked_parameter_count,
                                              const ForgeEffectLockBuffer *output_locked_parameters) {
    ForgeResult result;

    if (!biquad_is_float_format(input_locked_parameters->format) ||
        !biquad_is_float_format(output_locked_parameters->format)) {
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
    effect->state = (BiquadState *)effect->base.malloc_func(effect->channels * sizeof(BiquadState));
    if (effect->state == NULL) {
        fa_effect_base_unlock_for_process(&effect->base);
        return ForgeResultOutOfMemory;
    }

    biquad_apply_parameters(effect, (const ForgeBiquadParameters *)effect->base.parameters);
    biquad_reset_state(effect);
    return ForgeResultSuccess;
}

static void fa_biquad_unlock_for_process(ForgeBiquad *effect) {
    effect->base.free_func(effect->state);
    effect->state = NULL;
    effect->channels = 0;
    effect->sample_rate = 0;
    biquad_apply_parameters(effect, (const ForgeBiquadParameters *)effect->base.parameters);
    fa_effect_base_unlock_for_process(&effect->base);
}

static float biquad_process_sample(const BiquadCoefficients *coefficients, BiquadState *state, float sample) {
    float out = (coefficients->b0 * sample) + state->z1;

    state->z1 = (coefficients->b1 * sample) - (coefficients->a1 * out) + state->z2;
    state->z2 = (coefficients->b2 * sample) - (coefficients->a2 * out);

    if (!biquad_float_is_finite(out) || !biquad_float_is_finite(state->z1) || !biquad_float_is_finite(state->z2)) {
        state->z1 = 0.0f;
        state->z2 = 0.0f;
        out = 0.0f;
    }

    return out;
}

static void fa_biquad_process(ForgeBiquad *effect, uint32_t input_buffer_count,
                              const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                              ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    const float *input = (const float *)input_buffers->buffer;
    float *output = (float *)output_buffers->buffer;
    uint32_t frame_count = input_buffers->valid_frame_count;
    float output_peak = 0.0f;

    (void)input_buffer_count;
    (void)output_buffer_count;

    if (fa_effect_base_parameters_changed(&effect->base)) {
        biquad_apply_parameters(effect, (const ForgeBiquadParameters *)effect->base.parameters);
    }

    output_buffers->valid_frame_count = frame_count;

    if (is_enabled == 0) {
        output_buffers->buffer_flags = input_buffers->buffer_flags;
        if (input != output && input_buffers->buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            forge_memcpy(output, input, frame_count * effect->channels * sizeof(float));
        }
        biquad_reset_state(effect);
        fa_biquad_advance_automation(effect, frame_count);
        fa_effect_base_end_process(&effect->base);
        return;
    }

    for (uint32_t frame = 0; frame < frame_count; frame += 1) {
        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            uint32_t index = (frame * effect->channels) + channel;
            float dry = (input_buffers->buffer_flags == FORGE_EFFECT_BUFFER_SILENT) ? 0.0f : input[index];
            float wet = biquad_process_sample(&effect->coefficients, &effect->state[channel], dry);
            float mixed = (dry * effect->dry_mix) + (wet * effect->wet_mix);

            output[index] = mixed;
            if (forge_fabsf(mixed) > output_peak) {
                output_peak = forge_fabsf(mixed);
            }
        }

        if (biquad_automation_active(effect)) {
            biquad_advance_automation_one_frame(effect);
        }
    }

    output_buffers->buffer_flags =
        (output_peak > BIQUAD_SIGNAL_EPSILON) ? FORGE_EFFECT_BUFFER_VALID : FORGE_EFFECT_BUFFER_SILENT;

    fa_effect_base_end_process(&effect->base);
}

static void fa_biquad_reset(ForgeBiquad *effect) {
    fa_effect_base_reset(&effect->base);
    biquad_reset_state(effect);
}

static void fa_biquad_set_parameters(ForgeBiquad *effect, const ForgeBiquadParameters *parameters,
                                     uint32_t parameter_byte_size) {
    ForgeBiquadParameters clamped_parameters;

    forge_assert(parameter_byte_size == sizeof(ForgeBiquadParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeBiquadParameters)) {
        return;
    }

    clamped_parameters = biquad_clamp_parameters(parameters, effect->sample_rate);
    forge_memcpy(effect->base.parameters, &clamped_parameters, sizeof(clamped_parameters));
    effect->base.parameters_changed = 1;
    biquad_clear_automation(effect);
}

static void fa_biquad_get_parameters(ForgeBiquad *effect, ForgeBiquadParameters *parameters,
                                     uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == sizeof(ForgeBiquadParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeBiquadParameters)) {
        return;
    }

    forge_memcpy(parameters, effect->base.parameters, sizeof(ForgeBiquadParameters));
}

static void fa_biquad_free(void *effect) {
    ForgeBiquad *biquad = (ForgeBiquad *)effect;

    biquad->base.free_func(biquad->state);
    biquad->base.free_func(biquad->base.parameters);
    biquad->base.free_func(effect);
}

ForgeResult forge_create_biquad(ForgeEffect **effect, uint32_t flags) {
    return forge_create_biquad_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

ForgeResult forge_create_biquad_with_allocator(ForgeEffect **effect, uint32_t flags, ForgeMallocFunc custom_malloc,
                                               ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    const ForgeBiquadParameters default_parameters = {FORGE_BIQUAD_DEFAULT_TYPE,
                                                      FORGE_BIQUAD_DEFAULT_FREQUENCY_HZ,
                                                      FORGE_BIQUAD_DEFAULT_Q,
                                                      FORGE_BIQUAD_DEFAULT_GAIN_DB,
                                                      FORGE_BIQUAD_DEFAULT_WET_DRY_MIX};
    ForgeBiquad *result;
    uint8_t *params;

    (void)flags;

    if (effect == NULL) {
        return ForgeResultInvalidCall;
    }

    result = (ForgeBiquad *)custom_malloc(sizeof(ForgeBiquad));
    if (result == NULL) {
        return ForgeResultOutOfMemory;
    }
    params = (uint8_t *)custom_malloc(sizeof(ForgeBiquadParameters));
    if (params == NULL) {
        custom_free(result);
        return ForgeResultOutOfMemory;
    }

    fa_effect_base_init_with_allocator(&result->base, &biquad_info, params, sizeof(ForgeBiquadParameters),
                                       custom_malloc, custom_free, custom_realloc);
    forge_zero(&result->applied_parameters, sizeof(result->applied_parameters));
    forge_zero(&result->coefficients, sizeof(result->coefficients));
    forge_zero(&result->automation, sizeof(result->automation));
    result->state = NULL;
    result->sample_rate = 0;
    result->channels = 0;
    result->wet_mix = FORGE_BIQUAD_DEFAULT_WET_DRY_MIX;
    result->dry_mix = 1.0f - result->wet_mix;

    result->base.base.initialize = (ForgeEffectInitializeFunc)fa_biquad_initialize;
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)fa_biquad_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)fa_biquad_unlock_for_process;
    result->base.base.reset = (ForgeEffectResetFunc)fa_biquad_reset;
    result->base.base.process = (ForgeEffectProcessFunc)fa_biquad_process;
    result->base.base.set_parameters = (ForgeEffectSetParametersFunc)fa_biquad_set_parameters;
    result->base.base.get_parameters = (ForgeEffectGetParametersFunc)fa_biquad_get_parameters;
    result->base.base.kind = ForgeEffectKindBiquad;
    result->base.base.set_biquad_target = (ForgeEffectSetBiquadTargetFunc)fa_biquad_set_target;
    result->base.base.advance_automation = (ForgeEffectAdvanceAutomationFunc)fa_biquad_advance_automation;
    result->base.destructor = fa_biquad_free;

    result->base.base.initialize(result, &default_parameters, sizeof(default_parameters));

    *effect = &result->base.base;
    return ForgeResultSuccess;
}
