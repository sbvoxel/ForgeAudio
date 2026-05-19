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

#ifndef FORGE_AUDIO_H
#define FORGE_AUDIO_H

#ifdef _WIN32
#define FORGE_AUDIO_API __declspec(dllexport)
#define FORGE_AUDIO_CALL __cdecl
#elif defined(__GNUC__) || defined(__clang__)
#define FORGE_AUDIO_API __attribute__((visibility("default")))
#define FORGE_AUDIO_CALL
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
#include <forge/result.h>

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
typedef uint32_t ForgeAudioBatchId;

/* Enumerations */

typedef enum ForgeDeviceRole {
    ForgeDeviceRoleNone = 0x0,
    ForgeDeviceRoleConsole = 0x1,
    ForgeDeviceRoleMultimedia = 0x2,
    ForgeDeviceRoleCommunications = 0x4,
    ForgeDeviceRoleGame = 0x8,
    ForgeDeviceRoleDefault = 0xF,
    ForgeDeviceRoleInvalid = ~ForgeDeviceRoleDefault
} ForgeDeviceRole;

typedef enum ForgeFilterType {
    ForgeFilterLowPass,
    ForgeFilterBandPass,
    ForgeFilterHighPass,
    ForgeFilterNotch
} ForgeFilterType;

typedef enum ForgeAudioResamplerQuality {
    ForgeAudioResamplerLinear = 0,
    ForgeAudioResamplerCubic = 1,
    ForgeAudioResamplerSinc8 = 2,
    FORGE_AUDIO_RESAMPLER_LINEAR = ForgeAudioResamplerLinear,
    FORGE_AUDIO_RESAMPLER_CUBIC = ForgeAudioResamplerCubic,
    FORGE_AUDIO_RESAMPLER_SINC8 = ForgeAudioResamplerSinc8,
    FORGE_AUDIO_SOURCE_RESAMPLER_LINEAR = ForgeAudioResamplerLinear,
    FORGE_AUDIO_SOURCE_RESAMPLER_CUBIC = ForgeAudioResamplerCubic,
    FORGE_AUDIO_SOURCE_RESAMPLER_SINC8 = ForgeAudioResamplerSinc8
} ForgeAudioResamplerQuality;
typedef ForgeAudioResamplerQuality ForgeAudioSourceResamplerQuality;

/* Structures */

#pragma pack(push, 1)

typedef struct ForgeAudioFormat {
    uint16_t format_tag;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t average_bytes_per_second;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t extra_size;
} ForgeAudioFormat;

typedef struct ForgeAudioFormatExtensible {
    ForgeAudioFormat format;
    union {
        uint16_t valid_bits_per_sample;
        uint16_t samples_per_block;
        uint16_t reserved;
    } samples;
    uint32_t channel_mask;
    uint8_t format_id[16];
} ForgeAudioFormatExtensible;

typedef struct ForgeDeviceDetails {
    int16_t device_id[256];    /* Win32 wchar_t */
    int16_t display_name[256]; /* Win32 wchar_t */
    ForgeDeviceRole role;
    ForgeAudioFormatExtensible output_format;
} ForgeDeviceDetails;

typedef struct ForgeVoiceDetails {
    uint32_t creation_flags;
    uint32_t active_flags;
    uint32_t input_channels;
    uint32_t input_sample_rate;
} ForgeVoiceDetails;

typedef struct ForgeSend {
    uint32_t flags; /* 0 or FORGE_AUDIO_SEND_USEFILTER */
    ForgeVoice *output_voice;
} ForgeSend;

typedef struct ForgeSendList {
    uint32_t send_count;
    ForgeSend *sends;
} ForgeSendList;

#ifndef FORGE_EFFECT_DECL
#define FORGE_EFFECT_DECL
typedef struct ForgeEffect ForgeEffect;
#endif /* FORGE_EFFECT_DECL */

typedef struct ForgeEffectDesc {
    ForgeEffect *effect;
    int32_t initial_state; /* 1 - Enabled, 0 - Disabled */
    uint32_t output_channels;
} ForgeEffectDesc;

typedef struct ForgeEffectChain {
    uint32_t effect_count;
    ForgeEffectDesc *effects;
} ForgeEffectChain;

typedef struct ForgeFilterParameters {
    ForgeFilterType type;
    float cutoff_hz;   /* [0, voice-specific stable maximum], see forge_voice_get_filter_cutoff_range */
    float q;           /* [FORGE_AUDIO_MIN_FILTER_Q, FORGE_AUDIO_MAX_FILTER_Q] */
    float wet_dry_mix; /* [0, 1] */
} ForgeFilterParameters;

#define FORGE_FILTER_TARGET_CUTOFF_HZ 0x00000001u
#define FORGE_FILTER_TARGET_Q 0x00000002u
#define FORGE_FILTER_TARGET_WET_DRY_MIX 0x00000004u
#define FORGE_FILTER_TARGET_ALL                                                                                       \
    (FORGE_FILTER_TARGET_CUTOFF_HZ | FORGE_FILTER_TARGET_Q | FORGE_FILTER_TARGET_WET_DRY_MIX)

typedef struct ForgeFilterTarget {
    uint32_t field_mask; /* Mix of FORGE_FILTER_TARGET_* flags. */
    float cutoff_hz;   /* [0, voice-specific stable maximum], see forge_voice_get_filter_cutoff_range */
    float q;           /* [FORGE_AUDIO_MIN_FILTER_Q, FORGE_AUDIO_MAX_FILTER_Q] */
    float wet_dry_mix; /* [0, 1] */
} ForgeFilterTarget;

typedef struct ForgeBuffer {
    /* Either 0 or FORGE_AUDIO_END_OF_STREAM */
    uint32_t flags;
    /* Pointer to wave data, memory block size.
     * Note that audio_data is not copied; ForgeAudio reads directly from your
     * pointer! This pointer must be valid until ForgeAudio has finished using
     * it, at which point an on_buffer_end callback will be generated.
     */
    uint32_t audio_bytes;
    const uint8_t *audio_data;
    /* Play region, in sample frames. */
    uint32_t play_begin;
    uint32_t play_length;
    /* Loop region, in sample frames.
     * This can be used to loop a subregion of the wave instead of looping
     * the whole thing, i.e. if you have an intro/outro you can set these
     * to loop the middle sections instead. If you don't need this, set both
     * values to 0.
     */
    uint32_t loop_begin;
    uint32_t loop_length;
    /* [0, FORGE_AUDIO_LOOP_INFINITE] */
    uint32_t loop_count;
    /* This is sent to callbacks as buffer_context */
    void *context;
} ForgeBuffer;

