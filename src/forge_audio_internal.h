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

#include "forge_audio.h"
#include "forge_effect_base_internal.h"
#include <stdarg.h>
#include <stdbool.h>

#ifdef FORGE_AUDIO_WIN32_PLATFORM
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <assert.h>
#include <inttypes.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define forge_malloc malloc
#define forge_realloc realloc
#define forge_free free
#define forge_alloca(x) alloca(x)
#define forge_dealloca(x) (void)(x)
#define forge_zero(ptr, size) memset(ptr, '\0', size)
#define forge_memset(ptr, val, size) memset(ptr, val, size)
#define forge_memcpy(dst, src, size) memcpy(dst, src, size)
#define forge_memmove(dst, src, size) memmove(dst, src, size)
#define forge_memcmp(ptr1, ptr2, size) memcmp(ptr1, ptr2, size)

#define forge_strlen(ptr) strlen(ptr)
#define forge_strcmp(str1, str2) strcmp(str1, str2)
#define forge_strncmp(str1, str2, size) strncmp(str1, str2, size)
#define forge_strlcpy(ptr1, ptr2, size) lstrcpynA(ptr1, ptr2, size)

#define forge_pow(x, y) pow(x, y)
#define forge_powf(x, y) powf(x, y)
#define forge_log(x) log(x)
#define forge_log10(x) log10(x)
#define forge_sin(x) sin(x)
#define forge_cos(x) cos(x)
#define forge_tan(x) tan(x)
#define forge_acos(x) acos(x)
#define forge_ceil(x) ceil(x)
#define forge_floor(x) floor(x)
#define forge_abs(x) abs(x)
#define forge_ldexp(v, e) ldexp(v, e)
#define forge_exp(x) exp(x)

#define forge_cosf(x) cosf(x)
#define forge_sinf(x) sinf(x)
#define forge_sqrtf(x) sqrtf(x)
#define forge_acosf(x) acosf(x)
#define forge_atan2f(y, x) atan2f(y, x)
#define forge_fabsf(x) fabsf(x)

#define forge_qsort qsort

#define forge_assert assert
#define forge_snprintf snprintf
#define forge_vsnprintf vsnprintf
#define forge_getenv getenv
#define FORGE_PRIu64 PRIu64
#define FORGE_PRIx64 PRIx64

extern void forge_log_message(char const *msg);

/* FIXME: Assuming little-endian! */
#define forge_swap16le(x) (x)
#define forge_swap16be(x) \
	((x >> 8)	& 0x00FF) | \
	((x << 8)	& 0xFF00)
#define forge_swap32le(x) (x)
#define forge_swap32be(x) \
	((x >> 24)	& 0x000000FF) | \
	((x >> 8)	& 0x0000FF00) | \
	((x << 8)	& 0x00FF0000) | \
	((x << 24)	& 0xFF000000)
#define forge_swap64le(x) (x)
#define forge_swap64be(x) \
	((x >> 32)	& 0x00000000000000FF) | \
	((x >> 24)	& 0x000000000000FF00) | \
	((x >> 16)	& 0x0000000000FF0000) | \
	((x >> 8)	& 0x00000000FF000000) | \
	((x << 8)	& 0x000000FF00000000) | \
	((x << 16)	& 0x0000FF0000000000) | \
	((x << 24)	& 0x00FF000000000000) | \
	((x << 32)	& 0xFF00000000000000)
#else
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_log.h>

#define forge_swap16le(x) SDL_Swap16LE(x)
#define forge_swap16be(x) SDL_Swap16BE(x)
#define forge_swap32le(x) SDL_Swap32LE(x)
#define forge_swap32be(x) SDL_Swap32BE(x)
#define forge_swap64le(x) SDL_Swap64LE(x)
#define forge_swap64be(x) SDL_Swap64BE(x)

/* SDL3 allows memcpy/memset for compiler optimization reasons */
#ifdef SDL_SLOW_MEMCPY
#define STB_MEMCPY_OVERRIDE
#endif
#ifdef SDL_SLOW_MEMSET
#define STB_MEMSET_OVERRIDE
#endif

