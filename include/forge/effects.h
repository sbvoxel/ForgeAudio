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

#ifndef FORGE_EFFECTS_H
#define FORGE_EFFECTS_H

#include <forge/effect.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Structures */

#pragma pack(push, 1)

typedef struct ForgeVolumeMeterLevels {
    float *peak_levels;
    float *rms_levels;
    uint32_t channel_count;
} ForgeVolumeMeterLevels;

typedef struct ForgeLimiterParameters {
    float input_gain_db;
    float ceiling_db;
    float lookahead_ms;
    float release_ms;
} ForgeLimiterParameters;

typedef struct ForgeDelayParameters {
    float wet_dry_mix; /* Percent, [FORGE_DELAY_MIN_WET_DRY_MIX, FORGE_DELAY_MAX_WET_DRY_MIX] */
    float delay_ms;    /* Milliseconds. Hard-set in v1; changes may click or pitch-warp. */
    float feedback;    /* Linear feedback gain, clamped below 1.0 to avoid runaway. */
    float lowpass_hz;  /* Feedback damping cutoff. 0 disables damping; values at/above Nyquist bypass. */
} ForgeDelayParameters;

typedef struct ForgeReverbParameters {
    float wet_dry_mix;
    uint32_t reflections_delay;
    uint8_t reverb_delay;
    uint8_t rear_delay;
    uint8_t position_left;
    uint8_t position_right;
    uint8_t position_matrix_left;
    uint8_t position_matrix_right;
    uint8_t early_diffusion;
    uint8_t late_diffusion;
    uint8_t low_eq_gain;
    uint8_t low_eq_cutoff;
    uint8_t high_eq_gain;
    uint8_t high_eq_cutoff;
    float room_filter_freq;
    float room_filter_main;
    float room_filter_hf;
    float reflections_gain;
    float reverb_gain;
    float decay_time;
    float density;
    float room_size;
} ForgeReverbParameters;

typedef struct ForgeReverbParameters7Point1 {
    float wet_dry_mix;
    uint32_t reflections_delay;
    uint8_t reverb_delay;
    uint8_t rear_delay;
    uint8_t side_delay;
    uint8_t position_left;
    uint8_t position_right;
    uint8_t position_matrix_left;
    uint8_t position_matrix_right;
    uint8_t early_diffusion;
    uint8_t late_diffusion;
    uint8_t low_eq_gain;
    uint8_t low_eq_cutoff;
    uint8_t high_eq_gain;
    uint8_t high_eq_cutoff;
    float room_filter_freq;
    float room_filter_main;
    float room_filter_hf;
    float reflections_gain;
    float reverb_gain;
    float decay_time;
    float density;
    float room_size;
} ForgeReverbParameters7Point1;

typedef struct ForgeReverbTarget {
    uint32_t field_mask;  /* Mix of FORGE_REVERB_TARGET_* flags. */
    float wet_dry_mix;   /* Percent, [FORGE_REVERB_MIN_WET_DRY_MIX, FORGE_REVERB_MAX_WET_DRY_MIX] */
    float reflections_gain; /* dB, [FORGE_REVERB_MIN_REFLECTIONS_GAIN, FORGE_REVERB_MAX_REFLECTIONS_GAIN] */
    float reverb_gain;      /* dB, [FORGE_REVERB_MIN_REVERB_GAIN, FORGE_REVERB_MAX_REVERB_GAIN] */
    float room_filter_main; /* dB, [FORGE_REVERB_MIN_ROOM_FILTER_MAIN, FORGE_REVERB_MAX_ROOM_FILTER_MAIN] */
    float room_filter_hf;   /* dB, [FORGE_REVERB_MIN_ROOM_FILTER_HF, FORGE_REVERB_MAX_ROOM_FILTER_HF] */
} ForgeReverbTarget;

typedef struct ForgeReverbI3DL2Parameters {
    float wet_dry_mix;
    int32_t room;
    int32_t room_hf;
    float room_rolloff_factor;
    float decay_time;
    float decay_hf_ratio;
    int32_t reflections;
    float reflections_delay;
    int32_t reverb;
    float reverb_delay;
    float diffusion;
    float density;
    float hf_reference;
} ForgeReverbI3DL2Parameters;

#pragma pack(pop)

/* Constants */

#define FORGE_LIMITER_MIN_INPUT_GAIN_DB -60.0f
#define FORGE_LIMITER_MAX_INPUT_GAIN_DB 60.0f
#define FORGE_LIMITER_DEFAULT_INPUT_GAIN_DB 0.0f

