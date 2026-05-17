/* ForgeAudio
 *
 * This file is part of ForgeAudio, an altered source version of FAudio.
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

#include "forge_spatial_audio.h"
#include "forge_audio_internal.h"

#include <math.h> /* ONLY USE THIS FOR isnan! */
#include <float.h> /* ONLY USE THIS FOR FLT_MIN/FLT_MAX! */

/* UTILITY MACROS */

#define PARAM_CHECK_OK 1
#define PARAM_CHECK_FAIL (!PARAM_CHECK_OK)

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

#define LERP(a, x, y) ((1.0f - a) * x + a * y)

/* PARAMETER CHECK MACROS */

#define PARAM_CHECK(cond, msg) ForgeAudio_assert(cond && msg)

#define POINTER_CHECK(p) \
    PARAM_CHECK(p != NULL, "Pointer " #p " must be != NULL")

#define FLOAT_BETWEEN_CHECK(f, a, b) \
    PARAM_CHECK(f >= a, "Value" #f " is too low"); \
    PARAM_CHECK(f <= b, "Value" #f " is too big")


/* Spatial vectors are treated as orthonormal when their magnitude is within
 * 1e-5 of 1.0 and their dot product is within 1e-5 of zero.
 */

/* TODO: Switch to square length (to save CPU) */
#define VECTOR_NORMAL_CHECK(v) \
    PARAM_CHECK( \
        ForgeAudio_fabsf(VectorLength(v) - 1.0f) <= 1e-5f, \
        "Vector " #v " isn't normal" \
    )

#define VECTOR_BASE_CHECK(u, v) \
    PARAM_CHECK( \
        ForgeAudio_fabsf(VectorDot(u, v)) <= 1e-5f, \
        "Vector u and v have non-negligible dot product" \
    )

/******************************************
 * forge_spatializer_init Implementation *
 ******************************************/

#define SPEAKERMASK(spatializer) ((spatializer)->speaker_channel_mask)
#define SPEAKERCOUNT(spatializer) ((spatializer)->speaker_count)
#define FORGE_SPEAKER_LF_INDEX(spatializer) ((spatializer)->low_frequency_channel_index)
#define SPEEDOFSOUND(spatializer) ((spatializer)->speed_of_sound)
#define SPEEDOFSOUNDEPSILON(spatializer) ((spatializer)->speed_of_sound_epsilon)

static bool forge_spatializer_check_init_params(
    uint32_t speaker_channel_mask,
    float speed_of_sound,
    ForgeSpatializer *spatializer
) {
    const uint32_t kAllowedSpeakerMasks[] =
    {
        FORGE_SPEAKER_MONO,
        FORGE_SPEAKER_STEREO,
        FORGE_SPEAKER_2POINT1,
        FORGE_SPEAKER_QUAD,
        FORGE_SPEAKER_SURROUND,
        FORGE_SPEAKER_4POINT1,
        FORGE_SPEAKER_5POINT1,
        FORGE_SPEAKER_5POINT1_SURROUND,
        FORGE_SPEAKER_7POINT1,
        FORGE_SPEAKER_7POINT1_SURROUND,
    };
    uint8_t speakerMaskIsValid = 0;
    uint32_t i;

    if (spatializer == NULL)
    {
        PARAM_CHECK(spatializer != NULL, "spatializer must be != NULL");
        return false;
    }

    for (i = 0; i < ARRAY_COUNT(kAllowedSpeakerMasks); i += 1)
    {
        if (speaker_channel_mask == kAllowedSpeakerMasks[i])
        {
            speakerMaskIsValid = 1;
            break;
        }
    }

    /* The docs don't clearly say it, but the debug dll does check that
     * we're exactly in one of the allowed speaker configurations.
     * -Adrien
     */
    if (!speakerMaskIsValid)
    {
        PARAM_CHECK(
            speakerMaskIsValid == 1,
            "speaker_channel_mask is invalid. Needs to be one of"
            " MONO, STEREO, QUAD, 2POINT1, 4POINT1, 5POINT1, 7POINT1,"
            " SURROUND, 5POINT1_SURROUND, or 7POINT1_SURROUND."
        );
        return false;
    }

    if (speed_of_sound < FLT_MIN)
    {
        PARAM_CHECK(speed_of_sound >= FLT_MIN, "speed_of_sound needs to be >= FLT_MIN");
        return false;
    }

    return true;
}

bool forge_spatializer_init(
    uint32_t speaker_channel_mask,
    float speed_of_sound,
    ForgeSpatializer *spatializer
) {
    union
    {
        float f;
        uint32_t i;
    } epsilonHack;
    uint32_t speakerCount = 0;

    if (!forge_spatializer_check_init_params(speaker_channel_mask, speed_of_sound, spatializer))
    {
        return false;
    }

    SPEAKERMASK(spatializer) = speaker_channel_mask;
    SPEEDOFSOUND(spatializer) = speed_of_sound;

    /* "Convert" raw float to int... */
    epsilonHack.f = speed_of_sound;
    /* ... Subtract epsilon value... */
    epsilonHack.i -= 1;
    /* ... Convert back to float. */
    SPEEDOFSOUNDEPSILON(spatializer) = epsilonHack.f;

    FORGE_SPEAKER_LF_INDEX(spatializer) = 0xFFFFFFFF;
    if (speaker_channel_mask & FORGE_SPEAKER_LOW_FREQUENCY)
    {
        if (speaker_channel_mask & FORGE_SPEAKER_FRONT_CENTER)
        {
            FORGE_SPEAKER_LF_INDEX(spatializer) = 3;
        }
        else
        {
            FORGE_SPEAKER_LF_INDEX(spatializer) = 2;
        }
    }

    while (speaker_channel_mask)
    {
        speakerCount += 1;
        speaker_channel_mask &= speaker_channel_mask - 1;
    }
    SPEAKERCOUNT(spatializer) = speakerCount;

    return true;
}


/*********************************************
 * forge_spatializer_calculate Implementation *
 *********************************************/

/* VECTOR UTILITIES */

static inline ForgeVector3 Vec(float x, float y, float z)
{
    ForgeVector3 res;
    res.x = x;
    res.y = y;
    res.z = z;
    return res;
}

#define VectorAdd(u, v) Vec(u.x + v.x, u.y + v.y, u.z + v.z)

#define VectorSub(u, v) Vec(u.x - v.x, u.y - v.y, u.z - v.z)

#define VectorScale(u, s) Vec(u.x * s, u.y * s, u.z * s)

#define VectorCross(u, v) Vec( \
    (u.y * v.z) - (u.z * v.y), \
    (u.z * v.x) - (u.x * v.z), \
    (u.x * v.y) - (u.y * v.x) \
)

#define VectorLength(v) ForgeAudio_sqrtf( \
    (v.x * v.x) + (v.y * v.y) + (v.z * v.z) \
)

#define VectorDot(u, v) ((u.x * v.x) + (u.y * v.y) + (u.z * v.z))

/* This structure represent a tuple of vectors that form a left-handed basis.
 * That is, all vectors are normal, orthogonal to each other, and taken in the
 * order front, right, top they follow the left-hand rule.
 * (https://en.wikipedia.org/wiki/Right-hand_rule)
 */
typedef struct ForgeSpatialBasis
{
    ForgeVector3 front;
    ForgeVector3 right;
    ForgeVector3 top;
} ForgeSpatialBasis;

/* CHECK UTILITY FUNCTIONS */

static inline uint8_t CheckCone(ForgeSpatialCone *cone)
{
    if (!cone)
    {
        return PARAM_CHECK_OK;
    }

    FLOAT_BETWEEN_CHECK(cone->inner_angle, 0.0f, FORGE_SPATIAL_2PI);
    FLOAT_BETWEEN_CHECK(cone->outer_angle, cone->inner_angle, FORGE_SPATIAL_2PI);

    FLOAT_BETWEEN_CHECK(cone->inner_volume, 0.0f, 2.0f);
    FLOAT_BETWEEN_CHECK(cone->outer_volume, 0.0f, 2.0f);

    FLOAT_BETWEEN_CHECK(cone->inner_lpf, 0.0f, 1.0f);
    FLOAT_BETWEEN_CHECK(cone->outer_lpf, 0.0f, 1.0f);

    FLOAT_BETWEEN_CHECK(cone->inner_reverb, 0.0f, 2.0f);
    FLOAT_BETWEEN_CHECK(cone->outer_reverb, 0.0f, 2.0f);

    return PARAM_CHECK_OK;
}

static inline uint8_t CheckCurve(ForgeSpatialDistanceCurve *curve)
{
    ForgeSpatialDistanceCurvePoint *points;
    uint32_t i;
    if (!curve)
    {
        return PARAM_CHECK_OK;
    }

    points = curve->points;
    POINTER_CHECK(points);
    PARAM_CHECK(curve->point_count >= 2, "Invalid number of points for curve");

    for (i = 0; i < curve->point_count; i += 1)
    {
        FLOAT_BETWEEN_CHECK(points[i].distance, 0.0f, 1.0f);
    }

    PARAM_CHECK(
        points[0].distance == 0.0f,
        "First point in the curve must be at distance 0.0f"
    );
    PARAM_CHECK(
        points[curve->point_count - 1].distance == 1.0f,
        "Last point in the curve must be at distance 1.0f"
    );

    for (i = 0; i < (curve->point_count - 1); i += 1)
    {
        PARAM_CHECK(
            points[i].distance < points[i + 1].distance,
            "Curve points must be in strict ascending order"
        );
    }

    return PARAM_CHECK_OK;
}

static uint8_t forge_spatializer_check_calculate_params(
    const ForgeSpatializer *spatializer,
    const ForgeSpatialListener *listener,
    const ForgeSpatialEmitter *emitter,
    uint32_t flags,
    ForgeSpatialDspSettings *dsp_settings
) {
    uint32_t i, channel_count;

    POINTER_CHECK(spatializer);
    POINTER_CHECK(listener);
    POINTER_CHECK(emitter);
    POINTER_CHECK(dsp_settings);

    if (flags & FORGE_SPATIAL_CALCULATE_MATRIX)
    {
        POINTER_CHECK(dsp_settings->matrix_coefficients);
    }
    if (flags & FORGE_SPATIAL_CALCULATE_ZERO_CENTER)
    {
        const uint32_t isCalculateMatrix = (flags & FORGE_SPATIAL_CALCULATE_MATRIX);
        const uint32_t hasCenter = SPEAKERMASK(spatializer) & FORGE_SPEAKER_FRONT_CENTER;
        PARAM_CHECK(
            isCalculateMatrix && hasCenter,
            "FORGE_SPATIAL_CALCULATE_ZERO_CENTER is only valid for matrix"
            " calculations with an output format that has a center channel"
        );
    }

    if (flags & FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE)
    {
        const uint32_t isCalculateMatrix = (flags & FORGE_SPATIAL_CALCULATE_MATRIX);
        const uint32_t hasLF = SPEAKERMASK(spatializer) & FORGE_SPEAKER_LOW_FREQUENCY;
        PARAM_CHECK(
            isCalculateMatrix && hasLF,
            "FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE is only valid for matrix"
            " calculations with an output format that has a low-frequency"
            " channel"
        );
    }

    channel_count = SPEAKERCOUNT(spatializer);
    PARAM_CHECK(
        dsp_settings->dst_channel_count == channel_count,
        "Invalid channel count, DSP settings and speaker configuration must agree"
    );
    PARAM_CHECK(
        dsp_settings->src_channel_count == emitter->channel_count,
        "Invalid channel count, DSP settings and emitter must agree"
    );

    if (listener->cone)
    {
        PARAM_CHECK(
            CheckCone(listener->cone) == PARAM_CHECK_OK,
            "Invalid listener cone"
        );
    }
    VECTOR_NORMAL_CHECK(listener->orient_front);
    VECTOR_NORMAL_CHECK(listener->orient_top);
    VECTOR_BASE_CHECK(listener->orient_front, listener->orient_top);

    if (emitter->cone)
    {
        VECTOR_NORMAL_CHECK(emitter->orient_front);
        PARAM_CHECK(
            CheckCone(emitter->cone) == PARAM_CHECK_OK,
            "Invalid emitter cone"
        );
    }
    else if (flags & FORGE_SPATIAL_CALCULATE_EMITTER_ANGLE)
    {
        VECTOR_NORMAL_CHECK(emitter->orient_front);
    }
    if (emitter->channel_count > 1)
    {
        /* Only used for multi-channel emitters */
        VECTOR_NORMAL_CHECK(emitter->orient_front);
        VECTOR_NORMAL_CHECK(emitter->orient_top);
        VECTOR_BASE_CHECK(emitter->orient_front, emitter->orient_top);
    }
    FLOAT_BETWEEN_CHECK(emitter->inner_radius, 0.0f, FLT_MAX);
    FLOAT_BETWEEN_CHECK(emitter->inner_radius_angle, 0.0f, FORGE_SPATIAL_2PI / 4.0f);
    PARAM_CHECK(
        emitter->channel_count > 0,
        "Invalid channel count for emitter"
    );
    PARAM_CHECK(
        emitter->channel_radius >= 0.0f,
        "Invalid channel radius for emitter"
    );
    if (emitter->channel_count > 1)
    {
        PARAM_CHECK(
            emitter->channel_azimuths != NULL,
            "Invalid channel azimuths for multi-channel emitter"
        );
        if (emitter->channel_azimuths)
        {
            for (i = 0; i < emitter->channel_count; i += 1)
            {
                float currentAzimuth = emitter->channel_azimuths[i];
                FLOAT_BETWEEN_CHECK(currentAzimuth, 0.0f, FORGE_SPATIAL_2PI);
                if (currentAzimuth == FORGE_SPATIAL_2PI)
                {
                    PARAM_CHECK(
                        !(flags & FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE),
                        "FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE valid only for"
                        " matrix calculations with emitters that have no LFE"
                        " channel"
                    );
                }
            }
        }
    }
    FLOAT_BETWEEN_CHECK(emitter->curve_distance_scaler, FLT_MIN, FLT_MAX);
    FLOAT_BETWEEN_CHECK(emitter->doppler_scaler, 0.0f, FLT_MAX);

    PARAM_CHECK(
        CheckCurve(emitter->volume_curve) == PARAM_CHECK_OK,
        "Invalid volume curve"
    );
    PARAM_CHECK(
        CheckCurve(emitter->lfe_curve) == PARAM_CHECK_OK,
        "Invalid LFE curve"
    );
    PARAM_CHECK(
        CheckCurve(emitter->lpf_direct_curve) == PARAM_CHECK_OK,
        "Invalid LPFDirect curve"
    );
    PARAM_CHECK(
        CheckCurve(emitter->lpf_reverb_curve) == PARAM_CHECK_OK,
        "Invalid LPFReverb curve"
    );
    PARAM_CHECK(
        CheckCurve(emitter->reverb_curve) == PARAM_CHECK_OK,
        "Invalid reverb curve"
    );

    return PARAM_CHECK_OK;
}

/*
 * MATRIX CALCULATION
 */

/* This function computes the distance either according to a curve if curve
 * isn't NULL, or according to the inverse distance law 1/d otherwise.
 */
static inline float ComputeDistanceAttenuation(
    float normalizedDistance,
    ForgeSpatialDistanceCurve *curve
) {
    float res;
    float alpha;
    uint32_t n_points;
    size_t i;
    if (curve)
    {
        ForgeSpatialDistanceCurvePoint* points = curve->points;
        n_points = curve->point_count;

        /* By definition, the first point in the curve must be 0.0f
         * -Adrien
         */

        /* We advance i up until our normalizedDistance lies between the distances of
         * the i_th and (i-1)_th points, or we reach the last point.
         */
        for (i = 1; (i < n_points) && (normalizedDistance >= points[i].distance); i += 1);
        if (i == n_points)
        {
            /* We've reached the last point, so we use its value directly. */
            res = points[n_points - 1].dsp_setting;
        }
        else
        {
            /* We're between two points: the distance attenuation is the linear interpolation of the dsp_setting
             * values defined by our points, according to the distance.
             */
            alpha = (points[i].distance - normalizedDistance) / (points[i].distance - points[i - 1].distance);
            res = LERP(alpha, points[i].dsp_setting, points[i - 1].dsp_setting);
        }
    }
    else
    {
        res = 1.0f;
        if (normalizedDistance >= 1.0f)
        {
            res /= normalizedDistance;
        }
    }
    return res;
}

static inline float ComputeConeParameter(
    float distance,
    float angle,
    float innerAngle,
    float outerAngle,
    float innerParam,
    float outerParam
) {
    /* When computing whether a point lies inside a cone, first determine
     * whether the point is close enough to the apex of the cone.
     * If it is, the innerParam is used.
     * The following constant is the one that is used for this distance check;
     * It is an approximation, found by manual binary search.
     * TODO: find the exact value of the constant via automated binary search. */
    #define CONE_NULL_DISTANCE_TOLERANCE 1e-7

    float halfInnerAngle, halfOuterAngle, alpha;

    /* Both cone angles at 0 use the outer values; both at 2PI use the inner
     * values.
     */
    if (innerAngle == 0.0f && outerAngle == 0.0f)
    {
        return outerParam;
    }
    if (innerAngle == FORGE_SPATIAL_2PI && outerAngle == FORGE_SPATIAL_2PI)
    {
        return innerParam;
    }

    /* If we're within the inner angle, or close enough to the apex, we use
     * the innerParam. */
    halfInnerAngle = innerAngle / 2.0f;
    if (distance <= CONE_NULL_DISTANCE_TOLERANCE || angle <= halfInnerAngle)
    {
        return innerParam;
    }

    /* If we're between the inner angle and the outer angle, we must use
     * some interpolation of the innerParam and outerParam according to the
     * distance between our angle and the inner and outer angles.
     */
    halfOuterAngle = outerAngle / 2.0f;
    if (angle <= halfOuterAngle)
    {
        alpha = (angle - halfInnerAngle) / (halfOuterAngle - halfInnerAngle);

        /* This is an approximation. A more accurate version may need a
         * higher-order interpolation curve.
         *
         * TODO: HIGH_ACCURACY version.
         * -Adrien
         */
        return LERP(alpha, innerParam, outerParam);
    }

    /* Otherwise, we're outside the outer angle, so we just return the outer param. */
    return outerParam;
}

/* Here we declare the azimuths of every speaker for every speaker
 * configuration, ordered by increasing angle, as well as the index to which
 * they map in the final matrix for their respective configuration. It had to be
 * reverse engineered from output matrix results for the various speaker
 * configurations; *in particular*, FORGE_SPEAKER_STEREO is declared as having
 * front L and R speakers in the
 * bit mask, but in fact has L and R *side* speakers). LF speakers are
 * deliberately not included in the SpeakerInfo list, rather, we store the index
 * into a separate field (with a -1 sentinel value if it has no LF speaker).
 * -Adrien
 */
typedef struct
{
    float azimuth;
    uint32_t matrixIdx;
} SpeakerInfo;

typedef struct
{
    uint32_t configMask;
    const SpeakerInfo *speakers;

    /* Not strictly necessary because it can be inferred from the
     * speaker_count field of the spatializer, but makes code much
     * cleaner and less error prone
     */
    uint32_t numNonLFSpeakers;

    int32_t LFSpeakerIdx;
} ConfigInfo;

/* It is absolutely necessary that these are stored in increasing, *positive*
 * azimuth order (i.e. all angles between [0; 2PI]), as we'll do a linear
 * interval search inside FindSpeakerAzimuths.
 * -Adrien
 */

#define FORGE_SPEAKER_AZIMUTH_CENTER            0.0f
#define FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT_OF_CENTER    (FORGE_SPATIAL_PI *  1.0f / 8.0f)
#define FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT        (FORGE_SPATIAL_PI *  1.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT        (FORGE_SPATIAL_PI *  1.0f / 2.0f)
#define FORGE_SPEAKER_AZIMUTH_BACK_RIGHT        (FORGE_SPATIAL_PI *  3.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_BACK_CENTER        FORGE_SPATIAL_PI
#define FORGE_SPEAKER_AZIMUTH_BACK_LEFT        (FORGE_SPATIAL_PI *  5.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_SIDE_LEFT        (FORGE_SPATIAL_PI *  3.0f / 2.0f)
#define FORGE_SPEAKER_AZIMUTH_FRONT_LEFT        (FORGE_SPATIAL_PI *  7.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_FRONT_LEFT_OF_CENTER    (FORGE_SPATIAL_PI * 15.0f / 8.0f)

const SpeakerInfo kMonoConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_CENTER, 0 },
};
const SpeakerInfo kStereoConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_SIDE_LEFT,    0 },
};
const SpeakerInfo k2Point1ConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_SIDE_LEFT,    0 },
};
const SpeakerInfo kSurroundConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_CENTER,    2 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_BACK_CENTER,    3 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,    0 },
};
const SpeakerInfo kQuadConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1 },
    { FORGE_SPEAKER_AZIMUTH_BACK_RIGHT,  3 },
    { FORGE_SPEAKER_AZIMUTH_BACK_LEFT,   2 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,  0 },
};
const SpeakerInfo k4Point1ConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_BACK_RIGHT,    4 },
    { FORGE_SPEAKER_AZIMUTH_BACK_LEFT,    3 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,    0 },
};
const SpeakerInfo k5Point1ConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_CENTER,    2 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_BACK_RIGHT,    5 },
    { FORGE_SPEAKER_AZIMUTH_BACK_LEFT,    4 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,    0 },
};
const SpeakerInfo k7Point1ConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_CENTER,            2 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT_OF_CENTER,    7 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT,            1 },
    { FORGE_SPEAKER_AZIMUTH_BACK_RIGHT,            5 },
    { FORGE_SPEAKER_AZIMUTH_BACK_LEFT,            4 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,            0 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT_OF_CENTER,        6 },
};
const SpeakerInfo k5Point1SurroundConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_CENTER,    2 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT,    5 },
    { FORGE_SPEAKER_AZIMUTH_SIDE_LEFT,    4 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,    0 },
};
const SpeakerInfo k7Point1SurroundConfigSpeakers[] =
{
    { FORGE_SPEAKER_AZIMUTH_CENTER,    2 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT,    1 },
    { FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT,    7 },
    { FORGE_SPEAKER_AZIMUTH_BACK_RIGHT,    5 },
    { FORGE_SPEAKER_AZIMUTH_BACK_LEFT,    4 },
    { FORGE_SPEAKER_AZIMUTH_SIDE_LEFT,    6 },
    { FORGE_SPEAKER_AZIMUTH_FRONT_LEFT,    0 },
};

