/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "core_internal.h"

#include <stdio.h>
#include <stdlib.h>

static float absf(float value) {
    return value < 0.0f ? -value : value;
}

static ForgeAudioFormat float_format(uint32_t channels, uint32_t sample_rate) {
    ForgeAudioFormat format;

    forge_zero(&format, sizeof(format));
    format.format_tag = FORGE_AUDIO_FORMAT_IEEE_FLOAT;
    format.channels = (uint16_t)channels;
    format.sample_rate = sample_rate;
    format.bits_per_sample = 32;
    format.block_align = (uint16_t)(channels * sizeof(float));
    format.average_bytes_per_second = format.sample_rate * format.block_align;
    return format;
}

static int create_virtual_engine(ForgeAudioEngine **audio, ForgeMasterVoice **master, uint32_t channels,
                                 uint32_t sample_rate, uint32_t update_size) {
    ForgeResult result;

    result = forge_audio_create(audio, 0);
    if (result != 0) {
        fprintf(stderr, "forge_audio_create failed: %d\n", result);
        return 1;
    }

    result = forge_audio_test_create_virtual_master_voice(*audio, master, channels, sample_rate, update_size, NULL);
    if (result != 0) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed: %d\n", result);
        forge_audio_destroy(*audio);
        *audio = NULL;
        return 1;
    }

    return 0;
}

static int create_float_source(ForgeAudioEngine *audio, ForgeSourceVoice **voice, uint32_t channels,
                               uint32_t sample_rate) {
    ForgeAudioFormat format = float_format(channels, sample_rate);
    ForgeResult result =
        forge_audio_create_source_voice(audio, voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);

    if (result != 0) {
        fprintf(stderr, "forge_audio_create_source_voice failed: %d\n", result);
        return 1;
    }

    return 0;
}

static int submit_float_buffer(ForgeSourceVoice *voice, const float *samples, uint32_t frames, uint32_t channels) {
    ForgeBuffer buffer;
    ForgeResult result;

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_bytes = frames * channels * sizeof(float);
    buffer.audio_data = (const uint8_t *)samples;

    result = forge_source_voice_submit_buffer(voice, &buffer, NULL);
    if (result != 0) {
        fprintf(stderr, "forge_source_voice_submit_buffer failed: %d\n", result);
        return 1;
    }

    return 0;
}

static int render_checked(ForgeAudioEngine *audio, float *output, uint32_t frames) {
    ForgeResult result = forge_audio_test_render(audio, output, frames);

    if (result != 0) {
        fprintf(stderr, "forge_audio_test_render failed: %d\n", result);
        return 1;
    }

    return 0;
}

static int check_constant(const char *name, const float *samples, uint32_t frames, uint32_t channels, float expected,
                          float epsilon) {
    uint32_t count = frames * channels;

    for (uint32_t i = 0; i < count; i += 1) {
        if (absf(samples[i] - expected) > epsilon) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected, samples[i]);
            return 1;
        }
    }

    return 0;
}

static int check_equal(const char *name, const float *actual, const float *expected, uint32_t count, float epsilon) {
    for (uint32_t i = 0; i < count; i += 1) {
        if (absf(actual[i] - expected[i]) > epsilon) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected[i], actual[i]);
            return 1;
        }
    }

    return 0;
}

static int test_virtual_silence_smoke(void) {
    static const uint32_t sample_rates[] = {44100, 48000};
    static const uint32_t quanta[] = {64, 256, 512};
    int failed = 0;

    for (uint32_t rate_index = 0; rate_index < 2; rate_index += 1) {
        for (uint32_t quantum_index = 0; quantum_index < 3; quantum_index += 1) {
            const uint32_t channels = 2;
            uint32_t sample_rate = sample_rates[rate_index];
            uint32_t quantum = quanta[quantum_index];
            uint32_t frames = quantum * 2;
            ForgeAudioEngine *audio = NULL;
            ForgeMasterVoice *master = NULL;
            float *output = (float *)malloc(sizeof(float) * frames * channels);
            uint32_t quantum_numerator = 0;
            uint32_t quantum_denominator = 0;

            if (output == NULL) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }

            for (uint32_t i = 0; i < frames * channels; i += 1) {
                output[i] = 99.0f;
            }

            failed |= create_virtual_engine(&audio, &master, channels, sample_rate, quantum);
            if (!failed) {
                forge_audio_get_processing_quantum(audio, &quantum_numerator, &quantum_denominator);
                if (quantum_numerator != quantum || quantum_denominator != sample_rate) {
                    fprintf(stderr, "quantum: expected %u/%u, got %u/%u\n", quantum, sample_rate, quantum_numerator,
                            quantum_denominator);
                    failed = 1;
                }
                failed |= render_checked(audio, output, frames);
                failed |= check_constant("silence", output, frames, channels, 0.0f, 0.0f);
            }

            if (audio != NULL) {
                forge_audio_destroy(audio);
            }
            free(output);

            if (failed) {
                return 1;
            }
        }
    }

    return failed;
}

