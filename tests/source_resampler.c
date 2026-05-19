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

static void init_harness_channels(SourceHarness *harness, uint32_t decode_frames, uint32_t resample_frames,
                                  uint32_t channels) {
    forge_zero(harness, sizeof(*harness));

    harness->audio.malloc_func = malloc;
    harness->audio.free_func = free;
    harness->audio.realloc_func = realloc;
    harness->audio.decodeSamples =
        decode_frames + SOURCE_SINC8_LEFT_PADDING_FRAMES + SOURCE_SINC8_DECODE_PADDING_FRAMES;
    harness->audio.resampleSamples = resample_frames;
    harness->audio.decodeCache = (float *)malloc(sizeof(float) * harness->audio.decodeSamples * channels);
    harness->audio.resampleCache = (float *)malloc(sizeof(float) * harness->audio.resampleSamples * channels);

    harness->format.format_tag = FORGE_AUDIO_FORMAT_IEEE_FLOAT;
    harness->format.channels = channels;
    harness->format.sample_rate = 36000;
    harness->format.bits_per_sample = 32;
    harness->format.block_align = (uint16_t)(sizeof(float) * channels);
    harness->format.average_bytes_per_second = harness->format.sample_rate * harness->format.block_align;

    harness->voice.audio = &harness->audio;
    harness->voice.type = FORGE_AUDIO_VOICE_SOURCE;
    harness->voice.src.format = &harness->format;
    harness->voice.src.decodeSamples = decode_frames;
    harness->voice.src.resampleSamples = resample_frames;
    harness->voice.src.resampleStep = DOUBLE_TO_FIXED(0.75);
    harness->voice.src.resamplerQuality = ForgeAudioResamplerCubic;
    harness->voice.src.queued_buffers = harness->buffers;
    harness->voice.src.resampleHistoryCapacity = SOURCE_SINC8_LEFT_PADDING_FRAMES;
    harness->voice.src.resampleHistory =
        (float *)malloc(sizeof(float) * harness->voice.src.resampleHistoryCapacity * channels);
    forge_zero(harness->voice.src.resampleHistory,
               sizeof(float) * harness->voice.src.resampleHistoryCapacity * channels);
}

static void init_harness(SourceHarness *harness, uint32_t decode_frames, uint32_t resample_frames) {
    init_harness_channels(harness, decode_frames, resample_frames, 1);
}

static void destroy_harness(SourceHarness *harness) {
    free(harness->audio.decodeCache);
    free(harness->audio.resampleCache);
    free(harness->voice.src.resampleHistory);
}

static void set_buffer(SourceHarness *harness, size_t index, const float *samples, uint32_t frames) {
    struct queued_buffer *buffer = &harness->buffers[index];

    buffer->buffer.audio_bytes = frames * harness->format.block_align;
    buffer->buffer.audio_data = (const uint8_t *)samples;
    buffer->buffer.play_begin = 0;
    buffer->buffer.play_length = frames;
    buffer->play_bytes = frames * harness->format.block_align;
}

static void set_resampler_quality(SourceHarness *harness, ForgeAudioSourceResamplerQuality quality);