#define forge_malloc SDL_malloc
#define forge_realloc SDL_realloc
#define forge_free SDL_free
#define forge_alloca(x) SDL_stack_alloc(uint8_t, x)
#define forge_dealloca(x) SDL_stack_free(x)
#define forge_zero(ptr, size) SDL_memset(ptr, '\0', size)
#define forge_memset(ptr, val, size) SDL_memset(ptr, val, size)
#define forge_memcpy(dst, src, size) SDL_memcpy(dst, src, size)
#define forge_memmove(dst, src, size) SDL_memmove(dst, src, size)
#define forge_memcmp(ptr1, ptr2, size) SDL_memcmp(ptr1, ptr2, size)

#define forge_strlen(ptr) SDL_strlen(ptr)
#define forge_strcmp(str1, str2) SDL_strcmp(str1, str2)
#define forge_strncmp(str1, str2, size) SDL_strncmp(str1, str2, size)
#define forge_strlcpy(ptr1, ptr2, size) SDL_strlcpy(ptr1, ptr2, size)

#define forge_pow(x, y) SDL_pow(x, y)
#define forge_powf(x, y) SDL_powf(x, y)
#define forge_log(x) SDL_log(x)
#define forge_log10(x) SDL_log10(x)
#define forge_sin(x) SDL_sin(x)
#define forge_cos(x) SDL_cos(x)
#define forge_tan(x) SDL_tan(x)
#define forge_acos(x) SDL_acos(x)
#define forge_ceil(x) SDL_ceil(x)
#define forge_floor(x) SDL_floor(x)
#define forge_abs(x) SDL_abs(x)
#define forge_ldexp(v, e) SDL_scalbn(v, e)
#define forge_exp(x) SDL_exp(x)

#define forge_cosf(x) SDL_cosf(x)
#define forge_sinf(x) SDL_sinf(x)
#define forge_sqrtf(x) SDL_sqrtf(x)
#define forge_acosf(x) SDL_acosf(x)
#define forge_atan2f(y, x) SDL_atan2f(y, x)
#define forge_fabsf(x) SDL_fabsf(x)

#define forge_qsort SDL_qsort