static int test_public_source_dc_render(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64,
        render_frames = quantum * 2,
        buffer_frames = quantum * 3
    };
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeVoiceState state;
    float source[buffer_frames];
    float output[render_frames];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.25f;
    }

    failed |= create_virtual_engine(&audio, &master, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_float_source(audio, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = render_checked(audio, output, render_frames);
    }
    if (!failed) {
        failed = check_constant("dc_output", output, render_frames, channels, 0.25f, 0.000001f);
    }

    if (!failed) {
        forge_source_voice_get_state(voice, &state, 0);
        if (state.samples_played != render_frames) {
            fprintf(stderr, "samples_played: expected %u, got %llu\n", render_frames,
                    (unsigned long long)state.samples_played);
            failed = 1;
        }
    }

    if (audio != NULL) {
        forge_audio_destroy(audio);
    }
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
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
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

    failed |= create_virtual_engine(&audio, &master, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_float_source(audio, &voice_a, channels, sample_rate);
    }
    if (!failed) {
        failed = create_float_source(audio, &voice_b, channels, sample_rate);
    }
    if (!failed) {
        failed = submit_float_buffer(voice_a, source_a, buffer_frames, channels);
    }
    if (!failed) {
        failed = submit_float_buffer(voice_b, source_b, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice_a, 0, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_source_voice_start(voice_b, 0, batch_id) != 0;
    }

    if (!failed) {
        failed = render_checked(audio, output, quantum);
    }
    if (!failed) {
        failed = check_constant("before_batch_apply", output, quantum, channels, 0.0f, 0.0f);
    }

    if (!failed) {
        failed = forge_audio_apply_batch(audio, batch_id) != 0;
    }
    if (!failed) {
        failed = render_checked(audio, output, quantum);
    }
    if (!failed) {
        failed = check_constant("after_batch_apply", output, quantum, channels, 0.75f, 0.000001f);
    }

    if (audio != NULL) {
        forge_audio_destroy(audio);
    }
    return failed;
}

static int render_one_buffer_shape(const float *first, uint32_t first_frames, const float *second,
                                   uint32_t second_frames, float *output, uint32_t render_frames) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 64
    };
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    int failed = 0;

    failed |= create_virtual_engine(&audio, &master, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_float_source(audio, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = submit_float_buffer(voice, first, first_frames, channels);
    }
    if (!failed && second != NULL && second_frames != 0) {
        failed = submit_float_buffer(voice, second, second_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = render_checked(audio, output, render_frames);
    }

    if (audio != NULL) {
        forge_audio_destroy(audio);
    }
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
        failed = check_equal("split_vs_contiguous", split_output, contiguous_output, frames, 0.000001f);
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
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 1.0f;
    }

    failed |= create_virtual_engine(&audio, &master, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_float_source(audio, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }

    if (!failed) {
        failed = render_checked(audio, output, quantum);
    }
    if (!failed) {
        failed = check_constant("initial_volume", output, quantum, channels, 1.0f, 0.000001f);
    }

    if (!failed) {
        failed = forge_voice_set_volume(voice, 0.25f, batch_id) != 0;
    }
    if (!failed) {
        failed = render_checked(audio, output, quantum);
    }
    if (!failed) {
        failed = check_constant("before_volume_batch_apply", output, quantum, channels, 1.0f, 0.000001f);
    }

    if (!failed) {
        failed = forge_audio_apply_batch(audio, batch_id) != 0;
    }
    if (!failed) {
        failed = render_checked(audio, output, quantum);
    }
    if (!failed) {
        failed = check_constant("after_volume_batch_apply", output, quantum, channels, 0.25f, 0.000001f);
    }

    if (audio != NULL) {
        forge_audio_destroy(audio);
    }
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

    return failures == 0 ? 0 : 1;
}
