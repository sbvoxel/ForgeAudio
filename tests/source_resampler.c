/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "core_internal.h"
#include "simd_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct SourceHarness {
    ForgeAudioEngine audio;
    ForgeVoice voice;
    ForgeAudioFormat format;
    struct queued_buffer buffers[2];
} SourceHarness;

static uint32_t ceil_to_u32(double value) {
    uint32_t whole = (uint32_t)value;
    return ((double)whole < value) ? whole + 1 : whole;
}

static void init_harness(SourceHarness *harness, uint32_t decode_frames, uint32_t resample_frames) {
    forge_zero(harness, sizeof(*harness));

    harness->audio.malloc_func = malloc;
    harness->audio.free_func = free;
    harness->audio.realloc_func = realloc;
    harness->audio.decodeSamples = (decode_frames + EXTRA_DECODE_PADDING);
    harness->audio.resampleSamples = resample_frames;
    harness->audio.decodeCache = (float *)malloc(sizeof(float) * harness->audio.decodeSamples);
    harness->audio.resampleCache = (float *)malloc(sizeof(float) * harness->audio.resampleSamples);

    harness->format.format_tag = FORGE_AUDIO_FORMAT_IEEE_FLOAT;
    harness->format.channels = 1;
    harness->format.sample_rate = 36000;
    harness->format.bits_per_sample = 32;
    harness->format.block_align = sizeof(float);
    harness->format.average_bytes_per_second = harness->format.sample_rate * harness->format.block_align;

    harness->voice.audio = &harness->audio;
    harness->voice.type = FORGE_AUDIO_VOICE_SOURCE;
    harness->voice.src.format = &harness->format;
    harness->voice.src.decodeSamples = decode_frames;
    harness->voice.src.resampleSamples = resample_frames;
    harness->voice.src.resampleStep = DOUBLE_TO_FIXED(0.75);
    harness->voice.src.queued_buffers = harness->buffers;
}

static void destroy_harness(SourceHarness *harness) {
    free(harness->audio.decodeCache);
    free(harness->audio.resampleCache);
}

static void set_buffer(SourceHarness *harness, size_t index, const float *samples, uint32_t frames) {
    struct queued_buffer *buffer = &harness->buffers[index];

    buffer->buffer.audio_bytes = frames * harness->format.block_align;
    buffer->buffer.audio_data = (const uint8_t *)samples;
    buffer->buffer.play_begin = 0;
    buffer->buffer.play_length = frames;
    buffer->play_bytes = frames * harness->format.block_align;
}

static ForgeAudioTestSourceResampleResult render_buffers(const float *first, uint32_t first_frames, const float *second,
                                                         uint32_t second_frames, float *output, uint32_t output_frames) {
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;

    init_harness(&harness, 8, output_frames);
    set_buffer(&harness, 0, first, first_frames);
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    if (second != NULL) {
        set_buffer(&harness, 1, second, second_frames);
        harness.voice.src.queued_buffer_count = 2;
        harness.voice.src.queued_buffers_capacity = 2;
    }

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    destroy_harness(&harness);
    return result;
}

static int render_buffer_passes(const char *name, const float *first, uint32_t first_frames, const float *second,
                                uint32_t second_frames, uint64_t resample_step, uint32_t pass_frames,
                                uint32_t pass_count, float *output) {
    SourceHarness harness;
    int failed = 0;

    init_harness(&harness, 16, pass_frames);
    harness.voice.src.resampleStep = resample_step;
    set_buffer(&harness, 0, first, first_frames);
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    if (second != NULL) {
        set_buffer(&harness, 1, second, second_frames);
        harness.voice.src.queued_buffer_count = 2;
        harness.voice.src.queued_buffers_capacity = 2;
    }

    for (uint32_t i = 0; i < pass_count; i += 1) {
        ForgeAudioTestSourceResampleResult result =
            forge_audio_test_decode_resample_source(&harness.voice, output + (i * pass_frames));

        if (result.resampled_frames != pass_frames) {
            fprintf(stderr, "%s pass %u resampled_frames: expected %u, got %u\n", name, i, pass_frames,
                    result.resampled_frames);
            failed = 1;
            break;
        }
    }

    destroy_harness(&harness);
    return failed;
}

