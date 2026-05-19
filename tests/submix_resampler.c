/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "audio_render_harness.h"
#include "simd_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct SubmixRenderInfo {
    uint32_t input_samples;
    uint32_t output_samples;
} SubmixRenderInfo;

typedef struct SubmixHistoryRunInfo {
    uint32_t input_frames;
    uint32_t output_frames;
    uint32_t max_history_frames;
    uint64_t total_scheduled_input_frames;
    uint64_t consumed_input_frames;
} SubmixHistoryRunInfo;

typedef struct FrameLimitEffect {
    ForgeEffect effect;
    uint32_t locked_max_frames;
    uint32_t max_processed_frames;
} FrameLimitEffect;

static int check_values(const char *name, const float *actual, const float *expected, uint32_t count) {
    for (uint32_t i = 0; i < count; i += 1) {
        if (audio_test_absf(actual[i] - expected[i]) > 0.000001f) {
            fprintf(stderr, "%s[%u]: expected %.8f, got %.8f\n", name, i, expected[i], actual[i]);
            return 1;
        }
    }

    return 0;
}

static void fill_mono_ramp(float *samples, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i += 1) {
        samples[i] = (float)(i * 10);
    }
}

static void fill_channel_ramps(float *samples, uint32_t frames, uint32_t channels) {
    for (uint32_t frame = 0; frame < frames; frame += 1) {
        for (uint32_t channel = 0; channel < channels; channel += 1) {
            samples[frame * channels + channel] = (float)(frame * 10 + channel * 1000);
        }
    }
}

static float cubic_reference_catmull_rom(float p0, float p1, float p2, float p3, double t) {
    double tt = t * t;
    double ttt = tt * t;

    return (float)(0.5 * ((2.0 * p1) + ((-p0 + p2) * t) +
                          (((2.0 * p0) - (5.0 * p1) + (4.0 * p2) - p3) * tt) +
                          ((-p0 + (3.0 * p1) - (3.0 * p2) + p3) * ttt)));
}

static void submix_cubic_reference_render(const float *source, uint32_t source_frames, uint32_t channels,
                                          uint32_t output_frames_per_pass, uint32_t pass_count,
                                          uint64_t resample_step, float *output) {
    float history[SUBMIX_RESAMPLE_HISTORY_FRAMES * 8] = {0};
    uint32_t historyFrames = 0;
    uint64_t historyFrame = 0;
    uint64_t resampleOffset = 0;
    uint64_t inputFrameCursor = 0;
    uint32_t sourceCursor = 0;

    forge_assert(channels <= 8);

    for (uint32_t pass = 0; pass < pass_count; pass += 1) {
        uint64_t passEndOffset = resampleOffset + (uint64_t)output_frames_per_pass * resample_step;
        uint64_t scheduledEndFrame = (passEndOffset + FIXED_FRACTION_MASK) >> FIXED_PRECISION;
        uint32_t scheduledInputFrames =
            scheduledEndFrame > inputFrameCursor ? (uint32_t)(scheduledEndFrame - inputFrameCursor) : 0;
        uint32_t remainingSourceFrames = sourceCursor < source_frames ? source_frames - sourceCursor : 0;
        uint32_t currentInputFrames = forge_min(scheduledInputFrames, remainingSourceFrames);
        uint32_t stagedFrames = historyFrames + scheduledInputFrames + SUBMIX_RESAMPLE_EDGE_FRAMES;
        float stage[(SUBMIX_RESAMPLE_HISTORY_FRAMES + 32 + SUBMIX_RESAMPLE_EDGE_FRAMES) * 8];
        uint64_t baseFrame = historyFrames > 0 ? historyFrame : inputFrameCursor;
        uint64_t localOffset = resampleOffset - (baseFrame << FIXED_PRECISION);

        forge_assert(scheduledInputFrames <= 32);
        forge_zero(stage, sizeof(stage));
        if (historyFrames > 0) {
            forge_memcpy(stage, history, sizeof(float) * historyFrames * channels);
        }
        if (currentInputFrames > 0) {
            forge_memcpy(stage + (historyFrames * channels), source + (sourceCursor * channels),
                         sizeof(float) * currentInputFrames * channels);
        }
        if (currentInputFrames >= scheduledInputFrames && scheduledInputFrames > 0) {
            for (uint32_t edgeFrame = 0; edgeFrame < SUBMIX_RESAMPLE_EDGE_FRAMES; edgeFrame += 1) {
                forge_memcpy(stage + ((historyFrames + scheduledInputFrames + edgeFrame) * channels),
                             stage + ((historyFrames + scheduledInputFrames - 1) * channels),
                             sizeof(float) * channels);
            }
        }

        for (uint32_t outFrame = 0; outFrame < output_frames_per_pass; outFrame += 1) {
            uint64_t p1Frame = localOffset >> FIXED_PRECISION;
            uint64_t p0Frame;
            uint64_t p2Frame;
            uint64_t p3Frame;
            double t = FIXED_TO_DOUBLE(localOffset & FIXED_FRACTION_MASK);

            if (p1Frame >= stagedFrames) {
                p1Frame = stagedFrames - 1;
                t = 0.0;
            }
            p0Frame = p1Frame == 0 ? 0 : p1Frame - 1;
            p2Frame = p1Frame + 1;
            p3Frame = p1Frame + 2;
            if (p2Frame >= stagedFrames) {
                p2Frame = stagedFrames - 1;
            }
            if (p3Frame >= stagedFrames) {
                p3Frame = stagedFrames - 1;
            }

            for (uint32_t channel = 0; channel < channels; channel += 1) {
                output[((pass * output_frames_per_pass + outFrame) * channels) + channel] =
                    cubic_reference_catmull_rom(stage[p0Frame * channels + channel],
                                                stage[p1Frame * channels + channel],
                                                stage[p2Frame * channels + channel],
                                                stage[p3Frame * channels + channel], t);
            }

            localOffset += resample_step;
        }

        resampleOffset = passEndOffset;
        inputFrameCursor += scheduledInputFrames;
        sourceCursor += currentInputFrames;

        {
            uint64_t nextFrame = resampleOffset >> FIXED_PRECISION;
            uint64_t currentEndFrame = inputFrameCursor;
            uint64_t retainedFrames = 0;

            if (scheduledInputFrames != 0 && nextFrame < currentEndFrame) {
                if (nextFrame < baseFrame) {
                    nextFrame = baseFrame;
                }
                retainedFrames = currentEndFrame - nextFrame;
                if (retainedFrames > SUBMIX_RESAMPLE_HISTORY_FRAMES) {
                    nextFrame = currentEndFrame - SUBMIX_RESAMPLE_HISTORY_FRAMES;
                    retainedFrames = SUBMIX_RESAMPLE_HISTORY_FRAMES;
                }
                forge_memcpy(history, stage + ((nextFrame - baseFrame) * channels),
                             sizeof(float) * retainedFrames * channels);
            }

            historyFrame = retainedFrames > 0 ? nextFrame : currentEndFrame;
            historyFrames = (uint32_t)retainedFrames;
        }
    }
}

