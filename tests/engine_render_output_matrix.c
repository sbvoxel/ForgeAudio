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

int test_output_matrix_ramp_mono_to_stereo_four_frames(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float expected[quantum * destination_channels] = {
        1.0f, 0.0f, 0.75f, 0.25f, 0.5f, 0.5f, 0.25f, 0.75f,
        0.0f, 1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f,  1.0f};
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
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("output_matrix_ramp_four_frames", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_inactive_output_matrix_null_destination_single_send(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 2
    };
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float expected[quantum * destination_channels] = {
        1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * destination_channels];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames * source_channels; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, source_channels, sample_rate);
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, NULL, source_channels, destination_channels, start_matrix,
                                               FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, NULL, source_channels, destination_channels, target_matrix, 2,
                                                FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, source_channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("inactive_output_matrix_null_destination_single_send", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_output_matrix_ramp_reaches_target_mid_block(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float expected[quantum * destination_channels] = {
        1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
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
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("output_matrix_ramp_mid_block", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_start_and_output_matrix_ramp_same_batch(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 980;
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float expected[quantum * destination_channels] = {
        1.0f, 0.0f, 0.75f, 0.25f, 0.5f, 0.5f, 0.25f, 0.75f,
        0.0f, 1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f,  1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * destination_channels];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames * source_channels; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, source_channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, source_channels);
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, 4, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_output_matrix_start_ramp_batch", output, quantum,
                                           destination_channels, 0.0f, 0.0f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("output_matrix_start_ramp_same_batch", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_output_matrix_ramp_retarget_uses_current_values(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 5
    };
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float first_target[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float second_target[destination_channels * source_channels] = {0.0f, 0.0f};
    static const float first_expected[quantum * destination_channels] = {
        1.0f, 0.0f, 0.875f, 0.125f, 0.75f, 0.25f, 0.625f, 0.375f};
    static const float second_expected[quantum * 2 * destination_channels] = {
        0.5f, 0.5f, 0.375f, 0.375f, 0.25f, 0.25f, 0.125f, 0.125f,
        0.0f, 0.0f, 0.0f,   0.0f,   0.0f,  0.0f,  0.0f,   0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * 2 * destination_channels];
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
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                first_target, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("output_matrix_retarget_first_quantum", output, first_expected,
                                        quantum * destination_channels, 0.000001f);
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                second_target, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum * 2);
    }
    if (!failed) {
        failed = audio_test_check_equal("output_matrix_retarget_second_quantum", output, second_expected,
                                        quantum * 2 * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_output_matrix_cancels_active_ramp(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 5
    };
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float snap_matrix[destination_channels * source_channels] = {0.25f, 0.75f};
    static const float first_expected[quantum * destination_channels] = {
        1.0f, 0.0f, 0.875f, 0.125f, 0.75f, 0.25f, 0.625f, 0.375f};
    static const float second_expected[quantum * 2 * destination_channels] = {
        0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f,
        0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * 2 * destination_channels];
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
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("output_matrix_cancel_first_quantum", output, first_expected,
                                        quantum * destination_channels, 0.000001f);
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               snap_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum * 2);
    }
    if (!failed) {
        failed = audio_test_check_equal("set_output_matrix_cancels_ramp", output, second_expected,
                                        quantum * 2 * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_output_matrix_cancels_ready_deferred_ramp(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 981;
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float snap_matrix[destination_channels * source_channels] = {0.25f, 0.75f};
    static const float expected[quantum * destination_channels] = {
        0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f};
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
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               snap_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("set_output_matrix_cancels_ready_deferred_ramp", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_scalar_channel_and_output_matrix_ramps_multiply(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float start_channel_volume[source_channels] = {1.0f};
    static const float target_channel_volume[source_channels] = {0.5f};
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.5f};
    static const float target_matrix[destination_channels * source_channels] = {0.5f, 1.0f};
    static const float expected[quantum * destination_channels] = {
        0.0f,       0.0f,       0.19140625f, 0.13671875f, 0.28125f, 0.28125f,
        0.29296875f, 0.41015625f, 0.25f,      0.5f,        0.25f,    0.5f,
        0.25f,      0.5f,        0.25f,      0.5f};
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
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, source_channels, start_channel_volume,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 1.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, source_channels, target_channel_volume, 4,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("scalar_channel_and_output_matrix_ramps_multiply", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_output_matrix_ramp_advances_on_engine_timeline(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    static const float start_matrix[destination_channels * source_channels] = {1.0f, 0.0f};
    static const float target_matrix[destination_channels * source_channels] = {0.0f, 1.0f};
    static const float expected[quantum * destination_channels] = {
        0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * source_channels];
    float output[quantum * destination_channels];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames * source_channels; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, source_channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, source_channels);
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_output_matrix(voice, harness.master, source_channels, destination_channels,
                                                target_matrix, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("stopped_output_matrix_ramp_silence", output, quantum,
                                           destination_channels, 0.0f, 0.0f);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("stopped_output_matrix_ramp_after_start", output, expected,
                                        quantum * destination_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
