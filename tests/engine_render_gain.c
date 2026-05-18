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

int test_zero_duration_ramps_snap_immediately(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 2
    };
    static const float channel_volume[source_channels] = {0.5f};
    static const float matrix[destination_channels * source_channels] = {0.25f, 0.75f};
    static const float expected[quantum * destination_channels] = {
        0.0625f, 0.1875f, 0.0625f, 0.1875f, 0.0625f, 0.1875f, 0.0625f, 0.1875f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * destination_channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, source_channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 0.5f, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, source_channels, channel_volume, 0,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels, matrix,
                                                0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("zero_duration_ramps_snap_immediately", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_zero_duration_ramps_snap_after_apply(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 990;
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float channel_volume[source_channels] = {0.5f};
    static const float matrix[destination_channels * source_channels] = {0.25f, 0.75f};
    static const float before_expected[quantum * destination_channels] = {
        1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    static const float after_expected[quantum * destination_channels] = {
        0.0625f, 0.1875f, 0.0625f, 0.1875f, 0.0625f, 0.1875f, 0.0625f, 0.1875f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * destination_channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, source_channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 0.5f, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, source_channels, channel_volume, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels, matrix,
                                                0, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("deferred_zero_duration_before_apply", output, before_expected,
                                        quantum * destination_channels, 0.000001f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("deferred_zero_duration_after_apply", output, after_expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_gain_ramp_invalid_arguments(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 2
    };
    static const float channel_volume[source_channels] = {1.0f};
    static const float matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    ForgeSendList empty_sends = {0, NULL};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeSubmixVoice *unattached = NULL;
    float source[buffer_frames * source_channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, source_channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &unattached, destination_channels, sample_rate, 0, 0,
                                                 NULL, NULL) != 0;
    }

    if (!failed) {
        failed = check_result("volume_ramp_batch_all",
                              forge_voice_ramp_volume(voice, 0.5f, quantum, FORGE_AUDIO_BATCH_ALL),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("channel_ramp_null",
                              forge_voice_ramp_channel_volumes(voice, source_channels, NULL, quantum,
                                                               FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("channel_ramp_wrong_count",
                              forge_voice_ramp_channel_volumes(voice, source_channels + 1, channel_volume, quantum,
                                                               FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("channel_ramp_master",
                              forge_voice_ramp_channel_volumes(harness.master, destination_channels, channel_volume,
                                                               quantum, FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("channel_ramp_batch_all",
                              forge_voice_ramp_channel_volumes(voice, source_channels, channel_volume, quantum,
                                                               FORGE_AUDIO_BATCH_ALL),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("set_channel_null",
                              forge_voice_set_channel_volumes(voice, source_channels, NULL,
                                                              FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("output_matrix_ramp_null",
                              forge_voice_ramp_output_matrix(voice, harness.master, source_channels,
                                                             destination_channels, NULL, quantum,
                                                             FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("output_matrix_ramp_wrong_source_count",
                              forge_voice_ramp_output_matrix(voice, harness.master, source_channels + 1,
                                                             destination_channels, matrix, quantum,
                                                             FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("output_matrix_ramp_wrong_destination_count",
                              forge_voice_ramp_output_matrix(voice, harness.master, source_channels,
                                                             destination_channels + 1, matrix, quantum,
                                                             FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("output_matrix_ramp_unattached_destination",
                              forge_voice_ramp_output_matrix(voice, unattached, source_channels, destination_channels,
                                                             matrix, quantum, FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("output_matrix_ramp_batch_all",
                              forge_voice_ramp_output_matrix(voice, harness.master, source_channels,
                                                             destination_channels, matrix, quantum,
                                                             FORGE_AUDIO_BATCH_ALL),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("set_output_matrix_null",
                              forge_voice_set_output_matrix(voice, harness.master, source_channels,
                                                            destination_channels, NULL, FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = forge_voice_set_outputs(voice, &empty_sends) != 0;
    }
    if (!failed) {
        failed = check_result("output_matrix_ramp_missing_destination",
                              forge_voice_ramp_output_matrix(voice, NULL, source_channels, destination_channels,
                                                             matrix, quantum, FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