/* With that organization, the index of the LF speaker into the matrix array
 * is kept in a separate field within ConfigInfo because it makes the code
 * much cleaner.
 * -Adrien
 */
const ConfigInfo kSpeakersConfigInfo[] =
{
    { FORGE_SPEAKER_MONO,            kMonoConfigSpeakers,        ARRAY_COUNT(kMonoConfigSpeakers),        -1 },
    { FORGE_SPEAKER_STEREO,        kStereoConfigSpeakers,        ARRAY_COUNT(kStereoConfigSpeakers),        -1 },
    { FORGE_SPEAKER_2POINT1,        k2Point1ConfigSpeakers,        ARRAY_COUNT(k2Point1ConfigSpeakers),         2 },
    { FORGE_SPEAKER_SURROUND,        kSurroundConfigSpeakers,    ARRAY_COUNT(kSurroundConfigSpeakers),        -1 },
    { FORGE_SPEAKER_QUAD,            kQuadConfigSpeakers,        ARRAY_COUNT(kQuadConfigSpeakers),        -1 },
    { FORGE_SPEAKER_4POINT1,        k4Point1ConfigSpeakers,        ARRAY_COUNT(k4Point1ConfigSpeakers),         2 },
    { FORGE_SPEAKER_5POINT1,        k5Point1ConfigSpeakers,        ARRAY_COUNT(k5Point1ConfigSpeakers),         3 },
    { FORGE_SPEAKER_7POINT1,        k7Point1ConfigSpeakers,        ARRAY_COUNT(k7Point1ConfigSpeakers),         3 },
    { FORGE_SPEAKER_5POINT1_SURROUND,    k5Point1SurroundConfigSpeakers,    ARRAY_COUNT(k5Point1SurroundConfigSpeakers),     3 },
    { FORGE_SPEAKER_7POINT1_SURROUND,    k7Point1SurroundConfigSpeakers,    ARRAY_COUNT(k7Point1SurroundConfigSpeakers),     3 },
};