static ForgeAudioTestSourceResampleResult render_buffers(const float *first, uint32_t first_frames, const float *second,
                                                         uint32_t second_frames, float *output, uint32_t output_frames) {
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;

    init_harness(&harness, 8, output_frames);
    set_resampler_quality(&harness, FORGE_AUDIO_SOURCE_RESAMPLER_LINEAR);
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

static void set_resampler_quality(SourceHarness *harness, ForgeAudioSourceResamplerQuality quality) {
    forge_assert(quality == ForgeAudioResamplerLinear || quality == ForgeAudioResamplerCubic ||
                 quality == FA_AUDIO_SOURCE_RESAMPLER_SINC8);
    harness->voice.src.resamplerQuality = quality;
}

static ForgeAudioTestSourceResampleResult render_buffers_channels_quality(
    const float *first, uint32_t first_frames, const float *second, uint32_t second_frames, uint32_t channels,
    uint64_t resample_step, ForgeAudioSourceResamplerQuality quality, float *output, uint32_t output_frames) {
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    uint32_t decode_frames = (uint32_t)(((uint64_t)output_frames * resample_step + FIXED_FRACTION_MASK) >>
                                        FIXED_PRECISION);

    init_harness_channels(&harness, decode_frames + 2, output_frames, channels);
    harness.voice.src.resampleStep = resample_step;
    set_resampler_quality(&harness, quality);
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

static ForgeAudioTestSourceResampleResult render_buffers_channels(const float *first, uint32_t first_frames,
                                                                  const float *second, uint32_t second_frames,
                                                                  uint32_t channels, uint64_t resample_step,
                                                                  float *output, uint32_t output_frames) {
    return render_buffers_channels_quality(first, first_frames, second, second_frames, channels, resample_step,
                                           FORGE_AUDIO_SOURCE_RESAMPLER_LINEAR, output, output_frames);
}

static int render_buffer_passes_channels_quality(const char *name, const float *first, uint32_t first_frames,
                                                 const float *second, uint32_t second_frames, uint32_t channels,
                                                 uint64_t resample_step, ForgeAudioSourceResamplerQuality quality,
                                                 uint32_t pass_frames, uint32_t pass_count, float *output) {
    SourceHarness harness;
    int failed = 0;
    uint32_t decode_frames = (uint32_t)(((uint64_t)pass_frames * resample_step + FIXED_FRACTION_MASK) >>
                                        FIXED_PRECISION);

    init_harness_channels(&harness, decode_frames + 2, pass_frames, channels);
    harness.voice.src.resampleStep = resample_step;
    set_resampler_quality(&harness, quality);
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
            forge_audio_test_decode_resample_source(&harness.voice, output + (i * pass_frames * channels));

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

static int render_buffer_passes_channels(const char *name, const float *first, uint32_t first_frames,
                                         const float *second, uint32_t second_frames, uint32_t channels,
                                         uint64_t resample_step, uint32_t pass_frames, uint32_t pass_count,
                                         float *output) {
    return render_buffer_passes_channels_quality(name, first, first_frames, second, second_frames, channels,
                                                 resample_step, FORGE_AUDIO_SOURCE_RESAMPLER_LINEAR, pass_frames,
                                                 pass_count, output);
}

static int render_buffer_passes(const char *name, const float *first, uint32_t first_frames, const float *second,
                                uint32_t second_frames, uint64_t resample_step, uint32_t pass_frames,
                                uint32_t pass_count, float *output) {
    return render_buffer_passes_channels(name, first, first_frames, second, second_frames, 1, resample_step,
                                         pass_frames, pass_count, output);
}

static void fill_ramp(float *samples, uint32_t count) {
    for (uint32_t i = 0; i < count; i += 1) {
        samples[i] = (float)i;
    }
}

static void fill_channel_ramps(float *samples, uint32_t frames, uint32_t channels) {
    for (uint32_t frame = 0; frame < frames; frame += 1) {
        for (uint32_t channel = 0; channel < channels; channel += 1) {
            samples[frame * channels + channel] = (float)(frame * 10 + channel * 1000);
        }
    }
}

static float cubic_reference_catmull_rom(float p0, float p1, float p2, float p3, double t) {
    double tt = t * t;
    double ttt = tt * t;

    return (float)(0.5 * ((2.0 * p1) + ((-p0 + p2) * t) +
                          (((2.0 * p0) - (5.0 * p1) + (4.0 * p2) - p3) * tt) +
                          ((-p0 + (3.0 * p1) - (3.0 * p2) + p3) * ttt)));
}

static float cubic_reference_read(const float *samples, uint32_t frames, uint32_t channels, uint32_t frame,
                                  uint32_t channel) {
    if (frame >= frames) {
        return 0.0f;
    }

    return samples[(frame * channels) + channel];
}

static void cubic_reference_render(const float *samples, uint32_t frames, uint32_t channels, uint64_t resample_step,
                                   float *output, uint32_t output_frames) {
    uint64_t position = 0;

    for (uint32_t out_frame = 0; out_frame < output_frames; out_frame += 1) {
        uint32_t frame = (uint32_t)(position >> FIXED_PRECISION);
        double t = FIXED_TO_DOUBLE(position & FIXED_FRACTION_MASK);

        for (uint32_t channel = 0; channel < channels; channel += 1) {
            float p0 = frame == 0 ? cubic_reference_read(samples, frames, channels, 0, channel)
                                  : cubic_reference_read(samples, frames, channels, frame - 1, channel);
            float p1 = cubic_reference_read(samples, frames, channels, frame, channel);
            float p2 = cubic_reference_read(samples, frames, channels, frame + 1, channel);
            float p3 = cubic_reference_read(samples, frames, channels, frame + 2, channel);

            output[(out_frame * channels) + channel] = cubic_reference_catmull_rom(p0, p1, p2, p3, t);
        }

        position += resample_step;
    }
}

static double sinc8_reference_sinc(double x) {
    const double pi = 3.14159265358979323846264338327950288;
    double pix;

    if (x > -0.00000001 && x < 0.00000001) {
        return 1.0;
    }

    pix = pi * x;
    return forge_sin(pix) / pix;
}

static uint32_t sinc8_reference_phase(uint64_t fraction) {
    return (uint32_t)(((fraction & FIXED_FRACTION_MASK) * FA_RESAMPLE_SINC8_PHASES) >> FIXED_PRECISION);
}

static void sinc8_reference_coefficients(uint64_t fraction, float *coefficients) {
    uint32_t phase = sinc8_reference_phase(fraction);
    double t = (double)phase / (double)FA_RESAMPLE_SINC8_PHASES;
    double sum = 0.0;

    for (uint32_t tap = 0; tap < FA_RESAMPLE_SINC8_TAPS; tap += 1) {
        double x = (double)tap - (double)FA_RESAMPLE_SINC8_LEFT_TAPS - t;
        double coeff = sinc8_reference_sinc(x) * sinc8_reference_sinc(x / 4.0);
        coefficients[tap] = (float)coeff;
        sum += coeff;
    }

    if (sum != 0.0) {
        for (uint32_t tap = 0; tap < FA_RESAMPLE_SINC8_TAPS; tap += 1) {
            coefficients[tap] = (float)((double)coefficients[tap] / sum);
        }
    }
}

static float sinc8_reference_read(const float *samples, uint32_t frames, uint32_t channels, int64_t frame,
                                  uint32_t channel) {
    if (frame < 0) {
        return samples[channel];
    }
    if ((uint64_t)frame >= frames) {
        return 0.0f;
    }

    return samples[(frame * channels) + channel];
}

static void sinc8_reference_render(const float *samples, uint32_t frames, uint32_t channels, uint64_t resample_step,
                                   float *output, uint32_t output_frames) {
    uint64_t position = 0;

    for (uint32_t out_frame = 0; out_frame < output_frames; out_frame += 1) {
        int64_t frame = (int64_t)(position >> FIXED_PRECISION);
        uint64_t fraction = position & FIXED_FRACTION_MASK;
        float coefficients[FA_RESAMPLE_SINC8_TAPS];

        sinc8_reference_coefficients(fraction, coefficients);
        for (uint32_t channel = 0; channel < channels; channel += 1) {
            float sample = 0.0f;

            for (uint32_t tap = 0; tap < FA_RESAMPLE_SINC8_TAPS; tap += 1) {
                int64_t tap_frame = frame + (int64_t)tap - (int64_t)FA_RESAMPLE_SINC8_LEFT_TAPS;
                sample += sinc8_reference_read(samples, frames, channels, tap_frame, channel) * coefficients[tap];
            }

            output[(out_frame * channels) + channel] = sample;
        }

        position += resample_step;
    }
}

static int check_values_tolerance(const char *name, const float *actual, const float *expected, uint32_t count,
                                  float tolerance) {
    int failed = 0;

    for (uint32_t i = 0; i < count; i += 1) {
        float delta = actual[i] - expected[i];
        if (delta < 0.0f) {
            delta = -delta;
        }
        if (delta > tolerance) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected[i], actual[i]);
            failed = 1;
        }
    }

    return failed;
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

static int test_stereo_split_fractional_ratio_matches_contiguous_across_passes(void) {
    enum {
        channels = 2,
        frames = 40,
        pass_frames = 5,
        pass_count = 4,
        split = 9
    };
    float samples[frames * channels];
    float contiguous_out[pass_frames * pass_count * channels] = {0};
    float split_out[pass_frames * pass_count * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(1.25);
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);

    failed |= render_buffer_passes_channels("stereo-contiguous", samples, frames, NULL, 0, channels, resample_step,
                                            pass_frames, pass_count, contiguous_out);
    failed |= render_buffer_passes_channels("stereo-split", samples, split, samples + split * channels,
                                            frames - split, channels, resample_step, pass_frames, pass_count,
                                            split_out);
    failed |= check_values("stereo_split_vs_contiguous", split_out, contiguous_out,
                           pass_frames * pass_count * channels);

    return failed;
}

static int test_three_channel_split_fractional_ratio_matches_contiguous_across_passes(void) {
    enum {
        channels = 3,
        frames = 36,
        pass_frames = 6,
        pass_count = 5,
        split = 7
    };
    float samples[frames * channels];
    float contiguous_out[pass_frames * pass_count * channels] = {0};
    float split_out[pass_frames * pass_count * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.6);
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);

    failed |= render_buffer_passes_channels("three-channel-contiguous", samples, frames, NULL, 0, channels,
                                            resample_step, pass_frames, pass_count, contiguous_out);
    failed |= render_buffer_passes_channels("three-channel-split", samples, split, samples + split * channels,
                                            frames - split, channels, resample_step, pass_frames, pass_count,
                                            split_out);
    failed |= check_values("three_channel_split_vs_contiguous", split_out, contiguous_out,
                           pass_frames * pass_count * channels);

    return failed;
}

static int test_stereo_linear_interpolation_matches_expected(void) {
    enum {
        channels = 2,
        output_frames = 5
    };
    static const float samples[] = {
        0.0f, 100.0f,
        10.0f, 110.0f,
        20.0f, 120.0f,
        30.0f, 130.0f
    };
    static const float expected[] = {
        0.0f, 100.0f,
        5.0f, 105.0f,
        10.0f, 110.0f,
        15.0f, 115.0f,
        20.0f, 120.0f
    };
    float output[output_frames * channels] = {0};
    ForgeAudioTestSourceResampleResult result =
        render_buffers_channels(samples, 4, NULL, 0, channels, DOUBLE_TO_FIXED(0.5), output, output_frames);
    int failed = 0;

    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "stereo expected resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values("stereo_linear_expected", output, expected, output_frames * channels);
    return failed;
}

static int test_mono_cubic_interpolation_matches_expected(void) {
    enum {
        output_frames = 8
    };
    static const float samples[] = {0.0f, 10.0f, 25.0f, 45.0f, 70.0f};
    static const float expected[] = {0.0f, 4.0625f, 10.0f, 16.875f, 25.0f, 34.375f, 45.0f, 63.125f};
    float output[output_frames] = {0};
    ForgeAudioTestSourceResampleResult result =
        render_buffers_channels_quality(samples, 5, NULL, 0, 1, DOUBLE_TO_FIXED(0.5),
                                        FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, output, output_frames);
    int failed = 0;

    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "mono cubic resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values("mono_cubic_expected", output, expected, output_frames);
    return failed;
}

static int test_stereo_cubic_channel_order_matches_reference(void) {
    enum {
        channels = 2,
        frames = 6,
        output_frames = 7
    };
    float samples[frames * channels];
    float expected[output_frames * channels] = {0};
    float output[output_frames * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.5);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);
    cubic_reference_render(samples, frames, channels, resample_step, expected, output_frames);
    result = render_buffers_channels_quality(samples, frames, NULL, 0, channels, resample_step,
                                             FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, output, output_frames);

    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "stereo cubic resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values("stereo_cubic_channel_order", output, expected, output_frames * channels);
    return failed;
}

