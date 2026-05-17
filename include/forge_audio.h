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

#ifndef FORGE_AUDIO_H
#define FORGE_AUDIO_H

#ifdef _WIN32
#define FORGE_AUDIO_API __declspec(dllexport)
#define FORGE_AUDIO_CALL __cdecl
#else
#define FORGE_AUDIO_API
#define FORGE_AUDIO_CALL
#endif

/* -Wpedantic nameless union/struct silencing */
#ifndef FORGE_AUDIO_NAMELESS
#ifdef __GNUC__
#define FORGE_AUDIO_NAMELESS __extension__
#else
#define FORGE_AUDIO_NAMELESS
#endif /* __GNUC__ */
#endif /* FORGE_AUDIO_NAMELESS */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type Declarations */

typedef struct ForgeAudioEngine ForgeAudioEngine;
typedef struct ForgeVoice ForgeVoice;
typedef ForgeVoice ForgeSourceVoice;
typedef ForgeVoice ForgeSubmixVoice;
typedef ForgeVoice ForgeMasterVoice;
typedef struct ForgeEngineCallback ForgeEngineCallback;
typedef struct ForgeVoiceCallback ForgeVoiceCallback;

/* Enumerations */

typedef enum ForgeDeviceRole
{
    ForgeDeviceRoleNone =        0x0,
    ForgeDeviceRoleConsole =        0x1,
    ForgeDeviceRoleMultimedia =        0x2,
    ForgeDeviceRoleCommunications =    0x4,
    ForgeDeviceRoleGame =        0x8,
    ForgeDeviceRoleDefault =        0xF,
    ForgeDeviceRoleInvalid = ~ForgeDeviceRoleDefault
} ForgeDeviceRole;

typedef enum ForgeFilterType
{
    ForgeFilterLowPass,
    ForgeFilterBandPass,
    ForgeFilterHighPass,
    ForgeFilterNotch
} ForgeFilterType;

typedef enum ForgeStreamCategory
{
    ForgeStreamCategory_Other,
    ForgeStreamCategory_ForegroundOnlyMedia,
    ForgeStreamCategory_BackgroundCapableMedia,
    ForgeStreamCategory_Communications,
    ForgeStreamCategory_Alerts,
    ForgeStreamCategory_SoundEffects,
    ForgeStreamCategory_GameEffects,
    ForgeStreamCategory_GameMedia,
    ForgeStreamCategory_GameChat,
    ForgeStreamCategory_Speech,
    ForgeStreamCategory_Movie,
    ForgeStreamCategory_Media
} ForgeStreamCategory;

/* FIXME: The original enum violates ISO C and is platform specific anyway... */
typedef uint32_t ForgeProcessor;
#define FORGE_AUDIO_DEFAULT_PROCESSOR 0xFFFFFFFF

/* Structures */

#pragma pack(push, 1)

typedef struct ForgeGuid
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} ForgeGuid;

/* See MSDN:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/dd390970%28v=vs.85%29.aspx
 */
typedef struct ForgeAudioFormat
{
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
} ForgeAudioFormat;

/* See MSDN:
 * https://msdn.microsoft.com/en-us/library/windows/desktop/dd390971(v=vs.85).aspx
 */
typedef struct ForgeAudioFormatExtensible
{
    ForgeAudioFormat Format;
    union
    {
        uint16_t wValidBitsPerSample;
        uint16_t wSamplesPerBlock;
        uint16_t wReserved;
    } Samples;
    uint32_t dwChannelMask;
    ForgeGuid SubFormat;
} ForgeAudioFormatExtensible;

typedef struct ForgeDeviceDetails
{
    int16_t DeviceID[256]; /* Win32 wchar_t */
    int16_t DisplayName[256]; /* Win32 wchar_t */
    ForgeDeviceRole Role;
    ForgeAudioFormatExtensible OutputFormat;
} ForgeDeviceDetails;

typedef struct ForgeVoiceDetails
{
    uint32_t CreationFlags;
    uint32_t ActiveFlags;
    uint32_t InputChannels;
    uint32_t InputSampleRate;
} ForgeVoiceDetails;

typedef struct ForgeSend
{
    uint32_t Flags; /* 0 or FORGE_AUDIO_SEND_USEFILTER */
    ForgeVoice *pOutputVoice;
} ForgeSend;

typedef struct ForgeSendList
{
    uint32_t SendCount;
    ForgeSend *pSends;
} ForgeSendList;

#ifndef FAPO_DECL
#define FAPO_DECL
typedef struct FAPO FAPO;
#endif /* FAPO_DECL */

typedef struct ForgeEffect
{
    FAPO *pEffect;
    int32_t InitialState; /* 1 - Enabled, 0 - Disabled */
    uint32_t OutputChannels;
} ForgeEffect;

typedef struct ForgeEffectChain
{
    uint32_t EffectCount;
    ForgeEffect *pEffectDescriptors;
} ForgeEffectChain;

typedef struct ForgeFilterParameters
{
    ForgeFilterType Type;
    float Frequency;    /* [0, FORGE_AUDIO_MAX_FILTER_FREQUENCY] */
    float OneOverQ;        /* [0, FORGE_AUDIO_MAX_FILTER_ONEOVERQ] */
} ForgeFilterParameters;

typedef struct ForgeFilterParametersEx
{
    ForgeFilterType Type;
    float Frequency;    /* [0, FORGE_AUDIO_MAX_FILTER_FREQUENCY] */
    float OneOverQ;        /* [0, FORGE_AUDIO_MAX_FILTER_ONEOVERQ] */
    float WetDryMix;    /* [0, 1] */
} ForgeFilterParametersEx;