static void FORGE_AUDIO_CALL frame_limit_effect_destroy(void *effect_ptr) {
    (void)effect_ptr;
}

static void FORGE_AUDIO_CALL frame_limit_effect_get_info(void *effect_ptr, ForgeEffectInfo *effect_info) {
    (void)effect_ptr;

    effect_info->flags = FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED;
    effect_info->min_input_buffer_count = 1;
    effect_info->max_input_buffer_count = 1;
    effect_info->min_output_buffer_count = 1;
    effect_info->max_output_buffer_count = 1;
}

static ForgeResult FORGE_AUDIO_CALL frame_limit_effect_lock(
    void *effect_ptr, uint32_t input_locked_parameter_count, const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t output_locked_parameter_count, const ForgeEffectLockBuffer *output_locked_parameters) {
    FrameLimitEffect *effect = (FrameLimitEffect *)effect_ptr;

    (void)input_locked_parameter_count;
    (void)output_locked_parameter_count;
    (void)output_locked_parameters;

    effect->locked_max_frames = input_locked_parameters->max_frame_count;
    return ForgeResultSuccess;
}

static void FORGE_AUDIO_CALL frame_limit_effect_unlock(void *effect_ptr) {
    (void)effect_ptr;
}

static void FORGE_AUDIO_CALL frame_limit_effect_process(void *effect_ptr, uint32_t input_buffer_count,
                                                        const ForgeEffectProcessBuffer *input_buffers,
                                                        uint32_t output_buffer_count,
                                                        ForgeEffectProcessBuffer *output_buffers,
                                                        int32_t is_enabled) {
    FrameLimitEffect *effect = (FrameLimitEffect *)effect_ptr;
    uint32_t frames = input_buffers->valid_frame_count;

    (void)input_buffer_count;
    (void)output_buffer_count;
    (void)is_enabled;

    if (frames > effect->max_processed_frames) {
        effect->max_processed_frames = frames;
    }
    if (output_buffers->buffer != input_buffers->buffer) {
        forge_memcpy(output_buffers->buffer, input_buffers->buffer, sizeof(float) * frames);
    }
    output_buffers->valid_frame_count = frames;
    output_buffers->buffer_flags = input_buffers->buffer_flags;
}

static uint32_t FORGE_AUDIO_CALL frame_limit_effect_calc_frames(void *effect_ptr, uint32_t frame_count) {
    (void)effect_ptr;

    return frame_count;
}

static void init_frame_limit_effect(FrameLimitEffect *effect) {
    forge_zero(effect, sizeof(*effect));
    effect->effect.destroy = frame_limit_effect_destroy;
    effect->effect.get_info = frame_limit_effect_get_info;
    effect->effect.lock_for_process = frame_limit_effect_lock;
    effect->effect.unlock_for_process = frame_limit_effect_unlock;
    effect->effect.process = frame_limit_effect_process;
    effect->effect.calc_input_frames = frame_limit_effect_calc_frames;
    effect->effect.calc_output_frames = frame_limit_effect_calc_frames;
}

static int render_source_to_submix_quality(uint32_t channels, uint32_t master_rate, uint32_t submix_rate,
                                           uint32_t quantum, const float *source, uint32_t source_frames,
                                           ForgeAudioResamplerQuality quality, float *output, uint32_t output_frames,
                                           SubmixRenderInfo *info) {
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_resampler_quality(submix, quality) != ForgeResultSuccess;
    }
    if (!failed) {
        format = audio_test_float_format(channels, submix_rate);
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_submit_float_buffer(voice, source, source_frames, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, output_frames);
    }

    if (!failed && info != NULL) {
        info->input_samples = submix->mix.inputSamples;
        info->output_samples = submix->mix.outputSamples;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int render_source_to_submix(uint32_t channels, uint32_t master_rate, uint32_t submix_rate, uint32_t quantum,
                                   const float *source, uint32_t source_frames, float *output,
                                   uint32_t output_frames, SubmixRenderInfo *info) {
    return render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                           FORGE_AUDIO_RESAMPLER_LINEAR, output, output_frames, info);
}

