/*
 * ForgeAudio
 * Forked from FAudio
 *
 * Copyright (c) 2026 ForgeAudio
 *
 * Licensed under the same terms as FAudio:
 */

/* FAudio - XAudio Reimplementation for FNA
 *
 * Copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma, and the MonoGame Team
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

#ifndef FORGE_EFFECT_H
#define FORGE_EFFECT_H

#include "forge_audio.h"

#define FORGE_EFFECT_API FORGE_AUDIO_API
#define FORGE_EFFECT_CALL FORGE_AUDIO_CALL

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Enumerations */

typedef enum ForgeEffectBufferFlags
{
    FORGE_EFFECT_BUFFER_SILENT,
    FORGE_EFFECT_BUFFER_VALID
} ForgeEffectBufferFlags;

/* Structures */

#pragma pack(push, 1)

typedef struct ForgeEffectInfo
{
    uint32_t flags;
    uint32_t min_input_buffer_count;
    uint32_t max_input_buffer_count;
    uint32_t min_output_buffer_count;
    uint32_t max_output_buffer_count;
} ForgeEffectInfo;

typedef struct ForgeEffectLockBuffer
{
    const ForgeAudioFormat *format;
    uint32_t max_frame_count;
} ForgeEffectLockBuffer;

typedef struct ForgeEffectProcessBuffer
{
    void* buffer;
    ForgeEffectBufferFlags buffer_flags;
    uint32_t valid_frame_count;
} ForgeEffectProcessBuffer;

#pragma pack(pop)

/* Constants */

#define FORGE_EFFECT_MIN_CHANNELS 1
#define FORGE_EFFECT_MAX_CHANNELS 64

#define FORGE_EFFECT_MIN_SAMPLE_RATE 1000
#define FORGE_EFFECT_MAX_SAMPLE_RATE 200000

#define FORGE_EFFECT_FLAG_CHANNELS_MUST_MATCH        0x00000001
#define FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH        0x00000002
#define FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH    0x00000004
#define FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH    0x00000008
#define FORGE_EFFECT_FLAG_IN_PLACE_REQUIRED        0x00000020
#define FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED        0x00000010

/* ForgeEffect Interface */

#ifndef FORGE_EFFECT_DECL
#define FORGE_EFFECT_DECL
typedef struct ForgeEffect ForgeEffect;
#endif /* FORGE_EFFECT_DECL */

typedef void (FORGE_EFFECT_CALL * ForgeEffectDestroyFunc)(
    void *effect
);
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectGetInfoFunc)(
    void* effect,
    ForgeEffectInfo **effect_info
);
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectIsInputFormatSupportedFunc)(
    void* effect,
    const ForgeAudioFormat *output_format,
    const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format
);
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectIsOutputFormatSupportedFunc)(
    void* effect,
    const ForgeAudioFormat *input_format,
    const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format
);
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectInitializeFunc)(
    void* effect,
    const void* data,
    uint32_t data_byte_size
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectResetFunc)(
    void* effect
);
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectLockForProcessFunc)(
    void* effect,
    uint32_t input_locked_parameter_count,
    const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t output_locked_parameter_count,
    const ForgeEffectLockBuffer *output_locked_parameters
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectUnlockForProcessFunc)(
    void* effect
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectProcessFunc)(
    void* effect,
    uint32_t input_process_parameter_count,
    const ForgeEffectProcessBuffer* input_process_parameters,
    uint32_t output_process_parameter_count,
    ForgeEffectProcessBuffer* output_process_parameters,
    int32_t is_enabled
);
typedef uint32_t (FORGE_EFFECT_CALL * ForgeEffectCalcInputFramesFunc)(
    void* effect,
    uint32_t output_frame_count
);
typedef uint32_t (FORGE_EFFECT_CALL * ForgeEffectCalcOutputFramesFunc)(
    void* effect,
    uint32_t input_frame_count
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectSetParametersFunc)(
    void* effect,
    const void* parameters,
    uint32_t parameter_byte_size
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectGetParametersFunc)(
    void* effect,
    void* parameters,
    uint32_t parameter_byte_size
);

struct ForgeEffect
{
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
};

FORGE_EFFECT_API void forge_effect_destroy(ForgeEffect *effect);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_EFFECT_H */
