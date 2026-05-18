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

#ifndef FORGE_PLATFORM_INTERNAL_H
#define FORGE_PLATFORM_INTERNAL_H

#include "common_internal.h"

typedef void *ForgeAudioThread;
typedef void *ForgeAudioMutex;
typedef int32_t(FORGE_AUDIO_CALL *ForgeAudioThreadFunc)(void *data);
typedef enum ForgeAudioThreadPriority {
    FORGE_AUDIO_THREAD_PRIORITY_LOW,
    FORGE_AUDIO_THREAD_PRIORITY_NORMAL,
    FORGE_AUDIO_THREAD_PRIORITY_HIGH,
} ForgeAudioThreadPriority;

FORGE_INTERNAL_API void fa_platform_add_ref(void);
FORGE_INTERNAL_API void fa_platform_release(void);
/* Requires the caller to already hold the global platform lifetime. */
FORGE_INTERNAL_API void fa_platform_init(ForgeAudioEngine *audio, uint32_t flags, uint32_t device_index,
                                         ForgeAudioFormatExtensible *mix_format, uint32_t *update_size,
                                         void **platform_device);
FORGE_INTERNAL_API void fa_platform_quit(void *platform_device);

FORGE_INTERNAL_API uint32_t fa_platform_get_device_count(void);
FORGE_INTERNAL_API ForgeResult fa_platform_get_device_details(uint32_t index, ForgeDeviceDetails *details);

FORGE_INTERNAL_API ForgeAudioThread fa_platform_create_thread(ForgeAudioThreadFunc func, const char *name, void *data);
FORGE_INTERNAL_API void fa_platform_wait_thread(ForgeAudioThread thread, int32_t *retval);
FORGE_INTERNAL_API void fa_platform_set_thread_priority(ForgeAudioThreadPriority priority);
FORGE_INTERNAL_API uint64_t fa_platform_get_thread_id(void);
FORGE_INTERNAL_API ForgeAudioMutex fa_platform_create_mutex(void);
FORGE_INTERNAL_API void fa_platform_destroy_mutex(ForgeAudioMutex mutex);
FORGE_INTERNAL_API void fa_platform_lock_mutex(ForgeAudioMutex mutex);
FORGE_INTERNAL_API void fa_platform_unlock_mutex(ForgeAudioMutex mutex);
FORGE_INTERNAL_API void fa_platform_sleep(uint32_t ms);

FORGE_INTERNAL_API uint32_t fa_platform_time_ms(void);

#endif /* FORGE_PLATFORM_INTERNAL_H */
