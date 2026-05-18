/*
 * ForgeAudio
 * Forked from FAudio.
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 * Portions copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma,
 * and the MonoGame Team.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "format_internal.h"

#define MAKE_FORMAT_ID(name, fmt)                                                                                      \
    const uint8_t fa_format_id_##name[FORGE_AUDIO_FORMAT_ID_SIZE] = {(uint8_t)((fmt) & 0xFF),                          \
                                                                     (uint8_t)(((fmt) >> 8) & 0xFF),                   \
                                                                     0x00,                                             \
                                                                     0x00,                                             \
                                                                     0x00,                                             \
                                                                     0x00,                                             \
                                                                     0x10,                                             \
                                                                     0x00,                                             \
                                                                     0x80,                                             \
                                                                     0x00,                                             \
                                                                     0x00,                                             \
                                                                     0xAA,                                             \
                                                                     0x00,                                             \
                                                                     0x38,                                             \
                                                                     0x9B,                                             \
                                                                     0x71}
MAKE_FORMAT_ID(pcm, 1);
MAKE_FORMAT_ID(ieee_float, 3);
MAKE_FORMAT_ID(xmaudio2, FORGE_AUDIO_FORMAT_XMAUDIO2);
MAKE_FORMAT_ID(wmaudio2, FORGE_AUDIO_FORMAT_WMAUDIO2);
MAKE_FORMAT_ID(wmaudio3, FORGE_AUDIO_FORMAT_WMAUDIO3);
MAKE_FORMAT_ID(wmaudio_lossless, FORGE_AUDIO_FORMAT_WMAUDIO_LOSSLESS);
#undef MAKE_FORMAT_ID