typedef struct ForgeBuffer
{
    /* Either 0 or FORGE_AUDIO_END_OF_STREAM */
    uint32_t Flags;
    /* Pointer to wave data, memory block size.
     * Note that pAudioData is not copied; ForgeAudioEngine reads directly from your
     * pointer! This pointer must be valid until ForgeAudioEngine has finished using
     * it, at which point an OnBufferEnd callback will be generated.
     */
    uint32_t AudioBytes;
    const uint8_t *pAudioData;
    /* Play region, in sample frames. */
    uint32_t PlayBegin;
    uint32_t PlayLength;
    /* Loop region, in sample frames.
     * This can be used to loop a subregion of the wave instead of looping
     * the whole thing, i.e. if you have an intro/outro you can set these
     * to loop the middle sections instead. If you don't need this, set both
     * values to 0.
     */
    uint32_t LoopBegin;
    uint32_t LoopLength;
    /* [0, FORGE_AUDIO_LOOP_INFINITE] */
    uint32_t LoopCount;
    /* This is sent to callbacks as pBufferContext */
    void *pContext;
} ForgeBuffer;

typedef struct ForgeBufferWMA
{
    const uint32_t *pDecodedPacketCumulativeBytes;
    uint32_t PacketCount;
} ForgeBufferWMA;

typedef struct ForgeVoiceState
{
    void *pCurrentBufferContext;
    uint32_t BuffersQueued;
    uint64_t SamplesPlayed;
} ForgeVoiceState;

typedef struct ForgePerformanceData
{
    uint64_t AudioCyclesSinceLastQuery;
    uint64_t TotalCyclesSinceLastQuery;
    uint32_t MinimumCyclesPerQuantum;
    uint32_t MaximumCyclesPerQuantum;
    uint32_t MemoryUsageInBytes;
    uint32_t CurrentLatencyInSamples;
    uint32_t GlitchesSinceEngineStarted;
    uint32_t ActiveSourceVoiceCount;
    uint32_t TotalSourceVoiceCount;
    uint32_t ActiveSubmixVoiceCount;
    uint32_t ActiveResamplerCount;
    uint32_t ActiveMatrixMixCount;
    uint32_t ActiveXmaSourceVoices;
    uint32_t ActiveXmaStreams;
} ForgePerformanceData;

typedef struct ForgeDebugConfiguration
{
    /* See FORGE_AUDIO_LOG_* */
    uint32_t TraceMask;
    uint32_t BreakMask;
    /* 0 or 1 */
    int32_t LogThreadID;
    int32_t LogFileline;
    int32_t LogFunctionName;
    int32_t LogTiming;
} ForgeDebugConfiguration;

#pragma pack(pop)

/* This ISN'T packed. Strictly speaking it wouldn't have mattered anyway but eh.
 * See https://github.com/microsoft/DirectXTK/issues/256
 */
typedef struct ForgeXMA2FormatEx
{
    ForgeAudioFormat wfx;
    uint16_t wNumStreams;
    uint32_t dwChannelMask;
    uint32_t dwSamplesEncoded;
    uint32_t dwBytesPerBlock;
    uint32_t dwPlayBegin;
    uint32_t dwPlayLength;
    uint32_t dwLoopBegin;
    uint32_t dwLoopLength;
    uint8_t  bLoopCount;
    uint8_t  bEncoderVersion;
    uint16_t wBlockCount;
} ForgeXMA2Format;

/* Constants */

#define FORGE_AUDIO_E_OUT_OF_MEMORY        0x8007000e
#define FORGE_AUDIO_E_INVALID_ARG        0x80070057
#define FORGE_AUDIO_E_UNSUPPORTED_FORMAT    0x88890008
#define FORGE_AUDIO_E_INVALID_CALL        0x88960001
#define FORGE_AUDIO_E_DEVICE_INVALIDATED    0x88960004
#define FAPO_E_FORMAT_UNSUPPORTED    0x88970001

#define FORGE_AUDIO_MAX_BUFFER_BYTES        0x80000000
#define FORGE_AUDIO_MAX_QUEUED_BUFFERS    64
#define FORGE_AUDIO_MAX_AUDIO_CHANNELS    64
#define FORGE_AUDIO_MIN_SAMPLE_RATE        1000
#define FORGE_AUDIO_MAX_SAMPLE_RATE        200000
#define FORGE_AUDIO_MAX_VOLUME_LEVEL        16777216.0f
#define FORGE_AUDIO_MIN_FREQ_RATIO        (1.0f / 1024.0f)
#define FORGE_AUDIO_MAX_FREQ_RATIO        1024.0f
#define FORGE_AUDIO_DEFAULT_FREQ_RATIO    2.0f
#define FORGE_AUDIO_MAX_FILTER_ONEOVERQ    1.5f
#define FORGE_AUDIO_MAX_FILTER_FREQUENCY    1.0f
#define FORGE_AUDIO_MAX_LOOP_COUNT        254

#define FORGE_AUDIO_COMMIT_NOW        0
#define FORGE_AUDIO_COMMIT_ALL        0
#define FORGE_AUDIO_INVALID_OPSET        (uint32_t) (-1)
#define FORGE_AUDIO_NO_LOOP_REGION        0
#define FORGE_AUDIO_LOOP_INFINITE        255
#define FORGE_AUDIO_DEFAULT_CHANNELS        0
#define FORGE_AUDIO_DEFAULT_SAMPLERATE    0

#define FORGE_AUDIO_DEBUG_ENGINE        0x0001
#define FORGE_AUDIO_VOICE_NOPITCH        0x0002
#define FORGE_AUDIO_VOICE_NOSRC        0x0004
#define FORGE_AUDIO_VOICE_USEFILTER        0x0008
#define FORGE_AUDIO_VOICE_MUSIC        0x0010
#define FORGE_AUDIO_PLAY_TAILS        0x0020
#define FORGE_AUDIO_END_OF_STREAM        0x0040
#define FORGE_AUDIO_SEND_USEFILTER        0x0080
#define FORGE_AUDIO_VOICE_NOSAMPLESPLAYED    0x0100
#define FORGE_AUDIO_1024_QUANTUM        0x8000

