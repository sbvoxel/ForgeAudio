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

/* Batch command implementation originally written by Tyler Glaiel */

#include "batch_internal.h"

/* Core command types */

typedef enum ForgeAudioCommandType {
    FORGE_AUDIO_COMMAND_ENABLE_EFFECT,
    FORGE_AUDIO_COMMAND_DISABLE_EFFECT,
    FORGE_AUDIO_COMMAND_SET_EFFECT_PARAMETERS,
    FORGE_AUDIO_COMMAND_SET_FILTER_PARAMETERS,
    FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_PARAMETERS,
    FORGE_AUDIO_COMMAND_SET_VOLUME,
    FORGE_AUDIO_COMMAND_RAMP_VOLUME,
    FORGE_AUDIO_COMMAND_SET_CHANNEL_VOLUMES,
    FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES,
    FORGE_AUDIO_COMMAND_SET_OUTPUT_MATRIX,
    FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX,
    FORGE_AUDIO_COMMAND_START,
    FORGE_AUDIO_COMMAND_STOP,
    FORGE_AUDIO_COMMAND_FADE_STOP,
    FORGE_AUDIO_COMMAND_EXIT_LOOP,
    FORGE_AUDIO_COMMAND_SET_FREQUENCY_RATIO
} ForgeAudioCommandType;

struct ForgeAudioCommand {
    ForgeAudioCommandType type;
    ForgeAudioBatchId batch_id;
    ForgeVoice *voice;

    union {
        struct {
            uint32_t effect_index;
        } EnableEffect;
        struct {
            uint32_t effect_index;
        } DisableEffect;
        struct {
            uint32_t effect_index;
            void *parameters;
            uint32_t parameters_byte_size;
        } SetEffectParameters;
        struct {
            ForgeFilterParameters Parameters;
        } SetFilterParameters;
        struct {
            ForgeVoice *destination_voice;
            ForgeFilterParameters Parameters;
        } SetOutputFilterParameters;
        struct {
            float volume;
        } SetVolume;
        struct {
            float volume;
            uint32_t duration_frames;
        } RampVolume;
        struct {
            uint32_t channels;
            float *volumes;
        } SetChannelVolumes;
        struct {
            uint32_t channels;
            float *volumes;
            uint32_t duration_frames;
        } RampChannelVolumes;
        struct {
            ForgeVoice *destination_voice;
            uint32_t source_channels;
            uint32_t destination_channels;
            float *level_matrix;
        } SetOutputMatrix;
        struct {
            ForgeVoice *destination_voice;
            uint32_t source_channels;
            uint32_t destination_channels;
            float *level_matrix;
            uint32_t duration_frames;
        } RampOutputMatrix;
        struct {
            uint32_t flags;
        } Start;
        struct {
            uint32_t flags;
        } Stop;
        struct {
            float volume;
            uint32_t duration_frames;
        } FadeStop;
        /* No special data for ExitLoop
        struct
        {
        } ExitLoop;
        */
        struct {
            float ratio;
        } SetFrequencyRatio;
    } Data;

    ForgeAudioCommand *next;
};

/* Used by both apply and clear routines */

static inline void destroy_command(ForgeAudioCommand *op, ForgeFreeFunc free_func) {
    if (op->type == FORGE_AUDIO_COMMAND_SET_EFFECT_PARAMETERS) {
        free_func(op->Data.SetEffectParameters.parameters);
    } else if (op->type == FORGE_AUDIO_COMMAND_SET_CHANNEL_VOLUMES) {
        free_func(op->Data.SetChannelVolumes.volumes);
    } else if (op->type == FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES) {
        free_func(op->Data.RampChannelVolumes.volumes);
    } else if (op->type == FORGE_AUDIO_COMMAND_SET_OUTPUT_MATRIX) {
        free_func(op->Data.SetOutputMatrix.level_matrix);
    } else if (op->type == FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX) {
        free_func(op->Data.RampOutputMatrix.level_matrix);
    }
    free_func(op);
}

/* Command execution */

