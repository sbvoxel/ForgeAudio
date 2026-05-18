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

int test_volume_target_default_duration(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        target_frames = 128,
        quantum = target_frames,
        buffer_frames = quantum * 2
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float expected[quantum];
    float volume = -1.0f;
    int failed = 0;

    for (uint32_t i = 0; i < quantum; i += 1) {
        expected[i] = (float)i / (float)target_frames;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume_target(voice, 1.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("volume_target_default_duration", output, expected, quantum, 0.000001f);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - 1.0f) > 0.000001f) {
            fprintf(stderr, "volume target completion: expected 1.0, got %.8f\n", volume);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_volume_ramp_ms_uses_engine_rate(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_ms(voice, 1.0f, (1000.0 * 4.0) / sample_rate,
                                            FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("volume_ramp_ms_uses_engine_rate", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_volume_target_immediate_getter_visibility(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        target_frames = 128,
        quantum = 4,
        buffer_frames = target_frames
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float volume = -1.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume_target(voice, 1.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume) > 0.000001f) {
            fprintf(stderr, "queued immediate target visible before render: %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - ((float)quantum / (float)target_frames)) > 0.000001f) {
            fprintf(stderr, "queued immediate target after render: expected %.8f, got %.8f\n",
                    (float)quantum / (float)target_frames, volume);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_start_and_volume_target_same_batch(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        target_frames = 128,
        quantum = 8,
        buffer_frames = target_frames
    };
    const ForgeAudioBatchId batch_id = 905;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float expected[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 1.0f;
    }
    for (uint32_t i = 0; i < quantum; i += 1) {
        expected[i] = (float)i / (float)target_frames;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume_target(voice, 1.0f, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_start_target_batch", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("start_target_same_batch", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_volume_cancels_pending_immediate_target(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 2
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume_target(voice, 1.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.75f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("set_volume_cancels_pending_immediate_target", output, quantum,
                                           channels, 0.75f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_immediate_ramp_get_volume_visible_after_render(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    static const float expected[quantum] = {0.0f, 0.125f, 0.25f, 0.375f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float volume = -1.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum * 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume) > 0.000001f) {
            fprintf(stderr, "queued immediate ramp visible before render: %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("immediate_ramp_after_render", output, expected, quantum, 0.000001f);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - 0.5f) > 0.000001f) {
            fprintf(stderr, "queued immediate ramp after render: expected 0.5, got %.8f\n", volume);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_volume_cancels_pending_immediate_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float volume = -1.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume) > 0.000001f) {
            fprintf(stderr, "pending immediate ramp changed volume before render: %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.75f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - 0.75f) > 0.000001f) {
            fprintf(stderr, "set_volume did not snap before render: %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("set_volume_cancels_pending_immediate_ramp", output, quantum, channels,
                                           0.75f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_ready_order_apply_after_immediate_appends_deferred(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 903;
    static const float expected[quantum] = {0.0f, 0.0625f, 0.125f, 0.1875f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 0.25f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("ready_order_apply_after_immediate", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_destroy_voice_removes_pending_immediate_automation(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 2
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 0.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_destroy(voice);
        voice = NULL;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("destroy_voice_removes_pending_immediate_automation", output, quantum,
                                           channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_volume_ramp_four_frames(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("volume_ramp_four_frames", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_volume_ramp_reaches_target_mid_block(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 2
    };
    static const float expected[quantum] = {0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("volume_ramp_mid_block", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_start_and_volume_ramp_same_batch(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 456;
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
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
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 4, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_start_ramp_batch", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("start_ramp_same_batch", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_volume_ramp_retarget_uses_current_value(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    static const float first_expected[quantum] = {0.0f, 0.125f, 0.25f, 0.375f};
    static const float second_expected[quantum * 2] = {0.5f, 0.375f, 0.25f, 0.125f,
                                                       0.0f, 0.0f,   0.0f,  0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum * 2];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("retarget_first_quantum", output, first_expected, quantum, 0.000001f);
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 0.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum * 2);
    }
    if (!failed) {
        failed = audio_test_check_equal("retarget_second_quantum", output, second_expected, quantum * 2, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_volume_cancels_active_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    static const float first_expected[quantum] = {0.0f, 0.125f, 0.25f, 0.375f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum * 2];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("cancel_ramp_first_quantum", output, first_expected, quantum, 0.000001f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.75f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum * 2);
    }
    if (!failed) {
        failed = audio_test_check_constant("set_volume_cancels_ramp", output, quantum * 2, channels, 0.75f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_volume_cancels_ready_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    const ForgeAudioBatchId batch_id = 904;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.75f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("set_volume_cancels_ready_deferred_ramp", output, quantum, channels,
                                           0.75f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_volume_does_not_delete_pending_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 906;
    static const float after_apply_expected[quantum] = {0.5f, 0.625f, 0.75f, 0.875f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.5f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("volume_pending_ramp_before_apply", output, quantum, channels,
                                           0.5f, 0.000001f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("volume_pending_ramp_after_apply", output, after_apply_expected,
                                        quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_ramp_advances_on_engine_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 3
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float volume = -1.0f;
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
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
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("stopped_source_silence", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - 1.0f) > 0.000001f) {
            fprintf(stderr, "stopped source ramp volume: expected 1.0, got %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("stopped_source_ramp_after_start", output, quantum, channels, 1.0f,
                                           0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_volume_ramp_waits_for_apply(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 789;
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float volume = -1.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("deferred_ramp_before_apply", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume) > 0.000001f) {
            fprintf(stderr, "deferred ramp advanced before apply: %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("deferred_ramp_after_apply", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_get_volume_reports_next_ramp_coefficient(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    float volume = -1.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum * 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - 0.5f) > 0.000001f) {
            fprintf(stderr, "mid-ramp get_volume: expected 0.5, got %.8f\n", volume);
            failed = 1;
        }
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume - 1.0f) > 0.000001f) {
            fprintf(stderr, "completed-ramp get_volume: expected 1.0, got %.8f\n", volume);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_batch_volume_ramp_order_last_command_wins(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 900;
    static const float expected[quantum] = {0.0f, 0.5f, 1.0f, 1.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 0.25f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, 2, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("batch_ramp_order_last_wins", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_batch_set_then_ramp_volume_order(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 901;
    static const float expected[quantum] = {0.5f, 0.625f, 0.75f, 0.875f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.5f, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("batch_set_then_ramp", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_batch_ramp_then_set_volume_order(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 902;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(voice, 1.0f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.5f, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("batch_ramp_then_set", output, quantum, channels, 0.5f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_submix_volume_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f};
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeSend send;
    ForgeSendList send_list;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, sample_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_voice_set_volume(submix, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                                 NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(submix, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("submix_volume_ramp", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_master_volume_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(harness.master, 0.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume_frames(harness.master, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("master_volume_ramp", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