#define FORGE_AUDIO_DEFAULT_FILTER_TYPE    ForgeFilterLowPass
#define FORGE_AUDIO_DEFAULT_FILTER_FREQUENCY    FORGE_AUDIO_MAX_FILTER_FREQUENCY
#define FORGE_AUDIO_DEFAULT_FILTER_ONEOVERQ    1.0f
#define FORGE_AUDIO_DEFAULT_FILTER_WETDRYMIX_EXT    1.0f

#define FORGE_AUDIO_LOG_ERRORS        0x0001
#define FORGE_AUDIO_LOG_WARNINGS        0x0002
#define FORGE_AUDIO_LOG_INFO            0x0004
#define FORGE_AUDIO_LOG_DETAIL        0x0008
#define FORGE_AUDIO_LOG_API_CALLS        0x0010
#define FORGE_AUDIO_LOG_FUNC_CALLS        0x0020
#define FORGE_AUDIO_LOG_TIMING        0x0040
#define FORGE_AUDIO_LOG_LOCKS        0x0080
#define FORGE_AUDIO_LOG_MEMORY        0x0100
#define FORGE_AUDIO_LOG_STREAMING        0x1000

#ifndef _SPEAKER_POSITIONS_
#define SPEAKER_FRONT_LEFT        0x00000001
#define SPEAKER_FRONT_RIGHT        0x00000002
#define SPEAKER_FRONT_CENTER        0x00000004
#define SPEAKER_LOW_FREQUENCY        0x00000008
#define SPEAKER_BACK_LEFT        0x00000010
#define SPEAKER_BACK_RIGHT        0x00000020
#define SPEAKER_FRONT_LEFT_OF_CENTER    0x00000040
#define SPEAKER_FRONT_RIGHT_OF_CENTER    0x00000080
#define SPEAKER_BACK_CENTER        0x00000100
#define SPEAKER_SIDE_LEFT        0x00000200
#define SPEAKER_SIDE_RIGHT        0x00000400
#define SPEAKER_TOP_CENTER        0x00000800
#define SPEAKER_TOP_FRONT_LEFT        0x00001000
#define SPEAKER_TOP_FRONT_CENTER    0x00002000
#define SPEAKER_TOP_FRONT_RIGHT        0x00004000
#define SPEAKER_TOP_BACK_LEFT        0x00008000
#define SPEAKER_TOP_BACK_CENTER        0x00010000
#define SPEAKER_TOP_BACK_RIGHT        0x00020000
#define _SPEAKER_POSITIONS_
#endif

#ifndef SPEAKER_MONO
#define SPEAKER_MONO    SPEAKER_FRONT_CENTER
#define SPEAKER_STEREO    (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
#define SPEAKER_2POINT1 \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_LOW_FREQUENCY    )
#define SPEAKER_SURROUND \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_FRONT_CENTER    | \
        SPEAKER_BACK_CENTER    )
#define SPEAKER_QUAD \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_BACK_LEFT    | \
        SPEAKER_BACK_RIGHT    )
#define SPEAKER_4POINT1 \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_LOW_FREQUENCY    | \
        SPEAKER_BACK_LEFT    | \
        SPEAKER_BACK_RIGHT    )
#define SPEAKER_5POINT1 \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_FRONT_CENTER    | \
        SPEAKER_LOW_FREQUENCY    | \
        SPEAKER_BACK_LEFT    | \
        SPEAKER_BACK_RIGHT    )
#define SPEAKER_7POINT1 \
    (    SPEAKER_FRONT_LEFT        | \
        SPEAKER_FRONT_RIGHT        | \
        SPEAKER_FRONT_CENTER        | \
        SPEAKER_LOW_FREQUENCY        | \
        SPEAKER_BACK_LEFT        | \
        SPEAKER_BACK_RIGHT        | \
        SPEAKER_FRONT_LEFT_OF_CENTER    | \
        SPEAKER_FRONT_RIGHT_OF_CENTER    )
#define SPEAKER_5POINT1_SURROUND \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_FRONT_CENTER    | \
        SPEAKER_LOW_FREQUENCY    | \
        SPEAKER_SIDE_LEFT    | \
        SPEAKER_SIDE_RIGHT    )
#define SPEAKER_7POINT1_SURROUND \
    (    SPEAKER_FRONT_LEFT    | \
        SPEAKER_FRONT_RIGHT    | \
        SPEAKER_FRONT_CENTER    | \
        SPEAKER_LOW_FREQUENCY    | \
        SPEAKER_BACK_LEFT    | \
        SPEAKER_BACK_RIGHT    | \
        SPEAKER_SIDE_LEFT    | \
        SPEAKER_SIDE_RIGHT    )
#define SPEAKER_XBOX SPEAKER_5POINT1
#endif

#define FORGE_AUDIO_FORMAT_PCM        1
#define FORGE_AUDIO_FORMAT_IEEE_FLOAT    3
#define FORGE_AUDIO_FORMAT_WMAUDIO2        0x0161
#define FORGE_AUDIO_FORMAT_WMAUDIO3        0x0162
#define FORGE_AUDIO_FORMAT_WMAUDIO_LOSSLESS    0x0163
#define FORGE_AUDIO_FORMAT_XMAUDIO2        0x0166
#define FORGE_AUDIO_FORMAT_EXTENSIBLE    0xFFFE