typedef struct ForgeVoiceState {
    void *current_buffer_context;
    uint32_t buffers_queued;
    uint64_t samples_played;
} ForgeVoiceState;

typedef struct ForgePerformanceData {
    uint64_t audio_cycles_since_last_query;
    uint64_t total_cycles_since_last_query;
    uint32_t minimum_cycles_per_quantum;
    uint32_t maximum_cycles_per_quantum;
    uint32_t memory_usage_in_bytes;
    uint32_t current_latency_in_samples;
    uint32_t glitches_since_engine_started;
    uint32_t active_source_voice_count;
    uint32_t total_source_voice_count;
    uint32_t active_submix_voice_count;
    uint32_t active_resampler_count;
    uint32_t active_matrix_mix_count;
} ForgePerformanceData;

typedef struct ForgeDebugConfiguration {
    /* See FORGE_AUDIO_LOG_* */
    uint32_t trace_mask;
    uint32_t break_mask;
    /* 0 or 1 */
    int32_t log_thread_id;
    int32_t log_fileline;
    int32_t log_function_name;
    int32_t log_timing;
} ForgeDebugConfiguration;

#pragma pack(pop)

/* Constants */

#define FORGE_AUDIO_MAX_BUFFER_BYTES 0x80000000
#define FORGE_AUDIO_MAX_QUEUED_BUFFERS 64
#define FORGE_AUDIO_MAX_AUDIO_CHANNELS 64
#define FORGE_AUDIO_MIN_SAMPLE_RATE 1000
#define FORGE_AUDIO_MAX_SAMPLE_RATE 200000
#define FORGE_AUDIO_MAX_VOLUME_LEVEL 16777216.0f
#define FORGE_AUDIO_MIN_FREQ_RATIO (1.0f / 1024.0f)
#define FORGE_AUDIO_MAX_FREQ_RATIO 1024.0f
#define FORGE_AUDIO_DEFAULT_FREQ_RATIO 2.0f
#define FORGE_AUDIO_MIN_FILTER_Q 0.6666667f
#define FORGE_AUDIO_MAX_FILTER_Q 1000.0f
#define FORGE_AUDIO_MAX_LOOP_COUNT 254

/* Batch ids for calls that can be deferred.
 * FORGE_AUDIO_BATCH_IMMEDIATE applies a call immediately.
 * Values from 1 to UINT32_MAX - 1 are caller-chosen deferred batch ids.
 * FORGE_AUDIO_BATCH_ALL is only valid for forge_audio_apply_batch.
 */
#define FORGE_AUDIO_BATCH_IMMEDIATE ((ForgeAudioBatchId)0)
#define FORGE_AUDIO_BATCH_ALL ((ForgeAudioBatchId)UINT32_MAX)
#define FORGE_AUDIO_NO_LOOP_REGION 0
#define FORGE_AUDIO_LOOP_INFINITE 255
#define FORGE_AUDIO_DEFAULT_CHANNELS 0
#define FORGE_AUDIO_DEFAULT_SAMPLERATE 0

#define FORGE_AUDIO_DEBUG_ENGINE 0x0001
#define FORGE_AUDIO_VOICE_NOPITCH 0x0002
#define FORGE_AUDIO_VOICE_NOSRC 0x0004
#define FORGE_AUDIO_VOICE_USEFILTER 0x0008
#define FORGE_AUDIO_VOICE_MUSIC 0x0010
#define FORGE_AUDIO_PLAY_TAILS 0x0020
#define FORGE_AUDIO_END_OF_STREAM 0x0040
#define FORGE_AUDIO_SEND_USEFILTER 0x0080
#define FORGE_AUDIO_VOICE_NOSAMPLESPLAYED 0x0100
#define FORGE_AUDIO_1024_QUANTUM 0x8000

#define FORGE_AUDIO_DEFAULT_FILTER_TYPE ForgeFilterLowPass
#define FORGE_AUDIO_DEFAULT_FILTER_CUTOFF_HZ 20000.0f
#define FORGE_AUDIO_DEFAULT_FILTER_Q 1.0f
#define FORGE_AUDIO_DEFAULT_FILTER_WET_DRY_MIX 1.0f

#define FORGE_AUDIO_LOG_ERRORS 0x0001
#define FORGE_AUDIO_LOG_WARNINGS 0x0002
#define FORGE_AUDIO_LOG_INFO 0x0004
#define FORGE_AUDIO_LOG_DETAIL 0x0008
#define FORGE_AUDIO_LOG_API_CALLS 0x0010
#define FORGE_AUDIO_LOG_FUNC_CALLS 0x0020
#define FORGE_AUDIO_LOG_TIMING 0x0040
#define FORGE_AUDIO_LOG_LOCKS 0x0080
#define FORGE_AUDIO_LOG_MEMORY 0x0100
#define FORGE_AUDIO_LOG_STREAMING 0x1000

#ifndef FORGE_SPEAKER_POSITIONS_DEFINED
#define FORGE_SPEAKER_FRONT_LEFT 0x00000001
#define FORGE_SPEAKER_FRONT_RIGHT 0x00000002
#define FORGE_SPEAKER_FRONT_CENTER 0x00000004
#define FORGE_SPEAKER_LOW_FREQUENCY 0x00000008
#define FORGE_SPEAKER_BACK_LEFT 0x00000010
#define FORGE_SPEAKER_BACK_RIGHT 0x00000020
#define FORGE_SPEAKER_FRONT_LEFT_OF_CENTER 0x00000040
#define FORGE_SPEAKER_FRONT_RIGHT_OF_CENTER 0x00000080
#define FORGE_SPEAKER_BACK_CENTER 0x00000100
#define FORGE_SPEAKER_SIDE_LEFT 0x00000200
#define FORGE_SPEAKER_SIDE_RIGHT 0x00000400
#define FORGE_SPEAKER_TOP_CENTER 0x00000800
#define FORGE_SPEAKER_TOP_FRONT_LEFT 0x00001000
#define FORGE_SPEAKER_TOP_FRONT_CENTER 0x00002000
#define FORGE_SPEAKER_TOP_FRONT_RIGHT 0x00004000
#define FORGE_SPEAKER_TOP_BACK_LEFT 0x00008000
#define FORGE_SPEAKER_TOP_BACK_CENTER 0x00010000
#define FORGE_SPEAKER_TOP_BACK_RIGHT 0x00020000
#define FORGE_SPEAKER_POSITIONS_DEFINED
#endif

