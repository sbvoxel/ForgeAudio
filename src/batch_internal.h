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

#ifndef FORGE_BATCH_INTERNAL_H
#define FORGE_BATCH_INTERNAL_H

#include "core_internal.h"

typedef struct ForgeAudioCommand ForgeAudioCommand;

FORGE_INTERNAL_API void fa_batch_apply(ForgeAudioEngine *audio, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_apply_all(ForgeAudioEngine *audio);
FORGE_INTERNAL_API void fa_batch_execute(ForgeAudioEngine *audio);

FORGE_INTERNAL_API void fa_batch_clear_all(ForgeAudioEngine *audio);
FORGE_INTERNAL_API void fa_batch_clear_all_for_voice(ForgeVoice *voice);

FORGE_INTERNAL_API void fa_batch_queue_enable_effect(ForgeVoice *voice, uint32_t effect_index,
                                                     ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_disable_effect(ForgeVoice *voice, uint32_t effect_index,
                                                      ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_effect_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                             const void *parameters, uint32_t parameters_byte_size,
                                                             ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_filter_parameters(ForgeVoice *voice, const ForgeFilterParameters *parameters,
                                                             ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                                    const ForgeFilterParameters *parameters,
                                                                    ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_volume(ForgeVoice *voice, float volume, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_ramp_volume(ForgeVoice *voice, float volume, uint32_t duration_frames,
                                                   ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                           ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_ramp_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                            uint32_t duration_frames, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                         uint32_t source_channels, uint32_t destination_channels,
                                                         const float *level_matrix, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_start(ForgeSourceVoice *voice, uint32_t flags, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_stop(ForgeSourceVoice *voice, uint32_t flags, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_fade_stop(ForgeSourceVoice *voice, float volume, uint32_t duration_frames,
                                                 ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_exit_loop(ForgeSourceVoice *voice, ForgeAudioBatchId batch_id);
FORGE_INTERNAL_API void fa_batch_queue_set_frequency_ratio(ForgeSourceVoice *voice, float ratio,
                                                           ForgeAudioBatchId batch_id);

#endif /* FORGE_BATCH_INTERNAL_H */
