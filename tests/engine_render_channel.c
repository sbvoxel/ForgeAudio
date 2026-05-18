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

int test_channel_volume_target_default_duration(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        target_frames = 128,
        quantum = target_frames,
        buffer_frames = quantum * 2
    };
    static const float start_volumes[channels] = {0.0f, 1.0f};
    static const float target_volumes[channels] = {1.0f, 0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * channels];
    float expected[quantum * channels];
    int failed = 0;

    for (uint32_t i = 0; i < quantum; i += 1) {
        expected[i * channels] = (float)i / (float)target_frames;
        expected[i * channels + 1] = 1.0f - ((float)i / (float)target_frames);
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes_target(voice, channels, target_volumes,
                                                       FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_volume_target_default_duration", output, expected,
                                        quantum * channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_channel_volume_ramp_stereo_four_frames(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float start_volumes[channels] = {0.0f, 1.0f};
    static const float target_volumes[channels] = {1.0f, 0.0f};
    static const float expected[quantum * channels] = {0.0f,  1.0f, 0.25f, 0.75f, 0.5f,  0.5f,
                                                       0.75f, 0.25f, 1.0f,  0.0f,  1.0f,  0.0f,
                                                       1.0f,  0.0f,  1.0f,  0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, target_volumes, 4,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_volume_ramp_stereo_four_frames", output, expected,
                                        quantum * channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_channel_volume_ramp_reaches_target_mid_block(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float start_volumes[channels] = {0.0f, 0.0f};
    static const float target_volumes[channels] = {1.0f, 0.5f};
    static const float expected[quantum * channels] = {0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 0.5f,
                                                       1.0f, 0.5f, 1.0f, 0.5f,  1.0f, 0.5f,
                                                       1.0f, 0.5f, 1.0f, 0.5f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, target_volumes, 2,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_volume_ramp_mid_block", output, expected, quantum * channels,
                                        0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_start_and_channel_volume_ramp_same_batch(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 970;
    static const float start_volumes[channels] = {0.0f, 1.0f};
    static const float target_volumes[channels] = {1.0f, 0.0f};
    static const float expected[quantum * channels] = {0.0f,  1.0f, 0.25f, 0.75f, 0.5f,  0.5f,
                                                       0.75f, 0.25f, 1.0f,  0.0f,  1.0f,  0.0f,
                                                       1.0f,  0.0f,  1.0f,  0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * channels];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames * channels; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, target_volumes, 4, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_channel_start_ramp_batch", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_start_ramp_same_batch", output, expected, quantum * channels,
                                        0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_channel_volume_ramp_retarget_uses_current_values(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 5
    };
    static const float start_volumes[channels] = {0.0f, 1.0f};
    static const float first_target[channels] = {1.0f, 0.0f};
    static const float second_target[channels] = {0.0f, 0.0f};
    static const float first_expected[quantum * channels] = {0.0f,  1.0f, 0.125f, 0.875f,
                                                             0.25f, 0.75f, 0.375f, 0.625f};
    static const float second_expected[quantum * 2 * channels] = {
        0.5f, 0.5f, 0.375f, 0.375f, 0.25f, 0.25f, 0.125f, 0.125f,
        0.0f, 0.0f, 0.0f,   0.0f,   0.0f,  0.0f,  0.0f,   0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * 2 * channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, first_target, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_retarget_first_quantum", output, first_expected, quantum * channels,
                                        0.000001f);
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, second_target, 4,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum * 2);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_retarget_second_quantum", output, second_expected,
                                        quantum * 2 * channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_channel_volumes_cancels_active_ramp(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 5
    };
    static const float start_volumes[channels] = {0.0f, 1.0f};
    static const float target_volumes[channels] = {1.0f, 0.0f};
    static const float snap_volumes[channels] = {0.25f, 0.75f};
    static const float first_expected[quantum * channels] = {0.0f,  1.0f, 0.125f, 0.875f,
                                                             0.25f, 0.75f, 0.375f, 0.625f};
    static const float second_expected[quantum * 2 * channels] = {
        0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f,
        0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f, 0.25f, 0.75f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * 2 * channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, target_volumes, 8,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("channel_cancel_first_quantum", output, first_expected, quantum * channels,
                                        0.000001f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, snap_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum * 2);
    }
    if (!failed) {
        failed = audio_test_check_equal("set_channel_volumes_cancels_ramp", output, second_expected,
                                        quantum * 2 * channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_channel_volumes_cancels_ready_deferred_ramp(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 971;
    static const float start_volumes[channels] = {0.0f, 1.0f};
    static const float target_volumes[channels] = {1.0f, 0.0f};
    static const float snap_volumes[channels] = {0.25f, 0.75f};
    static const float expected[quantum * channels] = {0.25f, 0.75f, 0.25f, 0.75f,
                                                       0.25f, 0.75f, 0.25f, 0.75f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, target_volumes, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, snap_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("set_channel_volumes_cancels_ready_deferred_ramp", output, expected,
                                        quantum * channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_scalar_and_channel_volume_ramps_multiply(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float start_volumes[channels] = {1.0f, 0.5f};
    static const float target_volumes[channels] = {0.5f, 1.0f};
    static const float expected[quantum * channels] = {0.0f,     0.0f,    0.21875f, 0.15625f,
                                                       0.375f,   0.375f,  0.46875f, 0.65625f,
                                                       0.5f,     1.0f,    0.5f,     1.0f,
                                                       0.5f,     1.0f,    0.5f,     1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames * channels];
    float output[quantum * channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_channel_volumes(voice, channels, start_volumes, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 1.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_channel_volumes(voice, channels, target_volumes, 4,
                                                 FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("scalar_and_channel_volume_ramps_multiply", output, expected,
                                        quantum * channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
