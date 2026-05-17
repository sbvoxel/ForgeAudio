/*
 * ForgeAudio
 * Forked from FAudio
 *
 * Copyright (c) 2026 ForgeAudio
 *
 * Licensed under the same terms as FAudio:
 */

/* FAudio - XAudio Reimplementation for FNA
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

#include "forge_effect_base_internal.h"
#include "forge_audio_internal.h"

/* ForgeEffectBase Interface */

void forge_effect_base_init(ForgeEffectBase *effect, const ForgeEffectInfo *effect_info, uint8_t *parameters,
                            uint32_t parameter_block_byte_size) {
    forge_effect_base_init_with_allocator(effect, effect_info, parameters, parameter_block_byte_size, forge_malloc,
                                          forge_free, forge_realloc);
}

void forge_effect_base_init_with_allocator(ForgeEffectBase *effect, const ForgeEffectInfo *effect_info,
                                           uint8_t *parameters, uint32_t parameter_block_byte_size,
                                           ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                           ForgeReallocFunc custom_realloc) {
    /* Base Classes/Interfaces */
    effect->base.destroy = (ForgeEffectDestroyFunc)forge_effect_base_destroy;
    effect->base.get_info = (ForgeEffectGetInfoFunc)forge_effect_base_get_info;
    effect->base.is_input_format_supported =
        (ForgeEffectIsInputFormatSupportedFunc)forge_effect_base_is_input_format_supported;
    effect->base.is_output_format_supported =
        (ForgeEffectIsOutputFormatSupportedFunc)forge_effect_base_is_output_format_supported;
    effect->base.initialize = (ForgeEffectInitializeFunc)forge_effect_base_initialize;
    effect->base.reset = (ForgeEffectResetFunc)forge_effect_base_reset;
    effect->base.lock_for_process = (ForgeEffectLockForProcessFunc)forge_effect_base_lock_for_process;
    effect->base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)forge_effect_base_unlock_for_process;
    effect->base.calc_input_frames = (ForgeEffectCalcInputFramesFunc)forge_effect_base_calc_input_frames;
    effect->base.calc_output_frames = (ForgeEffectCalcOutputFramesFunc)forge_effect_base_calc_output_frames;
    effect->base.set_parameters = (ForgeEffectSetParametersFunc)forge_effect_base_set_parameters;
    effect->base.get_parameters = (ForgeEffectGetParametersFunc)forge_effect_base_get_parameters;

    /* Public Virtual Functions */
    effect->on_set_parameters = (ForgeEffectBaseSetParametersFunc)forge_effect_base_on_set_parameters;

    /* Private Variables */
    effect->effect_info = effect_info;
    effect->is_locked = 0;
    effect->parameters = parameters;
    effect->parameter_block_byte_size = parameter_block_byte_size;
    effect->parameters_changed = 0;

    /* Allocator Callbacks */
    effect->malloc_func = custom_malloc;
    effect->free_func = custom_free;
    effect->realloc_func = custom_realloc;
}

void forge_effect_destroy(ForgeEffect *effect) {
    effect->destroy(effect);
}

void forge_effect_base_destroy(ForgeEffectBase *effect) {
    effect->destructor(effect);
}

void forge_effect_base_get_info(ForgeEffectBase *effect, ForgeEffectInfo *effect_info) {
    forge_memcpy(effect_info, effect->effect_info, sizeof(ForgeEffectInfo));
}

