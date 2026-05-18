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

static ForgeDelayParameters delay_params(float wet_dry_mix, float delay_ms, float feedback, float lowpass_hz) {
    ForgeDelayParameters params;

    params.wet_dry_mix = wet_dry_mix;
    params.delay_ms = delay_ms;
    params.feedback = feedback;
    params.lowpass_hz = lowpass_hz;
    return params;
}

static ForgeDelayTarget delay_target(uint32_t field_mask, float wet_dry_mix, float feedback, float lowpass_hz) {
    ForgeDelayTarget target;

    target.field_mask = field_mask;
    target.wet_dry_mix = wet_dry_mix;
    target.feedback = feedback;
    target.lowpass_hz = lowpass_hz;
    return target;
}

static int create_delay_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                               uint32_t sample_rate, const ForgeDelayParameters *params) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeEffect *delay = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeResult result;

    result = forge_create_delay(&delay, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_delay failed: %d\n", result);
        return 1;
    }

    desc.effect = delay;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    result = forge_audio_create_source_voice(harness->audio, voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                             NULL, NULL, &chain);
    if (result != ForgeResultSuccess) {
        forge_effect_destroy(delay);
        fprintf(stderr, "forge_audio_create_source_voice delay failed: %d\n", result);
        return 1;
    }

    if (params != NULL) {
        result = forge_voice_set_effect_parameters(*voice, 0, params, sizeof(*params), FORGE_AUDIO_BATCH_IMMEDIATE);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "forge_voice_set_effect_parameters delay failed: %d\n", result);
            return 1;
        }
    }

    return 0;
}

static int lock_delay_effect(ForgeEffect *effect, uint32_t channels, uint32_t sample_rate) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeResult result;

    input_lock.format = &format;
    input_lock.max_frame_count = 64;
    output_lock.format = &format;
    output_lock.max_frame_count = 64;

    result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "delay lock failed: %d\n", result);
        return 1;
    }

    return 0;
}

