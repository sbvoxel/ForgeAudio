/* ForgeAudioEngine
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

#include "forge_effect_base.h"
#include "forge_audio_internal.h"

/* ForgeEffectBase Interface */

void forge_effect_base_init(
    ForgeEffectBase *effect,
    const ForgeEffectProperties *registration_properties,
    uint8_t *parameter_blocks,
    uint32_t parameter_block_byte_size,
    uint8_t producer
) {
    forge_effect_base_init_with_allocator(
        effect,
        registration_properties,
        parameter_blocks,
        parameter_block_byte_size,
        producer,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

void forge_effect_base_init_with_allocator(
    ForgeEffectBase *effect,
    const ForgeEffectProperties *registration_properties,
    uint8_t *parameter_blocks,
    uint32_t parameter_block_byte_size,
    uint8_t producer,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    /* Base Classes/Interfaces */
    effect->base.Destroy = (ForgeEffectDestroyFunc) forge_effect_base_destroy;
    effect->base.GetRegistrationProperties = (ForgeEffectGetPropertiesFunc)
        forge_effect_base_get_properties;
    effect->base.IsInputFormatSupported = (ForgeEffectIsInputFormatSupportedFunc)
        forge_effect_base_is_input_format_supported;
    effect->base.IsOutputFormatSupported = (ForgeEffectIsOutputFormatSupportedFunc)
        forge_effect_base_is_output_format_supported;
    effect->base.Initialize = (ForgeEffectInitializeFunc) forge_effect_base_initialize;
    effect->base.Reset = (ForgeEffectResetFunc) forge_effect_base_reset;
    effect->base.LockForProcess = (ForgeEffectLockForProcessFunc)
        forge_effect_base_lock_for_process;
    effect->base.UnlockForProcess = (ForgeEffectUnlockForProcessFunc)
        forge_effect_base_unlock_for_process;
    effect->base.CalcInputFrames = (ForgeEffectCalcInputFramesFunc)
        forge_effect_base_calc_input_frames;
    effect->base.CalcOutputFrames = (ForgeEffectCalcOutputFramesFunc)
        forge_effect_base_calc_output_frames;
    effect->base.SetParameters = (ForgeEffectSetParametersFunc)
        forge_effect_base_set_parameters;
    effect->base.GetParameters = (ForgeEffectGetParametersFunc)
        forge_effect_base_get_parameters;

    /* Public Virtual Functions */
    effect->OnSetParameters = (OnSetParametersFunc)
        forge_effect_base_on_set_parameters;

    /* Private Variables */
    effect->registration_properties = registration_properties; /* FIXME */
    effect->matrix_mix_function = NULL; /* FIXME */
    effect->matrix_coefficients = NULL; /* FIXME */
    effect->src_format_type = 0; /* FIXME */
    effect->is_scalar_matrix = 0; /* FIXME: */
    effect->is_locked = 0;
    effect->parameter_blocks = parameter_blocks;
    effect->current_parameters = parameter_blocks;
    effect->current_parameters_internal = parameter_blocks;
    effect->current_parameters_index = 0;
    effect->parameter_block_byte_size = parameter_block_byte_size;
    effect->newer_results_ready = 0;
    effect->producer = producer;

    /* Allocator Callbacks */
    effect->malloc_func = customMalloc;
    effect->free_func = customFree;
    effect->realloc_func = customRealloc;
}

void forge_effect_destroy(ForgeEffect *effect)
{
    effect->Destroy(effect);
}

void forge_effect_base_destroy(ForgeEffectBase *effect)
{
    effect->Destructor(effect);
}

ForgeResult forge_effect_base_get_properties(
    ForgeEffectBase *effect,
    ForgeEffectProperties **registration_properties
) {
    *registration_properties = (ForgeEffectProperties*) effect->malloc_func(
        sizeof(ForgeEffectProperties)
    );
    ForgeAudio_memcpy(
        *registration_properties,
        effect->registration_properties,
        sizeof(ForgeEffectProperties)
    );
    return 0;
}

ForgeResult forge_effect_base_is_input_format_supported(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *output_format,
    const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format
) {
    if (    requested_input_format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        requested_input_format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_input_format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_input_format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_input_format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_input_format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (supported_input_format != NULL)
        {
            (*supported_input_format)->format_tag =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            (*supported_input_format)->channels = ForgeAudio_clamp(
                requested_input_format->channels,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            (*supported_input_format)->sample_rate = ForgeAudio_clamp(
                requested_input_format->sample_rate,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            (*supported_input_format)->bits_per_sample =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_effect_base_is_output_format_supported(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *input_format,
    const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format
) {
    if (    requested_output_format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        requested_output_format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_output_format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_output_format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_output_format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_output_format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (supported_output_format != NULL)
        {
            (*supported_output_format)->format_tag =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            (*supported_output_format)->channels = ForgeAudio_clamp(
                requested_output_format->channels,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            (*supported_output_format)->sample_rate = ForgeAudio_clamp(
                requested_output_format->sample_rate,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            (*supported_output_format)->bits_per_sample =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_effect_base_initialize(
    ForgeEffectBase *effect,
    const void* data,
    uint32_t DataByteSize
) {
    return 0;
}

void forge_effect_base_reset(ForgeEffectBase *effect)
{
}

ForgeResult forge_effect_base_lock_for_process(
    ForgeEffectBase *effect,
    uint32_t InputLockedParameterCount,
    const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t OutputLockedParameterCount,
    const ForgeEffectLockBuffer *output_locked_parameters
) {
    /* Verify parameter counts... */
    if (    InputLockedParameterCount < effect->registration_properties->MinInputBufferCount ||
        InputLockedParameterCount > effect->registration_properties->MaxInputBufferCount ||
        OutputLockedParameterCount < effect->registration_properties->MinOutputBufferCount ||
        OutputLockedParameterCount > effect->registration_properties->MaxOutputBufferCount    )
    {
        return ForgeResultInvalidArgument;
    }


    /* Validate input/output formats */
    #define VERIFY_FORMAT_FLAG(flag, prop) \
        if (    (effect->registration_properties->Flags & flag) && \
            (input_locked_parameters->format->prop != output_locked_parameters->format->prop)    ) \
        { \
            return ForgeResultInvalidArgument; \
        }
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH, channels)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH, sample_rate)
    VERIFY_FORMAT_FLAG(FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH, bits_per_sample)
    #undef VERIFY_FORMAT_FLAG
    if (    (effect->registration_properties->Flags & FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (InputLockedParameterCount != OutputLockedParameterCount)    )
    {
        return ForgeResultInvalidArgument;
    }
    effect->is_locked = 1;
    return 0;
}

void forge_effect_base_unlock_for_process(ForgeEffectBase *effect)
{
    effect->is_locked = 0;
}

uint32_t forge_effect_base_calc_input_frames(ForgeEffectBase *effect, uint32_t OutputFrameCount)
{
    return OutputFrameCount;
}

uint32_t forge_effect_base_calc_output_frames(ForgeEffectBase *effect, uint32_t InputFrameCount)
{
    return InputFrameCount;
}

ForgeResult forge_effect_base_validate_default_format(
    ForgeEffectBase *effect,
    ForgeAudioFormat *format,
    uint8_t overwrite
) {
    if (    format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (overwrite)
        {
            format->format_tag =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            format->channels = ForgeAudio_clamp(
                format->channels,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            format->sample_rate = ForgeAudio_clamp(
                format->sample_rate,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            format->bits_per_sample =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_effect_base_validate_format_pair(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *supported_format,
    ForgeAudioFormat *requested_format,
    uint8_t overwrite
) {
    if (    requested_format->format_tag != FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG ||
        requested_format->channels < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_format->channels > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_format->sample_rate < FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_format->sample_rate > FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_format->bits_per_sample != FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (overwrite)
        {
            requested_format->format_tag =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_TAG;
            requested_format->channels = ForgeAudio_clamp(
                requested_format->channels,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            requested_format->sample_rate = ForgeAudio_clamp(
                requested_format->sample_rate,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            requested_format->bits_per_sample =
                FORGE_EFFECT_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultEffectFormatUnsupported;
    }
    return 0;
}

void forge_effect_base_process_through(
    ForgeEffectBase *effect,
    void* input_buffer,
    float *output_buffer,
    uint32_t FrameCount,
    uint16_t InputChannelCount,
    uint16_t OutputChannelCount,
    uint8_t MixWithOutput
) {
    uint32_t i, co, ci;
    float *input = (float*) input_buffer;

    if (MixWithOutput)
    {
        /* TODO: SSE */
        for (i = 0; i < FrameCount; i += 1)
        for (co = 0; co < OutputChannelCount; co += 1)
        for (ci = 0; ci < InputChannelCount; ci += 1)
        {
            /* Add, don't overwrite! */
            output_buffer[i * OutputChannelCount + co] +=
                input[i * InputChannelCount + ci];
        }
    }
    else
    {
        /* TODO: SSE */
        for (i = 0; i < FrameCount; i += 1)
        for (co = 0; co < OutputChannelCount; co += 1)
        for (ci = 0; ci < InputChannelCount; ci += 1)
        {
            /* Overwrite, don't add! */
            output_buffer[i * OutputChannelCount + co] =
                input[i * InputChannelCount + ci];
        }
    }
}

void forge_effect_base_set_parameters(
    ForgeEffectBase *effect,
    const void* parameters,
    uint32_t ParameterByteSize
) {
    ForgeAudio_assert(!effect->producer);

    /* User callback for validation */
    effect->OnSetParameters(
        effect,
        parameters,
        ParameterByteSize
    );

    /* Increment parameter block index... */
    effect->current_parameters_index += 1;
    if (effect->current_parameters_index == 3)
    {
        effect->current_parameters_index = 0;
    }
    effect->current_parameters_internal = effect->parameter_blocks + (
        effect->parameter_block_byte_size *
        effect->current_parameters_index
    );

    /* Copy to what will eventually be the next parameter update */
    ForgeAudio_memcpy(
        effect->current_parameters_internal,
        parameters,
        ParameterByteSize
    );
}

void forge_effect_base_get_parameters(
    ForgeEffectBase *effect,
    void* parameters,
    uint32_t ParameterByteSize
) {
    /* Copy what's current as of the last Process */
    ForgeAudio_memcpy(
        parameters,
        effect->current_parameters,
        ParameterByteSize
    );
}

void forge_effect_base_on_set_parameters(
    ForgeEffectBase *effect,
    const void* parameters,
    uint32_t parametersSize
) {
}

uint8_t forge_effect_base_parameters_changed(ForgeEffectBase *effect)
{
    /* Internal will get updated when SetParameters is called */
    return effect->current_parameters_internal != effect->current_parameters;
}

uint8_t* forge_effect_base_begin_process(ForgeEffectBase *effect)
{
    /* Set the latest block as "current", this is what Process will use now */
    effect->current_parameters = effect->current_parameters_internal;
    return effect->current_parameters;
}

void forge_effect_base_end_process(ForgeEffectBase *effect)
{
    /* I'm 100% sure my parameter block increment is wrong... */
}
