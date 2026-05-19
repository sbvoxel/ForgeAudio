/*
 * ForgeAudio
 * Forked from FAudio.
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "spatial_audio_internal.h"

/**********************************************
 * forge_native_spatializer_* Implementation *
 **********************************************/

#define NATIVE_LFE_NONE 0xFFFFFFFFu
#define NATIVE_DISTANCE_EPSILON 0.000001f
#define NATIVE_SPEAKER_FLAG_HEIGHT                                                                                     \
    (FORGE_SPEAKER_TOP_CENTER | FORGE_SPEAKER_TOP_FRONT_LEFT | FORGE_SPEAKER_TOP_FRONT_CENTER |                       \
     FORGE_SPEAKER_TOP_FRONT_RIGHT | FORGE_SPEAKER_TOP_BACK_LEFT | FORGE_SPEAKER_TOP_BACK_CENTER |                    \
     FORGE_SPEAKER_TOP_BACK_RIGHT)

typedef struct NativeSpeakerInfo {
    float azimuth_rad;
    uint32_t matrix_index;
} NativeSpeakerInfo;

typedef struct NativeConfigInfo {
    uint32_t config_mask;
    const NativeSpeakerInfo *speakers;
    uint32_t speaker_count;
    uint32_t lfe_index;
} NativeConfigInfo;

static const NativeSpeakerInfo native_mono_speakers[] = {
    {0.0f, 0},
};
static const NativeSpeakerInfo native_stereo_speakers[] = {
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {FORGE_SPATIAL_PI / 6.0f, 1},
};
static const NativeSpeakerInfo native_2point1_speakers[] = {
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {FORGE_SPATIAL_PI / 6.0f, 1},
};
static const NativeSpeakerInfo native_surround_speakers[] = {
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {0.0f, 2},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI, 3},
};
static const NativeSpeakerInfo native_quad_speakers[] = {
    {-FORGE_SPATIAL_PI * 5.0f / 6.0f, 2},
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI * 5.0f / 6.0f, 3},
};
static const NativeSpeakerInfo native_4point1_speakers[] = {
    {-FORGE_SPATIAL_PI * 5.0f / 6.0f, 3},
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI * 5.0f / 6.0f, 4},
};
static const NativeSpeakerInfo native_5point1_speakers[] = {
    {-FORGE_SPATIAL_PI * 5.0f / 6.0f, 4},
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {0.0f, 2},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI * 5.0f / 6.0f, 5},
};
static const NativeSpeakerInfo native_7point1_speakers[] = {
    {-FORGE_SPATIAL_PI * 5.0f / 6.0f, 4},
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {-FORGE_SPATIAL_PI / 12.0f, 6},
    {0.0f, 2},
    {FORGE_SPATIAL_PI / 12.0f, 7},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI * 5.0f / 6.0f, 5},
};
static const NativeSpeakerInfo native_5point1_surround_speakers[] = {
    {-FORGE_SPATIAL_PI * 5.0f / 9.0f, 4},
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {0.0f, 2},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI * 5.0f / 9.0f, 5},
};
static const NativeSpeakerInfo native_7point1_surround_speakers[] = {
    {-FORGE_SPATIAL_PI * 5.0f / 6.0f, 4},
    {-FORGE_SPATIAL_PI * 5.0f / 9.0f, 6},
    {-FORGE_SPATIAL_PI / 6.0f, 0},
    {0.0f, 2},
    {FORGE_SPATIAL_PI / 6.0f, 1},
    {FORGE_SPATIAL_PI * 5.0f / 9.0f, 7},
    {FORGE_SPATIAL_PI * 5.0f / 6.0f, 5},
};

