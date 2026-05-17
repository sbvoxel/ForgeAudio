/* ForgeAudio
 *
 * This file is part of ForgeAudio, an altered source version of FAudio.
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

#include "forge_audio_fx.h"
#include "forge_audio_internal.h"

/* volume Meter ForgeEffect Implementation */

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

static ForgeEffectProperties VolumeMeterProperties =
{
    /* .clsid = */ {0},
    /* .friendly_name = */
    {
        'V', 'o', 'l', 'u', 'm', 'e', 'M', 'e', 't', 'e', 'r', '\0'
    },
    /*.copyright_info = */
    {
        'C', 'o', 'p', 'y', 'r', 'i', 'g', 'h', 't', ' ', '(', 'c', ')',
        'E', 't', 'h', 'a', 'n', ' ', 'L', 'e', 'e', '\0'
    },
    /*.major_version = */ 0,
    /*.minor_version = */ 0,
    /*.flags = */(
        FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH |
        FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH |
        FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH |
        FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH |
        FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED |
        FORGE_EFFECT_FLAG_IN_PLACE_REQUIRED
    ),
    /*.min_input_buffer_count = */ 1,
    /*.max_input_buffer_count = */  1,
    /*.min_output_buffer_count = */ 1,
    /*.max_output_buffer_count =*/ 1
};

typedef struct ForgeAudioFxVolumeMeter
{
    ForgeEffectBase base;
    uint16_t channels;
} ForgeAudioFxVolumeMeter;

ForgeResult ForgeAudioFxVolumeMeter_LockForProcess(
    ForgeAudioFxVolumeMeter *effect,
    uint32_t input_locked_parameter_count,
    const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t output_locked_parameter_count,
    const ForgeEffectLockBuffer *output_locked_parameters
) {
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        effect->base.parameter_blocks;

    /* Verify parameter counts... */
    if (    input_locked_parameter_count < effect->base.registration_properties->min_input_buffer_count ||
        input_locked_parameter_count > effect->base.registration_properties->max_input_buffer_count ||
        output_locked_parameter_count < effect->base.registration_properties->min_output_buffer_count ||
        output_locked_parameter_count > effect->base.registration_properties->max_output_buffer_count    )
    {
        return ForgeResultInvalidArgument;
    }


    /* Validate input/output formats */
    #define VERIFY_FORMAT_FLAG(flag, prop) \
        if (    (effect->base.registration_properties->flags & flag) && \
            (input_locked_parameters->format->prop != output_locked_parameters->format->prop)    ) \
        { \
            return ForgeResultInvalidArgument; \
        }
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH, channels)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH, sample_rate)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH, bits_per_sample)
    #undef VERIFY_FORMAT_FLAG
    if (    (effect->base.registration_properties->flags & FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (input_locked_parameter_count != output_locked_parameter_count)    )
    {
        return ForgeResultInvalidArgument;
    }

    /* Allocate volume meter arrays */
    effect->channels = input_locked_parameters->format->channels;
    levels[0].peak_levels = (float*) effect->base.malloc_func(
        effect->channels * sizeof(float) * 6
    );
    ForgeAudio_zero(levels[0].peak_levels, effect->channels * sizeof(float) * 6);
    levels[0].rms_levels = levels[0].peak_levels + effect->channels;
    levels[1].peak_levels = levels[0].peak_levels + (effect->channels * 2);
    levels[1].rms_levels = levels[0].peak_levels + (effect->channels * 3);
    levels[2].peak_levels = levels[0].peak_levels + (effect->channels * 4);
    levels[2].rms_levels = levels[0].peak_levels + (effect->channels * 5);

    effect->base.is_locked = 1;
    return 0;
}

void ForgeAudioFxVolumeMeter_UnlockForProcess(ForgeAudioFxVolumeMeter *effect)
{
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        effect->base.parameter_blocks;
    effect->base.free_func(levels[0].peak_levels);
    effect->base.is_locked = 0;
}

