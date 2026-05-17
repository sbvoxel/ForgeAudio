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
#include "forge_effect_base.h"
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

#define ForgeAudio_malloc malloc
#define ForgeAudio_realloc realloc
#define ForgeAudio_free free
#define ForgeAudio_alloca(x) alloca(x)
#define ForgeAudio_dealloca(x) (void)(x)
#define ForgeAudio_zero(ptr, size) memset(ptr, '\0', size)
#define ForgeAudio_memset(ptr, val, size) memset(ptr, val, size)
#define ForgeAudio_memcpy(dst, src, size) memcpy(dst, src, size)
#define ForgeAudio_memmove(dst, src, size) memmove(dst, src, size)
#define ForgeAudio_memcmp(ptr1, ptr2, size) memcmp(ptr1, ptr2, size)

#define ForgeAudio_strlen(ptr) strlen(ptr)
#define ForgeAudio_strcmp(str1, str2) strcmp(str1, str2)
#define ForgeAudio_strncmp(str1, str2, size) strncmp(str1, str2, size)
#define ForgeAudio_strlcpy(ptr1, ptr2, size) lstrcpynA(ptr1, ptr2, size)

#define ForgeAudio_pow(x, y) pow(x, y)
#define ForgeAudio_powf(x, y) powf(x, y)
#define ForgeAudio_log(x) log(x)
#define ForgeAudio_log10(x) log10(x)
#define ForgeAudio_sin(x) sin(x)
#define ForgeAudio_cos(x) cos(x)
#define ForgeAudio_tan(x) tan(x)
#define ForgeAudio_acos(x) acos(x)
#define ForgeAudio_ceil(x) ceil(x)
#define ForgeAudio_floor(x) floor(x)
#define ForgeAudio_abs(x) abs(x)
#define ForgeAudio_ldexp(v, e) ldexp(v, e)
#define ForgeAudio_exp(x) exp(x)

#define ForgeAudio_cosf(x) cosf(x)
#define ForgeAudio_sinf(x) sinf(x)
#define ForgeAudio_sqrtf(x) sqrtf(x)
#define ForgeAudio_acosf(x) acosf(x)
#define ForgeAudio_atan2f(y, x) atan2f(y, x)
#define ForgeAudio_fabsf(x) fabsf(x)

#define ForgeAudio_qsort qsort

#define ForgeAudio_assert assert
#define ForgeAudio_snprintf snprintf
#define ForgeAudio_vsnprintf vsnprintf
#define ForgeAudio_getenv getenv
#define ForgeAudio_PRIu64 PRIu64
#define ForgeAudio_PRIx64 PRIx64

extern void ForgeAudio_Log(char const *msg);

/* FIXME: Assuming little-endian! */
#define ForgeAudio_swap16LE(x) (x)
#define ForgeAudio_swap16BE(x) \
	((x >> 8)	& 0x00FF) | \
	((x << 8)	& 0xFF00)
#define ForgeAudio_swap32LE(x) (x)
#define ForgeAudio_swap32BE(x) \
	((x >> 24)	& 0x000000FF) | \
	((x >> 8)	& 0x0000FF00) | \
	((x << 8)	& 0x00FF0000) | \
	((x << 24)	& 0xFF000000)
#define ForgeAudio_swap64LE(x) (x)
#define ForgeAudio_swap64BE(x) \
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

#define ForgeAudio_swap16LE(x) SDL_Swap16LE(x)
#define ForgeAudio_swap16BE(x) SDL_Swap16BE(x)
#define ForgeAudio_swap32LE(x) SDL_Swap32LE(x)
#define ForgeAudio_swap32BE(x) SDL_Swap32BE(x)
#define ForgeAudio_swap64LE(x) SDL_Swap64LE(x)
#define ForgeAudio_swap64BE(x) SDL_Swap64BE(x)

/* SDL3 allows memcpy/memset for compiler optimization reasons */
#ifdef SDL_SLOW_MEMCPY
#define STB_MEMCPY_OVERRIDE
#endif
#ifdef SDL_SLOW_MEMSET
#define STB_MEMSET_OVERRIDE
#endif

