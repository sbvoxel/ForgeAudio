/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "engine_render_tests.h"

#include <forge/spatial_audio.h>
#include <stdio.h>

static int create_filter_source(AudioRenderHarness *harness, ForgeSourceVoice **voice, uint32_t channels,
                                uint32_t sample_rate) {
    ForgeAudioFormat format = audio_test_float_format(channels, sample_rate);
    ForgeResult result = forge_audio_create_source_voice(harness->audio, voice, &format,
                                                         FORGE_AUDIO_VOICE_USEFILTER,
                                                         FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);

    if (result != 0) {
        fprintf(stderr, "forge_audio_create_source_voice filter failed: %d\n", result);
        return 1;
    }
    return 0;
}

static int create_submix(AudioRenderHarness *harness, ForgeSubmixVoice **submix, uint32_t channels,
                         uint32_t sample_rate) {
    ForgeResult result = forge_audio_create_submix_voice(harness->audio, submix, channels, sample_rate, 0, 0,
                                                         NULL, NULL);

    if (result != 0) {
        fprintf(stderr, "forge_audio_create_submix_voice failed: %d\n", result);
        return 1;
    }
    return 0;
}

int test_filter_cutoff_range_and_clamped_getter(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    float min_cutoff = -1.0f;
    float max_cutoff = -1.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = forge_voice_get_filter_cutoff_range(voice, &min_cutoff, &max_cutoff) != 0;
    }
    if (!failed && (audio_test_absf(min_cutoff) > 0.000001f ||
                    audio_test_absf(max_cutoff - 8000.0f) > 0.000001f)) {
        fprintf(stderr, "filter cutoff range: expected [0, 8000], got [%.8f, %.8f]\n",
                min_cutoff, max_cutoff);
        failed = 1;
    }
    if (!failed) {
        set_params.type = ForgeFilterHighPass;
        set_params.cutoff_hz = 20000.0f;
        set_params.q = 0.1f;
        set_params.wet_dry_mix = 2.0f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 8000.0f) > 0.000001f ||
            audio_test_absf(got_params.q - FORGE_AUDIO_MIN_FILTER_Q) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 1.0f) > 0.000001f) {
            fprintf(stderr, "filter clamped getter: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_filter_zero_duration_ramp_snaps_after_render(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 400.0f;
        target.q = 4.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &target, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (audio_test_absf(got_params.cutoff_hz) > 0.000001f ||
            audio_test_absf(got_params.q - 1.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix) > 0.000001f) {
            fprintf(stderr, "zero-duration filter visible before render: cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterLowPass ||
            audio_test_absf(got_params.cutoff_hz - 400.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 4.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 1.0f) > 0.000001f) {
            fprintf(stderr, "zero-duration filter after render: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_filter_ramp_getter_reports_current_value(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        ramp_frames = 128,
        buffer_frames = ramp_frames
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.25f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 128.0f;
        target.q = 2.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &target, ramp_frames, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterLowPass ||
            audio_test_absf(got_params.cutoff_hz - 4.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 1.03125f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.2734375f) > 0.000001f) {
            fprintf(stderr, "filter ramp getter: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_filter_type_preserves_ready_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        ramp_frames = 128,
        buffer_frames = ramp_frames
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.25f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 128.0f;
        target.q = 2.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &target, ramp_frames, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_filter_type(voice, ForgeFilterHighPass, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 4.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 1.03125f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.2734375f) > 0.000001f) {
            fprintf(stderr, "voice filter type/ramp: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_filter_field_mask_preserves_other_active_ramps(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        buffer_frames = 16
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget wet_target;
    ForgeFilterTarget cutoff_target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        wet_target.field_mask = FORGE_FILTER_TARGET_WET_DRY_MIX;
        wet_target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &wet_target, 8, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        cutoff_target.field_mask = FORGE_FILTER_TARGET_CUTOFF_HZ;
        cutoff_target.cutoff_hz = 400.0f;
        failed = forge_voice_ramp_filter_frames(voice, &cutoff_target, quantum, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterLowPass ||
            audio_test_absf(got_params.cutoff_hz - 400.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 1.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.5f) > 0.000001f) {
            fprintf(stderr, "filter field mask ramp: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_filter_parameters_does_not_delete_pending_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1181;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters snap_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 400.0f;
        target.q = 5.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        snap_params.type = ForgeFilterHighPass;
        snap_params.cutoff_hz = 100.0f;
        snap_params.q = 2.0f;
        snap_params.wet_dry_mix = 0.25f;
        failed = forge_voice_set_filter_parameters(voice, &snap_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 100.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 2.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.25f) > 0.000001f) {
            fprintf(stderr, "filter pending ramp before apply: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 400.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 5.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 1.0f) > 0.000001f) {
            fprintf(stderr, "filter pending ramp after apply: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_filter_ramp_advances_on_engine_timeline(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        ramp_frames = quantum
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        failed = create_filter_source(&harness, &voice, channels, sample_rate);
    }
    if (!failed) {
        set_params.type = ForgeFilterBandPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 400.0f;
        target.q = 4.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &target, ramp_frames, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (got_params.type != ForgeFilterBandPass ||
            audio_test_absf(got_params.cutoff_hz - 400.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 4.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 1.0f) > 0.000001f) {
            fprintf(stderr, "stopped filter ramp: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_filter_ramp_uses_output_rate(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 4,
        ramp_frames = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = create_submix(&harness, &submix, channels, submix_rate);
    }
    if (!failed) {
        format = audio_test_float_format(channels, master_rate);
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, FORGE_AUDIO_VOICE_USEFILTER,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_filter_parameters(voice, &set_params, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 400.0f;
        target.q = 5.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_filter_frames(voice, &target, ramp_frames, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_filter_parameters(voice, &got_params);
        if (audio_test_absf(got_params.cutoff_hz - 200.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 3.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.5f) > 0.000001f) {
            fprintf(stderr, "stopped diff-rate filter ramp: cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_output_filter_type_preserves_ready_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4,
        ramp_frames = 128,
        buffer_frames = ramp_frames
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float source[buffer_frames];
    float output[quantum];
    int failed = 0;

    for (uint32_t i = 0; i < buffer_frames; i += 1) {
        source[i] = 0.0f;
    }

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(channels, sample_rate);
        send.flags = FORGE_AUDIO_SEND_USEFILTER;
        send.output_voice = harness.master;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, buffer_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.25f;
        failed = forge_voice_set_output_filter_parameters(voice, harness.master, &set_params,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 128.0f;
        target.q = 2.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_output_filter_frames(voice, harness.master, &target, ramp_frames,
                                                       FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_output_filter_type(voice, harness.master, ForgeFilterHighPass,
                                                    FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_output_filter_parameters(voice, harness.master, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 4.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 1.03125f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.2734375f) > 0.000001f) {
            fprintf(stderr, "output filter type/ramp: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_set_output_filter_parameters_does_not_delete_pending_deferred_ramp(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    const ForgeAudioBatchId batch_id = 1182;
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeFilterParameters set_params;
    ForgeFilterParameters snap_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(channels, sample_rate);
        send.flags = FORGE_AUDIO_SEND_USEFILTER;
        send.output_voice = harness.master;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_output_filter_parameters(voice, harness.master, &set_params,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 400.0f;
        target.q = 5.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_output_filter_frames(voice, harness.master, &target, quantum, batch_id) != 0;
    }
    if (!failed) {
        snap_params.type = ForgeFilterHighPass;
        snap_params.cutoff_hz = 100.0f;
        snap_params.q = 2.0f;
        snap_params.wet_dry_mix = 0.25f;
        failed = forge_voice_set_output_filter_parameters(voice, harness.master, &snap_params,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_voice_get_output_filter_parameters(voice, harness.master, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 100.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 2.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.25f) > 0.000001f) {
            fprintf(stderr, "output filter pending ramp before apply: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }
    if (!failed) {
        failed = forge_audio_apply_batch(harness.audio, batch_id) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_output_filter_parameters(voice, harness.master, &got_params);
        if (got_params.type != ForgeFilterHighPass ||
            audio_test_absf(got_params.cutoff_hz - 400.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 5.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 1.0f) > 0.000001f) {
            fprintf(stderr, "output filter pending ramp after apply: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.type, got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_stopped_source_output_filter_ramp_uses_output_rate(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 4,
        ramp_frames = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeFilterParameters set_params;
    ForgeFilterParameters got_params;
    ForgeFilterTarget target;
    float output[quantum];
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = create_submix(&harness, &submix, channels, submix_rate);
    }
    if (!failed) {
        format = audio_test_float_format(channels, master_rate);
        send.flags = FORGE_AUDIO_SEND_USEFILTER;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        set_params.type = ForgeFilterLowPass;
        set_params.cutoff_hz = 0.0f;
        set_params.q = 1.0f;
        set_params.wet_dry_mix = 0.0f;
        failed = forge_voice_set_output_filter_parameters(voice, submix, &set_params,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        target.field_mask = FORGE_FILTER_TARGET_ALL;
        target.cutoff_hz = 400.0f;
        target.q = 5.0f;
        target.wet_dry_mix = 1.0f;
        failed = forge_voice_ramp_output_filter_frames(voice, submix, &target, ramp_frames,
                                                       FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_output_filter_parameters(voice, submix, &got_params);
        if (audio_test_absf(got_params.cutoff_hz - 200.0f) > 0.000001f ||
            audio_test_absf(got_params.q - 3.0f) > 0.000001f ||
            audio_test_absf(got_params.wet_dry_mix - 0.5f) > 0.000001f) {
            fprintf(stderr, "stopped diff-rate output filter ramp: cutoff=%.8f q=%.8f wet=%.8f\n",
                    got_params.cutoff_hz, got_params.q, got_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_filter_invalid_type_rejected(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeFilterParameters params;
    ForgeFilterTarget target;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(channels, sample_rate);
        send.flags = FORGE_AUDIO_SEND_USEFILTER;
        send.output_voice = harness.master;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, FORGE_AUDIO_VOICE_USEFILTER,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        params.type = (ForgeFilterType)99;
        params.cutoff_hz = 100.0f;
        params.q = 1.0f;
        params.wet_dry_mix = 1.0f;
        failed = check_result("invalid_voice_filter_parameters",
                              forge_voice_set_filter_parameters(voice, &params, FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        failed = check_result("invalid_voice_filter_type",
                              forge_voice_set_filter_type(voice, (ForgeFilterType)99,
                                                          FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        failed = check_result("invalid_output_filter_parameters",
                              forge_voice_set_output_filter_parameters(voice, harness.master, &params,
                                                                       FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        failed = check_result("invalid_output_filter_type",
                              forge_voice_set_output_filter_type(voice, harness.master, (ForgeFilterType)99,
                                                                 FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        target.field_mask = 0;
        failed = check_result("invalid_filter_target_empty_mask",
                              forge_voice_ramp_filter_frames(voice, &target, 4, FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        target.field_mask = 0x80000000u;
        failed = check_result("invalid_output_filter_target_unknown_mask",
                              forge_voice_ramp_output_filter_frames(voice, harness.master, &target, 4,
                                                                    FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_native_spatial_direct_apply_updates_matrix_rate_and_send_filter(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeNativeSpatialDspSettings dsp;
    ForgeFilterParameters filter_params;
    float matrix[destination_channels * source_channels] = {0.25f, 0.75f};
    float got_matrix[destination_channels * source_channels];
    float output[quantum * destination_channels];
    float rate = 0.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(source_channels, sample_rate);
        send.flags = FORGE_AUDIO_SEND_USEFILTER;
        send.output_voice = harness.master;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 2.0f, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        forge_zero(&dsp, sizeof(dsp));
        dsp.matrix_coefficients = matrix;
        dsp.src_channel_count = source_channels;
        dsp.dst_channel_count = destination_channels;
        dsp.doppler_rate_scalar = 1.5f;
        dsp.direct_lowpass_cutoff_hz = 400.0f;
        failed = forge_source_voice_apply_native_spatial_direct(voice, harness.master, &dsp, 0,
                                                                FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_output_matrix(voice, harness.master, source_channels, destination_channels, got_matrix);
        failed = audio_test_check_equal("native_spatial_direct_matrix", got_matrix, matrix,
                                        destination_channels * source_channels, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_rate(voice, &rate);
        if (audio_test_absf(rate - 1.5f) > 0.000001f) {
            fprintf(stderr, "native spatial direct rate: got %.8f\n", rate);
            failed = 1;
        }
    }
    if (!failed) {
        forge_voice_get_output_filter_parameters(voice, harness.master, &filter_params);
        if (filter_params.type != ForgeFilterLowPass ||
            audio_test_absf(filter_params.cutoff_hz - 400.0f) > 0.000001f ||
            audio_test_absf(filter_params.q - 1.0f) > 0.000001f ||
            audio_test_absf(filter_params.wet_dry_mix - 1.0f) > 0.000001f) {
            fprintf(stderr, "native spatial direct filter: type=%d cutoff=%.8f q=%.8f wet=%.8f\n",
                    filter_params.type, filter_params.cutoff_hz, filter_params.q, filter_params.wet_dry_mix);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_native_spatial_direct_apply_preflight_is_all_or_nothing(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeNativeSpatialDspSettings dsp;
    float start_matrix[destination_channels * source_channels] = {0.3f, 0.4f};
    float target_matrix[destination_channels * source_channels] = {0.1f, 0.9f};
    float got_matrix[destination_channels * source_channels];
    float output[quantum * destination_channels];
    float rate = 0.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(source_channels, sample_rate);
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 2.0f, NULL, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_zero(&dsp, sizeof(dsp));
        dsp.matrix_coefficients = target_matrix;
        dsp.src_channel_count = source_channels;
        dsp.dst_channel_count = destination_channels;
        dsp.doppler_rate_scalar = 1.5f;
        dsp.direct_lowpass_cutoff_hz = 400.0f;
        failed = check_result("native_spatial_direct_missing_send_filter",
                              forge_source_voice_apply_native_spatial_direct(voice, harness.master, &dsp, 0,
                                                                             FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_output_matrix(voice, harness.master, source_channels, destination_channels, got_matrix);
        failed = audio_test_check_equal("native_spatial_direct_failed_matrix_unchanged", got_matrix, start_matrix,
                                        destination_channels * source_channels, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_rate(voice, &rate);
        if (audio_test_absf(rate - 1.0f) > 0.000001f) {
            fprintf(stderr, "native spatial failed preflight changed rate: got %.8f\n", rate);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_native_spatial_direct_apply_rejects_invalid_preflight_arguments(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    ForgeNativeSpatialDspSettings dsp;
    float start_matrix[destination_channels * source_channels] = {0.3f, 0.4f};
    float target_matrix[destination_channels * source_channels] = {0.1f, 0.9f};
    float got_matrix[destination_channels * source_channels];
    float rate = 0.0f;
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(source_channels, sample_rate);
        send.flags = FORGE_AUDIO_SEND_USEFILTER;
        send.output_voice = harness.master;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 2.0f, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        forge_zero(&dsp, sizeof(dsp));
        dsp.matrix_coefficients = target_matrix;
        dsp.src_channel_count = source_channels;
        dsp.dst_channel_count = destination_channels + 1;
        dsp.doppler_rate_scalar = 1.5f;
        dsp.direct_lowpass_cutoff_hz = 400.0f;
        failed = check_result("native_spatial_direct_bad_dst_channels",
                              forge_source_voice_apply_native_spatial_direct(voice, harness.master, &dsp, 0,
                                                                             FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidCall);
    }
    if (!failed) {
        dsp.dst_channel_count = destination_channels;
        dsp.doppler_rate_scalar = 0.0f;
        failed = check_result("native_spatial_direct_bad_rate",
                              forge_source_voice_apply_native_spatial_direct(voice, harness.master, &dsp, 0,
                                                                             FORGE_AUDIO_BATCH_IMMEDIATE),
                              ForgeResultInvalidArgument);
    }
    if (!failed) {
        forge_voice_get_output_matrix(voice, harness.master, source_channels, destination_channels, got_matrix);
        failed = audio_test_check_equal("native_spatial_direct_invalid_args_matrix_unchanged", got_matrix,
                                        start_matrix, destination_channels * source_channels, 0.000001f);
    }
    if (!failed) {
        forge_source_voice_get_rate(voice, &rate);
        if (audio_test_absf(rate - 1.0f) > 0.000001f) {
            fprintf(stderr, "native spatial invalid preflight changed rate: got %.8f\n", rate);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

int test_spatial_send_gain_scales_current_matrix(void) {
    enum {
        source_channels = 1,
        destination_channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *voice = NULL;
    float start_matrix[destination_channels * source_channels] = {0.2f, 0.6f};
    float expected[destination_channels * source_channels];
    float got_matrix[destination_channels * source_channels];
    float output[quantum * destination_channels];
    int failed = 0;

    failed = audio_render_harness_init(&harness, destination_channels, sample_rate, quantum);
    if (!failed) {
        failed = audio_render_harness_create_float_source(&harness, &voice, source_channels, sample_rate);
    }
    if (!failed) {
        failed = forge_voice_set_output_matrix(voice, harness.master, source_channels, destination_channels,
                                               start_matrix, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        for (uint32_t i = 0; i < destination_channels * source_channels; i += 1) {
            expected[i] = start_matrix[i] * 0.25f;
        }
        failed = forge_voice_ramp_spatial_send_gain(voice, harness.master, 0.25f, 0,
                                                    FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        forge_voice_get_output_matrix(voice, harness.master, source_channels, destination_channels, got_matrix);
        failed = audio_test_check_equal("spatial_send_gain_matrix", got_matrix, expected,
                                        destination_channels * source_channels, 0.000001f);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}