extern ForgeGuid FORGE_AUDIO_SUBTYPE_PCM;
extern ForgeGuid FORGE_AUDIO_SUBTYPE_IEEE_FLOAT;
extern ForgeGuid FORGE_AUDIO_SUBTYPE_XMAUDIO2;
extern ForgeGuid FORGE_AUDIO_SUBTYPE_WMAUDIO2;
extern ForgeGuid FORGE_AUDIO_SUBTYPE_WMAUDIO3;
extern ForgeGuid FORGE_AUDIO_SUBTYPE_WMAUDIO_LOSSLESS;

/* Version API */

#define FORGE_AUDIO_ABI_VERSION     0
#define FORGE_AUDIO_MAJOR_VERSION     0
#define FORGE_AUDIO_MINOR_VERSION     1
#define FORGE_AUDIO_PATCH_VERSION     0

#define FORGE_AUDIO_COMPILED_VERSION ( \
    (FORGE_AUDIO_ABI_VERSION * 100 * 100 * 100) + \
    (FORGE_AUDIO_MAJOR_VERSION * 100 * 100) + \
    (FORGE_AUDIO_MINOR_VERSION * 100) + \
    (FORGE_AUDIO_PATCH_VERSION) \
)

FORGE_AUDIO_API uint32_t forge_audio_linked_version(void);

/* Engine Interface */

/* This should be your first ForgeAudio call.
 *
 * ppFAudio:        Filled with the audio engine context.
 * Flags:        Can be 0 or a combination of FORGE_AUDIO_DEBUG_ENGINE and FORGE_AUDIO_1024_QUANTUM.
 * XAudio2Processor:    Set this to FORGE_AUDIO_DEFAULT_PROCESSOR.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_create(
    ForgeAudioEngine **ppFAudio,
    uint32_t Flags,
    ForgeProcessor XAudio2Processor
);

/* See "extensions/COMConstructEXT.txt" for more details */
FORGE_AUDIO_API uint32_t forge_audio_construct(ForgeAudioEngine **ppFAudio);

/* Increments a reference counter. When counter is 0, audio is freed.
 * Returns the reference count after incrementing.
 */
FORGE_AUDIO_API uint32_t forge_audio_engine_retain(ForgeAudioEngine *audio);

/* Decrements a reference counter. When counter is 0, audio is freed.
 * Returns the reference count after decrementing.
 */
FORGE_AUDIO_API uint32_t forge_audio_engine_release(ForgeAudioEngine *audio);

/* Queries the number of sound devices available for use.
 *
 * pCount: Filled with the number of available sound devices.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_get_device_count(ForgeAudioEngine *audio, uint32_t *pCount);

/* Gets basic information about a sound device.
 *
 * Index:        Can be between 0 and the result of GetDeviceCount.
 * pDeviceDetails:    Filled with the device information.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_get_device_details(
    ForgeAudioEngine *audio,
    uint32_t Index,
    ForgeDeviceDetails *pDeviceDetails
);

/* You don't actually have to call this, unless you're using the COM APIs.
 * See the forge_audio_create API for parameter information.
 */
FORGE_AUDIO_API uint32_t forge_audio_initialize(
    ForgeAudioEngine *audio,
    uint32_t Flags,
    ForgeProcessor XAudio2Processor
);

/* Register a new set of engine callbacks.
 * There is no limit to the number of sets, but expect performance to degrade
 * if you have a whole bunch of these. You most likely only need one.
 *
 * pCallback: The completely-initialized ForgeEngineCallback structure.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_register_callback(
    ForgeAudioEngine *audio,
    ForgeEngineCallback *pCallback
);

/* Remove an active set of engine callbacks.
 * This checks the pointer value, NOT the callback values!
 *
 * pCallback: An ForgeEngineCallback structure previously sent to Register.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API void forge_audio_unregister_callback(
    ForgeAudioEngine *audio,
    ForgeEngineCallback *pCallback
);

/* Creates a "source" voice, used to play back wavedata.
 *
 * ppSourceVoice:    Filled with the source voice pointer.
 * pSourceFormat:    The input wavedata format, see the documentation for
 *            ForgeAudioFormat.
 * Flags:        Can be 0 or a mix of the following FORGE_AUDIO_VOICE_* flags:
 *            NOPITCH/NOSRC:    Resampling is disabled. If you set this,
 *                    the source format sample rate MUST match
 *                    the output voices' input sample rates.
 *                    Also, SetFrequencyRatio will fail.
 *            USEFILTER:    Enables the use of SetFilterParameters.
 *            MUSIC:        Unsupported.
 * MaxFrequencyRatio:    AKA your max pitch. This allows us to optimize the size
 *            of the decode/resample cache sizes. For example, if you
 *            only expect to raise pitch by a single octave, you can
 *            set this value to 2.0f. 2.0f is the default value.
 *            Bounds: [FORGE_AUDIO_MIN_FREQ_RATIO, FORGE_AUDIO_MAX_FREQ_RATIO].
 * pCallback:        Voice callbacks, see ForgeVoiceCallback documentation.
 * pSendList:        List of output voices. If NULL, defaults to master.
 *            All output voices must have the same sample rate!
 * pEffectChain:    List of FAPO effects. This value can be NULL.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_create_source_voice(
    ForgeAudioEngine *audio,
    ForgeSourceVoice **ppSourceVoice,
    const ForgeAudioFormat *pSourceFormat,
    uint32_t Flags,
    float MaxFrequencyRatio,
    ForgeVoiceCallback *pCallback,
    const ForgeSendList *pSendList,
    const ForgeEffectChain *pEffectChain
);

/* Creates a "submix" voice, used to mix/process input voices.
 * The typical use case for this is to perform CPU-intensive tasks on large
 * groups of voices all at once. Examples include resampling and FAPO effects.
 *
 * ppSubmixVoice:    Filled with the submix voice pointer.
 * InputChannels:    Input voices will convert to this channel count.
 * InputSampleRate:    Input voices will convert to this sample rate.
 * Flags:        Can be 0 or FORGE_AUDIO_VOICE_USEFILTER.
 * ProcessingStage:    If you have multiple submixes that depend on a specific
 *            order of processing, you can sort them by setting this
 *            value to prioritize them. For example, submixes with
 *            stage 0 will process first, then stage 1, 2, and so on.
 * pSendList:        List of output voices. If NULL, defaults to master.
 *            All output voices must have the same sample rate!
 * pEffectChain:    List of FAPO effects. This value can be NULL.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_create_submix_voice(
    ForgeAudioEngine *audio,
    ForgeSubmixVoice **ppSubmixVoice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint32_t ProcessingStage,
    const ForgeSendList *pSendList,
    const ForgeEffectChain *pEffectChain
);

/* This should be your second ForgeAudioEngine call, unless you care about which device
 * you want to use. In that case, see GetDeviceDetails.
 *
 * ppMasteringVoice:    Filled with the mastering voice pointer.
 * InputChannels:    Device channel count. Can be FORGE_AUDIO_DEFAULT_CHANNELS.
 * InputSampleRate:    Device sample rate. Can be FORGE_AUDIO_DEFAULT_SAMPLERATE.
 * Flags:        This value must be 0.
 * DeviceIndex:        0 for the default device. See GetDeviceCount.
 * pEffectChain:    List of FAPO effects. This value can be NULL.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_create_master_voice(
    ForgeAudioEngine *audio,
    ForgeMasterVoice **ppMasteringVoice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint32_t DeviceIndex,
    const ForgeEffectChain *pEffectChain
);

/* This is the XAudio 2.8+ version of CreateMasteringVoice.
 * Right now this doesn't do anything. Don't use this function.
 */
