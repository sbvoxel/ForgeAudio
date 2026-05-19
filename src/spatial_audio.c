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

#include <forge/spatial_audio.h>
#include "common_internal.h"

#include <math.h>  /* ONLY USE THIS FOR isnan! */
#include <float.h> /* ONLY USE THIS FOR FLT_MIN/FLT_MAX! */

/* UTILITY MACROS */

#define PARAM_CHECK_OK 1
#define PARAM_CHECK_FAIL (!PARAM_CHECK_OK)

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

#define LERP(a, x, y) ((1.0f - a) * x + a * y)

/* PARAMETER CHECK MACROS */

#define PARAM_CHECK(cond, msg) forge_assert(cond &&msg)

#define POINTER_CHECK(p) PARAM_CHECK(p != NULL, "Pointer " #p " must be != NULL")

#define FLOAT_BETWEEN_CHECK(f, a, b)                                                                                   \
    PARAM_CHECK(f >= a, "Value" #f " is too low");                                                                     \
    PARAM_CHECK(f <= b, "Value" #f " is too big")

/* Spatial vectors are treated as orthonormal when their magnitude is within
 * 1e-5 of 1.0 and their dot product is within 1e-5 of zero.
 */

/* Potential optimization: compare squared length to avoid sqrt in vector validation. */
#define VECTOR_NORMAL_CHECK(v) PARAM_CHECK(forge_fabsf(VECTOR_LENGTH(v) - 1.0f) <= 1e-5f, "Vector " #v " isn't normal")

#define VECTOR_BASE_CHECK(u, v)                                                                                        \
    PARAM_CHECK(forge_fabsf(VECTOR_DOT(u, v)) <= 1e-5f, "Vector u and v have non-negligible dot product")

/******************************************
 * forge_spatializer_init Implementation *
 ******************************************/

#define SPEAKERMASK(spatializer) ((spatializer)->speaker_channel_mask)
#define SPEAKERCOUNT(spatializer) ((spatializer)->speaker_count)
#define FORGE_SPEAKER_LF_INDEX(spatializer) ((spatializer)->low_frequency_channel_index)
#define SPEEDOFSOUND(spatializer) ((spatializer)->speed_of_sound)
#define SPEEDOFSOUNDEPSILON(spatializer) ((spatializer)->speed_of_sound_epsilon)

static bool check_init_params(uint32_t speaker_channel_mask, float speed_of_sound,
                                             ForgeSpatializer *spatializer) {
    const uint32_t allowed_speaker_masks[] = {
        FORGE_SPEAKER_MONO,     FORGE_SPEAKER_STEREO,           FORGE_SPEAKER_2POINT1, FORGE_SPEAKER_QUAD,
        FORGE_SPEAKER_SURROUND, FORGE_SPEAKER_4POINT1,          FORGE_SPEAKER_5POINT1, FORGE_SPEAKER_5POINT1_SURROUND,
        FORGE_SPEAKER_7POINT1,  FORGE_SPEAKER_7POINT1_SURROUND,
    };
    uint8_t speaker_mask_is_valid = 0;
    uint32_t i;

    if (spatializer == NULL) {
        PARAM_CHECK(spatializer != NULL, "spatializer must be != NULL");
        return false;
    }

    for (i = 0; i < ARRAY_COUNT(allowed_speaker_masks); i += 1) {
        if (speaker_channel_mask == allowed_speaker_masks[i]) {
            speaker_mask_is_valid = 1;
            break;
        }
    }

    /* The docs don't clearly say it, but the debug dll does check that
     * we're exactly in one of the allowed speaker configurations.
     * -Adrien
     */
    if (!speaker_mask_is_valid) {
        PARAM_CHECK(speaker_mask_is_valid == 1, "speaker_channel_mask is invalid. Needs to be one of"
                                             " MONO, STEREO, QUAD, 2POINT1, 4POINT1, 5POINT1, 7POINT1,"
                                             " SURROUND, 5POINT1_SURROUND, or 7POINT1_SURROUND.");
        return false;
    }

    if (speed_of_sound < FLT_MIN) {
        PARAM_CHECK(speed_of_sound >= FLT_MIN, "speed_of_sound needs to be >= FLT_MIN");
        return false;
    }

    return true;
}

bool forge_spatializer_init(uint32_t speaker_channel_mask, float speed_of_sound, ForgeSpatializer *spatializer) {
    union {
        float f;
        uint32_t i;
    } epsilon_hack;
    uint32_t speaker_count = 0;

    if (!check_init_params(speaker_channel_mask, speed_of_sound, spatializer)) {
        return false;
    }

    SPEAKERMASK(spatializer) = speaker_channel_mask;
    SPEEDOFSOUND(spatializer) = speed_of_sound;

    /* "Convert" raw float to int... */
    epsilon_hack.f = speed_of_sound;
    /* ... Subtract epsilon value... */
    epsilon_hack.i -= 1;
    /* ... Convert back to float. */
    SPEEDOFSOUNDEPSILON(spatializer) = epsilon_hack.f;

    FORGE_SPEAKER_LF_INDEX(spatializer) = 0xFFFFFFFF;
    if (speaker_channel_mask & FORGE_SPEAKER_LOW_FREQUENCY) {
        if (speaker_channel_mask & FORGE_SPEAKER_FRONT_CENTER) {
            FORGE_SPEAKER_LF_INDEX(spatializer) = 3;
        } else {
            FORGE_SPEAKER_LF_INDEX(spatializer) = 2;
        }
    }

    while (speaker_channel_mask) {
        speaker_count += 1;
        speaker_channel_mask &= speaker_channel_mask - 1;
    }
    SPEAKERCOUNT(spatializer) = speaker_count;

    return true;
}

/*********************************************
 * forge_spatializer_calculate Implementation *
 *********************************************/

/* VECTOR UTILITIES */

static inline ForgeVector3 vector_make(float x, float y, float z) {
    ForgeVector3 res;
    res.x = x;
    res.y = y;
    res.z = z;
    return res;
}

#define VECTOR_ADD(u, v) vector_make(u.x + v.x, u.y + v.y, u.z + v.z)

#define VECTOR_SUB(u, v) vector_make(u.x - v.x, u.y - v.y, u.z - v.z)

#define VECTOR_SCALE(u, s) vector_make(u.x *s, u.y *s, u.z *s)

#define VECTOR_CROSS(u, v) vector_make((u.y * v.z) - (u.z * v.y), (u.z * v.x) - (u.x * v.z), (u.x * v.y) - (u.y * v.x))

#define VECTOR_LENGTH(v) forge_sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z))

#define VECTOR_DOT(u, v) ((u.x * v.x) + (u.y * v.y) + (u.z * v.z))

/* This structure represent a tuple of vectors that form a left-handed basis.
 * That is, all vectors are normal, orthogonal to each other, and taken in the
 * order front, right, top they follow the left-hand rule.
 * (https://en.wikipedia.org/wiki/Right-hand_rule)
 */
typedef struct ForgeSpatialBasis {
    ForgeVector3 front;
    ForgeVector3 right;
    ForgeVector3 top;
} ForgeSpatialBasis;

/* CHECK UTILITY FUNCTIONS */

static inline uint8_t check_cone(ForgeSpatialCone *cone) {
    if (!cone) {
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

static inline uint8_t check_curve(ForgeSpatialDistanceCurve *curve) {
    ForgeSpatialDistanceCurvePoint *points;
    uint32_t i;
    if (!curve) {
        return PARAM_CHECK_OK;
    }

    points = curve->points;
    POINTER_CHECK(points);
    PARAM_CHECK(curve->point_count >= 2, "Invalid number of points for curve");

    for (i = 0; i < curve->point_count; i += 1) {
        FLOAT_BETWEEN_CHECK(points[i].distance, 0.0f, 1.0f);
    }

    PARAM_CHECK(points[0].distance == 0.0f, "First point in the curve must be at distance 0.0f");
    PARAM_CHECK(points[curve->point_count - 1].distance == 1.0f, "Last point in the curve must be at distance 1.0f");

    for (i = 0; i < (curve->point_count - 1); i += 1) {
        PARAM_CHECK(points[i].distance < points[i + 1].distance, "Curve points must be in strict ascending order");
    }

    return PARAM_CHECK_OK;
}

static uint8_t check_calculate_params(const ForgeSpatializer *spatializer,
                                                     const ForgeSpatialListener *listener,
                                                     const ForgeSpatialEmitter *emitter, uint32_t flags,
                                                     ForgeSpatialDspSettings *dsp_settings) {
    uint32_t i, channel_count;

    POINTER_CHECK(spatializer);
    POINTER_CHECK(listener);
    POINTER_CHECK(emitter);
    POINTER_CHECK(dsp_settings);

    if (flags & FORGE_SPATIAL_CALCULATE_MATRIX) {
        POINTER_CHECK(dsp_settings->matrix_coefficients);
    }
    if (flags & FORGE_SPATIAL_CALCULATE_ZERO_CENTER) {
        const uint32_t is_calculate_matrix = (flags & FORGE_SPATIAL_CALCULATE_MATRIX);
        const uint32_t has_center = SPEAKERMASK(spatializer) & FORGE_SPEAKER_FRONT_CENTER;
        PARAM_CHECK(is_calculate_matrix && has_center, "FORGE_SPATIAL_CALCULATE_ZERO_CENTER is only valid for matrix"
                                                    " calculations with an output format that has a center channel");
    }

    if (flags & FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE) {
        const uint32_t is_calculate_matrix = (flags & FORGE_SPATIAL_CALCULATE_MATRIX);
        const uint32_t has_lf = SPEAKERMASK(spatializer) & FORGE_SPEAKER_LOW_FREQUENCY;
        PARAM_CHECK(is_calculate_matrix && has_lf, "FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE is only valid for matrix"
                                                " calculations with an output format that has a low-frequency"
                                                " channel");
    }

    channel_count = SPEAKERCOUNT(spatializer);
    PARAM_CHECK(dsp_settings->dst_channel_count == channel_count,
                "Invalid channel count, DSP settings and speaker configuration must agree");
    PARAM_CHECK(dsp_settings->src_channel_count == emitter->channel_count,
                "Invalid channel count, DSP settings and emitter must agree");

    if (listener->cone) {
        PARAM_CHECK(check_cone(listener->cone) == PARAM_CHECK_OK, "Invalid listener cone");
    }
    VECTOR_NORMAL_CHECK(listener->orient_front);
    VECTOR_NORMAL_CHECK(listener->orient_top);
    VECTOR_BASE_CHECK(listener->orient_front, listener->orient_top);

    if (emitter->cone) {
        VECTOR_NORMAL_CHECK(emitter->orient_front);
        PARAM_CHECK(check_cone(emitter->cone) == PARAM_CHECK_OK, "Invalid emitter cone");
    } else if (flags & FORGE_SPATIAL_CALCULATE_EMITTER_ANGLE) {
        VECTOR_NORMAL_CHECK(emitter->orient_front);
    }
    if (emitter->channel_count > 1) {
        /* Only used for multi-channel emitters */
        VECTOR_NORMAL_CHECK(emitter->orient_front);
        VECTOR_NORMAL_CHECK(emitter->orient_top);
        VECTOR_BASE_CHECK(emitter->orient_front, emitter->orient_top);
    }
    FLOAT_BETWEEN_CHECK(emitter->inner_radius, 0.0f, FLT_MAX);
    PARAM_CHECK(emitter->channel_count > 0, "Invalid channel count for emitter");
    PARAM_CHECK(emitter->channel_radius >= 0.0f, "Invalid channel radius for emitter");
    if (emitter->channel_count > 1) {
        PARAM_CHECK(emitter->channel_azimuths != NULL, "Invalid channel azimuths for multi-channel emitter");
        if (emitter->channel_azimuths) {
            for (i = 0; i < emitter->channel_count; i += 1) {
                float current_azimuth = emitter->channel_azimuths[i];
                FLOAT_BETWEEN_CHECK(current_azimuth, 0.0f, FORGE_SPATIAL_2PI);
                if (current_azimuth == FORGE_SPATIAL_2PI) {
                    PARAM_CHECK(!(flags & FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE),
                                "FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE valid only for"
                                " matrix calculations with emitters that have no LFE"
                                " channel");
                }
            }
        }
    }
    FLOAT_BETWEEN_CHECK(emitter->curve_distance_scaler, FLT_MIN, FLT_MAX);
    FLOAT_BETWEEN_CHECK(emitter->doppler_scaler, 0.0f, FLT_MAX);

    PARAM_CHECK(check_curve(emitter->volume_curve) == PARAM_CHECK_OK, "Invalid volume curve");
    PARAM_CHECK(check_curve(emitter->lfe_curve) == PARAM_CHECK_OK, "Invalid LFE curve");
    PARAM_CHECK(check_curve(emitter->lpf_direct_curve) == PARAM_CHECK_OK, "Invalid LPFDirect curve");
    PARAM_CHECK(check_curve(emitter->lpf_reverb_curve) == PARAM_CHECK_OK, "Invalid LPFReverb curve");
    PARAM_CHECK(check_curve(emitter->reverb_curve) == PARAM_CHECK_OK, "Invalid reverb curve");

    return PARAM_CHECK_OK;
}

/*
 * MATRIX CALCULATION
 */

/* This function computes the distance either according to a curve if curve
 * isn't NULL, or according to the inverse distance law 1/d otherwise.
 */
static inline float compute_distance_attenuation(float normalized_distance, ForgeSpatialDistanceCurve *curve) {
    float res;
    float alpha;
    uint32_t n_points;
    size_t i;
    if (curve) {
        ForgeSpatialDistanceCurvePoint *points = curve->points;
        n_points = curve->point_count;

        /* By definition, the first point in the curve must be 0.0f
         * -Adrien
         */

        /* We advance i up until our normalized_distance lies between the distances of
         * the i_th and (i-1)_th points, or we reach the last point.
         */
        for (i = 1; (i < n_points) && (normalized_distance >= points[i].distance); i += 1)
            ;
        if (i == n_points) {
            /* We've reached the last point, so we use its value directly. */
            res = points[n_points - 1].dsp_setting;
        } else {
            /* We're between two points: the distance attenuation is the linear interpolation of the dsp_setting
             * values defined by our points, according to the distance.
             */
            alpha = (points[i].distance - normalized_distance) / (points[i].distance - points[i - 1].distance);
            res = LERP(alpha, points[i].dsp_setting, points[i - 1].dsp_setting);
        }
    } else {
        res = 1.0f;
        if (normalized_distance >= 1.0f) {
            res /= normalized_distance;
        }
    }
    return res;
}

static inline float compute_cone_parameter(float distance, float angle, float inner_angle, float outer_angle,
                                         float inner_param, float outer_param) {
/* When computing whether a point lies inside a cone, first determine
 * whether the point is close enough to the apex of the cone.
 * If it is, the inner_param is used.
 * The following empirical tolerance is used for this distance check. */
#define CONE_NULL_DISTANCE_TOLERANCE 1e-7

    float half_inner_angle, half_outer_angle, alpha;

    /* Both cone angles at 0 use the outer values; both at 2PI use the inner
     * values.
     */
    if (inner_angle == 0.0f && outer_angle == 0.0f) {
        return outer_param;
    }
    if (inner_angle == FORGE_SPATIAL_2PI && outer_angle == FORGE_SPATIAL_2PI) {
        return inner_param;
    }

    /* If we're within the inner angle, or close enough to the apex, we use
     * the inner_param. */
    half_inner_angle = inner_angle / 2.0f;
    if (distance <= CONE_NULL_DISTANCE_TOLERANCE || angle <= half_inner_angle) {
        return inner_param;
    }

    /* If we're between the inner angle and the outer angle, we must use
     * some interpolation of the inner_param and outer_param according to the
     * distance between our angle and the inner and outer angles.
     */
    half_outer_angle = outer_angle / 2.0f;
    if (angle <= half_outer_angle) {
        alpha = (angle - half_inner_angle) / (half_outer_angle - half_inner_angle);

        /* Linear interpolation is an empirical approximation for cone transition gain. */
        return LERP(alpha, inner_param, outer_param);
    }

    /* Otherwise, we're outside the outer angle, so we just return the outer param. */
    return outer_param;
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
typedef struct {
    float azimuth;
    uint32_t matrix_idx;
} SpeakerInfo;

typedef struct {
    uint32_t config_mask;
    const SpeakerInfo *speakers;

    /* Not strictly necessary because it can be inferred from the
     * speaker_count field of the spatializer, but makes code much
     * cleaner and less error prone
     */
    uint32_t num_non_lf_speakers;

    int32_t lf_speaker_idx;
} ConfigInfo;

/* It is absolutely necessary that these are stored in increasing, *positive*
 * azimuth order (i.e. all angles between [0; 2PI]), as we'll do a linear
 * interval search inside find_speaker_azimuths.
 * -Adrien
 */

#define FORGE_SPEAKER_AZIMUTH_CENTER 0.0f
#define FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT_OF_CENTER (FORGE_SPATIAL_PI * 1.0f / 8.0f)
#define FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT (FORGE_SPATIAL_PI * 1.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT (FORGE_SPATIAL_PI * 1.0f / 2.0f)
#define FORGE_SPEAKER_AZIMUTH_BACK_RIGHT (FORGE_SPATIAL_PI * 3.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_BACK_CENTER FORGE_SPATIAL_PI
#define FORGE_SPEAKER_AZIMUTH_BACK_LEFT (FORGE_SPATIAL_PI * 5.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_SIDE_LEFT (FORGE_SPATIAL_PI * 3.0f / 2.0f)
#define FORGE_SPEAKER_AZIMUTH_FRONT_LEFT (FORGE_SPATIAL_PI * 7.0f / 4.0f)
#define FORGE_SPEAKER_AZIMUTH_FRONT_LEFT_OF_CENTER (FORGE_SPATIAL_PI * 15.0f / 8.0f)

static const SpeakerInfo mono_config_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_CENTER, 0},
};
static const SpeakerInfo stereo_config_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_SIDE_LEFT, 0},
};
static const SpeakerInfo config_2point1_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_SIDE_LEFT, 0},
};
static const SpeakerInfo surround_config_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_CENTER, 2},
    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_BACK_CENTER, 3},
    {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
};
static const SpeakerInfo quad_config_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_BACK_RIGHT, 3},
    {FORGE_SPEAKER_AZIMUTH_BACK_LEFT, 2},
    {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
};
static const SpeakerInfo config_4point1_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_BACK_RIGHT, 4},
    {FORGE_SPEAKER_AZIMUTH_BACK_LEFT, 3},
    {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
};
static const SpeakerInfo config_5point1_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_CENTER, 2},    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1}, {FORGE_SPEAKER_AZIMUTH_BACK_RIGHT, 5},
    {FORGE_SPEAKER_AZIMUTH_BACK_LEFT, 4}, {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
};
static const SpeakerInfo config_7point1_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_CENTER, 2},
    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT_OF_CENTER, 7},
    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_BACK_RIGHT, 5},
    {FORGE_SPEAKER_AZIMUTH_BACK_LEFT, 4},
    {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
    {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT_OF_CENTER, 6},
};
static const SpeakerInfo config_5point1_surround_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_CENTER, 2},    {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1}, {FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT, 5},
    {FORGE_SPEAKER_AZIMUTH_SIDE_LEFT, 4}, {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
};
static const SpeakerInfo config_7point1_surround_speakers[] = {
    {FORGE_SPEAKER_AZIMUTH_CENTER, 2},     {FORGE_SPEAKER_AZIMUTH_FRONT_RIGHT, 1},
    {FORGE_SPEAKER_AZIMUTH_SIDE_RIGHT, 7}, {FORGE_SPEAKER_AZIMUTH_BACK_RIGHT, 5},
    {FORGE_SPEAKER_AZIMUTH_BACK_LEFT, 4},  {FORGE_SPEAKER_AZIMUTH_SIDE_LEFT, 6},
    {FORGE_SPEAKER_AZIMUTH_FRONT_LEFT, 0},
};