static int render_submix_history_long_run(const char *name, uint32_t master_rate, uint32_t submix_rate,
                                          uint32_t quantum, uint32_t pass_count, SubmixHistoryRunInfo *info) {
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    float *source = NULL;
    float *output = NULL;
    uint32_t source_frames;
    int failed = 0;

    failed = audio_render_harness_init(&harness, 1, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, 1, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(1, submix_rate);
        send.flags = 0;
        send.output_voice = submix;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        source_frames = submix->mix.inputFrames * (pass_count + 2);
        source = (float *)malloc(sizeof(float) * source_frames);
        output = (float *)malloc(sizeof(float) * quantum);
        if (source == NULL || output == NULL) {
            fprintf(stderr, "%s: allocation failed\n", name);
            failed = 1;
        }
    }
    if (!failed) {
        fill_mono_ramp(source, source_frames);
        failed = audio_render_harness_submit_float_buffer(voice, source, source_frames, 1);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        info->input_frames = submix->mix.inputFrames;
        info->output_frames = submix->mix.outputSamples;
        info->max_history_frames = 0;
        info->total_scheduled_input_frames = 0;
        info->consumed_input_frames = 0;

        for (uint32_t pass = 0; pass < pass_count; pass += 1) {
            failed = audio_render_harness_render(&harness, output, quantum);
            if (failed) {
                break;
            }

            info->total_scheduled_input_frames += submix->mix.scheduledInputFrames;
            if (submix->mix.resampleHistoryFrames > info->max_history_frames) {
                info->max_history_frames = submix->mix.resampleHistoryFrames;
            }
        }
        info->consumed_input_frames = (submix->mix.resampleOffset + FIXED_FRACTION_MASK) >> FIXED_PRECISION;
    }

    free(source);
    free(output);
    audio_render_harness_destroy(&harness);
    return failed;
}

static int render_submix_schedule_pattern(const char *name, uint32_t master_rate, uint32_t submix_rate,
                                          uint32_t quantum, uint32_t pass_count, uint32_t *scheduled_frames) {
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    float *output = NULL;
    int failed = 0;

    failed = audio_render_harness_init(&harness, 1, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, 1, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        output = (float *)malloc(sizeof(float) * quantum);
        if (output == NULL) {
            fprintf(stderr, "%s: allocation failed\n", name);
            failed = 1;
        }
    }
    if (!failed) {
        for (uint32_t pass = 0; pass < pass_count; pass += 1) {
            failed = audio_render_harness_render(&harness, output, quantum);
            if (failed) {
                break;
            }
            scheduled_frames[pass] = submix->mix.scheduledInputFrames;
        }
    }

    free(output);
    audio_render_harness_destroy(&harness);
    return failed;
}

static int render_empty_submix_quality(uint32_t channels, uint32_t master_rate, uint32_t submix_rate,
                                       uint32_t quantum, ForgeAudioResamplerQuality quality, float *output,
                                       uint32_t output_frames) {
    AudioRenderHarness harness;
    ForgeSubmixVoice *submix = NULL;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_resampler_quality(submix, quality) != ForgeResultSuccess;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, output_frames);
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_resampler_quality_public_api(void) {
    enum {
        channels = 1,
        sample_rate = 48000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSourceVoice *source = NULL;
    ForgeSubmixVoice *submix = NULL;
    ForgeAudioFormat format;
    ForgeAudioResamplerQuality quality = ForgeAudioResamplerLinear;
    ForgeResult result;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, sample_rate, quantum);
    if (!failed) {
        format = audio_test_float_format(channels, sample_rate);
        failed = forge_audio_create_source_voice(harness.audio, &source, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                                 NULL, NULL, NULL) != ForgeResultSuccess;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &submix, channels, sample_rate, 0, 0, NULL, NULL) !=
                 ForgeResultSuccess;
    }

    if (!failed) {
        result = forge_voice_get_resampler_quality(source, &quality);
        if (result != ForgeResultSuccess || quality != ForgeAudioResamplerCubic) {
            fprintf(stderr, "source public default resampler quality: result=%d quality=%d\n", result, quality);
            failed = 1;
        }
    }
    if (!failed) {
        result = forge_voice_get_resampler_quality(submix, &quality);
        if (result != ForgeResultSuccess || quality != ForgeAudioResamplerCubic) {
            fprintf(stderr, "submix public default resampler quality: result=%d quality=%d\n", result, quality);
            failed = 1;
        }
    }
    if (!failed) {
        result = forge_voice_set_resampler_quality(source, ForgeAudioResamplerLinear);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "source public set linear resampler quality: got %d\n", result);
            failed = 1;
        }
    }
    if (!failed) {
        result = forge_voice_set_resampler_quality(submix, ForgeAudioResamplerLinear);
        if (result != ForgeResultSuccess) {
            fprintf(stderr, "submix public set linear resampler quality: got %d\n", result);
            failed = 1;
        }
    }
    if (!failed) {
        result = forge_voice_get_resampler_quality(submix, &quality);
        if (result != ForgeResultSuccess || quality != ForgeAudioResamplerLinear) {
            fprintf(stderr, "submix public get linear resampler quality: result=%d quality=%d\n", result, quality);
            failed = 1;
        }
    }
    if (!failed) {
        result = forge_voice_set_resampler_quality(submix, (ForgeAudioResamplerQuality)99);
        if (result != ForgeResultInvalidArgument) {
            fprintf(stderr, "submix invalid resampler quality: expected %d, got %d\n", ForgeResultInvalidArgument,
                    result);
            failed = 1;
        }
    }
    if (!failed) {
        result = forge_voice_get_resampler_quality(harness.master, &quality);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "master get resampler quality: expected %d, got %d\n", ForgeResultInvalidCall, result);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_submix_stereo_identity_preserves_channel_order(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    static const float source[] = {
        0.0f, 100.0f,
        10.0f, 110.0f,
        20.0f, 120.0f,
        30.0f, 130.0f
    };
    float output[quantum * channels] = {0};
    int failed = 0;

    failed = render_source_to_submix(channels, sample_rate, sample_rate, quantum, source, quantum, output, quantum,
                                     NULL);
    if (!failed) {
        failed = check_values("submix_stereo_identity", output, source, quantum * channels);
    }

    return failed;
}

