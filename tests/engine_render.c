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
    failures += run_test("source_effect_initial_send_lock_uses_render_rate",
                         test_source_effect_initial_send_lock_uses_render_rate);
    failures += run_test("submix_effect_initial_send_lock_uses_render_rate",
                         test_submix_effect_initial_send_lock_uses_render_rate);
    failures += run_test("deferred_volume_boundary", test_deferred_volume_boundary);
    failures += run_test("immediate_volume_before_deferred_apply", test_immediate_volume_before_deferred_apply);
    failures += run_test("deferred_stop_boundary", test_deferred_stop_boundary);
    failures += run_test("source_rate_change_continuity_smoke", test_source_rate_change_continuity_smoke);
    failures += run_test("render_api_rejects_invalid_inputs", test_render_api_rejects_invalid_inputs);
    failures += run_test("ms_to_frames_conversion", test_ms_to_frames_conversion);
    failures += run_test("volume_target_default_duration", test_volume_target_default_duration);
    failures += run_test("volume_ramp_ms_uses_engine_rate", test_volume_ramp_ms_uses_engine_rate);
    failures += run_test("volume_target_immediate_getter_visibility",
                         test_volume_target_immediate_getter_visibility);
    failures += run_test("deferred_start_and_volume_target_same_batch",
                         test_deferred_start_and_volume_target_same_batch);
    failures += run_test("set_volume_cancels_pending_immediate_target",
                         test_set_volume_cancels_pending_immediate_target);
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
    failures += run_test("channel_volume_target_default_duration",
                         test_channel_volume_target_default_duration);
    failures += run_test("channel_volume_ramp_ms_uses_engine_rate",
                         test_channel_volume_ramp_ms_uses_engine_rate);
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
    failures += run_test("output_matrix_target_default_duration",
                         test_output_matrix_target_default_duration);
    failures += run_test("output_matrix_ramp_ms_uses_engine_rate",
                         test_output_matrix_ramp_ms_uses_engine_rate);
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
    failures += run_test("source_fade_stop_ms_stops_on_timeline", test_source_fade_stop_ms_stops_on_timeline);
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
    failures += run_test("filter_cutoff_range_and_clamped_getter",
                         test_filter_cutoff_range_and_clamped_getter);
    failures += run_test("filter_zero_duration_ramp_snaps_after_render",
                         test_filter_zero_duration_ramp_snaps_after_render);
    failures += run_test("filter_ramp_getter_reports_current_value",
                         test_filter_ramp_getter_reports_current_value);
    failures += run_test("filter_type_preserves_ready_ramp",
                         test_filter_type_preserves_ready_ramp);
    failures += run_test("filter_field_mask_preserves_other_active_ramps",
                         test_filter_field_mask_preserves_other_active_ramps);
    failures += run_test("stopped_source_filter_ramp_advances_on_engine_timeline",
                         test_stopped_source_filter_ramp_advances_on_engine_timeline);
    failures += run_test("stopped_source_filter_ramp_uses_output_rate",
                         test_stopped_source_filter_ramp_uses_output_rate);
    failures += run_test("output_filter_type_preserves_ready_ramp",
                         test_output_filter_type_preserves_ready_ramp);
    failures += run_test("stopped_source_output_filter_ramp_uses_output_rate",
                         test_stopped_source_output_filter_ramp_uses_output_rate);
    failures += run_test("filter_invalid_type_rejected", test_filter_invalid_type_rejected);
    failures += run_test("reverb_ramp_getter_reports_current_value",
                         test_reverb_ramp_getter_reports_current_value);
    failures += run_test("reverb_wet_dry_ramp_renders_dry_path",
                         test_reverb_wet_dry_ramp_renders_dry_path);
    failures += run_test("reverb_field_mask_preserves_other_active_ramps",
                         test_reverb_field_mask_preserves_other_active_ramps);
    failures += run_test("stopped_source_reverb_ramp_advances_on_engine_timeline",
                         test_stopped_source_reverb_ramp_advances_on_engine_timeline);
    failures += run_test("disabled_reverb_ramp_advances_on_engine_timeline",
                         test_disabled_reverb_ramp_advances_on_engine_timeline);
    failures += run_test("reverb_batch_blob_then_ramp_order", test_reverb_batch_blob_then_ramp_order);
    failures += run_test("reverb_batch_ramp_then_blob_order", test_reverb_batch_ramp_then_blob_order);
    failures += run_test("reverb_zero_duration_ramp_snaps_after_render",
                         test_reverb_zero_duration_ramp_snaps_after_render);
    failures += run_test("reverb_invalid_arguments", test_reverb_invalid_arguments);
    failures += run_test("reverb_7point1_target_and_getter", test_reverb_7point1_target_and_getter);
    failures += run_test("delay_creation_kind_and_destroy", test_delay_creation_kind_and_destroy);
    failures += run_test("delay_format_validation_rejects_channel_change",
                         test_delay_format_validation_rejects_channel_change);
    failures += run_test("delay_parameter_clamping", test_delay_parameter_clamping);
    failures += run_test("delay_blob_parameter_set_get_on_voice", test_delay_blob_parameter_set_get_on_voice);
    failures += run_test("delay_impulse_after_expected_frames", test_delay_impulse_after_expected_frames);
    failures += run_test("delay_wet_dry_mix", test_delay_wet_dry_mix);
    failures += run_test("delay_feedback_repeats_decay", test_delay_feedback_repeats_decay);
    failures += run_test("delay_feedback_clamp_prevents_runaway", test_delay_feedback_clamp_prevents_runaway);
    failures += run_test("delay_lowpass_damps_feedback_path", test_delay_lowpass_damps_feedback_path);
    failures += run_test("delay_source_timing_uses_render_sample_rate",
                         test_delay_source_timing_uses_render_sample_rate);
    failures += run_test("delay_in_place_processing", test_delay_in_place_processing);
    failures += run_test("delay_disabled_clears_delayed_samples", test_delay_disabled_clears_delayed_samples);
    failures += run_test("delay_reset_clears_delayed_samples", test_delay_reset_clears_delayed_samples);
    failures += run_test("delay_tail_flags_clear_after_consumed_sample",
                         test_delay_tail_flags_clear_after_consumed_sample);
    failures += run_test("delay_grow_discards_orphaned_samples", test_delay_grow_discards_orphaned_samples);
    failures += run_test("delay_tail_drains_with_play_tails", test_delay_tail_drains_with_play_tails);
    failures += run_test("limiter_creation_kind_and_destroy", test_limiter_creation_kind_and_destroy);
    failures += run_test("limiter_format_validation_rejects_channel_change",
                         test_limiter_format_validation_rejects_channel_change);
    failures += run_test("limiter_parameter_clamping", test_limiter_parameter_clamping);
    failures += run_test("limiter_below_ceiling_outputs_delayed_input",
                         test_limiter_below_ceiling_outputs_delayed_input);
    failures += run_test("limiter_above_ceiling_limits_sample_peaks",
                         test_limiter_above_ceiling_limits_sample_peaks);
    failures += run_test("limiter_linked_channels_reduce_all_channels",
                         test_limiter_linked_channels_reduce_all_channels);
    failures += run_test("limiter_release_recovers_gradually", test_limiter_release_recovers_gradually);
    failures += run_test("limiter_zero_lookahead_limits_without_delay",
                         test_limiter_zero_lookahead_limits_without_delay);
    failures += run_test("limiter_lookahead_uses_output_sample_rate",
                         test_limiter_lookahead_uses_output_sample_rate);
    failures += run_test("limiter_source_lookahead_uses_render_sample_rate",
                         test_limiter_source_lookahead_uses_render_sample_rate);
    failures += run_test("limiter_submix_lookahead_uses_render_sample_rate",
                         test_limiter_submix_lookahead_uses_render_sample_rate);
    failures += run_test("limiter_blob_parameter_set_updates_render",
                         test_limiter_blob_parameter_set_updates_render);
    failures += run_test("limiter_tail_drains_delayed_samples", test_limiter_tail_drains_delayed_samples);
    failures += run_test("limiter_disabled_clears_delayed_samples",
                         test_limiter_disabled_clears_delayed_samples);

    return failures == 0 ? 0 : 1;
}