#ifndef FORGE_SPEAKER_MONO
#define FORGE_SPEAKER_MONO FORGE_SPEAKER_FRONT_CENTER
#define FORGE_SPEAKER_STEREO (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT)
#define FORGE_SPEAKER_2POINT1 (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_LOW_FREQUENCY)
#define FORGE_SPEAKER_SURROUND                                                                                         \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_FRONT_CENTER | FORGE_SPEAKER_BACK_CENTER)
#define FORGE_SPEAKER_QUAD                                                                                             \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_BACK_LEFT | FORGE_SPEAKER_BACK_RIGHT)
#define FORGE_SPEAKER_4POINT1                                                                                          \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_LOW_FREQUENCY | FORGE_SPEAKER_BACK_LEFT |    \
     FORGE_SPEAKER_BACK_RIGHT)
#define FORGE_SPEAKER_5POINT1                                                                                          \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_FRONT_CENTER | FORGE_SPEAKER_LOW_FREQUENCY | \
     FORGE_SPEAKER_BACK_LEFT | FORGE_SPEAKER_BACK_RIGHT)
#define FORGE_SPEAKER_7POINT1                                                                                          \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_FRONT_CENTER | FORGE_SPEAKER_LOW_FREQUENCY | \
     FORGE_SPEAKER_BACK_LEFT | FORGE_SPEAKER_BACK_RIGHT | FORGE_SPEAKER_FRONT_LEFT_OF_CENTER |                         \
     FORGE_SPEAKER_FRONT_RIGHT_OF_CENTER)
#define FORGE_SPEAKER_5POINT1_SURROUND                                                                                 \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_FRONT_CENTER | FORGE_SPEAKER_LOW_FREQUENCY | \
     FORGE_SPEAKER_SIDE_LEFT | FORGE_SPEAKER_SIDE_RIGHT)
#define FORGE_SPEAKER_7POINT1_SURROUND                                                                                 \
    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT | FORGE_SPEAKER_FRONT_CENTER | FORGE_SPEAKER_LOW_FREQUENCY | \
     FORGE_SPEAKER_BACK_LEFT | FORGE_SPEAKER_BACK_RIGHT | FORGE_SPEAKER_SIDE_LEFT | FORGE_SPEAKER_SIDE_RIGHT)
#define FORGE_SPEAKER_XBOX FORGE_SPEAKER_5POINT1
#endif

#define FORGE_AUDIO_FORMAT_PCM 1
#define FORGE_AUDIO_FORMAT_IEEE_FLOAT 3
#define FORGE_AUDIO_FORMAT_EXTENSIBLE 0xFFFE

/* Version API */

#define FORGE_AUDIO_ABI_VERSION 3
#define FORGE_AUDIO_MAJOR_VERSION 0
#define FORGE_AUDIO_MINOR_VERSION 1
#define FORGE_AUDIO_PATCH_VERSION 0

#define FORGE_AUDIO_COMPILED_VERSION                                                                                   \
    ((FORGE_AUDIO_ABI_VERSION * 100 * 100 * 100) + (FORGE_AUDIO_MAJOR_VERSION * 100 * 100) +                           \
     (FORGE_AUDIO_MINOR_VERSION * 100) + (FORGE_AUDIO_PATCH_VERSION))

FORGE_AUDIO_API uint32_t forge_audio_linked_version(void);

/* Engine Interface */

/* This should be your first ForgeAudio call.
 *
 * engine:        Filled with the audio engine context.
 * flags:        Can be 0 or a combination of FORGE_AUDIO_DEBUG_ENGINE and FORGE_AUDIO_1024_QUANTUM.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_create(ForgeAudioEngine **engine, uint32_t flags);

/* Destroys the audio engine and all voices still owned by it. */
FORGE_AUDIO_API void forge_audio_destroy(ForgeAudioEngine *audio);

/* Queries the number of sound devices available for use.
 *
 * count: Filled with the number of available sound devices.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_get_device_count(ForgeAudioEngine *audio, uint32_t *count);

/* Gets basic information about a sound device.
 *
 * index:        Can be between 0 and the result of forge_audio_get_device_count.
 * device_details:    Filled with the device information.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_get_device_details(ForgeAudioEngine *audio, uint32_t index,
                                                           ForgeDeviceDetails *device_details);

/* Register a new set of engine callbacks.
 * There is no limit to the number of sets, but expect performance to degrade
 * if you have a whole bunch of these. You most likely only need one.
 *
 * callback: The completely-initialized ForgeEngineCallback structure.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_register_callback(ForgeAudioEngine *audio, ForgeEngineCallback *callback);

/* Remove an active set of engine callbacks.
 * This checks the pointer value, NOT the callback values!
 *
 * callback: An ForgeEngineCallback structure previously sent to Register.
 */
FORGE_AUDIO_API void forge_audio_unregister_callback(ForgeAudioEngine *audio, ForgeEngineCallback *callback);