#define FORGE_LIMITER_MIN_CEILING_DB -60.0f
#define FORGE_LIMITER_MAX_CEILING_DB 0.0f
#define FORGE_LIMITER_DEFAULT_CEILING_DB -1.0f

#define FORGE_LIMITER_MIN_LOOKAHEAD_MS 0.0f
#define FORGE_LIMITER_MAX_LOOKAHEAD_MS 5.0f
#define FORGE_LIMITER_DEFAULT_LOOKAHEAD_MS 1.5f

#define FORGE_LIMITER_MIN_RELEASE_MS 1.0f
#define FORGE_LIMITER_MAX_RELEASE_MS 1000.0f
#define FORGE_LIMITER_DEFAULT_RELEASE_MS 50.0f

#define FORGE_DELAY_MIN_WET_DRY_MIX 0.0f
#define FORGE_DELAY_MAX_WET_DRY_MIX 100.0f
#define FORGE_DELAY_DEFAULT_WET_DRY_MIX 50.0f

#define FORGE_DELAY_MIN_DELAY_MS 1.0f
#define FORGE_DELAY_MAX_DELAY_MS 2000.0f
#define FORGE_DELAY_DEFAULT_DELAY_MS 250.0f

#define FORGE_DELAY_MIN_FEEDBACK 0.0f
#define FORGE_DELAY_MAX_FEEDBACK 0.95f
#define FORGE_DELAY_DEFAULT_FEEDBACK 0.35f

#define FORGE_DELAY_BYPASS_LOWPASS_HZ 0.0f
#define FORGE_DELAY_MIN_LOWPASS_HZ 0.0f
#define FORGE_DELAY_MAX_LOWPASS_HZ 20000.0f
#define FORGE_DELAY_DEFAULT_LOWPASS_HZ FORGE_DELAY_BYPASS_LOWPASS_HZ

#define FORGE_REVERB_MIN_SAMPLE_RATE 20000
#define FORGE_REVERB_MAX_SAMPLE_RATE 48000

#define FORGE_REVERB_MIN_WET_DRY_MIX 0.0f
#define FORGE_REVERB_MIN_REFLECTIONS_DELAY 0
#define FORGE_REVERB_MIN_REVERB_DELAY 0
#define FORGE_REVERB_MIN_REAR_DELAY 0
#define FORGE_REVERB_MIN_7_1_SIDE_DELAY 0
#define FORGE_REVERB_MIN_7_1_REAR_DELAY 0
#define FORGE_REVERB_MIN_POSITION 0
#define FORGE_REVERB_MIN_DIFFUSION 0
#define FORGE_REVERB_MIN_LOW_EQ_GAIN 0
#define FORGE_REVERB_MIN_LOW_EQ_CUTOFF 0
#define FORGE_REVERB_MIN_HIGH_EQ_GAIN 0
#define FORGE_REVERB_MIN_HIGH_EQ_CUTOFF 0
#define FORGE_REVERB_MIN_ROOM_FILTER_FREQ 20.0f
#define FORGE_REVERB_MIN_ROOM_FILTER_MAIN -100.0f
#define FORGE_REVERB_MIN_ROOM_FILTER_HF -100.0f
#define FORGE_REVERB_MIN_REFLECTIONS_GAIN -100.0f
#define FORGE_REVERB_MIN_REVERB_GAIN -100.0f
#define FORGE_REVERB_MIN_DECAY_TIME 0.1f
#define FORGE_REVERB_MIN_DENSITY 0.0f
#define FORGE_REVERB_MIN_ROOM_SIZE 0.0f