/* With that organization, the index of the LF speaker into the matrix array
 * is kept in a separate field within ConfigInfo because it makes the code
 * much cleaner.
 * -Adrien
 */
static const ConfigInfo speakers_config_info[] = {
    {FORGE_SPEAKER_MONO, mono_config_speakers, ARRAY_COUNT(mono_config_speakers), -1},
    {FORGE_SPEAKER_STEREO, stereo_config_speakers, ARRAY_COUNT(stereo_config_speakers), -1},
    {FORGE_SPEAKER_2POINT1, config_2point1_speakers, ARRAY_COUNT(config_2point1_speakers), 2},
    {FORGE_SPEAKER_SURROUND, surround_config_speakers, ARRAY_COUNT(surround_config_speakers), -1},
    {FORGE_SPEAKER_QUAD, quad_config_speakers, ARRAY_COUNT(quad_config_speakers), -1},
    {FORGE_SPEAKER_4POINT1, config_4point1_speakers, ARRAY_COUNT(config_4point1_speakers), 2},
    {FORGE_SPEAKER_5POINT1, config_5point1_speakers, ARRAY_COUNT(config_5point1_speakers), 3},
    {FORGE_SPEAKER_7POINT1, config_7point1_speakers, ARRAY_COUNT(config_7point1_speakers), 3},
    {FORGE_SPEAKER_5POINT1_SURROUND, config_5point1_surround_speakers, ARRAY_COUNT(config_5point1_surround_speakers), 3},
    {FORGE_SPEAKER_7POINT1_SURROUND, config_7point1_surround_speakers, ARRAY_COUNT(config_7point1_surround_speakers), 3},
};