ForgeResult forge_effect_base_is_input_format_supported(ForgeEffectBase *effect, const ForgeAudioFormat *output_format,
                                                        const ForgeAudioFormat *requested_input_format,
                                                        ForgeAudioFormat **supported_input_format) {
    if (requested_input_format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        requested_input_format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_input_format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_input_format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_input_format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_input_format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE) {
        if (supported_input_format != NULL) {
            (*supported_input_format)->format_tag = FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            (*supported_input_format)->channels =
                forge_clamp(requested_input_format->channels, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                            FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS);
            (*supported_input_format)->sample_rate =
                forge_clamp(requested_input_format->sample_rate, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                            FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE);
            (*supported_input_format)->bits_per_sample = FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_effect_base_is_output_format_supported(ForgeEffectBase *effect, const ForgeAudioFormat *input_format,
                                                         const ForgeAudioFormat *requested_output_format,
                                                         ForgeAudioFormat **supported_output_format) {
    if (requested_output_format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        requested_output_format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_output_format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_output_format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_output_format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_output_format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE) {
        if (supported_output_format != NULL) {
            (*supported_output_format)->format_tag = FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            (*supported_output_format)->channels =
                forge_clamp(requested_output_format->channels, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                            FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS);
            (*supported_output_format)->sample_rate =
                forge_clamp(requested_output_format->sample_rate, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                            FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE);
            (*supported_output_format)->bits_per_sample = FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_effect_base_initialize(ForgeEffectBase *effect, const void *data, uint32_t data_byte_size) {
    return 0;
}

void forge_effect_base_reset(ForgeEffectBase *effect) {
}

ForgeResult forge_effect_base_lock_for_process(ForgeEffectBase *effect, uint32_t input_locked_parameter_count,
                                               const ForgeEffectLockBuffer *input_locked_parameters,
                                               uint32_t output_locked_parameter_count,
                                               const ForgeEffectLockBuffer *output_locked_parameters) {
    /* Verify parameter counts... */
    if (input_locked_parameter_count < effect->effect_info->min_input_buffer_count ||
        input_locked_parameter_count > effect->effect_info->max_input_buffer_count ||
        output_locked_parameter_count < effect->effect_info->min_output_buffer_count ||
        output_locked_parameter_count > effect->effect_info->max_output_buffer_count) {
        return ForgeResultInvalidArgument;
    }

/* Validate input/output formats */
#define VERIFY_FORMAT_FLAG(flag, prop)                                                                                 \
    if ((effect->effect_info->flags & flag) &&                                                                         \
        (input_locked_parameters->format->prop != output_locked_parameters->format->prop)) {                           \
        return ForgeResultInvalidArgument;                                                                             \
    }
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH, channels)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH, sample_rate)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH, bits_per_sample)
#undef VERIFY_FORMAT_FLAG
    if ((effect->effect_info->flags & FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (input_locked_parameter_count != output_locked_parameter_count)) {
        return ForgeResultInvalidArgument;
    }
    effect->is_locked = 1;
    return 0;
}

void forge_effect_base_unlock_for_process(ForgeEffectBase *effect) {
    effect->is_locked = 0;
}

uint32_t forge_effect_base_calc_input_frames(ForgeEffectBase *effect, uint32_t output_frame_count) {
    return output_frame_count;
}

uint32_t forge_effect_base_calc_output_frames(ForgeEffectBase *effect, uint32_t input_frame_count) {
    return input_frame_count;
}

ForgeResult forge_effect_base_validate_default_format(ForgeEffectBase *effect, ForgeAudioFormat *format,
                                                      uint8_t overwrite) {
    if (format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE) {
        if (overwrite) {
            format->format_tag = FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            format->channels = forge_clamp(format->channels, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                                           FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS);
            format->sample_rate = forge_clamp(format->sample_rate, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                                              FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE);
            format->bits_per_sample = FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_effect_base_validate_format_pair(ForgeEffectBase *effect, const ForgeAudioFormat *supported_format,
                                                   ForgeAudioFormat *requested_format, uint8_t overwrite) {
    if (requested_format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        requested_format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE) {
        if (overwrite) {
            requested_format->format_tag = FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            requested_format->channels =
                forge_clamp(requested_format->channels, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                            FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS);
            requested_format->sample_rate =
                forge_clamp(requested_format->sample_rate, FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                            FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE);
            requested_format->bits_per_sample = FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

void forge_effect_base_process_through(ForgeEffectBase *effect, void *input_buffer, float *output_buffer,
                                       uint32_t frame_count, uint16_t input_channel_count,
                                       uint16_t output_channel_count, uint8_t mix_with_output) {
    uint32_t i, co, ci;
    float *input = (float *)input_buffer;

    if (mix_with_output) {
        /* TODO: SSE */
        for (i = 0; i < frame_count; i += 1)
            for (co = 0; co < output_channel_count; co += 1)
                for (ci = 0; ci < input_channel_count; ci += 1) {
                    /* Add, don't overwrite! */
                    output_buffer[i * output_channel_count + co] += input[i * input_channel_count + ci];
                }
    } else {
        /* TODO: SSE */
        for (i = 0; i < frame_count; i += 1)
            for (co = 0; co < output_channel_count; co += 1)
                for (ci = 0; ci < input_channel_count; ci += 1) {
                    /* Overwrite, don't add! */
                    output_buffer[i * output_channel_count + co] = input[i * input_channel_count + ci];
                }
    }
}

void forge_effect_base_set_parameters(ForgeEffectBase *effect, const void *parameters, uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == effect->parameter_block_byte_size);
    if (parameter_byte_size != effect->parameter_block_byte_size) {
        return;
    }

    /* User callback for validation */
    effect->on_set_parameters(effect, parameters, parameter_byte_size);

    forge_memcpy(effect->parameters, parameters, parameter_byte_size);
    effect->parameters_changed = 1;
}

void forge_effect_base_get_parameters(ForgeEffectBase *effect, void *parameters, uint32_t parameter_byte_size) {
    forge_assert(parameter_byte_size == effect->parameter_block_byte_size);
    if (parameter_byte_size != effect->parameter_block_byte_size) {
        return;
    }

    forge_memcpy(parameters, effect->parameters, parameter_byte_size);
}

void forge_effect_base_on_set_parameters(ForgeEffectBase *effect, const void *parameters, uint32_t parametersSize) {
}

uint8_t forge_effect_base_parameters_changed(ForgeEffectBase *effect) {
    return effect->parameters_changed;
}

uint8_t *forge_effect_base_begin_process(ForgeEffectBase *effect) {
    return effect->parameters;
}

void forge_effect_base_end_process(ForgeEffectBase *effect) {
    effect->parameters_changed = 0;
}