/* A simple linear search is absolutely OK for 10 elements. */
static const ConfigInfo* GetConfigInfo(uint32_t speakerConfigMask)
{
    uint32_t i;
    for (i = 0; i < ARRAY_COUNT(kSpeakersConfigInfo); i += 1)
    {
        if (kSpeakersConfigInfo[i].configMask == speakerConfigMask)
        {
            return &kSpeakersConfigInfo[i];
        }
    }

    ForgeAudio_assert(0 && "Config info not found!");
    return NULL;
}

/* Given a configuration, this function finds the azimuths of the two speakers
 * between which the emitter lies. All the azimuths here are relative to the
 * listener's base, since that's where the speakers are defined.
 */
static inline void FindSpeakerAzimuths(
    const ConfigInfo* config,
    float emitterAzimuth,
    uint8_t skipCenter,
    const SpeakerInfo **speakerInfo
) {
    uint32_t i, nexti = 0;
    float a0 = 0.0f, a1 = 0.0f;

    ForgeAudio_assert(config != NULL);

    /* We want to find, given an azimuth, which speakers are the closest
     * ones (in terms of angle) to that azimuth.
     * This is done by iterating through the list of speaker azimuths, as
     * given to us by the current ConfigInfo (which stores speaker azimuths
     * in increasing order of azimuth for each possible speaker configuration;
     * each speaker azimuth is defined to be between 0 and 2PI by construction).
     */
    for (i = 0; i < config->numNonLFSpeakers; i += 1)
    {
        /* a0 and a1 are the azimuths of candidate speakers */
        a0 = config->speakers[i].azimuth;
        nexti = (i + 1) % config->numNonLFSpeakers;
        a1 = config->speakers[nexti].azimuth;

        if (a0 < a1)
        {
            if (emitterAzimuth >= a0 && emitterAzimuth < a1)
            {
                break;
            }
        }
        /* It is possible for a speaker pair to enclose the singulary at 0 == 2PI:
         * consider for example the quad config, which has a front left speaker
         * at 7PI/4 and a front right speaker at PI/4. In that case a0 = 7PI/4 and
         * a1 = PI/4, and the way we know whether our current azimuth lies between
         * that pair is by checking whether the azimuth is greather than 7PI/4 or
         * whether it's less than PI/4. (By contract, currentAzimuth is always less
         * than 2PI.)
         */
        else
        {
            if (emitterAzimuth >= a0 || emitterAzimuth < a1)
            {
                break;
            }
        }
    }
    ForgeAudio_assert(emitterAzimuth >= a0 || emitterAzimuth < a1);

    /* skipCenter means that we don't want to use the center speaker.
     * The easiest way to deal with this is to check whether either of our candidate
     * speakers are the center, which always has an azimuth of 0.0. If that is the case
     * we just replace it with either the previous one or the next one.
     */
    if (skipCenter)
    {
        if (a0 == 0.0f)
        {
            if (i == 0)
            {
                i = config->numNonLFSpeakers - 1;
            }
            else
            {
                i -= 1;
            }
        }
        else if (a1 == 0.0f)
        {
            nexti += 1;
            if (nexti >= config->numNonLFSpeakers)
            {
                nexti -= config->numNonLFSpeakers;
            }
        }
    }
    speakerInfo[0] = &config->speakers[i];
    speakerInfo[1] = &config->speakers[nexti];
}

