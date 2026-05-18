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

int test_source_fade_stop_stops_on_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 3
    };
    static const float expected[quantum] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f};
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
        failed = forge_source_voice_fade_stop(voice, 0.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("fade_stop_first_quantum", output, expected, quantum, 0.000001f);
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("fade_stop_second_quantum", output, quantum, channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_deferred_source_fade_stop_waits_for_apply(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 950;
    static const float expected[quantum] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f};
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
        failed = forge_source_voice_fade_stop(voice, 0.0f, 4, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("deferred_fade_stop_before_apply", output, quantum, channels, 1.0f,
                                           0.000001f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("deferred_fade_stop_after_apply", output, expected, quantum, 0.000001f);
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("deferred_fade_stop_after_stop", output, quantum, channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_source_fade_stop_zero_duration_stops_at_boundary(void) {
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
        failed = forge_source_voice_fade_stop(voice, 0.0f, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("fade_stop_zero_duration", output, quantum, channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_batch_fade_stop_then_set_volume_cancels_terminal_stop(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 960;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeVoiceState before;
    ForgeVoiceState after;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_source_voice_fade_stop(voice, 0.0f, quantum, batch_id) != 0;
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
        failed = audio_test_check_constant("fade_stop_then_set_first", output, quantum, channels, 0.5f, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &before, 0);
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("fade_stop_then_set_second", output, quantum, channels, 0.5f, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &after, 0);
        if (after.samples_played <= before.samples_played) {
            fprintf(stderr, "fade-stop terminal action was not cancelled: before=%llu after=%llu\n",
                    (unsigned long long)before.samples_played, (unsigned long long)after.samples_played);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_batch_ramp_then_fade_stop_order(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 961;
    static const float expected[quantum] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f};
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
        failed = forge_voice_ramp_volume(voice, 0.5f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_fade_stop(voice, 0.0f, 4, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("ramp_then_fade_stop", output, expected, quantum, 0.000001f);
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("ramp_then_fade_stop_after_stop", output, quantum, channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_batch_fade_stop_then_ramp_cancels_terminal_stop(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 962;
    static const float expected[quantum] = {1.0f, 0.875f, 0.75f, 0.625f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeVoiceState before;
    ForgeVoiceState after;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_source_voice_fade_stop(voice, 0.0f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 0.5f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("fade_stop_then_ramp_first", output, expected, quantum, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &before, 0);
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("fade_stop_then_ramp_second", output, quantum, channels, 0.5f, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &after, 0);
        if (after.samples_played <= before.samples_played) {
            fprintf(stderr, "fade-stop terminal action was not cancelled by ramp: before=%llu after=%llu\n",
                    (unsigned long long)before.samples_played, (unsigned long long)after.samples_played);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_fade_stop_completes_on_engine_timeline(void) {
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
        failed = forge_voice_set_volume(voice, 1.0f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_fade_stop(voice, 0.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("stopped_fade_stop_silence", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        forge_voice_get_volume(voice, &volume);
        if (audio_test_absf(volume) > 0.000001f) {
            fprintf(stderr, "stopped fade-stop volume: expected 0.0, got %.8f\n", volume);
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
        failed = audio_test_check_constant("stopped_fade_stop_after_start", output, quantum, channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_fade_stop_samples_played_stops_advancing(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 8,
        buffer_frames = quantum * 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeVoiceState before;
    ForgeVoiceState after;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_started_dc_source(&harness, &voice, source, buffer_frames, channels, sample_rate, 1.0f);
    }
    if (!failed) {
        failed = forge_source_voice_fade_stop(voice, 0.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &before, 0);
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &after, 0);
        if (after.samples_played != before.samples_played) {
            fprintf(stderr, "samples advanced after fade-stop: before=%llu after=%llu\n",
                    (unsigned long long)before.samples_played, (unsigned long long)after.samples_played);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
