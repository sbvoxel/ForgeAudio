/* ForgeAudioEngine - XAudio Reimplementation for FNA
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

#ifndef _SPEAKER_POSITIONS_
#define SPEAKER_FRONT_LEFT	    0x00000001
#define SPEAKER_FRONT_RIGHT	    0x00000002
#define SPEAKER_FRONT_CENTER	    0x00000004
#define SPEAKER_LOW_FREQUENCY	    0x00000008
#define SPEAKER_BACK_LEFT	    0x00000010
#define SPEAKER_BACK_RIGHT	    0x00000020
#define SPEAKER_FRONT_LEFT_OF_CENTER	0x00000040
#define SPEAKER_FRONT_RIGHT_OF_CENTER	0x00000080
#define SPEAKER_BACK_CENTER	    0x00000100
#define SPEAKER_SIDE_LEFT	    0x00000200
#define SPEAKER_SIDE_RIGHT	    0x00000400
#define SPEAKER_TOP_CENTER	    0x00000800
#define SPEAKER_TOP_FRONT_LEFT	    0x00001000
#define SPEAKER_TOP_FRONT_CENTER    0x00002000
#define SPEAKER_TOP_FRONT_RIGHT	    0x00004000
#define SPEAKER_TOP_BACK_LEFT	    0x00008000
#define SPEAKER_TOP_BACK_CENTER	    0x00010000
#define SPEAKER_TOP_BACK_RIGHT	    0x00020000
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
    uint32_t SpeakerChannelMask;
    uint32_t SpeakerCount;
    uint32_t LowFrequencyChannelIndex;
    float SpeedOfSound;
    float SpeedOfSoundEpsilon;
} ForgeSpatializer;

typedef struct ForgeVector3
{
    float x;
    float y;
    float z;
} ForgeVector3;

typedef struct ForgeSpatialDistanceCurvePoint
{
    float Distance;
    float DSPSetting;
} ForgeSpatialDistanceCurvePoint;

typedef struct ForgeSpatialDistanceCurve
{
    ForgeSpatialDistanceCurvePoint *pPoints;
    uint32_t PointCount;
} ForgeSpatialDistanceCurve;

typedef struct ForgeSpatialCone
{
    float InnerAngle;
    float OuterAngle;
    float InnerVolume;
    float OuterVolume;
    float InnerLPF;
    float OuterLPF;
    float InnerReverb;
    float OuterReverb;
} ForgeSpatialCone;

typedef struct ForgeSpatialListener
{
    ForgeVector3 OrientFront;
    ForgeVector3 OrientTop;
    ForgeVector3 Position;
    ForgeVector3 Velocity;
    ForgeSpatialCone *pCone;
} ForgeSpatialListener;

typedef struct ForgeSpatialEmitter
{
    ForgeSpatialCone *pCone;
    ForgeVector3 OrientFront;
    ForgeVector3 OrientTop;
    ForgeVector3 Position;
    ForgeVector3 Velocity;
    float InnerRadius;
    float InnerRadiusAngle;
    uint32_t ChannelCount;
    float ChannelRadius;
    float *pChannelAzimuths;
    ForgeSpatialDistanceCurve *pVolumeCurve;
    ForgeSpatialDistanceCurve *pLFECurve;
    ForgeSpatialDistanceCurve *pLPFDirectCurve;
    ForgeSpatialDistanceCurve *pLPFReverbCurve;
    ForgeSpatialDistanceCurve *pReverbCurve;
    float CurveDistanceScaler;
    float DopplerScaler;
} ForgeSpatialEmitter;

typedef struct ForgeSpatialDspSettings
{
    float *pMatrixCoefficients;
    float *pDelayTimes;
    uint32_t SrcChannelCount;
    uint32_t DstChannelCount;
    float LPFDirectCoefficient;
    float LPFReverbCoefficient;
    float ReverbLevel;
    float DopplerFactor;
    float EmitterToListenerAngle;
    float EmitterToListenerDistance;
    float EmitterVelocityComponent;
    float ListenerVelocityComponent;
} ForgeSpatialDspSettings;

#pragma pack(pop)

/* Functions */

FORGE_SPATIAL_API bool forge_spatializer_init(
    uint32_t SpeakerChannelMask,
    float SpeedOfSound,
    ForgeSpatializer *spatializer
);

FORGE_SPATIAL_API void forge_spatializer_calculate(
    const ForgeSpatializer *spatializer,
    const ForgeSpatialListener *pListener,
    const ForgeSpatialEmitter *pEmitter,
    uint32_t Flags,
    ForgeSpatialDspSettings *pDSPSettings
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_3D_AUDIO_H */
