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

typedef struct SubmixRenderInfo {
    uint32_t input_samples;
    uint32_t output_samples;
} SubmixRenderInfo;

static int check_values(const char *name, const float *actual, const float *expected, uint32_t count) {
    for (uint32_t i = 0; i < count; i += 1) {
        if (audio_test_absf(actual[i] - expected[i]) > 0.000001f) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected[i], actual[i]);
            return 1;
        }
    }

    return 0;
}

static void fill_mono_ramp(float *samples, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i += 1) {
        samples[i] = (float)(i * 10);
    }
}

static int render_source_to_submix(uint32_t channels, uint32_t master_rate, uint32_t submix_rate, uint32_t quantum,
                                   const float *source, uint32_t source_frames, float *output,
                                   uint32_t output_frames, SubmixRenderInfo *info) {
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, submix_rate);
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, source_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, output_frames);
    }

    if (!failed && info != NULL) {
        info->input_samples = submix->mix.inputSamples;
        info->output_samples = submix->mix.outputSamples;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_submix_stereo_identity_preserves_channel_order(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    static const float source[] = {
        0.0f, 100.0f,
        10.0f, 110.0f,
        20.0f, 120.0f,
        30.0f, 130.0f
    };
    float output[quantum * channels] = {0};
    int failed = 0;

    failed = render_source_to_submix(channels, sample_rate, sample_rate, quantum, source, quantum, output, quantum,
                                     NULL);
    if (!failed) {
        failed = check_values("submix_stereo_identity", output, source, quantum * channels);
    }

    return failed;
}

static int test_submix_mono_upsample_linear_interpolation(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 8,
        source_frames = 4
    };
    static const float expected[] = {
        /* The last output frame interpolates against the submix cache padding,
         * not the next queued source frame; submixes do not preserve source-style
         * split-buffer padding.
         */
        0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 15.0f
    };
    float source[source_frames];
    float output[quantum] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     quantum, NULL);
    if (!failed) {
        failed = check_values("submix_mono_upsample", output, expected, quantum);
    }

    return failed;
}

static int test_submix_mono_downsample_linear_interpolation_and_sizing(void) {
    enum {
        channels = 1,
        master_rate = 2000,
        submix_rate = 3000,
        quantum = 4,
        source_frames = 6
    };
    static const float expected[] = {
        0.0f, 15.0f, 30.0f, 45.0f
    };
    float source[source_frames];
    float output[quantum] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     quantum, NULL);
    if (!failed) {
        failed = check_values("submix_mono_downsample", output, expected, quantum);
    }

    return failed;
}

static int test_submix_resample_offset_resets_each_mix_pass(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 2,
        source_frames = 6,
        output_frames = quantum * pass_count
    };
    static const float expected[] = {
        /* Each submix mix pass starts resampling at phase zero. If submixes
         * carried fractional phase like source voices, the second pass would
         * begin between 30 and 40 instead of exactly at 30.
         */
        0.0f, 6.6666665f, 13.3333330f, 20.0f,
        30.0f, 36.6666680f, 43.3333320f, 50.0f
    };
    float source[source_frames];
    float output[output_frames] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_local_offset", output, expected, output_frames);
    }

    return failed;
}

static int test_submix_downsample_output_size_stays_within_initialized_input(void) {
    enum {
        channels = 1,
        master_rate = 2000,
        submix_rate = 3000,
        quantum = 4,
        source_frames = 6
    };
    float source[source_frames];
    float output[quantum] = {0};
    SubmixRenderInfo info;
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     quantum, &info);
    if (!failed && info.output_samples != quantum) {
        fprintf(stderr, "submix outputSamples: expected %u, got %u\n", quantum, info.output_samples);
        failed = 1;
    }
    if (!failed && info.input_samples / channels < source_frames + EXTRA_DECODE_PADDING) {
        fprintf(stderr, "submix inputSamples: expected at least %u frames, got %u\n",
                source_frames + EXTRA_DECODE_PADDING, info.input_samples / channels);
        failed = 1;
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

    failures += run_test("submix_stereo_identity_preserves_channel_order",
                         test_submix_stereo_identity_preserves_channel_order);
    failures += run_test("submix_mono_upsample_linear_interpolation",
                         test_submix_mono_upsample_linear_interpolation);
    failures += run_test("submix_mono_downsample_linear_interpolation_and_sizing",
                         test_submix_mono_downsample_linear_interpolation_and_sizing);
    failures += run_test("submix_resample_offset_resets_each_mix_pass",
                         test_submix_resample_offset_resets_each_mix_pass);
    failures += run_test("submix_downsample_output_size_stays_within_initialized_input",
                         test_submix_downsample_output_size_stays_within_initialized_input);

    return failures == 0 ? 0 : 1;
}