static int test_submix_mono_cubic_interpolation_matches_reference(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 8,
        source_frames = 4
    };
    static const float source[] = {0.0f, 10.0f, 25.0f, 45.0f};
    float expected[quantum] = {0};
    float output[quantum] = {0};
    int failed = 0;

    submix_cubic_reference_render(source, source_frames, channels, quantum, 1, DOUBLE_TO_FIXED(0.5), expected);
    failed = render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, quantum, NULL);
    if (!failed) {
        failed = check_values("submix_mono_cubic_reference", output, expected, quantum);
    }

    return failed;
}

static int test_submix_stereo_cubic_channel_order_matches_reference(void) {
    enum {
        channels = 2,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 8,
        source_frames = 4
    };
    float source[source_frames * channels];
    float expected[quantum * channels] = {0};
    float output[quantum * channels] = {0};
    int failed = 0;

    fill_channel_ramps(source, source_frames, channels);
    submix_cubic_reference_render(source, source_frames, channels, quantum, 1, DOUBLE_TO_FIXED(0.5), expected);
    failed = render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, quantum, NULL);
    if (!failed) {
        failed = check_values("submix_stereo_cubic_channel_order", output, expected, quantum * channels);
    }

    return failed;
}

static int test_submix_three_channel_cubic_channel_order_matches_reference(void) {
    enum {
        channels = 3,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        source_frames = 3
    };
    float source[source_frames * channels];
    float expected[quantum * channels] = {0};
    float output[quantum * channels] = {0};
    int failed = 0;

    fill_channel_ramps(source, source_frames, channels);
    submix_cubic_reference_render(source, source_frames, channels, quantum, 1, DOUBLE_TO_FIXED(2.0 / 3.0), expected);
    failed = render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, quantum, NULL);
    if (!failed) {
        failed = check_values("submix_three_channel_cubic_channel_order", output, expected, quantum * channels);
    }

    return failed;
}

static int test_submix_cubic_phase_continues_many_passes(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 5,
        source_frames = 15,
        output_frames = quantum * pass_count
    };
    float source[source_frames];
    float expected[output_frames] = {0};
    float output[output_frames] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);
    submix_cubic_reference_render(source, source_frames, channels, quantum, pass_count, DOUBLE_TO_FIXED(2.0 / 3.0),
                                  expected);
    failed = render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_cubic_many_pass_phase", output, expected, output_frames);
    }

    return failed;
}

static int test_submix_cubic_block_edge_holds_last_input_frame(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 8,
        source_frames = 4
    };
    float source[source_frames];
    float expected[quantum] = {0};
    float output[quantum] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);
    submix_cubic_reference_render(source, source_frames, channels, quantum, 1, DOUBLE_TO_FIXED(0.5), expected);
    failed = render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, quantum, NULL);
    if (!failed) {
        failed = check_values("submix_cubic_block_edge_hold", output, expected, quantum);
    }
    if (!failed && output[quantum - 1] == 0.0f) {
        fprintf(stderr, "submix cubic edge hold read zero padding at block edge\n");
        failed = 1;
    }

    return failed;
}

static int test_submix_cubic_true_no_input_is_silence(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        output_frames = quantum * 2
    };
    float output[output_frames] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    int failed = 0;

    failed = render_empty_submix_quality(channels, master_rate, submix_rate, quantum, FORGE_AUDIO_RESAMPLER_CUBIC,
                                         output, output_frames);
    if (!failed) {
        failed = audio_test_check_constant("submix_cubic_no_input_silence", output, output_frames, channels, 0.0f,
                                           0.000001f);
    }

    return failed;
}

static int test_submix_cubic_no_input_drains_history_to_silence(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 2,
        source_frames = 3,
        output_frames = quantum * pass_count
    };
    float source[source_frames];
    float expected[output_frames] = {0};
    float output[output_frames] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);
    submix_cubic_reference_render(source, source_frames, channels, quantum, pass_count, DOUBLE_TO_FIXED(2.0 / 3.0),
                                  expected);
    failed = render_source_to_submix_quality(channels, master_rate, submix_rate, quantum, source, source_frames,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_cubic_no_input_drain", output, expected, output_frames);
    }

    return failed;
}

static int test_submix_cubic_identity_bypass_remains_exact(void) {
    enum {
        channels = 2,
        sample_rate = 48000,
        quantum = 4
    };
    static const float source[] = {
        1.0f, 2.0f,
        16777216.0f, -5.5f,
        0.25f, -0.75f,
        9.0f, -11.0f
    };
    float output[quantum * channels] = {0};
    int failed = 0;

    failed = render_source_to_submix_quality(channels, sample_rate, sample_rate, quantum, source, quantum,
                                             FORGE_AUDIO_RESAMPLER_CUBIC, output, quantum, NULL);
    if (!failed && forge_memcmp(output, source, sizeof(source)) != 0) {
        fprintf(stderr, "submix cubic identity bypass did not preserve exact sample bytes\n");
        failed = 1;
    }

    return failed;
}

static int test_submix_mono_upsample_linear_interpolation(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 24000,
        quantum = 8,
        source_frames = 4
    };
    static const float expected[] = {
        /* The final frame uses an explicit right-edge hold, not zeroed cache padding. */
        0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 30.0f, 30.0f
    };
    float source[source_frames];
    float output[quantum] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     quantum, NULL);
    if (!failed) {
        failed = check_values("submix_mono_upsample", output, expected, quantum);
    }

    return failed;
}

