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

#ifndef FORGE_DEBUG_INTERNAL_H
#define FORGE_DEBUG_INTERNAL_H

#include "common_internal.h"

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION

#if defined(_MSC_VER)
/* VC doesn't support __attribute__ at all, and there's no replacement for format. */
FORGE_INTERNAL_API void forge_audio_debug(ForgeAudioEngine *audio, const char *file, uint32_t line, const char *func,
                                          const char *fmt, ...);
#else
FORGE_INTERNAL_API void forge_audio_debug(ForgeAudioEngine *audio, const char *file, uint32_t line, const char *func,
                                          const char *fmt, ...) __attribute__((format(printf, 5, 6)));
#endif

FORGE_INTERNAL_API void forge_audio_debug_fmt(ForgeAudioEngine *audio, const char *file, uint32_t line,
                                              const char *func, const ForgeAudioFormat *fmt);

#define PRINT_DEBUG(engine, cond, type, fmt, ...)                                                                      \
    if (engine->debug.trace_mask & FORGE_AUDIO_LOG_##cond) {                                                           \
        forge_audio_debug(engine, __FILE__, __LINE__, __func__, type ": " fmt, __VA_ARGS__);                           \
    }

#define LOG_ERROR(engine, fmt, ...) PRINT_DEBUG(engine, ERRORS, "ERROR", fmt, __VA_ARGS__)
#define LOG_WARNING(engine, fmt, ...) PRINT_DEBUG(engine, WARNINGS, "WARNING", fmt, __VA_ARGS__)
#define LOG_INFO(engine, fmt, ...) PRINT_DEBUG(engine, INFO, "INFO", fmt, __VA_ARGS__)
#define LOG_DETAIL(engine, fmt, ...) PRINT_DEBUG(engine, DETAIL, "DETAIL", fmt, __VA_ARGS__)
#define LOG_API_ENTER(engine) PRINT_DEBUG(engine, API_CALLS, "API Enter", "%s", __func__)
#define LOG_API_EXIT(engine) PRINT_DEBUG(engine, API_CALLS, "API Exit", "%s", __func__)
#define LOG_FUNC_ENTER(engine) PRINT_DEBUG(engine, FUNC_CALLS, "FUNC Enter", "%s", __func__)
#define LOG_FUNC_EXIT(engine) PRINT_DEBUG(engine, FUNC_CALLS, "FUNC Exit", "%s", __func__)
#define LOG_MUTEX_CREATE(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex Create", "%p (%s)", mutex, #mutex)
#define LOG_MUTEX_DESTROY(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex destroy", "%p (%s)", mutex, #mutex)
#define LOG_MUTEX_LOCK(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex Lock", "%p (%s)", mutex, #mutex)
#define LOG_MUTEX_UNLOCK(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex Unlock", "%p (%s)", mutex, #mutex)
#define LOG_FORMAT(engine, wave_format)                                                                                \
    if (engine->debug.trace_mask & FORGE_AUDIO_LOG_INFO) {                                                             \
        forge_audio_debug_fmt(engine, __FILE__, __LINE__, __func__, wave_format);                                      \
    }

#else

#define LOG_ERROR(engine, fmt, ...)
#define LOG_WARNING(engine, fmt, ...)
#define LOG_INFO(engine, fmt, ...)
#define LOG_DETAIL(engine, fmt, ...)
#define LOG_API_ENTER(engine)
#define LOG_API_EXIT(engine)
#define LOG_FUNC_ENTER(engine)
#define LOG_FUNC_EXIT(engine)
#define LOG_MUTEX_CREATE(engine, mutex)
#define LOG_MUTEX_DESTROY(engine, mutex)
#define LOG_MUTEX_LOCK(engine, mutex)
#define LOG_MUTEX_UNLOCK(engine, mutex)
#define LOG_FORMAT(engine, wave_format)

#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */

#endif /* FORGE_DEBUG_INTERNAL_H */
