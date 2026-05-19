/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include <forge/spatial_audio.h>

#include <stdio.h>

static float test_absf(float value) {
    return value < 0.0f ? -value : value;
}

static int check_close(const char *name, float actual, float expected) {
    if (test_absf(actual - expected) > 0.000001f) {
        fprintf(stderr, "%s: expected %.8f, got %.8f\n", name, expected, actual);
        return 1;
    }

    return 0;
}

static int check_close_tol(const char *name, float actual, float expected, float tolerance) {
    if (test_absf(actual - expected) > tolerance) {
        fprintf(stderr, "%s: expected %.8f, got %.8f\n", name, expected, actual);
        return 1;
    }

    return 0;
}

static int check_result(const char *name, ForgeResult actual, ForgeResult expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: expected result %d, got %d\n", name, expected, actual);
        return 1;
    }

    return 0;
}

static int check_finite_nonnegative(const char *name, const float *values, uint32_t count) {
    for (uint32_t i = 0; i < count; i += 1) {
        if (!(values[i] == values[i]) || values[i] < 0.0f || values[i] > 1000000.0f) {
            fprintf(stderr, "%s[%u]: expected finite nonnegative coefficient, got %.8f\n", name, i, values[i]);
            return 1;
        }
    }

    return 0;
}

static float calculate_listener_cone_gain(float emitter_z) {
    ForgeSpatializer spatializer;
    ForgeSpatialCone listener_cone;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;
    float matrix = 0.0f;

    forge_spatializer_init(FORGE_SPEAKER_MONO, 343.5f, &spatializer);

    listener_cone.inner_angle = FORGE_SPATIAL_PI / 2.0f;
    listener_cone.outer_angle = FORGE_SPATIAL_PI;
    listener_cone.inner_volume = 1.0f;
    listener_cone.outer_volume = 0.25f;
    listener_cone.inner_lpf = 0.0f;
    listener_cone.outer_lpf = 0.0f;
    listener_cone.inner_reverb = 0.0f;
    listener_cone.outer_reverb = 0.0f;

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = &listener_cone;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){0.0f, 0.0f, emitter_z};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.0f;
    emitter.channel_count = 1;
    emitter.channel_radius = 0.0f;
    emitter.channel_azimuths = NULL;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = &matrix;
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 1;
    dsp_settings.dst_channel_count = 1;

    forge_spatializer_calculate(&spatializer, &listener, &emitter, FORGE_SPATIAL_CALCULATE_MATRIX,
                                &dsp_settings);

    return matrix;
}

static int test_listener_cone_uses_listener_to_emitter_direction(void) {
    int failures = 0;

    failures += check_close("front listener cone gain", calculate_listener_cone_gain(1.0f), 1.0f);
    failures += check_close("behind listener cone gain", calculate_listener_cone_gain(-1.0f), 0.25f);

    return failures;
}

static int test_multichannel_lfe_skips_destination_without_lfe(void) {
    ForgeSpatializer spatializer;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;
    float channel_azimuths[2] = {0.0f, FORGE_SPATIAL_2PI};
    float guarded_matrix[5] = {-1234.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    forge_spatializer_init(FORGE_SPEAKER_STEREO, 343.5f, &spatializer);

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = NULL;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.0f;
    emitter.channel_count = 2;
    emitter.channel_radius = 0.0f;
    emitter.channel_azimuths = channel_azimuths;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = &guarded_matrix[1];
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 2;
    dsp_settings.dst_channel_count = 2;

    forge_spatializer_calculate(&spatializer, &listener, &emitter, FORGE_SPATIAL_CALCULATE_MATRIX,
                                &dsp_settings);

    return check_close("matrix guard before stereo destination", guarded_matrix[0], -1234.0f);
}

static void calculate_stereo_matrix_for_emitter_x(float emitter_x, float matrix[2]) {
    ForgeSpatializer spatializer;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;

    matrix[0] = 0.0f;
    matrix[1] = 0.0f;

    forge_spatializer_init(FORGE_SPEAKER_STEREO, 343.5f, &spatializer);

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = NULL;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){emitter_x, 0.0f, 0.0f};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.0f;
    emitter.channel_count = 1;
    emitter.channel_radius = 0.0f;
    emitter.channel_azimuths = NULL;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = matrix;
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 1;
    dsp_settings.dst_channel_count = 2;

    forge_spatializer_calculate(&spatializer, &listener, &emitter, FORGE_SPATIAL_CALCULATE_MATRIX,
                                &dsp_settings);
}