/* Creates a "source" voice, used to play back wavedata.
 *
 * source_voice:    Filled with the source voice pointer.
 * source_format:    The input wavedata format, see the documentation for
 *            ForgeAudioFormat.
 * flags:        Can be 0 or a mix of the following FORGE_AUDIO_VOICE_* flags:
 *            NOPITCH/NOSRC:    Resampling is disabled. If you set this,
 *                    the source format sample rate MUST match
 *                    the output voices' input sample rates.
 *                    Also, forge_source_voice_set_rate will fail.
 *            USEFILTER:    Enables the use of forge_voice_set_filter_parameters.
 *            MUSIC:        Unsupported.
 * max_frequency_ratio:    AKA your max pitch. This allows us to optimize the size
 *            of the decode/resample cache sizes. For example, if you
 *            only expect to raise pitch by a single octave, you can
 *            set this value to 2.0f. 2.0f is the default value.
 *            Bounds: [FORGE_AUDIO_MIN_FREQ_RATIO, FORGE_AUDIO_MAX_FREQ_RATIO].
 * callback:        Voice callbacks, see ForgeVoiceCallback documentation.
 * send_list:        List of output voices. If NULL, defaults to master.
 *            All output voices must have the same sample rate!
 * effect_chain:    List of caller-owned ForgeEffect effects. This value can be NULL.
 *            On success, ownership of every ForgeEffect in the chain is transferred
 *            to the voice. On failure, ownership remains with the caller.
 *            Sharing one ForgeEffect object across multiple voices is unsupported.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_create_source_voice(ForgeAudioEngine *audio, ForgeSourceVoice **source_voice,
                                                            const ForgeAudioFormat *source_format, uint32_t flags,
                                                            float max_frequency_ratio, ForgeVoiceCallback *callback,
                                                            const ForgeSendList *send_list,
                                                            const ForgeEffectChain *effect_chain);

/* Creates a "submix" voice, used to mix/process input voices.
 * The typical use case for this is to perform CPU-intensive tasks on large
 * groups of voices all at once. Examples include resampling and ForgeEffect effects.
 *
 * submix_voice:    Filled with the submix voice pointer.
 * input_channels:    Input voices will convert to this channel count.
 * input_sample_rate:    Input voices will convert to this sample rate.
 * flags:        Can be 0 or FORGE_AUDIO_VOICE_USEFILTER.
 * processing_stage:    If you have multiple submixes that depend on a specific
 *            order of processing, you can sort them by setting this
 *            value to prioritize them. For example, submixes with
 *            stage 0 will process first, then stage 1, 2, and so on.
 * send_list:        List of output voices. If NULL, defaults to master.
 *            All output voices must have the same sample rate!
 * effect_chain:    List of caller-owned ForgeEffect effects. This value can be NULL.
 *            On success, ownership of every ForgeEffect in the chain is transferred
 *            to the voice. On failure, ownership remains with the caller.
 *            Sharing one ForgeEffect object across multiple voices is unsupported.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_create_submix_voice(ForgeAudioEngine *audio, ForgeSubmixVoice **submix_voice,
                                                            uint32_t input_channels, uint32_t input_sample_rate,
                                                            uint32_t flags, uint32_t processing_stage,
                                                            const ForgeSendList *send_list,
                                                            const ForgeEffectChain *effect_chain);

/* This should be your second ForgeAudio call, unless you care about which device
 * you want to use. In that case, see forge_audio_get_device_details.
 *
 * mastering_voice:    Filled with the mastering voice pointer.
 * input_channels:    Device channel count. Can be FORGE_AUDIO_DEFAULT_CHANNELS.
 * input_sample_rate:    Device sample rate. Can be FORGE_AUDIO_DEFAULT_SAMPLERATE.
 * flags:        This value must be 0.
 * device_index:        0 for the default device. See forge_audio_get_device_count.
 * effect_chain:    List of caller-owned ForgeEffect effects. This value can be NULL.
 *            On success, ownership of every ForgeEffect in the chain is transferred
 *            to the voice. On failure, ownership remains with the caller.
 *            Sharing one ForgeEffect object across multiple voices is unsupported.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_create_master_voice(ForgeAudioEngine *audio, ForgeMasterVoice **mastering_voice,
                                                            uint32_t input_channels, uint32_t input_sample_rate,
                                                            uint32_t flags, uint32_t device_index,
                                                            const ForgeEffectChain *effect_chain);

/* Starts the engine, begins processing the audio graph.
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_start_engine(ForgeAudioEngine *audio);

/* Stops the engine and halts all processing.
 * The audio device will continue to run, but will produce silence.
 * The graph will be frozen until you call forge_audio_start_engine, where it will then
 * resume all processing exactly as it would have had this never been called.
 */
FORGE_AUDIO_API void forge_audio_stop_engine(ForgeAudioEngine *audio);

/* Makes a deferred batch ready to run at the start of the next processing pass.
 * For example, if you want to start two sources in the same processing pass, call
 * forge_source_voice_start with the same deferred batch_id for both voices, then
 * call forge_audio_apply_batch with that batch_id.
 *
 * batch_id: A caller-chosen batch id, or FORGE_AUDIO_BATCH_ALL to apply every
 *        pending batch. FORGE_AUDIO_BATCH_IMMEDIATE is invalid here.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_audio_apply_batch(ForgeAudioEngine *audio, ForgeAudioBatchId batch_id);

/* Requests various bits of performance information from the engine.
 *
 * perf_data: Filled with the data. See ForgePerformanceData for details.
 */
FORGE_AUDIO_API void forge_audio_get_performance_data(ForgeAudioEngine *audio, ForgePerformanceData *perf_data);

/* When using a Debug binary, this lets you configure what information gets
 * logged to output. Be careful, this can spit out a LOT of text.
 *
 * debug_configuration:    See ForgeDebugConfiguration for details.
 * reserved:        Set this to NULL.
 */
FORGE_AUDIO_API void forge_audio_set_debug_configuration(ForgeAudioEngine *audio,
                                                         ForgeDebugConfiguration *debug_configuration, void *reserved);

/* Requests the values that determine's the engine's update size.
 * For example, a 48KHz engine with a 1024-sample update period would return
 * 1024 for the numerator and 48000 for the denominator. With this information,
 * you can determine the precise update size in milliseconds.
 *
 * quantumNumerator - The engine's update size, in sample frames.
 * quantumDenominator - The engine's sample rate, in Hz
 */
FORGE_AUDIO_API void forge_audio_get_processing_quantum(ForgeAudioEngine *audio, uint32_t *quantumNumerator,
                                                        uint32_t *quantumDenominator);

/* Converts a duration in milliseconds to engine output sample frames.
 *
 * duration_ms: Duration in milliseconds. 0.0 converts to 0 frames. Positive
 *              finite values round to the nearest frame, and positive values
 *              that round to 0 are clamped to 1 frame.
 * frames:      Filled with the converted frame count.
 *
 * Returns ForgeResultSuccess on success. Returns ForgeResultInvalidCall for
 * NULL frames, negative/NaN/infinite durations, unavailable sample rate, or
 * overflow.
 */
FORGE_AUDIO_API ForgeResult forge_audio_ms_to_frames(ForgeAudioEngine *audio, double duration_ms, uint32_t *frames);

/* ForgeVoice Interface */

/* Requests basic information about a voice.
 *
 * voice_details: See ForgeVoiceDetails for details.
 */
FORGE_AUDIO_API void forge_voice_get_details(ForgeVoice *voice, ForgeVoiceDetails *voice_details);