/* Used to store diffusion factors */
/* See below for explanation. */
#define DIFFUSION_SPEAKERS_ALL        0
#define DIFFUSION_SPEAKERS_MATCHING    1
#define DIFFUSION_SPEAKERS_OPPOSITE    2
typedef float DiffusionSpeakerFactors[3];

/* ComputeInnerRadiusDiffusionFactors is a utility function that returns how
 * energy dissipates to the speakers, given the radial distance between the
 * emitter and the listener and the (optionally 0) inner_radius distance. It
 * returns 3 floats, via the diffusionFactors array, that say how much energy
 * (after distance attenuation) will need to be distributed between each of the
 * following cases:
 *
 * - SPEAKERS_ALL for all (non-LF) speakers, _INCLUDING_ the MATCHING and OPPOSITE.
 * - SPEAKERS_OPPOSITE corresponds to the two speakers OPPOSITE the emitter.
 * - SPEAKERS_MATCHING corresponds to the two speakers closest to the emitter.
 *
 * For a distance below a certain threshold (DISTANCE_EQUAL_ENERGY), all
 * speakers receive equal energy.
 *
 * Above that, the amount that all speakers receive decreases linearly as radial
 * distance increases, up until inner_radius / 2. (If inner_radius is null, we use
 * MINIMUM_INNER_RADIUS.)
 *
 * At the same time, both opposite and matching speakers start to receive sound
 * (in addition to the energy they receive from the aforementioned "all
 * speakers" linear law) according to some unknown as of now law,
 * that is currently emulated with a LERP. This is true up until inner_radius.
 *
 * Above inner_radius, only the two matching speakers receive sound.
 *
 */