/* A simple linear search is absolutely OK for 10 elements. */
static const ConfigInfo *get_config_info(uint32_t speaker_config_mask) {
    uint32_t i;
    for (i = 0; i < ARRAY_COUNT(speakers_config_info); i += 1) {
        if (speakers_config_info[i].config_mask == speaker_config_mask) {
            return &speakers_config_info[i];
        }
    }

    forge_assert(0 && "Config info not found!");
    return NULL;
}

/* Given a configuration, this function finds the azimuths of the two speakers
 * between which the emitter lies. All the azimuths here are relative to the
 * listener's base, since that's where the speakers are defined.
 */
static inline void find_speaker_azimuths(const ConfigInfo *config, float emitter_azimuth, uint8_t skip_center,
                                       const SpeakerInfo **speaker_info) {
    uint32_t i, next_i = 0;
    float a0 = 0.0f, a1 = 0.0f;

    forge_assert(config != NULL);

    /* We want to find, given an azimuth, which speakers are the closest
     * ones (in terms of angle) to that azimuth.
     * This is done by iterating through the list of speaker azimuths, as
     * given to us by the current ConfigInfo (which stores speaker azimuths
     * in increasing order of azimuth for each possible speaker configuration;
     * each speaker azimuth is defined to be between 0 and 2PI by construction).
     */
    for (i = 0; i < config->num_non_lf_speakers; i += 1) {
        /* a0 and a1 are the azimuths of candidate speakers */
        a0 = config->speakers[i].azimuth;
        next_i = (i + 1) % config->num_non_lf_speakers;
        a1 = config->speakers[next_i].azimuth;

        if (a0 < a1) {
            if (emitter_azimuth >= a0 && emitter_azimuth < a1) {
                break;
            }
        }
        /* It is possible for a speaker pair to enclose the singulary at 0 == 2PI:
         * consider for example the quad config, which has a front left speaker
         * at 7PI/4 and a front right speaker at PI/4. In that case a0 = 7PI/4 and
         * a1 = PI/4, and the way we know whether our current azimuth lies between
         * that pair is by checking whether the azimuth is greather than 7PI/4 or
         * whether it's less than PI/4. (By contract, current_azimuth is always less
         * than 2PI.)
         */
        else {
            if (emitter_azimuth >= a0 || emitter_azimuth < a1) {
                break;
            }
        }
    }
    forge_assert(emitter_azimuth >= a0 || emitter_azimuth < a1);

    /* skip_center means that we don't want to use the center speaker.
     * The easiest way to deal with this is to check whether either of our candidate
     * speakers are the center, which always has an azimuth of 0.0. If that is the case
     * we just replace it with either the previous one or the next one.
     */
    if (skip_center) {
        if (a0 == 0.0f) {
            if (i == 0) {
                i = config->num_non_lf_speakers - 1;
            } else {
                i -= 1;
            }
        } else if (a1 == 0.0f) {
            next_i += 1;
            if (next_i >= config->num_non_lf_speakers) {
                next_i -= config->num_non_lf_speakers;
            }
        }
    }
    speaker_info[0] = &config->speakers[i];
    speaker_info[1] = &config->speakers[next_i];
}

