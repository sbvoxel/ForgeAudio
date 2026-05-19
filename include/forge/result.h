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

#ifndef FORGE_RESULT_H
#define FORGE_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum ForgeResult {
    ForgeResultSuccess = 0,
    ForgeResultFormatSuggested = 1,
    ForgeResultFailed = -2147467259,
    ForgeResultOutOfMemory = -2147024882,
    ForgeResultInvalidArgument = -2147024809,
    ForgeResultUnsupportedFormat = -2004287480,
    ForgeResultInvalidCall = -2003435519,
    ForgeResultDeviceInvalidated = -2003435516,
    ForgeResultEffectFormatUnsupported = -2003369983
} ForgeResult;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_RESULT_H */