static inline void ComputeInnerRadiusDiffusionFactors(
    float radialDistance,
    float inner_radius,
    DiffusionSpeakerFactors diffusionFactors
) {

    /* Determined experimentally; this is the midpoint value, i.e. the
     * value at 0.5 for the matching speakers, used for the standard
     * diffusion curve.
     *
     * Note: It is SUSPICIOUSLY close to 1/sqrt(2), but I haven't figured out why.
     * -Adrien
     */
    #define DIFFUSION_LERP_MIDPOINT_VALUE 0.707107f

    /* Spatialization always uses an inner_radius-like behaviour (i.e. diffusing sound to more than
     * a pair of speakers) even if inner_radius is set to 0.0f.
     * This constant determines the distance at which this behaviour is produced in that case. */
    /* This constant was determined by manual binary search. TODO: get a more accurate version
     * via an automated binary search. */
    #define DIFFUSION_DISTANCE_MINIMUM_INNER_RADIUS 4e-7f
    float actualInnerRadius = ForgeAudio_max(inner_radius, DIFFUSION_DISTANCE_MINIMUM_INNER_RADIUS);
    float normalizedRadialDist;
    float a, ms, os;

    normalizedRadialDist = radialDistance / actualInnerRadius;

    /* Do another check for small radial distances before applying any inner_radius-like
     * behaviour. This is the constant that determines the threshold: below this distance we simply
     * diffuse to all speakers equally. */
    #define DIFFUSION_DISTANCE_EQUAL_ENERGY 1e-7f
    if (radialDistance <= DIFFUSION_DISTANCE_EQUAL_ENERGY)
    {
        a = 1.0f;
        ms = 0.0f;
        os = 0.0f;
    }
    else if (normalizedRadialDist <= 0.5f)
    {
        /* Determined experimentally that this is indeed a linear law,
         * with 100% confidence.
         * -Adrien
         */
        a = 1.0f - 2.0f * normalizedRadialDist;

        /* Lerping here is an approximation.
         * TODO: High accuracy version. Having stared at the curves long
         * enough, I'm pretty sure this is a quadratic, but trying to
         * polyfit with numpy didn't give nice, round polynomial
         * coefficients...
         * -Adrien
         */
        ms = LERP(2.0f * normalizedRadialDist, 0.0f, DIFFUSION_LERP_MIDPOINT_VALUE);
        os = 1.0f - a - ms;
    }
    else if (normalizedRadialDist <= 1.0f)
    {
        a = 0.0f;

        /* Similarly, this is a lerp based on the midpoint value; the
         * real, high-accuracy curve also looks like a quadratic.
         * -Adrien
         */
        ms = LERP(2.0f * (normalizedRadialDist - 0.5f), DIFFUSION_LERP_MIDPOINT_VALUE, 1.0f);
        os = 1.0f - a - ms;
    }
    else
    {
        a = 0.0f;
        ms = 1.0f;
        os = 0.0f;
    }
    diffusionFactors[DIFFUSION_SPEAKERS_ALL] = a;
    diffusionFactors[DIFFUSION_SPEAKERS_MATCHING] = ms;
    diffusionFactors[DIFFUSION_SPEAKERS_OPPOSITE] = os;
}

/* ComputeEmitterChannelCoefficients handles the coefficients calculation for 1
 * column of the matrix. It uses ComputeInnerRadiusDiffusionFactors to separate
 * into three discrete cases; and for each case does the right repartition of
 * the energy after attenuation to the right speakers, in particular in the
 * MATCHING and OPPOSITE cases, it gives each of the two speakers found a linear
 * amount of the energy, according to the angular distance between the emitter
 * and the speaker azimuth.
 */
