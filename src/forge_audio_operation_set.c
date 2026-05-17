/* ForgeAudioEngine
 *
 * Copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma, and the MonoGame Team
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

/* Operation-set implementation originally written by Tyler Glaiel */

#include "forge_audio_internal.h"

/* Core OperationSet Types */

typedef enum ForgeAudio_OperationSet_Type
{
    FORGE_AUDIO_OP_ENABLEEFFECT,
    FORGE_AUDIO_OP_DISABLEEFFECT,
    FORGE_AUDIO_OP_SETEFFECTPARAMETERS,
    FORGE_AUDIO_OP_SETFILTERPARAMETERS,
    FORGE_AUDIO_OP_SETOUTPUTFILTERPARAMETERS,
    FORGE_AUDIO_OP_SETVOLUME,
    FORGE_AUDIO_OP_SETCHANNELVOLUMES,
    FORGE_AUDIO_OP_SETOUTPUTMATRIX,
    FORGE_AUDIO_OP_START,
    FORGE_AUDIO_OP_STOP,
    FORGE_AUDIO_OP_EXITLOOP,
    FORGE_AUDIO_OP_SETFREQUENCYRATIO
} ForgeAudio_OperationSet_Type;

struct ForgeAudio_OperationSet_Operation
{
    ForgeAudio_OperationSet_Type Type;
    uint32_t OperationSet;
    ForgeVoice *Voice;

    union
    {
        struct
        {
            uint32_t EffectIndex;
        } EnableEffect;
        struct
        {
            uint32_t EffectIndex;
        } DisableEffect;
        struct
        {
            uint32_t EffectIndex;
            void *parameters;
            uint32_t ParametersByteSize;
        } SetEffectParameters;
        struct
        {
            ForgeFilterParameters Parameters;
        } SetFilterParameters;
        struct
        {
            ForgeVoice *destination_voice;
            ForgeFilterParameters Parameters;
        } SetOutputFilterParameters;
        struct
        {
            float Volume;
        } SetVolume;
        struct
        {
            uint32_t Channels;
            float *volumes;
        } SetChannelVolumes;
        struct
        {
            ForgeVoice *destination_voice;
            uint32_t SourceChannels;
            uint32_t DestinationChannels;
            float *level_matrix;
        } SetOutputMatrix;
        struct
        {
            uint32_t Flags;
        } Start;
        struct
        {
            uint32_t Flags;
        } Stop;
        /* No special data for ExitLoop
        struct
        {
        } ExitLoop;
        */
        struct
        {
            float Ratio;
        } SetFrequencyRatio;
    } Data;

    ForgeAudio_OperationSet_Operation *next;
};

/* Used by both Commit and Clear routines */

static inline void DeleteOperation(
    ForgeAudio_OperationSet_Operation *op,
    ForgeFreeFunc free_func
) {
    if (op->Type == FORGE_AUDIO_OP_SETEFFECTPARAMETERS)
    {
        free_func(op->Data.SetEffectParameters.parameters);
    }
    else if (op->Type == FORGE_AUDIO_OP_SETCHANNELVOLUMES)
    {
        free_func(op->Data.SetChannelVolumes.volumes);
    }
    else if (op->Type == FORGE_AUDIO_OP_SETOUTPUTMATRIX)
    {
        free_func(op->Data.SetOutputMatrix.level_matrix);
    }
    free_func(op);
}

/* OperationSet Execution */

