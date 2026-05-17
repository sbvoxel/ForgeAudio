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

#ifndef FORGE_EFFECT_H
#define FORGE_EFFECT_H

#include "forge_audio.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef FORGE_EFFECT_DECL
    #define FORGE_EFFECT_DECL
typedef struct ForgeEffect ForgeEffect;
#endif /* FORGE_EFFECT_DECL */

FORGE_AUDIO_API void forge_effect_destroy(ForgeEffect *effect);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_EFFECT_H */
