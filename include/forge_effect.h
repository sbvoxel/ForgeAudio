/* ForgeAudio
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

typedef struct ForgeEffectProperties
{
    ForgeGuid clsid;
    int16_t FriendlyName[256]; /* Win32 wchar_t */
    int16_t CopyrightInfo[256]; /* Win32 wchar_t */
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t Flags;
    uint32_t MinInputBufferCount;
    uint32_t MaxInputBufferCount;
    uint32_t MinOutputBufferCount;
    uint32_t MaxOutputBufferCount;
} ForgeEffectProperties;

typedef struct ForgeEffectLockBuffer
{
    const ForgeAudioFormat *format;
    uint32_t MaxFrameCount;
} ForgeEffectLockBuffer;

typedef struct ForgeEffectProcessBuffer
{
    void* buffer;
    ForgeEffectBufferFlags BufferFlags;
    uint32_t ValidFrameCount;
} ForgeEffectProcessBuffer;

#pragma pack(pop)

/* Constants */

#define FORGE_EFFECT_MIN_CHANNELS 1
#define FORGE_EFFECT_MAX_CHANNELS 64

#define FORGE_EFFECT_MIN_SAMPLE_RATE 1000
#define FORGE_EFFECT_MAX_SAMPLE_RATE 200000

#define FORGE_EFFECT_PROPERTIES_STRING_LENGTH 256

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
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectGetPropertiesFunc)(
    void* effect,
    ForgeEffectProperties **registration_properties
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
    uint32_t DataByteSize
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectResetFunc)(
    void* effect
);
typedef ForgeResult (FORGE_EFFECT_CALL * ForgeEffectLockForProcessFunc)(
    void* effect,
    uint32_t InputLockedParameterCount,
    const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t OutputLockedParameterCount,
    const ForgeEffectLockBuffer *output_locked_parameters
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectUnlockForProcessFunc)(
    void* effect
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectProcessFunc)(
    void* effect,
    uint32_t InputProcessParameterCount,
    const ForgeEffectProcessBuffer* input_process_parameters,
    uint32_t OutputProcessParameterCount,
    ForgeEffectProcessBuffer* output_process_parameters,
    int32_t IsEnabled
);
typedef uint32_t (FORGE_EFFECT_CALL * ForgeEffectCalcInputFramesFunc)(
    void* effect,
    uint32_t OutputFrameCount
);
typedef uint32_t (FORGE_EFFECT_CALL * ForgeEffectCalcOutputFramesFunc)(
    void* effect,
    uint32_t InputFrameCount
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectSetParametersFunc)(
    void* effect,
    const void* parameters,
    uint32_t ParameterByteSize
);
typedef void (FORGE_EFFECT_CALL * ForgeEffectGetParametersFunc)(
    void* effect,
    void* parameters,
    uint32_t ParameterByteSize
);

struct ForgeEffect
{
    ForgeEffectDestroyFunc Destroy;
    ForgeEffectGetPropertiesFunc GetRegistrationProperties;
    ForgeEffectIsInputFormatSupportedFunc IsInputFormatSupported;
    ForgeEffectIsOutputFormatSupportedFunc IsOutputFormatSupported;
    ForgeEffectInitializeFunc Initialize;
    ForgeEffectResetFunc Reset;
    ForgeEffectLockForProcessFunc LockForProcess;
    ForgeEffectUnlockForProcessFunc UnlockForProcess;
    ForgeEffectProcessFunc Process;
    ForgeEffectCalcInputFramesFunc CalcInputFrames;
    ForgeEffectCalcOutputFramesFunc CalcOutputFrames;
    ForgeEffectSetParametersFunc SetParameters;
    ForgeEffectGetParametersFunc GetParameters;
};

FORGE_EFFECT_API void forge_effect_destroy(ForgeEffect *effect);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_EFFECT_H */