static int test_three_channel_cubic_channel_order_matches_reference(void) {
    enum {
        channels = 3,
        frames = 7,
        output_frames = 8
    };
    float samples[frames * channels];
    float expected[output_frames * channels] = {0};
    float output[output_frames * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.75);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);
    cubic_reference_render(samples, frames, channels, resample_step, expected, output_frames);
    result = render_buffers_channels_quality(samples, frames, NULL, 0, channels, resample_step,
                                             FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, output, output_frames);

    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "three-channel cubic resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values("three_channel_cubic_channel_order", output, expected, output_frames * channels);
    return failed;
}

static int test_cubic_split_fractional_ratio_matches_contiguous_across_passes(void) {
    enum {
        channels = 3,
        frames = 48,
        pass_frames = 5,
        pass_count = 6,
        split = 11
    };
    float samples[frames * channels];
    float contiguous_out[pass_frames * pass_count * channels] = {0};
    float split_out[pass_frames * pass_count * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.875);
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);

    failed |= render_buffer_passes_channels_quality("cubic-contiguous", samples, frames, NULL, 0, channels,
                                                    resample_step, FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, pass_frames,
                                                    pass_count, contiguous_out);
    failed |= render_buffer_passes_channels_quality("cubic-split", samples, split, samples + split * channels,
                                                    frames - split, channels, resample_step,
                                                    FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, pass_frames, pass_count,
                                                    split_out);
    failed |= check_values("cubic_split_vs_contiguous", split_out, contiguous_out,
                           pass_frames * pass_count * channels);

    return failed;
}