void ForgeAudioFxVolumeMeter_Process(
    ForgeAudioFxVolumeMeter *effect,
    uint32_t input_process_parameter_count,
    const ForgeEffectProcessBuffer* input_process_parameters,
    uint32_t output_process_parameter_count,
    ForgeEffectProcessBuffer* output_process_parameters,
    int32_t is_enabled
) {
    float peak;
    float total;
    float *buffer;
    uint32_t i, j;
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        forge_effect_base_begin_process(&effect->base);

    /* TODO: This could probably be SIMD-ified... */
    for (i = 0; i < effect->channels; i += 1)
    {
        peak = 0.0f;
        total = 0.0f;
        buffer = ((float*) input_process_parameters->buffer) + i;
        for (j = 0; j < input_process_parameters->valid_frame_count; j += 1, buffer += effect->channels)
        {
            const float sampleAbs = ForgeAudio_fabsf(*buffer);
            if (sampleAbs > peak)
            {
                peak = sampleAbs;
            }
            total += (*buffer) * (*buffer);
        }
        levels->peak_levels[i] = peak;
        levels->rms_levels[i] = ForgeAudio_sqrtf(
            total / input_process_parameters->valid_frame_count
        );
    }

    forge_effect_base_end_process(&effect->base);
}

void ForgeAudioFxVolumeMeter_GetParameters(
    ForgeAudioFxVolumeMeter *effect,
    ForgeAudioFxVolumeMeterLevels *parameters,
    uint32_t parameter_byte_size
) {
    ForgeAudioFxVolumeMeterLevels *levels = (ForgeAudioFxVolumeMeterLevels*)
        effect->base.current_parameters;
    ForgeAudio_assert(parameter_byte_size == sizeof(ForgeAudioFxVolumeMeterLevels));
    ForgeAudio_assert(parameters->channel_count == effect->channels);

    /* Copy what's current as of the last process */
    if (parameters->peak_levels != NULL)
    {
        ForgeAudio_memcpy(
            parameters->peak_levels,
            levels->peak_levels,
            effect->channels * sizeof(float)
        );
    }
    if (parameters->rms_levels != NULL)
    {
        ForgeAudio_memcpy(
            parameters->rms_levels,
            levels->rms_levels,
            effect->channels * sizeof(float)
        );
    }
}

void ForgeAudioFxVolumeMeter_Free(void* effect)
{
    ForgeAudioFxVolumeMeter *volumemeter = (ForgeAudioFxVolumeMeter*) effect;
    volumemeter->base.free_func(volumemeter->base.parameter_blocks);
    volumemeter->base.free_func(effect);
}

/* Public API */

ForgeResult forge_audio_create_volume_meter(ForgeEffect** effect, uint32_t flags)
{
    return forge_audio_create_volume_meter_with_allocator(
        effect,
        flags,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

ForgeResult forge_audio_create_volume_meter_with_allocator(
    ForgeEffect** effect,
    uint32_t flags,
    ForgeMallocFunc custom_malloc,
    ForgeFreeFunc custom_free,
    ForgeReallocFunc custom_realloc
) {
    /* Allocate... */
    ForgeAudioFxVolumeMeter *result = (ForgeAudioFxVolumeMeter*) custom_malloc(
        sizeof(ForgeAudioFxVolumeMeter)
    );
    uint8_t *params = (uint8_t*) custom_malloc(
        sizeof(ForgeAudioFxVolumeMeterLevels) * 3
    );
    ForgeAudio_zero(params, sizeof(ForgeAudioFxVolumeMeterLevels) * 3);

    /* initialize... */
    ForgeAudio_memcpy(
        &VolumeMeterProperties.clsid,
        &FORGE_AUDIO_FX_ID_VOLUME_METER,
        sizeof(ForgeGuid)
    );
    forge_effect_base_init_with_allocator(
        &result->base,
        &VolumeMeterProperties,
        params,
        sizeof(ForgeAudioFxVolumeMeterLevels),
        1,
        custom_malloc,
        custom_free,
        custom_realloc
    );

    /* Function table... */
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)
        ForgeAudioFxVolumeMeter_LockForProcess;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)
        ForgeAudioFxVolumeMeter_UnlockForProcess;
    result->base.base.process = (ForgeEffectProcessFunc)
        ForgeAudioFxVolumeMeter_Process;
    result->base.base.get_parameters = (ForgeEffectGetParametersFunc)
        ForgeAudioFxVolumeMeter_GetParameters;
    result->base.destructor = ForgeAudioFxVolumeMeter_Free;

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}