#define FORGE_REVERB_MAX_WET_DRY_MIX 100.0f
#define FORGE_REVERB_MAX_REFLECTIONS_DELAY 300
#define FORGE_REVERB_MAX_REVERB_DELAY 85
#define FORGE_REVERB_MAX_REAR_DELAY 5
#define FORGE_REVERB_MAX_7_1_SIDE_DELAY 5
#define FORGE_REVERB_MAX_7_1_REAR_DELAY 20
#define FORGE_REVERB_MAX_POSITION 30
#define FORGE_REVERB_MAX_DIFFUSION 15
#define FORGE_REVERB_MAX_LOW_EQ_GAIN 12
#define FORGE_REVERB_MAX_LOW_EQ_CUTOFF 9
#define FORGE_REVERB_MAX_HIGH_EQ_GAIN 8
#define FORGE_REVERB_MAX_HIGH_EQ_CUTOFF 14
#define FORGE_REVERB_MAX_ROOM_FILTER_FREQ 20000.0f
#define FORGE_REVERB_MAX_ROOM_FILTER_MAIN 0.0f
#define FORGE_REVERB_MAX_ROOM_FILTER_HF 0.0f
#define FORGE_REVERB_MAX_REFLECTIONS_GAIN 20.0f
#define FORGE_REVERB_MAX_REVERB_GAIN 20.0f
#define FORGE_REVERB_MAX_DENSITY 100.0f
#define FORGE_REVERB_MAX_ROOM_SIZE 100.0f

#define FORGE_REVERB_DEFAULT_WET_DRY_MIX 100.0f
#define FORGE_REVERB_DEFAULT_REFLECTIONS_DELAY 5
#define FORGE_REVERB_DEFAULT_REVERB_DELAY 5
#define FORGE_REVERB_DEFAULT_REAR_DELAY 5
#define FORGE_REVERB_DEFAULT_7_1_SIDE_DELAY 5
#define FORGE_REVERB_DEFAULT_7_1_REAR_DELAY 20
#define FORGE_REVERB_DEFAULT_POSITION 6
#define FORGE_REVERB_DEFAULT_POSITION_MATRIX 27
#define FORGE_REVERB_DEFAULT_EARLY_DIFFUSION 8
#define FORGE_REVERB_DEFAULT_LATE_DIFFUSION 8
#define FORGE_REVERB_DEFAULT_LOW_EQ_GAIN 8
#define FORGE_REVERB_DEFAULT_LOW_EQ_CUTOFF 4
#define FORGE_REVERB_DEFAULT_HIGH_EQ_GAIN 8
#define FORGE_REVERB_DEFAULT_HIGH_EQ_CUTOFF 4
#define FORGE_REVERB_DEFAULT_ROOM_FILTER_FREQ 5000.0f
#define FORGE_REVERB_DEFAULT_ROOM_FILTER_MAIN 0.0f
#define FORGE_REVERB_DEFAULT_ROOM_FILTER_HF 0.0f
#define FORGE_REVERB_DEFAULT_REFLECTIONS_GAIN 0.0f
#define FORGE_REVERB_DEFAULT_REVERB_GAIN 0.0f
#define FORGE_REVERB_DEFAULT_DECAY_TIME 1.0f
#define FORGE_REVERB_DEFAULT_DENSITY 100.0f
#define FORGE_REVERB_DEFAULT_ROOM_SIZE 100.0f

#define FORGE_REVERB_TARGET_WET_DRY_MIX 0x00000001u
#define FORGE_REVERB_TARGET_REFLECTIONS_GAIN 0x00000002u
#define FORGE_REVERB_TARGET_REVERB_GAIN 0x00000004u
#define FORGE_REVERB_TARGET_ROOM_FILTER_MAIN 0x00000008u
#define FORGE_REVERB_TARGET_ROOM_FILTER_HF 0x00000010u
#define FORGE_REVERB_TARGET_ALL                                                                                       \
    (FORGE_REVERB_TARGET_WET_DRY_MIX | FORGE_REVERB_TARGET_REFLECTIONS_GAIN | FORGE_REVERB_TARGET_REVERB_GAIN |       \
     FORGE_REVERB_TARGET_ROOM_FILTER_MAIN | FORGE_REVERB_TARGET_ROOM_FILTER_HF)

