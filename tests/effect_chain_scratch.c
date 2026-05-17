/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "forge_audio_internal.h"

#include <stdio.h>

typedef struct TestEffect {
    ForgeEffect base;
    uint32_t output_channels;
    float value;
} TestEffect;

static void test_effect_process(void *effect_ptr, uint32_t input_buffer_count,
                                const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                                ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    TestEffect *effect = (TestEffect *)effect_ptr;
    float *output = (float *)output_buffers->buffer;
    uint32_t sample_count = output_buffers->valid_frame_count * effect->output_channels;

    (void)input_buffer_count;
    (void)input_buffers;
    (void)output_buffer_count;
    (void)is_enabled;

    for (uint32_t i = 0; i < sample_count; i += 1) {
        output[i] = effect->value;
    }
    output_buffers->buffer_flags = FORGE_EFFECT_BUFFER_VALID;
}

static int check_guard(const float *buffer, uint32_t original_samples, uint32_t total_samples, float guard_value) {
    for (uint32_t i = original_samples; i < total_samples; i += 1) {
        if (buffer[i] != guard_value) {
            fprintf(stderr, "guard sample %u changed from %f to %f\n", i, guard_value, buffer[i]);
            return 0;
        }
    }
    return 1;
}

int main(void) {
    enum {
        frame_count = 8,
        original_channels = 1,
        expanded_channels = 6,
        effect_count = 2
    };
    const float guard_value = 12345.0f;
    ForgeAudioEngine audio;
    ForgeVoice voice;
    TestEffect effects[effect_count];
    ForgeEffectDesc desc[effect_count];
    void *parameters[effect_count] = {0, 0};
    uint32_t parameter_sizes[effect_count] = {0, 0};
    uint8_t parameter_updates[effect_count] = {0, 0};
    uint8_t in_place_processing[effect_count] = {0, 0};
    float original[frame_count * expanded_channels];
    uint32_t samples = frame_count;
    float *result;

    forge_zero(&audio, sizeof(audio));
    forge_zero(&voice, sizeof(voice));
    forge_zero(effects, sizeof(effects));
    forge_zero(desc, sizeof(desc));

    audio.malloc_func = forge_malloc;
    audio.free_func = forge_free;
    audio.realloc_func = forge_realloc;

    voice.audio = &audio;
    voice.effects.count = effect_count;
    voice.effects.desc = desc;
    voice.effects.parameters = parameters;
    voice.effects.parameterSizes = parameter_sizes;
    voice.effects.parameterUpdates = parameter_updates;
    voice.effects.inPlaceProcessing = in_place_processing;

    for (uint32_t i = 0; i < frame_count * original_channels; i += 1) {
        original[i] = 1.0f;
    }
    for (uint32_t i = frame_count * original_channels; i < frame_count * expanded_channels; i += 1) {
        original[i] = guard_value;
    }

    for (uint32_t i = 0; i < effect_count; i += 1) {
        effects[i].base.process = test_effect_process;
        effects[i].output_channels = expanded_channels;
        effects[i].value = (float)(i + 2);
        desc[i].effect = &effects[i].base;
        desc[i].initial_state = 1;
        desc[i].output_channels = expanded_channels;
    }

    result = forge_audio_test_process_effect_chain(&voice, original, &samples);

    audio.free_func(audio.effectChainCache);
    audio.free_func(audio.effectChainCache2);

    if (samples != frame_count) {
        fprintf(stderr, "expected %u frames, got %u\n", frame_count, samples);
        return 1;
    }
    if (result == NULL) {
        fprintf(stderr, "expected non-null result buffer\n");
        return 1;
    }
    if (!check_guard(original, frame_count * original_channels, frame_count * expanded_channels, guard_value)) {
        return 1;
    }

    return 0;
}
