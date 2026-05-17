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

/* FXReverb ForgeEffect Implementation */

const ForgeGuid FORGE_EFFECT_FX_ID_REVERB =
{
    0x7D9ACA56,
    0xCB68,
    0x4807,
    {
        0xB6,
        0x32,
        0xB1,
        0x37,
        0x35,
        0x2E,
        0x85,
        0x96
    }
};

static ForgeEffectProperties FXReverbProperties =
{
    /* .clsid = */ {0},
    /* .friendly_name = */
    {
        'F', 'X', 'R', 'e', 'v', 'e', 'r', 'b', '\0'
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

typedef struct ForgeEffectReverb
{
    ForgeEffectBase base;

    /* TODO */
} ForgeEffectReverb;

ForgeResult ForgeEffectReverb_Initialize(
    ForgeEffectReverb *effect,
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

void ForgeEffectReverb_Process(
    ForgeEffectReverb *effect,
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

void ForgeEffectReverb_Free(void* effect)
{
    ForgeEffectReverb *reverb = (ForgeEffectReverb*) effect;
    reverb->base.free_func(reverb->base.parameter_blocks);
    reverb->base.free_func(effect);
}

/* Public API */

ForgeResult forge_effect_create_reverb(
    ForgeEffect **effect,
    const void *init_data,
    uint32_t init_data_byte_size,
    ForgeMallocFunc custom_malloc,
    ForgeFreeFunc custom_free,
    ForgeReallocFunc custom_realloc
) {
    const ForgeEffectReverbParameters fxdefault =
    {
        FORGE_EFFECT_REVERB_DEFAULT_DIFFUSION,
        FORGE_EFFECT_REVERB_DEFAULT_ROOM_SIZE,
    };

    /* Allocate... */
    ForgeEffectReverb *result = (ForgeEffectReverb*) custom_malloc(
        sizeof(ForgeEffectReverb)
    );
    uint8_t *params = (uint8_t*) custom_malloc(
        sizeof(ForgeEffectReverbParameters) * 3
    );
    if (init_data == NULL)
    {
        ForgeAudio_zero(params, sizeof(ForgeEffectReverbParameters) * 3);
        #define INITPARAMS(offset) \
            ForgeAudio_memcpy( \
                params + sizeof(ForgeEffectReverbParameters) * offset, \
                &fxdefault, \
                sizeof(ForgeEffectReverbParameters) \
            );
        INITPARAMS(0)
        INITPARAMS(1)
        INITPARAMS(2)
        #undef INITPARAMS
    }
    else
    {
        ForgeAudio_assert(init_data_byte_size == sizeof(ForgeEffectReverbParameters));
        ForgeAudio_memcpy(params, init_data, init_data_byte_size);
        ForgeAudio_memcpy(params + init_data_byte_size, init_data, init_data_byte_size);
        ForgeAudio_memcpy(params + (init_data_byte_size * 2), init_data, init_data_byte_size);
    }

    /* initialize... */
    ForgeAudio_memcpy(
        &FXReverbProperties.clsid,
        &FORGE_EFFECT_FX_ID_REVERB,
        sizeof(ForgeGuid)
    );
    forge_effect_base_init_with_allocator(
        &result->base,
        &FXReverbProperties,
        params,
        sizeof(ForgeEffectReverbParameters),
        0,
        custom_malloc,
        custom_free,
        custom_realloc
    );

    /* Function table... */
    result->base.base.initialize = (ForgeEffectInitializeFunc)
        ForgeEffectReverb_Initialize;
    result->base.base.process = (ForgeEffectProcessFunc)
        ForgeEffectReverb_Process;
    result->base.destructor = ForgeEffectReverb_Free;

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}