static inline void execute_command(ForgeAudioCommand *op) {
    switch (op->type) {
    case FORGE_AUDIO_COMMAND_ENABLE_EFFECT:
        forge_voice_enable_effect(op->voice, op->Data.EnableEffect.effect_index, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_DISABLE_EFFECT:
        forge_voice_disable_effect(op->voice, op->Data.DisableEffect.effect_index, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_EFFECT_PARAMETERS:
        forge_voice_set_effect_parameters(
            op->voice, op->Data.SetEffectParameters.effect_index, op->Data.SetEffectParameters.parameters,
            op->Data.SetEffectParameters.parameters_byte_size, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_FILTER_PARAMETERS:
        forge_voice_set_filter_parameters(op->voice, &op->Data.SetFilterParameters.Parameters,
                                          FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_PARAMETERS:
        forge_voice_set_output_filter_parameters(op->voice, op->Data.SetOutputFilterParameters.destination_voice,
                                                 &op->Data.SetOutputFilterParameters.Parameters,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_VOLUME:
        forge_voice_set_volume(op->voice, op->Data.SetVolume.volume, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_VOLUME:
        forge_voice_ramp_volume(op->voice, op->Data.RampVolume.volume, op->Data.RampVolume.duration_frames,
                                FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_CHANNEL_VOLUMES:
        forge_voice_set_channel_volumes(op->voice, op->Data.SetChannelVolumes.channels,
                                        op->Data.SetChannelVolumes.volumes, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES:
        forge_voice_ramp_channel_volumes(op->voice, op->Data.RampChannelVolumes.channels,
                                         op->Data.RampChannelVolumes.volumes,
                                         op->Data.RampChannelVolumes.duration_frames, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_OUTPUT_MATRIX:
        forge_voice_set_output_matrix(op->voice, op->Data.SetOutputMatrix.destination_voice,
                                      op->Data.SetOutputMatrix.source_channels,
                                      op->Data.SetOutputMatrix.destination_channels,
                                      op->Data.SetOutputMatrix.level_matrix, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX:
        forge_voice_ramp_output_matrix(op->voice, op->Data.RampOutputMatrix.destination_voice,
                                       op->Data.RampOutputMatrix.source_channels,
                                       op->Data.RampOutputMatrix.destination_channels,
                                       op->Data.RampOutputMatrix.level_matrix,
                                       op->Data.RampOutputMatrix.duration_frames, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_START:
        forge_source_voice_start(op->voice, op->Data.Start.flags, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_STOP:
        forge_source_voice_stop(op->voice, op->Data.Stop.flags, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_FADE_STOP:
        forge_source_voice_fade_stop(op->voice, op->Data.FadeStop.volume, op->Data.FadeStop.duration_frames,
                                     FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_EXIT_LOOP:
        forge_source_voice_break_loop(op->voice, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_FREQUENCY_RATIO:
        forge_source_voice_set_rate(op->voice, op->Data.SetFrequencyRatio.ratio, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    default:
        forge_assert(0 && "Unrecognized operation type!");
        break;
    }
}

void fa_batch_apply_all(ForgeAudioEngine *audio) {
    ForgeAudioCommand *op, *next, **ready_end;

    fa_platform_lock_mutex(audio->batchLock);
    LOG_MUTEX_LOCK(audio, audio->batchLock)

    if (audio->pending_commands == NULL) {
        fa_platform_unlock_mutex(audio->batchLock);
        LOG_MUTEX_UNLOCK(audio, audio->batchLock)
        return;
    }

    ready_end = &audio->ready_commands;
    while (*ready_end) {
        ready_end = &((*ready_end)->next);
    }

    op = audio->pending_commands;
    do {
        next = op->next;

        *ready_end = op;
        op->next = NULL;
        ready_end = &op->next;

        op = next;
    } while (op != NULL);
    audio->pending_commands = NULL;

    fa_platform_unlock_mutex(audio->batchLock);
    LOG_MUTEX_UNLOCK(audio, audio->batchLock)
}

void fa_batch_apply(ForgeAudioEngine *audio, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op, *next, *prev, **ready_end;

    fa_platform_lock_mutex(audio->batchLock);
    LOG_MUTEX_LOCK(audio, audio->batchLock)

    if (audio->pending_commands == NULL) {
        fa_platform_unlock_mutex(audio->batchLock);
        LOG_MUTEX_UNLOCK(audio, audio->batchLock)
        return;
    }

    ready_end = &audio->ready_commands;
    while (*ready_end) {
        ready_end = &((*ready_end)->next);
    }

    op = audio->pending_commands;
    prev = NULL;
    do {
        next = op->next;
        if (op->batch_id == batch_id) {
            if (prev == NULL) /* Start of linked list */
            {
                audio->pending_commands = next;
            } else {
                prev->next = next;
            }

            *ready_end = op;
            op->next = NULL;
            ready_end = &op->next;
        } else {
            prev = op;
        }
        op = next;
    } while (op != NULL);

    fa_platform_unlock_mutex(audio->batchLock);
    LOG_MUTEX_UNLOCK(audio, audio->batchLock)
}

void fa_batch_execute(ForgeAudioEngine *audio) {
    ForgeAudioCommand *op, *next;

    fa_platform_lock_mutex(audio->batchLock);
    LOG_MUTEX_LOCK(audio, audio->batchLock)

    op = audio->ready_commands;
    while (op != NULL) {
        next = op->next;
        execute_command(op);
        destroy_command(op, audio->free_func);
        op = next;
    }
    audio->ready_commands = NULL;

    fa_platform_unlock_mutex(audio->batchLock);
    LOG_MUTEX_UNLOCK(audio, audio->batchLock)
}

/* Command queueing */

static inline ForgeAudioCommand *queue_command(ForgeVoice *voice, ForgeAudioCommandType type,
                                               ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *latest;
    ForgeAudioCommand *newop = voice->audio->malloc_func(sizeof(ForgeAudioCommand));

    forge_assert(batch_id != FORGE_AUDIO_BATCH_IMMEDIATE);
    forge_assert(batch_id != FORGE_AUDIO_BATCH_ALL);

    newop->type = type;
    newop->voice = voice;
    newop->batch_id = batch_id;
    newop->next = NULL;

    if (voice->audio->pending_commands == NULL) {
        voice->audio->pending_commands = newop;
    } else {
        latest = voice->audio->pending_commands;
        while (latest->next != NULL) {
            latest = latest->next;
        }
        latest->next = newop;
    }

    return newop;
}

void fa_batch_queue_enable_effect(ForgeVoice *voice, uint32_t effect_index, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_ENABLE_EFFECT, batch_id);

    op->Data.EnableEffect.effect_index = effect_index;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_disable_effect(ForgeVoice *voice, uint32_t effect_index, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_DISABLE_EFFECT, batch_id);

    op->Data.DisableEffect.effect_index = effect_index;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_effect_parameters(ForgeVoice *voice, uint32_t effect_index, const void *parameters,
                                          uint32_t parameters_byte_size, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_EFFECT_PARAMETERS, batch_id);

    op->Data.SetEffectParameters.effect_index = effect_index;
    op->Data.SetEffectParameters.parameters = voice->audio->malloc_func(parameters_byte_size);
    forge_memcpy(op->Data.SetEffectParameters.parameters, parameters, parameters_byte_size);
    op->Data.SetEffectParameters.parameters_byte_size = parameters_byte_size;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_filter_parameters(ForgeVoice *voice, const ForgeFilterParameters *parameters,
                                          ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_FILTER_PARAMETERS, batch_id);

    forge_memcpy(&op->Data.SetFilterParameters.Parameters, parameters, sizeof(ForgeFilterParameters));

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                 const ForgeFilterParameters *parameters, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_PARAMETERS, batch_id);

    op->Data.SetOutputFilterParameters.destination_voice = destination_voice;
    forge_memcpy(&op->Data.SetOutputFilterParameters.Parameters, parameters, sizeof(ForgeFilterParameters));

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_volume(ForgeVoice *voice, float volume, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_VOLUME, batch_id);

    op->Data.SetVolume.volume = volume;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_volume(ForgeVoice *voice, float volume, uint32_t duration_frames,
                                ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_RAMP_VOLUME, batch_id);

    op->Data.RampVolume.volume = volume;
    op->Data.RampVolume.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                        ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_CHANNEL_VOLUMES, batch_id);

    op->Data.SetChannelVolumes.channels = channels;
    op->Data.SetChannelVolumes.volumes = voice->audio->malloc_func(sizeof(float) * channels);
    forge_memcpy(op->Data.SetChannelVolumes.volumes, volumes, sizeof(float) * channels);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                         uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES, batch_id);

    op->Data.RampChannelVolumes.channels = channels;
    op->Data.RampChannelVolumes.volumes = voice->audio->malloc_func(sizeof(float) * channels);
    forge_memcpy(op->Data.RampChannelVolumes.volumes, volumes, sizeof(float) * channels);
    op->Data.RampChannelVolumes.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t source_channels,
                                      uint32_t destination_channels, const float *level_matrix,
                                      ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_OUTPUT_MATRIX, batch_id);

    op->Data.SetOutputMatrix.destination_voice = destination_voice;
    op->Data.SetOutputMatrix.source_channels = source_channels;
    op->Data.SetOutputMatrix.destination_channels = destination_channels;
    op->Data.SetOutputMatrix.level_matrix =
        voice->audio->malloc_func(sizeof(float) * source_channels * destination_channels);
    forge_memcpy(op->Data.SetOutputMatrix.level_matrix, level_matrix,
                 sizeof(float) * source_channels * destination_channels);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice, uint32_t source_channels,
                                       uint32_t destination_channels, const float *level_matrix,
                                       uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX, batch_id);

    op->Data.RampOutputMatrix.destination_voice = destination_voice;
    op->Data.RampOutputMatrix.source_channels = source_channels;
    op->Data.RampOutputMatrix.destination_channels = destination_channels;
    op->Data.RampOutputMatrix.level_matrix =
        voice->audio->malloc_func(sizeof(float) * source_channels * destination_channels);
    forge_memcpy(op->Data.RampOutputMatrix.level_matrix, level_matrix,
                 sizeof(float) * source_channels * destination_channels);
    op->Data.RampOutputMatrix.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_start(ForgeSourceVoice *voice, uint32_t flags, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_START, batch_id);

    op->Data.Start.flags = flags;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_stop(ForgeSourceVoice *voice, uint32_t flags, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_STOP, batch_id);

    op->Data.Stop.flags = flags;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_fade_stop(ForgeSourceVoice *voice, float volume, uint32_t duration_frames,
                              ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_FADE_STOP, batch_id);

    op->Data.FadeStop.volume = volume;
    op->Data.FadeStop.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_exit_loop(ForgeSourceVoice *voice, ForgeAudioBatchId batch_id) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    queue_command(voice, FORGE_AUDIO_COMMAND_EXIT_LOOP, batch_id);

    /* No special data for ExitLoop */

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_set_frequency_ratio(ForgeSourceVoice *voice, float ratio, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_FREQUENCY_RATIO, batch_id);

    op->Data.SetFrequencyRatio.ratio = ratio;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

/* Called when releasing the engine */

void fa_batch_clear_all(ForgeAudioEngine *audio) {
    ForgeAudioCommand *current, *next;

    fa_platform_lock_mutex(audio->batchLock);
    LOG_MUTEX_LOCK(audio, audio->batchLock)

    current = audio->pending_commands;
    while (current != NULL) {
        next = current->next;
        destroy_command(current, audio->free_func);
        current = next;
    }
    audio->pending_commands = NULL;

    fa_platform_unlock_mutex(audio->batchLock);
    LOG_MUTEX_UNLOCK(audio, audio->batchLock)
}

/* Called when releasing a voice */

static inline void remove_voice_commands(ForgeVoice *voice, ForgeAudioCommand **list) {
    ForgeAudioCommand *current, *next, *prev;

    current = *list;
    prev = NULL;
    while (current != NULL) {
        const uint8_t baseVoice = (voice == current->voice);
        const uint8_t dstVoice = (current->type == FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_PARAMETERS &&
                                  voice == current->Data.SetOutputFilterParameters.destination_voice) ||
                                 (current->type == FORGE_AUDIO_COMMAND_SET_OUTPUT_MATRIX &&
                                  voice == current->Data.SetOutputMatrix.destination_voice) ||
                                 (current->type == FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX &&
                                  voice == current->Data.RampOutputMatrix.destination_voice);

        next = current->next;
        if (baseVoice || dstVoice) {
            if (prev == NULL) /* Start of linked list */
            {
                *list = next;
            } else {
                prev->next = next;
            }

            destroy_command(current, voice->audio->free_func);
        } else {
            prev = current;
        }
        current = next;
    }
}

void fa_batch_clear_all_for_voice(ForgeVoice *voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_voice_commands(voice, &voice->audio->pending_commands);
    remove_voice_commands(voice, &voice->audio->ready_commands);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}