static const NativeConfigInfo native_config_info[] = {
    {FORGE_SPEAKER_MONO, native_mono_speakers, ARRAY_COUNT(native_mono_speakers), NATIVE_LFE_NONE},
    {FORGE_SPEAKER_STEREO, native_stereo_speakers, ARRAY_COUNT(native_stereo_speakers), NATIVE_LFE_NONE},
    {FORGE_SPEAKER_2POINT1, native_2point1_speakers, ARRAY_COUNT(native_2point1_speakers), 2},
    {FORGE_SPEAKER_SURROUND, native_surround_speakers, ARRAY_COUNT(native_surround_speakers), NATIVE_LFE_NONE},
    {FORGE_SPEAKER_QUAD, native_quad_speakers, ARRAY_COUNT(native_quad_speakers), NATIVE_LFE_NONE},
    {FORGE_SPEAKER_4POINT1, native_4point1_speakers, ARRAY_COUNT(native_4point1_speakers), 2},
    {FORGE_SPEAKER_5POINT1, native_5point1_speakers, ARRAY_COUNT(native_5point1_speakers), 3},
    {FORGE_SPEAKER_7POINT1, native_7point1_speakers, ARRAY_COUNT(native_7point1_speakers), 3},
    {FORGE_SPEAKER_5POINT1_SURROUND, native_5point1_surround_speakers, ARRAY_COUNT(native_5point1_surround_speakers),
     3},
    {FORGE_SPEAKER_7POINT1_SURROUND, native_7point1_surround_speakers, ARRAY_COUNT(native_7point1_surround_speakers),
     3},
};

static uint32_t native_count_bits(uint32_t value) {
    uint32_t count = 0;
    while (value) {
        count += 1;
        value &= value - 1;
    }
    return count;
}

static const NativeConfigInfo *native_get_config_info(uint32_t speaker_channel_mask) {
    for (uint32_t i = 0; i < ARRAY_COUNT(native_config_info); i += 1) {
        if (native_config_info[i].config_mask == speaker_channel_mask) {
            return &native_config_info[i];
        }
    }
    return NULL;
}

