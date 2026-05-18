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

static ForgeBiquadParameters biquad_params(ForgeBiquadType type, float frequency_hz, float q, float gain_db,
                                           float wet_dry_mix) {
    ForgeBiquadParameters params;

    params.type = type;
    params.frequency_hz = frequency_hz;
    params.q = q;
    params.gain_db = gain_db;
    params.wet_dry_mix = wet_dry_mix;
    return params;
}

static ForgeBiquadTarget biquad_target(uint32_t field_mask, float frequency_hz, float q, float gain_db,
                                       float wet_dry_mix) {
    ForgeBiquadTarget target;

    target.field_mask = field_mask;
    target.frequency_hz = frequency_hz;
    target.q = q;
    target.gain_db = gain_db;
    target.wet_dry_mix = wet_dry_mix;
    return target;
}

static int lock_biquad_effect(ForgeEffect *effect, uint32_t channels, uint32_t sample_rate) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeResult result;

    input_lock.format = &format;
    input_lock.max_frame_count = 128;
    output_lock.format = &format;
    output_lock.max_frame_count = 128;

    result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "biquad lock failed: %d\n", result);
        return 1;
    }

    return 0;
}

static void process_biquad(ForgeEffect *effect, const float *input, float *output, uint32_t frames, uint32_t channels,
                           int32_t enabled) {
    ForgeEffectProcessBuffer input_buffer;
    ForgeEffectProcessBuffer output_buffer;

    input_buffer.buffer = (void *)input;
    input_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
    input_buffer.valid_frame_count = frames;
    output_buffer.buffer = output;
    output_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
    output_buffer.valid_frame_count = frames;

    effect->process(effect, 1, &input_buffer, 1, &output_buffer, enabled);
    (void)channels;
}

static int create_biquad_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                                uint32_t source_sample_rate, const ForgeBiquadParameters *params) {
    ForgeAudioFormat format = audio_test_float_format(channels, source_sample_rate);
    ForgeEffect *biquad = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeResult result;

    result = forge_create_biquad(&biquad, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_biquad failed: %d\n", result);
        return 1;
    }

    desc.effect = biquad;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    result = forge_audio_create_source_voice(harness->audio, voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                             NULL, NULL, &chain);
    if (result != ForgeResultSuccess) {
        forge_effect_destroy(biquad);
        fprintf(stderr, "forge_audio_create_source_voice biquad failed: %d\n", result);
        return 1;
    }

    if (params != NULL) {
        result = forge_voice_set_effect_parameters(*voice, 0, params, sizeof(*params), FORGE_AUDIO_BATCH_IMMEDIATE);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "forge_voice_set_effect_parameters biquad failed: %d\n", result);
            return 1;
        }
    }

    return 0;
}