static int test_inner_radius_minimum_diffusion_is_continuous_above_null_distance(void) {
    float at_null_distance[2];
    float above_null_distance[2];
    int failures = 0;

    calculate_stereo_matrix_for_emitter_x(1.0e-7f, at_null_distance);
    calculate_stereo_matrix_for_emitter_x(1.01e-7f, above_null_distance);

    if (test_absf(at_null_distance[0] - above_null_distance[0]) > 0.01f) {
        fprintf(stderr, "left near-null diffusion jumped from %.8f to %.8f\n", at_null_distance[0],
                above_null_distance[0]);
        failures += 1;
    }
    if (test_absf(at_null_distance[1] - above_null_distance[1]) > 0.01f) {
        fprintf(stderr, "right near-null diffusion jumped from %.8f to %.8f\n", at_null_distance[1],
                above_null_distance[1]);
        failures += 1;
    }

    return failures;
}

static int test_zero_distance_matrix_is_continuous_with_near_null_distance(void) {
    float at_zero[2];
    float at_null_distance[2];
    int failures = 0;

    calculate_stereo_matrix_for_emitter_x(0.0f, at_zero);
    calculate_stereo_matrix_for_emitter_x(1.0e-7f, at_null_distance);

    failures += check_close("zero-distance left coefficient", at_zero[0], 0.5f);
    failures += check_close("zero-distance right coefficient", at_zero[1], 0.5f);
    failures += check_close("zero/null left continuity", at_zero[0], at_null_distance[0]);
    failures += check_close("zero/null right continuity", at_zero[1], at_null_distance[1]);

    return failures;
}

