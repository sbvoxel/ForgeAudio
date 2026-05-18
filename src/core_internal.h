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

#ifndef FORGE_CORE_INTERNAL_H
#define FORGE_CORE_INTERNAL_H

#include "common_internal.h"
#include "platform_internal.h"
#include "util_internal.h"
#include "format_internal.h"
#include "debug_internal.h"
#include "effect_base_internal.h"

typedef enum ForgeAudioVoiceType {
    FORGE_AUDIO_VOICE_SOURCE,
    FORGE_AUDIO_VOICE_SUBMIX,
    FORGE_AUDIO_VOICE_MASTER
} ForgeAudioVoiceType;

struct queued_buffer {
    ForgeBuffer buffer;
    uint32_t loop_bytes, play_bytes;
    bool sent_OnStartBuffer;
};

typedef void(FORGE_AUDIO_CALL *ForgeAudioDecodeCallback)(ForgeVoice *voice, const void *src, float *decode_cache,
                                                         uint32_t samples);

typedef void(FORGE_AUDIO_CALL *ForgeAudioResampleCallback)(float *restrict d_cache, float *restrict resample_cache,
                                                           uint64_t *resample_offset, uint64_t resample_step,
                                                           uint64_t to_resample, uint8_t channels);

typedef void(FORGE_AUDIO_CALL *ForgeAudioMixCallback)(uint32_t to_mix, uint32_t src_chans, uint32_t dst_chans,
                                                      float *restrict src_data, float *restrict dst_data,
                                                      float *restrict coefficients);

typedef float ForgeAudioFilterState[4];

typedef struct ForgeFilterFieldAutomation {
    uint8_t active;
    float target;
    float step;
    uint32_t remainingFrames;
} ForgeFilterFieldAutomation;

typedef struct ForgeFilterRuntime {
    ForgeFilterType type;
    float cutoff_hz;
    float q;
    float wet_dry_mix;
    float frequency;
    float one_over_q;
    uint32_t sample_rate;
    struct {
        ForgeFilterFieldAutomation cutoff_hz;
        ForgeFilterFieldAutomation q;
        ForgeFilterFieldAutomation wet_dry_mix;
    } automation;
} ForgeFilterRuntime;

typedef struct ForgeVoiceSendRuntime {
    ForgeSend send;
    float *sendCoefficients;
    float *mixCoefficients;
    ForgeAudioMixCallback mix;
    ForgeFilterRuntime filter;
    ForgeAudioFilterState *filterState;
    struct {
        uint8_t active;
        float *target;
        float *step;
        uint32_t remainingFrames;
    } matrixAutomation;
} ForgeVoiceSendRuntime;

typedef struct ForgeVoiceSendRuntimeList {
    uint32_t send_count;
    ForgeVoiceSendRuntime *sends;
} ForgeVoiceSendRuntimeList;

typedef struct ForgeEffectChainRuntime {
    ForgeEffectBufferFlags state;
    uint32_t count;
    ForgeEffectDesc *desc;
    void **parameters;
    uint32_t *parameterSizes;
    uint8_t *parameterUpdates;
    uint8_t *inPlaceProcessing;
} ForgeEffectChainRuntime;

struct ForgeAudioEngine {
    uint8_t active;
    uint8_t platformLifetimeHeld;
    uint32_t initFlags;
    uint32_t updateSize;
    ForgeMasterVoice *master;
    ForgeLinkedList *sources;
    ForgeLinkedList *submixes;
    ForgeLinkedList *callbacks;
    ForgeAudioMutex sourceLock;
    ForgeAudioMutex submixLock;
    ForgeAudioMutex callbackLock;
    ForgeAudioMutex batchLock;
    ForgeAudioFormatExtensible mixFormat;

    struct ForgeAudioCommand *pending_commands;
    struct ForgeAudioCommand *ready_commands;

    /* Used to prevent destroying an active voice */
    ForgeSourceVoice *processingSource;

/* Temp storage for processing, interleaved PCM32F */
#define EXTRA_DECODE_PADDING 2
    uint32_t decodeSamples;
    uint32_t resampleSamples;
    uint32_t effectChainSamples;
    uint32_t effectChainSamples2;
    float *decodeCache;
    float *resampleCache;
    float *effectChainCache;
    float *effectChainCache2;

    /* Allocator callbacks */
    ForgeMallocFunc malloc_func;
    ForgeFreeFunc free_func;
    ForgeReallocFunc realloc_func;

    /* EngineProcedureEXT */
    void *clientEngineUser;
    ForgeEngineProcedure client_engine_proc;

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
    /* Debug Information */
    ForgeDebugConfiguration debug;
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */

    /* Platform opaque pointer */
    void *platform;
};

struct ForgeVoice {
    ForgeAudioEngine *audio;
    uint32_t flags;
    ForgeAudioVoiceType type;

    ForgeVoiceSendRuntimeList sends;
    ForgeEffectChainRuntime effects;
    ForgeFilterRuntime filter;
    ForgeAudioFilterState *filterState;
    ForgeAudioMutex sendLock;
    ForgeAudioMutex effectLock;
    ForgeAudioMutex filterLock;