static void fill_ramp(float *samples, uint32_t count) {
    for (uint32_t i = 0; i < count; i += 1) {
        samples[i] = (float)i;
    }
}

static int check_values(const char *name, const float *actual, const float *expected, uint32_t count) {
    int failed = 0;

    for (uint32_t i = 0; i < count; i += 1) {
        float delta = actual[i] - expected[i];
        if (delta < 0.0f) {
            delta = -delta;
        }
        if (delta > 0.000001f) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected[i], actual[i]);
            failed = 1;
        }
    }

    return failed;
}

static int test_split_fast_fractional_ratio_matches_contiguous_across_passes(void) {
    float samples[24];
    float contiguous_out[12] = {0};
    float split_out[12] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(1.5);
    const uint32_t pass_frames = 3;
    const uint32_t pass_count = 4;
    int failed = 0;

    fill_ramp(samples, 24);

    failed |= render_buffer_passes("fast-contiguous", samples, 24, NULL, 0, resample_step, pass_frames, pass_count,
                                   contiguous_out);
    failed |= render_buffer_passes("fast-split", samples, 4, samples + 4, 20, resample_step, pass_frames, pass_count,
                                   split_out);
    failed |= check_values("fast_split_vs_contiguous", split_out, contiguous_out, pass_frames * pass_count);

    return failed;
}

static int test_split_source_rate_ratio_matches_contiguous_across_passes(void) {
    float samples[32];
    float contiguous_out[28] = {0};
    float split_out[28] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(44100.0 / 48000.0);
    const uint32_t pass_frames = 7;
    const uint32_t pass_count = 4;
    int failed = 0;

    fill_ramp(samples, 32);

    failed |= render_buffer_passes("rate-contiguous", samples, 32, NULL, 0, resample_step, pass_frames, pass_count,
                                   contiguous_out);
    failed |= render_buffer_passes("rate-split", samples, 6, samples + 6, 26, resample_step, pass_frames, pass_count,
                                   split_out);
    failed |= check_values("rate_split_vs_contiguous", split_out, contiguous_out, pass_frames * pass_count);

    return failed;
}