static int test_cubic_padding_peeks_across_loop_boundary(void) {
    static const float samples[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    static const float virtual_loop_samples[] = {0.0f, 1.0f, 2.0f, 1.0f, 2.0f};
    float expected[4] = {0};
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    float output[4] = {0};
    int failed = 0;

    cubic_reference_render(virtual_loop_samples, 5, 1, DOUBLE_TO_FIXED(0.75), expected, 4);

    init_harness(&harness, 8, 4);
    harness.voice.src.resampleStep = DOUBLE_TO_FIXED(0.75);
    set_resampler_quality(&harness, FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC);
    set_buffer(&harness, 0, samples, 5);
    harness.buffers[0].buffer.loop_begin = 1;
    harness.buffers[0].buffer.loop_length = 2;
    harness.buffers[0].buffer.loop_count = 1;
    harness.buffers[0].loop_bytes = 2 * harness.format.block_align;
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    if (result.resampled_frames != 4) {
        fprintf(stderr, "cubic loop-boundary resampled_frames: expected 4, got %u\n", result.resampled_frames);
        failed = 1;
    }
    if (result.cur_buffer_offset != 3 || result.queued_buffer_count != 1) {
        fprintf(stderr, "cubic loop-boundary peek mutated playback state: offset=%u queued=%zu\n",
                result.cur_buffer_offset, result.queued_buffer_count);
        failed = 1;
    }

    failed |= check_values("cubic_loop_boundary_out", output, expected, 4);

    destroy_harness(&harness);
    return failed;
}

static int test_cubic_loop_entry_uses_prior_play_frame_before_first_wrap(void) {
    static const float samples[] = {0.0f, 100.0f, 10.0f, 20.0f, 30.0f};
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    float output[1] = {0};
    float expected;
    int failed = 0;

    expected = cubic_reference_catmull_rom(100.0f, 10.0f, 20.0f, 10.0f, 0.5);

    init_harness(&harness, 4, 1);
    harness.voice.src.resampleStep = FIXED_ONE;
    harness.voice.src.resampleOffset = DOUBLE_TO_FIXED(0.5);
    harness.voice.src.curBufferOffset = 2;
    harness.voice.src.totalSamples = 2;
    set_resampler_quality(&harness, FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC);
    set_buffer(&harness, 0, samples, 5);
    harness.buffers[0].buffer.loop_begin = 2;
    harness.buffers[0].buffer.loop_length = 2;
    harness.buffers[0].buffer.loop_count = 1;
    harness.buffers[0].loop_bytes = 2 * harness.format.block_align;
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    if (result.resampled_frames != 1) {
        fprintf(stderr, "cubic loop entry resampled_frames: expected 1, got %u\n", result.resampled_frames);
        failed = 1;
    }

    failed |= check_values("cubic_loop_entry_before_wrap", output, &expected, 1);

    destroy_harness(&harness);
    return failed;
}

static int test_cubic_identity_bypass_remains_exact(void) {
    enum {
        output_frames = 4
    };
    static const float samples[] = {1.0f, -0.0f, 16777216.0f, -5.5f};
    float output[output_frames] = {0};
    ForgeAudioTestSourceResampleResult result =
        render_buffers_channels_quality(samples, output_frames, NULL, 0, 1, FIXED_ONE,
                                        FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, output, output_frames);
    int failed = 0;

    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "cubic identity resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }
    if (forge_memcmp(output, samples, sizeof(samples)) != 0) {
        fprintf(stderr, "cubic identity bypass did not preserve exact sample bytes\n");
        failed = 1;
    }

    return failed;
}

static int test_sinc8_identity_bypass_remains_exact(void) {
    enum {
        output_frames = 4
    };
    static const float samples[] = {1.0f, -0.0f, 16777216.0f, -5.5f};
    float output[output_frames] = {0};
    ForgeAudioTestSourceResampleResult result =
        render_buffers_channels_quality(samples, output_frames, NULL, 0, 1, FIXED_ONE,
                                        FA_AUDIO_SOURCE_RESAMPLER_SINC8, output, output_frames);
    int failed = 0;

    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "sinc8 identity resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }
    if (forge_memcmp(output, samples, sizeof(samples)) != 0) {
        fprintf(stderr, "sinc8 identity bypass did not preserve exact sample bytes\n");
        failed = 1;
    }

    return failed;
}

static int test_mono_sinc8_interpolation_matches_reference(void) {
    enum {
        output_frames = 8
    };
    static const float samples[] = {0.0f, 10.0f, 25.0f, 45.0f, 70.0f, 100.0f};
    float expected[output_frames] = {0};
    float output[output_frames] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.5);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    sinc8_reference_render(samples, 6, 1, resample_step, expected, output_frames);
    result = render_buffers_channels_quality(samples, 6, NULL, 0, 1, resample_step,
                                             FA_AUDIO_SOURCE_RESAMPLER_SINC8, output, output_frames);
    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "mono sinc8 resampled_frames: expected %u, got %u\n", output_frames, result.resampled_frames);
        failed = 1;
    }

    failed |= check_values_tolerance("mono_sinc8_reference", output, expected, output_frames, 0.000002f);
    return failed;
}

