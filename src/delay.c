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

static const ForgeEffectInfo delay_info = {
    .flags = FORGE_EFFECT_BASE_DEFAULT_FLAG,
    .min_input_buffer_count = 1,
    .max_input_buffer_count = 1,
    .min_output_buffer_count = 1,
    .max_output_buffer_count = 1};

typedef struct ForgeDelay {
    ForgeEffectBase base;
    ForgeDelayParameters applied_parameters;
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t delay_frames;
    uint32_t max_delay_frames;
    uint32_t ring_capacity_frames;
    uint32_t read_frame;
    uint32_t write_frame;
    float *delay_buffer;
    float *lowpass_state;
    float wet_mix;
    float dry_mix;
    float lowpass_coeff;
    uint8_t lowpass_enabled;
} ForgeDelay;

static int8_t delay_is_float_format(const ForgeAudioFormat *format) {
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

static uint32_t delay_ms_to_frames(uint32_t sample_rate, float ms) {
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

static ForgeDelayParameters delay_clamp_parameters(const ForgeDelayParameters *parameters) {
    ForgeDelayParameters result = *parameters;

    result.wet_dry_mix =
        forge_clamp(result.wet_dry_mix, FORGE_DELAY_MIN_WET_DRY_MIX, FORGE_DELAY_MAX_WET_DRY_MIX);
    result.delay_ms = forge_clamp(result.delay_ms, FORGE_DELAY_MIN_DELAY_MS, FORGE_DELAY_MAX_DELAY_MS);
    result.feedback = forge_clamp(result.feedback, FORGE_DELAY_MIN_FEEDBACK, FORGE_DELAY_MAX_FEEDBACK);
    result.lowpass_hz = forge_clamp(result.lowpass_hz, FORGE_DELAY_MIN_LOWPASS_HZ, FORGE_DELAY_MAX_LOWPASS_HZ);

    return result;
}

static void delay_reset_ring(ForgeDelay *effect) {
    if (effect->delay_buffer != NULL) {
        forge_zero(effect->delay_buffer, effect->ring_capacity_frames * effect->channels * sizeof(float));
    }
    if (effect->lowpass_state != NULL) {
        forge_zero(effect->lowpass_state, effect->channels * sizeof(float));
    }
    effect->read_frame = 0;
    effect->write_frame = effect->delay_frames;
}

static void delay_update_read_frame(ForgeDelay *effect) {
    if (effect->ring_capacity_frames == 0) {
        effect->read_frame = 0;
        effect->write_frame = 0;
        return;
    }
    effect->read_frame = (effect->write_frame + effect->ring_capacity_frames - effect->delay_frames) %
                         effect->ring_capacity_frames;
}

static void delay_apply_parameters(ForgeDelay *effect, const ForgeDelayParameters *parameters) {
    uint32_t old_delay_frames = effect->delay_frames;
    const float pi = 3.14159265358979323846f;

    effect->applied_parameters = delay_clamp_parameters(parameters);
    effect->wet_mix = effect->applied_parameters.wet_dry_mix / 100.0f;
    effect->dry_mix = 1.0f - effect->wet_mix;

    if (effect->sample_rate == 0) {
        effect->delay_frames = 0;
        effect->lowpass_coeff = 1.0f;
        effect->lowpass_enabled = 0;
        return;
    }

    effect->delay_frames = delay_ms_to_frames(effect->sample_rate, effect->applied_parameters.delay_ms);
    if (effect->delay_frames > effect->max_delay_frames) {
        effect->delay_frames = effect->max_delay_frames;
    }

    if (effect->delay_frames != old_delay_frames) {
        delay_update_read_frame(effect);
    }

    if (effect->applied_parameters.lowpass_hz <= 0.0f ||
        effect->applied_parameters.lowpass_hz >= ((float)effect->sample_rate * 0.5f)) {
        effect->lowpass_enabled = 0;
        effect->lowpass_coeff = 1.0f;
    } else {
        effect->lowpass_enabled = 1;
        effect->lowpass_coeff =
            1.0f - (float)forge_exp(-2.0 * (double)pi * (double)effect->applied_parameters.lowpass_hz /
                                    (double)effect->sample_rate);
    }
}

static uint8_t delay_has_pending_signal(const ForgeDelay *effect) {
    if (effect->wet_mix <= 0.0000001f || effect->delay_buffer == NULL) {
        return 0;
    }

    for (uint32_t i = 0; i < effect->ring_capacity_frames * effect->channels; i += 1) {
        if (forge_fabsf(effect->delay_buffer[i]) > 0.0000001f) {
            return 1;
        }
    }

    return 0;
}

static ForgeResult fa_delay_initialize(ForgeDelay *effect, const void *data, uint32_t data_byte_size) {
    ForgeDelayParameters parameters;

    forge_assert(data_byte_size == sizeof(ForgeDelayParameters));
    if (data == NULL || data_byte_size != sizeof(ForgeDelayParameters)) {
        return ForgeResultInvalidArgument;
    }

    parameters = delay_clamp_parameters((const ForgeDelayParameters *)data);
    forge_memcpy(effect->base.parameters, &parameters, sizeof(parameters));
    effect->base.parameters_changed = 1;
    delay_apply_parameters(effect, &parameters);
    return ForgeResultSuccess;
}

static ForgeResult fa_delay_lock_for_process(ForgeDelay *effect, uint32_t input_locked_parameter_count,
                                             const ForgeEffectLockBuffer *input_locked_parameters,
                                             uint32_t output_locked_parameter_count,
                                             const ForgeEffectLockBuffer *output_locked_parameters) {
    ForgeResult result;
    uint32_t delay_samples;
    uint32_t lowpass_samples;

    if (!delay_is_float_format(input_locked_parameters->format) ||
        !delay_is_float_format(output_locked_parameters->format)) {
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
    effect->max_delay_frames = delay_ms_to_frames(effect->sample_rate, FORGE_DELAY_MAX_DELAY_MS);
    effect->ring_capacity_frames = effect->max_delay_frames + 1;

    delay_samples = effect->ring_capacity_frames * effect->channels;
    effect->delay_buffer = (float *)effect->base.malloc_func(delay_samples * sizeof(float));
    if (effect->delay_buffer == NULL) {
        fa_effect_base_unlock_for_process(&effect->base);
        return ForgeResultOutOfMemory;
    }

    lowpass_samples = effect->channels;
    effect->lowpass_state = (float *)effect->base.malloc_func(lowpass_samples * sizeof(float));
    if (effect->lowpass_state == NULL) {
        effect->base.free_func(effect->delay_buffer);
        effect->delay_buffer = NULL;
        fa_effect_base_unlock_for_process(&effect->base);
        return ForgeResultOutOfMemory;
    }

    delay_apply_parameters(effect, (const ForgeDelayParameters *)effect->base.parameters);
    delay_reset_ring(effect);
    return ForgeResultSuccess;
}

static void fa_delay_unlock_for_process(ForgeDelay *effect) {
    effect->base.free_func(effect->delay_buffer);
    effect->base.free_func(effect->lowpass_state);
    effect->delay_buffer = NULL;
    effect->lowpass_state = NULL;
    effect->channels = 0;
    effect->sample_rate = 0;
    effect->delay_frames = 0;
    effect->max_delay_frames = 0;
    effect->ring_capacity_frames = 0;
    effect->read_frame = 0;
    effect->write_frame = 0;
    effect->lowpass_coeff = 1.0f;
    effect->lowpass_enabled = 0;
    fa_effect_base_unlock_for_process(&effect->base);
}

static void fa_delay_process(ForgeDelay *effect, uint32_t input_buffer_count,
                             const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                             ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    const float *input = (const float *)input_buffers->buffer;
    float *output = (float *)output_buffers->buffer;
    uint32_t frame_count = input_buffers->valid_frame_count;
    float output_peak = 0.0f;

    (void)input_buffer_count;
    (void)output_buffer_count;

    if (fa_effect_base_parameters_changed(&effect->base)) {
        delay_apply_parameters(effect, (const ForgeDelayParameters *)effect->base.parameters);
    }

    output_buffers->valid_frame_count = frame_count;

    if (is_enabled == 0) {
        output_buffers->buffer_flags = input_buffers->buffer_flags;
        if (input != output && input_buffers->buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            forge_memcpy(output, input, frame_count * effect->channels * sizeof(float));
        }
        delay_reset_ring(effect);
        fa_effect_base_end_process(&effect->base);
        return;
    }

    for (uint32_t frame = 0; frame < frame_count; frame += 1) {
        float *read = effect->delay_buffer + (effect->read_frame * effect->channels);
        float *write = effect->delay_buffer + (effect->write_frame * effect->channels);

        for (uint32_t channel = 0; channel < effect->channels; channel += 1) {
            float sample = (input_buffers->buffer_flags == FORGE_EFFECT_BUFFER_SILENT)
                               ? 0.0f
                               : input[frame * effect->channels + channel];
            float delayed = read[channel];
            float feedback_sample = delayed;
            float mixed = (sample * effect->dry_mix) + (delayed * effect->wet_mix);

            if (effect->lowpass_enabled) {
                float state = effect->lowpass_state[channel];
                state += effect->lowpass_coeff * (delayed - state);
                effect->lowpass_state[channel] = state;
                feedback_sample = state;
            }

            write[channel] = sample + (feedback_sample * effect->applied_parameters.feedback);
            read[channel] = 0.0f;
            output[frame * effect->channels + channel] = mixed;

            if (forge_fabsf(mixed) > output_peak) {
                output_peak = forge_fabsf(mixed);
            }
        }

        effect->read_frame = (effect->read_frame + 1) % effect->ring_capacity_frames;
        effect->write_frame = (effect->write_frame + 1) % effect->ring_capacity_frames;
    }

    output_buffers->buffer_flags = (output_peak > 0.0000001f || delay_has_pending_signal(effect))
                                       ? FORGE_EFFECT_BUFFER_VALID
                                       : FORGE_EFFECT_BUFFER_SILENT;

    fa_effect_base_end_process(&effect->base);
}

static void fa_delay_reset(ForgeDelay *effect) {
    fa_effect_base_reset(&effect->base);
    delay_reset_ring(effect);
}

static void fa_delay_set_parameters(ForgeDelay *effect, const ForgeDelayParameters *parameters,
                                    uint32_t parameter_byte_size) {
    ForgeDelayParameters clamped_parameters;

    forge_assert(parameter_byte_size == sizeof(ForgeDelayParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeDelayParameters)) {
        return;
    }

    clamped_parameters = delay_clamp_parameters(parameters);
    forge_memcpy(effect->base.parameters, &clamped_parameters, sizeof(clamped_parameters));
    effect->base.parameters_changed = 1;
}

static void fa_delay_get_parameters(ForgeDelay *effect, ForgeDelayParameters *parameters, uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == sizeof(ForgeDelayParameters));
    if (parameters == NULL || parameter_byte_size != sizeof(ForgeDelayParameters)) {
        return;
    }

    forge_memcpy(parameters, effect->base.parameters, sizeof(ForgeDelayParameters));
}

static void fa_delay_free(void *effect) {
    ForgeDelay *delay = (ForgeDelay *)effect;

    delay->base.free_func(delay->delay_buffer);
    delay->base.free_func(delay->lowpass_state);
    delay->base.free_func(delay->base.parameters);
    delay->base.free_func(effect);
}

ForgeResult forge_create_delay(ForgeEffect **effect, uint32_t flags) {
    return forge_create_delay_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

ForgeResult forge_create_delay_with_allocator(ForgeEffect **effect, uint32_t flags, ForgeMallocFunc custom_malloc,
                                              ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    const ForgeDelayParameters default_parameters = {
        FORGE_DELAY_DEFAULT_WET_DRY_MIX,
        FORGE_DELAY_DEFAULT_DELAY_MS,
        FORGE_DELAY_DEFAULT_FEEDBACK,
        FORGE_DELAY_DEFAULT_LOWPASS_HZ};
    ForgeDelay *result;
    uint8_t *params;

    (void)flags;

    if (effect == NULL) {
        return ForgeResultInvalidCall;
    }

    result = (ForgeDelay *)custom_malloc(sizeof(ForgeDelay));
    if (result == NULL) {
        return ForgeResultOutOfMemory;
    }
    params = (uint8_t *)custom_malloc(sizeof(ForgeDelayParameters));
    if (params == NULL) {
        custom_free(result);
        return ForgeResultOutOfMemory;
    }

    fa_effect_base_init_with_allocator(&result->base, &delay_info, params, sizeof(ForgeDelayParameters),
                                       custom_malloc, custom_free, custom_realloc);
    forge_zero(&result->applied_parameters, sizeof(result->applied_parameters));
    result->sample_rate = 0;
    result->channels = 0;
    result->delay_frames = 0;
    result->max_delay_frames = 0;
    result->ring_capacity_frames = 0;
    result->read_frame = 0;
    result->write_frame = 0;
    result->delay_buffer = NULL;
    result->lowpass_state = NULL;
    result->wet_mix = FORGE_DELAY_DEFAULT_WET_DRY_MIX / 100.0f;
    result->dry_mix = 1.0f - result->wet_mix;
    result->lowpass_coeff = 1.0f;
    result->lowpass_enabled = 0;

    result->base.base.initialize = (ForgeEffectInitializeFunc)fa_delay_initialize;
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)fa_delay_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)fa_delay_unlock_for_process;
    result->base.base.reset = (ForgeEffectResetFunc)fa_delay_reset;
    result->base.base.process = (ForgeEffectProcessFunc)fa_delay_process;
    result->base.base.set_parameters = (ForgeEffectSetParametersFunc)fa_delay_set_parameters;
    result->base.base.get_parameters = (ForgeEffectGetParametersFunc)fa_delay_get_parameters;
    result->base.base.kind = ForgeEffectKindDelay;
    result->base.destructor = fa_delay_free;

    result->base.base.initialize(result, &default_parameters, sizeof(default_parameters));

    *effect = &result->base.base;
    return ForgeResultSuccess;
}
