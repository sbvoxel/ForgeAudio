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

#ifndef FORGE_APO_FX_H
#define FORGE_APO_FX_H

#include "forge_apo.h"

#define FORGE_APO_FX_API FORGE_AUDIO_API

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* GUIDs */

extern const ForgeGuid FORGE_APO_FX_ID_EQ;
extern const ForgeGuid FORGE_APO_FX_ID_MASTERING_LIMITER;
extern const ForgeGuid FORGE_APO_FX_ID_REVERB;
extern const ForgeGuid FORGE_APO_FX_ID_ECHO;

/* Structures */

#pragma pack(push, 1)

/* See FORGE_APO_EQ_* constants below.
 * FrequencyCenter is in Hz, Gain is amplitude ratio, Bandwidth is Q factor.
 */
typedef struct ForgeApoEqParameters
{
    float FrequencyCenter0;
    float Gain0;
    float Bandwidth0;
    float FrequencyCenter1;
    float Gain1;
    float Bandwidth1;
    float FrequencyCenter2;
    float Gain2;
    float Bandwidth2;
    float FrequencyCenter3;
    float Gain3;
    float Bandwidth3;
} ForgeApoEqParameters;

/* See FORGE_APO_MASTERING_LIMITER_* constants below. */
typedef struct ForgeApoMasteringLimiterParameters
{
    uint32_t Release;    /* In milliseconds */
    uint32_t Loudness;
} ForgeApoMasteringLimiterParameters;

/* See FORGE_APO_REVERB_* constants below.
 * Both parameters are arbitrary and should be treated subjectively.
 */
typedef struct ForgeApoReverbParameters
{
    float Diffusion;
    float RoomSize;
} ForgeApoReverbParameters;

/* See FORGE_APO_ECHO_* constants below. */
typedef struct ForgeApoEchoParameters
{
    float WetDryMix;    /* Percentage of processed signal vs original */
    float Feedback;        /* Percentage to feed back into input */
    float Delay;        /* In milliseconds */
} ForgeApoEchoParameters;

#pragma pack(pop)

/* Constants */

#define FORGE_APO_EQ_MIN_SAMPLE_RATE 22000
#define FORGE_APO_EQ_MAX_SAMPLE_RATE 48000

#define FORGE_APO_EQ_MIN_FREQUENCY_CENTER        20.0f
#define FORGE_APO_EQ_MAX_FREQUENCY_CENTER        20000.0f
#define FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_0    100.0f
#define FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_1    800.0f
#define FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_2    2000.0f
#define FORGE_APO_EQ_DEFAULT_FREQUENCY_CENTER_3    10000.0f

#define FORGE_APO_EQ_MIN_GAIN    0.126f
#define FORGE_APO_EQ_MAX_GAIN    7.94f
#define FORGE_APO_EQ_DEFAULT_GAIN    1.0f

#define FORGE_APO_EQ_MIN_BANDWIDTH        0.1f
#define FORGE_APO_EQ_MAX_BANDWIDTH        2.0f
#define FORGE_APO_EQ_DEFAULT_BANDWIDTH    1.0f

#define FORGE_APO_MASTERING_LIMITER_MIN_RELEASE    1
#define FORGE_APO_MASTERING_LIMITER_MAX_RELEASE    20
#define FORGE_APO_MASTERING_LIMITER_DEFAULT_RELEASE    6

#define FORGE_APO_MASTERING_LIMITER_MIN_LOUDNESS    1
#define FORGE_APO_MASTERING_LIMITER_MAX_LOUDNESS    1800
#define FORGE_APO_MASTERING_LIMITER_DEFAULT_LOUDNESS    1000

#define FORGE_APO_REVERB_MIN_DIFFUSION    0.0f
#define FORGE_APO_REVERB_MAX_DIFFUSION    1.0f
#define FORGE_APO_REVERB_DEFAULT_DIFFUSION    0.9f

#define FORGE_APO_REVERB_MIN_ROOM_SIZE    0.0001f
#define FORGE_APO_REVERB_MAX_ROOM_SIZE    1.0f
#define FORGE_APO_REVERB_DEFAULT_ROOM_SIZE    0.6f

#define FORGE_APO_ECHO_MIN_WET_DRY_MIX    0.0f
#define FORGE_APO_ECHO_MAX_WET_DRY_MIX    1.0f
#define FORGE_APO_ECHO_DEFAULT_WET_DRY_MIX    0.5f

#define FORGE_APO_ECHO_MIN_FEEDBACK        0.0f
#define FORGE_APO_ECHO_MAX_FEEDBACK        1.0f
#define FORGE_APO_ECHO_DEFAULT_FEEDBACK    0.5f

#define FORGE_APO_ECHO_MIN_DELAY        1.0f
#define FORGE_APO_ECHO_MAX_DELAY        2000.0f
#define FORGE_APO_ECHO_DEFAULT_DELAY    500.0f

/* Functions */

/* Creates an effect from the pre-made ForgeApoFx effect library.
 *
 * clsid:        A reference to one of the FORGE_APO_FX_ID_* GUIDs
 * pEffect:        Filled with the resulting ForgeApo object
 * pInitData:        Starting parameters, pass NULL to use the default values
 * InitDataByteSize:    Parameter struct size, pass 0 if pInitData is NULL
 *
 * Returns 0 on success.
 */
FORGE_APO_FX_API uint32_t forge_apo_create_effect(
    const ForgeGuid *clsid,
    ForgeApo **pEffect,
    const void *pInitData,
    uint32_t InitDataByteSize
);

/* See "extensions/custom allocator.txt" for more details. */
FORGE_APO_FX_API uint32_t forge_apo_create_effect_with_allocator(
    const ForgeGuid *clsid,
    ForgeApo **pEffect,
    const void *pInitData,
    uint32_t InitDataByteSize,
    ForgeMallocFunc customMalloc,
    ForgeFreeFunc customFree,
    ForgeReallocFunc customRealloc
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_APO_FX_H */