/* Change the output voices for this voice.
 * This function is invalid for mastering voices.
 *
 * send_list:    List of output voices. If NULL, defaults to master.
 *        All output voices must have the same sample rate!
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_outputs(ForgeVoice *voice, const ForgeSendList *send_list);

/* Change/Remove the effect chain for this voice.
 *
 * effect_chain:    List of caller-owned ForgeEffect effects. This value can be NULL.
 *            Note that the final channel counts for this chain MUST
 *            match the input/output channel count that was
 *            determined at voice creation time!
 *            On success, ownership of every ForgeEffect in the new chain is
 *            transferred to the voice. On failure, ownership remains with the
 *            caller and the current chain is unchanged. Passing NULL destroys
 *            the current chain. Sharing one ForgeEffect object across multiple
 *            voices is unsupported.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_effect_chain(ForgeVoice *voice, const ForgeEffectChain *effect_chain);

/* Sets the interpolation quality used when this source or submix voice resamples.
 * Master voices do not resample and return ForgeResultInvalidCall.
 *
 * ForgeAudioResamplerCubic is the default. It is Catmull-Rom interpolation:
 * smoother than linear, but not a bandlimited anti-aliasing sample-rate converter.
 * ForgeAudioResamplerLinear is available as a lower-CPU fast path.
 * ForgeAudioResamplerSinc8 is an 8-tap tabled windowed-sinc source resampler
 * intended as an opt-in higher-quality mode. It is not currently supported for
 * submix voices.
 *
 * The change applies immediately and may slightly change the rendered waveform
 * if the voice is already active.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_resampler_quality(ForgeVoice *voice,
                                                              ForgeAudioResamplerQuality quality);

/* Gets the interpolation quality used when this source or submix voice resamples.
 * Master voices do not resample and return ForgeResultInvalidCall.
 */
FORGE_AUDIO_API ForgeResult forge_voice_get_resampler_quality(ForgeVoice *voice,
                                                              ForgeAudioResamplerQuality *quality);

/* Enables an effect in the effect chain.
 *
 * effect_index:        The index of the effect (based on the chain order).
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_enable_effect(ForgeVoice *voice, uint32_t effect_index,
                                                      ForgeAudioBatchId batch_id);

/* Disables an effect in the effect chain.
 *
 * effect_index:        The index of the effect (based on the chain order).
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_disable_effect(ForgeVoice *voice, uint32_t effect_index,
                                                       ForgeAudioBatchId batch_id);

/* Queries the enabled/disabled state of an effect in the effect chain.
 *
 * effect_index:    The index of the effect (based on the chain order).
 * enabled:    Filled with either 1 (Enabled) or 0 (Disabled).
 */
FORGE_AUDIO_API void forge_voice_get_effect_state(ForgeVoice *voice, uint32_t effect_index, int32_t *enabled);

/* Submits a block of memory to be sent to ForgeEffect::set_parameters.
 * This is a hard parameter set. For built-in effects with typed automation,
 * setting the blob cancels active typed automation for that effect when the
 * set is applied on the audio timeline.
 *
 * effect_index:        The index of the effect (based on the chain order).
 * parameters:        The values to be copied and submitted to the ForgeEffect.
 * parameters_byte_size:    This should match what the ForgeEffect expects!
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_effect_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                              const void *parameters, uint32_t parameters_byte_size,
                                                              ForgeAudioBatchId batch_id);

/* Requests the latest parameters from ForgeEffect::get_parameters.
 *
 * effect_index:        The index of the effect (based on the chain order).
 * parameters:        Filled with the latest parameter values from the ForgeEffect.
 * parameters_byte_size:    This should match what the ForgeEffect expects!
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_get_effect_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                              void *parameters, uint32_t parameters_byte_size);

/* Sets the filter variables for a voice.
 * This is only valid on voices with the USEFILTER flag.
 *
 * parameters:        See ForgeFilterParameters for details.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_filter_parameters(ForgeVoice *voice,
                                                              const ForgeFilterParameters *parameters,
                                                              ForgeAudioBatchId batch_id);

/* Sets only the discrete filter type.
 * Any active cutoff/Q/wet-dry automation is preserved. The type switch itself is
 * immediate at the batch boundary and may click for some signals.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_filter_type(ForgeVoice *voice, ForgeFilterType type,
                                                        ForgeAudioBatchId batch_id);

/* Targets selected continuous filter variables using ForgeAudio's internal default de-zip duration.
 * This does not change the filter type.
 * target->field_mask chooses which fields are affected; omitted fields keep
 * their current value and any active automation already controlling them.
 *
 * The current implementation advances cutoff_hz, q, and wet_dry_mix once per
 * rendered frame. The exact cutoff interpolation curve is intentionally not a
 * long-term API promise while ForgeAudio is pre-1.0; it may become explicitly
 * linear/log/curve-selectable later.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_filter_target(ForgeVoice *voice, const ForgeFilterTarget *target,
                                                          ForgeAudioBatchId batch_id);

/* Ramps selected continuous filter variables over an exact number of rendered sample frames.
 * This does not change the filter type. See forge_voice_set_filter_target for
 * the current interpolation policy and future curve note.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_filter_frames(ForgeVoice *voice, const ForgeFilterTarget *target,
                                                           uint32_t duration_frames,
                                                           ForgeAudioBatchId batch_id);

/* Ramps selected continuous filter variables over a duration in milliseconds.
 * duration_ms is converted to engine output sample frames using
 * forge_audio_ms_to_frames when this function is called.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_filter_ms(ForgeVoice *voice, const ForgeFilterTarget *target,
                                                       double duration_ms, ForgeAudioBatchId batch_id);

/* Requests the filter variables for a voice.
 * This is only valid on voices with the USEFILTER flag.
 *
 * parameters: Filled with the current effective clamped filter values. During
 *             an active ramp, these are the latest rendered timeline values,
 *             not the final target values.
 */
FORGE_AUDIO_API void forge_voice_get_filter_parameters(ForgeVoice *voice, ForgeFilterParameters *parameters);

/* Gets the stable cutoff range for a voice filter, in Hz.
 * The maximum depends on the sample rate at which the voice filter runs.
 */
FORGE_AUDIO_API ForgeResult forge_voice_get_filter_cutoff_range(ForgeVoice *voice, float *min_cutoff_hz,
                                                                float *max_cutoff_hz);

/* Sets the filter variables for a voice's output voice.
 * This is only valid on sends with the USEFILTER flag.
 *
 * destination_voice:    An output voice from the voice's send list.
 * parameters:        See ForgeFilterParameters for details.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                                     const ForgeFilterParameters *parameters,
                                                                     ForgeAudioBatchId batch_id);

/* Sets only a send filter's discrete type. Active cutoff/Q/wet-dry automation is preserved. */
FORGE_AUDIO_API ForgeResult forge_voice_set_output_filter_type(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                               ForgeFilterType type,
                                                               ForgeAudioBatchId batch_id);

