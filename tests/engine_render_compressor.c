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

static ForgeCompressorParameters compressor_params(float threshold_db, float ratio, float knee_db, float attack_ms,
                                                   float release_ms, float makeup_gain_db, float wet_dry_mix) {
    ForgeCompressorParameters params;

    params.threshold_db = threshold_db;
    params.ratio = ratio;
    params.knee_db = knee_db;
    params.attack_ms = attack_ms;
    params.release_ms = release_ms;
    params.makeup_gain_db = makeup_gain_db;
    params.wet_dry_mix = wet_dry_mix;
    return params;
}

static float compressor_test_db_to_linear(float db) {
    return forge_powf(10.0f, db / 20.0f);
}

static ForgeEffectBufferFlags compressor_test_buffer_flags(const float *input, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i += 1) {
        if (input[i] != 0.0f) {
            return FORGE_EFFECT_BUFFER_VALID;
        }
    }
    return FORGE_EFFECT_BUFFER_SILENT;
}

static int process_compressor_buffer(const ForgeCompressorParameters *params, uint32_t channels, uint32_t sample_rate,
                                     const float *input, float *output, uint32_t frames) {
    ForgeEffect *effect = NULL;
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeEffectProcessBuffer input_buffer;
    ForgeEffectProcessBuffer output_buffer;
    ForgeResult result;
    int failed = 0;
    uint8_t locked = 0;

    result = forge_create_compressor(&effect, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_compressor failed: %d\n", result);
        return 1;
    }

    if (params != NULL) {
        effect->set_parameters(effect, params, sizeof(*params));
    }

    input_lock.format = &format;
    input_lock.max_frame_count = frames;
    output_lock.format = &format;
    output_lock.max_frame_count = frames;
    result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "compressor lock failed: %d\n", result);
        failed = 1;
    } else {
        locked = 1;
    }

    if (!failed) {
        input_buffer.buffer = (void *)input;
        input_buffer.buffer_flags = compressor_test_buffer_flags(input, frames * channels);
        input_buffer.valid_frame_count = frames;
        output_buffer.buffer = output;
        output_buffer.buffer_flags = FORGE_EFFECT_BUFFER_VALID;
        output_buffer.valid_frame_count = frames;
        effect->process(effect, 1, &input_buffer, 1, &output_buffer, 1);
    }

    if (locked) {
        effect->unlock_for_process(effect);
    }
    forge_effect_destroy(effect);
    return failed;
}

static int create_compressor_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                                    uint32_t sample_rate, const ForgeCompressorParameters *params) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeEffect *compressor = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeResult result;

    result = forge_create_compressor(&compressor, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_compressor failed: %d\n", result);
        return 1;
    }

    desc.effect = compressor;
    desc.initial_state = 1;
    desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    result = forge_audio_create_source_voice(harness->audio, voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                             NULL, NULL, &chain);
    if (result != ForgeResultSuccess) {
        forge_effect_destroy(compressor);
        fprintf(stderr, "forge_audio_create_source_voice compressor failed: %d\n", result);
        return 1;
    }

    if (params != NULL) {
        result = forge_voice_set_effect_parameters(*voice, 0, params, sizeof(*params), FORGE_AUDIO_BATCH_IMMEDIATE);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "forge_voice_set_effect_parameters compressor failed: %d\n", result);
            return 1;
        }
    }

    return 0;
}