static int test_submix_mono_downsample_linear_interpolation_and_sizing(void) {
    enum {
        channels = 1,
        master_rate = 2000,
        submix_rate = 3000,
        quantum = 4,
        source_frames = 6
    };
    static const float expected[] = {
        0.0f, 15.0f, 30.0f, 45.0f
    };
    float source[source_frames];
    float output[quantum] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     quantum, NULL);
    if (!failed) {
        failed = check_values("submix_mono_downsample", output, expected, quantum);
    }

    return failed;
}

static int test_submix_resample_phase_continues_each_mix_pass(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 2,
        source_frames = 6,
        output_frames = quantum * pass_count
    };
    static const float expected[] = {
        0.0f, 6.6666665f, 13.3333330f, 20.0f,
        26.6666660f, 33.3333320f, 40.0f, 46.6666680f
    };
    float source[source_frames];
    float output[output_frames] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_continuous_phase", output, expected, output_frames);
    }

    return failed;
}

static int test_submix_resample_phase_continues_many_passes(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 5,
        source_frames = 15,
        output_frames = quantum * pass_count
    };
    static const float expected[] = {
        0.0f, 6.6666665f, 13.3333330f, 20.0f,
        26.6666660f, 33.3333320f, 40.0f, 46.6666680f,
        53.3333320f, 60.0f, 66.6666640f, 73.3333360f,
        80.0f, 86.6666640f, 93.3333360f, 100.0f,
        106.6666640f, 113.3333360f, 120.0f, 126.6666640f
    };
    float source[source_frames];
    float output[output_frames] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_many_pass_phase", output, expected, output_frames);
    }

    return failed;
}

static int test_submix_true_no_input_drains_history_to_silence(void) {
    enum {
        channels = 1,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 2,
        source_frames = 3,
        output_frames = quantum * pass_count
    };
    static const float expected[] = {
        0.0f, 6.6666665f, 13.3333330f, 20.0f,
        6.6666665f, 0.0f, 0.0f, 0.0f
    };
    float source[source_frames];
    float output[output_frames] = {0};
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_no_input_drain", output, expected, output_frames);
    }

    return failed;
}

static int test_submix_three_channel_history_preserves_channel_order(void) {
    enum {
        channels = 3,
        master_rate = 3000,
        submix_rate = 2000,
        quantum = 4,
        pass_count = 2,
        source_frames = 6,
        output_frames = quantum * pass_count
    };
    static const float expected_frames[] = {
        0.0f, 6.6666665f, 13.3333330f, 20.0f,
        26.6666660f, 33.3333320f, 40.0f, 46.6666680f
    };
    float source[source_frames * channels];
    float expected[output_frames * channels];
    float output[output_frames * channels] = {0};
    int failed = 0;

    fill_channel_ramps(source, source_frames, channels);
    for (uint32_t frame = 0; frame < output_frames; frame += 1) {
        for (uint32_t channel = 0; channel < channels; channel += 1) {
            expected[frame * channels + channel] = expected_frames[frame] + (float)(channel * 1000);
        }
    }

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     output_frames, NULL);
    if (!failed) {
        failed = check_values("submix_three_channel_history", output, expected, output_frames * channels);
    }

    return failed;
}

static int test_submix_downsample_output_size_stays_within_initialized_input(void) {
    enum {
        channels = 1,
        master_rate = 2000,
        submix_rate = 3000,
        quantum = 4,
        source_frames = 6
    };
    float source[source_frames];
    float output[quantum] = {0};
    SubmixRenderInfo info;
    int failed = 0;

    fill_mono_ramp(source, source_frames);

    failed = render_source_to_submix(channels, master_rate, submix_rate, quantum, source, source_frames, output,
                                     quantum, &info);
    if (!failed && info.output_samples != quantum) {
        fprintf(stderr, "submix outputSamples: expected %u, got %u\n", quantum, info.output_samples);
        failed = 1;
    }
    if (!failed && info.input_samples / channels < source_frames + SUBMIX_RESAMPLE_INPUT_PADDING_FRAMES) {
        fprintf(stderr, "submix inputSamples: expected at least %u frames, got %u\n",
                source_frames + SUBMIX_RESAMPLE_INPUT_PADDING_FRAMES, info.input_samples / channels);
        failed = 1;
    }

    return failed;
}

static int test_submix_retained_history_stays_bounded_long_run(void) {
    enum {
        quantum = 1024,
        pass_count = 1000
    };
    static const struct {
        const char *name;
        uint32_t master_rate;
        uint32_t submix_rate;
        uint32_t expected_max_history_frames;
    } cases[] = {
        {"submix_history_44100_to_48000", 48000, 44100, 1},
        {"submix_history_48000_to_44100", 44100, 48000, 1},
        {"submix_history_3000_to_2000", 3000, 2000, 1}
    };
    int failed = 0;

    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1) {
        SubmixHistoryRunInfo info;
        int case_failed;

        case_failed = render_submix_history_long_run(cases[i].name, cases[i].master_rate, cases[i].submix_rate,
                                                     quantum, pass_count, &info);
        if (!case_failed) {
            printf("%s: input=%u output=%u scheduled=%llu consumed=%llu max_history=%u\n", cases[i].name,
                   info.input_frames, info.output_frames, (unsigned long long)info.total_scheduled_input_frames,
                   (unsigned long long)info.consumed_input_frames, info.max_history_frames);
            if (info.max_history_frames != cases[i].expected_max_history_frames) {
                fprintf(stderr, "%s: expected max_history %u, got %u\n", cases[i].name,
                        cases[i].expected_max_history_frames, info.max_history_frames);
                case_failed = 1;
            }
            if (info.total_scheduled_input_frames != info.consumed_input_frames) {
                fprintf(stderr, "%s: expected scheduled input %llu to match consumed input %llu\n", cases[i].name,
                        (unsigned long long)info.total_scheduled_input_frames,
                        (unsigned long long)info.consumed_input_frames);
                case_failed = 1;
            }
        }
        failed |= case_failed;
    }

    return failed;
}