/* Used to store diffusion factors */
/* See below for explanation. */
#define DIFFUSION_SPEAKERS_ALL 0
#define DIFFUSION_SPEAKERS_MATCHING 1
#define DIFFUSION_SPEAKERS_OPPOSITE 2
typedef float DiffusionSpeakerFactors[3];

/* compute_inner_radius_diffusion_factors is a utility function that returns how
 * energy dissipates to the speakers, given the radial distance between the
 * emitter and the listener and the (optionally 0) inner_radius distance. It
 * returns 3 floats, via the diffusion_factors array, that say how much energy
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
static inline void compute_inner_radius_diffusion_factors(float radial_distance, float inner_radius,
                                                      DiffusionSpeakerFactors diffusion_factors) {

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
 * This empirical minimum radius avoids the zero-radius diffusion singularity. */
#define DIFFUSION_DISTANCE_MINIMUM_INNER_RADIUS 4e-7f
    float actual_inner_radius = forge_max(inner_radius, DIFFUSION_DISTANCE_MINIMUM_INNER_RADIUS);
    float normalized_radial_dist;
    float a, ms, os;

    normalized_radial_dist = radial_distance / actual_inner_radius;

/* Do another check for small radial distances before applying any inner_radius-like
 * behaviour. This is the constant that determines the threshold: below this distance we simply
 * diffuse to all speakers equally. */
