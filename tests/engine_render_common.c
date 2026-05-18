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

int create_started_dc_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, float *source,
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

int check_result(const char *label, ForgeResult actual, ForgeResult expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    return 0;
}

int run_test(const char *name, int (*test_func)(void)) {
    int failed = test_func();

    if (failed) {
        fprintf(stderr, "FAIL %s\n", name);
        return 1;
    }

    printf("PASS %s\n", name);
    return 0;
}
