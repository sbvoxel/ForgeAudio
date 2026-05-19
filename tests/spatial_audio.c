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

    return failures;
}
