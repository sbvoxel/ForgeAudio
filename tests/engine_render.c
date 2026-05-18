/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "audio_render_harness.h"

#include <stdio.h>
#include <stdlib.h>

static int test_virtual_silence_smoke(void) {
    static const uint32_t sample_rates[] = {44100, 48000};
    static const uint32_t quanta[] = {64, 256, 512};

    for (uint32_t rate_index = 0; rate_index < 2; rate_index += 1) {
        for (uint32_t quantum_index = 0; quantum_index < 3; quantum_index += 1) {
            const uint32_t channels = 2;
            uint32_t sample_rate = sample_rates[rate_index];
            uint32_t quantum = quanta[quantum_index];
            uint32_t frames = quantum * 2;
            AudioRenderHarness harness;
            float *output = (float *)malloc(sizeof(float) * frames * channels);
            uint32_t quantum_numerator = 0;
            uint32_t quantum_denominator = 0;
            int failed = 0;

            if (output == NULL) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }

            for (uint32_t i = 0; i < frames * channels; i += 1) {
                output[i] = 99.0f;
            }

            failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
            if (!failed) {
                forge_audio_get_processing_quantum(harness.audio, &quantum_numerator, &quantum_denominator);
                if (quantum_numerator != quantum || quantum_denominator != sample_rate) {
                    fprintf(stderr, "quantum: expected %u/%u, got %u/%u\n", quantum, sample_rate, quantum_numerator,
                            quantum_denominator);
                    failed = 1;
                }
            }
            if (!failed) {
                failed = audio_render_harness_render(&harness, output, frames);
            }
            if (!failed) {
                failed = audio_test_check_constant("silence", output, frames, channels, 0.0f, 0.0f);
            }

            audio_render_harness_destroy(&harness);
            free(output);

            if (failed) {
                return 1;
            }
        }
    }

    return 0;
}

