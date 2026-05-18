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

    return failures == 0 ? 0 : 1;
}
