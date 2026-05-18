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

#ifndef FORGE_COMMON_INTERNAL_H
#define FORGE_COMMON_INTERNAL_H

#include <forge/audio.h>
#include <stdalign.h>
#include <stdarg.h>

#ifdef FORGE_AUDIO_WIN32_PLATFORM
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <assert.h>
#include <inttypes.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define forge_malloc malloc
#define forge_realloc realloc
#define forge_free free
#define forge_alloca(x) alloca(x)
#define forge_dealloca(x) (void)(x)
#define forge_zero(ptr, size) memset(ptr, '\0', size)
#define forge_memset(ptr, val, size) memset(ptr, val, size)
#define forge_memcpy(dst, src, size) memcpy(dst, src, size)
#define forge_memmove(dst, src, size) memmove(dst, src, size)
#define forge_memcmp(ptr1, ptr2, size) memcmp(ptr1, ptr2, size)

#define forge_strlen(ptr) strlen(ptr)
#define forge_strcmp(str1, str2) strcmp(str1, str2)
#define forge_strncmp(str1, str2, size) strncmp(str1, str2, size)
#define forge_strlcpy(ptr1, ptr2, size) lstrcpynA(ptr1, ptr2, size)

#define forge_pow(x, y) pow(x, y)
#define forge_powf(x, y) powf(x, y)
#define forge_log(x) log(x)
#define forge_log10(x) log10(x)
#define forge_sin(x) sin(x)
#define forge_cos(x) cos(x)
#define forge_tan(x) tan(x)
#define forge_acos(x) acos(x)
#define forge_ceil(x) ceil(x)
#define forge_floor(x) floor(x)
#define forge_abs(x) abs(x)
#define forge_ldexp(v, e) ldexp(v, e)
#define forge_exp(x) exp(x)

#define forge_cosf(x) cosf(x)
#define forge_sinf(x) sinf(x)
#define forge_sqrtf(x) sqrtf(x)
#define forge_acosf(x) acosf(x)
#define forge_atan2f(y, x) atan2f(y, x)
#define forge_fabsf(x) fabsf(x)

#define forge_qsort qsort

#define forge_assert assert
#define forge_snprintf snprintf
#define forge_vsnprintf vsnprintf
#define forge_getenv getenv
#define FORGE_PRIu64 PRIu64
#define FORGE_PRIx64 PRIx64

extern void fa_platform_log_message(char const *msg);

/* Native Win32 backend currently supports little-endian targets. */
#define forge_swap16le(x) (x)
#define forge_swap16be(x) ((x >> 8) & 0x00FF) | ((x << 8) & 0xFF00)
#define forge_swap32le(x) (x)
#define forge_swap32be(x)                                                                                              \
    ((x >> 24) & 0x000000FF) | ((x >> 8) & 0x0000FF00) | ((x << 8) & 0x00FF0000) | ((x << 24) & 0xFF000000)
#define forge_swap64le(x) (x)
#define forge_swap64be(x)                                                                                              \
    ((x >> 32) & 0x00000000000000FF) | ((x >> 24) & 0x000000000000FF00) | ((x >> 16) & 0x0000000000FF0000) |           \
        ((x >> 8) & 0x00000000FF000000) | ((x << 8) & 0x000000FF00000000) | ((x << 16) & 0x0000FF0000000000) |         \
        ((x << 24) & 0x00FF000000000000) | ((x << 32) & 0xFF00000000000000)
#else
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_log.h>

#define forge_swap16le(x) SDL_Swap16LE(x)
#define forge_swap16be(x) SDL_Swap16BE(x)
#define forge_swap32le(x) SDL_Swap32LE(x)
#define forge_swap32be(x) SDL_Swap32BE(x)
#define forge_swap64le(x) SDL_Swap64LE(x)
#define forge_swap64be(x) SDL_Swap64BE(x)

#define forge_malloc SDL_malloc
#define forge_realloc SDL_realloc
#define forge_free SDL_free
#define forge_alloca(x) SDL_stack_alloc(uint8_t, x)
#define forge_dealloca(x) SDL_stack_free(x)
#define forge_zero(ptr, size) SDL_memset(ptr, '\0', size)
#define forge_memset(ptr, val, size) SDL_memset(ptr, val, size)
#define forge_memcpy(dst, src, size) SDL_memcpy(dst, src, size)
#define forge_memmove(dst, src, size) SDL_memmove(dst, src, size)
#define forge_memcmp(ptr1, ptr2, size) SDL_memcmp(ptr1, ptr2, size)

#define forge_strlen(ptr) SDL_strlen(ptr)
#define forge_strcmp(str1, str2) SDL_strcmp(str1, str2)
#define forge_strncmp(str1, str2, size) SDL_strncmp(str1, str2, size)
#define forge_strlcpy(ptr1, ptr2, size) SDL_strlcpy(ptr1, ptr2, size)

#define forge_pow(x, y) SDL_pow(x, y)
#define forge_powf(x, y) SDL_powf(x, y)
#define forge_log(x) SDL_log(x)
#define forge_log10(x) SDL_log10(x)
#define forge_sin(x) SDL_sin(x)
#define forge_cos(x) SDL_cos(x)
#define forge_tan(x) SDL_tan(x)
#define forge_acos(x) SDL_acos(x)
#define forge_ceil(x) SDL_ceil(x)
#define forge_floor(x) SDL_floor(x)
#define forge_abs(x) SDL_abs(x)
#define forge_ldexp(v, e) SDL_scalbn(v, e)
#define forge_exp(x) SDL_exp(x)

#define forge_cosf(x) SDL_cosf(x)
#define forge_sinf(x) SDL_sinf(x)
#define forge_sqrtf(x) SDL_sqrtf(x)
#define forge_acosf(x) SDL_acosf(x)
#define forge_atan2f(y, x) SDL_atan2f(y, x)
#define forge_fabsf(x) SDL_fabsf(x)

#define forge_qsort SDL_qsort

#ifdef FORGE_AUDIO_LOG_ASSERTIONS
#define forge_assert(condition)                                                                                        \
    {                                                                                                                  \
        static uint8_t logged = 0;                                                                                     \
        if (!(condition) && !logged) {                                                                                 \
            SDL_Log("Assertion failed: %s", #condition);                                                               \
            logged = 1;                                                                                                \
        }                                                                                                              \
    }
#else
#define forge_assert SDL_assert
#endif
#define forge_snprintf SDL_snprintf
#define forge_vsnprintf SDL_vsnprintf
#define fa_platform_log_message(msg) SDL_Log("%s", msg)
#define forge_getenv SDL_getenv
#define FORGE_PRIu64 SDL_PRIu64
#define FORGE_PRIx64 SDL_PRIx64
#endif

#define forge_min(val1, val2) (val1 < val2 ? val1 : val2)
#define forge_max(val1, val2) (val1 > val2 ? val1 : val2)
#define forge_clamp(val, min, max) (val > max ? max : (val < min ? min : val))

#if defined(__GNUC__) || defined(__clang__)
#define FORGE_INTERNAL_API __attribute__((visibility("hidden")))
#else
#define FORGE_INTERNAL_API
#endif

#endif /* FORGE_COMMON_INTERNAL_H */