static int test_submix_scheduled_input_frames_vary_for_fractional_ratios(void) {
    enum {
        pass_count = 10
    };
    static const struct {
        const char *name;
        uint32_t master_rate;
        uint32_t submix_rate;
        uint32_t quantum;
        uint32_t expected[pass_count];
    } cases[] = {
        {"submix_schedule_44100_to_48000", 48000, 44100, 4, {4, 4, 4, 3, 4, 4, 3, 4, 4, 3}},
        {"submix_schedule_48000_to_44100", 44100, 48000, 4, {5, 4, 5, 4, 4, 5, 4, 4, 5, 4}},
        {"submix_schedule_3000_to_2000", 3000, 2000, 4, {3, 3, 3, 2, 3, 3, 2, 3, 3, 2}}
    };
    int failed = 0;

    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i += 1) {
        uint32_t scheduled[pass_count] = {0};
        int case_failed = render_submix_schedule_pattern(cases[i].name, cases[i].master_rate,
                                                         cases[i].submix_rate, cases[i].quantum, pass_count,
                                                         scheduled);

        if (!case_failed) {
            for (uint32_t pass = 0; pass < pass_count; pass += 1) {
                if (scheduled[pass] != cases[i].expected[pass]) {
                    fprintf(stderr, "%s pass %u: expected scheduled input %u, got %u\n", cases[i].name, pass,
                            cases[i].expected[pass], scheduled[pass]);
                    case_failed = 1;
                    break;
                }
            }
        }

        failed |= case_failed;
    }

    return failed;
}