#define FORGE_REVERB_I3DL2_PRESET_DEFAULT                                                                              \
    {100, -10000, 0, 0.0f, 1.00f, 0.50f, -10000, 0.020f, -10000, 0.040f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_GENERIC                                                                              \
    {100, -1000, -100, 0.0f, 1.49f, 0.83f, -2602, 0.007f, 200, 0.011f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_PADDEDCELL                                                                           \
    {100, -1000, -6000, 0.0f, 0.17f, 0.10f, -1204, 0.001f, 207, 0.002f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_ROOM                                                                                 \
    {100, -1000, -454, 0.0f, 0.40f, 0.83f, -1646, 0.002f, 53, 0.003f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_BATHROOM                                                                             \
    {100, -1000, -1200, 0.0f, 1.49f, 0.54f, -370, 0.007f, 1030, 0.011f, 100.0f, 60.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_LIVINGROOM                                                                           \
    {100, -1000, -6000, 0.0f, 0.50f, 0.10f, -1376, 0.003f, -1104, 0.004f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_STONEROOM                                                                            \
    {100, -1000, -300, 0.0f, 2.31f, 0.64f, -711, 0.012f, 83, 0.017f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_AUDITORIUM                                                                           \
    {100, -1000, -476, 0.0f, 4.32f, 0.59f, -789, 0.020f, -289, 0.030f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_CONCERTHALL                                                                          \
    {100, -1000, -500, 0.0f, 3.92f, 0.70f, -1230, 0.020f, -2, 0.029f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_CAVE                                                                                 \
    {100, -1000, 0, 0.0f, 2.91f, 1.30f, -602, 0.015f, -302, 0.022f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_ARENA                                                                                \
    {100, -1000, -698, 0.0f, 7.24f, 0.33f, -1166, 0.020f, 16, 0.030f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_HANGAR                                                                               \
    {100, -1000, -1000, 0.0f, 10.05f, 0.23f, -602, 0.020f, 198, 0.030f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_CARPETEDHALLWAY                                                                      \
    {100, -1000, -4000, 0.0f, 0.30f, 0.10f, -1831, 0.002f, -1630, 0.030f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_HALLWAY                                                                              \
    {100, -1000, -300, 0.0f, 1.49f, 0.59f, -1219, 0.007f, 441, 0.011f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_STONECORRIDOR                                                                        \
    {100, -1000, -237, 0.0f, 2.70f, 0.79f, -1214, 0.013f, 395, 0.020f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_ALLEY                                                                                \
    {100, -1000, -270, 0.0f, 1.49f, 0.86f, -1204, 0.007f, -4, 0.011f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_FOREST                                                                               \
    {100, -1000, -3300, 0.0f, 1.49f, 0.54f, -2560, 0.162f, -613, 0.088f, 79.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_CITY                                                                                 \
    {100, -1000, -800, 0.0f, 1.49f, 0.67f, -2273, 0.007f, -2217, 0.011f, 50.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_MOUNTAINS                                                                            \
    {100, -1000, -2500, 0.0f, 1.49f, 0.21f, -2780, 0.300f, -2014, 0.100f, 27.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_QUARRY                                                                               \
    {100, -1000, -1000, 0.0f, 1.49f, 0.83f, -10000, 0.061f, 500, 0.025f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_PLAIN                                                                                \
    {100, -1000, -2000, 0.0f, 1.49f, 0.50f, -2466, 0.179f, -2514, 0.100f, 21.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_PARKINGLOT                                                                           \
    {100, -1000, 0, 0.0f, 1.65f, 1.50f, -1363, 0.008f, -1153, 0.012f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_SEWERPIPE                                                                            \
    {100, -1000, -1000, 0.0f, 2.81f, 0.14f, 429, 0.014f, 648, 0.021f, 80.0f, 60.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_UNDERWATER                                                                           \
    {100, -1000, -4000, 0.0f, 1.49f, 0.10f, -449, 0.007f, 1700, 0.011f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_SMALLROOM                                                                            \
    {100, -1000, -600, 0.0f, 1.10f, 0.83f, -400, 0.005f, 500, 0.010f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_MEDIUMROOM                                                                           \
    {100, -1000, -600, 0.0f, 1.30f, 0.83f, -1000, 0.010f, -200, 0.020f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_LARGEROOM                                                                            \
    {100, -1000, -600, 0.0f, 1.50f, 0.83f, -1600, 0.020f, -1000, 0.040f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_MEDIUMHALL                                                                           \
    {100, -1000, -600, 0.0f, 1.80f, 0.70f, -1300, 0.015f, -800, 0.030f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_LARGEHALL                                                                            \
    {100, -1000, -600, 0.0f, 1.80f, 0.70f, -2000, 0.030f, -1400, 0.060f, 100.0f, 100.0f, 5000.0f}
#define FORGE_REVERB_I3DL2_PRESET_PLATE                                                                                \
    {100, -1000, -200, 0.0f, 1.30f, 0.90f, 0, 0.002f, 0, 0.010f, 100.0f, 75.0f, 5000.0f}

/* Functions */

/* These return caller-owned ForgeEffect objects. Destroy with forge_effect_destroy,
 * or transfer to a voice effect chain.
 */
FORGE_AUDIO_API ForgeResult forge_create_volume_meter(ForgeEffect **effect, uint32_t flags);
FORGE_AUDIO_API ForgeResult forge_create_limiter(ForgeEffect **effect, uint32_t flags);
FORGE_AUDIO_API ForgeResult forge_create_delay(ForgeEffect **effect, uint32_t flags);
FORGE_AUDIO_API ForgeResult forge_create_reverb(ForgeEffect **effect, uint32_t flags);
FORGE_AUDIO_API ForgeResult forge_create_reverb_7point1(ForgeEffect **effect, uint32_t flags);
FORGE_AUDIO_API void forge_volume_meter_get_levels(ForgeEffect *effect, ForgeVolumeMeterLevels *levels);

FORGE_AUDIO_API ForgeResult forge_create_volume_meter_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                                     ForgeMallocFunc custom_malloc,
                                                                     ForgeFreeFunc custom_free,
                                                                     ForgeReallocFunc custom_realloc);
FORGE_AUDIO_API ForgeResult forge_create_limiter_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                                ForgeMallocFunc custom_malloc,
                                                                ForgeFreeFunc custom_free,
                                                                ForgeReallocFunc custom_realloc);
FORGE_AUDIO_API ForgeResult forge_create_delay_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                              ForgeMallocFunc custom_malloc,
                                                              ForgeFreeFunc custom_free,
                                                              ForgeReallocFunc custom_realloc);
FORGE_AUDIO_API ForgeResult forge_create_reverb_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                               ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                                               ForgeReallocFunc custom_realloc);
FORGE_AUDIO_API ForgeResult forge_create_reverb_7point1_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                                       ForgeMallocFunc custom_malloc,
                                                                       ForgeFreeFunc custom_free,
                                                                       ForgeReallocFunc custom_realloc);

FORGE_AUDIO_API void forge_reverb_convert_i3dl2(const ForgeReverbI3DL2Parameters *i3dl2,
                                                ForgeReverbParameters *parameters);
FORGE_AUDIO_API void forge_reverb_convert_i3dl2_7point1(const ForgeReverbI3DL2Parameters *i3dl2,
                                                        ForgeReverbParameters7Point1 *parameters,
                                                        int32_t use_7point1_rear_delay);

/* Targets selected cheap continuous reverb parameters using ForgeAudio's
 * internal default de-zip duration. Only FORGE_REVERB_TARGET_* fields are
 * affected; omitted fields keep their current value and active automation.
 * This API is valid for both forge_create_reverb and forge_create_reverb_7point1
 * effect slots.
 */
FORGE_AUDIO_API ForgeResult forge_voice_set_reverb_parameters_target(ForgeVoice *voice, uint32_t effect_index,
                                                                     const ForgeReverbTarget *target,
                                                                     ForgeAudioBatchId batch_id);

/* Ramps selected cheap continuous reverb parameters over exact rendered frames.
 * Blob parameter sets remain hard sets and cancel active typed reverb automation
 * when they are applied on the audio timeline.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_reverb_parameters_frames(ForgeVoice *voice, uint32_t effect_index,
                                                                      const ForgeReverbTarget *target,
                                                                      uint32_t duration_frames,
                                                                      ForgeAudioBatchId batch_id);

/* Ramps selected cheap continuous reverb parameters over milliseconds converted
 * through forge_audio_ms_to_frames when this function is called.
 */
FORGE_AUDIO_API ForgeResult forge_voice_ramp_reverb_parameters_ms(ForgeVoice *voice, uint32_t effect_index,
                                                                  const ForgeReverbTarget *target,
                                                                  double duration_ms,
                                                                  ForgeAudioBatchId batch_id);

/* Gets the current effective standard reverb parameters. During active typed
 * ramps, smoothable fields report the current timeline value; hard-set-only
 * fields report their current hard-set value. Returns ForgeResultInvalidCall
 * for 7.1 reverb slots.
 */
FORGE_AUDIO_API ForgeResult forge_voice_get_reverb_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                              ForgeReverbParameters *parameters);

/* Gets the current effective 7.1 reverb parameters. Returns
 * ForgeResultInvalidCall for standard reverb slots.
 */
FORGE_AUDIO_API ForgeResult forge_voice_get_reverb_7point1_parameters(ForgeVoice *voice, uint32_t effect_index,
                                                                      ForgeReverbParameters7Point1 *parameters);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FORGE_EFFECTS_H */