/* Targets selected send filter continuous variables using ForgeAudio's internal default de-zip duration.
 * This does not change the send filter type. See forge_voice_set_filter_target
 * for the current interpolation policy and future curve note.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_output_filter_target(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                                 const ForgeFilterTarget *target,
                                                                 ForgeAudioBatchId batch_id);

/* Ramps selected send filter continuous variables over an exact number of rendered sample frames. */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_output_filter_frames(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                                  const ForgeFilterTarget *target,
                                                                  uint32_t duration_frames,
                                                                  ForgeAudioBatchId batch_id);

/* Ramps selected send filter continuous variables over a duration in milliseconds. */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_output_filter_ms(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                              const ForgeFilterTarget *target,
                                                              double duration_ms,
                                                              ForgeAudioBatchId batch_id);

/* Requests the filter variables for a voice's output voice.
 * This is only valid on sends with the USEFILTER flag.
 *
 * destination_voice:    An output voice from the voice's send list.
 * parameters:        Filled with the current effective clamped filter values.
 */
FORGE_AUDIO_API void forge_voice_get_output_filter_parameters(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                              ForgeFilterParameters *parameters);

/* Gets the stable cutoff range for a send filter, in Hz.
 * The maximum depends on the sample rate at which the send filter runs.
 */
FORGE_AUDIO_API ForgeResult forge_voice_get_output_filter_cutoff_range(ForgeVoice *voice,
                                                                       ForgeVoice *destination_voice,
                                                                       float *min_cutoff_hz,
                                                                       float *max_cutoff_hz);

/* Sets the global volume of a voice.
 *
 * volume:        Amplitude ratio. 1.0f is default, 0.0f is silence.
 *            Note that you can actually set volume < 0.0f!
 *            Bounds: [-FORGE_AUDIO_MAX_VOLUME_LEVEL, FORGE_AUDIO_MAX_VOLUME_LEVEL]
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_volume(ForgeVoice *voice, float volume, ForgeAudioBatchId batch_id);

/* Targets the global volume of a voice using ForgeAudio's internal default de-zip duration.
 *
 * volume:        Target amplitude ratio. Same bounds as forge_voice_set_volume.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_volume_target(ForgeVoice *voice, float volume,
                                                          ForgeAudioBatchId batch_id);

/* Ramps the global volume of a voice over an exact number of rendered sample frames.
 *
 * volume:        Target amplitude ratio. Same bounds as forge_voice_set_volume.
 * duration_frames: Number of output sample frames over which to reach the target.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_volume_frames(ForgeVoice *voice, float volume, uint32_t duration_frames,
                                                           ForgeAudioBatchId batch_id);

/* Ramps the global volume of a voice over a duration in milliseconds.
 *
 * duration_ms is converted to engine output sample frames using
 * forge_audio_ms_to_frames when this function is called.
 *
 * volume:        Target amplitude ratio. Same bounds as forge_voice_set_volume.
 * duration_ms:   Duration in milliseconds. See forge_audio_ms_to_frames.
 * batch_id:      Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *                the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_volume_ms(ForgeVoice *voice, float volume, double duration_ms,
                                                       ForgeAudioBatchId batch_id);

/* Requests the global volume of a voice.
 *
 * volume: Filled with the current voice amplitude ratio.
 */
FORGE_AUDIO_API void forge_voice_get_volume(ForgeVoice *voice, float *volume);

/* Sets the per-channel volumes of a voice.
 *
 * channels:        Must match the channel count of this voice!
 * volumes:        Amplitude ratios for each channel. Same as forge_voice_set_volume.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_channel_volumes(ForgeVoice *voice, uint32_t channels, const float *volumes,
                                                            ForgeAudioBatchId batch_id);

/* Targets the per-channel volumes of a voice using ForgeAudio's internal default de-zip duration.
 *
 * channels:        Must match the channel count of this voice!
 * volumes:        Target amplitude ratios for each channel. Same bounds as forge_voice_set_volume.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_channel_volumes_target(ForgeVoice *voice, uint32_t channels,
                                                                   const float *volumes,
                                                                   ForgeAudioBatchId batch_id);

/* Ramps the per-channel volumes of a voice over an exact number of rendered sample frames.
 *
 * channels:        Must match the channel count of this voice!
 * volumes:        Target amplitude ratios for each channel. Same bounds as forge_voice_set_volume.
 * duration_frames: Number of output sample frames over which to reach the targets.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_channel_volumes_frames(ForgeVoice *voice, uint32_t channels,
                                                                    const float *volumes, uint32_t duration_frames,
                                                                    ForgeAudioBatchId batch_id);

/* Ramps the per-channel volumes of a voice over a duration in milliseconds.
 *
 * duration_ms is converted to engine output sample frames using
 * forge_audio_ms_to_frames when this function is called.
 *
 * channels:      Must match the channel count of this voice!
 * volumes:       Target amplitude ratios for each channel. Same bounds as forge_voice_set_volume.
 * duration_ms:   Duration in milliseconds. See forge_audio_ms_to_frames.
 * batch_id:      Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *                the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_channel_volumes_ms(ForgeVoice *voice, uint32_t channels,
                                                                const float *volumes, double duration_ms,
                                                                ForgeAudioBatchId batch_id);

/* Requests the per-channel volumes of a voice.
 *
 * channels:    Must match the channel count of this voice!
 * volumes:    Filled with the current channel amplitude ratios.
 */
FORGE_AUDIO_API void forge_voice_get_channel_volumes(ForgeVoice *voice, uint32_t channels, float *volumes);