static inline void ComputeEmitterChannelCoefficients(
    const ConfigInfo *curConfig,
    const ForgeSpatialBasis *listenerBasis,
    float innerRadius,
    ForgeVector3 channelPosition,
    float attenuation,
    float LFEattenuation,
    uint32_t flags,
    uint32_t currentChannel,
    uint32_t numSrcChannels,
    float *matrix_coefficients
) {
    float elevation, radialDistance;
    ForgeVector3 projTopVec, projPlane;
    uint8_t skipCenter = (flags & FORGE_SPATIAL_CALCULATE_ZERO_CENTER) ? 1 : 0;
    DiffusionSpeakerFactors diffusionFactors = { 0.0f };

    float x, y;
    float emitterAzimuth;
    float energyPerChannel;
    float totalEnergy;
    uint32_t channels_to_diffuse_to;
    uint32_t iS, centerChannelIdx = -1;
    const SpeakerInfo* infos[2];
    float a0, a1, val;
    uint32_t i0, i1;

    /* We project against the listener basis' top vector to get the elevation of the
     * current emitter channel position.
     */
    elevation = VectorDot(listenerBasis->top, channelPosition);

    /* To obtain the projection in the front-right plane of the listener's basis of the
     * emitter channel position, we simply remove the projection against the top vector.
     * The radial distance is then the length of the projected vector.
     */
    projTopVec = VectorScale(listenerBasis->top, elevation);
    projPlane = VectorSub(channelPosition, projTopVec);
    radialDistance = VectorLength(projPlane);

    ComputeInnerRadiusDiffusionFactors(
        radialDistance,
        innerRadius,
        diffusionFactors
    );

    /* See the ComputeInnerRadiusDiffusionFactors comment above for more context. */
    /* DIFFUSION_SPEAKERS_ALL corresponds to diffusing part of the sound to all of the
     * speakers, equally. The amount of sound is determined by the float value
     * diffusionFactors[DIFFUSION_SPEAKERS_ALL]. */
    if (diffusionFactors[DIFFUSION_SPEAKERS_ALL] > 0.0f)
    {
        channels_to_diffuse_to = curConfig->numNonLFSpeakers;
        totalEnergy = diffusionFactors[DIFFUSION_SPEAKERS_ALL] * attenuation;

        if (skipCenter)
        {
            channels_to_diffuse_to -= 1;
            ForgeAudio_assert(curConfig->speakers[0].azimuth == FORGE_SPEAKER_AZIMUTH_CENTER);
            centerChannelIdx = curConfig->speakers[0].matrixIdx;
        }

        energyPerChannel = totalEnergy / channels_to_diffuse_to;

        for (iS = 0; iS < curConfig->numNonLFSpeakers; iS += 1)
        {
            const uint32_t curSpeakerIdx = curConfig->speakers[iS].matrixIdx;
            if (skipCenter && curSpeakerIdx == centerChannelIdx)
            {
                continue;
            }

            matrix_coefficients[curSpeakerIdx * numSrcChannels + currentChannel] += energyPerChannel;
        }
    }

    /* DIFFUSION_SPEAKERS_MATCHING corresponds to sending part of the sound to the speakers closest
     * (in terms of azimuth) to the current position of the emitter. The amount of sound we shoud send
     * corresponds here to diffusionFactors[DIFFUSION_SPEAKERS_MATCHING].
     * We use the FindSpeakerAzimuths function to find the speakers that match. */
    if (diffusionFactors[DIFFUSION_SPEAKERS_MATCHING] > 0.0f)
    {
        const float totalEnergy = diffusionFactors[DIFFUSION_SPEAKERS_MATCHING] * attenuation;

        x = VectorDot(listenerBasis->front, projPlane);
        y = VectorDot(listenerBasis->right, projPlane);

        /* Now, a critical point: We shouldn't be sending sound to
         * matching speakers when x and y are close to 0. That's the
         * contract we get from ComputeInnerRadiusDiffusionFactors,
         * which checks that we're not too close to the zero distance.
         * This allows the atan2 calculation to give good results.
         */

        /* atan2 returns [-PI, PI], but we want [0, 2PI] */
        emitterAzimuth = ForgeAudio_atan2f(y, x);
        if (emitterAzimuth < 0.0f)
        {
            emitterAzimuth += FORGE_SPATIAL_2PI;
        }

        FindSpeakerAzimuths(curConfig, emitterAzimuth, skipCenter, infos);
        a0 = infos[0]->azimuth;
        a1 = infos[1]->azimuth;

        /* The following code is necessary to handle the singularity in
         * (0 == 2PI). It'll give us a nice, well ordered interval.
         */
        if (a0 > a1)
        {
            if (emitterAzimuth >= a0)
            {
                emitterAzimuth -= FORGE_SPATIAL_2PI;
            }
            a0 -= FORGE_SPATIAL_2PI;
        }
        ForgeAudio_assert(emitterAzimuth >= a0 && emitterAzimuth <= a1);

        val = (emitterAzimuth - a0) / (a1 - a0);

        i0 = infos[0]->matrixIdx;
        i1 = infos[1]->matrixIdx;

        matrix_coefficients[i0 * numSrcChannels + currentChannel] += (1.0f - val) * totalEnergy;
        matrix_coefficients[i1 * numSrcChannels + currentChannel] += (       val) * totalEnergy;
    }

    /* DIFFUSION_SPEAKERS_OPPOSITE corresponds to sending part of the sound to the speakers
     * _opposite_ the ones that are the closest to the current emitter position.
     * To find these, we simply find the ones that are closest to the current emitter's azimuth + PI
     * using the FindSpeakerAzimuth function. */
    if (diffusionFactors[DIFFUSION_SPEAKERS_OPPOSITE] > 0.0f)
    {
        /* This code is similar to the matching speakers code above. */
        const float totalEnergy = diffusionFactors[DIFFUSION_SPEAKERS_OPPOSITE] * attenuation;

        x = VectorDot(listenerBasis->front, projPlane);
        y = VectorDot(listenerBasis->right, projPlane);

        /* Similarly, we expect atan2 to be well behaved here. */
        emitterAzimuth = ForgeAudio_atan2f(y, x);

        /* Opposite speakers lie at azimuth + PI */
        emitterAzimuth += FORGE_SPATIAL_PI;

        /* Normalize to [0; 2PI) range. */
        if (emitterAzimuth < 0.0f)
        {
            emitterAzimuth += FORGE_SPATIAL_2PI;
        }
        else if (emitterAzimuth > FORGE_SPATIAL_2PI)
        {
            emitterAzimuth -= FORGE_SPATIAL_2PI;
        }

        FindSpeakerAzimuths(curConfig, emitterAzimuth, skipCenter, infos);
        a0 = infos[0]->azimuth;
        a1 = infos[1]->azimuth;

        /* The following code is necessary to handle the singularity in
         * (0 == 2PI). It'll give us a nice, well ordered interval.
         */
        if (a0 > a1)
        {
            if (emitterAzimuth >= a0)
            {
                emitterAzimuth -= FORGE_SPATIAL_2PI;
            }
            a0 -= FORGE_SPATIAL_2PI;
        }
        ForgeAudio_assert(emitterAzimuth >= a0 && emitterAzimuth <= a1);

        val = (emitterAzimuth - a0) / (a1 - a0);

        i0 = infos[0]->matrixIdx;
        i1 = infos[1]->matrixIdx;

        matrix_coefficients[i0 * numSrcChannels + currentChannel] += (1.0f - val) * totalEnergy;
        matrix_coefficients[i1 * numSrcChannels + currentChannel] += (       val) * totalEnergy;
    }

    if (flags & FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE)
    {
        ForgeAudio_assert(curConfig->LFSpeakerIdx != -1);
        matrix_coefficients[curConfig->LFSpeakerIdx * numSrcChannels + currentChannel] += LFEattenuation / numSrcChannels;
    }
}