#define DIFFUSION_DISTANCE_EQUAL_ENERGY 1e-7f
    if (radial_distance <= DIFFUSION_DISTANCE_EQUAL_ENERGY) {
        a = 1.0f;
        ms = 0.0f;
        os = 0.0f;
    } else if (normalized_radial_dist <= 0.5f) {
        /* Determined experimentally that this is indeed a linear law,
         * with 100% confidence.
         * -Adrien
         */
        a = 1.0f - 2.0f * normalized_radial_dist;

        /* Empirical approximation for inner-radius diffusion between equal
         * and matching speaker energy.
         */
        ms = LERP(2.0f * normalized_radial_dist, 0.0f, DIFFUSION_LERP_MIDPOINT_VALUE);
        os = 1.0f - a - ms;
    } else if (normalized_radial_dist <= 1.0f) {
        a = 0.0f;

        /* Similarly, this is a lerp based on the midpoint value; the
         * real, high-accuracy curve also looks like a quadratic.
         * -Adrien
         */
        ms = LERP(2.0f * (normalized_radial_dist - 0.5f), DIFFUSION_LERP_MIDPOINT_VALUE, 1.0f);
        os = 1.0f - a - ms;
    } else {
        a = 0.0f;
        ms = 1.0f;
        os = 0.0f;
    }
    diffusion_factors[DIFFUSION_SPEAKERS_ALL] = a;
    diffusion_factors[DIFFUSION_SPEAKERS_MATCHING] = ms;
    diffusion_factors[DIFFUSION_SPEAKERS_OPPOSITE] = os;
}

/* compute_emitter_channel_coefficients handles the coefficients calculation for 1
 * column of the matrix. It uses compute_inner_radius_diffusion_factors to separate
 * into three discrete cases; and for each case does the right repartition of
 * the energy after attenuation to the right speakers, in particular in the
 * MATCHING and OPPOSITE cases, it gives each of the two speakers found a linear
 * amount of the energy, according to the angular distance between the emitter
 * and the speaker azimuth.
 */
