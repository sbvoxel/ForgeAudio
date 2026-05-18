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

static ForgeLimiterParameters limiter_params(float input_gain_db, float ceiling_db, float lookahead_ms,
                                             float release_ms) {
    ForgeLimiterParameters params;

    params.input_gain_db = input_gain_db;
    params.ceiling_db = ceiling_db;
    params.lookahead_ms = lookahead_ms;
    params.release_ms = release_ms;
    return params;
}

static int create_limiter_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                                 uint32_t sample_rate, const ForgeLimiterParameters *params) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeEffect *limiter = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeResult result;

    result = forge_create_limiter(&limiter, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_limiter failed: %d\n", result);
        return 1;
    }

    desc.effect = limiter;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    result = forge_audio_create_source_voice(harness->audio, voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                             NULL, NULL, &chain);
    if (result != ForgeResultSuccess) {
        forge_effect_destroy(limiter);
        fprintf(stderr, "forge_audio_create_source_voice limiter failed: %d\n", result);
        return 1;
    }

    if (params != NULL) {
        result = forge_voice_set_effect_parameters(*voice, 0, params, sizeof(*params), FORGE_AUDIO_BATCH_IMMEDIATE);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "forge_voice_set_effect_parameters limiter failed: %d\n", result);
            return 1;
        }
    }

    return 0;
}

static int create_limiter_submix(AudioRenderHarness *harness, ForgeSubmixVoice **voice, uint32_t channels,
                                 uint32_t sample_rate, const ForgeLimiterParameters *params) {
    ForgeEffect *limiter = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeResult result;

    result = forge_create_limiter(&limiter, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_limiter failed: %d\n", result);
        return 1;
    }

    desc.effect = limiter;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    result = forge_audio_create_submix_voice(harness->audio, voice, channels, sample_rate, 0, 0, NULL, &chain);
    if (result != ForgeResultSuccess) {
        forge_effect_destroy(limiter);
        fprintf(stderr, "forge_audio_create_submix_voice limiter failed: %d\n", result);
        return 1;
    }

    if (params != NULL) {
        result = forge_voice_set_effect_parameters(*voice, 0, params, sizeof(*params), FORGE_AUDIO_BATCH_IMMEDIATE);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "forge_voice_set_effect_parameters limiter submix failed: %d\n", result);
            return 1;
        }
    }

    return 0;
}