/* Calculations consist of several orthogonal steps that compose multiplicatively:
 *
 * First, we compute the attenuations (volume and LFE) due to distance, which
 * may involve an optional volume and/or LFE volume curve.
 *
 * Then, we compute those due to optional cones.
 *
 * We then compute how much energy is diffuse w.r.t inner_radius. If inner_radius
 * is 0.0f, this step is computed as if it was inner_radius was
 * NON_NULL_DISTANCE_DISK_RADIUS. The way this works is, we look at the radial
 * distance of the current emitter channel to the listener, with regard to the
 * listener's top orientation (i.e. this distance is independant of the
 * emitter's elevation!). If this distance is less than NULL_DISTANCE_RADIUS,
 * energy is diffused equally between all channels. If it's greater than
 * inner_radius (or NON_NULL_DISTANCE_RADIUS, if inner_radius is 0.0f, as
 * mentioned above), the two closest speakers, by azimuth, receive all the
 * energy. Between inner_radius/2.0f and inner_radius, the energy starts bleeding
 * into the opposite speakers. Once we go below inner_radius/2.0f, the energy
 * also starts to bleed into the other (non-opposite) channels, if there are
 * any. This computation is handled by the ComputeInnerRadiusDiffusionFactors
 * function. (TODO: High-accuracy version of this.)
 *
 * Finally, if we're not in the equal diffusion case, we find out the azimuths
 * of the two closest speakers (with azimuth being defined with respect to the
 * listener's front orientation, in the plane normal to the listener's top
 * vector), as well as the azimuths of the two opposite speakers, if necessary,
 * and linearly interpolate with respect to the angular distance. In the equal
 * diffusion case, each channel receives the same value.
 *
 * Note: in the case of multi-channel emitters, the distance attenuation is only
 * computed once, but all the azimuths and inner_radius calculations are done per
 * emitter channel.
 *
 * TODO: Handle inner_radius_angle.
 * -Adrien
 */
static inline void CalculateMatrix(
    uint32_t ChannelMask,
    uint32_t flags,
    const ForgeSpatialListener *listener,
    const ForgeSpatialEmitter *emitter,
    uint32_t src_channel_count,
    uint32_t dst_channel_count,
    ForgeVector3 emitterToListener,
    float eToLDistance,
    float normalizedDistance,
    float* MatrixCoefficients
) {
    uint32_t iEC;
    float curEmAzimuth;
    const ConfigInfo* curConfig = GetConfigInfo(ChannelMask);
    float attenuation = ComputeDistanceAttenuation(
        normalizedDistance,
        emitter->volume_curve
    );
    /* TODO: this could be skipped if the destination has no LFE */
    float LFEattenuation = ComputeDistanceAttenuation(
        normalizedDistance,
        emitter->lfe_curve
    );

    ForgeVector3 listenerToEmitter;
    ForgeVector3 listenerToEmChannel;
    ForgeSpatialBasis listenerBasis;

    /* Note: For both cone calculations, the angle might be NaN or infinite
     * if distance == 0... ComputeConeParameter *does* check for this
     * special case. It is necessary that we still go through the
     * ComputeConeParameter function, because omnidirectional cones might
     * give either inner_volume or outer_volume.
     * -Adrien
     */
    if (listener->cone)
    {
        /* Negate the dot product because we need listenerToEmitter in
         * this case
         * -Adrien
         */
        const float angle = -ForgeAudio_acosf(
            VectorDot(listener->orient_front, emitterToListener) /
            eToLDistance
        );

        const float listenerConeParam = ComputeConeParameter(
            eToLDistance,
            angle,
            listener->cone->inner_angle,
            listener->cone->outer_angle,
            listener->cone->inner_volume,
            listener->cone->outer_volume
        );
        attenuation *= listenerConeParam;
        LFEattenuation *= listenerConeParam;
    }

    /* See note above. */
    if (emitter->cone && emitter->channel_count == 1)
    {
        const float angle = ForgeAudio_acosf(
            VectorDot(emitter->orient_front, emitterToListener) /
            eToLDistance
        );

        const float emitterConeParam = ComputeConeParameter(
            eToLDistance,
            angle,
            emitter->cone->inner_angle,
            emitter->cone->outer_angle,
            emitter->cone->inner_volume,
            emitter->cone->outer_volume
        );
        attenuation *= emitterConeParam;
    }

    ForgeAudio_zero(MatrixCoefficients, sizeof(float) * src_channel_count * dst_channel_count);

    /* In the FORGE_SPEAKER_MONO case, we can skip all energy diffusion calculation. */
    if (dst_channel_count == 1)
    {
        for (iEC = 0; iEC < emitter->channel_count; iEC += 1)
        {
            curEmAzimuth = 0.0f;
            if (emitter->channel_azimuths)
            {
                curEmAzimuth = emitter->channel_azimuths[iEC];
            }

            /* The MONO setup doesn't have an LFE speaker. */
            if (curEmAzimuth != FORGE_SPATIAL_2PI)
            {
                MatrixCoefficients[iEC] = attenuation;
            }
        }
    }
    else if (curConfig != NULL)
    {
        listenerToEmitter = VectorScale(emitterToListener, -1.0f);

        /* Remember here that the coordinate system is Left-Handed. */
        listenerBasis.front = listener->orient_front;
        listenerBasis.right = VectorCross(listener->orient_top, listener->orient_front);
        listenerBasis.top = listener->orient_top;


        /* Handling the mono-channel emitter case separately is easier
         * than having it as a separate case of a for-loop; indeed, in
         * this case, we need to ignore the non-relevant values from the
         * emitter, _even if they're set_.
         */
        if (emitter->channel_count == 1)
        {
            listenerToEmChannel = listenerToEmitter;

            ComputeEmitterChannelCoefficients(
                curConfig,
                &listenerBasis,
                emitter->inner_radius,
                listenerToEmChannel,
                attenuation,
                LFEattenuation,
                flags,
                0 /* currentChannel */,
                1 /* numSrcChannels */,
                MatrixCoefficients
            );
        }
        else /* Multi-channel emitter case. */
        {
            const ForgeVector3 emitterRight = VectorCross(emitter->orient_top, emitter->orient_front);

            for (iEC = 0; iEC < emitter->channel_count; iEC += 1)
            {
                const float emChAzimuth = emitter->channel_azimuths[iEC];

                /* LFEs are easy enough to deal with; we can
                 * just do them separately.
                 */
                if (emChAzimuth == FORGE_SPATIAL_2PI)
                {
                    MatrixCoefficients[curConfig->LFSpeakerIdx * emitter->channel_count + iEC] = LFEattenuation;
                }
                else
                {
                    /* First compute the emitter channel
                     * vector relative to the emitter base...
                     */
                    const ForgeVector3 emitterBaseToChannel = VectorAdd(
                        VectorScale(emitter->orient_front, emitter->channel_radius * ForgeAudio_cosf(emChAzimuth)),
                        VectorScale(emitterRight, emitter->channel_radius * ForgeAudio_sinf(emChAzimuth))
                    );
                    /* ... then translate. */
                    listenerToEmChannel = VectorAdd(
                        listenerToEmitter,
                        emitterBaseToChannel
                    );

                    ComputeEmitterChannelCoefficients(
                        curConfig,
                        &listenerBasis,
                        emitter->inner_radius,
                        listenerToEmChannel,
                        attenuation,
                        LFEattenuation,
                        flags,
                        iEC,
                        emitter->channel_count,
                        MatrixCoefficients
                    );
                }
            }
        }
    }
    else
    {
        ForgeAudio_assert(0 && "Config info not found!");
    }

    /* TODO: add post check to validate values
     * (sum < 1, all values > 0, no Inf / NaN..
     * Sum can be >1 when cone or curve is set to a gain!
     * Perhaps under a paranoid check disabled by default.
     */
}

