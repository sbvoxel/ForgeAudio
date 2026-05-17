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

#ifndef FORGE_APO_H
#define FORGE_APO_H

#include "forge_audio.h"

#define FORGE_APO_API FORGE_AUDIO_API
#define FORGE_APO_CALL FORGE_AUDIO_CALL

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Enumerations */

typedef enum ForgeApoBufferFlags
{
    FORGE_APO_BUFFER_SILENT,
    FORGE_APO_BUFFER_VALID
} ForgeApoBufferFlags;

/* Structures */

#pragma pack(push, 1)

typedef struct ForgeApoProperties
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
} ForgeApoProperties;

typedef struct ForgeApoLockBuffer
{
    const ForgeAudioFormat *format;
    uint32_t MaxFrameCount;
} ForgeApoLockBuffer;

typedef struct ForgeApoProcessBuffer
{
    void* buffer;
    ForgeApoBufferFlags BufferFlags;
    uint32_t ValidFrameCount;
} ForgeApoProcessBuffer;

#pragma pack(pop)

/* Constants */

#define FORGE_APO_MIN_CHANNELS 1
#define FORGE_APO_MAX_CHANNELS 64

#define FORGE_APO_MIN_SAMPLE_RATE 1000
#define FORGE_APO_MAX_SAMPLE_RATE 200000

#define FORGE_APO_PROPERTIES_STRING_LENGTH 256

#define FORGE_APO_FLAG_CHANNELS_MUST_MATCH        0x00000001
#define FORGE_APO_FLAG_SAMPLE_RATE_MUST_MATCH        0x00000002
#define FORGE_APO_FLAG_BITS_PER_SAMPLE_MUST_MATCH    0x00000004
#define FORGE_APO_FLAG_BUFFER_COUNT_MUST_MATCH    0x00000008
#define FORGE_APO_FLAG_IN_PLACE_REQUIRED        0x00000020
#define FORGE_APO_FLAG_IN_PLACE_SUPPORTED        0x00000010

/* ForgeApo Interface */

#ifndef FORGE_APO_DECL
#define FORGE_APO_DECL
typedef struct ForgeApo ForgeApo;
#endif /* FORGE_APO_DECL */

typedef void (FORGE_APO_CALL * ForgeApoDestroyFunc)(
    void *fapo
);
typedef ForgeResult (FORGE_APO_CALL * ForgeApoGetPropertiesFunc)(
    void* fapo,
    ForgeApoProperties **registration_properties
);
typedef ForgeResult (FORGE_APO_CALL * ForgeApoIsInputFormatSupportedFunc)(
    void* fapo,
    const ForgeAudioFormat *output_format,
    const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format
);
typedef ForgeResult (FORGE_APO_CALL * ForgeApoIsOutputFormatSupportedFunc)(
    void* fapo,
    const ForgeAudioFormat *input_format,
    const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format
);
typedef ForgeResult (FORGE_APO_CALL * ForgeApoInitializeFunc)(
    void* fapo,
    const void* data,
    uint32_t DataByteSize
);
typedef void (FORGE_APO_CALL * ForgeApoResetFunc)(
    void* fapo
);
typedef ForgeResult (FORGE_APO_CALL * ForgeApoLockForProcessFunc)(
    void* fapo,
    uint32_t InputLockedParameterCount,
    const ForgeApoLockBuffer *input_locked_parameters,
    uint32_t OutputLockedParameterCount,
    const ForgeApoLockBuffer *output_locked_parameters
);
typedef void (FORGE_APO_CALL * ForgeApoUnlockForProcessFunc)(
    void* fapo
);
typedef void (FORGE_APO_CALL * ForgeApoProcessFunc)(
    void* fapo,
    uint32_t InputProcessParameterCount,
    const ForgeApoProcessBuffer* input_process_parameters,
    uint32_t OutputProcessParameterCount,
    ForgeApoProcessBuffer* output_process_parameters,
    int32_t IsEnabled
);
typedef uint32_t (FORGE_APO_CALL * ForgeApoCalcInputFramesFunc)(
    void* fapo,
    uint32_t OutputFrameCount
);
typedef uint32_t (FORGE_APO_CALL * ForgeApoCalcOutputFramesFunc)(
    void* fapo,
    uint32_t InputFrameCount
);
typedef void (FORGE_APO_CALL * ForgeApoSetParametersFunc)(
    void* fapo,
    const void* parameters,
    uint32_t ParameterByteSize
);
typedef void (FORGE_APO_CALL * ForgeApoGetParametersFunc)(
    void* fapo,
    void* parameters,
    uint32_t ParameterByteSize
);

struct ForgeApo
{
    ForgeApoDestroyFunc Destroy;
    ForgeApoGetPropertiesFunc GetRegistrationProperties;
    ForgeApoIsInputFormatSupportedFunc IsInputFormatSupported;
    ForgeApoIsOutputFormatSupportedFunc IsOutputFormatSupported;
    ForgeApoInitializeFunc Initialize;
    ForgeApoResetFunc Reset;
    ForgeApoLockForProcessFunc LockForProcess;
    ForgeApoUnlockForProcessFunc UnlockForProcess;
    ForgeApoProcessFunc Process;
    ForgeApoCalcInputFramesFunc CalcInputFrames;
    ForgeApoCalcOutputFramesFunc CalcOutputFrames;
    ForgeApoSetParametersFunc SetParameters;
    ForgeApoGetParametersFunc GetParameters;
};

FORGE_APO_API void forge_apo_destroy(ForgeApo *apo);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_APO_H */