static int test_public_source_dc_render(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        render_frames = quantum * 2,
        buffer_frames = quantum * 3
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeVoiceState state;
    float source[buffer_frames];
    float output[render_frames];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, render_frames);
    }
    if (!failed) {
        failed = audio_test_check_constant("dc_output", output, render_frames, channels, 0.25f, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_state(voice, &state, 0);
        if (state.samples_played != render_frames) {
            fprintf(stderr, "samples_played: expected %u, got %llu\n", render_frames,
                    (unsigned long long)state.samples_played);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_deferred_batch_start_timing(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        buffer_frames = quantum * 2
    };
    const ForgeAudioBatchId batch_id = 42;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice_a = NULL;
    ForgeSourceVoice *voice_b = NULL;
    float source_a[buffer_frames];
    float source_b[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source_a[i] = 1.0f;
        source_b[i] = -0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice_a, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice_b, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice_a, source_a, buffer_frames, channels);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice_b, source_b, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice_a, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_start(voice_b, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_batch_apply", output, quantum, channels, 0.0f, 0.0f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("after_batch_apply", output, quantum, channels, 0.75f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int render_one_buffer_shape(const float *first, uint32_t first_frames, const float *second,
                                   uint32_t second_frames, float *output, uint32_t render_frames) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, first, first_frames, channels);
    }
    if (!failed && second != NULL && second_frames != 0) {
        failed = audio_render_harness_submit_float_buffer(voice, second, second_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, render_frames);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_split_buffer_equals_contiguous(void) {
    enum {
        quantum = 64,
        frames = quantum * 2,
        split = 37
    };
    float contiguous[frames];
    float contiguous_output[frames];
    float split_output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        contiguous[i] = (float)i / (float)frames;
    }

    failed = render_one_buffer_shape(contiguous, frames, NULL, 0, contiguous_output, frames);
    if (!failed) {
        failed = render_one_buffer_shape(contiguous, split, contiguous + split, frames - split, split_output, frames);
    }
    if (!failed) {
        failed = audio_test_check_equal("split_vs_contiguous", split_output, contiguous_output, frames, 0.000001f);
    }

    return failed;
}

static int test_deferred_volume_boundary(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 77;
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
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("initial_volume", output, quantum, channels, 1.0f, 0.000001f);
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.25f, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_volume_batch_apply", output, quantum, channels, 1.0f, 0.000001f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("after_volume_batch_apply", output, quantum, channels, 0.25f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_immediate_volume_before_deferred_apply(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 88;
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
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.25f, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.5f, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("immediate_volume_before_apply", output, quantum, channels, 0.5f, 0.000001f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("deferred_volume_after_apply", output, quantum, channels, 0.25f, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_deferred_stop_boundary(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        buffer_frames = quantum * 4
    };
    const ForgeAudioBatchId batch_id = 99;
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
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_stop_queue", output, quantum, channels, 1.0f, 0.000001f);
    }
    if (!failed) {
        failed = forge_source_voice_stop(voice, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("before_stop_apply", output, quantum, channels, 1.0f, 0.000001f);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_constant("after_stop_apply", output, quantum, channels, 0.0f, 0.0f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_rate_change_continuity_smoke(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        buffer_frames = quantum * 8
    };
    const ForgeAudioBatchId batch_id = 123;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum * 3];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = (float)i / 256.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_source_voice_set_rate(voice, 0.5f, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output + quantum, quantum);
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output + quantum * 2, quantum);
    }
    if (!failed) {
        failed = audio_test_check_max_adjacent_delta("rate_change_output", output, quantum * 3, 0.01f);
    }
    if (!failed && audio_test_absf(output[quantum * 2] - output[(quantum * 2) - 1]) > 0.01f) {
        fprintf(stderr, "rate change boundary discontinuity: before %.8f after %.8f\n", output[(quantum * 2) - 1],
                output[quantum * 2]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_render_api_rejects_invalid_inputs(void) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    float output[128];
    int failed = 0;

    if (forge_audio_create(&audio, 0) != 0) {
        fprintf(stderr, "forge_audio_create failed\n");
        return 1;
    }

    if (forge_audio_test_render(audio, output, 64) != ForgeResultInvalidArgument) {
        fprintf(stderr, "render without master should fail\n");
        failed = 1;
    }
    if (!failed && forge_audio_test_create_virtual_master_voice(audio, &master, 0, 48000, 64, NULL) !=
                       ForgeResultInvalidArgument) {
        fprintf(stderr, "virtual master accepted zero channels\n");
        failed = 1;
    }
    if (!failed && forge_audio_test_create_virtual_master_voice(audio, &master, 1, 0, 64, NULL) !=
                       ForgeResultInvalidArgument) {
        fprintf(stderr, "virtual master accepted zero sample rate\n");
        failed = 1;
    }
    if (!failed && forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 0, NULL) !=
                       ForgeResultInvalidArgument) {
        fprintf(stderr, "virtual master accepted zero quantum\n");
        failed = 1;
    }
    if (!failed && forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != 0) {
        fprintf(stderr, "virtual master creation failed\n");
        failed = 1;
    }
    if (!failed && forge_audio_test_render(audio, output, 65) != ForgeResultInvalidArgument) {
        fprintf(stderr, "render accepted non-quantum frame count\n");
        failed = 1;
    }
    if (!failed && forge_audio_test_render(audio, NULL, 64) != ForgeResultInvalidArgument) {
        fprintf(stderr, "render accepted null output\n");
        failed = 1;
    }

    forge_audio_destroy(audio);
    return failed;
}

static int create_started_dc_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, float *source,
                                    uint32_t buffer_frames, uint32_t channels, uint32_t sample_rate,
                                    float source_value) {
    int failed;

    for (uint32_t i = 0; i < buffer_frames * channels; i += 1) {
        source[i] = source_value;
    }

    failed = audio_render_harness_create_float_source(harness, voice, channels, sample_rate);
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(*voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(*voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }

    return failed;
}

static int test_volume_ramp_four_frames(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_volume_ramp_reaches_target_mid_block(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_deferred_start_and_volume_ramp_same_batch(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, 4, batch_id) != 0;
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

static int test_volume_ramp_retarget_uses_current_value(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("retarget_first_quantum", output, first_expected, quantum, 0.000001f);
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 0.0f, 4, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_set_volume_cancels_active_ramp(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_stopped_source_ramp_advances_on_engine_timeline(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_deferred_volume_ramp_waits_for_apply(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, quantum, batch_id) != 0;
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

static int test_get_volume_reports_next_ramp_coefficient(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, quantum * 2, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_batch_volume_ramp_order_last_command_wins(void) {
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
        failed = forge_voice_ramp_volume(voice, 0.25f, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_volume(voice, 1.0f, 2, batch_id) != 0;
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

static int test_batch_set_then_ramp_volume_order(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, quantum, batch_id) != 0;
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

static int test_batch_ramp_then_set_volume_order(void) {
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
        failed = forge_voice_ramp_volume(voice, 1.0f, quantum, batch_id) != 0;
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

static int test_submix_volume_ramp(void) {
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
        failed = forge_voice_ramp_volume(submix, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_master_volume_ramp(void) {
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
        failed = forge_voice_ramp_volume(harness.master, 1.0f, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
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

static int test_source_fade_stop_stops_on_timeline(void) {
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

static int test_deferred_source_fade_stop_waits_for_apply(void) {
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

static int test_source_fade_stop_zero_duration_stops_at_boundary(void) {
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

static int run_test(const char *name, int (*test_func)(void)) {
    int failed = test_func();

    if (failed) {
        fprintf(stderr, "FAIL %s\n", name);
        return 1;
    }

    printf("PASS %s\n", name);
    return 0;
}

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
    failures += run_test("volume_ramp_four_frames", test_volume_ramp_four_frames);
    failures += run_test("volume_ramp_reaches_target_mid_block", test_volume_ramp_reaches_target_mid_block);
    failures += run_test("deferred_start_and_volume_ramp_same_batch", test_deferred_start_and_volume_ramp_same_batch);
    failures += run_test("volume_ramp_retarget_uses_current_value", test_volume_ramp_retarget_uses_current_value);
    failures += run_test("set_volume_cancels_active_ramp", test_set_volume_cancels_active_ramp);
    failures += run_test("stopped_source_ramp_advances_on_engine_timeline",
                         test_stopped_source_ramp_advances_on_engine_timeline);
    failures += run_test("deferred_volume_ramp_waits_for_apply", test_deferred_volume_ramp_waits_for_apply);
    failures += run_test("get_volume_reports_next_ramp_coefficient", test_get_volume_reports_next_ramp_coefficient);
    failures += run_test("batch_volume_ramp_order_last_command_wins", test_batch_volume_ramp_order_last_command_wins);
    failures += run_test("batch_set_then_ramp_volume_order", test_batch_set_then_ramp_volume_order);
    failures += run_test("batch_ramp_then_set_volume_order", test_batch_ramp_then_set_volume_order);
    failures += run_test("submix_volume_ramp", test_submix_volume_ramp);
    failures += run_test("master_volume_ramp", test_master_volume_ramp);
    failures += run_test("source_fade_stop_stops_on_timeline", test_source_fade_stop_stops_on_timeline);
    failures += run_test("deferred_source_fade_stop_waits_for_apply", test_deferred_source_fade_stop_waits_for_apply);
    failures += run_test("source_fade_stop_zero_duration_stops_at_boundary",
                         test_source_fade_stop_zero_duration_stops_at_boundary);

    return failures == 0 ? 0 : 1;
}