/* Sets the volumes of a send's output channels. The matrix is based on the
 * voice's rendered output channels. For example, the default matrix for a 2-channel source and a 2-channel output
 * voice is as follows:
 * [0] = 1.0f; <- Left input, left output
 * [1] = 0.0f; <- Right input, left output
 * [2] = 0.0f; <- Left input, right output
 * [3] = 1.0f; <- Right input, right output
 * This is typically only used for panning or 3D sound (via forge_spatializer_calculate).
 *
 * destination_voice:    An output voice from the voice's send list.
 * source_channels:    Must match the voice's rendered output channel count!
 * destination_channels:    Must match the destination's input channel count!
 * level_matrix:    A float[source_channels * destination_channels].
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                          uint32_t source_channels, uint32_t destination_channels,
                                                          const float *level_matrix, ForgeAudioBatchId batch_id);

/* Targets the volumes of a send's output channels using ForgeAudio's internal default de-zip duration.
 *
 * destination_voice:    An output voice from the voice's send list.
 * source_channels:    Must match the voice's rendered output channel count!
 * destination_channels:    Must match the destination's input channel count!
 * level_matrix:    Target matrix, as a float[source_channels * destination_channels].
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_output_matrix_target(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                                 uint32_t source_channels,
                                                                 uint32_t destination_channels,
                                                                 const float *level_matrix,
                                                                 ForgeAudioBatchId batch_id);

/* Ramps the volumes of a send's output channels over an exact number of rendered sample frames.
 *
 * destination_voice:    An output voice from the voice's send list.
 * source_channels:    Must match the voice's rendered output channel count!
 * destination_channels:    Must match the destination's input channel count!
 * level_matrix:    Target matrix, as a float[source_channels * destination_channels].
 * duration_frames: Number of output sample frames over which to reach the target matrix.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_output_matrix_frames(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                                  uint32_t source_channels,
                                                                  uint32_t destination_channels,
                                                                  const float *level_matrix,
                                                                  uint32_t duration_frames,
                                                                  ForgeAudioBatchId batch_id);

/* Ramps the volumes of a send's output channels over a duration in milliseconds.
 *
 * duration_ms is converted to engine output sample frames using
 * forge_audio_ms_to_frames when this function is called.
 *
 * destination_voice:    An output voice from the voice's send list.
 * source_channels:    Must match the voice's rendered output channel count!
 * destination_channels:    Must match the destination's input channel count!
 * level_matrix:    Target matrix, as a float[source_channels * destination_channels].
 * duration_ms: Duration in milliseconds. See forge_audio_ms_to_frames.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_output_matrix_ms(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                              uint32_t source_channels,
                                                              uint32_t destination_channels,
                                                              const float *level_matrix, double duration_ms,
                                                              ForgeAudioBatchId batch_id);

/* Gets the volumes of a send's output channels. See forge_voice_set_output_matrix.
 *
 * destination_voice:    An output voice from the voice's send list.
 * source_channels:    Must match the voice's rendered output channel count!
 * destination_channels:    Must match the destination's input channel count!
 * level_matrix:    A float[source_channels * destination_channels].
 */
FORGE_AUDIO_API void forge_voice_get_output_matrix(ForgeVoice *voice, ForgeVoice *destination_voice,
                                                   uint32_t source_channels, uint32_t destination_channels,
                                                   float *level_matrix);

/* Removes this voice from the audio graph and frees memory. */
FORGE_AUDIO_API void forge_voice_destroy(ForgeVoice *voice);

/* Returns ForgeResultSuccess on success or an error if the voice is still in use. */
FORGE_AUDIO_API ForgeResult forge_voice_try_destroy(ForgeVoice *voice);

/* ForgeSourceVoice Interface */

/* Starts processing for a source voice.
 *
 * flags:        Must be 0.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_start(ForgeSourceVoice *voice, uint32_t flags,
                                                     ForgeAudioBatchId batch_id);

/* Pauses processing for a source voice. Yes, I said pausing.
 * If you want to _actually_ stop, call forge_source_voice_flush_buffers next.
 *
 * flags:        Can be 0 or FORGE_AUDIO_PLAY_TAILS, which allows effects to
 *            keep emitting output even after processing has stopped.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_stop(ForgeSourceVoice *voice, uint32_t flags,
                                                    ForgeAudioBatchId batch_id);

/* Ramps the source voice volume, then stops the voice on the audio timeline.
 *
 * volume:        Target amplitude ratio. Same bounds as forge_voice_set_volume.
 * duration_frames: Number of output sample frames over which to reach the target before stopping.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_fade_stop_frames(ForgeSourceVoice *voice, float volume,
                                                                uint32_t duration_frames, ForgeAudioBatchId batch_id);

/* Ramps the source voice volume over a duration in milliseconds, then stops the
 * voice on the audio timeline.
 *
 * duration_ms is converted to engine output sample frames using
 * forge_audio_ms_to_frames when this function is called.
 *
 * volume:      Target amplitude ratio. Same bounds as forge_voice_set_volume.
 * duration_ms: Duration in milliseconds. See forge_audio_ms_to_frames.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply at the start of the next processing pass while
 *              the engine is active, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_fade_stop_ms(ForgeSourceVoice *voice, float volume, double duration_ms,
                                                            ForgeAudioBatchId batch_id);

/* Submits a block of wavedata for the source to process.
 *
 * buffer:    See ForgeBuffer for details.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_submit_buffer(ForgeSourceVoice *voice, const ForgeBuffer *buffer);

/* Removes all buffers from a source, with a minor exception.
 * If the voice is still playing, the active buffer is left alone.
 * All buffers that are removed will spawn an on_buffer_end callback.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_flush_buffers(ForgeSourceVoice *voice);

/* Takes the last buffer currently queued and sets the END_OF_STREAM flag.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_end_stream(ForgeSourceVoice *voice);

/* Sets the loop count of the active buffer to 0.
 *
 * batch_id: Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_break_loop(ForgeSourceVoice *voice, ForgeAudioBatchId batch_id);

/* Requests the state and some basic statistics for this source.
 *
 * voice_state:    See ForgeVoiceState for details.
 * flags:    Can be 0 or FORGE_AUDIO_VOICE_NOSAMPLESPLAYED.
 */
FORGE_AUDIO_API void forge_source_voice_get_state(ForgeSourceVoice *voice, ForgeVoiceState *voice_state,
                                                  uint32_t flags);

/* Sets the frequency ratio (fancy phrase for pitch) of this source.
 *
 * ratio:        The frequency ratio, must be <= max_frequency_ratio.
 * batch_id:    Use FORGE_AUDIO_BATCH_IMMEDIATE to apply immediately, or pass a valid deferred batch id to defer.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_set_rate(ForgeSourceVoice *voice, float ratio,
                                                        ForgeAudioBatchId batch_id);
FORGE_AUDIO_API ForgeResult forge_source_voice_set_rate_target(ForgeSourceVoice *voice, float ratio,
                                                               ForgeAudioBatchId batch_id);
FORGE_AUDIO_API ForgeResult forge_source_voice_ramp_rate_frames(ForgeSourceVoice *voice, float ratio,
                                                                uint32_t duration_frames,
                                                                ForgeAudioBatchId batch_id);
FORGE_AUDIO_API ForgeResult forge_source_voice_ramp_rate_ms(ForgeSourceVoice *voice, float ratio,
                                                            double duration_ms,
                                                            ForgeAudioBatchId batch_id);

/* Requests the frequency ratio (fancy phrase for pitch) of this source.
 *
 * ratio: Filled with the frequency ratio.
 */