static int test_submix_set_outputs_resizes_input_schedule_capacity(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        routed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4,
        expected_scheduled_frames = 16
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    float source[expected_scheduled_frames * 2];
    float output[quantum] = {0};
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, routed_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 1, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_voice_set_outputs(routed, &send_list) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, routed_rate);
        send.flags = 0;
        send.output_voice = routed;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        fill_mono_ramp(source, expected_scheduled_frames * 2);
        failed = audio_render_harness_submit_float_buffer(voice, source, expected_scheduled_frames * 2, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed && routed->mix.inputFrames < expected_scheduled_frames) {
        fprintf(stderr, "submix reroute inputFrames: expected at least %u, got %u\n", expected_scheduled_frames,
                routed->mix.inputFrames);
        failed = 1;
    }
    if (!failed && routed->mix.scheduledInputFrames != expected_scheduled_frames) {
        fprintf(stderr, "submix reroute scheduledInputFrames: expected %u, got %u\n", expected_scheduled_frames,
                routed->mix.scheduledInputFrames);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_effect_lock_covers_scheduled_submix_input(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        routed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4,
        expected_scheduled_frames = 16
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    FrameLimitEffect effect;
    ForgeEffectDesc effect_desc;
    ForgeEffectChain chain;
    float source[expected_scheduled_frames * 2];
    float output[quantum] = {0};
    int failed = 0;

    init_frame_limit_effect(&effect);
    effect_desc.effect = &effect.effect;
    effect_desc.initial_state = 1;
    effect_desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &effect_desc;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, routed_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 1, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_voice_set_outputs(routed, &send_list) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, routed_rate);
        send.flags = 0;
        send.output_voice = routed;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, &chain) != 0;
    }
    if (!failed) {
        fill_mono_ramp(source, expected_scheduled_frames * 2);
        failed = audio_render_harness_submit_float_buffer(voice, source, expected_scheduled_frames * 2, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed && effect.locked_max_frames < expected_scheduled_frames) {
        fprintf(stderr, "source effect lock max: expected at least %u, got %u\n", expected_scheduled_frames,
                effect.locked_max_frames);
        failed = 1;
    }
    if (!failed && effect.max_processed_frames > effect.locked_max_frames) {
        fprintf(stderr, "source effect processed %u frames with lock max %u\n", effect.max_processed_frames,
                effect.locked_max_frames);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_submix_set_outputs_rejects_growth_past_attached_source_effect_lock(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        routed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    FrameLimitEffect effect;
    ForgeEffectDesc effect_desc;
    ForgeEffectChain chain;
    ForgeResult result;
    uint32_t old_input_frames;
    int failed = 0;

    init_frame_limit_effect(&effect);
    effect_desc.effect = &effect.effect;
    effect_desc.initial_state = 1;
    effect_desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &effect_desc;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, routed_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 1, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, routed_rate);
        send.flags = 0;
        send.output_voice = routed;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, &chain) != 0;
    }
    if (!failed) {
        old_input_frames = routed->mix.inputFrames;
        send.flags = 0;
        send.output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = &send;
        result = forge_voice_set_outputs(routed, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "submix reroute with attached source effect: got %d, expected %d\n", result,
                    ForgeResultInvalidCall);
            failed = 1;
        }
        if (routed->mix.inputFrames != old_input_frames) {
            fprintf(stderr, "submix reroute with attached source effect changed inputFrames from %u to %u\n",
                    old_input_frames, routed->mix.inputFrames);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_effect_attached_after_submix_growth_uses_grown_lock(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        routed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4,
        expected_scheduled_frames = 16
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend send;
    ForgeSendList send_list;
    FrameLimitEffect effect;
    ForgeEffectDesc effect_desc;
    ForgeEffectChain chain;
    float source[expected_scheduled_frames * 2];
    float output[quantum] = {0};
    int failed = 0;

    init_frame_limit_effect(&effect);
    effect_desc.effect = &effect.effect;
    effect_desc.initial_state = 1;
    effect_desc.output_channels = channels;
    chain.effect_count = 1;
    chain.effects = &effect_desc;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, routed_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 1, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, routed_rate);
        send.flags = 0;
        send.output_voice = routed;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        send.flags = 0;
        send.output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = &send;
        failed = forge_voice_set_outputs(routed, &send_list) != 0;
    }
    if (!failed) {
        failed = forge_voice_set_effect_chain(voice, &chain) != ForgeResultSuccess;
    }
    if (!failed) {
        fill_mono_ramp(source, expected_scheduled_frames * 2);
        failed = audio_render_harness_submit_float_buffer(voice, source, expected_scheduled_frames * 2, channels);
    }
    if (!failed) {
        failed = forge_source_voice_start(voice, 0, FORGE_AUDIO_BATCH_IMMEDIATE) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed && effect.locked_max_frames < expected_scheduled_frames) {
        fprintf(stderr, "source effect late lock max: expected at least %u, got %u\n", expected_scheduled_frames,
                effect.locked_max_frames);
        failed = 1;
    }
    if (!failed && effect.max_processed_frames > effect.locked_max_frames) {
        fprintf(stderr, "source effect late processed %u frames with lock max %u\n", effect.max_processed_frames,
                effect.locked_max_frames);
        failed = 1;
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_multi_send_rejects_mismatched_scheduled_frame_counts(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        routed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, routed_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 1, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        sends[0].flags = 0;
        sends[0].output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = sends;
        failed = forge_voice_set_outputs(routed, &send_list) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, routed_rate);
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL) != 0;
    }
    if (!failed) {
        sends[0].flags = 0;
        sends[0].output_voice = harness.master;
        sends[1].flags = 0;
        sends[1].output_voice = routed;
        send_list.send_count = 2;
        send_list.sends = sends;
        result = forge_voice_set_outputs(voice, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "source multi-send master-first: got %d, expected %d\n", result, ForgeResultInvalidCall);
            failed = 1;
        }
    }
    if (!failed) {
        sends[0].output_voice = routed;
        sends[1].output_voice = harness.master;
        result = forge_voice_set_outputs(voice, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "source multi-send submix-first: got %d, expected %d\n", result, ForgeResultInvalidCall);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_submix_growth_rejects_existing_multi_send_frame_mismatch(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        routed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    uint32_t old_input_frames;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, routed_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 1, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, routed_rate);
        sends[0].flags = 0;
        sends[0].output_voice = harness.master;
        sends[1].flags = 0;
        sends[1].output_voice = routed;
        send_list.send_count = 2;
        send_list.sends = sends;
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL) != 0;
    }
    if (!failed) {
        old_input_frames = routed->mix.inputFrames;
        sends[0].flags = 0;
        sends[0].output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = sends;
        result = forge_voice_set_outputs(routed, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "submix growth with existing multi-send source: got %d, expected %d\n", result,
                    ForgeResultInvalidCall);
            failed = 1;
        }
        if (routed->mix.inputFrames != old_input_frames) {
            fprintf(stderr, "submix growth with existing multi-send source changed inputFrames from %u to %u\n",
                    old_input_frames, routed->mix.inputFrames);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_multi_send_rejects_divergent_submix_schedule_phase(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 44100,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *advanced = NULL;
    ForgeSubmixVoice *fresh = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    float output[quantum] = {0};
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &advanced, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    for (uint32_t pass = 0; !failed && pass < 3; pass += 1) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &fresh, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, submix_rate);
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL) != 0;
    }
    if (!failed) {
        sends[0].flags = 0;
        sends[0].output_voice = advanced;
        sends[1].flags = 0;
        sends[1].output_voice = fresh;
        send_list.send_count = 2;
        send_list.sends = sends;
        result = forge_voice_set_outputs(voice, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "source multi-send divergent submix phase: got %d, expected %d\n", result,
                    ForgeResultInvalidCall);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_multi_send_rejects_variable_submix_schedules(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 44100,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *rerouted = NULL;
    ForgeSubmixVoice *peer = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &rerouted, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &peer, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, submix_rate);
        sends[0].flags = 0;
        sends[0].output_voice = rerouted;
        sends[1].flags = 0;
        sends[1].output_voice = peer;
        send_list.send_count = 2;
        send_list.sends = sends;
        result = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list, NULL);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "source multi-send variable submix setup: got %d, expected %d\n", result,
                    ForgeResultInvalidCall);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_source_multi_send_rejects_offset_submix_schedule_phase(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        submix_rate = 44100,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *advanced = NULL;
    ForgeSubmixVoice *fresh = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeAudioFormat format;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    float output[quantum] = {0};
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &advanced, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        failed = audio_render_harness_render(&harness, output, quantum);
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &fresh, channels, submix_rate, 0, 0, NULL, NULL) != 0;
    }
    if (!failed) {
        format = audio_test_float_format(channels, submix_rate);
        failed = forge_audio_create_source_voice(harness.audio, &voice, &format, 0,
                                                 FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL) != 0;
    }
    if (!failed) {
        sends[0].flags = 0;
        sends[0].output_voice = advanced;
        sends[1].flags = 0;
        sends[1].output_voice = fresh;
        send_list.send_count = 2;
        send_list.sends = sends;
        result = forge_voice_set_outputs(voice, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "source multi-send offset submix phase: got %d, expected %d\n", result,
                    ForgeResultInvalidCall);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int test_submix_growth_rejects_existing_upstream_submix_multi_send(void) {
    enum {
        channels = 1,
        master_rate = 48000,
        fixed_rate = 48000,
        downstream_rate = 3000,
        quantum = 4
    };
    AudioRenderHarness harness;
    ForgeSubmixVoice *upstream = NULL;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    uint32_t old_input_frames;
    int failed = 0;

    failed = audio_render_harness_init(&harness, channels, master_rate, quantum);
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &routed, channels, fixed_rate, 0, 1, NULL, NULL) != 0;
    }
    if (!failed) {
        sends[0].flags = 0;
        sends[0].output_voice = harness.master;
        sends[1].flags = 0;
        sends[1].output_voice = routed;
        send_list.send_count = 2;
        send_list.sends = sends;
        failed = forge_audio_create_submix_voice(harness.audio, &upstream, channels, fixed_rate, 0, 0, &send_list,
                                                NULL) != 0;
    }
    if (!failed) {
        failed = forge_audio_create_submix_voice(harness.audio, &downstream, channels, downstream_rate, 0, 2, NULL,
                                                NULL) != 0;
    }
    if (!failed) {
        old_input_frames = routed->mix.inputFrames;
        sends[0].flags = 0;
        sends[0].output_voice = downstream;
        send_list.send_count = 1;
        send_list.sends = sends;
        result = forge_voice_set_outputs(routed, &send_list);
        if (result != ForgeResultInvalidCall) {
            fprintf(stderr, "submix growth with upstream submix multi-send: got %d, expected %d\n", result,
                    ForgeResultInvalidCall);
            failed = 1;
        }
        if (routed->mix.inputFrames != old_input_frames) {
            fprintf(stderr, "submix growth with upstream submix multi-send changed inputFrames from %u to %u\n",
                    old_input_frames, routed->mix.inputFrames);
            failed = 1;
        }
    }

    audio_render_harness_destroy(&harness);
    return failed;
}