int test_limiter_creation_kind_and_destroy(void) {
    ForgeEffect *effect = NULL;
    ForgeResult result = forge_create_limiter(&effect, 0);
    int failed = 0;

    if (result != ForgeResultSuccess || effect == NULL) {
        fprintf(stderr, "limiter create: result=%d effect=%p\n", result, (void *)effect);
        failed = 1;
    }
    if (!failed && effect->kind != ForgeEffectKindLimiter) {
        fprintf(stderr, "limiter kind: got %d\n", effect->kind);
        failed = 1;
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_limiter_format_validation_rejects_channel_change(void) {
    ForgeEffect *effect = NULL;
    ForgeAudioFormat input_format = audio_test_float_format(1, 48000);
    ForgeAudioFormat output_format = audio_test_float_format(2, 48000);
    ForgeAudioFormat pcm_format = audio_test_float_format(1, 48000);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeResult result;
    int failed = 0;

    pcm_format.format_tag = FORGE_AUDIO_FORMAT_PCM;

    failed = check_result("limiter_create", forge_create_limiter(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        input_lock.format = &input_format;
        input_lock.max_frame_count = 64;
        output_lock.format = &output_format;
        output_lock.max_frame_count = 64;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("limiter_channel_change", result, ForgeResultEffectFormatUnsupported);
    }
    if (!failed) {
        output_lock.format = &input_format;
        input_lock.format = &pcm_format;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("limiter_pcm_format", result, ForgeResultEffectFormatUnsupported);
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_limiter_parameter_clamping(void) {
    ForgeEffect *effect = NULL;
    ForgeLimiterParameters set_params =
        limiter_params(-1000.0f, 12.0f, 100.0f, -5.0f);
    ForgeLimiterParameters got_params;
    int failed = 0;

    failed = check_result("limiter_create", forge_create_limiter(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &set_params, sizeof(set_params));
        effect->get_parameters(effect, &got_params, sizeof(got_params));
        if (got_params.input_gain_db != FORGE_LIMITER_MIN_INPUT_GAIN_DB ||
            got_params.ceiling_db != FORGE_LIMITER_MAX_CEILING_DB ||
            got_params.lookahead_ms != FORGE_LIMITER_MAX_LOOKAHEAD_MS ||
            got_params.release_ms != FORGE_LIMITER_MIN_RELEASE_MS) {
            fprintf(stderr, "limiter clamped params: gain=%.3f ceiling=%.3f lookahead=%.3f release=%.3f\n",
                    got_params.input_gain_db, got_params.ceiling_db, got_params.lookahead_ms, got_params.release_ms);
            failed = 1;
        }
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_limiter_below_ceiling_outputs_delayed_input(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 6
    };
    static const float expected[frames] = {0.0f, 0.0f, 0.25f, 0.25f, 0.25f, 0.25f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, 0.0f, 2.0f, 50.0f);
    float source[frames];
    float output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
        failed = audio_test_check_equal("limiter_below_ceiling", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_above_ceiling_limits_sample_peaks(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 8
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, -6.0205999f, 2.0f, 50.0f);
    float source[frames];
    float output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        source[i] = 2.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
    for (uint32_t i = 2; !failed && i < frames; i += 1) {
        if (audio_test_absf(output[i]) > 0.500001f) {
            fprintf(stderr, "limiter ceiling[%u]: got %.8f\n", i, output[i]);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_linked_channels_reduce_all_channels(void) {
    enum {
        channels = 2,
        sample_rate = 1000,
        frames = 4,
        samples = frames * channels
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, -6.0205999f, 0.0f, 100.0f);
    float source[samples];
    float output[samples];
    int failed = 0;

    for (uint32_t frame = 0; frame < frames; frame += 1) {
        source[frame * channels + 0] = 2.0f;
        source[frame * channels + 1] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
    if (!failed && (audio_test_absf(output[0] - 0.5f) > 0.000001f ||
                    audio_test_absf(output[1] - 0.0625f) > 0.000001f)) {
        fprintf(stderr, "limiter linked: L=%.8f R=%.8f\n", output[0], output[1]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_release_recovers_gradually(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 5
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, -6.0205999f, 0.0f, 100.0f);
    float source[frames] = {2.0f, 0.25f, 0.25f, 0.25f, 0.25f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
    if (!failed && !(output[1] < output[2] && output[2] < output[3] && output[3] < 0.25f)) {
        fprintf(stderr, "limiter release: %.8f %.8f %.8f %.8f\n", output[0], output[1], output[2], output[3]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_zero_lookahead_limits_without_delay(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 3
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, -6.0205999f, 0.0f, 50.0f);
    float source[frames] = {2.0f, 0.25f, 0.25f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
    if (!failed && audio_test_absf(output[0] - 0.5f) > 0.000001f) {
        fprintf(stderr, "limiter zero lookahead: got %.8f\n", output[0]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_lookahead_uses_output_sample_rate(void) {
    enum {
        channels = 1,
        sample_rate = 2000,
        frames = 5
    };
    static const float expected[frames] = {0.0f, 0.0f, 0.25f, 0.25f, 0.25f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, 0.0f, 1.0f, 50.0f);
    float source[frames];
    float output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
        failed = audio_test_check_equal("limiter_lookahead_sample_rate", output, expected, frames, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_source_lookahead_uses_render_sample_rate(void) {
    enum {
        channels = 1,
        source_rate = 24000,
        render_rate = 48000,
        frames = 50,
        source_frames = 64
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, 0.0f, 1.0f, 50.0f);
    float source[source_frames];
    float output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < source_frames; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, render_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, source_rate, &params);
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
    for (uint32_t i = 0; !failed && i < 48; i += 1) {
        if (audio_test_absf(output[i]) > 0.000001f) {
            fprintf(stderr, "limiter source render-rate lookahead[%u]: expected silence, got %.8f\n", i, output[i]);
            failed = 1;
        }
    }
    for (uint32_t i = 48; !failed && i < frames; i += 1) {
        if (audio_test_absf(output[i] - 0.25f) > 0.000001f) {
            fprintf(stderr, "limiter source render-rate lookahead[%u]: expected 0.25, got %.8f\n", i, output[i]);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_submix_lookahead_uses_render_sample_rate(void) {
    enum {
        channels = 1,
        submix_rate = 24000,
        render_rate = 48000,
        frames = 50,
        source_frames = 64
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, 0.0f, 1.0f, 50.0f);
    ForgeAudioFormat source_format = audio_test_float_format(channels, submix_rate);
    ForgeSend send;
    ForgeSendList send_list;
    float source[source_frames];
    float output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < source_frames; i += 1) {
        source[i] = 0.25f;
    }

    failed = audio_render_harness_init(&harness, channels, render_rate, frames);
    if (!failed) {
        failed = create_limiter_submix(&harness, &submix, channels, submix_rate, &params);
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &source_format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
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
    for (uint32_t i = 0; !failed && i < 48; i += 1) {
        if (audio_test_absf(output[i]) > 0.000001f) {
            fprintf(stderr, "limiter submix render-rate lookahead[%u]: expected silence, got %.8f\n", i, output[i]);
            failed = 1;
        }
    }
    for (uint32_t i = 48; !failed && i < frames; i += 1) {
        if (audio_test_absf(output[i] - 0.25f) > 0.000001f) {
            fprintf(stderr, "limiter submix render-rate lookahead[%u]: expected 0.25, got %.8f\n", i, output[i]);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_blob_parameter_set_updates_render(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, -6.0205999f, 0.0f, 50.0f);
    ForgeLimiterParameters got_params;
    float source[frames];
    float output[frames];
    int failed = 0;

    for (uint32_t i = 0; i < frames; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_effect_parameters(voice, 0, &got_params, sizeof(got_params)) != 0;
    }
    if (!failed && audio_test_absf(got_params.ceiling_db - FORGE_LIMITER_DEFAULT_CEILING_DB) > 0.000001f) {
        fprintf(stderr, "limiter blob queued getter: got ceiling %.8f\n", got_params.ceiling_db);
        failed = 1;
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
        failed = forge_voice_get_effect_parameters(voice, 0, &got_params, sizeof(got_params)) != 0;
    }
    if (!failed && audio_test_absf(got_params.ceiling_db - params.ceiling_db) > 0.000001f) {
        fprintf(stderr, "limiter blob applied getter: got ceiling %.8f\n", got_params.ceiling_db);
        failed = 1;
    }
    if (!failed && audio_test_absf(output[0] - 0.5f) > 0.000001f) {
        fprintf(stderr, "limiter blob render: got %.8f\n", output[0]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_tail_drains_delayed_samples(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 1
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, 0.0f, 2.0f, 50.0f);
    float source[1] = {0.25f};
    float output[3];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
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
        /* Natural buffer exhaustion follows the current effect-chain buffer flag
         * heuristic. Explicit PLAY_TAILS is the current contract for draining delayed
         * limiter lookahead samples after a caller stops a source.
         */
        failed = forge_source_voice_stop(voice, FORGE_AUDIO_PLAY_TAILS, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    for (uint32_t i = 1; !failed && i < 3; i += 1) {
        failed = audio_render_harness_render(&harness, &output[i], 1);
    }
    if (!failed && (audio_test_absf(output[0]) > 0.000001f || audio_test_absf(output[1]) > 0.000001f ||
                    audio_test_absf(output[2] - 0.25f) > 0.000001f)) {
        fprintf(stderr, "limiter tail: %.8f %.8f %.8f\n", output[0], output[1], output[2]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_limiter_disabled_clears_delayed_samples(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        quantum = 1,
        frames = 6
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeLimiterParameters params = limiter_params(0.0f, 0.0f, 2.0f, 50.0f);
    float source[frames] = {0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float output[4];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_limiter_source(&harness, &voice, channels, sample_rate, &params);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, &output[0], 1);
    }
    if (!failed) {
        failed = forge_voice_disable_effect(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, &output[1], 1);
    }
    if (!failed) {
        failed = forge_voice_enable_effect(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    for (uint32_t i = 2; !failed && i < 4; i += 1) {
        failed = audio_render_harness_render(&harness, &output[i], 1);
    }
    if (!failed && (audio_test_absf(output[0]) > 0.000001f || audio_test_absf(output[1]) > 0.000001f ||
                    audio_test_absf(output[2]) > 0.000001f || audio_test_absf(output[3]) > 0.000001f)) {
        fprintf(stderr, "limiter disabled stale output: %.8f %.8f %.8f %.8f\n", output[0], output[1], output[2],
                output[3]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