FORGE_AUDIO_API void forge_source_voice_get_rate(ForgeSourceVoice *voice, float *ratio);

/* Resets the core sample rate of this source.
 * You probably don't want this, it's more likely you want forge_source_voice_set_rate.
 * This is used to recycle voices without having to constantly reallocate them.
 * For example, if you have wavedata that's all float32 mono, but the sample
 * rates are different, you can take a source that was being used for a 48KHz
 * wave and call this so it can be used for a 44.1KHz wave.
 *
 * new_source_sample_rate: The new sample rate for this source.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_source_voice_set_sample_rate(ForgeSourceVoice *voice,
                                                               uint32_t new_source_sample_rate);

/* ForgeMasterVoice Interface */

/* Requests the channel mask for the mastering voice.
 * This is typically used with forge_spatializer_init, but you may find it
 * interesting if you want to see the user's basic speaker layout.
 *
 * channel_mask: Filled with the channel mask.
 *
 * Returns ForgeResultSuccess on success.
 */
FORGE_AUDIO_API ForgeResult forge_master_voice_get_channel_mask(ForgeMasterVoice *voice, uint32_t *channel_mask);

/* ForgeEngineCallback Interface */

/* If something horrible happens, this will be called.
 *
 * error: The error code that spawned this callback.
 */
typedef void(FORGE_AUDIO_CALL *ForgeEngineCriticalErrorFunc)(ForgeEngineCallback *callback, ForgeResult error);

/* This is called at the end of a processing update. */
typedef void(FORGE_AUDIO_CALL *ForgeEngineProcessingPassEndFunc)(ForgeEngineCallback *callback);

/* This is called at the beginning of a processing update. */
typedef void(FORGE_AUDIO_CALL *ForgeEngineProcessingPassStartFunc)(ForgeEngineCallback *callback);

struct ForgeEngineCallback {
    ForgeEngineCriticalErrorFunc on_critical_error;
    ForgeEngineProcessingPassEndFunc on_processing_pass_end;
    ForgeEngineProcessingPassStartFunc on_processing_pass_start;
};

/* ForgeVoiceCallback Interface */

/* When a buffer is no longer in use, this is called.
 *
 * buffer_context: The context for the ForgeBuffer in question.
 */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceBufferEndFunc)(ForgeVoiceCallback *callback, void *buffer_context);

/* When a buffer is now being used, this is called.
 *
 * buffer_context: The context for the ForgeBuffer in question.
 */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceBufferStartFunc)(ForgeVoiceCallback *callback, void *buffer_context);

/* When a buffer completes a loop, this is called.
 *
 * buffer_context: The context for the ForgeBuffer in question.
 */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceLoopEndFunc)(ForgeVoiceCallback *callback, void *buffer_context);

/* When a buffer that has the END_OF_STREAM flag is finished, this is called. */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceStreamEndFunc)(ForgeVoiceCallback *callback);

/* If something horrible happens to a voice, this is called.
 *
 * buffer_context:    The context for the ForgeBuffer in question.
 * error:        The error code that spawned this callback.
 */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceErrorFunc)(ForgeVoiceCallback *callback, void *buffer_context,
                                                    ForgeResult error);

/* When this voice is done being processed, this is called. */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceProcessingPassEndFunc)(ForgeVoiceCallback *callback);

/* When a voice is about to start being processed, this is called.
 *
 * bytes_required:    The number of bytes needed from the application to
 *            complete a full update. For example, if we need 512
 *            frames for a whole update, and the voice is a float32
 *            stereo source, bytes_required will be 4096.
 */
typedef void(FORGE_AUDIO_CALL *ForgeVoiceProcessingPassStartFunc)(ForgeVoiceCallback *callback,
                                                                  uint32_t bytes_required);

struct ForgeVoiceCallback {
    ForgeVoiceBufferEndFunc on_buffer_end;
    ForgeVoiceBufferStartFunc on_buffer_start;
    ForgeVoiceLoopEndFunc on_loop_end;
    ForgeVoiceStreamEndFunc on_stream_end;
    ForgeVoiceErrorFunc on_voice_error;
    ForgeVoiceProcessingPassEndFunc on_voice_processing_pass_end;
    ForgeVoiceProcessingPassStartFunc on_voice_processing_pass_start;
};

/* Custom Allocator API */

typedef void *(FORGE_AUDIO_CALL *ForgeMallocFunc)(size_t size);
typedef void(FORGE_AUDIO_CALL *ForgeFreeFunc)(void *ptr);
typedef void *(FORGE_AUDIO_CALL *ForgeReallocFunc)(void *ptr, size_t size);

FORGE_AUDIO_API ForgeResult forge_audio_create_with_allocator(ForgeAudioEngine **engine, uint32_t flags,
                                                              ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                                              ForgeReallocFunc custom_realloc);

/* Engine Procedure API */
typedef void(FORGE_AUDIO_CALL *ForgeEngineCall)(ForgeAudioEngine *audio, float *output);
typedef void(FORGE_AUDIO_CALL *ForgeEngineProcedure)(ForgeEngineCall default_engine_proc, ForgeAudioEngine *audio,
                                                     float *output, void *user);

FORGE_AUDIO_API void forge_audio_set_engine_procedure(ForgeAudioEngine *audio, ForgeEngineProcedure client_engine_proc,
                                                      void *user);

/* I/O API */

#define FORGE_AUDIO_SEEK_SET 0
#define FORGE_AUDIO_SEEK_CUR 1
#define FORGE_AUDIO_SEEK_END 2
#define FORGE_AUDIO_EOF -1

typedef size_t(FORGE_AUDIO_CALL *ForgeReadFunc)(void *data, void *dst, size_t size, size_t count);
typedef int64_t(FORGE_AUDIO_CALL *ForgeSeekFunc)(void *data, int64_t offset, int whence);
typedef int(FORGE_AUDIO_CALL *ForgeCloseFunc)(void *data);

typedef struct ForgeIOStream {
    void *data;
    ForgeReadFunc read;
    ForgeSeekFunc seek;
    ForgeCloseFunc close;
    void *lock;
} ForgeIOStream;

FORGE_AUDIO_API ForgeIOStream *forge_audio_fopen(const char *path);
FORGE_AUDIO_API ForgeIOStream *forge_audio_memopen(void *mem, int len);
FORGE_AUDIO_API uint8_t *forge_audio_memptr(ForgeIOStream *io, size_t offset);
FORGE_AUDIO_API void forge_audio_close(ForgeIOStream *io);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_AUDIO_H */