FORGE_AUDIO_API uint32_t forge_audio_create_master_voice_with_category(
    ForgeAudioEngine *audio,
    ForgeMasterVoice **ppMasteringVoice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    uint16_t *szDeviceId,
    const ForgeEffectChain *pEffectChain,
    ForgeStreamCategory StreamCategory
);

/* Starts the engine, begins processing the audio graph.
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_start_engine(ForgeAudioEngine *audio);

/* Stops the engine and halts all processing.
 * The audio device will continue to run, but will produce silence.
 * The graph will be frozen until you call StartEngine, where it will then
 * resume all processing exactly as it would have had this never been called.
 */
FORGE_AUDIO_API void forge_audio_stop_engine(ForgeAudioEngine *audio);

/* Flushes a batch of ForgeAudioEngine calls compiled with a given "OperationSet" tag.
 * This function is based on IXAudio2::CommitChanges from the XAudio2 spec.
 * This is useful for pushing calls that need to be done perfectly in sync. For
 * example, if you want to play two separate sources at the exact same time, you
 * can call forge_source_voice_start with an OperationSet value of your choice,
 * then call CommitChanges with that same value to start the sources together.
 *
 * OperationSet: Either a value known by you or FORGE_AUDIO_COMMIT_ALL
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_audio_commit_operation_set(
    ForgeAudioEngine *audio,
    uint32_t OperationSet
);

/* Requests various bits of performance information from the engine.
 *
 * pPerfData: Filled with the data. See ForgePerformanceData for details.
 */
FORGE_AUDIO_API void forge_audio_get_performance_data(
    ForgeAudioEngine *audio,
    ForgePerformanceData *pPerfData
);

/* When using a Debug binary, this lets you configure what information gets
 * logged to output. Be careful, this can spit out a LOT of text.
 *
 * pDebugConfiguration:    See ForgeDebugConfiguration for details.
 * pReserved:        Set this to NULL.
 */
FORGE_AUDIO_API void forge_audio_set_debug_configuration(
    ForgeAudioEngine *audio,
    ForgeDebugConfiguration *pDebugConfiguration,
    void* pReserved
);

/* Requests the values that determine's the engine's update size.
 * For example, a 48KHz engine with a 1024-sample update period would return
 * 1024 for the numerator and 48000 for the denominator. With this information,
 * you can determine the precise update size in milliseconds.
 *
 * quantumNumerator - The engine's update size, in sample frames.
 * quantumDenominator - The engine's sample rate, in Hz
 */
FORGE_AUDIO_API void forge_audio_get_processing_quantum(
    ForgeAudioEngine *audio,
    uint32_t *quantumNumerator,
    uint32_t *quantumDenominator
);

/* ForgeVoice Interface */

/* Requests basic information about a voice.
 *
 * pVoiceDetails: See ForgeVoiceDetails for details.
 */
FORGE_AUDIO_API void forge_voice_get_details(
    ForgeVoice *voice,
    ForgeVoiceDetails *pVoiceDetails
);

