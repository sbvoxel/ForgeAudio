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

#include "core_internal.h"

bool fa_array_reserve(ForgeAudioEngine *audio, void **elements, size_t *capacity, size_t count, size_t size) {
    size_t new_capacity, max_capacity;
    void *new_elements;

    if (count <= *capacity) {
        return true;
    }

    max_capacity = ~(size_t)0 / size;
    if (count > max_capacity) {
        return false;
    }

    new_capacity = forge_max(4, *capacity);
    while (new_capacity < count && new_capacity <= max_capacity / 2) {
        new_capacity *= 2;
    }
    if (new_capacity < count) {
        new_capacity = max_capacity;
    }

    new_elements = audio->realloc_func(*elements, new_capacity * size);
    if (new_elements == NULL) {
        return false;
    }

    *elements = new_elements;
    *capacity = new_capacity;
    return true;
}

bool fa_linked_list_add_entry(ForgeLinkedList **start, void *toAdd, ForgeAudioMutex lock, ForgeMallocFunc malloc_func) {
    ForgeLinkedList *newEntry, *latest;
    newEntry = (ForgeLinkedList *)malloc_func(sizeof(ForgeLinkedList));
    if (newEntry == NULL) {
        return false;
    }
    newEntry->entry = toAdd;
    newEntry->next = NULL;
    fa_platform_lock_mutex(lock);
    if (*start == NULL) {
        *start = newEntry;
    } else {
        latest = *start;
        while (latest->next != NULL) {
            latest = latest->next;
        }
        latest->next = newEntry;
    }
    fa_platform_unlock_mutex(lock);
    return true;
}

bool fa_linked_list_prepend_entry(ForgeLinkedList **start, void *toAdd, ForgeAudioMutex lock,
                                  ForgeMallocFunc malloc_func) {
    ForgeLinkedList *newEntry;
    newEntry = (ForgeLinkedList *)malloc_func(sizeof(ForgeLinkedList));
    if (newEntry == NULL) {
        return false;
    }
    newEntry->entry = toAdd;
    fa_platform_lock_mutex(lock);
    newEntry->next = *start;
    *start = newEntry;
    fa_platform_unlock_mutex(lock);
    return true;
}

void fa_linked_list_remove_entry(ForgeLinkedList **start, void *toRemove, ForgeAudioMutex lock,
                                 ForgeFreeFunc free_func) {
    ForgeLinkedList *latest, *prev;
    fa_platform_lock_mutex(lock);
    latest = *start;
    prev = latest;
    while (latest != NULL) {
        if (latest->entry == toRemove) {
            if (latest == prev) /* First in list */
            {
                *start = latest->next;
            } else {
                prev->next = latest->next;
            }
            free_func(latest);
            fa_platform_unlock_mutex(lock);
            return;
        }
        prev = latest;
        latest = latest->next;
    }
    fa_platform_unlock_mutex(lock);
    forge_assert(0 && "ForgeLinkedList element not found!");
}