static inline void compute_emitter_channel_coefficients(const ConfigInfo *cur_config,
                                                     const ForgeSpatialBasis *listener_basis, float inner_radius,
                                                     ForgeVector3 channel_position, float attenuation,
                                                     float lfe_attenuation, uint32_t flags, uint32_t current_channel,
                                                     uint32_t num_src_channels, float *matrix_coefficients) {
    float elevation, radial_distance;
    ForgeVector3 proj_top_vec, proj_plane;
    uint8_t skip_center = (flags & FORGE_SPATIAL_CALCULATE_ZERO_CENTER) ? 1 : 0;
    DiffusionSpeakerFactors diffusion_factors = {0.0f};

    float x, y;
    float emitter_azimuth;
    float energy_per_channel;
    float total_energy;
    uint32_t channels_to_diffuse_to;
    uint32_t i_s, center_channel_idx = -1;
    const SpeakerInfo *infos[2];
    float a0, a1, val;
    uint32_t i0, i1;

    /* We project against the listener basis' top vector to get the elevation of the
     * current emitter channel position.
     */
    elevation = VECTOR_DOT(listener_basis->top, channel_position);

    /* To obtain the projection in the front-right plane of the listener's basis of the
     * emitter channel position, we simply remove the projection against the top vector.
     * The radial distance is then the length of the projected vector.
     */
    proj_top_vec = VECTOR_SCALE(listener_basis->top, elevation);
    proj_plane = VECTOR_SUB(channel_position, proj_top_vec);
    radial_distance = VECTOR_LENGTH(proj_plane);

    compute_inner_radius_diffusion_factors(radial_distance, inner_radius, diffusion_factors);

    /* See the compute_inner_radius_diffusion_factors comment above for more context. */
    /* DIFFUSION_SPEAKERS_ALL corresponds to diffusing part of the sound to all of the
     * speakers, equally. The amount of sound is determined by the float value
     * diffusion_factors[DIFFUSION_SPEAKERS_ALL]. */
    if (diffusion_factors[DIFFUSION_SPEAKERS_ALL] > 0.0f) {
        channels_to_diffuse_to = cur_config->num_non_lf_speakers;
        total_energy = diffusion_factors[DIFFUSION_SPEAKERS_ALL] * attenuation;

        if (skip_center) {
            channels_to_diffuse_to -= 1;
            forge_assert(cur_config->speakers[0].azimuth == FORGE_SPEAKER_AZIMUTH_CENTER);
            center_channel_idx = cur_config->speakers[0].matrix_idx;
        }

        energy_per_channel = total_energy / channels_to_diffuse_to;

        for (i_s = 0; i_s < cur_config->num_non_lf_speakers; i_s += 1) {
            const uint32_t cur_speaker_idx = cur_config->speakers[i_s].matrix_idx;
            if (skip_center && cur_speaker_idx == center_channel_idx) {
                continue;
            }

            matrix_coefficients[cur_speaker_idx * num_src_channels + current_channel] += energy_per_channel;
        }
    }

    /* DIFFUSION_SPEAKERS_MATCHING corresponds to sending part of the sound to the speakers closest
     * (in terms of azimuth) to the current position of the emitter. The amount of sound we shoud send
     * corresponds here to diffusion_factors[DIFFUSION_SPEAKERS_MATCHING].
     * We use the find_speaker_azimuths function to find the speakers that match. */
    if (diffusion_factors[DIFFUSION_SPEAKERS_MATCHING] > 0.0f) {
        const float total_energy = diffusion_factors[DIFFUSION_SPEAKERS_MATCHING] * attenuation;

        x = VECTOR_DOT(listener_basis->front, proj_plane);
        y = VECTOR_DOT(listener_basis->right, proj_plane);

        /* Now, a critical point: We shouldn't be sending sound to
         * matching speakers when x and y are close to 0. That's the
         * contract we get from compute_inner_radius_diffusion_factors,
         * which checks that we're not too close to the zero distance.
         * This allows the atan2 calculation to give good results.
         */

        /* atan2 returns [-PI, PI], but we want [0, 2PI] */
        emitter_azimuth = forge_atan2f(y, x);
        if (emitter_azimuth < 0.0f) {
            emitter_azimuth += FORGE_SPATIAL_2PI;
        }

        find_speaker_azimuths(cur_config, emitter_azimuth, skip_center, infos);
        a0 = infos[0]->azimuth;
        a1 = infos[1]->azimuth;

        /* The following code is necessary to handle the singularity in
         * (0 == 2PI). It'll give us a nice, well ordered interval.
         */
        if (a0 > a1) {
            if (emitter_azimuth >= a0) {
                emitter_azimuth -= FORGE_SPATIAL_2PI;
            }
            a0 -= FORGE_SPATIAL_2PI;
        }
        forge_assert(emitter_azimuth >= a0 && emitter_azimuth <= a1);

        val = (emitter_azimuth - a0) / (a1 - a0);

        i0 = infos[0]->matrix_idx;
        i1 = infos[1]->matrix_idx;

        matrix_coefficients[i0 * num_src_channels + current_channel] += (1.0f - val) * total_energy;
        matrix_coefficients[i1 * num_src_channels + current_channel] += (val)*total_energy;
    }

    /* DIFFUSION_SPEAKERS_OPPOSITE corresponds to sending part of the sound to the speakers
     * _opposite_ the ones that are the closest to the current emitter position.
     * To find these, we simply find the ones that are closest to the current emitter's azimuth + PI
     * using the FindSpeakerAzimuth function. */
    if (diffusion_factors[DIFFUSION_SPEAKERS_OPPOSITE] > 0.0f) {
        /* This code is similar to the matching speakers code above. */
        const float total_energy = diffusion_factors[DIFFUSION_SPEAKERS_OPPOSITE] * attenuation;

        x = VECTOR_DOT(listener_basis->front, proj_plane);
        y = VECTOR_DOT(listener_basis->right, proj_plane);

        /* Similarly, we expect atan2 to be well behaved here. */
        emitter_azimuth = forge_atan2f(y, x);

        /* Opposite speakers lie at azimuth + PI */
        emitter_azimuth += FORGE_SPATIAL_PI;

        /* Normalize to [0; 2PI) range. */
        if (emitter_azimuth < 0.0f) {
            emitter_azimuth += FORGE_SPATIAL_2PI;
        } else if (emitter_azimuth > FORGE_SPATIAL_2PI) {
            emitter_azimuth -= FORGE_SPATIAL_2PI;
        }

        find_speaker_azimuths(cur_config, emitter_azimuth, skip_center, infos);
        a0 = infos[0]->azimuth;
        a1 = infos[1]->azimuth;

        /* The following code is necessary to handle the singularity in
         * (0 == 2PI). It'll give us a nice, well ordered interval.
         */
        if (a0 > a1) {
            if (emitter_azimuth >= a0) {
                emitter_azimuth -= FORGE_SPATIAL_2PI;
            }
            a0 -= FORGE_SPATIAL_2PI;
        }
        forge_assert(emitter_azimuth >= a0 && emitter_azimuth <= a1);

        val = (emitter_azimuth - a0) / (a1 - a0);

        i0 = infos[0]->matrix_idx;
        i1 = infos[1]->matrix_idx;

        matrix_coefficients[i0 * num_src_channels + current_channel] += (1.0f - val) * total_energy;
        matrix_coefficients[i1 * num_src_channels + current_channel] += (val)*total_energy;
    }

    if (flags & FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE) {
        forge_assert(cur_config->lf_speaker_idx != -1);
        matrix_coefficients[cur_config->lf_speaker_idx * num_src_channels + current_channel] +=
            lfe_attenuation / num_src_channels;
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
 * any. This computation is handled by the compute_inner_radius_diffusion_factors
 * function.
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
 * TODO: Design a ForgeAudio source spread/extent model for wide or near-field emitters.
 */
static inline void calculate_matrix(uint32_t channel_mask, uint32_t flags, const ForgeSpatialListener *listener,
                                   const ForgeSpatialEmitter *emitter, uint32_t src_channel_count,
                                   uint32_t dst_channel_count, ForgeVector3 emitter_to_listener, float emitter_to_listener_distance,
                                   float normalized_distance, float *matrix_coefficients) {
    uint32_t i_ec;
    float current_emitter_azimuth;
    const ConfigInfo *cur_config = get_config_info(channel_mask);
    float attenuation = compute_distance_attenuation(normalized_distance, emitter->volume_curve);
    /* Potential optimization: skip LFE attenuation when the output layout has no LFE channel. */
    float lfe_attenuation = compute_distance_attenuation(normalized_distance, emitter->lfe_curve);

    ForgeVector3 listener_to_emitter;
    ForgeVector3 listener_to_emitter_channel;
    ForgeSpatialBasis listener_basis;

    /* Note: For both cone calculations, the angle might be NaN or infinite
     * if distance == 0... compute_cone_parameter *does* check for this
     * special case. It is necessary that we still go through the
     * compute_cone_parameter function, because omnidirectional cones might
     * give either inner_volume or outer_volume.
     * -Adrien
     */
    if (listener->cone) {
        /* Listener cone uses listener_to_emitter, hence the negated dot product. */
        const float raw_cone_dp =
            -VECTOR_DOT(listener->orient_front, emitter_to_listener) / emitter_to_listener_distance;
        const float cone_dp = forge_clamp(raw_cone_dp, -1.0f, 1.0f);
        const float angle = forge_acosf(cone_dp);

        const float listener_cone_param =
            compute_cone_parameter(emitter_to_listener_distance, angle, listener->cone->inner_angle, listener->cone->outer_angle,
                                 listener->cone->inner_volume, listener->cone->outer_volume);
        attenuation *= listener_cone_param;
        lfe_attenuation *= listener_cone_param;
    }

    /* See note above. */
    if (emitter->cone && emitter->channel_count == 1) {
        const float raw_cone_dp = VECTOR_DOT(emitter->orient_front, emitter_to_listener) /
                                  emitter_to_listener_distance;
        const float cone_dp = forge_clamp(raw_cone_dp, -1.0f, 1.0f);
        const float angle = forge_acosf(cone_dp);

        const float emitter_cone_param =
            compute_cone_parameter(emitter_to_listener_distance, angle, emitter->cone->inner_angle, emitter->cone->outer_angle,
                                 emitter->cone->inner_volume, emitter->cone->outer_volume);
        attenuation *= emitter_cone_param;
    }

    forge_zero(matrix_coefficients, sizeof(float) * src_channel_count * dst_channel_count);

    /* In the FORGE_SPEAKER_MONO case, we can skip all energy diffusion calculation. */
    if (dst_channel_count == 1) {
        for (i_ec = 0; i_ec < emitter->channel_count; i_ec += 1) {
            current_emitter_azimuth = 0.0f;
            if (emitter->channel_azimuths) {
                current_emitter_azimuth = emitter->channel_azimuths[i_ec];
            }

            /* The MONO setup doesn't have an LFE speaker. */
            if (current_emitter_azimuth != FORGE_SPATIAL_2PI) {
                matrix_coefficients[i_ec] = attenuation;
            }
        }
    } else if (cur_config != NULL) {
        listener_to_emitter = VECTOR_SCALE(emitter_to_listener, -1.0f);

        /* Remember here that the coordinate system is Left-Handed. */
        listener_basis.front = listener->orient_front;
        listener_basis.right = VECTOR_CROSS(listener->orient_top, listener->orient_front);
        listener_basis.top = listener->orient_top;

        /* Handling the mono-channel emitter case separately is easier
         * than having it as a separate case of a for-loop; indeed, in
         * this case, we need to ignore the non-relevant values from the
         * emitter, _even if they're set_.
         */
        if (emitter->channel_count == 1) {
            listener_to_emitter_channel = listener_to_emitter;

            compute_emitter_channel_coefficients(cur_config, &listener_basis, emitter->inner_radius, listener_to_emitter_channel,
                                              attenuation, lfe_attenuation, flags, 0 /* current_channel */,
                                              1 /* num_src_channels */, matrix_coefficients);
        } else /* Multi-channel emitter case. */
        {
            const ForgeVector3 emitter_right = VECTOR_CROSS(emitter->orient_top, emitter->orient_front);

            for (i_ec = 0; i_ec < emitter->channel_count; i_ec += 1) {
                const float emitter_channel_azimuth = emitter->channel_azimuths[i_ec];

                /* LFEs are easy enough to deal with; we can
                 * just do them separately.
                 */
                if (emitter_channel_azimuth == FORGE_SPATIAL_2PI) {
                    if (cur_config->lf_speaker_idx != -1) {
                        matrix_coefficients[cur_config->lf_speaker_idx * emitter->channel_count + i_ec] = lfe_attenuation;
                    }
                } else {
                    /* First compute the emitter channel
                     * vector relative to the emitter base...
                     */
                    const ForgeVector3 emitter_base_to_channel =
                        VECTOR_ADD(VECTOR_SCALE(emitter->orient_front, emitter->channel_radius * forge_cosf(emitter_channel_azimuth)),
                                  VECTOR_SCALE(emitter_right, emitter->channel_radius * forge_sinf(emitter_channel_azimuth)));
                    /* ... then translate. */
                    listener_to_emitter_channel = VECTOR_ADD(listener_to_emitter, emitter_base_to_channel);

                    compute_emitter_channel_coefficients(cur_config, &listener_basis, emitter->inner_radius,
                                                      listener_to_emitter_channel, attenuation, lfe_attenuation, flags, i_ec,
                                                      emitter->channel_count, matrix_coefficients);
                }
            }
        }
    } else {
        forge_assert(0 && "Config info not found!");
    }

    /* Optional debug validation: finite, nonnegative matrix coefficients
     * and documented gain bounds.
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
static inline void calculate_doppler(float speed_of_sound, const ForgeSpatialListener *listener,
                                    const ForgeSpatialEmitter *emitter, ForgeVector3 emitter_to_listener,
                                    float emitter_to_listener_distance, float *listener_velocity_component,
                                    float *emitter_velocity_component, float *doppler_factor) {
    float scaled_speed_of_sound;
    *doppler_factor = 1.0f;

    /* Project... */
    if (emitter_to_listener_distance != 0.0f) {
        *listener_velocity_component = VECTOR_DOT(emitter_to_listener, listener->velocity) / emitter_to_listener_distance;
        *emitter_velocity_component = VECTOR_DOT(emitter_to_listener, emitter->velocity) / emitter_to_listener_distance;
    } else {
        *listener_velocity_component = 0.0f;
        *emitter_velocity_component = 0.0f;
    }

    if (emitter->doppler_scaler > 0.0f) {
        scaled_speed_of_sound = speed_of_sound / emitter->doppler_scaler;

        /* Clamp... */
        *listener_velocity_component = forge_min(*listener_velocity_component, scaled_speed_of_sound);
        *emitter_velocity_component = forge_min(*emitter_velocity_component, scaled_speed_of_sound);

        /* ... then Multiply. */
        *doppler_factor = (speed_of_sound - emitter->doppler_scaler * *listener_velocity_component) /
                          (speed_of_sound - emitter->doppler_scaler * *emitter_velocity_component);
        if (isnan(*doppler_factor)) /* If emitter/listener are at the same pos... */
        {
            *doppler_factor = 1.0f;
        }

        /* Limit the pitch shifting to 2 octaves up and 1 octave down */
        *doppler_factor = forge_clamp(*doppler_factor, 0.5f, 4.0f);
    }
}

void forge_spatializer_calculate(const ForgeSpatializer *spatializer, const ForgeSpatialListener *listener,
                                 const ForgeSpatialEmitter *emitter, uint32_t flags,
                                 ForgeSpatialDspSettings *dsp_settings) {
    uint32_t i;
    ForgeVector3 emitter_to_listener;
    float emitter_to_listener_distance, normalized_distance, dp;

#define DEFAULT_POINTS(name, x1, y1, x2, y2)                                                                           \
    static ForgeSpatialDistanceCurvePoint name##_points[2] = {{x1, y1}, {x2, y2}};                                      \
    static ForgeSpatialDistanceCurve name##_default = {(ForgeSpatialDistanceCurvePoint *)&name##_points[0], 2};
    DEFAULT_POINTS(lpf_direct, 0.0f, 1.0f, 1.0f, 0.75f)
    DEFAULT_POINTS(lpf_reverb, 0.0f, 0.75f, 1.0f, 0.75f)
    DEFAULT_POINTS(reverb, 0.0f, 1.0f, 1.0f, 0.0f)
#undef DEFAULT_POINTS

    /* Compute emitter-to-listener distance. */
    emitter_to_listener = VECTOR_SUB(listener->position, emitter->position);
    emitter_to_listener_distance = VECTOR_LENGTH(emitter_to_listener);
    dsp_settings->emitter_to_listener_distance = emitter_to_listener_distance;

    check_calculate_params(spatializer, listener, emitter, flags, dsp_settings);

    /* This is used by MATRIX, LPF, and REVERB */
    normalized_distance = emitter_to_listener_distance / emitter->curve_distance_scaler;

    if (flags & FORGE_SPATIAL_CALCULATE_MATRIX) {
        calculate_matrix(SPEAKERMASK(spatializer), flags, listener, emitter, dsp_settings->src_channel_count,
                        dsp_settings->dst_channel_count, emitter_to_listener, emitter_to_listener_distance, normalized_distance,
                        dsp_settings->matrix_coefficients);
    }

    if (flags & FORGE_SPATIAL_CALCULATE_LPF_DIRECT) {
        dsp_settings->lpf_direct_coefficient = compute_distance_attenuation(
            normalized_distance, (emitter->lpf_direct_curve != NULL) ? emitter->lpf_direct_curve : &lpf_direct_default);
    }

    if (flags & FORGE_SPATIAL_CALCULATE_LPF_REVERB) {
        dsp_settings->lpf_reverb_coefficient = compute_distance_attenuation(
            normalized_distance, (emitter->lpf_reverb_curve != NULL) ? emitter->lpf_reverb_curve : &lpf_reverb_default);
    }

    if (flags & FORGE_SPATIAL_CALCULATE_REVERB) {
        dsp_settings->reverb_level = compute_distance_attenuation(
            normalized_distance, (emitter->reverb_curve != NULL) ? emitter->reverb_curve : &reverb_default);
    }

    /* Compute Doppler pitch scalar and velocity components. */
    if (flags & FORGE_SPATIAL_CALCULATE_DOPPLER) {
        calculate_doppler(SPEEDOFSOUND(spatializer), listener, emitter, emitter_to_listener, emitter_to_listener_distance,
                         &dsp_settings->listener_velocity_component, &dsp_settings->emitter_velocity_component,
                         &dsp_settings->doppler_factor);
    }

    /* Compute emitter-to-listener orientation angle. */
    if (flags & FORGE_SPATIAL_CALCULATE_EMITTER_ANGLE) {
/* Empirical threshold below which emitter angle collapses to PI/2. */
#define EMITTER_ANGLE_NULL_DISTANCE 1.2e-7
        if (emitter_to_listener_distance < EMITTER_ANGLE_NULL_DISTANCE) {
            dsp_settings->emitter_to_listener_angle = FORGE_SPATIAL_PI / 2.0f;
        } else {
            /* Note: emitter->orient_front is normalized. */
            dp = VECTOR_DOT(emitter_to_listener, emitter->orient_front) / emitter_to_listener_distance;
            dp = forge_clamp(dp, -1.0f, 1.0f);
            dsp_settings->emitter_to_listener_angle = forge_acosf(dp);
        }
    }

    /* Unimplemented flags */
    if ((flags & FORGE_SPATIAL_CALCULATE_DELAY) && SPEAKERMASK(spatializer) == FORGE_SPEAKER_STEREO) {
        for (i = 0; i < dsp_settings->dst_channel_count; i += 1) {
            dsp_settings->delay_times[i] = 0.0f;
        }
        forge_assert(0 && "DELAY not implemented!");
    }
}
