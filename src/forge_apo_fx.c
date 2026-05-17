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

uint32_t forge_apo_create_effect(
    const ForgeGuid *clsid,
    ForgeApo **pEffect,
    const void *pInitData,
    uint32_t InitDataByteSize
) {
    return forge_apo_create_effect_with_allocator(
        clsid,
        pEffect,
        pInitData,
        InitDataByteSize,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

uint32_t forge_apo_create_effect_with_allocator(
    const ForgeGuid *clsid,
    ForgeApo **pEffect,
    const void *pInitData,
    uint32_t InitDataByteSize,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
) {
#define CHECK_AND_RETURN(id, create_func) \
    if (ForgeAudio_memcmp(clsid, &(id), sizeof(ForgeGuid)) == 0) \
    { \
        return create_func( \
            pEffect, \
            pInitData, \
            InitDataByteSize, \
            customMalloc, \
            customFree, \
            customRealloc \
        ); \
    }
    CHECK_AND_RETURN(FORGE_APO_FX_ID_EQ, forge_apo_create_eq)
    CHECK_AND_RETURN(
        FORGE_APO_FX_ID_MASTERING_LIMITER,
        forge_apo_create_mastering_limiter
    )
    CHECK_AND_RETURN(FORGE_APO_FX_ID_REVERB, forge_apo_create_reverb)
    CHECK_AND_RETURN(FORGE_APO_FX_ID_ECHO, forge_apo_create_echo)
#undef CHECK_AND_RETURN
    return -1;
}