static int run_test(const char *name, int (*test_func)(void)) {
    int failed = test_func();

    if (failed) {
        fprintf(stderr, "FAIL %s\n", name);
        return 1;
    }

    printf("PASS %s\n", name);
    return 0;
}

int main(void) {
    int failures = 0;

    failures += run_test("resampler_quality_public_api", test_resampler_quality_public_api);
    failures += run_test("submix_stereo_identity_preserves_channel_order",
                         test_submix_stereo_identity_preserves_channel_order);
    failures += run_test("submix_mono_cubic_interpolation_matches_reference",
                         test_submix_mono_cubic_interpolation_matches_reference);
    failures += run_test("submix_stereo_cubic_channel_order_matches_reference",
                         test_submix_stereo_cubic_channel_order_matches_reference);
    failures += run_test("submix_three_channel_cubic_channel_order_matches_reference",
                         test_submix_three_channel_cubic_channel_order_matches_reference);
    failures += run_test("submix_cubic_phase_continues_many_passes",
                         test_submix_cubic_phase_continues_many_passes);
    failures += run_test("submix_cubic_block_edge_holds_last_input_frame",
                         test_submix_cubic_block_edge_holds_last_input_frame);
    failures += run_test("submix_cubic_true_no_input_is_silence", test_submix_cubic_true_no_input_is_silence);
    failures += run_test("submix_cubic_no_input_drains_history_to_silence",
                         test_submix_cubic_no_input_drains_history_to_silence);
    failures += run_test("submix_cubic_identity_bypass_remains_exact",
                         test_submix_cubic_identity_bypass_remains_exact);
    failures += run_test("submix_mono_upsample_linear_interpolation",
                         test_submix_mono_upsample_linear_interpolation);
    failures += run_test("submix_mono_downsample_linear_interpolation_and_sizing",
                         test_submix_mono_downsample_linear_interpolation_and_sizing);
    failures += run_test("submix_resample_phase_continues_each_mix_pass",
                         test_submix_resample_phase_continues_each_mix_pass);
    failures += run_test("submix_resample_phase_continues_many_passes",
                         test_submix_resample_phase_continues_many_passes);
    failures += run_test("submix_downsample_output_size_stays_within_initialized_input",
                         test_submix_downsample_output_size_stays_within_initialized_input);
    failures += run_test("submix_true_no_input_drains_history_to_silence",
                         test_submix_true_no_input_drains_history_to_silence);
    failures += run_test("submix_three_channel_history_preserves_channel_order",
                         test_submix_three_channel_history_preserves_channel_order);
    failures += run_test("submix_retained_history_stays_bounded_long_run",
                         test_submix_retained_history_stays_bounded_long_run);
    failures += run_test("submix_scheduled_input_frames_vary_for_fractional_ratios",
                         test_submix_scheduled_input_frames_vary_for_fractional_ratios);
    failures += run_test("submix_set_outputs_resizes_input_schedule_capacity",
                         test_submix_set_outputs_resizes_input_schedule_capacity);
    failures += run_test("source_effect_lock_covers_scheduled_submix_input",
                         test_source_effect_lock_covers_scheduled_submix_input);
    failures += run_test("submix_set_outputs_rejects_growth_past_attached_source_effect_lock",
                         test_submix_set_outputs_rejects_growth_past_attached_source_effect_lock);
    failures += run_test("source_effect_attached_after_submix_growth_uses_grown_lock",
                         test_source_effect_attached_after_submix_growth_uses_grown_lock);
    failures += run_test("source_multi_send_rejects_mismatched_scheduled_frame_counts",
                         test_source_multi_send_rejects_mismatched_scheduled_frame_counts);
    failures += run_test("submix_growth_rejects_existing_multi_send_frame_mismatch",
                         test_submix_growth_rejects_existing_multi_send_frame_mismatch);
    failures += run_test("source_multi_send_rejects_divergent_submix_schedule_phase",
                         test_source_multi_send_rejects_divergent_submix_schedule_phase);
    failures += run_test("source_multi_send_rejects_variable_submix_schedules",
                         test_source_multi_send_rejects_variable_submix_schedules);
    failures += run_test("source_multi_send_rejects_offset_submix_schedule_phase",
                         test_source_multi_send_rejects_offset_submix_schedule_phase);
    failures += run_test("submix_growth_rejects_existing_upstream_submix_multi_send",
                         test_submix_growth_rejects_existing_upstream_submix_multi_send);

    return failures == 0 ? 0 : 1;
}
