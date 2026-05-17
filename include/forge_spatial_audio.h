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

#ifndef FORGE_3D_AUDIO_H
#define FORGE_3D_AUDIO_H

#ifdef _WIN32
#define FORGE_SPATIAL_API __declspec(dllexport)
#else
#define FORGE_SPATIAL_API
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Constants */

#ifndef FORGE_SPEAKER_POSITIONS_DEFINED
#define FORGE_SPEAKER_FRONT_LEFT	    0x00000001
#define FORGE_SPEAKER_FRONT_RIGHT	    0x00000002
#define FORGE_SPEAKER_FRONT_CENTER	    0x00000004
#define FORGE_SPEAKER_LOW_FREQUENCY	    0x00000008
#define FORGE_SPEAKER_BACK_LEFT	    0x00000010
#define FORGE_SPEAKER_BACK_RIGHT	    0x00000020
#define FORGE_SPEAKER_FRONT_LEFT_OF_CENTER	0x00000040
#define FORGE_SPEAKER_FRONT_RIGHT_OF_CENTER	0x00000080
#define FORGE_SPEAKER_BACK_CENTER	    0x00000100
#define FORGE_SPEAKER_SIDE_LEFT	    0x00000200
#define FORGE_SPEAKER_SIDE_RIGHT	    0x00000400
#define FORGE_SPEAKER_TOP_CENTER	    0x00000800
#define FORGE_SPEAKER_TOP_FRONT_LEFT	    0x00001000
#define FORGE_SPEAKER_TOP_FRONT_CENTER    0x00002000
#define FORGE_SPEAKER_TOP_FRONT_RIGHT	    0x00004000
#define FORGE_SPEAKER_TOP_BACK_LEFT	    0x00008000
#define FORGE_SPEAKER_TOP_BACK_CENTER	    0x00010000
#define FORGE_SPEAKER_TOP_BACK_RIGHT	    0x00020000
#define FORGE_SPEAKER_POSITIONS_DEFINED
#endif

#ifndef FORGE_SPEAKER_MONO
#define FORGE_SPEAKER_MONO    FORGE_SPEAKER_FRONT_CENTER
#define FORGE_SPEAKER_STEREO    (FORGE_SPEAKER_FRONT_LEFT | FORGE_SPEAKER_FRONT_RIGHT)
#define FORGE_SPEAKER_2POINT1 \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_LOW_FREQUENCY    )
#define FORGE_SPEAKER_SURROUND \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_FRONT_CENTER    | \
        FORGE_SPEAKER_BACK_CENTER    )
#define FORGE_SPEAKER_QUAD \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_BACK_LEFT    | \
        FORGE_SPEAKER_BACK_RIGHT    )
#define FORGE_SPEAKER_4POINT1 \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_LOW_FREQUENCY    | \
        FORGE_SPEAKER_BACK_LEFT    | \
        FORGE_SPEAKER_BACK_RIGHT    )
#define FORGE_SPEAKER_5POINT1 \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_FRONT_CENTER    | \
        FORGE_SPEAKER_LOW_FREQUENCY    | \
        FORGE_SPEAKER_BACK_LEFT    | \
        FORGE_SPEAKER_BACK_RIGHT    )
#define FORGE_SPEAKER_7POINT1 \
    (    FORGE_SPEAKER_FRONT_LEFT        | \
        FORGE_SPEAKER_FRONT_RIGHT        | \
        FORGE_SPEAKER_FRONT_CENTER        | \
        FORGE_SPEAKER_LOW_FREQUENCY        | \
        FORGE_SPEAKER_BACK_LEFT        | \
        FORGE_SPEAKER_BACK_RIGHT        | \
        FORGE_SPEAKER_FRONT_LEFT_OF_CENTER    | \
        FORGE_SPEAKER_FRONT_RIGHT_OF_CENTER    )
#define FORGE_SPEAKER_5POINT1_SURROUND \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_FRONT_CENTER    | \
        FORGE_SPEAKER_LOW_FREQUENCY    | \
        FORGE_SPEAKER_SIDE_LEFT    | \
        FORGE_SPEAKER_SIDE_RIGHT    )
