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

float audio_test_absf(float value) {
    return value < 0.0f ? -value : value;
}

ForgeAudioFormat audio_test_float_format(uint32_t channels, uint32_t sample_rate) {
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

int audio_render_harness_init(AudioRenderHarness *harness, uint32_t channels, uint32_t sample_rate, uint32_t quantum) {
    ForgeResult result;

    forge_zero(harness, sizeof(*harness));
    harness->channels = channels;
    harness->sample_rate = sample_rate;
    harness->quantum = quantum;

    result = forge_audio_create(&harness->audio, 0);
    if (result != 0) {
        fprintf(stderr, "forge_audio_create failed: %d\n", result);
        return 1;
    }

    result = forge_audio_test_create_virtual_master_voice(harness->audio, &harness->master, channels, sample_rate,
                                                         quantum, NULL);
    if (result != 0) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed: %d\n", result);
        audio_render_harness_destroy(harness);
        return 1;
    }

    return 0;
}

void audio_render_harness_destroy(AudioRenderHarness *harness) {
    if (harness->audio != NULL) {
        forge_audio_destroy(harness->audio);
    }
    forge_zero(harness, sizeof(*harness));
}

int audio_render_harness_create_float_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                                             uint32_t sample_rate) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeResult result = forge_audio_create_source_voice(harness->audio, voice, &format, 0,
                                                         FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);

    if (result != 0) {
        fprintf(stderr, "forge_audio_create_source_voice failed: %d\n", result);
        return 1;
    }

    return 0;
}

int audio_render_harness_submit_float_buffer(ForgeSourceVoice *voice, const float *samples, uint32_t frames,
                                             uint32_t channels) {
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

int audio_render_harness_render(AudioRenderHarness *harness, float *output, uint32_t frames) {
    ForgeResult result = forge_audio_test_render(harness->audio, output, frames);

    if (result != 0) {
        fprintf(stderr, "forge_audio_test_render failed: %d\n", result);
        return 1;
    }

    return 0;
}

int audio_test_check_constant(const char *name, const float *samples, uint32_t frames, uint32_t channels,
                              float expected, float epsilon) {
    uint32_t count = frames * channels;

    for (uint32_t i = 0; i < count; i += 1) {
        if (audio_test_absf(samples[i] - expected) > epsilon) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected, samples[i]);
            return 1;
        }
    }

    return 0;
}

int audio_test_check_equal(const char *name, const float *actual, const float *expected, uint32_t count,
                           float epsilon) {
    for (uint32_t i = 0; i < count; i += 1) {
        if (audio_test_absf(actual[i] - expected[i]) > epsilon) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected[i], actual[i]);
            return 1;
        }
    }

    return 0;
}

int audio_test_check_max_adjacent_delta(const char *name, const float *samples, uint32_t count, float max_delta) {
    for (uint32_t i = 1; i < count; i += 1) {
        float delta = audio_test_absf(samples[i] - samples[i - 1]);

        if (delta > max_delta) {
            fprintf(stderr, "%s[%u]: adjacent delta %.8f exceeds %.8f\n", name, i, delta, max_delta);
            return 1;
        }
    }

    return 0;
}