int test_delay_creation_kind_and_destroy(void) {
    ForgeEffect *effect = NULL;
    ForgeResult result = forge_create_delay(&effect, 0);
    int failed = 0;

    if (result != ForgeResultSuccess || effect == NULL) {
        fprintf(stderr, "delay create: result=%d effect=%p\n", result, (void *)effect);
        failed = 1;
    }
    if (!failed && effect->kind != ForgeEffectKindDelay) {
        fprintf(stderr, "delay kind: got %d\n", effect->kind);
        failed = 1;
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_format_validation_rejects_channel_change(void) {
    ForgeEffect *effect = NULL;
    ForgeAudioFormat input_format = audio_test_float_format(1, 48000);
    ForgeAudioFormat output_format = audio_test_float_format(2, 48000);
    ForgeAudioFormat pcm_format = audio_test_float_format(1, 48000);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeResult result;
    int failed = 0;

    pcm_format.format_tag = FORGE_AUDIO_FORMAT_PCM;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        input_lock.format = &input_format;
        input_lock.max_frame_count = 64;
        output_lock.format = &output_format;
        output_lock.max_frame_count = 64;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("delay_channel_change", result, ForgeResultEffectFormatUnsupported);
    }
    if (!failed) {
        output_lock.format = &input_format;
        input_lock.format = &pcm_format;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("delay_pcm_format", result, ForgeResultEffectFormatUnsupported);
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_parameter_clamping(void) {
    ForgeEffect *effect = NULL;
    ForgeDelayParameters set_params = delay_params(-10.0f, 10000.0f, 10.0f, -100.0f);
    ForgeDelayParameters got_params;
    int failed = 0;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &set_params, sizeof(set_params));
        effect->get_parameters(effect, &got_params, sizeof(got_params));
        if (got_params.wet_dry_mix != FORGE_DELAY_MIN_WET_DRY_MIX ||
            got_params.delay_ms != FORGE_DELAY_MAX_DELAY_MS ||
            got_params.feedback != FORGE_DELAY_MAX_FEEDBACK ||
            got_params.lowpass_hz != FORGE_DELAY_MIN_LOWPASS_HZ) {
            fprintf(stderr, "delay clamped params: wet=%.3f delay=%.3f feedback=%.3f lowpass=%.3f\n",
                    got_params.wet_dry_mix, got_params.delay_ms, got_params.feedback, got_params.lowpass_hz);
            failed = 1;
        }
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_blob_parameter_set_get_on_voice(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(75.0f, 12.0f, 0.25f, 500.0f);
    ForgeDelayParameters got_params;
    float source[quantum] = {0.0f, 0.0f, 0.0f, 0.0f};
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_effect_parameters(voice, 0, &got_params, sizeof(got_params)) != 0;
    }
    if (!failed &&
        (audio_test_absf(got_params.wet_dry_mix - FORGE_DELAY_DEFAULT_WET_DRY_MIX) > 0.000001f ||
         audio_test_absf(got_params.delay_ms - FORGE_DELAY_DEFAULT_DELAY_MS) > 0.000001f ||
         audio_test_absf(got_params.feedback - FORGE_DELAY_DEFAULT_FEEDBACK) > 0.000001f ||
         audio_test_absf(got_params.lowpass_hz - FORGE_DELAY_DEFAULT_LOWPASS_HZ) > 0.000001f)) {
        fprintf(stderr, "delay voice blob queued get: wet=%.3f delay=%.3f feedback=%.3f lowpass=%.3f\n",
                got_params.wet_dry_mix, got_params.delay_ms, got_params.feedback, got_params.lowpass_hz);
        failed = 1;
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, quantum, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_effect_parameters(voice, 0, &got_params, sizeof(got_params)) != 0;
    }
    if (!failed &&
        (audio_test_absf(got_params.wet_dry_mix - params.wet_dry_mix) > 0.000001f ||
         audio_test_absf(got_params.delay_ms - params.delay_ms) > 0.000001f ||
         audio_test_absf(got_params.feedback - params.feedback) > 0.000001f ||
         audio_test_absf(got_params.lowpass_hz - params.lowpass_hz) > 0.000001f)) {
        fprintf(stderr, "delay voice blob get: wet=%.3f delay=%.3f feedback=%.3f lowpass=%.3f\n",
                got_params.wet_dry_mix, got_params.delay_ms, got_params.feedback, got_params.lowpass_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_typed_getter_reports_defaults(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters got;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed &&
        (audio_test_absf(got.wet_dry_mix - FORGE_DELAY_DEFAULT_WET_DRY_MIX) > 0.000001f ||
         audio_test_absf(got.delay_ms - FORGE_DELAY_DEFAULT_DELAY_MS) > 0.000001f ||
         audio_test_absf(got.feedback - FORGE_DELAY_DEFAULT_FEEDBACK) > 0.000001f ||
         audio_test_absf(got.lowpass_hz - FORGE_DELAY_DEFAULT_LOWPASS_HZ) > 0.000001f)) {
        fprintf(stderr, "delay typed defaults: wet=%.3f delay=%.3f feedback=%.3f lowpass=%.3f\n",
                got.wet_dry_mix, got.delay_ms, got.feedback, got.lowpass_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_wrong_effect_kind_for_typed_api(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeBiquadParameters biquad_params;
    ForgeEffect *effect = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = forge_create_biquad(&effect, 0) != 0;
    }
    if (!failed) {
        desc.effect = effect;
        desc.initial_state = 1;
        desc.output_channels = channels;
        chain.effect_count = 1;
        chain.effects = &desc;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                                 NULL, NULL, &chain) != 0;
    }
    if (!failed) {
        biquad_params.type = ForgeBiquadLowPass;
        biquad_params.frequency_hz = 100.0f;
        biquad_params.q = 1.0f;
        biquad_params.gain_db = 0.0f;
        biquad_params.wet_dry_mix = 1.0f;
        failed = forge_voice_set_effect_parameters(voice, 0, &biquad_params, sizeof(biquad_params),
                                                   FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = check_result("delay_wrong_kind_ramp",
                              forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                                       FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("delay_wrong_kind_get", forge_voice_get_delay_parameters(voice, 0, &got),
                              ForgeResultInvalidCall);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_invalid_typed_arguments(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayTarget target;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = check_result("delay_null_target",
                              forge_voice_ramp_delay_parameters_frames(voice, 0, NULL, quantum,
                                                                       FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        target = delay_target(0, 0.0f, 0.0f, 0.0f);
        failed = check_result("delay_empty_mask",
                              forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                                       FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        target = delay_target(0x80000000u, 0.0f, 0.0f, 0.0f);
        failed = check_result("delay_unknown_mask",
                              forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                                       FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
        failed = check_result("delay_batch_all",
                              forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                                       FORGE_AUDIO_BATCH_ALL),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("delay_get_null", forge_voice_get_delay_parameters(voice, 0, NULL),
                              ForgeResultInvalidCall);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_ramp_ms_uses_engine_rate(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 2,
        buffer_frames = 8
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_ms(voice, 0, &target, 4.0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 75.0f) > 0.000001f) {
        fprintf(stderr, "delay ramp ms: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_typed_automation_clamping_zero_and_mid_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 2,
        ramp_frames = 4,
        buffer_frames = 12
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(50.0f, 1.0f, 0.35f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeDelayTarget target;
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[buffer_frames];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target = delay_target(FORGE_DELAY_TARGET_ALL, 100.0f, 0.95f, 1000.0f);
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, ramp_frames,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, 2);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - 75.0f) > 0.000001f ||
                    audio_test_absf(got.feedback - 0.65f) > 0.000001f ||
                    audio_test_absf(got.lowpass_hz - 500.0f) > 0.000001f ||
                    audio_test_absf(got.delay_ms - params.delay_ms) > 0.000001f)) {
        fprintf(stderr, "delay mid ramp: wet=%.8f delay=%.8f feedback=%.8f lowpass=%.8f\n",
                got.wet_dry_mix, got.delay_ms, got.feedback, got.lowpass_hz);
        failed = 1;
    }
    if (!failed) {
        target = delay_target(FORGE_DELAY_TARGET_ALL, -10.0f, 10.0f, -100.0f);
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, ramp_frames,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, ramp_frames);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - FORGE_DELAY_MIN_WET_DRY_MIX) > 0.000001f ||
                    audio_test_absf(got.feedback - FORGE_DELAY_MAX_FEEDBACK) > 0.000001f ||
                    audio_test_absf(got.lowpass_hz - FORGE_DELAY_MIN_LOWPASS_HZ) > 0.000001f)) {
        fprintf(stderr, "delay clamped ramp: wet=%.8f feedback=%.8f lowpass=%.8f\n",
                got.wet_dry_mix, got.feedback, got.lowpass_hz);
        failed = 1;
    }
    if (!failed) {
        target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 25.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, 0,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - FORGE_DELAY_MIN_WET_DRY_MIX) > 0.000001f) {
        fprintf(stderr, "delay zero before boundary: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 25.0f) > 0.000001f) {
        fprintf(stderr, "delay zero snap: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_field_mask_preserves_other_active_ramps(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4,
        buffer_frames = 12
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(0.0f, 1.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeDelayTarget wet_target;
    ForgeDelayTarget feedback_target;
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        wet_target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &wet_target, 8,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        feedback_target = delay_target(FORGE_DELAY_TARGET_FEEDBACK, 0.0f, 0.95f, 0.0f);
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &feedback_target, quantum,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - 50.0f) > 0.000001f ||
                    audio_test_absf(got.feedback - 0.95f) > 0.000001f)) {
        fprintf(stderr, "delay field mask partial: wet=%.8f feedback=%.8f\n", got.wet_dry_mix, got.feedback);
        failed = 1;
    }
    if (!failed) {
        wet_target.wet_dry_mix = 0.0f;
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &wet_target, quantum,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix) > 0.000001f ||
                    audio_test_absf(got.feedback - 0.95f) > 0.000001f)) {
        fprintf(stderr, "delay field mask preserve: wet=%.8f feedback=%.8f\n", got.wet_dry_mix, got.feedback);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_deferred_ramp_waits_for_apply(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4,
        buffer_frames = 12
    };
    const ForgeAudioBatchId batch_id = 1401;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - FORGE_DELAY_DEFAULT_WET_DRY_MIX) > 0.000001f) {
        fprintf(stderr, "delay deferred before apply: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 100.0f) > 0.000001f) {
        fprintf(stderr, "delay deferred after apply: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_blob_set_cancels_typed_automation_when_applied(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4,
        buffer_frames = 12
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters blob = delay_params(25.0f, 2.0f, 0.2f, 100.0f);
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum * 2,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &blob, sizeof(blob), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - blob.wet_dry_mix) > 0.000001f ||
                    audio_test_absf(got.delay_ms - blob.delay_ms) > 0.000001f ||
                    audio_test_absf(got.feedback - blob.feedback) > 0.000001f ||
                    audio_test_absf(got.lowpass_hz - blob.lowpass_hz) > 0.000001f)) {
        fprintf(stderr, "delay blob cancel: wet=%.8f delay=%.8f feedback=%.8f lowpass=%.8f\n",
                got.wet_dry_mix, got.delay_ms, got.feedback, got.lowpass_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_blob_set_does_not_delete_pending_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4,
        buffer_frames = 12
    };
    const ForgeAudioBatchId batch_id = 1402;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters blob = delay_params(25.0f, 2.0f, 0.2f, 100.0f);
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &blob, sizeof(blob), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - blob.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "delay pending before apply: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - 100.0f) > 0.000001f ||
                    audio_test_absf(got.delay_ms - blob.delay_ms) > 0.000001f)) {
        fprintf(stderr, "delay pending after apply: wet=%.8f delay=%.8f\n", got.wet_dry_mix, got.delay_ms);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_batch_blob_then_ramp_order(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1403;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters blob = delay_params(25.0f, 2.0f, 0.2f, 100.0f);
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &blob, sizeof(blob), batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - 100.0f) > 0.000001f ||
                    audio_test_absf(got.delay_ms - blob.delay_ms) > 0.000001f ||
                    audio_test_absf(got.feedback - blob.feedback) > 0.000001f ||
                    audio_test_absf(got.lowpass_hz - blob.lowpass_hz) > 0.000001f)) {
        fprintf(stderr, "delay blob then ramp order: wet=%.8f delay=%.8f feedback=%.8f lowpass=%.8f\n",
                got.wet_dry_mix, got.delay_ms, got.feedback, got.lowpass_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_batch_ramp_then_blob_order(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1404;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters blob = delay_params(75.0f, 2.0f, 0.2f, 100.0f);
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &blob, sizeof(blob), batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - blob.wet_dry_mix) > 0.000001f ||
                    audio_test_absf(got.delay_ms - blob.delay_ms) > 0.000001f ||
                    audio_test_absf(got.feedback - blob.feedback) > 0.000001f ||
                    audio_test_absf(got.lowpass_hz - blob.lowpass_hz) > 0.000001f)) {
        fprintf(stderr, "delay ramp then blob order: wet=%.8f delay=%.8f feedback=%.8f lowpass=%.8f\n",
                got.wet_dry_mix, got.delay_ms, got.feedback, got.lowpass_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_delay_ramp_advances_on_engine_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "stopped delay ramp: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_disabled_delay_ramp_advances_on_engine_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4,
        buffer_frames = 8
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f);
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_disable_effect(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "disabled delay ramp: wet=%.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_wet_dry_ramp_renders_expected_mix(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 4
    };
    static const float expected[frames] = {1.0f, 0.25f, 0.0f, 0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(0.0f, 1.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeDelayTarget target = delay_target(FORGE_DELAY_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f);
    float source[frames] = {1.0f, 0.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, frames,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    if (!failed) {
        failed = audio_test_check_equal("delay_wet_dry_ramp", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_feedback_lowpass_ramp_getter_state(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4,
        buffer_frames = 8
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(50.0f, 1.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeDelayTarget target =
        delay_target(FORGE_DELAY_TARGET_FEEDBACK | FORGE_DELAY_TARGET_LOWPASS_HZ, 0.0f, 0.8f, 200.0f);
    ForgeDelayParameters got;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    forge_zero(source, sizeof(source));
    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_delay_parameters_frames(voice, 0, &target, quantum,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_delay_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.feedback - 0.8f) > 0.000001f ||
                    audio_test_absf(got.lowpass_hz - 200.0f) > 0.000001f ||
                    audio_test_absf(got.delay_ms - params.delay_ms) > 0.000001f)) {
        fprintf(stderr, "delay feedback/lowpass ramp: delay=%.8f feedback=%.8f lowpass=%.8f\n",
                got.delay_ms, got.feedback, got.lowpass_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_impulse_after_expected_frames(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 5
    };
    static const float expected[frames] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 2.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    float source[frames] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    if (!failed) {
        failed = audio_test_check_equal("delay_impulse", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_wet_dry_mix(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 3
    };
    static const float expected[frames] = {0.75f, 0.25f, 0.0f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(25.0f, 1.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    float source[frames] = {1.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    if (!failed) {
        failed = audio_test_check_equal("delay_wet_dry", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_feedback_repeats_decay(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 5
    };
    static const float expected[frames] = {0.0f, 1.0f, 0.5f, 0.25f, 0.125f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 1.0f, 0.5f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    float source[frames] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    if (!failed) {
        failed = audio_test_check_equal("delay_feedback", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_feedback_clamp_prevents_runaway(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 4
    };
    static const float expected[frames] = {0.0f, 1.0f, 0.95f, 0.9025f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 1.0f, 10.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    float source[frames] = {1.0f, 0.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    if (!failed) {
        failed = audio_test_check_equal("delay_feedback_clamp", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_lowpass_damps_feedback_path(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 3
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 1.0f, 0.5f, 10.0f);
    float source[frames] = {1.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    if (!failed && !(output[1] > 0.999999f && output[2] > 0.0f && output[2] < 0.1f)) {
        fprintf(stderr, "delay lowpass damping: %.8f %.8f %.8f\n", output[0], output[1], output[2]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_source_timing_uses_render_sample_rate(void) {
    enum {
        channels = 1,
        source_rate = 1000,
        render_rate = 2000,
        frames = 8,
        source_frames = 8
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 2.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    float source[source_frames] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, render_rate, frames);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, source_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, source_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, frames);
    }
    for (uint32_t i = 0; !failed && i < 4; i += 1) {
        if (audio_test_absf(output[i]) > 0.000001f) {
            fprintf(stderr, "delay render-rate timing[%u]: expected silence, got %.8f\n", i, output[i]);
            failed = 1;
        }
    }
    if (!failed && audio_test_absf(output[4]) < 0.000001f && audio_test_absf(output[5]) < 0.000001f) {
        fprintf(stderr, "delay render-rate timing: expected delayed signal near frame 4, got %.8f %.8f\n",
                output[4], output[5]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_delay_in_place_processing(void) {
    ForgeEffect *effect = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 1.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeEffectProcessBuffer buffer;
    float samples[3] = {1.0f, 0.0f, 0.0f};
    float expected[3] = {0.0f, 1.0f, 0.0f};
    int failed = 0;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        failed = lock_delay_effect(effect, 1, 1000);
    }
    if (!failed) {
        buffer.buffer = samples;
        buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        buffer.valid_frame_count = 3;
        effect->process(effect, 1, &buffer, 1, &buffer, 1);
        failed = audio_test_check_equal("delay_in_place", samples, expected, 3, 0.000001f);
    }

    if (effect != NULL) {
        effect->unlock_for_process(effect);
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_disabled_clears_delayed_samples(void) {
    ForgeEffect *effect = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 2.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeEffectProcessBuffer input_buffer;
    ForgeEffectProcessBuffer output_buffer;
    float input[1];
    float output[1];
    float collected[4];
    int failed = 0;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        failed = lock_delay_effect(effect, 1, 1000);
    }
    if (!failed) {
        input_buffer.buffer = input;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        input_buffer.valid_frame_count = 1;
        output_buffer.buffer = output;
        output_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        output_buffer.valid_frame_count = 1;

        input[0] = 1.0f;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        collected[0] = output[0];

        input[0] = 0.0f;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 0);
        collected[1] = output[0];

        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        collected[2] = output[0];
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        collected[3] = output[0];

        failed = audio_test_check_constant("delay_disabled_clear", collected, 4, 1, 0.0f, 0.000001f);
    }

    if (effect != NULL) {
        effect->unlock_for_process(effect);
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_reset_clears_delayed_samples(void) {
    ForgeEffect *effect = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 2.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeEffectProcessBuffer input_buffer;
    ForgeEffectProcessBuffer output_buffer;
    float input[1];
    float output[1];
    float collected[3];
    int failed = 0;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        failed = lock_delay_effect(effect, 1, 1000);
    }
    if (!failed) {
        input_buffer.buffer = input;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        input_buffer.valid_frame_count = 1;
        output_buffer.buffer = output;
        output_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        output_buffer.valid_frame_count = 1;

        input[0] = 1.0f;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        effect->reset(effect);

        input[0] = 0.0f;
        for (uint32_t i = 0; i < 3; i += 1) {
            effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
            collected[i] = output[0];
        }

        failed = audio_test_check_constant("delay_reset_clear", collected, 3, 1, 0.0f, 0.000001f);
    }

    if (effect != NULL) {
        effect->unlock_for_process(effect);
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_tail_flags_clear_after_consumed_sample(void) {
    ForgeEffect *effect = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 1.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeEffectProcessBuffer input_buffer;
    ForgeEffectProcessBuffer output_buffer;
    float input[1];
    float output[1];
    int failed = 0;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        failed = lock_delay_effect(effect, 1, 1000);
    }
    if (!failed) {
        input_buffer.buffer = input;
        input_buffer.valid_frame_count = 1;
        output_buffer.buffer = output;
        output_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        output_buffer.valid_frame_count = 1;

        input[0] = 1.0f;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        if (output_buffer.buffer_flags != FORGE_EFFECT_BUFFER_VALID) {
            fprintf(stderr, "delay tail flags initial: got %u\n", output_buffer.buffer_flags);
            failed = 1;
        }
    }
    if (!failed) {
        input[0] = 0.0f;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_SILENT;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        if (audio_test_absf(output[0] - 1.0f) > 0.000001f ||
            output_buffer.buffer_flags != FORGE_EFFECT_BUFFER_VALID) {
            fprintf(stderr, "delay tail flags consumed: output=%.8f flags=%u\n", output[0],
                    output_buffer.buffer_flags);
            failed = 1;
        }
    }
    if (!failed) {
        input[0] = 0.0f;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_SILENT;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        if (audio_test_absf(output[0]) > 0.000001f || output_buffer.buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            fprintf(stderr, "delay tail flags stale: output=%.8f flags=%u\n", output[0], output_buffer.buffer_flags);
            failed = 1;
        }
    }

    if (effect != NULL) {
        effect->unlock_for_process(effect);
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_grow_discards_orphaned_samples(void) {
    ForgeEffect *effect = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 4.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    ForgeEffectProcessBuffer input_buffer;
    ForgeEffectProcessBuffer output_buffer;
    float input[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float output[4];
    int failed = 0;

    failed = check_result("delay_create", forge_create_delay(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        failed = lock_delay_effect(effect, 1, 1000);
    }
    if (!failed) {
        input_buffer.buffer = input;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        input_buffer.valid_frame_count = 4;
        output_buffer.buffer = output;
        output_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        output_buffer.valid_frame_count = 4;

        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        failed = audio_test_check_constant("delay_orphan_seed", output, 4, 1, 0.0f, 0.000001f);
    }
    if (!failed) {
        params.delay_ms = 1.0f;
        effect->set_parameters(effect, &params, sizeof(params));
        input[0] = 0.0f;
        input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_SILENT;
        input_buffer.valid_frame_count = 1;
        output_buffer.valid_frame_count = 1;

        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        if (audio_test_absf(output[0] - 1.0f) > 0.000001f ||
            output_buffer.buffer_flags != FORGE_EFFECT_BUFFER_VALID) {
            fprintf(stderr, "delay orphan shrink: output=%.8f flags=%u\n", output[0], output_buffer.buffer_flags);
            failed = 1;
        }
    }
    if (!failed) {
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        if (audio_test_absf(output[0]) > 0.000001f || output_buffer.buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            fprintf(stderr, "delay orphan drained: output=%.8f flags=%u\n", output[0], output_buffer.buffer_flags);
            failed = 1;
        }
    }
    if (!failed) {
        params.delay_ms = 4.0f;
        effect->set_parameters(effect, &params, sizeof(params));
        input_buffer.valid_frame_count = 4;
        output_buffer.valid_frame_count = 4;

        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
        if (output_buffer.buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            fprintf(stderr, "delay orphan grow flags: got %u\n", output_buffer.buffer_flags);
            failed = 1;
        }
        if (!failed) {
            failed = audio_test_check_constant("delay_orphan_grow", output, 4, 1, 0.0f, 0.000001f);
        }
    }

    if (effect != NULL) {
        effect->unlock_for_process(effect);
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_delay_tail_drains_with_play_tails(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 1
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters params = delay_params(100.0f, 2.0f, 0.0f, FORGE_DELAY_BYPASS_LOWPASS_HZ);
    float source[1] = {1.0f};
    float output[3];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_delay_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, 1, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, &output[0], 1);
    }
    if (!failed) {
        /* Natural source exhaustion is not the delay-tail contract. Explicit
         * PLAY_TAILS asks the current effect-chain machinery to drain delayed state.
         */
        failed = forge_source_voice_stop(voice, FORGE_AUDIO_PLAY_TAILS, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    for (uint32_t i = 1; !failed && i < 3; i += 1) {
        failed = audio_render_harness_render(&harness, &output[i], 1);
    }
    if (!failed && (audio_test_absf(output[0]) > 0.000001f || audio_test_absf(output[1]) > 0.000001f ||
                    audio_test_absf(output[2] - 1.0f) > 0.000001f)) {
        fprintf(stderr, "delay tail: %.8f %.8f %.8f\n", output[0], output[1], output[2]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
