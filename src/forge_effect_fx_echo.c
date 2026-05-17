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

#include "forge_effect_fx.h"
#include "forge_audio_internal.h"

/* FXEcho ForgeEffect Implementation */

const ForgeGuid FORGE_EFFECT_FX_ID_ECHO =
{
    0x5039D740,
    0xF736,
    0x449A,
    {
        0x84,
        0xD3,
        0xA5,
        0x62,
        0x02,
        0x55,
        0x7B,
        0x87
    }
};

static ForgeEffectProperties FXEchoProperties =
{
    /* .clsid = */ {0},
    /* .friendly_name = */
    {
        'F', 'X', 'E', 'c', 'h', 'o', '\0'
    },
    /*.copyright_info = */
    {
        'C', 'o', 'p', 'y', 'r', 'i', 'g', 'h', 't', ' ', '(', 'c', ')',
        'E', 't', 'h', 'a', 'n', ' ', 'L', 'e', 'e', '\0'
    },
    /*.major_version = */ 0,
    /*.minor_version = */ 0,
    /*.flags = */(
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

typedef struct ForgeEffectEcho
{
    ForgeEffectBase base;

    /* TODO */
} ForgeEffectEcho;

ForgeResult ForgeEffectEcho_Initialize(
    ForgeEffectEcho *effect,
    const void* data,
    uint32_t data_byte_size
) {
    #define INITPARAMS(offset) \
        ForgeAudio_memcpy( \
            effect->base.parameter_blocks + data_byte_size * offset, \
            data, \
            data_byte_size \
        );
    INITPARAMS(0)
    INITPARAMS(1)
    INITPARAMS(2)
    #undef INITPARAMS
    return 0;
}

void ForgeEffectEcho_Process(
    ForgeEffectEcho *effect,
    uint32_t input_process_parameter_count,
    const ForgeEffectProcessBuffer* input_process_parameters,
    uint32_t output_process_parameter_count,
    ForgeEffectProcessBuffer* output_process_parameters,
    int32_t is_enabled
) {
    forge_effect_base_begin_process(&effect->base);

    /* TODO */

    forge_effect_base_end_process(&effect->base);
}

void ForgeEffectEcho_Free(void* effect)
{
    ForgeEffectEcho *echo = (ForgeEffectEcho*) effect;
    echo->base.free_func(echo->base.parameter_blocks);
    echo->base.free_func(effect);
}

/* Public API */

ForgeResult forge_effect_create_echo(
    ForgeEffect **effect,
    const void *init_data,
    uint32_t init_data_byte_size,
    ForgeMallocFunc custom_malloc,
    ForgeFreeFunc custom_free,
    ForgeReallocFunc custom_realloc
) {
    const ForgeEffectEchoParameters fxdefault =
    {
        FORGE_EFFECT_ECHO_DEFAULT_WET_DRY_MIX,
        FORGE_EFFECT_ECHO_DEFAULT_FEEDBACK,
        FORGE_EFFECT_ECHO_DEFAULT_DELAY
    };

    /* Allocate... */
    ForgeEffectEcho *result = (ForgeEffectEcho*) custom_malloc(
        sizeof(ForgeEffectEcho)
    );
    uint8_t *params = (uint8_t*) custom_malloc(
        sizeof(ForgeEffectEchoParameters) * 3
    );
    if (init_data == NULL)
    {
        ForgeAudio_zero(params, sizeof(ForgeEffectEchoParameters) * 3);
        #define INITPARAMS(offset) \
            ForgeAudio_memcpy( \
                params + sizeof(ForgeEffectEchoParameters) * offset, \
                &fxdefault, \
                sizeof(ForgeEffectEchoParameters) \
            );
        INITPARAMS(0)
        INITPARAMS(1)
        INITPARAMS(2)
        #undef INITPARAMS
    }
    else
    {
        ForgeAudio_assert(init_data_byte_size == sizeof(ForgeEffectEchoParameters));
        ForgeAudio_memcpy(params, init_data, init_data_byte_size);
        ForgeAudio_memcpy(params + init_data_byte_size, init_data, init_data_byte_size);
        ForgeAudio_memcpy(params + (init_data_byte_size * 2), init_data, init_data_byte_size);
    }

    /* initialize... */
    ForgeAudio_memcpy(
        &FXEchoProperties.clsid,
        &FORGE_EFFECT_FX_ID_ECHO,
        sizeof(ForgeGuid)
    );
    forge_effect_base_init_with_allocator(
        &result->base,
        &FXEchoProperties,
        params,
        sizeof(ForgeEffectEchoParameters),
        0,
        custom_malloc,
        custom_free,
        custom_realloc
    );

    /* Function table... */
    result->base.base.initialize = (ForgeEffectInitializeFunc)
        ForgeEffectEcho_Initialize;
    result->base.base.process = (ForgeEffectProcessFunc)
        ForgeEffectEcho_Process;
    result->base.destructor = ForgeEffectEcho_Free;

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}