static uint8_t native_float_valid(float value) {
    return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static uint8_t native_vector_valid(ForgeVector3 value) {
    return native_float_valid(value.x) && native_float_valid(value.y) && native_float_valid(value.z);
}

static ForgeVector3 native_vector_normalize_or_default(ForgeVector3 value, ForgeVector3 default_value) {
    const float length = VECTOR_LENGTH(value);
    if (length <= NATIVE_DISTANCE_EPSILON || !native_float_valid(length)) {
        return default_value;
    }
    return VECTOR_SCALE(value, 1.0f / length);
}

static ForgeResult native_validate_curve(const ForgeNativeSpatialDistanceCurve *curve) {
    if (curve == NULL) {
        return ForgeResultSuccess;
    }
    if (curve->points == NULL || curve->point_count < 2) {
        return ForgeResultInvalidArgument;
    }
    for (uint32_t i = 0; i < curve->point_count; i += 1) {
        if (!native_float_valid(curve->points[i].distance_m) || !native_float_valid(curve->points[i].dsp_setting) ||
            curve->points[i].distance_m < 0.0f || curve->points[i].dsp_setting < 0.0f) {
            return ForgeResultInvalidArgument;
        }
        if (i > 0 && curve->points[i - 1].distance_m >= curve->points[i].distance_m) {
            return ForgeResultInvalidArgument;
        }
    }
    return ForgeResultSuccess;
}

static ForgeResult native_validate_cone(const ForgeNativeSpatialCone *cone) {
    if (cone == NULL) {
        return ForgeResultSuccess;
    }
    if (!native_float_valid(cone->inner_angle_rad) || !native_float_valid(cone->outer_angle_rad) ||
        cone->inner_angle_rad < 0.0f || cone->outer_angle_rad < cone->inner_angle_rad ||
        cone->outer_angle_rad > FORGE_SPATIAL_PI) {
        return ForgeResultInvalidArgument;
    }
    if (!native_float_valid(cone->inner_direct_gain) || !native_float_valid(cone->outer_direct_gain) ||
        !native_float_valid(cone->inner_lowpass_cutoff_hz) || !native_float_valid(cone->outer_lowpass_cutoff_hz) ||
        !native_float_valid(cone->inner_reverb_send_gain) || !native_float_valid(cone->outer_reverb_send_gain) ||
        cone->inner_direct_gain < 0.0f || cone->outer_direct_gain < 0.0f ||
        cone->inner_lowpass_cutoff_hz < 0.0f || cone->outer_lowpass_cutoff_hz < 0.0f ||
        cone->inner_reverb_send_gain < 0.0f || cone->outer_reverb_send_gain < 0.0f) {
        return ForgeResultInvalidArgument;
    }
    return ForgeResultSuccess;
}

static float native_curve_evaluate(const ForgeNativeSpatialDistanceCurve *curve, float distance_m) {
    const ForgeNativeSpatialDistanceCurvePoint *points = curve->points;
    if (distance_m <= points[0].distance_m) {
        return points[0].dsp_setting;
    }
    for (uint32_t i = 1; i < curve->point_count; i += 1) {
        if (distance_m <= points[i].distance_m) {
            const float distance_range = points[i].distance_m - points[i - 1].distance_m;
            const float alpha = (distance_m - points[i - 1].distance_m) / distance_range;
            return LERP(alpha, points[i - 1].dsp_setting, points[i].dsp_setting);
        }
    }
    return points[curve->point_count - 1].dsp_setting;
}

static float native_lowest_positive_cutoff(float current_hz, float candidate_hz) {
    if (candidate_hz <= 0.0f) {
        return current_hz;
    }
    if (current_hz <= 0.0f || candidate_hz < current_hz) {
        return candidate_hz;
    }
    return current_hz;
}

static float native_cone_evaluate(float angle_rad, const ForgeNativeSpatialCone *cone, float inner_value,
                                  float outer_value) {
    if (angle_rad <= cone->inner_angle_rad) {
        return inner_value;
    }
    if (angle_rad >= cone->outer_angle_rad) {
        return outer_value;
    }
    if (cone->outer_angle_rad <= cone->inner_angle_rad) {
        return outer_value;
    }
    return LERP((angle_rad - cone->inner_angle_rad) / (cone->outer_angle_rad - cone->inner_angle_rad), inner_value,
                outer_value);
}

static float native_reference_distance(const ForgeNativeSpatialEmitter *emitter) {
    if (emitter->reference_distance_m > 0.0f) {
        return emitter->reference_distance_m;
    }
    return 1.0f;
}

static float native_default_direct_gain(float distance_m, float reference_distance_m) {
    if (distance_m <= reference_distance_m) {
        return 1.0f;
    }
    return reference_distance_m / distance_m;
}

static float native_wrap_to_pi(float value) {
    while (value <= -FORGE_SPATIAL_PI) {
        value += FORGE_SPATIAL_2PI;
    }
    while (value > FORGE_SPATIAL_PI) {
        value -= FORGE_SPATIAL_2PI;
    }
    return value;
}

static void native_vbap_gains(const NativeConfigInfo *config, float azimuth_rad, uint32_t *speaker_a,
                              uint32_t *speaker_b, float *gain_a, float *gain_b) {
    float azimuth = native_wrap_to_pi(azimuth_rad);

    if (config->speaker_count == 1) {
        *speaker_a = config->speakers[0].matrix_index;
        *speaker_b = config->speakers[0].matrix_index;
        *gain_a = 1.0f;
        *gain_b = 0.0f;
        return;
    }

    while (azimuth < config->speakers[0].azimuth_rad) {
        azimuth += FORGE_SPATIAL_2PI;
    }
    while (azimuth >= config->speakers[0].azimuth_rad + FORGE_SPATIAL_2PI) {
        azimuth -= FORGE_SPATIAL_2PI;
    }

    for (uint32_t i = 0; i < config->speaker_count; i += 1) {
        const uint32_t next = (i + 1) % config->speaker_count;
        const float a0 = config->speakers[i].azimuth_rad;
        const float a1 =
            (next == 0) ? config->speakers[next].azimuth_rad + FORGE_SPATIAL_2PI : config->speakers[next].azimuth_rad;

        if (azimuth >= a0 && azimuth <= a1) {
            const float r0 = forge_sinf(a0);
            const float f0 = forge_cosf(a0);
            const float r1 = forge_sinf(a1);
            const float f1 = forge_cosf(a1);
            const float rt = forge_sinf(azimuth);
            const float ft = forge_cosf(azimuth);
            const float determinant = (r0 * f1) - (r1 * f0);
            float g0;
            float g1;
            float norm;

            *speaker_a = config->speakers[i].matrix_index;
            *speaker_b = config->speakers[next].matrix_index;

            if (forge_fabsf(determinant) <= 0.000001f) {
                *gain_a = 1.0f;
                *gain_b = 0.0f;
                return;
            }

            g0 = ((rt * f1) - (r1 * ft)) / determinant;
            g1 = ((r0 * ft) - (rt * f0)) / determinant;
            if (g0 < 0.0f && g0 > -0.00001f) {
                g0 = 0.0f;
            }
            if (g1 < 0.0f && g1 > -0.00001f) {
                g1 = 0.0f;
            }
            g0 = forge_max(g0, 0.0f);
            g1 = forge_max(g1, 0.0f);
            norm = forge_sqrtf((g0 * g0) + (g1 * g1));
            if (norm <= 0.000001f) {
                *gain_a = 1.0f;
                *gain_b = 0.0f;
            } else {
                *gain_a = g0 / norm;
                *gain_b = g1 / norm;
            }
            return;
        }
    }

    *speaker_a = config->speakers[0].matrix_index;
    *speaker_b = config->speakers[0].matrix_index;
    *gain_a = 1.0f;
    *gain_b = 0.0f;
}

static void native_calculate_matrix(const NativeConfigInfo *config, float azimuth_rad, float distance_m, float radius_m,
                                    float direct_gain, uint32_t dst_channel_count, float *matrix_coefficients) {
    static const float tap_offsets[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    static const float tap_weights[5] = {1.0f / 9.0f, 2.0f / 9.0f, 3.0f / 9.0f, 2.0f / 9.0f, 1.0f / 9.0f};
    float energy[FORGE_AUDIO_MAX_AUDIO_CHANNELS];
    const float half_angle = forge_atan2f(forge_max(radius_m, 0.0f), forge_max(distance_m, NATIVE_DISTANCE_EPSILON));

    forge_zero(matrix_coefficients, sizeof(float) * dst_channel_count);
    forge_zero(energy, sizeof(energy));

    for (uint32_t tap = 0; tap < ARRAY_COUNT(tap_offsets); tap += 1) {
        uint32_t speaker_a;
        uint32_t speaker_b;
        float gain_a;
        float gain_b;

        native_vbap_gains(config, azimuth_rad + (tap_offsets[tap] * half_angle), &speaker_a, &speaker_b, &gain_a,
                          &gain_b);
        energy[speaker_a] += tap_weights[tap] * gain_a * gain_a;
        energy[speaker_b] += tap_weights[tap] * gain_b * gain_b;
    }

    for (uint32_t i = 0; i < dst_channel_count; i += 1) {
        matrix_coefficients[i] = forge_sqrtf(energy[i]) * direct_gain;
    }
    if (config->lfe_index != NATIVE_LFE_NONE && config->lfe_index < dst_channel_count) {
        matrix_coefficients[config->lfe_index] = 0.0f;
    }
}

static void native_calculate_doppler(const ForgeNativeSpatializer *spatializer,
                                     const ForgeNativeSpatialListener *listener,
                                     const ForgeNativeSpatialEmitter *emitter, ForgeVector3 emitter_to_listener,
                                     float distance_m, ForgeNativeSpatialDspSettings *dsp_settings) {
    float listener_component = 0.0f;
    float emitter_component = 0.0f;
    float rate = 1.0f;

    if (distance_m > NATIVE_DISTANCE_EPSILON) {
        const ForgeVector3 direction = VECTOR_SCALE(emitter_to_listener, 1.0f / distance_m);
        listener_component = VECTOR_DOT(direction, listener->velocity_m_per_sec);
        emitter_component = VECTOR_DOT(direction, emitter->velocity_m_per_sec);
    }

    if (emitter->doppler_scale > 0.0f) {
        const float speed = spatializer->speed_of_sound_m_per_sec;
        const float scaled_emitter = emitter_component * emitter->doppler_scale;
        const float numerator = speed - listener_component;
        const float denominator = speed - scaled_emitter;
        if (forge_fabsf(denominator) > 0.000001f) {
            rate = numerator / denominator;
        }
        if (!native_float_valid(rate)) {
            rate = 1.0f;
        }
        rate = forge_clamp(rate, 0.5f, 4.0f);
    }

    dsp_settings->listener_velocity_component_m_per_sec = listener_component;
    dsp_settings->emitter_velocity_component_m_per_sec = emitter_component;
    dsp_settings->doppler_rate_scalar = rate;
}

ForgeResult forge_native_spatializer_init(uint32_t speaker_channel_mask, float speed_of_sound_m_per_sec,
                                          ForgeNativeSpatializer *spatializer) {
    const NativeConfigInfo *config;

    if (spatializer == NULL || !native_float_valid(speed_of_sound_m_per_sec) || speed_of_sound_m_per_sec < FLT_MIN) {
        return ForgeResultInvalidArgument;
    }
    if (speaker_channel_mask & NATIVE_SPEAKER_FLAG_HEIGHT) {
        return ForgeResultInvalidArgument;
    }

    config = native_get_config_info(speaker_channel_mask);
    if (config == NULL) {
        return ForgeResultInvalidArgument;
    }

    spatializer->speaker_channel_mask = speaker_channel_mask;
    spatializer->speaker_count = native_count_bits(speaker_channel_mask);
    spatializer->non_lfe_speaker_count = config->speaker_count;
    spatializer->low_frequency_channel_index = config->lfe_index;
    spatializer->speed_of_sound_m_per_sec = speed_of_sound_m_per_sec;
    return ForgeResultSuccess;
}

ForgeResult forge_native_spatializer_calculate(const ForgeNativeSpatializer *spatializer,
                                               const ForgeNativeSpatialListener *listener,
                                               const ForgeNativeSpatialEmitter *emitter, uint32_t flags,
                                               ForgeNativeSpatialDspSettings *dsp_settings) {
    const NativeConfigInfo *config;
    ForgeVector3 listener_front;
    ForgeVector3 listener_top;
    ForgeVector3 listener_right;
    ForgeVector3 listener_to_emitter;
    ForgeVector3 emitter_to_listener;
    float x;
    float y;
    float z;
    float horizontal_distance;
    float reference_distance_m;
    float direct_gain;
    float lowpass_hz = 0.0f;
    float reverb_send_gain = 0.0f;
    float cone_reverb_send_gain = 0.0f;

    if (spatializer == NULL || listener == NULL || emitter == NULL || dsp_settings == NULL ||
        dsp_settings->matrix_coefficients == NULL) {
        return ForgeResultInvalidArgument;
    }
    if (dsp_settings->src_channel_count != 1 || dsp_settings->dst_channel_count != spatializer->speaker_count ||
        spatializer->speaker_count != native_count_bits(spatializer->speaker_channel_mask) ||
        spatializer->speaker_count > FORGE_AUDIO_MAX_AUDIO_CHANNELS ||
        !native_float_valid(spatializer->speed_of_sound_m_per_sec) ||
        spatializer->speed_of_sound_m_per_sec < FLT_MIN) {
        return ForgeResultInvalidArgument;
    }
    config = native_get_config_info(spatializer->speaker_channel_mask);
    if (config == NULL || config->speaker_count != spatializer->non_lfe_speaker_count) {
        return ForgeResultInvalidArgument;
    }
    if (!native_vector_valid(listener->orient_front) || !native_vector_valid(listener->orient_top) ||
        !native_vector_valid(listener->position_m) || !native_vector_valid(listener->velocity_m_per_sec) ||
        !native_vector_valid(emitter->orient_front) || !native_vector_valid(emitter->position_m) ||
        !native_vector_valid(emitter->velocity_m_per_sec) || !native_float_valid(emitter->radius_m) ||
        !native_float_valid(emitter->reference_distance_m) || !native_float_valid(emitter->doppler_scale) ||
        !native_float_valid(emitter->direct_occlusion) || !native_float_valid(emitter->reverb_occlusion) ||
        !native_float_valid(emitter->occluded_lowpass_cutoff_hz) || emitter->radius_m < 0.0f ||
        emitter->reference_distance_m < 0.0f || emitter->doppler_scale < 0.0f || emitter->direct_occlusion < 0.0f ||
        emitter->direct_occlusion > 1.0f || emitter->reverb_occlusion < 0.0f || emitter->reverb_occlusion > 1.0f ||
        emitter->occluded_lowpass_cutoff_hz < 0.0f) {
        return ForgeResultInvalidArgument;
    }
    if (native_validate_curve(emitter->direct_gain_curve) != ForgeResultSuccess ||
        native_validate_curve(emitter->direct_lowpass_curve) != ForgeResultSuccess ||
        native_validate_curve(emitter->reverb_send_curve) != ForgeResultSuccess ||
        native_validate_cone(emitter->cone) != ForgeResultSuccess) {
        return ForgeResultInvalidArgument;
    }

    listener_front = native_vector_normalize_or_default(listener->orient_front, vector_make(0.0f, 0.0f, 1.0f));
    listener_top = native_vector_normalize_or_default(listener->orient_top, vector_make(0.0f, 1.0f, 0.0f));
    listener_right = native_vector_normalize_or_default(VECTOR_CROSS(listener_top, listener_front),
                                                        vector_make(1.0f, 0.0f, 0.0f));
    listener_to_emitter = VECTOR_SUB(emitter->position_m, listener->position_m);
    emitter_to_listener = VECTOR_SCALE(listener_to_emitter, -1.0f);

    x = VECTOR_DOT(listener_right, listener_to_emitter);
    y = VECTOR_DOT(listener_top, listener_to_emitter);
    z = VECTOR_DOT(listener_front, listener_to_emitter);
    horizontal_distance = forge_sqrtf((x * x) + (z * z));

    dsp_settings->distance_m = VECTOR_LENGTH(listener_to_emitter);
    dsp_settings->azimuth_rad = native_wrap_to_pi(forge_atan2f(x, z));
    dsp_settings->elevation_rad = forge_atan2f(y, horizontal_distance);
    dsp_settings->doppler_rate_scalar = 1.0f;
    dsp_settings->listener_velocity_component_m_per_sec = 0.0f;
    dsp_settings->emitter_velocity_component_m_per_sec = 0.0f;

    reference_distance_m = native_reference_distance(emitter);
    if (emitter->direct_gain_curve != NULL) {
        direct_gain = native_curve_evaluate(emitter->direct_gain_curve, dsp_settings->distance_m);
    } else {
        direct_gain = native_default_direct_gain(dsp_settings->distance_m, reference_distance_m);
    }
    if (emitter->direct_lowpass_curve != NULL) {
        lowpass_hz = native_lowest_positive_cutoff(lowpass_hz,
                                                   native_curve_evaluate(emitter->direct_lowpass_curve,
                                                                         dsp_settings->distance_m));
    }
    if (emitter->reverb_send_curve != NULL) {
        reverb_send_gain = native_curve_evaluate(emitter->reverb_send_curve, dsp_settings->distance_m);
    }

    if (emitter->cone != NULL) {
        float cone_angle = 0.0f;
        const ForgeVector3 emitter_front =
            native_vector_normalize_or_default(emitter->orient_front, vector_make(0.0f, 0.0f, 1.0f));
        if (dsp_settings->distance_m > NATIVE_DISTANCE_EPSILON) {
            cone_angle = forge_acosf(forge_clamp(VECTOR_DOT(emitter_front, emitter_to_listener) /
                                                     dsp_settings->distance_m,
                                                 -1.0f, 1.0f));
        }

        direct_gain *= native_cone_evaluate(cone_angle, emitter->cone, emitter->cone->inner_direct_gain,
                                            emitter->cone->outer_direct_gain);
        lowpass_hz = native_lowest_positive_cutoff(
            lowpass_hz, native_cone_evaluate(cone_angle, emitter->cone, emitter->cone->inner_lowpass_cutoff_hz,
                                             emitter->cone->outer_lowpass_cutoff_hz));
        cone_reverb_send_gain = native_cone_evaluate(cone_angle, emitter->cone,
                                                     emitter->cone->inner_reverb_send_gain,
                                                     emitter->cone->outer_reverb_send_gain);
        if (emitter->reverb_send_curve != NULL) {
            reverb_send_gain *= cone_reverb_send_gain;
        } else {
            reverb_send_gain = cone_reverb_send_gain;
        }
    }

    if (emitter->direct_occlusion > 0.0f) {
        direct_gain *= 1.0f - emitter->direct_occlusion;
        if (emitter->occluded_lowpass_cutoff_hz > 0.0f) {
            const double clear_log = forge_log(20000.0);
            const double occluded_log = forge_log((double)emitter->occluded_lowpass_cutoff_hz);
            const float occlusion_cutoff =
                (float)forge_exp(clear_log + ((occluded_log - clear_log) * (double)emitter->direct_occlusion));
            lowpass_hz = native_lowest_positive_cutoff(lowpass_hz, occlusion_cutoff);
        }
    }
    reverb_send_gain *= 1.0f - emitter->reverb_occlusion;

    dsp_settings->direct_gain = direct_gain;
    dsp_settings->direct_lowpass_cutoff_hz = lowpass_hz;
    dsp_settings->reverb_send_gain = reverb_send_gain;

    native_calculate_matrix(config, dsp_settings->azimuth_rad, dsp_settings->distance_m, emitter->radius_m, direct_gain,
                            dsp_settings->dst_channel_count, dsp_settings->matrix_coefficients);

    if (flags & FORGE_NATIVE_SPATIAL_CALCULATE_DOPPLER) {
        native_calculate_doppler(spatializer, listener, emitter, emitter_to_listener, dsp_settings->distance_m,
                                 dsp_settings);
    }

    return ForgeResultSuccess;
}