static int test_stereo_sinc8_channel_order_matches_reference(void) {
    enum {
        channels = 2,
        frames = 8,
        output_frames = 9
    };
    float samples[frames * channels];
    float expected[output_frames * channels] = {0};
    float output[output_frames * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.625);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);
    sinc8_reference_render(samples, frames, channels, resample_step, expected, output_frames);
    result = render_buffers_channels_quality(samples, frames, NULL, 0, channels, resample_step,
                                             FA_AUDIO_SOURCE_RESAMPLER_SINC8, output, output_frames);
    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "stereo sinc8 resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values_tolerance("stereo_sinc8_channel_order", output, expected, output_frames * channels,
                                     0.000002f);
    return failed;
}

static int test_three_channel_sinc8_channel_order_matches_reference(void) {
    enum {
        channels = 3,
        frames = 9,
        output_frames = 10
    };
    float samples[frames * channels];
    float expected[output_frames * channels] = {0};
    float output[output_frames * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.75);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);
    sinc8_reference_render(samples, frames, channels, resample_step, expected, output_frames);
    result = render_buffers_channels_quality(samples, frames, NULL, 0, channels, resample_step,
                                             FA_AUDIO_SOURCE_RESAMPLER_SINC8, output, output_frames);
    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "three-channel sinc8 resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values_tolerance("three_channel_sinc8_channel_order", output, expected, output_frames * channels,
                                     0.000002f);
    return failed;
}