static inline void ExecuteOperation(ForgeAudio_OperationSet_Operation *op)
{
    switch (op->Type)
    {
    case FORGE_AUDIO_OP_ENABLEEFFECT:
        forge_voice_enable_effect(
            op->Voice,
            op->Data.EnableEffect.EffectIndex,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_DISABLEEFFECT:
        forge_voice_disable_effect(
            op->Voice,
            op->Data.DisableEffect.EffectIndex,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETEFFECTPARAMETERS:
        forge_voice_set_effect_parameters(
            op->Voice,
            op->Data.SetEffectParameters.EffectIndex,
            op->Data.SetEffectParameters.parameters,
            op->Data.SetEffectParameters.ParametersByteSize,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETFILTERPARAMETERS:
        forge_voice_set_filter_parameters(
            op->Voice,
            &op->Data.SetFilterParameters.Parameters,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETOUTPUTFILTERPARAMETERS:
        forge_voice_set_output_filter_parameters(
            op->Voice,
            op->Data.SetOutputFilterParameters.destination_voice,
            &op->Data.SetOutputFilterParameters.Parameters,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETVOLUME:
        forge_voice_set_volume(
            op->Voice,
            op->Data.SetVolume.Volume,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETCHANNELVOLUMES:
        forge_voice_set_channel_volumes(
            op->Voice,
            op->Data.SetChannelVolumes.Channels,
            op->Data.SetChannelVolumes.volumes,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETOUTPUTMATRIX:
        forge_voice_set_output_matrix(
            op->Voice,
            op->Data.SetOutputMatrix.destination_voice,
            op->Data.SetOutputMatrix.SourceChannels,
            op->Data.SetOutputMatrix.DestinationChannels,
            op->Data.SetOutputMatrix.level_matrix,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_START:
        forge_source_voice_start(
            op->Voice,
            op->Data.Start.Flags,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_STOP:
        forge_source_voice_stop(
            op->Voice,
            op->Data.Stop.Flags,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_EXITLOOP:
        forge_source_voice_break_loop(
            op->Voice,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    case FORGE_AUDIO_OP_SETFREQUENCYRATIO:
        forge_source_voice_set_rate(
            op->Voice,
            op->Data.SetFrequencyRatio.Ratio,
            FORGE_AUDIO_COMMIT_NOW
        );
    break;

    default:
        ForgeAudio_assert(0 && "Unrecognized operation type!");
    break;
    }
}

void ForgeAudio_OperationSet_CommitAll(ForgeAudioEngine *audio)
{
    ForgeAudio_OperationSet_Operation *op, *next, **committed_end;

    ForgeAudio_PlatformLockMutex(audio->operationLock);
    LOG_MUTEX_LOCK(audio, audio->operationLock)

    if (audio->queuedOperations == NULL)
    {
        ForgeAudio_PlatformUnlockMutex(audio->operationLock);
        LOG_MUTEX_UNLOCK(audio, audio->operationLock)
        return;
    }

    committed_end = &audio->committedOperations;
    while (*committed_end)
    {
        committed_end = &((*committed_end)->next);
    }

    op = audio->queuedOperations;
    do
    {
        next = op->next;

        *committed_end = op;
        op->next = NULL;
        committed_end = &op->next;

        op = next;
    } while (op != NULL);
    audio->queuedOperations = NULL;

    ForgeAudio_PlatformUnlockMutex(audio->operationLock);
    LOG_MUTEX_UNLOCK(audio, audio->operationLock)
}

void ForgeAudio_OperationSet_Commit(ForgeAudioEngine *audio, uint32_t OperationSet)
{
    ForgeAudio_OperationSet_Operation *op, *next, *prev, **committed_end;

    ForgeAudio_PlatformLockMutex(audio->operationLock);
    LOG_MUTEX_LOCK(audio, audio->operationLock)

    if (audio->queuedOperations == NULL)
    {
        ForgeAudio_PlatformUnlockMutex(audio->operationLock);
        LOG_MUTEX_UNLOCK(audio, audio->operationLock)
        return;
    }

    committed_end = &audio->committedOperations;
    while (*committed_end)
    {
        committed_end = &((*committed_end)->next);
    }

    op = audio->queuedOperations;
    prev = NULL;
    do
    {
        next = op->next;
        if (op->OperationSet == OperationSet)
        {
            if (prev == NULL) /* Start of linked list */
            {
                audio->queuedOperations = next;
            }
            else
            {
                prev->next = next;
            }

            *committed_end = op;
            op->next = NULL;
            committed_end = &op->next;
        }
        else
        {
            prev = op;
        }
        op = next;
    } while (op != NULL);

    ForgeAudio_PlatformUnlockMutex(audio->operationLock);
    LOG_MUTEX_UNLOCK(audio, audio->operationLock)
}

void ForgeAudio_OperationSet_Execute(ForgeAudioEngine *audio)
{
    ForgeAudio_OperationSet_Operation *op, *next;

    ForgeAudio_PlatformLockMutex(audio->operationLock);
    LOG_MUTEX_LOCK(audio, audio->operationLock)

    op = audio->committedOperations;
    while (op != NULL)
    {
        next = op->next;
        ExecuteOperation(op);
        DeleteOperation(op, audio->free_func);
        op = next;
    }
    audio->committedOperations = NULL;

    ForgeAudio_PlatformUnlockMutex(audio->operationLock);
    LOG_MUTEX_UNLOCK(audio, audio->operationLock)
}

/* OperationSet Compilation */

static inline ForgeAudio_OperationSet_Operation* QueueOperation(
    ForgeVoice *voice,
    ForgeAudio_OperationSet_Type type,
    uint32_t operationSet
) {
    ForgeAudio_OperationSet_Operation *latest;
    ForgeAudio_OperationSet_Operation *newop = voice->audio->malloc_func(
        sizeof(ForgeAudio_OperationSet_Operation)
    );

    newop->Type = type;
    newop->Voice = voice;
    newop->OperationSet = operationSet;
    newop->next = NULL;

    if (voice->audio->queuedOperations == NULL)
    {
        voice->audio->queuedOperations = newop;
    }
    else
    {
        latest = voice->audio->queuedOperations;
        while (latest->next != NULL)
        {
            latest = latest->next;
        }
        latest->next = newop;
    }

    return newop;
}

void ForgeAudio_OperationSet_QueueEnableEffect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_ENABLEEFFECT,
        OperationSet
    );

    op->Data.EnableEffect.EffectIndex = EffectIndex;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueDisableEffect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_DISABLEEFFECT,
        OperationSet
    );

    op->Data.DisableEffect.EffectIndex = EffectIndex;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetEffectParameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    const void *parameters,
    uint32_t ParametersByteSize,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETEFFECTPARAMETERS,
        OperationSet
    );

    op->Data.SetEffectParameters.EffectIndex = EffectIndex;
    op->Data.SetEffectParameters.parameters = voice->audio->malloc_func(
        ParametersByteSize
    );
    ForgeAudio_memcpy(
        op->Data.SetEffectParameters.parameters,
        parameters,
        ParametersByteSize
    );
    op->Data.SetEffectParameters.ParametersByteSize = ParametersByteSize;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetFilterParameters(
    ForgeVoice *voice,
    const ForgeFilterParameters *parameters,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETFILTERPARAMETERS,
        OperationSet
    );

    ForgeAudio_memcpy(
        &op->Data.SetFilterParameters.Parameters,
        parameters,
        sizeof(ForgeFilterParameters)
    );

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetOutputFilterParameters(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    const ForgeFilterParameters *parameters,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETOUTPUTFILTERPARAMETERS,
        OperationSet
    );

    op->Data.SetOutputFilterParameters.destination_voice = destination_voice;
    ForgeAudio_memcpy(
        &op->Data.SetOutputFilterParameters.Parameters,
        parameters,
        sizeof(ForgeFilterParameters)
    );

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetVolume(
    ForgeVoice *voice,
    float Volume,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETVOLUME,
        OperationSet
    );

    op->Data.SetVolume.Volume = Volume;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetChannelVolumes(
    ForgeVoice *voice,
    uint32_t Channels,
    const float *volumes,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETCHANNELVOLUMES,
        OperationSet
    );

    op->Data.SetChannelVolumes.Channels = Channels;
    op->Data.SetChannelVolumes.volumes = voice->audio->malloc_func(
        sizeof(float) * Channels
    );
    ForgeAudio_memcpy(
        op->Data.SetChannelVolumes.volumes,
        volumes,
        sizeof(float) * Channels
    );

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetOutputMatrix(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    const float *level_matrix,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETOUTPUTMATRIX,
        OperationSet
    );

    op->Data.SetOutputMatrix.destination_voice = destination_voice;
    op->Data.SetOutputMatrix.SourceChannels = SourceChannels;
    op->Data.SetOutputMatrix.DestinationChannels = DestinationChannels;
    op->Data.SetOutputMatrix.level_matrix = voice->audio->malloc_func(
        sizeof(float) * SourceChannels * DestinationChannels
    );
    ForgeAudio_memcpy(
        op->Data.SetOutputMatrix.level_matrix,
        level_matrix,
        sizeof(float) * SourceChannels * DestinationChannels
    );

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueStart(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_START,
        OperationSet
    );

    op->Data.Start.Flags = Flags;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueStop(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_STOP,
        OperationSet
    );

    op->Data.Stop.Flags = Flags;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueExitLoop(
    ForgeSourceVoice *voice,
    uint32_t OperationSet
) {
    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    QueueOperation(
        voice,
        FORGE_AUDIO_OP_EXITLOOP,
        OperationSet
    );

    /* No special data for ExitLoop */

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

void ForgeAudio_OperationSet_QueueSetFrequencyRatio(
    ForgeSourceVoice *voice,
    float Ratio,
    uint32_t OperationSet
) {
    ForgeAudio_OperationSet_Operation *op;

    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    op = QueueOperation(
        voice,
        FORGE_AUDIO_OP_SETFREQUENCYRATIO,
        OperationSet
    );

    op->Data.SetFrequencyRatio.Ratio = Ratio;

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}

/* Called when releasing the engine */

void ForgeAudio_OperationSet_ClearAll(ForgeAudioEngine *audio)
{
    ForgeAudio_OperationSet_Operation *current, *next;

    ForgeAudio_PlatformLockMutex(audio->operationLock);
    LOG_MUTEX_LOCK(audio, audio->operationLock)

    current = audio->queuedOperations;
    while (current != NULL)
    {
        next = current->next;
        DeleteOperation(current, audio->free_func);
        current = next;
    }
    audio->queuedOperations = NULL;

    ForgeAudio_PlatformUnlockMutex(audio->operationLock);
    LOG_MUTEX_UNLOCK(audio, audio->operationLock)
}

/* Called when releasing a voice */

static inline void RemoveFromList(
    ForgeVoice *voice,
    ForgeAudio_OperationSet_Operation **list
) {
    ForgeAudio_OperationSet_Operation *current, *next, *prev;

    current = *list;
    prev = NULL;
    while (current != NULL)
    {
        const uint8_t baseVoice = (voice == current->Voice);
        const uint8_t dstVoice = (
            current->Type == FORGE_AUDIO_OP_SETOUTPUTFILTERPARAMETERS &&
            voice == current->Data.SetOutputFilterParameters.destination_voice
        ) || (
            current->Type == FORGE_AUDIO_OP_SETOUTPUTMATRIX &&
            voice == current->Data.SetOutputMatrix.destination_voice
        );

        next = current->next;
        if (baseVoice || dstVoice)
        {
            if (prev == NULL) /* Start of linked list */
            {
                *list = next;
            }
            else
            {
                prev->next = next;
            }

            DeleteOperation(current, voice->audio->free_func);
        }
        else
        {
            prev = current;
        }
        current = next;
    }
}

void ForgeAudio_OperationSet_ClearAllForVoice(ForgeVoice *voice)
{
    ForgeAudio_PlatformLockMutex(voice->audio->operationLock);
    LOG_MUTEX_LOCK(voice->audio, voice->audio->operationLock)

    RemoveFromList(voice, &voice->audio->queuedOperations);
    RemoveFromList(voice, &voice->audio->committedOperations);

    ForgeAudio_PlatformUnlockMutex(voice->audio->operationLock);
    LOG_MUTEX_UNLOCK(voice->audio, voice->audio->operationLock)
}