#define ForgeAudio_malloc SDL_malloc
#define ForgeAudio_realloc SDL_realloc
#define ForgeAudio_free SDL_free
#define ForgeAudio_alloca(x) SDL_stack_alloc(uint8_t, x)
#define ForgeAudio_dealloca(x) SDL_stack_free(x)
#define ForgeAudio_zero(ptr, size) SDL_memset(ptr, '\0', size)
#define ForgeAudio_memset(ptr, val, size) SDL_memset(ptr, val, size)
#define ForgeAudio_memcpy(dst, src, size) SDL_memcpy(dst, src, size)
#define ForgeAudio_memmove(dst, src, size) SDL_memmove(dst, src, size)
#define ForgeAudio_memcmp(ptr1, ptr2, size) SDL_memcmp(ptr1, ptr2, size)

#define ForgeAudio_strlen(ptr) SDL_strlen(ptr)
#define ForgeAudio_strcmp(str1, str2) SDL_strcmp(str1, str2)
#define ForgeAudio_strncmp(str1, str2, size) SDL_strncmp(str1, str2, size)
#define ForgeAudio_strlcpy(ptr1, ptr2, size) SDL_strlcpy(ptr1, ptr2, size)

#define ForgeAudio_pow(x, y) SDL_pow(x, y)
#define ForgeAudio_powf(x, y) SDL_powf(x, y)
#define ForgeAudio_log(x) SDL_log(x)
#define ForgeAudio_log10(x) SDL_log10(x)
#define ForgeAudio_sin(x) SDL_sin(x)
#define ForgeAudio_cos(x) SDL_cos(x)
#define ForgeAudio_tan(x) SDL_tan(x)
#define ForgeAudio_acos(x) SDL_acos(x)
#define ForgeAudio_ceil(x) SDL_ceil(x)
#define ForgeAudio_floor(x) SDL_floor(x)
#define ForgeAudio_abs(x) SDL_abs(x)
#define ForgeAudio_ldexp(v, e) SDL_scalbn(v, e)
#define ForgeAudio_exp(x) SDL_exp(x)

#define ForgeAudio_cosf(x) SDL_cosf(x)
#define ForgeAudio_sinf(x) SDL_sinf(x)
#define ForgeAudio_sqrtf(x) SDL_sqrtf(x)
#define ForgeAudio_acosf(x) SDL_acosf(x)
#define ForgeAudio_atan2f(y, x) SDL_atan2f(y, x)
#define ForgeAudio_fabsf(x) SDL_fabsf(x)

#define ForgeAudio_qsort SDL_qsort

