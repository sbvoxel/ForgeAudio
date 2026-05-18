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

#ifndef FORGE_UTIL_INTERNAL_H
#define FORGE_UTIL_INTERNAL_H

#include "common_internal.h"
#include "platform_internal.h"

typedef struct ForgeLinkedList ForgeLinkedList;
struct ForgeLinkedList {
    void *entry;
    ForgeLinkedList *next;
};

FORGE_INTERNAL_API void forge_linked_list_add_entry(ForgeLinkedList **start, void *to_add, ForgeAudioMutex lock,
                                                    ForgeMallocFunc malloc_func);
FORGE_INTERNAL_API void forge_linked_list_prepend_entry(ForgeLinkedList **start, void *to_add, ForgeAudioMutex lock,
                                                        ForgeMallocFunc malloc_func);
FORGE_INTERNAL_API void forge_linked_list_remove_entry(ForgeLinkedList **start, void *to_remove, ForgeAudioMutex lock,
                                                       ForgeFreeFunc free_func);

FORGE_INTERNAL_API bool forge_array_reserve(ForgeAudioEngine *audio, void **elements, size_t *capacity, size_t count,
                                            size_t size);

#endif /* FORGE_UTIL_INTERNAL_H */
