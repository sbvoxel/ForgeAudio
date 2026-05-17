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

#include "forge_effect_fx.h"
#include "forge_audio_internal.h"

ForgeResult forge_effect_create(
    const ForgeGuid *clsid,
    ForgeEffect **effect,
    const void *init_data,
    uint32_t init_data_byte_size
) {
    return forge_effect_create_with_allocator(
        clsid,
        effect,
        init_data,
        init_data_byte_size,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

ForgeResult forge_effect_create_with_allocator(
    const ForgeGuid *clsid,
    ForgeEffect **effect,
    const void *init_data,
    uint32_t init_data_byte_size,
    ForgeMallocFunc custom_malloc,
    ForgeFreeFunc custom_free,
    ForgeReallocFunc custom_realloc
) {
#define CHECK_AND_RETURN(id, create_func) \
    if (ForgeAudio_memcmp(clsid, &(id), sizeof(ForgeGuid)) == 0) \
    { \
        return create_func( \
            effect, \
            init_data, \
            init_data_byte_size, \
            custom_malloc, \
            custom_free, \
            custom_realloc \
        ); \
    }
    CHECK_AND_RETURN(FORGE_EFFECT_FX_ID_EQ, forge_effect_create_eq)
    CHECK_AND_RETURN(
        FORGE_EFFECT_FX_ID_MASTERING_LIMITER,
        forge_effect_create_mastering_limiter
    )
    CHECK_AND_RETURN(FORGE_EFFECT_FX_ID_REVERB, forge_effect_create_reverb)
    CHECK_AND_RETURN(FORGE_EFFECT_FX_ID_ECHO, forge_effect_create_echo)
#undef CHECK_AND_RETURN
    return ForgeResultFailed;
}
