/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "engine_render_tests.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int failures = 0;

    failures += run_test("virtual_silence_smoke", test_virtual_silence_smoke);
    failures += run_test("public_source_dc_render", test_public_source_dc_render);
    failures += run_test("deferred_batch_start_timing", test_deferred_batch_start_timing);
    failures += run_test("split_buffer_equals_contiguous", test_split_buffer_equals_contiguous);
    failures += run_test("deferred_volume_boundary", test_deferred_volume_boundary);
    failures += run_test("immediate_volume_before_deferred_apply", test_immediate_volume_before_deferred_apply);
    failures += run_test("deferred_stop_boundary", test_deferred_stop_boundary);
    failures += run_test("source_rate_change_continuity_smoke", test_source_rate_change_continuity_smoke);
    failures += run_test("render_api_rejects_invalid_inputs", test_render_api_rejects_invalid_inputs);
    failures += run_test("immediate_ramp_get_volume_visible_after_render",
                         test_immediate_ramp_get_volume_visible_after_render);
    failures += run_test("set_volume_cancels_pending_immediate_ramp",
                         test_set_volume_cancels_pending_immediate_ramp);
    failures += run_test("ready_order_apply_after_immediate_appends_deferred",
                         test_ready_order_apply_after_immediate_appends_deferred);
    failures += run_test("destroy_voice_removes_pending_immediate_automation",
                         test_destroy_voice_removes_pending_immediate_automation);
    failures += run_test("volume_ramp_four_frames", test_volume_ramp_four_frames);
    failures += run_test("volume_ramp_reaches_target_mid_block", test_volume_ramp_reaches_target_mid_block);
    failures += run_test("deferred_start_and_volume_ramp_same_batch", test_deferred_start_and_volume_ramp_same_batch);
    failures += run_test("volume_ramp_retarget_uses_current_value", test_volume_ramp_retarget_uses_current_value);
    failures += run_test("set_volume_cancels_active_ramp", test_set_volume_cancels_active_ramp);
    failures += run_test("set_volume_cancels_ready_deferred_ramp",
                         test_set_volume_cancels_ready_deferred_ramp);
    failures += run_test("stopped_source_ramp_advances_on_engine_timeline",
                         test_stopped_source_ramp_advances_on_engine_timeline);
    failures += run_test("deferred_volume_ramp_waits_for_apply", test_deferred_volume_ramp_waits_for_apply);
    failures += run_test("get_volume_reports_next_ramp_coefficient", test_get_volume_reports_next_ramp_coefficient);
    failures += run_test("batch_volume_ramp_order_last_command_wins", test_batch_volume_ramp_order_last_command_wins);
    failures += run_test("batch_set_then_ramp_volume_order", test_batch_set_then_ramp_volume_order);
    failures += run_test("batch_ramp_then_set_volume_order", test_batch_ramp_then_set_volume_order);
    failures += run_test("submix_volume_ramp", test_submix_volume_ramp);
    failures += run_test("channel_volume_ramp_stereo_four_frames", test_channel_volume_ramp_stereo_four_frames);
    failures += run_test("channel_volume_ramp_reaches_target_mid_block",
                         test_channel_volume_ramp_reaches_target_mid_block);
    failures += run_test("deferred_start_and_channel_volume_ramp_same_batch",
                         test_deferred_start_and_channel_volume_ramp_same_batch);
    failures += run_test("channel_volume_ramp_retarget_uses_current_values",
                         test_channel_volume_ramp_retarget_uses_current_values);
    failures += run_test("set_channel_volumes_cancels_active_ramp", test_set_channel_volumes_cancels_active_ramp);
    failures += run_test("set_channel_volumes_cancels_ready_deferred_ramp",
                         test_set_channel_volumes_cancels_ready_deferred_ramp);
    failures += run_test("scalar_and_channel_volume_ramps_multiply",
                         test_scalar_and_channel_volume_ramps_multiply);
    failures += run_test("output_matrix_ramp_mono_to_stereo_four_frames",
                         test_output_matrix_ramp_mono_to_stereo_four_frames);
    failures += run_test("inactive_output_matrix_null_destination_single_send",
                         test_inactive_output_matrix_null_destination_single_send);
    failures += run_test("output_matrix_ramp_reaches_target_mid_block",
                         test_output_matrix_ramp_reaches_target_mid_block);
    failures += run_test("deferred_start_and_output_matrix_ramp_same_batch",
                         test_deferred_start_and_output_matrix_ramp_same_batch);
    failures += run_test("output_matrix_ramp_retarget_uses_current_values",
                         test_output_matrix_ramp_retarget_uses_current_values);
    failures += run_test("set_output_matrix_cancels_active_ramp", test_set_output_matrix_cancels_active_ramp);
    failures += run_test("set_output_matrix_cancels_ready_deferred_ramp",
                         test_set_output_matrix_cancels_ready_deferred_ramp);
    failures += run_test("scalar_channel_and_output_matrix_ramps_multiply",
                         test_scalar_channel_and_output_matrix_ramps_multiply);
    failures += run_test("stopped_source_output_matrix_ramp_advances_on_engine_timeline",
                         test_stopped_source_output_matrix_ramp_advances_on_engine_timeline);
    failures += run_test("zero_duration_ramps_snap_immediately", test_zero_duration_ramps_snap_immediately);
    failures += run_test("deferred_zero_duration_ramps_snap_after_apply",
                         test_deferred_zero_duration_ramps_snap_after_apply);
    failures += run_test("gain_ramp_invalid_arguments", test_gain_ramp_invalid_arguments);
    failures += run_test("master_volume_ramp", test_master_volume_ramp);
    failures += run_test("source_fade_stop_stops_on_timeline", test_source_fade_stop_stops_on_timeline);
    failures += run_test("deferred_source_fade_stop_waits_for_apply", test_deferred_source_fade_stop_waits_for_apply);
    failures += run_test("source_fade_stop_zero_duration_stops_at_boundary",
                         test_source_fade_stop_zero_duration_stops_at_boundary);
    failures += run_test("batch_fade_stop_then_set_volume_cancels_terminal_stop",
                         test_batch_fade_stop_then_set_volume_cancels_terminal_stop);
    failures += run_test("batch_ramp_then_fade_stop_order", test_batch_ramp_then_fade_stop_order);
    failures += run_test("batch_fade_stop_then_ramp_cancels_terminal_stop",
                         test_batch_fade_stop_then_ramp_cancels_terminal_stop);
    failures += run_test("stopped_source_fade_stop_completes_on_engine_timeline",
                         test_stopped_source_fade_stop_completes_on_engine_timeline);
    failures += run_test("fade_stop_samples_played_stops_advancing", test_fade_stop_samples_played_stops_advancing);

    return failures == 0 ? 0 : 1;
}