/* Change the output voices for this voice.
 * This function is invalid for mastering voices.
 *
 * pSendList:    List of output voices. If NULL, defaults to master.
 *        All output voices must have the same sample rate!
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_outputs(
    ForgeVoice *voice,
    const ForgeSendList *pSendList
);

/* Change/Remove the effect chain for this voice.
 *
 * pEffectChain:    List of FAPO effects. This value can be NULL.
 *            Note that the final channel counts for this chain MUST
 *            match the input/output channel count that was
 *            determined at voice creation time!
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_effect_chain(
    ForgeVoice *voice,
    const ForgeEffectChain *pEffectChain
);

/* Enables an effect in the effect chain.
 *
 * EffectIndex:        The index of the effect (based on the chain order).
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_enable_effect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
);

/* Disables an effect in the effect chain.
 *
 * EffectIndex:        The index of the effect (based on the chain order).
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_disable_effect(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    uint32_t OperationSet
);

/* Queries the enabled/disabled state of an effect in the effect chain.
 *
 * EffectIndex:    The index of the effect (based on the chain order).
 * pEnabled:    Filled with either 1 (Enabled) or 0 (Disabled).
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API void forge_voice_get_effect_state(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    int32_t *pEnabled
);

/* Submits a block of memory to be sent to FAPO::SetParameters.
 *
 * EffectIndex:        The index of the effect (based on the chain order).
 * pParameters:        The values to be copied and submitted to the FAPO.
 * ParametersByteSize:    This should match what the FAPO expects!
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_effect_parameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    const void *pParameters,
    uint32_t ParametersByteSize,
    uint32_t OperationSet
);

/* Requests the latest parameters from FAPO::GetParameters.
 *
 * EffectIndex:        The index of the effect (based on the chain order).
 * pParameters:        Filled with the latest parameter values from the FAPO.
 * ParametersByteSize:    This should match what the FAPO expects!
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_get_effect_parameters(
    ForgeVoice *voice,
    uint32_t EffectIndex,
    void *pParameters,
    uint32_t ParametersByteSize
);

/* Sets the filter variables for a voice.
 * This is only valid on voices with the USEFILTER flag.
 *
 * pParameters:        See ForgeFilterParameters for details.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_filter_parameters(
    ForgeVoice *voice,
    const ForgeFilterParameters *pParameters,
    uint32_t OperationSet
);

/* Requests the filter variables for a voice.
 * This is only valid on voices with the USEFILTER flag.
 *
 * pParameters: See ForgeFilterParameters for details.
 */
FORGE_AUDIO_API void forge_voice_get_filter_parameters(
    ForgeVoice *voice,
    ForgeFilterParameters *pParameters
);

/* Sets the filter variables for a voice's output voice.
 * This is only valid on sends with the USEFILTER flag.
 *
 * pDestinationVoice:    An output voice from the voice's send list.
 * pParameters:        See ForgeFilterParameters for details.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    const ForgeFilterParameters *pParameters,
    uint32_t OperationSet
);

/* Requests the filter variables for a voice's output voice.
 * This is only valid on sends with the USEFILTER flag.
 *
 * pDestinationVoice:    An output voice from the voice's send list.
 * pParameters:        See ForgeFilterParameters for details.
 */
FORGE_AUDIO_API void forge_voice_get_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    ForgeFilterParameters *pParameters
);

/* Sets the filter variables for a voice.
 * This is only valid on voices with the USEFILTER flag.
 *
 * pParameters:        See ForgeFilterParametersEx for details.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_filter_parameters_ex(
    ForgeVoice* voice,
    const ForgeFilterParametersEx* pParameters,
    uint32_t OperationSet
);

/* Requests the filter variables for a voice.
 * This is only valid on voices with the USEFILTER flag.
 *
 * pParameters: See ForgeFilterParametersEx for details.
 */
FORGE_AUDIO_API void forge_voice_get_filter_parameters_ex(
    ForgeVoice* voice,
    ForgeFilterParametersEx* pParameters
);

/* Sets the filter variables for a voice's output voice.
 * This is only valid on sends with the USEFILTER flag.
 *
 * pDestinationVoice:    An output voice from the voice's send list.
 * pParameters:        See ForgeFilterParametersEx for details.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_output_filter_parameters_ex(
    ForgeVoice* voice,
    ForgeVoice* pDestinationVoice,
    const ForgeFilterParametersEx* pParameters,
    uint32_t OperationSet
);

/* Requests the filter variables for a voice's output voice.
 * This is only valid on sends with the USEFILTER flag.
 *
 * pDestinationVoice:    An output voice from the voice's send list.
 * pParameters:        See ForgeFilterParametersEx for details.
 */
FORGE_AUDIO_API void forge_voice_get_output_filter_parameters_ex(
    ForgeVoice* voice,
    ForgeVoice* pDestinationVoice,
    ForgeFilterParametersEx* pParameters
);

/* Sets the global volume of a voice.
 *
 * Volume:        Amplitude ratio. 1.0f is default, 0.0f is silence.
 *            Note that you can actually set volume < 0.0f!
 *            Bounds: [-FORGE_AUDIO_MAX_VOLUME_LEVEL, FORGE_AUDIO_MAX_VOLUME_LEVEL]
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_volume(
    ForgeVoice *voice,
    float Volume,
    uint32_t OperationSet
);

/* Requests the global volume of a voice.
 *
 * pVolume: Filled with the current voice amplitude ratio.
 */
FORGE_AUDIO_API void forge_voice_get_volume(
    ForgeVoice *voice,
    float *pVolume
);

/* Sets the per-channel volumes of a voice.
 *
 * Channels:        Must match the channel count of this voice!
 * pVolumes:        Amplitude ratios for each channel. Same as SetVolume.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_channel_volumes(
    ForgeVoice *voice,
    uint32_t Channels,
    const float *pVolumes,
    uint32_t OperationSet
);

/* Requests the per-channel volumes of a voice.
 *
 * Channels:    Must match the channel count of this voice!
 * pVolumes:    Filled with the current channel amplitude ratios.
 */
FORGE_AUDIO_API void forge_voice_get_channel_volumes(
    ForgeVoice *voice,
    uint32_t Channels,
    float *pVolumes
);

