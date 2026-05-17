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

#include "forge_apo_fx.h"
#include "forge_audio_internal.h"

/* FXEQ ForgeApo Implementation */

const ForgeGuid FORGE_APO_FX_ID_EQ =
{
    0xF5E01117,
    0xD6C4,
    0x485A,
    {
        0xA3,
        0xF5,
        0x69,
        0x51,
        0x96,
        0xF3,
        0xDB,
        0xFA
    }
};

static ForgeApoProperties FXEQProperties =
{
    /* .clsid = */ {0},
    /* .FriendlyName = */
    {
        'F', 'X', 'E', 'Q', '\0'
    },
    /*.CopyrightInfo = */
    {
        'C', 'o', 'p', 'y', 'r', 'i', 'g', 'h', 't', ' ', '(', 'c', ')',
        'E', 't', 'h', 'a', 'n', ' ', 'L', 'e', 'e', '\0'
    },
    /*.MajorVersion = */ 0,
    /*.MinorVersion = */ 0,
    /*.Flags = */(
        FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH |
        FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH |
        FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH |
        FORGE_APO_FLAG_IN_PLACE_SUPPORTED |
        FORGE_APO_FLAG_IN_PLACE_REQUIRED
    ),
    /*.MinInputBufferCount = */ 1,
    /*.MaxInputBufferCount = */  1,
    /*.MinOutputBufferCount = */ 1,
    /*.MaxOutputBufferCount =*/ 1
};

typedef struct ForgeApoEq
{
    ForgeApoBase base;

    /* TODO */
} ForgeApoEq;

ForgeResult FORGE_APO_EQ_Initialize(
    ForgeApoEq *fapo,
    const void* data,
    uint32_t DataByteSize
) {
    #define INITPARAMS(offset) \
        ForgeAudio_memcpy( \
            fapo->base.parameter_blocks + DataByteSize * offset, \
            data, \
            DataByteSize \
        );
    INITPARAMS(0)
    INITPARAMS(1)
    INITPARAMS(2)
    #undef INITPARAMS
    return 0;
}

void FORGE_APO_EQ_Process(
    ForgeApoEq *fapo,
    uint32_t InputProcessParameterCount,
    const ForgeApoProcessBuffer* input_process_parameters,
    uint32_t OutputProcessParameterCount,
    ForgeApoProcessBuffer* output_process_parameters,
    int32_t IsEnabled
) {
    forge_apo_base_begin_process(&fapo->base);

    /* TODO */

    forge_apo_base_end_process(&fapo->base);
}

void FORGE_APO_EQ_Free(void* fapo)
{
    ForgeApoEq *eq = (ForgeApoEq*) fapo;
    eq->base.free_func(eq->base.parameter_blocks);
    eq->base.free_func(fapo);
}

/* Public API */

ForgeResult forge_apo_create_eq(
    ForgeApo **effect,
    const void *init_data,
    uint32_t InitDataByteSize,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
    const ForgeApoEqParameters fxdefault =
    {
        FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_0,
        FORGE_APO_EQ_DEFAULT_GAIN,
        FORGE_APO_EQ_DEFAULT_BANDWIDTH,
        FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_1,
        FORGE_APO_EQ_DEFAULT_GAIN,
        FORGE_APO_EQ_DEFAULT_BANDWIDTH,
        FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_2,
        FORGE_APO_EQ_DEFAULT_GAIN,
        FORGE_APO_EQ_DEFAULT_BANDWIDTH,
        FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_3,
        FORGE_APO_EQ_DEFAULT_GAIN,
        FORGE_APO_EQ_DEFAULT_BANDWIDTH
    };

    /* Allocate... */
    ForgeApoEq *result = (ForgeApoEq*) customMalloc(
        sizeof(ForgeApoEq)
    );
    uint8_t *params = (uint8_t*) customMalloc(
        sizeof(ForgeApoEqParameters) * 3
    );
    if (init_data == NULL)
    {
        ForgeAudio_zero(params, sizeof(ForgeApoEqParameters) * 3);
        #define INITPARAMS(offset) \
            ForgeAudio_memcpy( \
                params + sizeof(ForgeApoEqParameters) * offset, \
                &fxdefault, \
                sizeof(ForgeApoEqParameters) \
            );
        INITPARAMS(0)
        INITPARAMS(1)
        INITPARAMS(2)
        #undef INITPARAMS
    }
    else
    {
        ForgeAudio_assert(InitDataByteSize == sizeof(ForgeApoEqParameters));
        ForgeAudio_memcpy(params, init_data, InitDataByteSize);
        ForgeAudio_memcpy(params + InitDataByteSize, init_data, InitDataByteSize);
        ForgeAudio_memcpy(params + (InitDataByteSize * 2), init_data, InitDataByteSize);
    }

    /* Initialize... */
    ForgeAudio_memcpy(
        &FXEQProperties.clsid,
        &FORGE_APO_FX_ID_EQ,
        sizeof(ForgeGuid)
    );
    forge_apo_base_init_with_allocator(
        &result->base,
        &FXEQProperties,
        params,
        sizeof(ForgeApoEqParameters),
        0,
        customMalloc,
        customFree,
        customRealloc
    );

    /* Function table... */
    result->base.base.Initialize = (ForgeApoInitializeFunc)
        FORGE_APO_EQ_Initialize;
    result->base.base.Process = (ForgeApoProcessFunc)
        FORGE_APO_EQ_Process;
    result->base.Destructor = FORGE_APO_EQ_Free;

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}
