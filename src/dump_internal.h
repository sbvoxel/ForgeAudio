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

#ifndef FORGE_DUMP_INTERNAL_H
#define FORGE_DUMP_INTERNAL_H

#ifdef FORGE_AUDIO_DUMP_VOICES

#include "common_internal.h"

typedef size_t(FORGE_AUDIO_CALL *ForgeAudioWriteFunc)(void *data, const void *src, size_t size, size_t count);
typedef size_t(FORGE_AUDIO_CALL *ForgeAudioSizeFunc)(void *data);
typedef struct ForgeAudioIOStreamOut {
    void *data;
    ForgeReadFunc read;
    ForgeAudioWriteFunc write;
    ForgeSeekFunc seek;
    ForgeAudioSizeFunc size;
    ForgeCloseFunc close;
    void *lock;
} ForgeAudioIOStreamOut;

FORGE_INTERNAL_API ForgeAudioIOStreamOut *fa_dump_fopen_out(const char *path, const char *mode);
FORGE_INTERNAL_API void fa_dump_close_out(ForgeAudioIOStreamOut *io);

#endif /* FORGE_AUDIO_DUMP_VOICES */

#endif /* FORGE_DUMP_INTERNAL_H */
