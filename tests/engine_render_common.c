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

typedef struct RateProbeEffect {
    ForgeEffect base;
    uint32_t input_sample_rate;
    uint32_t output_sample_rate;
    uint32_t lock_count;
    uint32_t unlock_count;
    uint32_t destroy_count;
} RateProbeEffect;

static void rate_probe_destroy(void *effect_ptr) {
    RateProbeEffect *effect = (RateProbeEffect *)effect_ptr;

    effect->destroy_count += 1;
}

static void rate_probe_get_info(void *effect_ptr, ForgeEffectInfo *effect_info) {
    (void)effect_ptr;
    effect_info->flags = FORGE_EFFECT_BASE_DEFAULT_FLAG;
    effect_info->min_input_buffer_count = 1;
    effect_info->max_input_buffer_count = 1;
    effect_info->min_output_buffer_count = 1;
    effect_info->max_output_buffer_count = 1;
}

static ForgeResult rate_probe_lock_for_process(void *effect_ptr, uint32_t input_locked_parameter_count,
                                               const ForgeEffectLockBuffer *input_locked_parameters,
                                               uint32_t output_locked_parameter_count,
                                               const ForgeEffectLockBuffer *output_locked_parameters) {
    RateProbeEffect *effect = (RateProbeEffect *)effect_ptr;

    (void)input_locked_parameter_count;
    (void)output_locked_parameter_count;
    effect->input_sample_rate = input_locked_parameters->format->sample_rate;
    effect->output_sample_rate = output_locked_parameters->format->sample_rate;
    effect->lock_count += 1;
    return ForgeResultSuccess;
}

static void rate_probe_unlock_for_process(void *effect_ptr) {
    RateProbeEffect *effect = (RateProbeEffect *)effect_ptr;

    effect->unlock_count += 1;
}

static void rate_probe_init(RateProbeEffect *effect) {
    forge_zero(effect, sizeof(*effect));
    effect->base.destroy = rate_probe_destroy;
    effect->base.get_info = rate_probe_get_info;
    effect->base.lock_for_process = rate_probe_lock_for_process;
    effect->base.unlock_for_process = rate_probe_unlock_for_process;
}

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

int test_source_effect_initial_send_lock_uses_render_rate(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        send_rate = 24000,
        quantum = 16
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *source = NULL;
    RateProbeEffect probe;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeAudioFormat source_format = audio_test_float_format(channels, send_rate);
    ForgeSend send;
    ForgeSendList send_list;
    int failed = 0;

    rate_probe_init(&probe);
    desc.effect = &probe.base;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, send_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &source, &source_format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, &chain) != 0;
    }
    if (!failed && (probe.lock_count != 1 || probe.input_sample_rate != send_rate ||
                    probe.output_sample_rate != send_rate)) {
        fprintf(stderr, "source initial send effect rate: locks=%u input=%u output=%u expected=%u\n",
                probe.lock_count, probe.input_sample_rate, probe.output_sample_rate, send_rate);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_submix_effect_initial_send_lock_uses_render_rate(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        send_rate = 24000,
        quantum = 16
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *destination = NULL;
    ForgeSubmixVoice *submix = NULL;
    RateProbeEffect probe;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeSend send;
    ForgeSendList send_list;
    int failed = 0;

    rate_probe_init(&probe);
    desc.effect = &probe.base;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed =
            forge_audio_create_submix_voice(harness.audio, &destination, channels, send_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = destination;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, send_rate, 0, 0, &send_list,
                                                 &chain) != 0;
    }
    if (!failed && (probe.lock_count != 1 || probe.input_sample_rate != send_rate ||
                    probe.output_sample_rate != send_rate)) {
        fprintf(stderr, "submix initial send effect rate: locks=%u input=%u output=%u expected=%u\n",
                probe.lock_count, probe.input_sample_rate, probe.output_sample_rate, send_rate);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