int test_compressor_creation_kind_and_destroy(void) {
    ForgeEffect *effect = NULL;
    ForgeResult result = forge_create_compressor(&effect, 0);
    int failed = 0;

    if (result != ForgeResultSuccess || effect == NULL) {
        fprintf(stderr, "compressor create: result=%d effect=%p\n", result, (void *)effect);
        failed = 1;
    }
    if (!failed && effect->kind != ForgeEffectKindCompressor) {
        fprintf(stderr, "compressor kind: got %d\n", effect->kind);
        failed = 1;
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_compressor_parameter_clamping(void) {
    ForgeEffect *effect = NULL;
    ForgeCompressorParameters set_params = compressor_params(-1000.0f, 1000.0f, -1.0f, -10.0f, 0.0f, 100.0f, 2.0f);
    ForgeCompressorParameters got_params;
    int failed = 0;

    failed = check_result("compressor_create", forge_create_compressor(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        effect->set_parameters(effect, &set_params, sizeof(set_params));
        effect->get_parameters(effect, &got_params, sizeof(got_params));
        if (got_params.threshold_db != FORGE_COMPRESSOR_MIN_THRESHOLD_DB ||
            got_params.ratio != FORGE_COMPRESSOR_MAX_RATIO ||
            got_params.knee_db != FORGE_COMPRESSOR_MIN_KNEE_DB ||
            got_params.attack_ms != FORGE_COMPRESSOR_MIN_ATTACK_MS ||
            got_params.release_ms != FORGE_COMPRESSOR_MIN_RELEASE_MS ||
            got_params.makeup_gain_db != FORGE_COMPRESSOR_MAX_MAKEUP_GAIN_DB ||
            got_params.wet_dry_mix != FORGE_COMPRESSOR_MAX_WET_DRY_MIX) {
            fprintf(stderr,
                    "compressor clamped params: threshold=%.3f ratio=%.3f knee=%.3f attack=%.3f release=%.3f "
                    "makeup=%.3f mix=%.3f\n",
                    got_params.threshold_db, got_params.ratio, got_params.knee_db, got_params.attack_ms,
                    got_params.release_ms, got_params.makeup_gain_db, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_compressor_format_validation_rejects_channel_change(void) {
    ForgeEffect *effect = NULL;
    ForgeAudioFormat input_format = audio_test_float_format(1, 48000);
    ForgeAudioFormat output_format = audio_test_float_format(2, 48000);
    ForgeAudioFormat pcm_format = audio_test_float_format(1, 48000);
    ForgeEffectLockBuffer input_lock;
    ForgeEffectLockBuffer output_lock;
    ForgeResult result;
    int failed = 0;

    pcm_format.format_tag = FORGE_AUDIO_FORMAT_PCM;

    failed = check_result("compressor_create", forge_create_compressor(&effect, 0), ForgeResultSuccess);
    if (!failed) {
        input_lock.format = &input_format;
        input_lock.max_frame_count = 64;
        output_lock.format = &output_format;
        output_lock.max_frame_count = 64;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("compressor_channel_change", result, ForgeResultEffectFormatUnsupported);
    }
    if (!failed) {
        output_lock.format = &input_format;
        input_lock.format = &pcm_format;
        result = effect->lock_for_process(effect, 1, &input_lock, 1, &output_lock);
        failed = check_result("compressor_pcm_format", result, ForgeResultEffectFormatUnsupported);
    }

    if (effect != NULL) {
        forge_effect_destroy(effect);
    }
    return failed;
}

int test_compressor_below_threshold_passes_with_makeup_and_mix(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 4
    };
    ForgeCompressorParameters params = compressor_params(-6.0f, 8.0f, 0.0f, 0.0f, 10.0f, 6.0f, 0.5f);
    float source[frames] = {0.1f, 0.1f, 0.1f, 0.1f};
    float output[frames];
    float expected = 0.1f * (0.5f + (0.5f * compressor_test_db_to_linear(6.0f)));
    int failed = process_compressor_buffer(&params, channels, sample_rate, source, output, frames);

    if (!failed) {
        failed = audio_test_check_constant("compressor_below_threshold", output, frames, channels, expected, 0.000001f);
    }
    return failed;
}

int test_compressor_above_threshold_reduces_signal(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 3
    };
    ForgeCompressorParameters params = compressor_params(-12.0f, 4.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    float source[frames] = {1.0f, 1.0f, 1.0f};
    float output[frames];
    int failed = process_compressor_buffer(&params, channels, sample_rate, source, output, frames);

    if (!failed && !(output[0] < 0.5f && output[1] < 0.5f && output[2] < 0.5f)) {
        fprintf(stderr, "compressor above threshold: %.8f %.8f %.8f\n", output[0], output[1], output[2]);
        failed = 1;
    }
    return failed;
}

int test_compressor_ratio_one_is_no_compression(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 4
    };
    ForgeCompressorParameters params = compressor_params(-40.0f, 1.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    float source[frames] = {1.0f, -0.5f, 0.25f, -0.125f};
    float output[frames];
    int failed = process_compressor_buffer(&params, channels, sample_rate, source, output, frames);

    if (!failed) {
        failed = audio_test_check_equal("compressor_ratio_one", output, source, frames, 0.000001f);
    }
    return failed;
}

int test_compressor_higher_ratio_reduces_more(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 1
    };
    ForgeCompressorParameters low_ratio = compressor_params(-20.0f, 2.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    ForgeCompressorParameters high_ratio = compressor_params(-20.0f, 10.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    float source[frames] = {1.0f};
    float low_output[frames];
    float high_output[frames];
    int failed = process_compressor_buffer(&low_ratio, channels, sample_rate, source, low_output, frames);

    if (!failed) {
        failed = process_compressor_buffer(&high_ratio, channels, sample_rate, source, high_output, frames);
    }
    if (!failed && !(high_output[0] < low_output[0])) {
        fprintf(stderr, "compressor higher ratio: low=%.8f high=%.8f\n", low_output[0], high_output[0]);
        failed = 1;
    }
    return failed;
}

int test_compressor_soft_knee_transitions_gradually(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 1
    };
    ForgeCompressorParameters hard = compressor_params(-20.0f, 4.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    ForgeCompressorParameters soft = compressor_params(-20.0f, 4.0f, 20.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    float source[frames] = {compressor_test_db_to_linear(-25.0f)};
    float hard_output[frames];
    float soft_output[frames];
    int failed = process_compressor_buffer(&hard, channels, sample_rate, source, hard_output, frames);

    if (!failed) {
        failed = process_compressor_buffer(&soft, channels, sample_rate, source, soft_output, frames);
    }
    if (!failed && audio_test_absf(hard_output[0] - source[0]) > 0.000001f) {
        fprintf(stderr, "compressor hard knee below threshold: input=%.8f output=%.8f\n", source[0], hard_output[0]);
        failed = 1;
    }
    if (!failed && !(soft_output[0] < hard_output[0] && soft_output[0] > 0.0f)) {
        fprintf(stderr, "compressor soft knee: hard=%.8f soft=%.8f\n", hard_output[0], soft_output[0]);
        failed = 1;
    }
    return failed;
}

int test_compressor_attack_delays_full_gain_reduction(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 8
    };
    ForgeCompressorParameters slow_attack = compressor_params(-24.0f, 8.0f, 0.0f, 20.0f, 100.0f, 0.0f, 1.0f);
    ForgeCompressorParameters fast_attack = compressor_params(-24.0f, 8.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1.0f);
    float source[frames];
    float slow_output[frames];
    float fast_output[frames];
    int failed;

    for (uint32_t i = 0; i < frames; i += 1) {
        source[i] = 1.0f;
    }

    failed = process_compressor_buffer(&slow_attack, channels, sample_rate, source, slow_output, frames);
    if (!failed) {
        failed = process_compressor_buffer(&fast_attack, channels, sample_rate, source, fast_output, frames);
    }
    if (!failed && !(slow_output[0] > fast_output[0] && slow_output[0] > slow_output[frames - 1])) {
        fprintf(stderr, "compressor attack: slow_first=%.8f slow_last=%.8f fast_first=%.8f\n", slow_output[0],
                slow_output[frames - 1], fast_output[0]);
        failed = 1;
    }
    return failed;
}

int test_compressor_release_recovers_gradually(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 5
    };
    ForgeCompressorParameters params = compressor_params(-24.0f, 8.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1.0f);
    float source[frames] = {1.0f, 0.02f, 0.02f, 0.02f, 0.02f};
    float output[frames];
    int failed = process_compressor_buffer(&params, channels, sample_rate, source, output, frames);

    if (!failed && !(output[1] < output[2] && output[2] < output[3] && output[3] < source[3])) {
        fprintf(stderr, "compressor release: %.8f %.8f %.8f %.8f\n", output[0], output[1], output[2], output[3]);
        failed = 1;
    }
    return failed;
}

int test_compressor_linked_channels_reduce_all_channels(void) {
    enum {
        channels = 2,
        sample_rate = 1000,
        frames = 1,
        samples = channels * frames
    };
    ForgeCompressorParameters params = compressor_params(-12.0f, 4.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    float source[samples] = {1.0f, 0.25f};
    float output[samples];
    int failed = process_compressor_buffer(&params, channels, sample_rate, source, output, frames);

    if (!failed) {
        float left_gain = output[0] / source[0];
        float right_gain = output[1] / source[1];

        if (!(output[0] < source[0] && output[1] < source[1] &&
              audio_test_absf(left_gain - right_gain) < 0.000001f)) {
            fprintf(stderr, "compressor linked: L=%.8f R=%.8f gains %.8f %.8f\n", output[0], output[1], left_gain,
                    right_gain);
            failed = 1;
        }
    }
    return failed;
}

int test_compressor_makeup_gain_increases_compressed_output(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 1
    };
    ForgeCompressorParameters no_makeup = compressor_params(-20.0f, 4.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    ForgeCompressorParameters makeup = compressor_params(-20.0f, 4.0f, 0.0f, 0.0f, 10.0f, 6.0f, 1.0f);
    float source[frames] = {1.0f};
    float no_makeup_output[frames];
    float makeup_output[frames];
    int failed = process_compressor_buffer(&no_makeup, channels, sample_rate, source, no_makeup_output, frames);

    if (!failed) {
        failed = process_compressor_buffer(&makeup, channels, sample_rate, source, makeup_output, frames);
    }
    if (!failed && !(makeup_output[0] > no_makeup_output[0])) {
        fprintf(stderr, "compressor makeup: dry=%.8f makeup=%.8f\n", no_makeup_output[0], makeup_output[0]);
        failed = 1;
    }
    return failed;
}

int test_compressor_wet_dry_mix_blends_compressed_and_dry(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 1
    };
    ForgeCompressorParameters wet = compressor_params(-24.0f, 8.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f);
    ForgeCompressorParameters dry = compressor_params(-24.0f, 8.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
    ForgeCompressorParameters blend = compressor_params(-24.0f, 8.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.25f);
    float source[frames] = {1.0f};
    float wet_output[frames];
    float dry_output[frames];
    float blend_output[frames];
    float expected;
    int failed = process_compressor_buffer(&wet, channels, sample_rate, source, wet_output, frames);

    if (!failed) {
        failed = process_compressor_buffer(&dry, channels, sample_rate, source, dry_output, frames);
    }
    if (!failed) {
        failed = process_compressor_buffer(&blend, channels, sample_rate, source, blend_output, frames);
    }
    expected = (dry_output[0] * 0.75f) + (wet_output[0] * 0.25f);
    if (!failed && (audio_test_absf(dry_output[0] - source[0]) > 0.000001f ||
                    audio_test_absf(blend_output[0] - expected) > 0.000001f)) {
        fprintf(stderr, "compressor wet/dry: dry=%.8f wet=%.8f blend=%.8f expected=%.8f\n", dry_output[0],
                wet_output[0], blend_output[0], expected);
        failed = 1;
    }
    return failed;
}

int test_compressor_blob_parameter_set_updates_render_behavior(void) {
    enum {
        channels = 1,
        sample_rate = 1000,
        frames = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeCompressorParameters params = compressor_params(-24.0f, 8.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1.0f);
    ForgeCompressorParameters got_params;
    float source[frames] = {1.0f, 1.0f, 1.0f, 1.0f};
    float output[frames];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, frames);
    if (!failed) {
        failed = create_compressor_source(&harness, &voice, channels, sample_rate, NULL);
    }
    if (!failed) {
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params), FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_effect_parameters(voice, 0, &got_params, sizeof(got_params)) != 0;
    }
    if (!failed && audio_test_absf(got_params.threshold_db - FORGE_COMPRESSOR_DEFAULT_THRESHOLD_DB) > 0.000001f) {
        fprintf(stderr, "compressor blob queued getter: got threshold %.8f\n", got_params.threshold_db);
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
    if (!failed && audio_test_absf(got_params.threshold_db - params.threshold_db) > 0.000001f) {
        fprintf(stderr, "compressor blob applied getter: got threshold %.8f\n", got_params.threshold_db);
        failed = 1;
    }
    if (!failed && !(output[0] < 0.2f)) {
        fprintf(stderr, "compressor blob render: got %.8f\n", output[0]);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
