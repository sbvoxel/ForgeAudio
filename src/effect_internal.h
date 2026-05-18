/*
 * ForgeAudio
 * Forked from FAudio.
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 * Portions copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma,
 * and the MonoGame Team.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#ifndef FORGE_EFFECT_INTERNAL_H
#define FORGE_EFFECT_INTERNAL_H

#include <forge/effects.h>

#define FORGE_EFFECT_CALL FORGE_AUDIO_CALL

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum ForgeEffectBufferFlags {
    FORGE_EFFECT_BUFFER_SILENT,
    FORGE_EFFECT_BUFFER_VALID
} ForgeEffectBufferFlags;

#pragma pack(push, 1)

typedef struct ForgeEffectInfo {
    uint32_t flags;
    uint32_t min_input_buffer_count;
    uint32_t max_input_buffer_count;
    uint32_t min_output_buffer_count;
    uint32_t max_output_buffer_count;
} ForgeEffectInfo;

typedef struct ForgeEffectLockBuffer {
    const ForgeAudioFormat *format;
    uint32_t max_frame_count;
} ForgeEffectLockBuffer;

typedef struct ForgeEffectProcessBuffer {
    void *buffer;
    ForgeEffectBufferFlags buffer_flags;
    uint32_t valid_frame_count;
} ForgeEffectProcessBuffer;

#pragma pack(pop)

#define FORGE_EFFECT_MIN_CHANNELS 1
#define FORGE_EFFECT_MAX_CHANNELS 64

#define FORGE_EFFECT_MIN_SAMPLE_RATE 1000
#define FORGE_EFFECT_MAX_SAMPLE_RATE 200000

#define FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH 0x00000001
#define FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH 0x00000002
#define FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH 0x00000004
#define FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH 0x00000008
#define FORGE_EFFECT_FLAG_IN_PLACE_REQUIRED 0x00000020
#define FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED 0x00000010

typedef void(FORGE_EFFECT_CALL *ForgeEffectDestroyFunc)(void *effect);
typedef void(FORGE_EFFECT_CALL *ForgeEffectGetInfoFunc)(void *effect, ForgeEffectInfo *effect_info);
typedef ForgeResult(FORGE_EFFECT_CALL *ForgeEffectIsInputFormatSupportedFunc)(
    void *effect, const ForgeAudioFormat *output_format, const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format);
typedef ForgeResult(FORGE_EFFECT_CALL *ForgeEffectIsOutputFormatSupportedFunc)(
    void *effect, const ForgeAudioFormat *input_format, const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format);
typedef ForgeResult(FORGE_EFFECT_CALL *ForgeEffectInitializeFunc)(void *effect, const void *data,
                                                                  uint32_t data_byte_size);
typedef void(FORGE_EFFECT_CALL *ForgeEffectResetFunc)(void *effect);
typedef ForgeResult(FORGE_EFFECT_CALL *ForgeEffectLockForProcessFunc)(
    void *effect, uint32_t input_locked_parameter_count, const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t output_locked_parameter_count, const ForgeEffectLockBuffer *output_locked_parameters);
typedef void(FORGE_EFFECT_CALL *ForgeEffectUnlockForProcessFunc)(void *effect);
typedef void(FORGE_EFFECT_CALL *ForgeEffectProcessFunc)(void *effect, uint32_t input_buffer_count,
                                                        const ForgeEffectProcessBuffer *input_buffers,
                                                        uint32_t output_buffer_count,
                                                        ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled);
typedef uint32_t(FORGE_EFFECT_CALL *ForgeEffectCalcInputFramesFunc)(void *effect, uint32_t output_frame_count);
typedef uint32_t(FORGE_EFFECT_CALL *ForgeEffectCalcOutputFramesFunc)(void *effect, uint32_t input_frame_count);
typedef void(FORGE_EFFECT_CALL *ForgeEffectSetParametersFunc)(void *effect, const void *parameters,
                                                              uint32_t parameter_byte_size);
typedef void(FORGE_EFFECT_CALL *ForgeEffectGetParametersFunc)(void *effect, void *parameters,
                                                              uint32_t parameter_byte_size);
typedef ForgeResult(FORGE_EFFECT_CALL *ForgeEffectSetReverbTargetFunc)(void *effect, const ForgeReverbTarget *target,
                                                                       uint32_t duration_frames);
typedef void(FORGE_EFFECT_CALL *ForgeEffectAdvanceAutomationFunc)(void *effect, uint32_t frame_count);

typedef enum ForgeEffectKind {
    ForgeEffectKindUnknown,
    ForgeEffectKindReverb,
    ForgeEffectKindReverb7Point1,
    ForgeEffectKindVolumeMeter,
    ForgeEffectKindLimiter,
    ForgeEffectKindDelay
} ForgeEffectKind;

struct ForgeEffect {
    ForgeEffectDestroyFunc destroy;
    ForgeEffectGetInfoFunc get_info;
    ForgeEffectIsInputFormatSupportedFunc is_input_format_supported;
    ForgeEffectIsOutputFormatSupportedFunc is_output_format_supported;
    ForgeEffectInitializeFunc initialize;
    ForgeEffectResetFunc reset;
    ForgeEffectLockForProcessFunc lock_for_process;
    ForgeEffectUnlockForProcessFunc unlock_for_process;
    ForgeEffectProcessFunc process;
    ForgeEffectCalcInputFramesFunc calc_input_frames;
    ForgeEffectCalcOutputFramesFunc calc_output_frames;
    ForgeEffectSetParametersFunc set_parameters;
    ForgeEffectGetParametersFunc get_parameters;
    ForgeEffectKind kind;
    ForgeEffectSetReverbTargetFunc set_reverb_target;
    ForgeEffectAdvanceAutomationFunc advance_automation;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_EFFECT_INTERNAL_H */
