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
    /* .FriendlyName = */
    {
        'F', 'X', 'R', 'e', 'v', 'e', 'r', 'b', '\0'
    },
    /*.CopyrightInfo = */
    {
        'C', 'o', 'p', 'y', 'r', 'i', 'g', 'h', 't', ' ', '(', 'c', ')',
        'E', 't', 'h', 'a', 'n', ' ', 'L', 'e', 'e', '\0'
    },
    /*.MajorVersion = */ 0,
    /*.MinorVersion = */ 0,
    /*.Flags = */(
        FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH |
        FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH |
        FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH |
        FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED |
        FORGE_EFFECT_FLAG_IN_PLACE_REQUIRED
    ),
    /*.MinInputBufferCount = */ 1,
    /*.MaxInputBufferCount = */  1,
    /*.MinOutputBufferCount = */ 1,
    /*.MaxOutputBufferCount =*/ 1
};

typedef struct ForgeEffectReverb
{
    ForgeEffectBase base;

    /* TODO */
} ForgeEffectReverb;

ForgeResult ForgeEffectReverb_Initialize(
    ForgeEffectReverb *effect,
    const void* data,
    uint32_t DataByteSize
) {
    #define INITPARAMS(offset) \
        ForgeAudio_memcpy( \
            effect->base.parameter_blocks + DataByteSize * offset, \
            data, \
            DataByteSize \
        );
    INITPARAMS(0)
    INITPARAMS(1)
    INITPARAMS(2)
    #undef INITPARAMS
    return 0;
}

void ForgeEffectReverb_Process(
    ForgeEffectReverb *effect,
    uint32_t InputProcessParameterCount,
    const ForgeEffectProcessBuffer* input_process_parameters,
    uint32_t OutputProcessParameterCount,
    ForgeEffectProcessBuffer* output_process_parameters,
    int32_t IsEnabled
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
    uint32_t InitDataByteSize,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    const ForgeEffectReverbParameters fxdefault =
    {
        FORGE_EFFECT_REVERB_DEFAULT_DIFFUSION,
        FORGE_EFFECT_REVERB_DEFAULT_ROOM_SIZE,
    };

    /* Allocate... */
    ForgeEffectReverb *result = (ForgeEffectReverb*) customMalloc(
        sizeof(ForgeEffectReverb)
    );
    uint8_t *params = (uint8_t*) customMalloc(
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
        ForgeAudio_assert(InitDataByteSize == sizeof(ForgeEffectReverbParameters));
        ForgeAudio_memcpy(params, init_data, InitDataByteSize);
        ForgeAudio_memcpy(params + InitDataByteSize, init_data, InitDataByteSize);
        ForgeAudio_memcpy(params + (InitDataByteSize * 2), init_data, InitDataByteSize);
    }

    /* Initialize... */
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
        customMalloc,
        customFree,
        customRealloc
    );

    /* Function table... */
    result->base.base.Initialize = (ForgeEffectInitializeFunc)
        ForgeEffectReverb_Initialize;
    result->base.base.Process = (ForgeEffectProcessFunc)
        ForgeEffectReverb_Process;
    result->base.Destructor = ForgeEffectReverb_Free;

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}