static int test_zero_center_skips_center_channel(void) {
    ForgeSpatializer spatializer;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;
    float matrix[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int failures = 0;

    forge_spatializer_init(FORGE_SPEAKER_5POINT1, 343.5f, &spatializer);

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = NULL;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.0f;
    emitter.channel_count = 1;
    emitter.channel_radius = 0.0f;
    emitter.channel_azimuths = NULL;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = matrix;
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 1;
    dsp_settings.dst_channel_count = 6;

    forge_spatializer_calculate(&spatializer, &listener, &emitter,
                                FORGE_SPATIAL_CALCULATE_MATRIX | FORGE_SPATIAL_CALCULATE_ZERO_CENTER,
                                &dsp_settings);

    failures += check_close("zero-center center coefficient", matrix[2], 0.0f);
    if (matrix[0] <= 0.0f || matrix[1] <= 0.0f) {
        fprintf(stderr, "zero-center expected front left/right coefficients, got %.8f %.8f\n", matrix[0], matrix[1]);
        failures += 1;
    }
    failures += check_finite_nonnegative("zero-center matrix", matrix, 6);

    return failures;
}

static int test_redirect_to_lfe_writes_lfe_channel(void) {
    ForgeSpatializer spatializer;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;
    float matrix[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int failures = 0;

    forge_spatializer_init(FORGE_SPEAKER_5POINT1, 343.5f, &spatializer);

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = NULL;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.0f;
    emitter.channel_count = 1;
    emitter.channel_radius = 0.0f;
    emitter.channel_azimuths = NULL;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = matrix;
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 1;
    dsp_settings.dst_channel_count = 6;

    forge_spatializer_calculate(&spatializer, &listener, &emitter,
                                FORGE_SPATIAL_CALCULATE_MATRIX | FORGE_SPATIAL_CALCULATE_REDIRECT_TO_LFE,
                                &dsp_settings);

    failures += check_close("redirect-to-lfe coefficient", matrix[3], 1.0f);
    failures += check_finite_nonnegative("redirect-to-lfe matrix", matrix, 6);

    return failures;
}

static int test_acos_inputs_resist_valid_vector_roundoff(void) {
    ForgeSpatializer spatializer;
    ForgeSpatialCone listener_cone;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;
    float matrix = 0.0f;
    int failures = 0;

    forge_spatializer_init(FORGE_SPEAKER_MONO, 343.5f, &spatializer);

    listener_cone.inner_angle = FORGE_SPATIAL_PI / 2.0f;
    listener_cone.outer_angle = FORGE_SPATIAL_PI;
    listener_cone.inner_volume = 1.0f;
    listener_cone.outer_volume = 0.25f;
    listener_cone.inner_lpf = 0.0f;
    listener_cone.outer_lpf = 0.0f;
    listener_cone.inner_reverb = 0.0f;
    listener_cone.outer_reverb = 0.0f;

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.000001f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = &listener_cone;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.000001f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.0f;
    emitter.channel_count = 1;
    emitter.channel_radius = 0.0f;
    emitter.channel_azimuths = NULL;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = &matrix;
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 1;
    dsp_settings.dst_channel_count = 1;

    forge_spatializer_calculate(&spatializer, &listener, &emitter,
                                FORGE_SPATIAL_CALCULATE_MATRIX | FORGE_SPATIAL_CALCULATE_EMITTER_ANGLE,
                                &dsp_settings);

    failures += check_close("roundoff listener cone gain", matrix, 1.0f);
    failures += check_close("roundoff emitter angle", dsp_settings.emitter_to_listener_angle, FORGE_SPATIAL_PI);
    if (!(dsp_settings.emitter_to_listener_angle == dsp_settings.emitter_to_listener_angle)) {
        fprintf(stderr, "roundoff emitter angle became NaN\n");
        failures += 1;
    }

    return failures;
}

static int test_matrix_coefficients_are_finite_and_nonnegative(void) {
    ForgeSpatializer spatializer;
    ForgeSpatialListener listener;
    ForgeSpatialEmitter emitter;
    ForgeSpatialDspSettings dsp_settings;
    float channel_azimuths[3] = {0.0f, FORGE_SPATIAL_PI, FORGE_SPATIAL_2PI};
    float matrix[18];

    for (uint32_t i = 0; i < 18; i += 1) {
        matrix[i] = 0.0f;
    }

    forge_spatializer_init(FORGE_SPEAKER_5POINT1, 343.5f, &spatializer);

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.cone = NULL;

    emitter.cone = NULL;
    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    emitter.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    emitter.position = (ForgeVector3){0.25f, 0.0f, 1.0f};
    emitter.velocity = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.inner_radius = 0.5f;
    emitter.channel_count = 3;
    emitter.channel_radius = 0.25f;
    emitter.channel_azimuths = channel_azimuths;
    emitter.volume_curve = NULL;
    emitter.lfe_curve = NULL;
    emitter.lpf_direct_curve = NULL;
    emitter.lpf_reverb_curve = NULL;
    emitter.reverb_curve = NULL;
    emitter.curve_distance_scaler = 1.0f;
    emitter.doppler_scaler = 0.0f;

    dsp_settings.matrix_coefficients = matrix;
    dsp_settings.delay_times = NULL;
    dsp_settings.src_channel_count = 3;
    dsp_settings.dst_channel_count = 6;

    forge_spatializer_calculate(&spatializer, &listener, &emitter, FORGE_SPATIAL_CALCULATE_MATRIX,
                                &dsp_settings);

    return check_finite_nonnegative("finite matrix", matrix, 18);
}

static ForgeNativeSpatialListener native_default_listener(void) {
    ForgeNativeSpatialListener listener;

    listener.orient_front = (ForgeVector3){0.0f, 0.0f, 1.0f};
    listener.orient_top = (ForgeVector3){0.0f, 1.0f, 0.0f};
    listener.position_m = (ForgeVector3){0.0f, 0.0f, 0.0f};
    listener.velocity_m_per_sec = (ForgeVector3){0.0f, 0.0f, 0.0f};

    return listener;
}

static ForgeNativeSpatialEmitter native_default_emitter(float x, float y, float z) {
    ForgeNativeSpatialEmitter emitter;

    emitter.orient_front = (ForgeVector3){0.0f, 0.0f, -1.0f};
    emitter.position_m = (ForgeVector3){x, y, z};
    emitter.velocity_m_per_sec = (ForgeVector3){0.0f, 0.0f, 0.0f};
    emitter.radius_m = 0.0f;
    emitter.reference_distance_m = 1.0f;
    emitter.doppler_scale = 1.0f;
    emitter.cone = NULL;
    emitter.direct_gain_curve = NULL;
    emitter.direct_lowpass_curve = NULL;
    emitter.reverb_send_curve = NULL;
    emitter.direct_occlusion = 0.0f;
    emitter.reverb_occlusion = 0.0f;
    emitter.occluded_lowpass_cutoff_hz = 0.0f;

    return emitter;
}

static float matrix_power(const float *matrix, uint32_t count) {
    float power = 0.0f;
    for (uint32_t i = 0; i < count; i += 1) {
        power += matrix[i] * matrix[i];
    }
    return power;
}

static uint32_t matrix_nonzero_count(const float *matrix, uint32_t count) {
    uint32_t nonzero = 0;
    for (uint32_t i = 0; i < count; i += 1) {
        if (matrix[i] > 0.0001f) {
            nonzero += 1;
        }
    }
    return nonzero;
}

static int native_calculate(uint32_t speaker_mask, ForgeNativeSpatialEmitter *emitter, uint32_t flags,
                            float *matrix, uint32_t dst_channels, ForgeNativeSpatialDspSettings *settings) {
    ForgeNativeSpatializer spatializer;
    ForgeNativeSpatialListener listener = native_default_listener();
    ForgeResult result;

    result = forge_native_spatializer_init(speaker_mask, 343.0f, &spatializer);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "native init failed with %d\n", result);
        return 1;
    }

    settings->matrix_coefficients = matrix;
    settings->src_channel_count = 1;
    settings->dst_channel_count = dst_channels;

    result = forge_native_spatializer_calculate(&spatializer, &listener, emitter, flags, settings);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "native calculate failed with %d\n", result);
        return 1;
    }

    return 0;
}