#ifdef FORGE_AUDIO_LOG_ASSERTIONS
#define ForgeAudio_assert(condition) \
	{ \
		static uint8_t logged = 0; \
		if (!(condition) && !logged) \
		{ \
			SDL_Log("Assertion failed: %s", #condition); \
			logged = 1; \
		} \
	}
#else
#define ForgeAudio_assert SDL_assert
#endif
#define ForgeAudio_snprintf SDL_snprintf
#define ForgeAudio_vsnprintf SDL_vsnprintf
#define ForgeAudio_Log(msg) SDL_Log("%s", msg)
#define ForgeAudio_getenv SDL_getenv
#define ForgeAudio_PRIu64 SDL_PRIu64
#define ForgeAudio_PRIx64 SDL_PRIx64
#endif

/* Easy Macros */
#define ForgeAudio_min(val1, val2) \
    (val1 < val2 ? val1 : val2)
#define ForgeAudio_max(val1, val2) \
    (val1 > val2 ? val1 : val2)
#define ForgeAudio_clamp(val, min, max) \
    (val > max ? max : (val < min ? min : val))

/* Alignment macro for gcc/clang/msvc */
#if defined(__clang__) || defined(__GNUC__)
#define ALIGN(type, boundary) type __attribute__((aligned(boundary)))
#elif defined(_MSC_VER)
#define ALIGN(type, boundary) __declspec(align(boundary)) type
#else
#define ALIGN(type, boundary) type
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

typedef struct LinkedList LinkedList;
struct LinkedList
{
    void* entry;
    LinkedList *next;
};
void LinkedList_AddEntry(
    LinkedList **start,
    void* toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
);
void LinkedList_PrependEntry(
    LinkedList **start,
    void* toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
);
void LinkedList_RemoveEntry(
    LinkedList **start,
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

typedef struct ForgeAudio_OperationSet_Operation ForgeAudio_OperationSet_Operation;

void ForgeAudio_OperationSet_Commit(ForgeAudioEngine *audio, uint32_t operation_set);
void ForgeAudio_OperationSet_CommitAll(ForgeAudioEngine *audio);
void ForgeAudio_OperationSet_Execute(ForgeAudioEngine *audio);

void ForgeAudio_OperationSet_ClearAll(ForgeAudioEngine *audio);
void ForgeAudio_OperationSet_ClearAllForVoice(ForgeVoice *voice);

void ForgeAudio_OperationSet_QueueEnableEffect(
    ForgeVoice *voice,
    uint32_t effect_index,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueDisableEffect(
    ForgeVoice *voice,
    uint32_t effect_index,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetEffectParameters(
    ForgeVoice *voice,
    uint32_t effect_index,
    const void *parameters,
    uint32_t parameters_byte_size,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetFilterParameters(
    ForgeVoice *voice,
    const ForgeFilterParameters *parameters,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetOutputFilterParameters(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    const ForgeFilterParameters *parameters,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetVolume(
    ForgeVoice *voice,
    float volume,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetChannelVolumes(
    ForgeVoice *voice,
    uint32_t channels,
    const float *volumes,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetOutputMatrix(
    ForgeVoice *voice,
    ForgeVoice *destination_voice,
    uint32_t source_channels,
    uint32_t destination_channels,
    const float *level_matrix,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueStart(
    ForgeSourceVoice *voice,
    uint32_t flags,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueStop(
    ForgeSourceVoice *voice,
    uint32_t flags,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueExitLoop(
    ForgeSourceVoice *voice,
    uint32_t operation_set
);
void ForgeAudio_OperationSet_QueueSetFrequencyRatio(
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
    LinkedList *sources;
    LinkedList *submixes;
    LinkedList *callbacks;
    ForgeAudioMutex sourceLock;
    ForgeAudioMutex submixLock;
    ForgeAudioMutex callbackLock;
    ForgeAudioMutex operationLock;
    ForgeAudioFormatExtensible mixFormat;

    ForgeAudio_OperationSet_Operation *queuedOperations;
    ForgeAudio_OperationSet_Operation *committedOperations;

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
void ForgeAudio_Internal_InsertSubmixSorted(
    LinkedList **start,
    ForgeSubmixVoice *toAdd,
    ForgeAudioMutex lock,
    ForgeMallocFunc malloc_func
);
void ForgeAudio_Internal_UpdateEngine(ForgeAudioEngine *audio, float *output);
void ForgeAudio_Internal_ResizeDecodeCache(ForgeAudioEngine *audio, uint32_t size);
void ForgeAudio_Internal_AllocEffectChain(
    ForgeVoice *voice,
    const ForgeEffectChain *effect_chain
);
void ForgeAudio_Internal_FreeEffectChain(ForgeVoice *voice);
ForgeResult ForgeAudio_Internal_VoiceOutputFrequency(
    ForgeVoice *voice,
    const ForgeSendList *send_list
);
extern const float FORGE_AUDIO_INTERNAL_MATRIX_DEFAULTS[8][8][64];

bool array_reserve(ForgeAudioEngine *audio, void **elements, size_t *capacity, size_t count, size_t size);

/* Debug */

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION

#if defined(_MSC_VER)
/* VC doesn't support __attribute__ at all, and there's no replacement for format. */
void ForgeAudio_Internal_debug(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const char *fmt,
    ...
);
#else
void ForgeAudio_Internal_debug(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const char *fmt,
    ...
) __attribute__((format(printf,5,6)));
#endif

void ForgeAudio_Internal_debug_fmt(
    ForgeAudioEngine *audio,
    const char *file,
    uint32_t line,
    const char *func,
    const ForgeAudioFormat *fmt
);

#define PRINT_DEBUG(engine, cond, type, fmt, ...) \
    if (engine->debug.trace_mask & FORGE_AUDIO_LOG_##cond) \
    { \
        ForgeAudio_Internal_debug( \
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
        ForgeAudio_Internal_debug_fmt( \
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

extern void (*ForgeAudio_Internal_Convert_U8_To_F32)(
    const uint8_t *restrict src,
    float *restrict dst,
    uint32_t len
);
extern void (*ForgeAudio_Internal_Convert_S16_To_F32)(
    const int16_t *restrict src,
    float *restrict dst,
    uint32_t len
);
extern void (*ForgeAudio_Internal_Convert_S32_To_F32)(
    const int32_t *restrict src,
    float *restrict dst,
    uint32_t len
);

extern ForgeAudioResampleCallback ForgeAudio_Internal_ResampleMono;
extern ForgeAudioResampleCallback ForgeAudio_Internal_ResampleStereo;
extern void ForgeAudio_Internal_ResampleGeneric(
    float *restrict dCache,
    float *restrict resampleCache,
    uint64_t *resampleOffset,
    uint64_t resampleStep,
    uint64_t toResample,
    uint8_t channels
);

extern void (*ForgeAudio_Internal_Amplify)(
    float *output,
    uint32_t totalSamples,
    float volume
);

extern ForgeAudioMixCallback ForgeAudio_Internal_Mix_Generic;

#define MIX_FUNC(type) \
    extern void ForgeAudio_Internal_Mix_##type##_Scalar( \
        uint32_t toMix, \
        uint32_t srcChans, \
        uint32_t dstChans, \
        float *restrict srcData, \
        float *restrict dstData, \
        float *restrict coefficients \
    );
MIX_FUNC(Generic)
MIX_FUNC(1in_1out)
MIX_FUNC(1in_2out)
MIX_FUNC(1in_6out)
MIX_FUNC(1in_8out)
MIX_FUNC(2in_1out)
MIX_FUNC(2in_2out)
MIX_FUNC(2in_6out)
MIX_FUNC(2in_8out)
#undef MIX_FUNC

void ForgeAudio_Internal_InitSIMDFunctions(uint8_t hasSSE2, uint8_t hasNEON);

/* Decoders */

#define DECODE_FUNC(type) \
    extern void ForgeAudio_Internal_Decode##type( \
        ForgeVoice *voice, \
        const void *src, \
        float *decodeCache, \
        uint32_t samples \
    );
DECODE_FUNC(PCM8)
DECODE_FUNC(PCM16)
DECODE_FUNC(PCM24)
DECODE_FUNC(PCM32)
DECODE_FUNC(PCM32F)
DECODE_FUNC(WMAERROR)
#undef DECODE_FUNC

/* Platform Functions */

void ForgeAudio_PlatformAddRef(void);
void ForgeAudio_PlatformRelease(void);
void ForgeAudio_PlatformInit(
    ForgeAudioEngine *audio,
    uint32_t flags,
    uint32_t deviceIndex,
    ForgeAudioFormatExtensible *mixFormat,
    uint32_t *updateSize,
    void** platformDevice
);
void ForgeAudio_PlatformQuit(void* platformDevice);

uint32_t ForgeAudio_PlatformGetDeviceCount(void);
ForgeResult ForgeAudio_PlatformGetDeviceDetails(
    uint32_t index,
    ForgeDeviceDetails *details
);

/* Threading */

ForgeAudioThread ForgeAudio_PlatformCreateThread(
    ForgeAudioThreadFunc func,
    const char *name,
    void* data
);
void ForgeAudio_PlatformWaitThread(ForgeAudioThread thread, int32_t *retval);
void ForgeAudio_PlatformThreadPriority(ForgeAudioThreadPriority priority);
uint64_t ForgeAudio_PlatformGetThreadID(void);
ForgeAudioMutex ForgeAudio_PlatformCreateMutex(void);
void ForgeAudio_PlatformDestroyMutex(ForgeAudioMutex mutex);
void ForgeAudio_PlatformLockMutex(ForgeAudioMutex mutex);
void ForgeAudio_PlatformUnlockMutex(ForgeAudioMutex mutex);
void ForgeAudio_sleep(uint32_t ms);

/* Time */

uint32_t ForgeAudio_timems(void);

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
    ForgeAudio_assert(0 && "Unrecognized speaker layout!");
    return 0;
}

static inline void WriteWaveFormatExtensible(
    ForgeAudioFormatExtensible *fmt,
    int channels,
    int samplerate,
    const ForgeGuid *subformat
) {
    ForgeAudio_assert(fmt != NULL);
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
    ForgeAudio_memcpy(&fmt->sub_format, subformat, sizeof(ForgeGuid));
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

ForgeAudioIOStreamOut* ForgeAudio_fopen_out(const char *path, const char *mode);
void ForgeAudio_close_out(ForgeAudioIOStreamOut *io);
#endif /* FORGE_AUDIO_DUMP_VOICES */