/*
 * OTHER CALCULATIONS
 */

/* DopplerPitchScalar
 * Adapted from algorithm published as a part of the webaudio specification:
 * https://dvcs.w3.org/hg/audio/raw-file/tip/webaudio/specification.html#Spatialization-doppler-shift
 * -Chad
 */
static inline void CalculateDoppler(
    float speed_of_sound,
    const ForgeSpatialListener* listener,
    const ForgeSpatialEmitter* emitter,
    ForgeVector3 emitterToListener,
    float eToLDistance,
    float* listenerVelocityComponent,
    float* emitterVelocityComponent,
    float* doppler_factor
) {
    float scaledSpeedOfSound;
    *doppler_factor = 1.0f;

    /* Project... */
    if (eToLDistance != 0.0f)
    {
        *listenerVelocityComponent =
            VectorDot(emitterToListener, listener->velocity) / eToLDistance;
        *emitterVelocityComponent =
            VectorDot(emitterToListener, emitter->velocity) / eToLDistance;
    }
    else
    {
        *listenerVelocityComponent = 0.0f;
        *emitterVelocityComponent = 0.0f;
    }

    if (emitter->doppler_scaler > 0.0f)
    {
        scaledSpeedOfSound = speed_of_sound / emitter->doppler_scaler;

        /* Clamp... */
        *listenerVelocityComponent = ForgeAudio_min(
            *listenerVelocityComponent,
            scaledSpeedOfSound
        );
        *emitterVelocityComponent = ForgeAudio_min(
            *emitterVelocityComponent,
            scaledSpeedOfSound
        );

        /* ... then Multiply. */
        *doppler_factor = (
            speed_of_sound - emitter->doppler_scaler * *listenerVelocityComponent
        ) / (
            speed_of_sound - emitter->doppler_scaler * *emitterVelocityComponent
        );
        if (isnan(*doppler_factor)) /* If emitter/listener are at the same pos... */
        {
            *doppler_factor = 1.0f;
        }

        /* Limit the pitch shifting to 2 octaves up and 1 octave down */
        *doppler_factor = ForgeAudio_clamp(
            *doppler_factor,
            0.5f,
            4.0f
        );
    }
}

void forge_spatializer_calculate(
    const ForgeSpatializer *spatializer,
    const ForgeSpatialListener *listener,
    const ForgeSpatialEmitter *emitter,
    uint32_t flags,
    ForgeSpatialDspSettings *dsp_settings
) {
    uint32_t i;
    ForgeVector3 emitterToListener;
    float eToLDistance, normalizedDistance, dp;

    #define DEFAULT_POINTS(name, x1, y1, x2, y2) \
        static ForgeSpatialDistanceCurvePoint name##Points[2] = \
        { \
            { x1, y1 }, \
            { x2, y2 } \
        }; \
        static ForgeSpatialDistanceCurve name##Default = \
        { \
            (ForgeSpatialDistanceCurvePoint*) &name##Points[0], 2 \
        };
    DEFAULT_POINTS(lpfDirect, 0.0f, 1.0f, 1.0f, 0.75f)
    DEFAULT_POINTS(lpfReverb, 0.0f, 0.75f, 1.0f, 0.75f)
    DEFAULT_POINTS(reverb, 0.0f, 1.0f, 1.0f, 0.0f)
    #undef DEFAULT_POINTS

    /* For XACT, this calculates "distance" */
    emitterToListener = VectorSub(listener->position, emitter->position);
    eToLDistance = VectorLength(emitterToListener);
    dsp_settings->emitter_to_listener_distance = eToLDistance;

    forge_spatializer_check_calculate_params(spatializer, listener, emitter, flags, dsp_settings);

    /* This is used by MATRIX, LPF, and REVERB */
    normalizedDistance = eToLDistance / emitter->curve_distance_scaler;

    if (flags & FORGE_SPATIAL_CALCULATE_MATRIX)
    {
        CalculateMatrix(
            SPEAKERMASK(spatializer),
            flags,
            listener,
            emitter,
            dsp_settings->src_channel_count,
            dsp_settings->dst_channel_count,
            emitterToListener,
            eToLDistance,
            normalizedDistance,
            dsp_settings->matrix_coefficients
        );
    }

    if (flags & FORGE_SPATIAL_CALCULATE_LPF_DIRECT)
    {
        dsp_settings->lpf_direct_coefficient = ComputeDistanceAttenuation(
            normalizedDistance,
            (emitter->lpf_direct_curve != NULL) ?
                emitter->lpf_direct_curve :
                &lpfDirectDefault
        );
    }

    if (flags & FORGE_SPATIAL_CALCULATE_LPF_REVERB)
    {
        dsp_settings->lpf_reverb_coefficient = ComputeDistanceAttenuation(
            normalizedDistance,
            (emitter->lpf_reverb_curve != NULL) ?
                emitter->lpf_reverb_curve :
                &lpfReverbDefault
        );
    }

    if (flags & FORGE_SPATIAL_CALCULATE_REVERB)
    {
        dsp_settings->reverb_level = ComputeDistanceAttenuation(
            normalizedDistance,
            (emitter->reverb_curve != NULL) ?
                emitter->reverb_curve :
                &reverbDefault
        );
    }

    /* For XACT, this calculates "DopplerPitchScalar" */
    if (flags & FORGE_SPATIAL_CALCULATE_DOPPLER)
    {
        CalculateDoppler(
            SPEEDOFSOUND(spatializer),
            listener,
            emitter,
            emitterToListener,
            eToLDistance,
            &dsp_settings->listener_velocity_component,
            &dsp_settings->emitter_velocity_component,
            &dsp_settings->doppler_factor
        );
    }

    /* For XACT, this calculates "OrientationAngle" */
    if (flags & FORGE_SPATIAL_CALCULATE_EMITTER_ANGLE)
    {
        /* Determined roughly.
         * Below that distance, the emitter angle is considered to be PI/2.
         */
        #define EMITTER_ANGLE_NULL_DISTANCE 1.2e-7
        if (eToLDistance < EMITTER_ANGLE_NULL_DISTANCE)
        {
            dsp_settings->emitter_to_listener_angle = FORGE_SPATIAL_PI / 2.0f;
        }
        else
        {
            /* Note: emitter->orient_front is normalized. */
            dp = VectorDot(emitterToListener, emitter->orient_front) / eToLDistance;
            dsp_settings->emitter_to_listener_angle = ForgeAudio_acosf(dp);
        }
    }

    /* Unimplemented flags */
    if (    (flags & FORGE_SPATIAL_CALCULATE_DELAY) &&
        SPEAKERMASK(spatializer) == FORGE_SPEAKER_STEREO    )
    {
        for (i = 0; i < dsp_settings->dst_channel_count; i += 1)
        {
            dsp_settings->delay_times[i] = 0.0f;
        }
        ForgeAudio_assert(0 && "DELAY not implemented!");
    }
}
