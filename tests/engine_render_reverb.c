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

static int create_reverb_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t source_channels,
                                uint32_t sample_rate, uint32_t output_channels, int use_7point1) {
    ForgeAudioFormat format = audio_test_float_format(source_channels, sample_rate);
    ForgeEffect *reverb = NULL;
    ForgeEffectDesc desc;
    ForgeEffectChain chain;
    ForgeResult result;

    result = use_7point1 ? forge_create_reverb_7point1(&reverb, 0) : forge_create_reverb(&reverb, 0);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "forge_create_reverb failed: %d\n", result);
        return 1;
    }

    desc.effect = reverb;
    desc.initial_state = 1;
    desc.output_channels = output_channels;
    chain.effect_count = 1;
    chain.effects = &desc;

    result = forge_audio_create_source_voice(harness->audio, voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                             NULL, NULL, &chain);
    if (result != ForgeResultSuccess) {
        forge_effect_destroy(reverb);
        fprintf(stderr, "forge_audio_create_source_voice reverb failed: %d\n", result);
        return 1;
    }

    return 0;
}

static ForgeReverbTarget reverb_target(uint32_t field_mask, float wet_dry_mix, float reflections_gain,
                                       float reverb_gain, float room_filter_main, float room_filter_hf) {
    ForgeReverbTarget target;

    forge_zero(&target, sizeof(target));
    target.field_mask = field_mask;
    target.wet_dry_mix = wet_dry_mix;
    target.reflections_gain = reflections_gain;
    target.reverb_gain = reverb_gain;
    target.room_filter_main = room_filter_main;
    target.room_filter_hf = room_filter_hf;
    return target;
}

int test_reverb_ramp_getter_reports_current_value(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        ramp_frames = 8,
        buffer_frames = 16
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, ramp_frames,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 50.0f) > 0.000001f) {
        fprintf(stderr, "reverb ramp getter: expected wet 50, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_wet_dry_ramp_renders_dry_path(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = 8
    };
    static const float expected[quantum] = {0.0f, 0.25f, 0.5f, 0.75f};
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbTarget target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 1.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = audio_test_check_equal("reverb_wet_dry_ramp_dry_path", output, expected, quantum, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_field_mask_preserves_other_active_ramps(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters got;
    ForgeReverbTarget wet_target;
    ForgeReverbTarget gain_target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        wet_target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &wet_target, 8,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        gain_target = reverb_target(FORGE_REVERB_TARGET_REVERB_GAIN, 0.0f, 0.0f, -8.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &gain_target, quantum,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && (audio_test_absf(got.wet_dry_mix - 50.0f) > 0.000001f ||
                    audio_test_absf(got.reverb_gain + 8.0f) > 0.000001f)) {
        fprintf(stderr, "reverb field mask: wet=%.8f reverb_gain=%.8f\n", got.wet_dry_mix, got.reverb_gain);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_reverb_ramp_advances_on_engine_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "stopped reverb ramp: expected wet 0, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_disabled_reverb_ramp_advances_on_engine_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = 8
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
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
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "disabled reverb ramp: expected wet 0, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_batch_blob_then_ramp_order(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1201;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters params;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &params) != 0;
    }
    if (!failed) {
        params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params), batch_id) != 0;
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 100.0f) > 0.000001f) {
        fprintf(stderr, "reverb blob then ramp order: expected wet 100, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_batch_ramp_then_blob_order(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1202;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters params;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &params) != 0;
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        params.wet_dry_mix = 75.0f;
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params), batch_id) != 0;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 75.0f) > 0.000001f) {
        fprintf(stderr, "reverb ramp then blob order: expected wet 75, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_blob_set_does_not_delete_pending_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1203;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters params;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &params) != 0;
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        params.wet_dry_mix = 75.0f;
        failed = forge_voice_set_effect_parameters(voice, 0, &params, sizeof(params),
                                                   FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 75.0f) > 0.000001f) {
        fprintf(stderr, "reverb pending ramp before apply: expected wet 75, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "reverb pending ramp after apply: expected wet 0, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_zero_duration_ramp_snaps_after_render(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters got;
    ForgeReverbTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, 0,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 100.0f) > 0.000001f) {
        fprintf(stderr, "reverb zero-duration visible before render: %.8f\n", got.wet_dry_mix);
        failed = 1;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_parameters(voice, 0, &got) != 0;
    }
    if (!failed && audio_test_absf(got.wet_dry_mix - 25.0f) > 0.000001f) {
        fprintf(stderr, "reverb zero-duration after render: expected wet 25, got %.8f\n", got.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_invalid_arguments(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeSourceVoice *meter_voice = NULL;
    ForgeEffect *meter = NULL;
    ForgeEffectDesc meter_desc;
    ForgeEffectChain meter_chain;
    ForgeAudioFormat format;
    ForgeReverbTarget target;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, channels, sample_rate, channels, 0);
    }
    if (!failed) {
        failed = forge_create_volume_meter(&meter, 0) != 0;
    }
    if (!failed) {
        meter_desc.effect = meter;
        meter_desc.initial_state = 1;
        meter_desc.output_channels = channels;
        meter_chain.effect_count = 1;
        meter_chain.effects = &meter_desc;
        format = audio_test_float_format(channels, sample_rate);
        failed = forge_audio_create_source_voice(harness.audio, &meter_voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, &meter_chain) != 0;
        if (failed) {
            forge_effect_destroy(meter);
        }
    }
    if (!failed) {
        target = reverb_target(0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = check_result("reverb_empty_mask",
                              forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                                        FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        target = reverb_target(0x80000000u, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = check_result("reverb_unknown_mask",
                              forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                                        FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = check_result("reverb_batch_all",
                              forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                                        FORGE_AUDIO_BATCH_ALL),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("reverb_wrong_effect_kind",
                              forge_voice_ramp_reverb_parameters_frames(meter_voice, 0, &target, quantum,
                                                                        FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = check_result("reverb_get_null",
                              forge_voice_get_reverb_parameters(voice, 0, NULL),
                              ForgeResultInvalidCall);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_reverb_7point1_target_and_getter(void) {
    enum {
        source_channels = 1,
        output_channels = 6,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeReverbParameters normal_params;
    ForgeReverbParameters7Point1 params;
    ForgeReverbTarget target;
    float output[quantum * output_channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, output_channels, sample_rate, quantum);
    if (!failed) {
        failed = create_reverb_source(&harness, &voice, source_channels, sample_rate, output_channels, 1);
    }
    if (!failed) {
        failed = check_result("reverb_7point1_wrong_getter",
                              forge_voice_get_reverb_parameters(voice, 0, &normal_params),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        target = reverb_target(FORGE_REVERB_TARGET_WET_DRY_MIX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        failed = forge_voice_ramp_reverb_parameters_frames(voice, 0, &target, quantum,
                                                           FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_voice_get_reverb_7point1_parameters(voice, 0, &params) != 0;
    }
    if (!failed && audio_test_absf(params.wet_dry_mix) > 0.000001f) {
        fprintf(stderr, "reverb 7.1 target/getter: expected wet 0, got %.8f\n", params.wet_dry_mix);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
