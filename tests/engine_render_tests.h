/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#ifndef FORGE_ENGINE_RENDER_TESTS_H
#define FORGE_ENGINE_RENDER_TESTS_H

#include "audio_render_harness.h"

int create_started_dc_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, float *source,
                             uint32_t buffer_frames, uint32_t channels, uint32_t sample_rate,
                             float source_value);
int check_result(const char *label, ForgeResult actual, ForgeResult expected);
int run_test(const char *name, int (*test_func)(void));

int test_virtual_silence_smoke(void);
int test_public_source_dc_render(void);
int test_deferred_batch_start_timing(void);
int test_split_buffer_equals_contiguous(void);
int test_deferred_volume_boundary(void);
int test_immediate_volume_before_deferred_apply(void);
int test_deferred_stop_boundary(void);
int test_source_rate_change_continuity_smoke(void);
int test_render_api_rejects_invalid_inputs(void);
int test_volume_target_default_duration(void);
int test_volume_target_immediate_getter_visibility(void);
int test_deferred_start_and_volume_target_same_batch(void);
int test_set_volume_cancels_pending_immediate_target(void);
int test_immediate_ramp_get_volume_visible_after_render(void);
int test_set_volume_cancels_pending_immediate_ramp(void);
int test_ready_order_apply_after_immediate_appends_deferred(void);
int test_destroy_voice_removes_pending_immediate_automation(void);
int test_volume_ramp_four_frames(void);
int test_volume_ramp_reaches_target_mid_block(void);
int test_deferred_start_and_volume_ramp_same_batch(void);
int test_volume_ramp_retarget_uses_current_value(void);
int test_set_volume_cancels_active_ramp(void);
int test_set_volume_cancels_ready_deferred_ramp(void);
int test_stopped_source_ramp_advances_on_engine_timeline(void);
int test_deferred_volume_ramp_waits_for_apply(void);
int test_get_volume_reports_next_ramp_coefficient(void);
int test_batch_volume_ramp_order_last_command_wins(void);
int test_batch_set_then_ramp_volume_order(void);
int test_batch_ramp_then_set_volume_order(void);
int test_submix_volume_ramp(void);
int test_channel_volume_target_default_duration(void);
int test_channel_volume_ramp_stereo_four_frames(void);
int test_channel_volume_ramp_reaches_target_mid_block(void);
int test_deferred_start_and_channel_volume_ramp_same_batch(void);
int test_channel_volume_ramp_retarget_uses_current_values(void);
int test_set_channel_volumes_cancels_active_ramp(void);
int test_set_channel_volumes_cancels_ready_deferred_ramp(void);
int test_scalar_and_channel_volume_ramps_multiply(void);
int test_output_matrix_target_default_duration(void);
int test_output_matrix_ramp_mono_to_stereo_four_frames(void);
int test_inactive_output_matrix_null_destination_single_send(void);
int test_output_matrix_ramp_reaches_target_mid_block(void);
int test_deferred_start_and_output_matrix_ramp_same_batch(void);
int test_output_matrix_ramp_retarget_uses_current_values(void);
int test_set_output_matrix_cancels_active_ramp(void);
int test_set_output_matrix_cancels_ready_deferred_ramp(void);
int test_scalar_channel_and_output_matrix_ramps_multiply(void);
int test_stopped_source_output_matrix_ramp_advances_on_engine_timeline(void);
int test_zero_duration_ramps_snap_immediately(void);
int test_deferred_zero_duration_ramps_snap_after_apply(void);
int test_gain_ramp_invalid_arguments(void);
int test_master_volume_ramp(void);
int test_source_fade_stop_stops_on_timeline(void);
int test_deferred_source_fade_stop_waits_for_apply(void);
int test_source_fade_stop_zero_duration_stops_at_boundary(void);
int test_batch_fade_stop_then_set_volume_cancels_terminal_stop(void);
int test_batch_ramp_then_fade_stop_order(void);
int test_batch_fade_stop_then_ramp_cancels_terminal_stop(void);
int test_stopped_source_fade_stop_completes_on_engine_timeline(void);
int test_fade_stop_samples_played_stops_advancing(void);

#endif /* FORGE_ENGINE_RENDER_TESTS_H */