#define FORGE_SPEAKER_7POINT1_SURROUND \
    (    FORGE_SPEAKER_FRONT_LEFT    | \
        FORGE_SPEAKER_FRONT_RIGHT    | \
        FORGE_SPEAKER_FRONT_CENTER    | \
        FORGE_SPEAKER_LOW_FREQUENCY    | \
        FORGE_SPEAKER_BACK_LEFT    | \
        FORGE_SPEAKER_BACK_RIGHT    | \
        FORGE_SPEAKER_SIDE_LEFT    | \
        FORGE_SPEAKER_SIDE_RIGHT    )
#define FORGE_SPEAKER_XBOX FORGE_SPEAKER_5POINT1
#endif

#define FORGE_SPATIAL_PI		3.141592654f
#define FORGE_SPATIAL_2PI		6.283185307f

#define FORGE_SPATIAL_CALCULATE_MATRIX	    0x00000001
#define FORGE_SPATIAL_CALCULATE_DELAY	    0x00000002
#define FORGE_SPATIAL_CALCULATE_LPF_DIRECT	    0x00000004
#define FORGE_SPATIAL_CALCULATE_LPF_REVERB	    0x00000008
#define FORGE_SPATIAL_CALCULATE_REVERB	    0x00000010
#define FORGE_SPATIAL_CALCULATE_DOPPLER	    0x00000020
#define FORGE_SPATIAL_CALCULATE_EMITTER_ANGLE    0x00000040
#define FORGE_SPATIAL_CALCULATE_ZERO_CENTER	    0x00010000
#define FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE  0x00020000

/* Structures */

#pragma pack(push, 1)

typedef struct ForgeSpatializer
{
    uint32_t speaker_channel_mask;
    uint32_t speaker_count;
    uint32_t low_frequency_channel_index;
    float speed_of_sound;
    float speed_of_sound_epsilon;
} ForgeSpatializer;

typedef struct ForgeVector3
{
    float x;
    float y;
    float z;
} ForgeVector3;

typedef struct ForgeSpatialDistanceCurvePoint
{
    float distance;
    float dsp_setting;
} ForgeSpatialDistanceCurvePoint;

typedef struct ForgeSpatialDistanceCurve
{
    ForgeSpatialDistanceCurvePoint *points;
    uint32_t point_count;
} ForgeSpatialDistanceCurve;

typedef struct ForgeSpatialCone
{
    float inner_angle;
    float outer_angle;
    float inner_volume;
    float outer_volume;
    float inner_lpf;
    float outer_lpf;
    float inner_reverb;
    float outer_reverb;
} ForgeSpatialCone;

typedef struct ForgeSpatialListener
{
    ForgeVector3 orient_front;
    ForgeVector3 orient_top;
    ForgeVector3 position;
    ForgeVector3 velocity;
    ForgeSpatialCone *cone;
} ForgeSpatialListener;

typedef struct ForgeSpatialEmitter
{
    ForgeSpatialCone *cone;
    ForgeVector3 orient_front;
    ForgeVector3 orient_top;
    ForgeVector3 position;
    ForgeVector3 velocity;
    float inner_radius;
    float inner_radius_angle;
    uint32_t channel_count;
    float channel_radius;
    float *channel_azimuths;
    ForgeSpatialDistanceCurve *volume_curve;
    ForgeSpatialDistanceCurve *lfe_curve;
    ForgeSpatialDistanceCurve *lpf_direct_curve;
    ForgeSpatialDistanceCurve *lpf_reverb_curve;
    ForgeSpatialDistanceCurve *reverb_curve;
    float curve_distance_scaler;
    float doppler_scaler;
} ForgeSpatialEmitter;

typedef struct ForgeSpatialDspSettings
{
    float *matrix_coefficients;
    float *delay_times;
    uint32_t src_channel_count;
    uint32_t dst_channel_count;
    float lpf_direct_coefficient;
    float lpf_reverb_coefficient;
    float reverb_level;
    float doppler_factor;
    float emitter_to_listener_angle;
    float emitter_to_listener_distance;
    float emitter_velocity_component;
    float listener_velocity_component;
} ForgeSpatialDspSettings;

#pragma pack(pop)

/* Functions */

FORGE_SPATIAL_API bool forge_spatializer_init(
    uint32_t speaker_channel_mask,
    float speed_of_sound,
    ForgeSpatializer *spatializer
);

FORGE_SPATIAL_API void forge_spatializer_calculate(
    const ForgeSpatializer *spatializer,
    const ForgeSpatialListener *listener,
    const ForgeSpatialEmitter *emitter,
    uint32_t flags,
    ForgeSpatialDspSettings *dsp_settings
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_3D_AUDIO_H */
