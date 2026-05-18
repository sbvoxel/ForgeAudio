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

    audio_render_harness_destroy(&harness);
    return failed;
}