    float volume;
    float *channelVolume;
    uint32_t outputChannels;
    ForgeAudioMutex volumeLock;
    struct {
        uint8_t active;
        float target;
        float step;
        uint32_t remainingFrames;
        uint8_t stopSourceOnComplete;
    } volumeAutomation;
    struct {
        uint8_t active;
        float *target;
        float *step;
        uint32_t remainingFrames;
    } channelVolumeAutomation;

    FORGE_AUDIO_NAMELESS union {
        struct {
            /* Sample storage */
            uint32_t decodeSamples;
            uint32_t resampleSamples;

            /* Resampler */
            float resampleFreq;
            uint64_t resampleStep;
            uint64_t resampleOffset;
            uint64_t curBufferOffsetDec;
            uint32_t curBufferOffset;

            /* Read-only */
            float maxFreqRatio;
            ForgeAudioFormat *format;
            ForgeAudioDecodeCallback decode;
            ForgeAudioResampleCallback resample;
            ForgeVoiceCallback *callback;

            /* Dynamic */
            uint8_t active;
            float freqRatio;
            uint64_t totalSamples;
            struct queued_buffer *queued_buffers;
            size_t queued_buffer_count, queued_buffers_capacity;
            struct queued_buffer *flush_buffers;
            size_t flush_buffer_count, flush_buffers_capacity;

            ForgeAudioMutex bufferLock;
        } src;
        struct {
            /* Sample storage */
            uint32_t inputSamples;
            uint32_t outputSamples;
            float *inputCache;
            uint64_t resampleStep;
            ForgeAudioResampleCallback resample;

            /* Read-only */
            uint32_t inputChannels;
            uint32_t inputSampleRate;
            uint32_t processingStage;
        } mix;
        struct {
            /* Output stream, allocated by Platform */
            float *output;

            /* Needed when inputChannels != outputChannels */
            float *effectCache;

            /* Read-only */
            uint32_t inputChannels;
            uint32_t inputSampleRate;
        } master;
    };
};

FORGE_INTERNAL_API bool fa_audio_insert_submix_sorted(ForgeLinkedList **start, ForgeSubmixVoice *to_add,
                                                      ForgeAudioMutex lock, ForgeMallocFunc malloc_func);
FORGE_INTERNAL_API void fa_audio_update_engine(ForgeAudioEngine *audio, float *output);
FORGE_INTERNAL_API bool fa_audio_resize_decode_cache(ForgeAudioEngine *audio, uint32_t size);
FORGE_INTERNAL_API ForgeResult fa_audio_alloc_effect_chain(ForgeVoice *voice, const ForgeEffectChain *effect_chain);
FORGE_INTERNAL_API void fa_audio_free_effect_chain(ForgeVoice *voice);
FORGE_INTERNAL_API ForgeResult fa_audio_voice_output_frequency(ForgeVoice *voice, const ForgeSendList *send_list);

#ifdef FORGE_AUDIO_TESTING
typedef struct ForgeAudioTestSourceResampleResult {
    uint32_t requested_decode_frames;
    uint32_t decoded_frames;
    uint64_t unclamped_resample_frames;
    uint32_t resampled_frames;
    uint32_t cur_buffer_offset;
    uint64_t cur_buffer_offset_dec;
    size_t queued_buffer_count;
} ForgeAudioTestSourceResampleResult;

FORGE_AUDIO_API float *forge_audio_test_process_effect_chain(ForgeVoice *voice, float *buffer, uint32_t *samples);
FORGE_AUDIO_API ForgeAudioTestSourceResampleResult forge_audio_test_decode_resample_source(ForgeSourceVoice *voice,
                                                                                           float *output);
FORGE_AUDIO_API uint32_t forge_audio_test_source_decode_frame_count(uint32_t resample_samples,
                                                                    float max_frequency_ratio,
                                                                    uint32_t source_sample_rate,
                                                                    uint32_t output_sample_rate);
/* Creates a fully initialized active engine without acquiring a platform audio device. */
FORGE_AUDIO_API ForgeResult forge_audio_test_create_offline_engine(ForgeAudioEngine **engine);
FORGE_AUDIO_API ForgeResult forge_audio_test_create_offline_engine_with_allocator(ForgeAudioEngine **engine,
                                                                                  ForgeMallocFunc custom_malloc,
                                                                                  ForgeFreeFunc custom_free,
                                                                                  ForgeReallocFunc custom_realloc);
FORGE_AUDIO_API void forge_audio_test_destroy_offline_engine(ForgeAudioEngine *audio);
FORGE_AUDIO_API ForgeResult forge_audio_test_create_virtual_master_voice(ForgeAudioEngine *audio,
                                                                         ForgeMasterVoice **mastering_voice,
                                                                         uint32_t input_channels,
                                                                         uint32_t input_sample_rate,
                                                                         uint32_t update_size,
                                                                         const ForgeEffectChain *effect_chain);
FORGE_AUDIO_API ForgeResult forge_audio_test_render(ForgeAudioEngine *audio, float *output, uint32_t frame_count);
#endif

FORGE_INTERNAL_API extern const float fa_audio_matrix_defaults[8][8][64];
FORGE_INTERNAL_API void fa_voice_recalc_mix_matrix(ForgeVoice *voice, uint32_t sendIndex);

#endif /* FORGE_CORE_INTERNAL_H */