static int test_sinc8_split_fractional_ratio_matches_contiguous_across_passes(void) {
    enum {
        channels = 3,
        frames = 64,
        pass_frames = 5,
        pass_count = 8,
        split = 13
    };
    float samples[frames * channels];
    float contiguous_out[pass_frames * pass_count * channels] = {0};
    float split_out[pass_frames * pass_count * channels] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.875);
    int failed = 0;

    fill_channel_ramps(samples, frames, channels);

    failed |= render_buffer_passes_channels_quality("sinc8-contiguous", samples, frames, NULL, 0, channels,
                                                    resample_step, FA_AUDIO_SOURCE_RESAMPLER_SINC8, pass_frames,
                                                    pass_count, contiguous_out);
    failed |= render_buffer_passes_channels_quality("sinc8-split", samples, split, samples + split * channels,
                                                    frames - split, channels, resample_step,
                                                    FA_AUDIO_SOURCE_RESAMPLER_SINC8, pass_frames, pass_count,
                                                    split_out);
    failed |= check_values_tolerance("sinc8_split_vs_contiguous", split_out, contiguous_out,
                                     pass_frames * pass_count * channels, 0.000003f);

    return failed;
}

static int test_sinc8_padding_peeks_across_loop_boundary(void) {
    static const float samples[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    static const float virtual_loop_samples[] = {0.0f, 1.0f, 2.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    float expected[4] = {0};
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    float output[4] = {0};
    int failed = 0;

    sinc8_reference_render(virtual_loop_samples, 7, 1, DOUBLE_TO_FIXED(0.75), expected, 4);

    init_harness(&harness, 8, 4);
    harness.voice.src.resampleStep = DOUBLE_TO_FIXED(0.75);
    set_resampler_quality(&harness, FA_AUDIO_SOURCE_RESAMPLER_SINC8);
    set_buffer(&harness, 0, samples, 5);
    harness.buffers[0].buffer.loop_begin = 1;
    harness.buffers[0].buffer.loop_length = 2;
    harness.buffers[0].buffer.loop_count = 1;
    harness.buffers[0].loop_bytes = 2 * harness.format.block_align;
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    if (result.resampled_frames != 4) {
        fprintf(stderr, "sinc8 loop-boundary resampled_frames: expected 4, got %u\n", result.resampled_frames);
        failed = 1;
    }
    if (result.cur_buffer_offset != 3 || result.queued_buffer_count != 1) {
        fprintf(stderr, "sinc8 loop-boundary peek mutated playback state: offset=%u queued=%zu\n",
                result.cur_buffer_offset, result.queued_buffer_count);
        failed = 1;
    }

    failed |= check_values_tolerance("sinc8_loop_boundary_out", output, expected, 4, 0.000002f);

    destroy_harness(&harness);
    return failed;
}

static int test_sinc8_loop_entry_uses_prior_play_frames_before_first_wrap(void) {
    static const float samples[] = {50.0f, 100.0f, 10.0f, 20.0f, 30.0f};
    static const float taps[] = {50.0f, 50.0f, 100.0f, 10.0f, 20.0f, 10.0f, 20.0f, 30.0f};
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    float coefficients[FA_RESAMPLE_SINC8_TAPS];
    float output[1] = {0};
    float expected = 0.0f;
    int failed = 0;

    sinc8_reference_coefficients(DOUBLE_TO_FIXED(0.5), coefficients);
    for (uint32_t tap = 0; tap < FA_RESAMPLE_SINC8_TAPS; tap += 1) {
        expected += taps[tap] * coefficients[tap];
    }

    init_harness(&harness, 5, 1);
    harness.voice.src.resampleStep = FIXED_ONE;
    harness.voice.src.resampleOffset = DOUBLE_TO_FIXED(0.5);
    harness.voice.src.curBufferOffset = 2;
    harness.voice.src.totalSamples = 2;
    set_resampler_quality(&harness, FA_AUDIO_SOURCE_RESAMPLER_SINC8);
    set_buffer(&harness, 0, samples, 5);
    harness.buffers[0].buffer.loop_begin = 2;
    harness.buffers[0].buffer.loop_length = 2;
    harness.buffers[0].buffer.loop_count = 1;
    harness.buffers[0].loop_bytes = 2 * harness.format.block_align;
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    if (result.resampled_frames != 1) {
        fprintf(stderr, "sinc8 loop entry resampled_frames: expected 1, got %u\n", result.resampled_frames);
        failed = 1;
    }

    failed |= check_values_tolerance("sinc8_loop_entry_before_wrap", output, &expected, 1, 0.000002f);

    destroy_harness(&harness);
    return failed;
}

static int test_sinc8_true_start_and_end_policy_matches_reference(void) {
    enum {
        output_frames = 8
    };
    static const float samples[] = {10.0f, 20.0f, 40.0f};
    float expected[output_frames] = {0};
    float output[output_frames] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(0.5);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    sinc8_reference_render(samples, 3, 1, resample_step, expected, output_frames);
    result = render_buffers_channels_quality(samples, 3, NULL, 0, 1, resample_step,
                                             FA_AUDIO_SOURCE_RESAMPLER_SINC8, output, output_frames);
    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "sinc8 boundary resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values_tolerance("sinc8_true_start_end", output, expected, output_frames, 0.000002f);
    return failed;
}

static void sinc8_reference_render_variable_rate(const float *samples, uint32_t frames, float ratio, float ratio_step,
                                                 uint32_t ratio_frames, float *output, uint32_t output_frames) {
    uint64_t position = 0;

    for (uint32_t out_frame = 0; out_frame < output_frames; out_frame += 1) {
        uint64_t fraction = position & FIXED_FRACTION_MASK;
        int64_t frame = (int64_t)(position >> FIXED_PRECISION);
        float coefficients[FA_RESAMPLE_SINC8_TAPS];
        float sample = 0.0f;

        sinc8_reference_coefficients(fraction, coefficients);
        for (uint32_t tap = 0; tap < FA_RESAMPLE_SINC8_TAPS; tap += 1) {
            int64_t tap_frame = frame + (int64_t)tap - (int64_t)FA_RESAMPLE_SINC8_LEFT_TAPS;
            sample += sinc8_reference_read(samples, frames, 1, tap_frame, 0) * coefficients[tap];
        }
        output[out_frame] = sample;

        position += DOUBLE_TO_FIXED(ratio);
        if (out_frame + 1 < ratio_frames) {
            ratio += ratio_step;
        }
    }
}

static int test_sinc8_variable_rate_path_matches_reference(void) {
    enum {
        frames = 12,
        output_frames = 6
    };
    float samples[frames];
    float expected[output_frames] = {0};
    float output[output_frames] = {0};
    SourceHarness harness;
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        samples[i] = (float)(i * i - (int32_t)i);
    }
    sinc8_reference_render_variable_rate(samples, frames, 0.5f, 0.1f, output_frames, expected, output_frames);

    init_harness(&harness, frames, output_frames);
    harness.voice.src.resampleStep = DOUBLE_TO_FIXED(0.5);
    harness.voice.src.freqRatio = 0.5f;
    harness.voice.src.rateAutomation.active = 1;
    harness.voice.src.rateAutomation.target = 1.0f;
    harness.voice.src.rateAutomation.step = 0.1f;
    harness.voice.src.rateAutomation.remainingFrames = output_frames;
    set_resampler_quality(&harness, FA_AUDIO_SOURCE_RESAMPLER_SINC8);
    set_buffer(&harness, 0, samples, frames);
    harness.voice.src.queued_buffer_count = 1;
    harness.voice.src.queued_buffers_capacity = 1;

    result = forge_audio_test_decode_resample_source(&harness.voice, output);
    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "sinc8 variable resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }

    failed |= check_values_tolerance("sinc8_variable_rate", output, expected, output_frames, 0.000003f);
    destroy_harness(&harness);
    return failed;
}

static double dft_bin_power(const float *samples, uint32_t frames, uint32_t bin) {
    const double pi = 3.14159265358979323846264338327950288;
    double real = 0.0;
    double imag = 0.0;

    for (uint32_t i = 0; i < frames; i += 1) {
        double angle = (2.0 * pi * (double)bin * (double)i) / (double)frames;
        real += (double)samples[i] * forge_cos(angle);
        imag -= (double)samples[i] * forge_sin(angle);
    }

    return real * real + imag * imag;
}

static int test_sinc8_reduces_upsample_image_energy_vs_cubic(void) {
    enum {
        source_frames = 300,
        output_frames = 512,
        signal_bin = 96,
        image_bin = 160
    };
    float source[source_frames];
    float cubic[output_frames] = {0};
    float sinc8[output_frames] = {0};
    double cubicSignal;
    double cubicImage;
    double sincSignal;
    double sincImage;
    int failed = 0;

    for (uint32_t i = 0; i < source_frames; i += 1) {
        source[i] = (float)forge_sin(2.0 * 3.14159265358979323846264338327950288 * 0.375 * (double)i);
    }

    render_buffers_channels_quality(source, source_frames, NULL, 0, 1, DOUBLE_TO_FIXED(0.5),
                                    FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC, cubic, output_frames);
    render_buffers_channels_quality(source, source_frames, NULL, 0, 1, DOUBLE_TO_FIXED(0.5),
                                    FA_AUDIO_SOURCE_RESAMPLER_SINC8, sinc8, output_frames);

    cubicSignal = dft_bin_power(cubic, output_frames, signal_bin);
    cubicImage = dft_bin_power(cubic, output_frames, image_bin);
    sincSignal = dft_bin_power(sinc8, output_frames, signal_bin);
    sincImage = dft_bin_power(sinc8, output_frames, image_bin);

    if (sincSignal <= cubicSignal * 0.25) {
        fprintf(stderr, "sinc8 spectral signal unexpectedly low: cubic %.8f sinc8 %.8f\n", cubicSignal, sincSignal);
        failed = 1;
    }
    if (sincImage >= cubicImage * 0.75) {
        fprintf(stderr, "sinc8 image energy not lower enough: cubic %.8f sinc8 %.8f\n", cubicImage, sincImage);
        failed = 1;
    }

    return failed;
}

static int test_fractional_phase_continuity_matches_single_pass(void) {
    enum {
        frames = 64,
        pass_frames = 4,
        pass_count = 6,
        output_frames = pass_frames * pass_count
    };
    float samples[frames];
    float single_pass_out[output_frames] = {0};
    float multi_pass_out[output_frames] = {0};
    const uint64_t resample_step = DOUBLE_TO_FIXED(1.375);
    ForgeAudioTestSourceResampleResult result;
    int failed = 0;

    fill_ramp(samples, frames);

    result = render_buffers_channels(samples, frames, NULL, 0, 1, resample_step, single_pass_out, output_frames);
    if (result.resampled_frames != output_frames) {
        fprintf(stderr, "phase single pass resampled_frames: expected %u, got %u\n", output_frames,
                result.resampled_frames);
        failed = 1;
    }
    failed |= render_buffer_passes("phase-multi-pass", samples, frames, NULL, 0, resample_step, pass_frames,
                                   pass_count, multi_pass_out);
    failed |= check_values("phase_multi_vs_single", multi_pass_out, single_pass_out, output_frames);

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
    set_resampler_quality(&harness, FORGE_AUDIO_SOURCE_RESAMPLER_LINEAR);
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
    failures += run_test("stereo_split_fractional_ratio_matches_contiguous_across_passes",
                         test_stereo_split_fractional_ratio_matches_contiguous_across_passes);
    failures += run_test("three_channel_split_fractional_ratio_matches_contiguous_across_passes",
                         test_three_channel_split_fractional_ratio_matches_contiguous_across_passes);
    failures += run_test("stereo_linear_interpolation_matches_expected",
                         test_stereo_linear_interpolation_matches_expected);
    failures += run_test("mono_cubic_interpolation_matches_expected",
                         test_mono_cubic_interpolation_matches_expected);
    failures += run_test("stereo_cubic_channel_order_matches_reference",
                         test_stereo_cubic_channel_order_matches_reference);
    failures += run_test("three_channel_cubic_channel_order_matches_reference",
                         test_three_channel_cubic_channel_order_matches_reference);
    failures += run_test("cubic_split_fractional_ratio_matches_contiguous_across_passes",
                         test_cubic_split_fractional_ratio_matches_contiguous_across_passes);
    failures += run_test("cubic_padding_peeks_across_loop_boundary",
                         test_cubic_padding_peeks_across_loop_boundary);
    failures += run_test("cubic_loop_entry_uses_prior_play_frame_before_first_wrap",
                         test_cubic_loop_entry_uses_prior_play_frame_before_first_wrap);
    failures += run_test("cubic_identity_bypass_remains_exact",
                         test_cubic_identity_bypass_remains_exact);
    failures += run_test("sinc8_identity_bypass_remains_exact",
                         test_sinc8_identity_bypass_remains_exact);
    failures += run_test("mono_sinc8_interpolation_matches_reference",
                         test_mono_sinc8_interpolation_matches_reference);
    failures += run_test("stereo_sinc8_channel_order_matches_reference",
                         test_stereo_sinc8_channel_order_matches_reference);
    failures += run_test("three_channel_sinc8_channel_order_matches_reference",
                         test_three_channel_sinc8_channel_order_matches_reference);
    failures += run_test("sinc8_split_fractional_ratio_matches_contiguous_across_passes",
                         test_sinc8_split_fractional_ratio_matches_contiguous_across_passes);
    failures += run_test("sinc8_padding_peeks_across_loop_boundary",
                         test_sinc8_padding_peeks_across_loop_boundary);
    failures += run_test("sinc8_loop_entry_uses_prior_play_frames_before_first_wrap",
                         test_sinc8_loop_entry_uses_prior_play_frames_before_first_wrap);
    failures += run_test("sinc8_true_start_and_end_policy_matches_reference",
                         test_sinc8_true_start_and_end_policy_matches_reference);
    failures += run_test("sinc8_variable_rate_path_matches_reference",
                         test_sinc8_variable_rate_path_matches_reference);
    failures += run_test("sinc8_reduces_upsample_image_energy_vs_cubic",
                         test_sinc8_reduces_upsample_image_energy_vs_cubic);
    failures += run_test("fractional_phase_continuity_matches_single_pass",
                         test_fractional_phase_continuity_matches_single_pass);
    failures += run_test("split_ramp_matches_contiguous", test_split_ramp_matches_contiguous);
    failures += run_test("one_padding_frame_is_preserved", test_one_padding_frame_is_preserved);
    failures += run_test("padding_peeks_across_loop_boundary", test_padding_peeks_across_loop_boundary);
    failures += run_test("decode_sizing_covers_output_rate", test_decode_sizing_covers_output_rate);

    return failures == 0 ? 0 : 1;
}