static int test_split_ramp_matches_contiguous(void) {
    static const float contiguous[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    static const float split_a[] = {0.0f, 1.0f, 2.0f};
    static const float split_b[] = {3.0f, 4.0f};
    static const float expected[] = {0.0f, 0.75f, 1.5f, 2.25f};
    float contiguous_out[4] = {0};
    float split_out[4] = {0};
    ForgeAudioTestSourceResampleResult contiguous_result;
    ForgeAudioTestSourceResampleResult split_result;
    int failed = 0;

    contiguous_result = render_buffers(contiguous, 5, NULL, 0, contiguous_out, 4);
    split_result = render_buffers(split_a, 3, split_b, 2, split_out, 4);

    if (contiguous_result.resampled_frames != 4) {
        fprintf(stderr, "contiguous resampled_frames: expected 4, got %u\n", contiguous_result.resampled_frames);
        failed = 1;
    }
    if (split_result.resampled_frames != 4) {
        fprintf(stderr, "split resampled_frames: expected 4, got %u\n", split_result.resampled_frames);
        failed = 1;
    }
    if (split_result.cur_buffer_offset != 3 || split_result.queued_buffer_count != 2) {
        fprintf(stderr, "split padding peek mutated playback state: offset=%u queued=%zu\n",
                split_result.cur_buffer_offset, split_result.queued_buffer_count);
        failed = 1;
    }

    failed |= check_values("contiguous_out", contiguous_out, expected, 4);
    failed |= check_values("split_out", split_out, expected, 4);
    failed |= check_values("split_vs_contiguous", split_out, contiguous_out, 4);

    return failed;
}

static int test_one_padding_frame_is_preserved(void) {
    static const float samples[] = {0.0f, 1.0f, 2.0f, 3.0f};
    static const float expected[] = {0.0f, 0.75f, 1.5f, 2.25f};
    float output[4] = {0};
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    result = render_buffers(samples, 4, NULL, 0, output, 4);
    if (result.resampled_frames != 4) {
        fprintf(stderr, "one-padding resampled_frames: expected 4, got %u\n", result.resampled_frames);
        failed = 1;
    }
    if (result.cur_buffer_offset != 3 || result.queued_buffer_count != 1) {
        fprintf(stderr, "one-padding peek mutated playback state: offset=%u queued=%zu\n", result.cur_buffer_offset,
                result.queued_buffer_count);
        failed = 1;
    }

    failed |= check_values("one_padding_out", output, expected, 4);
    return failed;
}

static int test_padding_peeks_across_loop_boundary(void) {
    static const float samples[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    static const float expected[] = {0.0f, 0.75f, 1.5f, 1.75f};
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    float output[4] = {0};
    int failed = 0;

    init_harness(&harness, 8, 4);
    set_buffer(&harness, 0, samples, 5);
    harness.buffers[0].buffer.loop_begin = 1;
    harness.buffers[0].buffer.loop_length = 2;
    harness.buffers[0].buffer.loop_count = 1;
    harness.buffers[0].loop_bytes = 2 * harness.format.block_align;
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    if (result.resampled_frames != 4) {
        fprintf(stderr, "loop-boundary resampled_frames: expected 4, got %u\n", result.resampled_frames);
        failed = 1;
    }
    if (result.cur_buffer_offset != 3 || result.queued_buffer_count != 1) {
        fprintf(stderr, "loop-boundary peek mutated playback state: offset=%u queued=%zu\n",
                result.cur_buffer_offset, result.queued_buffer_count);
        failed = 1;
    }

    failed |= check_values("loop_boundary_out", output, expected, 4);

    destroy_harness(&harness);
    return failed;
}

static int test_decode_sizing_covers_output_rate(void) {
    const uint32_t update_size = 1024;
    const uint32_t master_rate = 48000;
    const uint32_t output_rate = 44100;
    const uint32_t source_rate = 200000;
    const uint32_t channels = 1;
    const float max_ratio = FORGE_AUDIO_MAX_FREQ_RATIO;
    uint32_t resample_samples;
    uint32_t current_decode_samples;
    uint64_t max_step;
    uint64_t required_decode_samples;

    resample_samples = ceil_to_u32((double)update_size * (double)output_rate / (double)master_rate);
    current_decode_samples =
        forge_audio_test_source_decode_frame_count(resample_samples, max_ratio, source_rate, output_rate);
    max_step = DOUBLE_TO_FIXED((double)max_ratio * (double)source_rate / (double)output_rate);
    required_decode_samples =
        (((uint64_t)resample_samples * max_step) + FIXED_FRACTION_MASK + FIXED_FRACTION_MASK) >> FIXED_PRECISION;

    if ((uint64_t)current_decode_samples < required_decode_samples) {
        fprintf(stderr,
                "decode sizing: current formula provides %u frames, but %llu frames are required "
                "for resampleSamples=%u\n",
                current_decode_samples, (unsigned long long)required_decode_samples, resample_samples);
        return 1;
    }
    if ((uint64_t)(current_decode_samples + EXTRA_DECODE_PADDING) * channels <= required_decode_samples * channels) {
        fprintf(stderr, "decode sizing allocation leaves no interpolation padding\n");
        return 1;
    }

    return 0;
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

    failures += run_test("split_fast_fractional_ratio_matches_contiguous_across_passes",
                         test_split_fast_fractional_ratio_matches_contiguous_across_passes);
    failures += run_test("split_source_rate_ratio_matches_contiguous_across_passes",
                         test_split_source_rate_ratio_matches_contiguous_across_passes);
    failures += run_test("split_ramp_matches_contiguous", test_split_ramp_matches_contiguous);
    failures += run_test("one_padding_frame_is_preserved", test_one_padding_frame_is_preserved);
    failures += run_test("padding_peeks_across_loop_boundary", test_padding_peeks_across_loop_boundary);
    failures += run_test("decode_sizing_covers_output_rate", test_decode_sizing_covers_output_rate);

    return failures == 0 ? 0 : 1;
}
