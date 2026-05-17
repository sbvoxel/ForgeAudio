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
#include "FAudio_internal.h"

/* FXReverb ForgeApo Implementation */

const ForgeGuid FORGE_APO_FX_ID_REVERB =
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

static ForgeApoProperties FXReverbProperties =
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

const ForgeGuid FORGE_APO_FX_ID_REVERB_LEGACY =
{
    0xA90BC001,
    0xE897,
    0xE897,
    {
        0x74,
        0x39,
        0x43,
        0x55,
        0x00,
        0x00,
        0x00,
        0x02
    }
};

static ForgeApoProperties FXReverbProperties_LEGACY =
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

typedef struct ForgeApoReverb
{
    ForgeApoBase base;

    /* TODO */
} ForgeApoReverb;

uint32_t ForgeApoReverb_Initialize(
    ForgeApoReverb *fapo,
    const void* pData,
    uint32_t DataByteSize
) {
    #define INITPARAMS(offset) \
        FAudio_memcpy( \
            fapo->base.m_pParameterBlocks + DataByteSize * offset, \
            pData, \
            DataByteSize \
        );
    INITPARAMS(0)
    INITPARAMS(1)
    INITPARAMS(2)
    #undef INITPARAMS
    return 0;
}

void ForgeApoReverb_Process(
    ForgeApoReverb *fapo,
    uint32_t InputProcessParameterCount,
    const ForgeApoProcessBuffer* pInputProcessParameters,
    uint32_t OutputProcessParameterCount,
    ForgeApoProcessBuffer* pOutputProcessParameters,
    int32_t IsEnabled
) {
    forge_apo_base_begin_process(&fapo->base);

    /* TODO */

    forge_apo_base_end_process(&fapo->base);
}

void ForgeApoReverb_Free(void* fapo)
{
    ForgeApoReverb *reverb = (ForgeApoReverb*) fapo;
    reverb->base.pFree(reverb->base.m_pParameterBlocks);
    reverb->base.pFree(fapo);
}

/* Public API */

uint32_t forge_apo_create_reverb(
    ForgeApo **pEffect,
    const void *pInitData,
    uint32_t InitDataByteSize,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc,
    uint8_t legacy
) {
    const ForgeApoReverbParameters fxdefault =
    {
        FORGE_APO_REVERB_DEFAULT_DIFFUSION,
        FORGE_APO_REVERB_DEFAULT_ROOM_SIZE,
    };

    /* Allocate... */
    ForgeApoReverb *result = (ForgeApoReverb*) customMalloc(
        sizeof(ForgeApoReverb)
    );
    uint8_t *params = (uint8_t*) customMalloc(
        sizeof(ForgeApoReverbParameters) * 3
    );
    if (pInitData == NULL)
    {
        FAudio_zero(params, sizeof(ForgeApoReverbParameters) * 3);
        #define INITPARAMS(offset) \
            FAudio_memcpy( \
                params + sizeof(ForgeApoReverbParameters) * offset, \
                &fxdefault, \
                sizeof(ForgeApoReverbParameters) \
            );
        INITPARAMS(0)
        INITPARAMS(1)
        INITPARAMS(2)
        #undef INITPARAMS
    }
    else
    {
        FAudio_assert(InitDataByteSize == sizeof(ForgeApoReverbParameters));
        FAudio_memcpy(params, pInitData, InitDataByteSize);
        FAudio_memcpy(params + InitDataByteSize, pInitData, InitDataByteSize);
        FAudio_memcpy(params + (InitDataByteSize * 2), pInitData, InitDataByteSize);
    }

    /* Initialize... */
    FAudio_memcpy(
        &FXReverbProperties_LEGACY.clsid,
        &FORGE_APO_FX_ID_REVERB_LEGACY,
        sizeof(ForgeGuid)
    );
    FAudio_memcpy(
        &FXReverbProperties.clsid,
        &FORGE_APO_FX_ID_REVERB,
        sizeof(ForgeGuid)
    );
    forge_apo_base_init_with_allocator(
        &result->base,
        legacy ? &FXReverbProperties_LEGACY : &FXReverbProperties,
        params,
        sizeof(ForgeApoReverbParameters),
        0,
        customMalloc,
        customFree,
        customRealloc
    );

    /* Function table... */
    result->base.base.Initialize = (ForgeApoInitializeFunc)
        ForgeApoReverb_Initialize;
    result->base.base.Process = (ForgeApoProcessFunc)
        ForgeApoReverb_Process;
    result->base.Destructor = ForgeApoReverb_Free;

    /* Finally. */
    *pEffect = &result->base.base;
    return 0;
}
