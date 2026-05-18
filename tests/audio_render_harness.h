/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#ifndef FORGE_AUDIO_RENDER_HARNESS_H
#define FORGE_AUDIO_RENDER_HARNESS_H

#include "core_internal.h"

typedef struct AudioRenderHarness {
    ForgeAudioEngine *audio;
    ForgeMasterVoice *master;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t quantum;
} AudioRenderHarness;

float audio_test_absf(float value);
ForgeAudioFormat audio_test_float_format(uint32_t channels, uint32_t sample_rate);

int audio_render_harness_init(AudioRenderHarness *harness, uint32_t channels, uint32_t sample_rate, uint32_t quantum);
void audio_render_harness_destroy(AudioRenderHarness *harness);
int audio_render_harness_create_float_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                                             uint32_t sample_rate);
int audio_render_harness_submit_float_buffer(ForgeSourceVoice *voice, const float *samples, uint32_t frames,
                                             uint32_t channels);
int audio_render_harness_render(AudioRenderHarness *harness, float *output, uint32_t frames);

int audio_test_check_constant(const char *name, const float *samples, uint32_t frames, uint32_t channels,
                              float expected, float epsilon);
int audio_test_check_equal(const char *name, const float *actual, const float *expected, uint32_t count,
                           float epsilon);
int audio_test_check_max_adjacent_delta(const char *name, const float *samples, uint32_t count, float max_delta);

#endif /* FORGE_AUDIO_RENDER_HARNESS_H */
