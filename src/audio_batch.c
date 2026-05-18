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
    FORGE_AUDIO_COMMAND_RAMP_REVERB_PARAMETERS,
    FORGE_AUDIO_COMMAND_RAMP_DELAY_PARAMETERS,
    FORGE_AUDIO_COMMAND_RAMP_BIQUAD_PARAMETERS,
    FORGE_AUDIO_COMMAND_SET_FILTER_PARAMETERS,
    FORGE_AUDIO_COMMAND_SET_FILTER_TYPE,
    FORGE_AUDIO_COMMAND_RAMP_FILTER,
    FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_PARAMETERS,
    FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_TYPE,
    FORGE_AUDIO_COMMAND_RAMP_OUTPUT_FILTER,
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
    FORGE_AUDIO_COMMAND_SET_FREQUENCY_RATIO,
    FORGE_AUDIO_COMMAND_RAMP_FREQUENCY_RATIO
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
            uint32_t effect_index;
            ForgeReverbTarget target;
            uint32_t duration_frames;
        } RampReverbParameters;
        struct {
            uint32_t effect_index;
            ForgeDelayTarget target;
            uint32_t duration_frames;
        } RampDelayParameters;
        struct {
            uint32_t effect_index;
            ForgeBiquadTarget target;
            uint32_t duration_frames;
        } RampBiquadParameters;
        struct {
            ForgeFilterParameters Parameters;
        } SetFilterParameters;
        struct {
            ForgeFilterType type;
        } SetFilterType;
        struct {
            ForgeFilterTarget target;
            uint32_t duration_frames;
        } RampFilter;
        struct {
            ForgeVoice *destination_voice;
            ForgeFilterParameters Parameters;
        } SetOutputFilterParameters;
        struct {
            ForgeVoice *destination_voice;
            ForgeFilterType type;
        } SetOutputFilterType;
        struct {
            ForgeVoice *destination_voice;
            ForgeFilterTarget target;
            uint32_t duration_frames;
        } RampOutputFilter;
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
        struct {
            float ratio;
            uint32_t duration_frames;
        } RampFrequencyRatio;
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
        fa_voice_install_set_effect_parameters(op->voice, op->Data.SetEffectParameters.effect_index,
                                               op->Data.SetEffectParameters.parameters,
                                               op->Data.SetEffectParameters.parameters_byte_size);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_REVERB_PARAMETERS:
        fa_voice_install_ramp_reverb_parameters(op->voice, op->Data.RampReverbParameters.effect_index,
                                                &op->Data.RampReverbParameters.target,
                                                op->Data.RampReverbParameters.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_DELAY_PARAMETERS:
        fa_voice_install_ramp_delay_parameters(op->voice, op->Data.RampDelayParameters.effect_index,
                                               &op->Data.RampDelayParameters.target,
                                               op->Data.RampDelayParameters.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_BIQUAD_PARAMETERS:
        fa_voice_install_ramp_biquad_parameters(op->voice, op->Data.RampBiquadParameters.effect_index,
                                                &op->Data.RampBiquadParameters.target,
                                                op->Data.RampBiquadParameters.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_SET_FILTER_PARAMETERS:
        fa_voice_install_set_filter_parameters(op->voice, &op->Data.SetFilterParameters.Parameters);
        break;

    case FORGE_AUDIO_COMMAND_SET_FILTER_TYPE:
        fa_voice_install_set_filter_type(op->voice, op->Data.SetFilterType.type);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_FILTER:
        fa_voice_install_ramp_filter(op->voice, &op->Data.RampFilter.target,
                                     op->Data.RampFilter.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_PARAMETERS:
        fa_voice_install_set_output_filter_parameters(op->voice, op->Data.SetOutputFilterParameters.destination_voice,
                                                      &op->Data.SetOutputFilterParameters.Parameters);
        break;

    case FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_TYPE:
        fa_voice_install_set_output_filter_type(op->voice, op->Data.SetOutputFilterType.destination_voice,
                                                op->Data.SetOutputFilterType.type);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_OUTPUT_FILTER:
        fa_voice_install_ramp_output_filter(op->voice, op->Data.RampOutputFilter.destination_voice,
                                            &op->Data.RampOutputFilter.target,
                                            op->Data.RampOutputFilter.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_SET_VOLUME:
        fa_voice_install_set_volume(op->voice, op->Data.SetVolume.volume);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_VOLUME:
        fa_voice_install_ramp_volume(op->voice, op->Data.RampVolume.volume, op->Data.RampVolume.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_SET_CHANNEL_VOLUMES:
        fa_voice_install_set_channel_volumes(op->voice, op->Data.SetChannelVolumes.channels,
                                             op->Data.SetChannelVolumes.volumes);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES:
        fa_voice_install_ramp_channel_volumes(op->voice, op->Data.RampChannelVolumes.channels,
                                              op->Data.RampChannelVolumes.volumes,
                                              op->Data.RampChannelVolumes.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_SET_OUTPUT_MATRIX:
        fa_voice_install_set_output_matrix(op->voice, op->Data.SetOutputMatrix.destination_voice,
                                           op->Data.SetOutputMatrix.source_channels,
                                           op->Data.SetOutputMatrix.destination_channels,
                                           op->Data.SetOutputMatrix.level_matrix);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX:
        fa_voice_install_ramp_output_matrix(op->voice, op->Data.RampOutputMatrix.destination_voice,
                                            op->Data.RampOutputMatrix.source_channels,
                                            op->Data.RampOutputMatrix.destination_channels,
                                            op->Data.RampOutputMatrix.level_matrix,
                                            op->Data.RampOutputMatrix.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_START:
        forge_source_voice_start(op->voice, op->Data.Start.flags, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_STOP:
        forge_source_voice_stop(op->voice, op->Data.Stop.flags, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_FADE_STOP:
        fa_source_voice_install_fade_stop(op->voice, op->Data.FadeStop.volume, op->Data.FadeStop.duration_frames);
        break;

    case FORGE_AUDIO_COMMAND_EXIT_LOOP:
        forge_source_voice_break_loop(op->voice, FORGE_AUDIO_BATCH_IMMEDIATE);
        break;

    case FORGE_AUDIO_COMMAND_SET_FREQUENCY_RATIO:
        fa_source_voice_install_set_rate(op->voice, op->Data.SetFrequencyRatio.ratio);
        break;

    case FORGE_AUDIO_COMMAND_RAMP_FREQUENCY_RATIO:
        fa_source_voice_install_ramp_rate(op->voice, op->Data.RampFrequencyRatio.ratio,
                                          op->Data.RampFrequencyRatio.duration_frames);
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

static inline ForgeAudioCommand *queue_command_to_list(ForgeVoice *voice, ForgeAudioCommandType type,
                                                       ForgeAudioBatchId batch_id, ForgeAudioCommand **list) {
    ForgeAudioCommand *latest;
    ForgeAudioCommand *newop = voice->audio->malloc_func(sizeof(ForgeAudioCommand));

    forge_assert(batch_id != FORGE_AUDIO_BATCH_ALL);

    newop->type = type;
    newop->voice = voice;
    newop->batch_id = batch_id;
    newop->next = NULL;

    if (*list == NULL) {
        *list = newop;
    } else {
        latest = *list;
        while (latest->next != NULL) {
            latest = latest->next;
        }
        latest->next = newop;
    }

    return newop;
}

static inline ForgeAudioCommand *queue_command(ForgeVoice *voice, ForgeAudioCommandType type,
                                               ForgeAudioBatchId batch_id) {
    forge_assert(batch_id != FORGE_AUDIO_BATCH_IMMEDIATE);
    return queue_command_to_list(voice, type, batch_id, &voice->audio->pending_commands);
}

static inline ForgeAudioCommand *queue_automation_command(ForgeVoice *voice, ForgeAudioCommandType type,
                                                          ForgeAudioBatchId batch_id) {
    if (batch_id == FORGE_AUDIO_BATCH_IMMEDIATE) {
        return queue_command_to_list(voice, type, batch_id, &voice->audio->ready_commands);
    }
    return queue_command(voice, type, batch_id);
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

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_SET_EFFECT_PARAMETERS, batch_id);

    op->Data.SetEffectParameters.effect_index = effect_index;
    op->Data.SetEffectParameters.parameters = voice->audio->malloc_func(parameters_byte_size);
    forge_memcpy(op->Data.SetEffectParameters.parameters, parameters, parameters_byte_size);
    op->Data.SetEffectParameters.parameters_byte_size = parameters_byte_size;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_reverb_parameters(ForgeVoice *voice, uint32_t effect_index, const ForgeReverbTarget *target,
                                           uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_REVERB_PARAMETERS, batch_id);

    op->Data.RampReverbParameters.effect_index = effect_index;
    op->Data.RampReverbParameters.target = *target;
    op->Data.RampReverbParameters.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_delay_parameters(ForgeVoice *voice, uint32_t effect_index, const ForgeDelayTarget *target,
                                          uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_DELAY_PARAMETERS, batch_id);

    op->Data.RampDelayParameters.effect_index = effect_index;
    op->Data.RampDelayParameters.target = *target;
    op->Data.RampDelayParameters.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_biquad_parameters(ForgeVoice *voice, uint32_t effect_index, const ForgeBiquadTarget *target,
                                           uint32_t duration_frames, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_BIQUAD_PARAMETERS, batch_id);

    op->Data.RampBiquadParameters.effect_index = effect_index;
    op->Data.RampBiquadParameters.target = *target;
    op->Data.RampBiquadParameters.duration_frames = duration_frames;

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

void fa_batch_queue_set_filter_type(ForgeVoice *voice, ForgeFilterType type, ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_FILTER_TYPE, batch_id);

    op->Data.SetFilterType.type = type;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_filter(ForgeVoice *voice, const ForgeFilterTarget *target, uint32_t duration_frames,
                                ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_FILTER, batch_id);

    op->Data.RampFilter.target = *target;
    op->Data.RampFilter.duration_frames = duration_frames;

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

void fa_batch_queue_set_output_filter_type(ForgeVoice *voice, ForgeVoice *destination_voice, ForgeFilterType type,
                                           ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_command(voice, FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_TYPE, batch_id);

    op->Data.SetOutputFilterType.destination_voice = destination_voice;
    op->Data.SetOutputFilterType.type = type;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_queue_ramp_output_filter(ForgeVoice *voice, ForgeVoice *destination_voice,
                                       const ForgeFilterTarget *target, uint32_t duration_frames,
                                       ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_OUTPUT_FILTER, batch_id);

    op->Data.RampOutputFilter.destination_voice = destination_voice;
    op->Data.RampOutputFilter.target = *target;
    op->Data.RampOutputFilter.duration_frames = duration_frames;

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

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_VOLUME, batch_id);

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

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES, batch_id);

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

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX, batch_id);

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

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_FADE_STOP, batch_id);

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

void fa_batch_queue_ramp_frequency_ratio(ForgeSourceVoice *voice, float ratio, uint32_t duration_frames,
                                         ForgeAudioBatchId batch_id) {
    ForgeAudioCommand *op;

    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    op = queue_automation_command(voice, FORGE_AUDIO_COMMAND_RAMP_FREQUENCY_RATIO, batch_id);

    op->Data.RampFrequencyRatio.ratio = ratio;
    op->Data.RampFrequencyRatio.duration_frames = duration_frames;

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

/* Called when releasing the engine */

static inline void destroy_command_list(ForgeAudioCommand **list, ForgeFreeFunc free_func) {
    ForgeAudioCommand *current, *next;

    current = *list;
    while (current != NULL) {
        next = current->next;
        destroy_command(current, free_func);
        current = next;
    }
    *list = NULL;
}

void fa_batch_clear_all(ForgeAudioEngine *audio) {
    fa_platform_lock_mutex(audio->batchLock);
    LOG_MUTEX_LOCK(audio, audio->batchLock)

    destroy_command_list(&audio->pending_commands, audio->free_func);
    destroy_command_list(&audio->ready_commands, audio->free_func);

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
                                 (current->type == FORGE_AUDIO_COMMAND_SET_OUTPUT_FILTER_TYPE &&
                                  voice == current->Data.SetOutputFilterType.destination_voice) ||
                                 (current->type == FORGE_AUDIO_COMMAND_RAMP_OUTPUT_FILTER &&
                                  voice == current->Data.RampOutputFilter.destination_voice) ||
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

static inline void remove_ready_automation_commands(ForgeVoice *voice, ForgeAudioCommandType type,
                                                    ForgeVoice *destination_voice) {
    ForgeAudioCommand *current, *next, *prev;

    current = voice->audio->ready_commands;
    prev = NULL;
    while (current != NULL) {
        uint8_t remove = current->voice == voice && current->type == type;

        if (remove && type == FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX) {
            remove = current->Data.RampOutputMatrix.destination_voice == destination_voice;
        }
        if (remove && type == FORGE_AUDIO_COMMAND_RAMP_OUTPUT_FILTER) {
            remove = current->Data.RampOutputFilter.destination_voice == destination_voice ||
                     current->Data.RampOutputFilter.destination_voice == NULL || destination_voice == NULL;
        }
        next = current->next;
        if (remove) {
            if (prev == NULL) {
                voice->audio->ready_commands = next;
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

void fa_batch_clear_ready_volume_automation(ForgeVoice *voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_RAMP_VOLUME, NULL);
    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_FADE_STOP, NULL);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_clear_ready_channel_volume_automation(ForgeVoice *voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_RAMP_CHANNEL_VOLUMES, NULL);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_clear_ready_output_matrix_automation(ForgeVoice *voice, ForgeVoice *destination_voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_RAMP_OUTPUT_MATRIX, destination_voice);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_clear_ready_filter_automation(ForgeVoice *voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_RAMP_FILTER, NULL);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_clear_ready_output_filter_automation(ForgeVoice *voice, ForgeVoice *destination_voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_RAMP_OUTPUT_FILTER, destination_voice);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_clear_ready_rate_automation(ForgeSourceVoice *voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_ready_automation_commands(voice, FORGE_AUDIO_COMMAND_RAMP_FREQUENCY_RATIO, NULL);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}

void fa_batch_clear_all_for_voice(ForgeVoice *voice) {
    fa_platform_lock_mutex(voice->audio->batchLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->batchLock)

    remove_voice_commands(voice, &voice->audio->pending_commands);
    remove_voice_commands(voice, &voice->audio->ready_commands);

    fa_platform_unlock_mutex(voice->audio->batchLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->batchLock)
}