/* Sets the volumes of a send's output channels. The matrix is based on the
 * voice's input channels. For example, the default matrix for a 2-channel
 * source and a 2-channel output voice is as follows:
 * [0] = 1.0f; <- Left input, left output
 * [1] = 0.0f; <- Right input, left output
 * [2] = 0.0f; <- Left input, right output
 * [3] = 1.0f; <- Right input, right output
 * This is typically only used for panning or 3D sound (via F3DAudio).
 *
 * pDestinationVoice:    An output voice from the voice's send list.
 * SourceChannels:    Must match the voice's input channel count!
 * DestinationChannels:    Must match the destination's input channel count!
 * pLevelMatrix:    A float[SourceChannels * DestinationChannels].
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_voice_set_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    const float *pLevelMatrix,
    uint32_t OperationSet
);

/* Gets the volumes of a send's output channels. See SetOutputMatrix.
 *
 * pDestinationVoice:    An output voice from the voice's send list.
 * SourceChannels:    Must match the voice's input channel count!
 * DestinationChannels:    Must match the voice's output channel count!
 * pLevelMatrix:    A float[SourceChannels * DestinationChannels].
 */
FORGE_AUDIO_API void forge_voice_get_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *pDestinationVoice,
    uint32_t SourceChannels,
    uint32_t DestinationChannels,
    float *pLevelMatrix
);

/* Removes this voice from the audio graph and frees memory. */
FORGE_AUDIO_API void forge_voice_destroy(ForgeVoice *voice);

/*
 * Returns S_OK on success and E_FAIL if voice could not be destroyed (e. g., because it is in use).
*/

FORGE_AUDIO_API uint32_t forge_voice_destroy_safe(ForgeVoice *voice);

/* ForgeSourceVoice Interface */

/* Starts processing for a source voice.
 *
 * Flags:        Must be 0.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_start(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
);

/* Pauses processing for a source voice. Yes, I said pausing.
 * If you want to _actually_ stop, call FlushSourceBuffers next.
 *
 * Flags:        Can be 0 or FORGE_AUDIO_PLAY_TAILS, which allows effects to
 *            keep emitting output even after processing has stopped.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_stop(
    ForgeSourceVoice *voice,
    uint32_t Flags,
    uint32_t OperationSet
);

/* Submits a block of wavedata for the source to process.
 *
 * pBuffer:    See ForgeBuffer for details.
 * pBufferWMA:    See ForgeBufferWMA for details. (Also, don't use WMA.)
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_submit_buffer(
    ForgeSourceVoice *voice,
    const ForgeBuffer *pBuffer,
    const ForgeBufferWMA *pBufferWMA
);

/* Removes all buffers from a source, with a minor exception.
 * If the voice is still playing, the active buffer is left alone.
 * All buffers that are removed will spawn an OnBufferEnd callback.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_flush_buffers(
    ForgeSourceVoice *voice
);

/* Takes the last buffer currently queued and sets the END_OF_STREAM flag.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_end_stream(
    ForgeSourceVoice *voice
);

/* Sets the loop count of the active buffer to 0.
 *
 * OperationSet: See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_break_loop(
    ForgeSourceVoice *voice,
    uint32_t OperationSet
);

/* Requests the state and some basic statistics for this source.
 *
 * pVoiceState:    See ForgeVoiceState for details.
 * Flags:    Can be 0 or FORGE_AUDIO_VOICE_NOSAMPLESPLAYED.
 */
FORGE_AUDIO_API void forge_source_voice_get_state(
    ForgeSourceVoice *voice,
    ForgeVoiceState *pVoiceState,
    uint32_t Flags
);

/* Sets the frequency ratio (fancy phrase for pitch) of this source.
 *
 * Ratio:        The frequency ratio, must be <= MaxFrequencyRatio.
 * OperationSet:    See CommitChanges. Default is FORGE_AUDIO_COMMIT_NOW.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_set_rate(
    ForgeSourceVoice *voice,
    float Ratio,
    uint32_t OperationSet
);

/* Requests the frequency ratio (fancy phrase for pitch) of this source.
 *
 * pRatio: Filled with the frequency ratio.
 */
FORGE_AUDIO_API void forge_source_voice_get_rate(
    ForgeSourceVoice *voice,
    float *pRatio
);

/* Resets the core sample rate of this source.
 * You probably don't want this, it's more likely you want SetFrequencyRatio.
 * This is used to recycle voices without having to constantly reallocate them.
 * For example, if you have wavedata that's all float32 mono, but the sample
 * rates are different, you can take a source that was being used for a 48KHz
 * wave and call this so it can be used for a 44.1KHz wave.
 *
 * NewSourceSampleRate: The new sample rate for this source.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_source_voice_set_sample_rate(
    ForgeSourceVoice *voice,
    uint32_t NewSourceSampleRate
);

/* ForgeMasterVoice Interface */

/* Requests the channel mask for the mastering voice.
 * This is typically used with F3DAudioInitialize, but you may find it
 * interesting if you want to see the user's basic speaker layout.
 *
 * pChannelMask: Filled with the channel mask.
 *
 * Returns 0 on success.
 */
FORGE_AUDIO_API uint32_t forge_master_voice_get_channel_mask(
    ForgeMasterVoice *voice,
    uint32_t *pChannelMask
);

/* ForgeEngineCallback Interface */

/* If something horrible happens, this will be called.
 *
 * Error: The error code that spawned this callback.
 */
typedef void (FORGE_AUDIO_CALL * OnCriticalErrorFunc)(
    ForgeEngineCallback *callback,
    uint32_t Error
);

/* This is called at the end of a processing update. */
typedef void (FORGE_AUDIO_CALL * OnProcessingPassEndFunc)(
    ForgeEngineCallback *callback
);

/* This is called at the beginning of a processing update. */
typedef void (FORGE_AUDIO_CALL * OnProcessingPassStartFunc)(
    ForgeEngineCallback *callback
);

struct ForgeEngineCallback
{
    OnCriticalErrorFunc OnCriticalError;
    OnProcessingPassEndFunc OnProcessingPassEnd;
    OnProcessingPassStartFunc OnProcessingPassStart;
};