int test_biquad_creation_kind_and_destroy(void) {
    ForgeEffect *effect = NULL;
    ForgeResult result = forge_create_biquad(&effect, 0);
    int failed = 0;

    if (result != ForgeResultSuccess || effect == NULL) {
        fprintf(stderr, "biquad create: result=%d effect=%p\n", result, (void *)effect);
        failed = 1;
    }
    if (!failed && effect->kind != ForgeEffectKindBiquad) {
        fprintf(stderr, "biquad kind: got %d\n", effect->kind);
        failed = 1;
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_format_validation_rejects_channel_change(void) {
    ForgeEffect *effect = NULL;
    ForgeAudioFormat input_format = audio_test_float_format(1, 48000);
    ForgeAudioFormat output_format = audio_test_float_format(2, 48000);
    ForgeAudioFormat pcm_format = audio_test_float_format(1, 48000);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeResult result;
    int failed = 0;

    pcm_format.format_tag = FORGE_AUDIO_FORMAT_PCM;

    failed = check_result("biquad_create", forge_create_biquad(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        input_lock.format = &input_format;
        input_lock.max_frame_count = 64;
        output_lock.format = &output_format;
        output_lock.max_frame_count = 64;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("biquad_channel_change", result, ForgeResultEffectFormatUnsupported);
    }
    if (!failed) {
        output_lock.format = &input_format;
        input_lock.format = &pcm_format;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("biquad_pcm_format", result, ForgeResultEffectFormatUnsupported);
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_parameter_clamping(void) {
    ForgeEffect *effect = NULL;
    ForgeBiquadParameters set_params = biquad_params((ForgeBiquadType)99, -100.0f, -1.0f, 100.0f, 2.0f);
    ForgeBiquadParameters got_params;
    int failed = 0;

    failed = check_result("biquad_create", forge_create_biquad(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &set_params, sizeof(set_params));
        effect->get_parameters(effect, &got_params, sizeof(got_params));
        if (got_params.type != FORGE_BIQUAD_DEFAULT_TYPE ||
            got_params.frequency_hz != FORGE_BIQUAD_MIN_FREQUENCY_HZ ||
            got_params.q != FORGE_BIQUAD_MIN_Q ||
            got_params.gain_db != FORGE_BIQUAD_MAX_GAIN_DB ||
            got_params.wet_dry_mix != FORGE_BIQUAD_MAX_WET_DRY_MIX) {
            fprintf(stderr, "biquad clamped params: type=%d freq=%.3f q=%.3f gain=%.3f wet=%.3f\n",
                    got_params.type, got_params.frequency_hz, got_params.q, got_params.gain_db,
                    got_params.wet_dry_mix);
            failed = 1;
        }
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_blob_parameter_set_get_on_voice(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadPeaking, 120.0f, 2.0f, 6.0f, 0.25f);
    ForgeBiquadParameters got_params;
    float source[quantum] = {0.0f, 0.0f, 0.0f, 0.0f};
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_biquad_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got_params) != 0;
    }
    if (!failed &&
        (got_params.type != FORGE_BIQUAD_DEFAULT_TYPE ||
         audio_test_absf(got_params.frequency_hz - 499.999f) > 0.00001f)) {
        fprintf(stderr, "biquad voice blob queued get: type=%d freq=%.3f\n", got_params.type,
                got_params.frequency_hz);
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
        failed = forge_voice_get_biquad_parameters(voice, 0, &got_params) != 0;
    }
    if (!failed &&
        (got_params.type != params.type || audio_test_absf(got_params.frequency_hz - params.frequency_hz) > 0.000001f ||
         audio_test_absf(got_params.q - params.q) > 0.000001f ||
         audio_test_absf(got_params.gain_db - params.gain_db) > 0.000001f ||
         audio_test_absf(got_params.wet_dry_mix - params.wet_dry_mix) > 0.000001f)) {
        fprintf(stderr, "biquad voice blob get: type=%d freq=%.3f q=%.3f gain=%.3f wet=%.3f\n",
                got_params.type, got_params.frequency_hz, got_params.q, got_params.gain_db, got_params.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_biquad_wrong_effect_kind_for_typed_api(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeDelayParameters delay = {0.0f, 10.0f, 0.0f, 0.0f};
    ForgeEffect *effect = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeBiquadTarget target = biquad_target(FORGE_BIQUAD_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.5f);
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = forge_create_delay(&effect, 0) != 0;
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
        failed = forge_voice_set_effect_parameters(voice, 0, &delay, sizeof(delay), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = check_result("biquad_wrong_kind",
                              forge_voice_ramp_biquad_parameters_frames(voice, 0, &target, quantum,
                                                                        FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_biquad_wet_dry_and_in_place(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 8
    };
    static const float input[frames] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f, 0.7f, -0.8f};
    float output[frames];
    float inplace[frames];
    ForgeEffect *effect = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadLowPass, 80.0f, 1.0f, 0.0f, 0.0f);
    int failed = 0;

    failed = check_result("biquad_create", forge_create_biquad(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        failed = lock_biquad_effect(effect, channels, sample_rate);
    }
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        process_biquad(effect, input, output, frames, channels, 1);
        failed = audio_test_check_equal("biquad_wet_zero", output, input, frames, 0.000001f);
    }
    if (!failed) {
        effect->reset(effect);
        params.wet_dry_mix = 1.0f;
        effect->set_parameters(effect, &params, sizeof(params));
        forge_memcpy(inplace, input, sizeof(input));
        process_biquad(effect, inplace, inplace, frames, channels, 1);
        effect->reset(effect);
        process_biquad(effect, input, output, frames, channels, 1);
        failed = audio_test_check_equal("biquad_inplace", inplace, output, frames, 0.000001f);
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_lowpass_highpass_functional(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 64
    };
    float input[frames];
    float output[frames];
    ForgeEffect *effect = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadLowPass, 80.0f, 1.0f, 0.0f, 1.0f);
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        input[i] = 1.0f;
    }

    failed = check_result("biquad_create", forge_create_biquad(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        failed = lock_biquad_effect(effect, channels, sample_rate);
    }
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        process_biquad(effect, input, output, frames, channels, 1);
        if (output[frames - 1] < 0.9f || output[frames - 1] > 1.1f) {
            fprintf(stderr, "biquad lowpass dc expected near 1, got %.8f\n", output[frames - 1]);
            failed = 1;
        }
    }
    if (!failed) {
        effect->reset(effect);
        params.type = ForgeBiquadHighPass;
        effect->set_parameters(effect, &params, sizeof(params));
        process_biquad(effect, input, output, frames, channels, 1);
        if (audio_test_absf(output[frames - 1]) > 0.05f) {
            fprintf(stderr, "biquad highpass dc expected near 0, got %.8f\n", output[frames - 1]);
            failed = 1;
        }
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_disabled_and_reset_clear_state(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 16
    };
    float impulse[frames];
    float zeros[frames];
    float output[frames];
    ForgeEffect *effect = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadLowPass, 60.0f, 1.0f, 0.0f, 1.0f);
    int failed = 0;

    forge_zero(impulse, sizeof(impulse));
    forge_zero(zeros, sizeof(zeros));
    impulse[0] = 1.0f;

    failed = check_result("biquad_create", forge_create_biquad(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        failed = lock_biquad_effect(effect, channels, sample_rate);
    }
    if (!failed) {
        effect->set_parameters(effect, &params, sizeof(params));
        process_biquad(effect, impulse, output, frames, channels, 1);
        process_biquad(effect, zeros, output, frames, channels, 0);
        failed = audio_test_check_equal("biquad_disabled_passthrough", output, zeros, frames, 0.000001f);
    }
    if (!failed) {
        process_biquad(effect, zeros, output, frames, channels, 1);
        failed = audio_test_check_equal("biquad_disabled_reset_state", output, zeros, frames, 0.000001f);
    }
    if (!failed) {
        process_biquad(effect, impulse, output, frames, channels, 1);
        effect->reset(effect);
        process_biquad(effect, zeros, output, frames, channels, 1);
        failed = audio_test_check_equal("biquad_reset_state", output, zeros, frames, 0.000001f);
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_coefficients_remain_finite_for_extremes(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 16
    };
    static const ForgeBiquadType types[] = {ForgeBiquadLowPass,   ForgeBiquadHighPass, ForgeBiquadBandPass,
                                            ForgeBiquadNotch,     ForgeBiquadLowShelf, ForgeBiquadHighShelf,
                                            ForgeBiquadPeaking,   ForgeBiquadAllPass};
    float input[frames];
    float output[frames];
    ForgeEffect *effect = NULL;
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        input[i] = (i & 1) ? -1.0f : 1.0f;
    }

    failed = check_result("biquad_create", forge_create_biquad(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        failed = lock_biquad_effect(effect, channels, sample_rate);
    }
    for (uint32_t t = 0; !failed && t < sizeof(types) / sizeof(types[0]); t += 1) {
        ForgeBiquadParameters params = biquad_params(types[t], FORGE_BIQUAD_MAX_FREQUENCY_HZ,
                                                     FORGE_BIQUAD_MAX_Q, FORGE_BIQUAD_MAX_GAIN_DB, 1.0f);

        effect->reset(effect);
        effect->set_parameters(effect, &params, sizeof(params));
        process_biquad(effect, input, output, frames, channels, 1);
        for (uint32_t i = 0; i < frames; i += 1) {
            if (output[i] != output[i] || audio_test_absf(output[i]) > 1000000.0f) {
                fprintf(stderr, "biquad finite type=%d index=%u value=%.8f\n", types[t], i, output[i]);
                failed = 1;
                break;
            }
        }
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_biquad_typed_automation_getter_and_masks(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadAllPass, 100.0f, 1.0f, 0.0f, 0.0f);
    ForgeBiquadTarget wet_target = biquad_target(FORGE_BIQUAD_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 1.0f);
    ForgeBiquadTarget gain_target = biquad_target(FORGE_BIQUAD_TARGET_GAIN_DB, 0.0f, 0.0f, 12.0f, 0.0f);
    ForgeBiquadParameters got;
    float source[quantum * 3];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < quantum * 3; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_biquad_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, quantum * 2, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_biquad_parameters_frames(voice, 0, &wet_target, quantum * 2,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_ramp_biquad_parameters_frames(voice, 0, &gain_target, quantum * 2,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - 0.5f) > 0.000001f ||
                    audio_test_absf(got.gain_db - 6.0f) > 0.000001f)) {
        fprintf(stderr, "biquad automation partial: wet=%.8f gain=%.8f\n", got.wet_dry_mix, got.gain_db);
        failed = 1;
    }
    if (!failed) {
        wet_target.wet_dry_mix = 0.0f;
        failed = forge_voice_ramp_biquad_parameters_frames(voice, 0, &wet_target, quantum,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix) > 0.000001f ||
                    audio_test_absf(got.gain_db - 12.0f) > 0.000001f)) {
        fprintf(stderr, "biquad field mask preserve: wet=%.8f gain=%.8f\n", got.wet_dry_mix, got.gain_db);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_biquad_deferred_zero_duration_and_blob_cancel(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadAllPass, 100.0f, 1.0f, 0.0f, 0.0f);
    ForgeBiquadParameters blob = biquad_params(ForgeBiquadLowPass, 200.0f, 2.0f, 3.0f, 0.25f);
    ForgeBiquadTarget target = biquad_target(FORGE_BIQUAD_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 1.0f);
    ForgeBiquadParameters got;
    float source[quantum * 2];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < quantum * 2; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_biquad_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, quantum * 3, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "biquad initial wet after render: %.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = forge_voice_ramp_biquad_parameters_frames(voice, 0, &target, 0, 17) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "biquad deferred zero visible before apply: %.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, 17) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 1.0f) > 0.000001f) {
        fprintf(stderr, "biquad deferred zero after apply: %.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        target.wet_dry_mix = 0.0f;
        failed = forge_voice_ramp_biquad_parameters_frames(voice, 0, &target, quantum * 2,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &blob, sizeof(blob), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (got.type != blob.type || audio_test_absf(got.wet_dry_mix - blob.wet_dry_mix) > 0.000001f)) {
        fprintf(stderr, "biquad blob cancel: type=%d wet=%.8f\n", got.type, got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_biquad_blob_set_does_not_delete_pending_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1301;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeBiquadParameters blob = biquad_params(ForgeBiquadLowPass, 200.0f, 2.0f, 3.0f, 0.25f);
    ForgeBiquadTarget target = biquad_target(FORGE_BIQUAD_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 1.0f);
    ForgeBiquadParameters got;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_biquad_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_ramp_biquad_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &blob, sizeof(blob),
                                                   FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (got.type != blob.type || audio_test_absf(got.wet_dry_mix - blob.wet_dry_mix) > 0.000001f)) {
        fprintf(stderr, "biquad pending ramp before apply: type=%d wet=%.8f\n", got.type, got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (got.type != blob.type || audio_test_absf(got.wet_dry_mix - 1.0f) > 0.000001f)) {
        fprintf(stderr, "biquad pending ramp after apply: type=%d wet=%.8f\n", got.type, got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_biquad_uses_render_sample_rate_for_clamp(void) {
    enum {
        channels = 1,
        source_rate = 8000,
        render_rate = 1000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeBiquadParameters params = biquad_params(ForgeBiquadLowPass, 20000.0f, 1.0f, 0.0f, 1.0f);
    ForgeBiquadParameters got;
    float source[quantum] = {0.0f, 0.0f, 0.0f, 0.0f};
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, render_rate, quantum);
    if (!failed) {
        failed = create_biquad_source(&harness, &voice, channels, source_rate, &params);
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
        failed = forge_voice_get_biquad_parameters(voice, 0, &got) != 0;
    }
    if (!failed && got.frequency_hz > 500.0f) {
        fprintf(stderr, "biquad render-rate clamp expected below 500, got %.8f\n", got.frequency_hz);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