#ifdef FORGE_AUDIO_LOG_ASSERTIONS
#define forge_assert(condition) \
	{ \
		static uint8_t logged = 0; \
		if (!(condition) && !logged) \
		{ \
			SDL_Log("Assertion failed: %s", #condition); \
			logged = 1; \
		} \
	}
#else
#define forge_assert SDL_assert
#endif
#define forge_snprintf SDL_snprintf
#define forge_vsnprintf SDL_vsnprintf
#define forge_log_message(msg) SDL_Log("%s", msg)
#define forge_getenv SDL_getenv
#define FORGE_PRIu64 SDL_PRIu64
#define FORGE_PRIx64 SDL_PRIx64
#endif

/* Easy Macros */
#define forge_min(val1, val2) \
    (val1 < val2 ? val1 : val2)
#define forge_max(val1, val2) \
    (val1 > val2 ? val1 : val2)
#define forge_clamp(val, min, max) \
    (val > max ? max : (val < min ? min : val))

/* Alignment macro for gcc/clang/msvc */
#if defined(__clang__) || defined(__GNUC__)
#define ALIGN(type, boundary) type __attribute__((aligned(boundary)))
#elif defined(_MSC_VER)
#define ALIGN(type, boundary) __declspec(align(boundary)) type
#else
#define ALIGN(type, boundary) type
#endif

#if defined(__GNUC__) || defined(__clang__)
#define FORGE_INTERNAL_API __attribute__((visibility("hidden")))
#else
#define FORGE_INTERNAL_API
#endif

/* Threading Types */

typedef void* ForgeAudioThread;
typedef void* ForgeAudioMutex;
typedef int32_t (FORGE_AUDIO_CALL * ForgeAudioThreadFunc)(void* data);
typedef enum ForgeAudioThreadPriority
{
    FORGE_AUDIO_THREAD_PRIORITY_LOW,
    FORGE_AUDIO_THREAD_PRIORITY_NORMAL,
    FORGE_AUDIO_THREAD_PRIORITY_HIGH,
} ForgeAudioThreadPriority;

/* Linked Lists */

typedef struct ForgeLinkedList ForgeLinkedList;
struct ForgeLinkedList
{
    void* entry;
    ForgeLinkedList *next;
};
FORGE_INTERNAL_API void forge_linked_list_add_entry(
    ForgeLinkedList **start,
    void* toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
);
FORGE_INTERNAL_API void forge_linked_list_prepend_entry(
    ForgeLinkedList **start,
    void* toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
);
FORGE_INTERNAL_API void forge_linked_list_remove_entry(
    ForgeLinkedList **start,
    void* toRemove,
    ForgeAudioMutex lock,
    ForgeFreeFunc free_func
);

/* Internal ForgeAudio Types */

typedef enum ForgeAudioVoiceType
{
    FORGE_AUDIO_VOICE_SOURCE,
    FORGE_AUDIO_VOICE_SUBMIX,
    FORGE_AUDIO_VOICE_MASTER
} ForgeAudioVoiceType;

struct queued_buffer
{
    ForgeBuffer buffer;
    ForgeBufferWMA bufferWMA;
    uint32_t loop_bytes, play_bytes;
    bool sent_OnStartBuffer;
};

typedef void (FORGE_AUDIO_CALL * ForgeAudioDecodeCallback)(
    ForgeVoice *voice,
    const void *src,
    float *decodeCache,
    uint32_t samples
);

typedef void (FORGE_AUDIO_CALL * ForgeAudioResampleCallback)(
    float *restrict dCache,
    float *restrict resampleCache,
    uint64_t *resampleOffset,
    uint64_t resampleStep,
    uint64_t toResample,
    uint8_t channels
);

typedef void (FORGE_AUDIO_CALL * ForgeAudioMixCallback)(
    uint32_t toMix,
    uint32_t srcChans,
    uint32_t dstChans,
    float *restrict srcData,
    float *restrict dstData,
    float *restrict coefficients
);

typedef float ForgeAudioFilterState[4];

/* Operation Sets, original implementation by Tyler Glaiel */

typedef struct ForgeOperationSetOperation ForgeOperationSetOperation;

FORGE_INTERNAL_API void forge_operation_set_commit(ForgeAudioEngine *audio, uint32_t operation_set);
FORGE_INTERNAL_API void forge_operation_set_commit_all(ForgeAudioEngine *audio);
FORGE_INTERNAL_API void forge_operation_set_execute(ForgeAudioEngine *audio);

FORGE_INTERNAL_API void forge_operation_set_clear_all(ForgeAudioEngine *audio);
FORGE_INTERNAL_API void forge_operation_set_clear_all_for_voice(ForgeVoice *voice);

FORGE_INTERNAL_API void forge_operation_set_queue_enable_effect(
    ForgeVoice *voice,
    uint32_t effect_index,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_disable_effect(
    ForgeVoice *voice,
    uint32_t effect_index,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_effect_parameters(
    ForgeVoice *voice,
    uint32_t effect_index,
    const void *parameters,
    uint32_t parameters_byte_size,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_filter_parameters(
    ForgeVoice *voice,
    const ForgeFilterParameters *parameters,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_output_filter_parameters(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    const ForgeFilterParameters *parameters,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_volume(
    ForgeVoice *voice,
    float volume,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_channel_volumes(
    ForgeVoice *voice,
    uint32_t channels,
    const float *volumes,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_output_matrix(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    uint32_t source_channels,
    uint32_t destination_channels,
    const float *level_matrix,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_start(
    ForgeSourceVoice *voice,
    uint32_t flags,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_stop(
    ForgeSourceVoice *voice,
    uint32_t flags,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_exit_loop(
    ForgeSourceVoice *voice,
    uint32_t operation_set
);
FORGE_INTERNAL_API void forge_operation_set_queue_set_frequency_ratio(
    ForgeSourceVoice *voice,
    float ratio,
    uint32_t operation_set
);

/* Public ForgeAudio Types */

struct ForgeAudioEngine
{
    uint8_t active;
    uint32_t initFlags;
    uint32_t updateSize;
    ForgeMasterVoice *master;
    ForgeLinkedList *sources;
    ForgeLinkedList *submixes;
    ForgeLinkedList *callbacks;
    ForgeAudioMutex sourceLock;
    ForgeAudioMutex submixLock;
    ForgeAudioMutex callbackLock;
    ForgeAudioMutex operationLock;
    ForgeAudioFormatExtensible mixFormat;

    ForgeOperationSetOperation *queuedOperations;
    ForgeOperationSetOperation *committedOperations;

    /* Used to prevent destroying an active voice */
    ForgeSourceVoice *processingSource;

    /* Temp storage for processing, interleaved PCM32F */
    #define EXTRA_DECODE_PADDING 2
    uint32_t decodeSamples;
    uint32_t resampleSamples;
    uint32_t effectChainSamples;
    float *decodeCache;
    float *resampleCache;
    float *effectChainCache;

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

struct ForgeVoice
{
    ForgeAudioEngine *audio;
    uint32_t flags;
    ForgeAudioVoiceType type;

    ForgeSendList sends;
    float **sendCoefficients;
    float **mixCoefficients;
    ForgeAudioMixCallback *sendMix;
    ForgeFilterParameters *sendFilter;
    ForgeAudioFilterState **sendFilterState;
    struct
    {
        ForgeEffectBufferFlags state;
        uint32_t count;
        ForgeEffectDesc *desc;
        void **parameters;
        uint32_t *parameterSizes;
        uint8_t *parameterUpdates;
        uint8_t *inPlaceProcessing;
    } effects;
    ForgeFilterParameters filter;
    ForgeAudioFilterState *filterState;
    ForgeAudioMutex sendLock;
    ForgeAudioMutex effectLock;
    ForgeAudioMutex filterLock;

    float volume;
    float *channelVolume;
    uint32_t outputChannels;
    ForgeAudioMutex volumeLock;

    FORGE_AUDIO_NAMELESS union
    {
        struct
        {
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
        struct
        {
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
        struct
        {
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

/* Internal Functions */
FORGE_INTERNAL_API void forge_audio_insert_submix_sorted(
    ForgeLinkedList **start,
    ForgeSubmixVoice *toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
);
FORGE_INTERNAL_API void forge_audio_update_engine(ForgeAudioEngine *audio, float *output);
FORGE_INTERNAL_API void forge_audio_resize_decode_cache(ForgeAudioEngine *audio, uint32_t size);
FORGE_INTERNAL_API void forge_audio_alloc_effect_chain(
    ForgeVoice *voice,
    const ForgeEffectChain *effect_chain
);
FORGE_INTERNAL_API void forge_audio_free_effect_chain(ForgeVoice *voice);
FORGE_INTERNAL_API ForgeResult forge_audio_voice_output_frequency(
    ForgeVoice *voice,
    const ForgeSendList *send_list
);
FORGE_INTERNAL_API extern const float forge_audio_internal_matrix_defaults[8][8][64];

FORGE_INTERNAL_API bool forge_array_reserve(ForgeAudioEngine *audio, void **elements, size_t *capacity, size_t count, size_t size);

/* Debug */

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION

#if defined(_MSC_VER)
/* VC doesn't support __attribute__ at all, and there's no replacement for format. */
FORGE_INTERNAL_API void forge_audio_debug(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const char *fmt,
    ...
);
#else
FORGE_INTERNAL_API void forge_audio_debug(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const char *fmt,
    ...
) __attribute__((format(printf,5,6)));
#endif

FORGE_INTERNAL_API void forge_audio_debug_fmt(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const ForgeAudioFormat *fmt
);

#define PRINT_DEBUG(engine, cond, type, fmt, ...) \
    if (engine->debug.trace_mask & FORGE_AUDIO_LOG_##cond) \
    { \
        forge_audio_debug( \
            engine, \
            __FILE__, \
            __LINE__, \
            __func__, \
            type ": " fmt, \
            __VA_ARGS__ \
        ); \
    }

#define LOG_ERROR(engine, fmt, ...) PRINT_DEBUG(engine, ERRORS, "ERROR", fmt, __VA_ARGS__)
#define LOG_WARNING(engine, fmt, ...) PRINT_DEBUG(engine, WARNINGS, "WARNING", fmt, __VA_ARGS__)
#define LOG_INFO(engine, fmt, ...) PRINT_DEBUG(engine, INFO, "INFO", fmt, __VA_ARGS__)
#define LOG_DETAIL(engine, fmt, ...) PRINT_DEBUG(engine, DETAIL, "DETAIL", fmt, __VA_ARGS__)
#define LOG_API_ENTER(engine) PRINT_DEBUG(engine, API_CALLS, "API Enter", "%s", __func__)
#define LOG_API_EXIT(engine) PRINT_DEBUG(engine, API_CALLS, "API Exit", "%s", __func__)
#define LOG_FUNC_ENTER(engine) PRINT_DEBUG(engine, FUNC_CALLS, "FUNC Enter", "%s", __func__)
#define LOG_FUNC_EXIT(engine) PRINT_DEBUG(engine, FUNC_CALLS, "FUNC Exit", "%s", __func__)
/* TODO: LOG_TIMING */
#define LOG_MUTEX_CREATE(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex Create", "%p (%s)", mutex, #mutex)
#define LOG_MUTEX_DESTROY(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex destroy", "%p (%s)", mutex, #mutex)
#define LOG_MUTEX_LOCK(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex Lock", "%p (%s)", mutex, #mutex)
#define LOG_MUTEX_UNLOCK(engine, mutex) PRINT_DEBUG(engine, LOCKS, "Mutex Unlock", "%p (%s)", mutex, #mutex)
/* TODO: LOG_MEMORY */
/* TODO: LOG_STREAMING */

#define LOG_FORMAT(engine, waveFormat) \
    if (engine->debug.trace_mask & FORGE_AUDIO_LOG_INFO) \
    { \
        forge_audio_debug_fmt( \
            engine, \
            __FILE__, \
            __LINE__, \
            __func__, \
            waveFormat \
        ); \
    }


#else

#define LOG_ERROR(engine, fmt, ...)
#define LOG_WARNING(engine, fmt, ...)
#define LOG_INFO(engine, fmt, ...)
#define LOG_DETAIL(engine, fmt, ...)
#define LOG_API_ENTER(engine)
#define LOG_API_EXIT(engine)
#define LOG_FUNC_ENTER(engine)
#define LOG_FUNC_EXIT(engine)
/* TODO: LOG_TIMING */
#define LOG_MUTEX_CREATE(engine, mutex)
#define LOG_MUTEX_DESTROY(engine, mutex)
#define LOG_MUTEX_LOCK(engine, mutex)
#define LOG_MUTEX_UNLOCK(engine, mutex)
/* TODO: LOG_MEMORY */
/* TODO: LOG_STREAMING */

#define LOG_FORMAT(engine, waveFormat)

#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */

/* SIMD Stuff */

/* Callbacks declared as functions (rather than function pointers) are
 * scalar-only, for now. SIMD versions should be possible for these.
 */

FORGE_INTERNAL_API extern void (*forge_audio_convert_u8_to_f32)(
    const uint8_t *restrict src,
    float *restrict dst,
    uint32_t len
);
FORGE_INTERNAL_API extern void (*forge_audio_convert_s16_to_f32)(
    const int16_t *restrict src,
    float *restrict dst,
    uint32_t len
);
FORGE_INTERNAL_API extern void (*forge_audio_convert_s32_to_f32)(
    const int32_t *restrict src,
    float *restrict dst,
    uint32_t len
);

FORGE_INTERNAL_API extern ForgeAudioResampleCallback forge_audio_resample_mono;
FORGE_INTERNAL_API extern ForgeAudioResampleCallback forge_audio_resample_stereo;
FORGE_INTERNAL_API extern void forge_audio_resample_generic(
    float *restrict dCache,
    float *restrict resampleCache,
    uint64_t *resampleOffset,
    uint64_t resampleStep,
    uint64_t toResample,
    uint8_t channels
);

FORGE_INTERNAL_API extern void (*forge_audio_amplify)(
    float *output,
    uint32_t totalSamples,
    float volume
);

FORGE_INTERNAL_API extern ForgeAudioMixCallback forge_audio_mix_generic;

#define MIX_FUNC(name) \
    FORGE_INTERNAL_API extern void forge_audio_mix_##name##_scalar( \
        uint32_t toMix, \
        uint32_t srcChans, \
        uint32_t dstChans, \
        float *restrict srcData, \
        float *restrict dstData, \
        float *restrict coefficients \
    );
MIX_FUNC(generic)
MIX_FUNC(1in_1out)
MIX_FUNC(1in_2out)
MIX_FUNC(1in_6out)
MIX_FUNC(1in_8out)
MIX_FUNC(2in_1out)
MIX_FUNC(2in_2out)
MIX_FUNC(2in_6out)
MIX_FUNC(2in_8out)
#undef MIX_FUNC

FORGE_INTERNAL_API void forge_audio_init_simd_functions(uint8_t hasSSE2, uint8_t hasNEON);

/* Decoders */

#define DECODE_FUNC(name) \
    FORGE_INTERNAL_API extern void forge_audio_decode_##name( \
        ForgeVoice *voice, \
        const void *src, \
        float *decodeCache, \
        uint32_t samples \
    );
DECODE_FUNC(pcm8)
DECODE_FUNC(pcm16)
DECODE_FUNC(pcm24)
DECODE_FUNC(pcm32)
DECODE_FUNC(pcm32f)
DECODE_FUNC(wma_error)
#undef DECODE_FUNC

/* Platform Functions */

FORGE_INTERNAL_API void forge_platform_add_ref(void);
FORGE_INTERNAL_API void forge_platform_release(void);
FORGE_INTERNAL_API void forge_platform_init(
    ForgeAudioEngine *audio,
    uint32_t flags,
    uint32_t deviceIndex,
    ForgeAudioFormatExtensible *mixFormat,
    uint32_t *updateSize,
    void** platformDevice
);
FORGE_INTERNAL_API void forge_platform_quit(void* platformDevice);

FORGE_INTERNAL_API uint32_t forge_platform_get_device_count(void);
FORGE_INTERNAL_API ForgeResult forge_platform_get_device_details(
    uint32_t index,
    ForgeDeviceDetails *details
);

/* Threading */

FORGE_INTERNAL_API ForgeAudioThread forge_platform_create_thread(
    ForgeAudioThreadFunc func,
    const char *name,
    void* data
);
FORGE_INTERNAL_API void forge_platform_wait_thread(ForgeAudioThread thread, int32_t *retval);
FORGE_INTERNAL_API void forge_platform_set_thread_priority(ForgeAudioThreadPriority priority);
FORGE_INTERNAL_API uint64_t forge_platform_get_thread_id(void);
FORGE_INTERNAL_API ForgeAudioMutex forge_platform_create_mutex(void);
FORGE_INTERNAL_API void forge_platform_destroy_mutex(ForgeAudioMutex mutex);
FORGE_INTERNAL_API void forge_platform_lock_mutex(ForgeAudioMutex mutex);
FORGE_INTERNAL_API void forge_platform_unlock_mutex(ForgeAudioMutex mutex);
FORGE_INTERNAL_API void forge_audio_sleep(uint32_t ms);

/* Time */

FORGE_INTERNAL_API uint32_t forge_audio_time_ms(void);

/* WaveFormatExtensible Helpers */

static inline uint32_t GetMask(uint16_t channels)
{
    if (channels == 1) return FORGE_SPEAKER_MONO;
    if (channels == 2) return FORGE_SPEAKER_STEREO;
    if (channels == 3) return FORGE_SPEAKER_2POINT1;
    if (channels == 4) return FORGE_SPEAKER_QUAD;
    if (channels == 5) return FORGE_SPEAKER_4POINT1;
    if (channels == 6) return FORGE_SPEAKER_5POINT1;
    if (channels == 8) return FORGE_SPEAKER_7POINT1_SURROUND;
    forge_assert(0 && "Unrecognized speaker layout!");
    return 0;
}

static inline void WriteWaveFormatExtensible(
    ForgeAudioFormatExtensible *fmt,
    int channels,
    int samplerate,
    const ForgeGuid *subformat
) {
    forge_assert(fmt != NULL);
    fmt->format.bits_per_sample = 32;
    fmt->format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
    fmt->format.channels = channels;
    fmt->format.sample_rate = samplerate;
    fmt->format.block_align = (
        fmt->format.channels *
        (fmt->format.bits_per_sample / 8)
    );
    fmt->format.average_bytes_per_second = (
        fmt->format.sample_rate *
        fmt->format.block_align
    );
    fmt->format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
    fmt->samples.valid_bits_per_sample = 32;
    fmt->channel_mask = GetMask(fmt->format.channels);
    forge_memcpy(&fmt->sub_format, subformat, sizeof(ForgeGuid));
}

/* Resampling */

/* Okay, so here's what all this fixed-point goo is for:
 *
 * Inevitably you're going to run into weird sample rates,
 * both from WaveBank data and from pitch shifting changes.
 *
 * How we deal with this is by calculating a fixed "step"
 * value that steps from sample to sample at the speed needed
 * to get the correct output sample rate, and the offset
 * is stored as separate integer and fraction values.
 *
 * This allows us to do weird fractional steps between samples,
 * while at the same time not letting it drift off into death
 * thanks to floating point madness.
 *
 * Steps are stored in fixed-point with 32 bits for the fraction:
 *
 * 00000000000000000000000000000000 00000000000000000000000000000000
 * ^ Integer block (32)             ^ Fraction block (32)
 *
 * For example, to get 1.5:
 * 00000000000000000000000000000001 10000000000000000000000000000000
 *
 * The Integer block works exactly like you'd expect.
 * The Fraction block is divided by the Integer's "One" value.
 * So, the above Fraction represented visually...
 *   1 << 31
 *   -------
 *   1 << 32
 * ... which, simplified, is...
 *   1 << 0
 *   ------
 *   1 << 1
 * ... in other words, 1 / 2, or 0.5.
 */
#define FIXED_PRECISION        32
#define FIXED_ONE        (1LL << FIXED_PRECISION)

/* Quick way to drop parts */
#define FIXED_FRACTION_MASK    (FIXED_ONE - 1)
#define FIXED_INTEGER_MASK    ~FIXED_FRACTION_MASK

/* Helper macros to convert fixed to float */
#define DOUBLE_TO_FIXED(dbl) \
    ((uint64_t) (dbl * FIXED_ONE + 0.5))
#define FIXED_TO_DOUBLE(fxd) ( \
    (double) (fxd >> FIXED_PRECISION) + /* Integer part */ \
    ((fxd & FIXED_FRACTION_MASK) * (1.0 / FIXED_ONE)) /* Fraction part */ \
)
#define FIXED_TO_FLOAT(fxd) ( \
    (float) (fxd >> FIXED_PRECISION) + /* Integer part */ \
    ((fxd & FIXED_FRACTION_MASK) * (1.0f / FIXED_ONE)) /* Fraction part */ \
)

#ifdef FORGE_AUDIO_DUMP_VOICES
/* File writing structure */
typedef size_t (FORGE_AUDIO_CALL * ForgeAudioWriteFunc)(
    void *data,
    const void *src,
    size_t size,
    size_t count
);
typedef size_t (FORGE_AUDIO_CALL * ForgeAudioSizeFunc)(
    void *data
);
typedef struct ForgeAudioIOStreamOut
{
    void *data;
    ForgeReadFunc read;
    ForgeAudioWriteFunc write;
    ForgeSeekFunc seek;
    ForgeAudioSizeFunc size;
    ForgeCloseFunc close;
    void *lock;
} ForgeAudioIOStreamOut;

FORGE_INTERNAL_API ForgeAudioIOStreamOut* forge_audio_fopen_out(const char *path, const char *mode);
FORGE_INTERNAL_API void forge_audio_close_out(ForgeAudioIOStreamOut *io);
#endif /* FORGE_AUDIO_DUMP_VOICES */