static int test_native_stereo_front_center_equal_power(void) {
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[2] = {0.0f, 0.0f};
    int failures = 0;

    failures += native_calculate(FORGE_SPEAKER_STEREO, &emitter, 0, matrix, 2, &settings);
    if (!failures) {
        failures += check_close_tol("native stereo left power", matrix[0] * matrix[0], 0.5f, 0.00001f);
        failures += check_close_tol("native stereo right power", matrix[1] * matrix[1], 0.5f, 0.00001f);
        failures += check_close_tol("native stereo total power", matrix_power(matrix, 2), 1.0f, 0.00001f);
    }

    return failures;
}

static int test_native_center_speaker_anchors_front_center(void) {
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int failures = 0;

    failures += native_calculate(FORGE_SPEAKER_5POINT1, &emitter, 0, matrix, 6, &settings);
    if (!failures) {
        failures += check_close_tol("native front-center FL", matrix[0], 0.0f, 0.00001f);
        failures += check_close_tol("native front-center FR", matrix[1], 0.0f, 0.00001f);
        failures += check_close_tol("native front-center C", matrix[2], 1.0f, 0.00001f);
        failures += check_close("native front-center LFE", matrix[3], 0.0f);
    }

    return failures;
}

static int test_native_vbap_preserves_power_across_pair_boundaries(void) {
    const float positions[4][3] = {
        {0.0f, 0.0f, 1.0f},
        {0.2679492f, 0.0f, 1.0f},
        {0.5773503f, 0.0f, 1.0f},
        {-0.2679492f, 0.0f, 1.0f},
    };
    int failures = 0;

    for (uint32_t i = 0; i < 4; i += 1) {
        ForgeNativeSpatialEmitter emitter = native_default_emitter(positions[i][0], positions[i][1], positions[i][2]);
        ForgeNativeSpatialDspSettings settings;
        float matrix[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        char label[64];

        failures += native_calculate(FORGE_SPEAKER_5POINT1, &emitter, 0, matrix, 6, &settings);
        snprintf(label, sizeof(label), "native VBAP power %u", i);
        failures += check_close_tol(label, matrix_power(matrix, 6), settings.direct_gain * settings.direct_gain,
                                    0.00001f);
    }

    return failures;
}

static int test_native_source_radius_spreads_and_preserves_power(void) {
    ForgeNativeSpatialEmitter point = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialEmitter wide = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings point_settings;
    ForgeNativeSpatialDspSettings wide_settings;
    float point_matrix[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float wide_matrix[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int failures = 0;

    wide.radius_m = 1.0f;

    failures += native_calculate(FORGE_SPEAKER_5POINT1_SURROUND, &point, 0, point_matrix, 6, &point_settings);
    failures += native_calculate(FORGE_SPEAKER_5POINT1_SURROUND, &wide, 0, wide_matrix, 6, &wide_settings);
    if (!failures) {
        if (matrix_nonzero_count(wide_matrix, 6) <= matrix_nonzero_count(point_matrix, 6)) {
            fprintf(stderr, "native radius did not spread energy across more speakers\n");
            failures += 1;
        }
        failures += check_close_tol("native point power", matrix_power(point_matrix, 6), 1.0f, 0.00001f);
        failures += check_close_tol("native radius power", matrix_power(wide_matrix, 6), 1.0f, 0.00001f);
    }

    return failures;
}

static int test_native_default_inverse_distance_gain(void) {
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 4.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[2] = {0.0f, 0.0f};
    int failures = 0;

    emitter.reference_distance_m = 2.0f;

    failures += native_calculate(FORGE_SPEAKER_STEREO, &emitter, 0, matrix, 2, &settings);
    if (!failures) {
        failures += check_close("native inverse distance direct gain", settings.direct_gain, 0.5f);
        failures += check_close_tol("native inverse distance matrix power", matrix_power(matrix, 2), 0.25f, 0.00001f);
    }

    return failures;
}

static int test_native_meter_based_curves_interpolate(void) {
    const ForgeNativeSpatialDistanceCurvePoint gain_points[2] = {{1.0f, 0.8f}, {3.0f, 0.4f}};
    const ForgeNativeSpatialDistanceCurvePoint lpf_points[2] = {{1.0f, 20000.0f}, {3.0f, 10000.0f}};
    const ForgeNativeSpatialDistanceCurvePoint reverb_points[2] = {{1.0f, 0.2f}, {3.0f, 0.6f}};
    const ForgeNativeSpatialDistanceCurve gain_curve = {gain_points, 2};
    const ForgeNativeSpatialDistanceCurve lpf_curve = {lpf_points, 2};
    const ForgeNativeSpatialDistanceCurve reverb_curve = {reverb_points, 2};
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 2.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[2] = {0.0f, 0.0f};
    int failures = 0;

    emitter.direct_gain_curve = &gain_curve;
    emitter.direct_lowpass_curve = &lpf_curve;
    emitter.reverb_send_curve = &reverb_curve;

    failures += native_calculate(FORGE_SPEAKER_STEREO, &emitter, 0, matrix, 2, &settings);
    if (!failures) {
        failures += check_close("native curve direct gain", settings.direct_gain, 0.6f);
        failures += check_close("native curve lowpass", settings.direct_lowpass_cutoff_hz, 15000.0f);
        failures += check_close("native curve reverb", settings.reverb_send_gain, 0.4f);
    }

    return failures;
}

static int test_native_cone_uses_unsigned_angles_for_gain_filter_reverb(void) {
    ForgeNativeSpatialCone cone;
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[2] = {0.0f, 0.0f};
    int failures = 0;

    cone.inner_angle_rad = 0.0f;
    cone.outer_angle_rad = FORGE_SPATIAL_PI;
    cone.inner_direct_gain = 1.0f;
    cone.outer_direct_gain = 0.25f;
    cone.inner_lowpass_cutoff_hz = 20000.0f;
    cone.outer_lowpass_cutoff_hz = 1000.0f;
    cone.inner_reverb_send_gain = 0.0f;
    cone.outer_reverb_send_gain = 0.8f;
    emitter.cone = &cone;
    emitter.orient_front = (ForgeVector3){1.0f, 0.0f, 0.0f};

    failures += native_calculate(FORGE_SPEAKER_STEREO, &emitter, 0, matrix, 2, &settings);
    if (!failures) {
        failures += check_close("native cone gain", settings.direct_gain, 0.625f);
        failures += check_close("native cone lowpass", settings.direct_lowpass_cutoff_hz, 10500.0f);
        failures += check_close("native cone reverb", settings.reverb_send_gain, 0.4f);
    }

    return failures;
}

static int test_native_occlusion_direct_muffle_and_reverb_are_separate(void) {
    const ForgeNativeSpatialDistanceCurvePoint reverb_points[2] = {{0.0f, 0.8f}, {10.0f, 0.8f}};
    const ForgeNativeSpatialDistanceCurve reverb_curve = {reverb_points, 2};
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[2] = {0.0f, 0.0f};
    int failures = 0;

    emitter.reverb_send_curve = &reverb_curve;
    emitter.direct_occlusion = 0.5f;
    emitter.reverb_occlusion = 0.25f;
    emitter.occluded_lowpass_cutoff_hz = 2000.0f;

    failures += native_calculate(FORGE_SPEAKER_STEREO, &emitter, 0, matrix, 2, &settings);
    if (!failures) {
        failures += check_close("native occlusion direct gain", settings.direct_gain, 0.5f);
        failures += check_close("native occlusion reverb", settings.reverb_send_gain, 0.6f);
        if (!(settings.direct_lowpass_cutoff_hz > 2000.0f && settings.direct_lowpass_cutoff_hz < 20000.0f)) {
            fprintf(stderr, "native occlusion lowpass expected log-muffled cutoff, got %.8f\n",
                    settings.direct_lowpass_cutoff_hz);
            failures += 1;
        }
    }

    return failures;
}

static int test_native_mono_source_never_writes_destination_lfe(void) {
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[3] = {0.0f, 0.0f, -1.0f};
    int failures = 0;

    failures += native_calculate(FORGE_SPEAKER_2POINT1, &emitter, 0, matrix, 3, &settings);
    if (!failures) {
        failures += check_close("native 2.1 LFE", matrix[2], 0.0f);
    }

    return failures;
}

static int test_native_rejects_multichannel_and_height_layouts(void) {
    ForgeNativeSpatializer spatializer;
    ForgeNativeSpatialListener listener = native_default_listener();
    ForgeNativeSpatialEmitter emitter = native_default_emitter(0.0f, 0.0f, 1.0f);
    ForgeNativeSpatialDspSettings settings;
    float matrix[2] = {0.0f, 0.0f};
    int failures = 0;

    failures += check_result("native height init",
                             forge_native_spatializer_init(FORGE_SPEAKER_TOP_CENTER, 343.0f, &spatializer),
                             ForgeResultInvalidArgument);
    failures += check_result("native stereo init", forge_native_spatializer_init(FORGE_SPEAKER_STEREO, 343.0f,
                                                                                 &spatializer),
                             ForgeResultSuccess);
    settings.matrix_coefficients = matrix;
    settings.src_channel_count = 2;
    settings.dst_channel_count = 2;
    failures += check_result("native multichannel source",
                             forge_native_spatializer_calculate(&spatializer, &listener, &emitter, 0, &settings),
                             ForgeResultInvalidArgument);

    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_listener_cone_uses_listener_to_emitter_direction();
    failures += test_multichannel_lfe_skips_destination_without_lfe();
    failures += test_inner_radius_minimum_diffusion_is_continuous_above_null_distance();
    failures += test_zero_distance_matrix_is_continuous_with_near_null_distance();
    failures += test_zero_center_skips_center_channel();
    failures += test_redirect_to_lfe_writes_lfe_channel();
    failures += test_acos_inputs_resist_valid_vector_roundoff();
    failures += test_matrix_coefficients_are_finite_and_nonnegative();
    failures += test_native_stereo_front_center_equal_power();
    failures += test_native_center_speaker_anchors_front_center();
    failures += test_native_vbap_preserves_power_across_pair_boundaries();
    failures += test_native_source_radius_spreads_and_preserves_power();
    failures += test_native_default_inverse_distance_gain();
    failures += test_native_meter_based_curves_interpolate();
    failures += test_native_cone_uses_unsigned_angles_for_gain_filter_reverb();
    failures += test_native_occlusion_direct_muffle_and_reverb_are_separate();
    failures += test_native_mono_source_never_writes_destination_lfe();
    failures += test_native_rejects_multichannel_and_height_layouts();

    return failures;
}
