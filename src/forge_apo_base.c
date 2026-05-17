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

#include "forge_apo_base.h"
#include "forge_audio_internal.h"

/* ForgeApoBase Interface */

void forge_apo_base_init(
    ForgeApoBase *fapo,
    const ForgeApoProperties *registration_properties,
    uint8_t *parameter_blocks,
    uint32_t parameter_block_byte_size,
    uint8_t producer
) {
    forge_apo_base_init_with_allocator(
        fapo,
        registration_properties,
        parameter_blocks,
        parameter_block_byte_size,
        producer,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

void forge_apo_base_init_with_allocator(
    ForgeApoBase *fapo,
    const ForgeApoProperties *registration_properties,
    uint8_t *parameter_blocks,
    uint32_t parameter_block_byte_size,
    uint8_t producer,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    /* Base Classes/Interfaces */
    fapo->base.AddRef = (ForgeApoAddRefFunc) forge_apo_base_retain;
    fapo->base.Release = (ForgeApoReleaseFunc) forge_apo_base_release;
    fapo->base.GetRegistrationProperties = (ForgeApoGetPropertiesFunc)
        forge_apo_base_get_properties;
    fapo->base.IsInputFormatSupported = (ForgeApoIsInputFormatSupportedFunc)
        forge_apo_base_is_input_format_supported;
    fapo->base.IsOutputFormatSupported = (ForgeApoIsOutputFormatSupportedFunc)
        forge_apo_base_is_output_format_supported;
    fapo->base.Initialize = (ForgeApoInitializeFunc) forge_apo_base_initialize;
    fapo->base.Reset = (ForgeApoResetFunc) forge_apo_base_reset;
    fapo->base.LockForProcess = (ForgeApoLockForProcessFunc)
        forge_apo_base_lock_for_process;
    fapo->base.UnlockForProcess = (ForgeApoUnlockForProcessFunc)
        forge_apo_base_unlock_for_process;
    fapo->base.CalcInputFrames = (ForgeApoCalcInputFramesFunc)
        forge_apo_base_calc_input_frames;
    fapo->base.CalcOutputFrames = (ForgeApoCalcOutputFramesFunc)
        forge_apo_base_calc_output_frames;
    fapo->base.SetParameters = (ForgeApoSetParametersFunc)
        forge_apo_base_set_parameters;
    fapo->base.GetParameters = (ForgeApoGetParametersFunc)
        forge_apo_base_get_parameters;

    /* Public Virtual Functions */
    fapo->OnSetParameters = (OnSetParametersFunc)
        forge_apo_base_on_set_parameters;

    /* Private Variables */
    fapo->registration_properties = registration_properties; /* FIXME */
    fapo->matrix_mix_function = NULL; /* FIXME */
    fapo->matrix_coefficients = NULL; /* FIXME */
    fapo->src_format_type = 0; /* FIXME */
    fapo->is_scalar_matrix = 0; /* FIXME: */
    fapo->is_locked = 0;
    fapo->parameter_blocks = parameter_blocks;
    fapo->current_parameters = parameter_blocks;
    fapo->current_parameters_internal = parameter_blocks;
    fapo->current_parameters_index = 0;
    fapo->parameter_block_byte_size = parameter_block_byte_size;
    fapo->newer_results_ready = 0;
    fapo->producer = producer;

    /* Allocator Callbacks */
    fapo->malloc_func = customMalloc;
    fapo->free_func = customFree;
    fapo->realloc_func = customRealloc;

    /* Protected Variables */
    fapo->reference_count = 1;
}

int32_t forge_apo_base_retain(ForgeApoBase *fapo)
{
    fapo->reference_count += 1;
    return fapo->reference_count;
}

int32_t forge_apo_base_release(ForgeApoBase *fapo)
{
    fapo->reference_count -= 1;
    if (fapo->reference_count == 0)
    {
        fapo->Destructor(fapo);
        return 0;
    }
    return fapo->reference_count;
}

ForgeResult forge_apo_base_get_properties(
    ForgeApoBase *fapo,
    ForgeApoProperties **registration_properties
) {
    *registration_properties = (ForgeApoProperties*) fapo->malloc_func(
        sizeof(ForgeApoProperties)
    );
    ForgeAudio_memcpy(
        *registration_properties,
        fapo->registration_properties,
        sizeof(ForgeApoProperties)
    );
    return 0;
}

ForgeResult forge_apo_base_is_input_format_supported(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *output_format,
    const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format
) {
    if (    requested_input_format->format_tag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        requested_input_format->channels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_input_format->channels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_input_format->sample_rate < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_input_format->sample_rate > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_input_format->bits_per_sample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (supported_input_format != NULL)
        {
            (*supported_input_format)->format_tag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            (*supported_input_format)->channels = ForgeAudio_clamp(
                requested_input_format->channels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            (*supported_input_format)->sample_rate = ForgeAudio_clamp(
                requested_input_format->sample_rate,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            (*supported_input_format)->bits_per_sample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_apo_base_is_output_format_supported(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *input_format,
    const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format
) {
    if (    requested_output_format->format_tag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        requested_output_format->channels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_output_format->channels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_output_format->sample_rate < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_output_format->sample_rate > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_output_format->bits_per_sample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (supported_output_format != NULL)
        {
            (*supported_output_format)->format_tag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            (*supported_output_format)->channels = ForgeAudio_clamp(
                requested_output_format->channels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            (*supported_output_format)->sample_rate = ForgeAudio_clamp(
                requested_output_format->sample_rate,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            (*supported_output_format)->bits_per_sample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_apo_base_initialize(
    ForgeApoBase *fapo,
    const void* data,
    uint32_t DataByteSize
) {
    return 0;
}

void forge_apo_base_reset(ForgeApoBase *fapo)
{
}

ForgeResult forge_apo_base_lock_for_process(
    ForgeApoBase *fapo,
    uint32_t InputLockedParameterCount,
    const ForgeApoLockBuffer *input_locked_parameters,
    uint32_t OutputLockedParameterCount,
    const ForgeApoLockBuffer *output_locked_parameters
) {
    /* Verify parameter counts... */
    if (    InputLockedParameterCount < fapo->registration_properties->MinInputBufferCount ||
        InputLockedParameterCount > fapo->registration_properties->MaxInputBufferCount ||
        OutputLockedParameterCount < fapo->registration_properties->MinOutputBufferCount ||
        OutputLockedParameterCount > fapo->registration_properties->MaxOutputBufferCount    )
    {
        return ForgeResultInvalidArgument;
    }


    /* Validate input/output formats */
    #define VERIFY_FORMAT_FLAG(flag, prop) \
        if (    (fapo->registration_properties->Flags & flag) && \
            (input_locked_parameters->format->prop != output_locked_parameters->format->prop)    ) \
        { \
            return ForgeResultInvalidArgument; \
        }
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_CHANNELS_MUST_MATCH, channels)
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH, sample_rate)
    VERIFY_FORMAT_FLAG(FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH, bits_per_sample)
    #undef VERIFY_FORMAT_FLAG
    if (    (fapo->registration_properties->Flags & FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH) &&
        (InputLockedParameterCount != OutputLockedParameterCount)    )
    {
        return ForgeResultInvalidArgument;
    }
    fapo->is_locked = 1;
    return 0;
}

void forge_apo_base_unlock_for_process(ForgeApoBase *fapo)
{
    fapo->is_locked = 0;
}

uint32_t forge_apo_base_calc_input_frames(ForgeApoBase *fapo, uint32_t OutputFrameCount)
{
    return OutputFrameCount;
}

uint32_t forge_apo_base_calc_output_frames(ForgeApoBase *fapo, uint32_t InputFrameCount)
{
    return InputFrameCount;
}

ForgeResult forge_apo_base_validate_default_format(
    ForgeApoBase *fapo,
    ForgeAudioFormat *format,
    uint8_t overwrite
) {
    if (    format->format_tag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        format->channels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        format->channels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        format->sample_rate < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        format->sample_rate > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        format->bits_per_sample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (overwrite)
        {
            format->format_tag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            format->channels = ForgeAudio_clamp(
                format->channels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            format->sample_rate = ForgeAudio_clamp(
                format->sample_rate,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            format->bits_per_sample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

ForgeResult forge_apo_base_validate_format_pair(
    ForgeApoBase *fapo,
    const ForgeAudioFormat *supported_format,
    ForgeAudioFormat *requested_format,
    uint8_t overwrite
) {
    if (    requested_format->format_tag != FORGE_APO_BASE_DEFAULT_FORMAT_TAG ||
        requested_format->channels < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS ||
        requested_format->channels > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS ||
        requested_format->sample_rate < FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE ||
        requested_format->sample_rate > FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE ||
        requested_format->bits_per_sample != FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE    )
    {
        if (overwrite)
        {
            requested_format->format_tag =
                FORGE_APO_BASE_DEFAULT_FORMAT_TAG;
            requested_format->channels = ForgeAudio_clamp(
                requested_format->channels,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_CHANNELS,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_CHANNELS
            );
            requested_format->sample_rate = ForgeAudio_clamp(
                requested_format->sample_rate,
                FORGE_APO_BASE_DEFAULT_FORMAT_MIN_SAMPLE_RATE,
                FORGE_APO_BASE_DEFAULT_FORMAT_MAX_SAMPLE_RATE
            );
            requested_format->bits_per_sample =
                FORGE_APO_BASE_DEFAULT_FORMAT_BITS_PER_SAMPLE;
        }
        return ForgeResultApoFormatUnsupported;
    }
    return 0;
}

void forge_apo_base_process_through(
    ForgeApoBase *fapo,
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

void forge_apo_base_set_parameters(
    ForgeApoBase *fapo,
    const void* parameters,
    uint32_t ParameterByteSize
) {
    ForgeAudio_assert(!fapo->producer);

    /* User callback for validation */
    fapo->OnSetParameters(
        fapo,
        parameters,
        ParameterByteSize
    );

    /* Increment parameter block index... */
    fapo->current_parameters_index += 1;
    if (fapo->current_parameters_index == 3)
    {
        fapo->current_parameters_index = 0;
    }
    fapo->current_parameters_internal = fapo->parameter_blocks + (
        fapo->parameter_block_byte_size *
        fapo->current_parameters_index
    );

    /* Copy to what will eventually be the next parameter update */
    ForgeAudio_memcpy(
        fapo->current_parameters_internal,
        parameters,
        ParameterByteSize
    );
}

void forge_apo_base_get_parameters(
    ForgeApoBase *fapo,
    void* parameters,
    uint32_t ParameterByteSize
) {
    /* Copy what's current as of the last Process */
    ForgeAudio_memcpy(
        parameters,
        fapo->current_parameters,
        ParameterByteSize
    );
}

void forge_apo_base_on_set_parameters(
    ForgeApoBase *fapo,
    const void* parameters,
    uint32_t parametersSize
) {
}

uint8_t forge_apo_base_parameters_changed(ForgeApoBase *fapo)
{
    /* Internal will get updated when SetParameters is called */
    return fapo->current_parameters_internal != fapo->current_parameters;
}

uint8_t* forge_apo_base_begin_process(ForgeApoBase *fapo)
{
    /* Set the latest block as "current", this is what Process will use now */
    fapo->current_parameters = fapo->current_parameters_internal;
    return fapo->current_parameters;
}

void forge_apo_base_end_process(ForgeApoBase *fapo)
{
    /* I'm 100% sure my parameter block increment is wrong... */
}