/* ForgeVoiceCallback Interface */

/* When a buffer is no longer in use, this is called.
 *
 * pBufferContext: The pContext for the ForgeBuffer in question.
 */
typedef void (FORGE_AUDIO_CALL * OnBufferEndFunc)(
    ForgeVoiceCallback *callback,
    void *pBufferContext
);

/* When a buffer is now being used, this is called.
 *
 * pBufferContext: The pContext for the ForgeBuffer in question.
 */
typedef void (FORGE_AUDIO_CALL * OnBufferStartFunc)(
    ForgeVoiceCallback *callback,
    void *pBufferContext
);

/* When a buffer completes a loop, this is called.
 *
 * pBufferContext: The pContext for the ForgeBuffer in question.
 */
typedef void (FORGE_AUDIO_CALL * OnLoopEndFunc)(
    ForgeVoiceCallback *callback,
    void *pBufferContext
);

/* When a buffer that has the END_OF_STREAM flag is finished, this is called. */
typedef void (FORGE_AUDIO_CALL * OnStreamEndFunc)(
    ForgeVoiceCallback *callback
);

/* If something horrible happens to a voice, this is called.
 *
 * pBufferContext:    The pContext for the ForgeBuffer in question.
 * Error:        The error code that spawned this callback.
 */
typedef void (FORGE_AUDIO_CALL * OnVoiceErrorFunc)(
    ForgeVoiceCallback *callback,
    void *pBufferContext,
    uint32_t Error
);

/* When this voice is done being processed, this is called. */
typedef void (FORGE_AUDIO_CALL * OnVoiceProcessingPassEndFunc)(
    ForgeVoiceCallback *callback
);

/* When a voice is about to start being processed, this is called.
 *
 * BytesRequested:    The number of bytes needed from the application to
 *            complete a full update. For example, if we need 512
 *            frames for a whole update, and the voice is a float32
 *            stereo source, BytesRequired will be 4096.
 */
typedef void (FORGE_AUDIO_CALL * OnVoiceProcessingPassStartFunc)(
    ForgeVoiceCallback *callback,
    uint32_t BytesRequired
);

struct ForgeVoiceCallback
{
    OnBufferEndFunc OnBufferEnd;
    OnBufferStartFunc OnBufferStart;
    OnLoopEndFunc OnLoopEnd;
    OnStreamEndFunc OnStreamEnd;
    OnVoiceErrorFunc OnVoiceError;
    OnVoiceProcessingPassEndFunc OnVoiceProcessingPassEnd;
    OnVoiceProcessingPassStartFunc OnVoiceProcessingPassStart;
};

/* Custom Allocator API
 * See "extensions/CustomAllocatorEXT.txt" for more information.
 */

typedef void* (FORGE_AUDIO_CALL * ForgeMallocFunc)(size_t size);
typedef void (FORGE_AUDIO_CALL * ForgeFreeFunc)(void* ptr);
typedef void* (FORGE_AUDIO_CALL * ForgeReallocFunc)(void* ptr, size_t size);

FORGE_AUDIO_API uint32_t forge_audio_create_with_allocator(
    ForgeAudioEngine **ppFAudio,
    uint32_t Flags,
    ForgeProcessor XAudio2Processor,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);
FORGE_AUDIO_API uint32_t forge_audio_construct_with_allocator(
    ForgeAudioEngine **ppFAudio,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);

/* Engine Procedure API
 * See "extensions/EngineProcedureEXT.txt" for more information.
 */
typedef void (FORGE_AUDIO_CALL *ForgeEngineCall)(ForgeAudioEngine *audio, float *output);
typedef void (FORGE_AUDIO_CALL *ForgeEngineProcedure)(ForgeEngineCall defaultEngineProc, ForgeAudioEngine *audio, float *output, void *user);

FORGE_AUDIO_API void forge_audio_set_engine_procedure(
    ForgeAudioEngine *audio,
    ForgeEngineProcedure clientEngineProc,
    void *user
);


/* I/O API */

#define FORGE_AUDIO_SEEK_SET 0
#define FORGE_AUDIO_SEEK_CUR 1
#define FORGE_AUDIO_SEEK_END 2
#define FORGE_AUDIO_EOF -1

typedef size_t (FORGE_AUDIO_CALL * ForgeReadFunc)(
    void *data,
    void *dst,
    size_t size,
    size_t count
);
typedef int64_t (FORGE_AUDIO_CALL * ForgeSeekFunc)(
    void *data,
    int64_t offset,
    int whence
);
typedef int (FORGE_AUDIO_CALL * ForgeCloseFunc)(
    void *data
);

typedef struct ForgeIOStream
{
    void *data;
    ForgeReadFunc read;
    ForgeSeekFunc seek;
    ForgeCloseFunc close;
    void *lock;
} ForgeIOStream;

FORGE_AUDIO_API ForgeIOStream* forge_audio_fopen(const char *path);
FORGE_AUDIO_API ForgeIOStream* forge_audio_memopen(void *mem, int len);
FORGE_AUDIO_API uint8_t* forge_audio_memptr(ForgeIOStream *io, size_t offset);
FORGE_AUDIO_API void forge_audio_close(ForgeIOStream *io);

/* XNA Song */

FORGE_AUDIO_API void XNA_SongInit();
FORGE_AUDIO_API void XNA_SongQuit();
FORGE_AUDIO_API float XNA_PlaySong(const char *name);
FORGE_AUDIO_API void XNA_PauseSong();
FORGE_AUDIO_API void XNA_ResumeSong();
FORGE_AUDIO_API void XNA_StopSong();
FORGE_AUDIO_API void XNA_SetSongVolume(float volume);
FORGE_AUDIO_API uint32_t XNA_GetSongEnded();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_AUDIO_H */
