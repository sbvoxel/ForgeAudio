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

#include <forge/effects.h>
#include "effect_base_internal.h"
#include "format_internal.h"

/* #define DISABLE_SUBNORMALS */
#ifdef DISABLE_SUBNORMALS
#include <math.h> /* ONLY USE THIS FOR fpclassify/_fpclass! */
#define IS_SUBNORMAL(a) (fpclassify(a) == FP_SUBNORMAL)
#endif /* DISABLE_SUBNORMALS */

/* Utility Functions */

static inline float DbGainToFactor(float gain) {
    return (float)forge_pow(10, gain / 20.0f);
}

static inline uint32_t MsToSamples(float msec, int32_t sampleRate) {
    return (uint32_t)((sampleRate * msec) / 1000.0f);
}

#ifndef DISABLE_SUBNORMALS
#define Undenormalize(a) ((a))
#else  /* DISABLE_SUBNORMALS */
static inline float Undenormalize(float sample_in) {
    if (IS_SUBNORMAL(sample_in)) {
        return 0.0f;
    }
    return sample_in;
}
#endif /* DISABLE_SUBNORMALS */

/* Component - delay */

#define DSP_DELAY_MAX_DELAY_MS 300

typedef struct DspDelay {
    int32_t sampleRate;
    uint32_t capacity; /* In samples */
    uint32_t delay;    /* In samples */
    uint32_t read_idx;
    uint32_t write_idx;
    float *buffer;
} DspDelay;

static inline void DspDelay_Initialize(DspDelay *filter, int32_t sampleRate, float delay_ms,
                                       ForgeMallocFunc malloc_func) {
    forge_assert(delay_ms >= 0 && delay_ms <= DSP_DELAY_MAX_DELAY_MS);

    filter->sampleRate = sampleRate;
    filter->capacity = MsToSamples(DSP_DELAY_MAX_DELAY_MS, sampleRate);
    filter->delay = MsToSamples(delay_ms, sampleRate);
    filter->read_idx = 0;
    filter->write_idx = filter->delay;
    filter->buffer = (float *)malloc_func(filter->capacity * sizeof(float));
    forge_zero(filter->buffer, filter->capacity * sizeof(float));
}

static inline void DspDelay_Change(DspDelay *filter, float delay_ms) {
    forge_assert(delay_ms >= 0 && delay_ms <= DSP_DELAY_MAX_DELAY_MS);

    /* Length */
    filter->delay = MsToSamples(delay_ms, filter->sampleRate);
    filter->read_idx = (filter->write_idx - filter->delay + filter->capacity) % filter->capacity;
}

static inline float DspDelay_Read(DspDelay *filter) {
    float delay_out;

    forge_assert(filter->read_idx < filter->capacity);

    delay_out = filter->buffer[filter->read_idx];
    filter->read_idx = (filter->read_idx + 1) % filter->capacity;
    return delay_out;
}

static inline void DspDelay_Write(DspDelay *filter, float sample) {
    forge_assert(filter->write_idx < filter->capacity);

    filter->buffer[filter->write_idx] = sample;
    filter->write_idx = (filter->write_idx + 1) % filter->capacity;
}

static inline float DspDelay_Process(DspDelay *filter, float sample_in) {
    float delay_out = DspDelay_Read(filter);
    DspDelay_Write(filter, sample_in);
    return delay_out;
}

static inline void DspDelay_Reset(DspDelay *filter) {
    filter->read_idx = 0;
    filter->write_idx = filter->delay;
    forge_zero(filter->buffer, filter->capacity * sizeof(float));
}

static inline void DspDelay_Destroy(DspDelay *filter, ForgeFreeFunc free_func) {
    free_func(filter->buffer);
}

static inline float DspComb_FeedbackFromRT60(DspDelay *delay, float rt60_ms) {
    float exponent = ((-3.0f * delay->delay * 1000.0f) / (delay->sampleRate * rt60_ms));
    return (float)forge_pow(10.0f, exponent);
}

/* Component - Bi-Quad Filter */

typedef enum DspBiQuadType {
    DSP_BIQUAD_LOWSHELVING,
    DSP_BIQUAD_HIGHSHELVING
} DspBiQuadType;

typedef struct DspBiQuad {
    int32_t sampleRate;
    float a0, a1, a2;
    float b1, b2;
    float c0, d0;
    float delay0, delay1;
} DspBiQuad;

static inline void DspBiQuad_Change(DspBiQuad *filter, DspBiQuadType type, float frequency, float q, float gain) {
    const float TWOPI = 6.283185307179586476925286766559005;
    float theta_c = (TWOPI * frequency) / (float)filter->sampleRate;
    float mu = DbGainToFactor(gain);
    float beta = (type == DSP_BIQUAD_LOWSHELVING) ? 4.0f / (1 + mu) : (1 + mu) / 4.0f;
    float delta = beta * (float)forge_tan(theta_c * 0.5f);
    float gamma = (1 - delta) / (1 + delta);

    if (type == DSP_BIQUAD_LOWSHELVING) {
        filter->a0 = (1 - gamma) * 0.5f;
        filter->a1 = filter->a0;
    } else {
        filter->a0 = (1 + gamma) * 0.5f;
        filter->a1 = -filter->a0;
    }

    filter->a2 = 0.0f;
    filter->b1 = -gamma;
    filter->b2 = 0.0f;
    filter->c0 = mu - 1.0f;
    filter->d0 = 1.0f;
}

static inline void DspBiQuad_Initialize(DspBiQuad *filter, int32_t sampleRate, DspBiQuadType type,
                                        float frequency, /* Corner frequency */
                                        float q,         /* Only used by low/high-pass filters */
                                        float gain       /* Only used by low/high-shelving filters */
) {
    filter->sampleRate = sampleRate;
    filter->delay0 = 0.0f;
    filter->delay1 = 0.0f;
    DspBiQuad_Change(filter, type, frequency, q, gain);
}

static inline float DspBiQuad_Process(DspBiQuad *filter, float sample_in) {
    /* Direct Form II Transposed:
     * - Less delay registers than Direct Form I
     * - More numerically stable than Direct Form II
     */
    float result = (filter->a0 * sample_in) + filter->delay0;
    filter->delay0 = (filter->a1 * sample_in) - (filter->b1 * result) + filter->delay1;
    filter->delay1 = (filter->a2 * sample_in) - (filter->b2 * result);

    return Undenormalize((result * filter->c0) + (sample_in * filter->d0));
}

static inline void DspBiQuad_Reset(DspBiQuad *filter) {
    filter->delay0 = 0.0f;
    filter->delay1 = 0.0f;
}

static inline void DspBiQuad_Destroy(DspBiQuad *filter) {
}

/* Component - Comb Filter with Integrated Low/High Shelving Filters */

typedef struct DspCombShelving {
    DspDelay comb_delay;
    float comb_feedback_gain;

    DspBiQuad low_shelving;
    DspBiQuad high_shelving;
} DspCombShelving;

static inline void DspCombShelving_Initialize(DspCombShelving *filter, int32_t sampleRate, float delay_ms,
                                              float rt60_ms, float low_frequency, float low_gain, float high_frequency,
                                              float high_gain, ForgeMallocFunc malloc_func) {
    DspDelay_Initialize(&filter->comb_delay, sampleRate, delay_ms, malloc_func);
    filter->comb_feedback_gain = DspComb_FeedbackFromRT60(&filter->comb_delay, rt60_ms);

    DspBiQuad_Initialize(&filter->low_shelving, sampleRate, DSP_BIQUAD_LOWSHELVING, low_frequency, 0.0f, low_gain);
    DspBiQuad_Initialize(&filter->high_shelving, sampleRate, DSP_BIQUAD_HIGHSHELVING, high_frequency, 0.0f, high_gain);
}

static inline float DspCombShelving_Process(DspCombShelving *filter, float sample_in) {
    float delay_out, feedback, to_buf;

    delay_out = DspDelay_Read(&filter->comb_delay);

    /* Apply shelving filters */
    feedback = DspBiQuad_Process(&filter->high_shelving, delay_out);
    feedback = DspBiQuad_Process(&filter->low_shelving, feedback);

    /* Apply comb filter */
    to_buf = Undenormalize(sample_in + (filter->comb_feedback_gain * feedback));
    DspDelay_Write(&filter->comb_delay, to_buf);

    return delay_out;
}

static inline void DspCombShelving_Reset(DspCombShelving *filter) {
    DspDelay_Reset(&filter->comb_delay);
    DspBiQuad_Reset(&filter->low_shelving);
    DspBiQuad_Reset(&filter->high_shelving);
}

static inline void DspCombShelving_Destroy(DspCombShelving *filter, ForgeFreeFunc free_func) {
    DspDelay_Destroy(&filter->comb_delay, free_func);
    DspBiQuad_Destroy(&filter->low_shelving);
    DspBiQuad_Destroy(&filter->high_shelving);
}

/* Component - Delaying All-Pass Filter */

typedef struct DspAllPass {
    DspDelay delay;
    float feedback_gain;
} DspAllPass;

static inline void DspAllPass_Initialize(DspAllPass *filter, int32_t sampleRate, float delay_ms, float gain,
                                         ForgeMallocFunc malloc_func) {
    DspDelay_Initialize(&filter->delay, sampleRate, delay_ms, malloc_func);
    filter->feedback_gain = gain;
}

static inline void DspAllPass_Change(DspAllPass *filter, float delay_ms, float gain) {
    DspDelay_Change(&filter->delay, delay_ms);
    filter->feedback_gain = gain;
}

static inline float DspAllPass_Process(DspAllPass *filter, float sample_in) {
    float delay_out, to_buf;

    delay_out = DspDelay_Read(&filter->delay);

    to_buf = Undenormalize(sample_in + (filter->feedback_gain * delay_out));
    DspDelay_Write(&filter->delay, to_buf);

    return Undenormalize(delay_out - (filter->feedback_gain * to_buf));
}

static inline void DspAllPass_Reset(DspAllPass *filter) {
    DspDelay_Reset(&filter->delay);
}

static inline void DspAllPass_Destroy(DspAllPass *filter, ForgeFreeFunc free_func) {
    DspDelay_Destroy(&filter->delay, free_func);
}

/*
reverb network - loosely based on the reverberator from
"Designing Audio Effect Plug-Ins in C++" by Will Pirkle and
the classic classic Schroeder-Moorer reverberator with modifications
to fit the ForgeAudio reverb parameters.


    In    +--------+   +----+      +------------+      +-----+
  ----|--->PreDelay---->APF1---+--->Sub LeftCh  |----->|     |   Left Out
      |   +--------+   +----+  |   +------------+      | Wet |-------->
      |                        |   +------------+      |     |
      |                        |---|Sub RightCh |----->| Dry |
      |                            +------------+      |     |  Right Out
      |                                                | Mix |-------->
      +----------------------------------------------->|     |
                                                       +-----+
 Sub routine per channel :

     In  +-----+      +-----+ * cg
   ---+->|delay|--+---|Comb1|------+
      |  +-----+  |   +-----+      |
      |           |                |
      |           |   +-----+ * cg |
      |           +--->Comb2|------+
      |           |   +-----+      |     +-----+
      |           |                +---->| SUM |--------+
      |           |   +-----+ * cg |     +-----+        |
      |           +--->...  |------+                    |
      | * g0      |   +-----+      |                    |
      |           |                |                    |
      |           +--->-----+ * cg |                    |
      |               |Comb8|------+                    |
      |               +-----+                           |
      v                                                 |
   +-----+  g1   +----+   +----+   +----+   +----+      |
   | SUM |<------|APF4|<--|APF3|<--|APF2|<--|APF1|<-----+
   +-----+       +----+   +----+   +----+   +----+
      |
      |
      |            +-------------+          Out
      +----------->|RoomFilter   |------------------------>
                   +-------------+


Parameters:

float wet_dry_mix;        0 - 100 (0 = fully dry, 100 = fully wet)
uint32_t reflections_delay;    0 - 300 ms
uint8_t reverb_delay;        0 - 85 ms
uint8_t rear_delay;        0 - 5 ms
uint8_t position_left;        0 - 30
uint8_t position_right;        0 - 30
uint8_t position_matrix_left;    0 - 30
uint8_t position_matrix_right;    0 - 30
uint8_t early_diffusion;        0 - 15
uint8_t late_diffusion;        0 - 15
uint8_t low_eq_gain;        0 - 12 (formula dB = low_eq_gain - 8)
uint8_t low_eq_cutoff;        0 - 9  (formula Hz = 50 + (low_eq_cutoff * 50))
uint8_t high_eq_gain;        0 - 8  (formula dB = high_eq_gain - 8)
uint8_t high_eq_cutoff;        0 - 14 (formula Hz = 1000 + (HighEqCutoff * 500))
float room_filter_freq;        20 - 20000Hz
float room_filter_main;        -100 - 0dB
float room_filter_hf;        -100 - 0dB
float reflections_gain;        -100 - 20dB
float reverb_gain;        -100 - 20dB
float decay_time;        0.1 - .... ms
float density;            0 - 100 %
float room_size;            1 - 100 feet (NOT USED YET)

*/

#define REVERB_COUNT_COMB 8
#define REVERB_COUNT_APF_IN 1
#define REVERB_COUNT_APF_OUT 4

static const float COMB_DELAYS[REVERB_COUNT_COMB] = {25.31f, 26.94f, 28.96f, 30.75f, 32.24f, 33.80f, 35.31f, 36.67f};

static const float APF_IN_DELAYS[REVERB_COUNT_APF_IN] = {
    13.28f,
    /*    28.13f */
};

static const float APF_OUT_DELAYS[REVERB_COUNT_APF_OUT] = {5.10f, 12.61f, 10.0f, 7.73f};

typedef enum ForgeReverbChannelPositionFlags {
    Position_Left = 0x1,
    Position_Right = 0x2,
    Position_Center = 0x4,
    Position_Rear = 0x8,
} ForgeReverbChannelPositionFlags;

static ForgeReverbChannelPositionFlags get_channel_position_flags(int32_t total_channels, int32_t channel) {
    switch (total_channels) {
    case 1:
        return Position_Center;

    case 2:
        return (channel == 0) ? Position_Left : Position_Right;

    case 4:
        switch (channel) {
        case 0:
            return Position_Left;
        case 1:
            return Position_Right;
        case 2:
            return Position_Left | Position_Rear;
        case 3:
            return Position_Right | Position_Rear;
        }
        forge_assert(0 && "Unsupported channel count");
        break;
    case 5:
        switch (channel) {
        case 0:
            return Position_Left;
        case 1:
            return Position_Right;
        case 2:
            return Position_Center;
        case 3:
            return Position_Left | Position_Rear;
        case 4:
            return Position_Right | Position_Rear;
        }
        forge_assert(0 && "Unsupported channel count");
        break;
    default:
        forge_assert(0 && "Unsupported channel count");
        break;
    }

    /* shouldn't happen, but default to left speaker */
    return Position_Left;
}

static float get_stereo_spread_delay_ms(int32_t total_channels, int32_t channel) {
    ForgeReverbChannelPositionFlags flags = get_channel_position_flags(total_channels, channel);
    return (flags & Position_Right) ? 0.5216f : 0.0f;
}

typedef struct DspReverbChannel {
    DspDelay reverb_delay;
    DspCombShelving lpf_comb[REVERB_COUNT_COMB];
    DspAllPass apf_out[REVERB_COUNT_APF_OUT];
    DspBiQuad room_high_shelf;
    float early_gain;
    float gain;
} DspReverbChannel;

typedef struct DspReverb {
    DspDelay early_delay;
    DspAllPass apf_in[REVERB_COUNT_APF_IN];

    int32_t in_channels;
    int32_t out_channels;
    int32_t reverb_channels;
    DspReverbChannel channel[5];

    float early_gain;
    float reverb_gain;
    float room_gain;
    float wet_ratio;
    float dry_ratio;
} DspReverb;

typedef enum ForgeReverbParameterLayout {
    FORGE_REVERB_PARAMETER_LAYOUT_STANDARD,
    FORGE_REVERB_PARAMETER_LAYOUT_7POINT1
} ForgeReverbParameterLayout;

typedef struct ForgeReverbFieldAutomation {
    uint8_t active;
    float target;
    float step;
    uint32_t remainingFrames;
} ForgeReverbFieldAutomation;

typedef struct ForgeReverbAutomation {
    ForgeReverbFieldAutomation wet_dry_mix;
    ForgeReverbFieldAutomation reflections_gain;
    ForgeReverbFieldAutomation reverb_gain;
    ForgeReverbFieldAutomation room_filter_main;
    ForgeReverbFieldAutomation room_filter_hf;
} ForgeReverbAutomation;

typedef struct ForgeReverb {
    ForgeEffectBase base;

    uint16_t inChannels;
    uint16_t outChannels;
    uint32_t sampleRate;
    uint16_t inBlockAlign;
    uint16_t outBlockAlign;

    ForgeReverbParameterLayout parameter_layout;
    DspReverb reverb;
    ForgeReverbAutomation automation;
} ForgeReverb;

static inline void DspReverb_Create(DspReverb *reverb, int32_t sampleRate, int32_t in_channels, int32_t out_channels,
                                    ForgeMallocFunc malloc_func) {
    int32_t i, c;

    forge_assert(in_channels == 1 || in_channels == 2 || in_channels == 6);
    forge_assert(out_channels == 1 || out_channels == 2 || out_channels == 6);

    forge_zero(reverb, sizeof(DspReverb));
    DspDelay_Initialize(&reverb->early_delay, sampleRate, 10, malloc_func);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1) {
        DspAllPass_Initialize(&reverb->apf_in[i], sampleRate, APF_IN_DELAYS[i], 0.5f, malloc_func);
    }

    if (out_channels == 6) {
        reverb->reverb_channels = (in_channels == 6) ? 5 : 4;
    } else {
        reverb->reverb_channels = out_channels;
    }

    for (c = 0; c < reverb->reverb_channels; c += 1) {
        DspDelay_Initialize(&reverb->channel[c].reverb_delay, sampleRate, 10, malloc_func);

        for (i = 0; i < REVERB_COUNT_COMB; i += 1) {
            DspCombShelving_Initialize(&reverb->channel[c].lpf_comb[i], sampleRate,
                                       COMB_DELAYS[i] +
                                           get_stereo_spread_delay_ms(reverb->reverb_channels, c),
                                       500, 500, -6, 5000, -6, malloc_func);
        }

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1) {
            DspAllPass_Initialize(&reverb->channel[c].apf_out[i], sampleRate,
                                  APF_OUT_DELAYS[i] + get_stereo_spread_delay_ms(reverb->reverb_channels, c),
                                  0.5f, malloc_func);
        }

        DspBiQuad_Initialize(&reverb->channel[c].room_high_shelf, sampleRate, DSP_BIQUAD_HIGHSHELVING, 5000, 0, -10);
        reverb->channel[c].gain = 1.0f;
    }

    reverb->early_gain = 1.0f;
    reverb->reverb_gain = 1.0f;
    reverb->dry_ratio = 0.0f;
    reverb->wet_ratio = 1.0f;
    reverb->in_channels = in_channels;
    reverb->out_channels = out_channels;
}

static inline void DspReverb_Destroy(DspReverb *reverb, ForgeFreeFunc free_func) {
    int32_t i, c;

    DspDelay_Destroy(&reverb->early_delay, free_func);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1) {
        DspAllPass_Destroy(&reverb->apf_in[i], free_func);
    }

    for (c = 0; c < reverb->reverb_channels; c += 1) {
        DspDelay_Destroy(&reverb->channel[c].reverb_delay, free_func);

        for (i = 0; i < REVERB_COUNT_COMB; i += 1) {
            DspCombShelving_Destroy(&reverb->channel[c].lpf_comb[i], free_func);
        }

        DspBiQuad_Destroy(&reverb->channel[c].room_high_shelf);

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1) {
            DspAllPass_Destroy(&reverb->channel[c].apf_out[i], free_func);
        }
    }
}

static inline void DspReverb_SetParameters(DspReverb *reverb, ForgeReverbParameters *params) {
    float early_diffusion, late_diffusion;
    DspCombShelving *comb;
    int32_t i, c;

    /* Pre-delay */
    DspDelay_Change(&reverb->early_delay, (float)params->reflections_delay);

    /* Early reflections - diffusion */
    early_diffusion = 0.6f - ((params->early_diffusion / 15.0f) * 0.2f);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1) {
        DspAllPass_Change(&reverb->apf_in[i], APF_IN_DELAYS[i], early_diffusion);
    }

    /* Reverberation */
    for (c = 0; c < reverb->reverb_channels; c += 1) {
        float channel_delay = (get_channel_position_flags(reverb->reverb_channels, c) & Position_Rear)
                                  ? params->rear_delay
                                  : 0.0f;

        DspDelay_Change(&reverb->channel[c].reverb_delay, (float)params->reverb_delay + channel_delay);

        for (i = 0; i < REVERB_COUNT_COMB; i += 1) {
            comb = &reverb->channel[c].lpf_comb[i];

            /* Set decay time of comb filter */
            DspDelay_Change(&comb->comb_delay,
                            COMB_DELAYS[i] + get_stereo_spread_delay_ms(reverb->reverb_channels, c));
            comb->comb_feedback_gain = DspComb_FeedbackFromRT60(
                &comb->comb_delay, forge_max(params->decay_time, FORGE_REVERB_MIN_DECAY_TIME) * 1000.0f);

            /* High/Low shelving */
            DspBiQuad_Change(&comb->low_shelving, DSP_BIQUAD_LOWSHELVING, 50.0f + params->low_eq_cutoff * 50.0f, 0.0f,
                             params->low_eq_gain - 8.0f);
            DspBiQuad_Change(&comb->high_shelving, DSP_BIQUAD_HIGHSHELVING, 1000 + params->high_eq_cutoff * 500.0f,
                             0.0f, params->high_eq_gain - 8.0f);
        }
    }

    /* Gain */
    reverb->early_gain = DbGainToFactor(params->reflections_gain);
    reverb->reverb_gain = DbGainToFactor(params->reverb_gain);
    reverb->room_gain = DbGainToFactor(params->room_filter_main);

    /* Late diffusion */
    late_diffusion = 0.6f - ((params->late_diffusion / 15.0f) * 0.2f);

    for (c = 0; c < reverb->reverb_channels; c += 1) {
        ForgeReverbChannelPositionFlags position = get_channel_position_flags(reverb->reverb_channels, c);
        float gain;

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1) {
            DspAllPass_Change(&reverb->channel[c].apf_out[i],
                              APF_OUT_DELAYS[i] + get_stereo_spread_delay_ms(reverb->reverb_channels, c),
                              late_diffusion);
        }

        DspBiQuad_Change(&reverb->channel[c].room_high_shelf, DSP_BIQUAD_HIGHSHELVING, params->room_filter_freq, 0.0f,
                         params->room_filter_main + params->room_filter_hf);

        if (position & Position_Left) {
            gain = params->position_matrix_left;
        } else if (position & Position_Right) {
            gain = params->position_matrix_right;
        } else /*if (position & Position_Center) */
        {
            gain = (params->position_matrix_left + params->position_matrix_right) / 2.0f;
        }
        reverb->channel[c].gain = 1.5f - (gain / 27.0f) * 0.5f;

        if (position & Position_Rear) {
            /* Rear-channel Attenuation */
            reverb->channel[c].gain *= 0.75f;
        }

        if (position & Position_Left) {
            gain = params->position_left;
        } else if (position & Position_Right) {
            gain = params->position_right;
        } else /*if (position & Position_Center) */
        {
            gain = (params->position_left + params->position_right) / 2.0f;
        }
        reverb->channel[c].early_gain = 1.2f - (gain / 6.0f) * 0.2f;
        reverb->channel[c].early_gain = (reverb->channel[c].early_gain * reverb->early_gain);
    }

    /* Wet/Dry Mix (100 = fully wet, 0 = fully dry) */
    reverb->wet_ratio = params->wet_dry_mix / 100.0f;
    reverb->dry_ratio = 1.0f - reverb->wet_ratio;
}

static inline void DspReverb_SetParameters7Point1(DspReverb *reverb, ForgeReverbParameters7Point1 *params) {
    ForgeReverbParameters standard_params;
    standard_params.wet_dry_mix = params->wet_dry_mix;
    standard_params.reflections_delay = params->reflections_delay;
    standard_params.reverb_delay = params->reverb_delay;
    standard_params.rear_delay = params->rear_delay;
    standard_params.position_left = params->position_left;
    standard_params.position_right = params->position_right;
    standard_params.position_matrix_left = params->position_matrix_left;
    standard_params.position_matrix_right = params->position_matrix_right;
    standard_params.early_diffusion = params->early_diffusion;
    standard_params.late_diffusion = params->late_diffusion;
    standard_params.low_eq_gain = params->low_eq_gain;
    standard_params.low_eq_cutoff = params->low_eq_cutoff;
    standard_params.high_eq_gain = params->high_eq_gain;
    standard_params.high_eq_cutoff = params->high_eq_cutoff;
    standard_params.room_filter_freq = params->room_filter_freq;
    standard_params.room_filter_main = params->room_filter_main;
    standard_params.room_filter_hf = params->room_filter_hf;
    standard_params.reflections_gain = params->reflections_gain;
    standard_params.reverb_gain = params->reverb_gain;
    standard_params.decay_time = params->decay_time;
    standard_params.density = params->density;
    standard_params.room_size = params->room_size;
    DspReverb_SetParameters(reverb, &standard_params);
}

static inline void DspReverb_SetSmoothParameters(DspReverb *reverb, ForgeReverbParameters *params) {
    int32_t c;

    reverb->early_gain = DbGainToFactor(params->reflections_gain);
    reverb->reverb_gain = DbGainToFactor(params->reverb_gain);
    reverb->room_gain = DbGainToFactor(params->room_filter_main);

    for (c = 0; c < reverb->reverb_channels; c += 1) {
        ForgeReverbChannelPositionFlags position = get_channel_position_flags(reverb->reverb_channels, c);
        float gain;

        DspBiQuad_Change(&reverb->channel[c].room_high_shelf, DSP_BIQUAD_HIGHSHELVING, params->room_filter_freq, 0.0f,
                         params->room_filter_main + params->room_filter_hf);

        if (position & Position_Left) {
            gain = params->position_left;
        } else if (position & Position_Right) {
            gain = params->position_right;
        } else {
            gain = (params->position_left + params->position_right) / 2.0f;
        }
        reverb->channel[c].early_gain = 1.2f - (gain / 6.0f) * 0.2f;
        reverb->channel[c].early_gain = reverb->channel[c].early_gain * reverb->early_gain;
    }

    reverb->wet_ratio = params->wet_dry_mix / 100.0f;
    reverb->dry_ratio = 1.0f - reverb->wet_ratio;
}

static void fa_reverb_get_standard_parameters(ForgeReverb *effect, ForgeReverbParameters *params) {
    if (effect->parameter_layout == FORGE_REVERB_PARAMETER_LAYOUT_7POINT1) {
        ForgeReverbParameters7Point1 *params7 = (ForgeReverbParameters7Point1 *)effect->base.parameters;

        params->wet_dry_mix = params7->wet_dry_mix;
        params->reflections_delay = params7->reflections_delay;
        params->reverb_delay = params7->reverb_delay;
        params->rear_delay = params7->rear_delay;
        params->position_left = params7->position_left;
        params->position_right = params7->position_right;
        params->position_matrix_left = params7->position_matrix_left;
        params->position_matrix_right = params7->position_matrix_right;
        params->early_diffusion = params7->early_diffusion;
        params->late_diffusion = params7->late_diffusion;
        params->low_eq_gain = params7->low_eq_gain;
        params->low_eq_cutoff = params7->low_eq_cutoff;
        params->high_eq_gain = params7->high_eq_gain;
        params->high_eq_cutoff = params7->high_eq_cutoff;
        params->room_filter_freq = params7->room_filter_freq;
        params->room_filter_main = params7->room_filter_main;
        params->room_filter_hf = params7->room_filter_hf;
        params->reflections_gain = params7->reflections_gain;
        params->reverb_gain = params7->reverb_gain;
        params->decay_time = params7->decay_time;
        params->density = params7->density;
        params->room_size = params7->room_size;
    } else {
        forge_memcpy(params, effect->base.parameters, sizeof(*params));
    }
}

static float *fa_reverb_parameter_ptr(ForgeReverb *effect, uint32_t field) {
    if (effect->parameter_layout == FORGE_REVERB_PARAMETER_LAYOUT_7POINT1) {
        ForgeReverbParameters7Point1 *params = (ForgeReverbParameters7Point1 *)effect->base.parameters;

        switch (field) {
        case FORGE_REVERB_TARGET_WET_DRY_MIX:
            return &params->wet_dry_mix;
        case FORGE_REVERB_TARGET_REFLECTIONS_GAIN:
            return &params->reflections_gain;
        case FORGE_REVERB_TARGET_REVERB_GAIN:
            return &params->reverb_gain;
        case FORGE_REVERB_TARGET_ROOM_FILTER_MAIN:
            return &params->room_filter_main;
        case FORGE_REVERB_TARGET_ROOM_FILTER_HF:
            return &params->room_filter_hf;
        }
    } else {
        ForgeReverbParameters *params = (ForgeReverbParameters *)effect->base.parameters;

        switch (field) {
        case FORGE_REVERB_TARGET_WET_DRY_MIX:
            return &params->wet_dry_mix;
        case FORGE_REVERB_TARGET_REFLECTIONS_GAIN:
            return &params->reflections_gain;
        case FORGE_REVERB_TARGET_REVERB_GAIN:
            return &params->reverb_gain;
        case FORGE_REVERB_TARGET_ROOM_FILTER_MAIN:
            return &params->room_filter_main;
        case FORGE_REVERB_TARGET_ROOM_FILTER_HF:
            return &params->room_filter_hf;
        }
    }

    return NULL;
}

static ForgeReverbFieldAutomation *fa_reverb_automation_ptr(ForgeReverb *effect, uint32_t field) {
    switch (field) {
    case FORGE_REVERB_TARGET_WET_DRY_MIX:
        return &effect->automation.wet_dry_mix;
    case FORGE_REVERB_TARGET_REFLECTIONS_GAIN:
        return &effect->automation.reflections_gain;
    case FORGE_REVERB_TARGET_REVERB_GAIN:
        return &effect->automation.reverb_gain;
    case FORGE_REVERB_TARGET_ROOM_FILTER_MAIN:
        return &effect->automation.room_filter_main;
    case FORGE_REVERB_TARGET_ROOM_FILTER_HF:
        return &effect->automation.room_filter_hf;
    }

    return NULL;
}

static float fa_reverb_clamp_target_field(uint32_t field, float value) {
    switch (field) {
    case FORGE_REVERB_TARGET_WET_DRY_MIX:
        return forge_clamp(value, FORGE_REVERB_MIN_WET_DRY_MIX, FORGE_REVERB_MAX_WET_DRY_MIX);
    case FORGE_REVERB_TARGET_REFLECTIONS_GAIN:
        return forge_clamp(value, FORGE_REVERB_MIN_REFLECTIONS_GAIN, FORGE_REVERB_MAX_REFLECTIONS_GAIN);
    case FORGE_REVERB_TARGET_REVERB_GAIN:
        return forge_clamp(value, FORGE_REVERB_MIN_REVERB_GAIN, FORGE_REVERB_MAX_REVERB_GAIN);
    case FORGE_REVERB_TARGET_ROOM_FILTER_MAIN:
        return forge_clamp(value, FORGE_REVERB_MIN_ROOM_FILTER_MAIN, FORGE_REVERB_MAX_ROOM_FILTER_MAIN);
    case FORGE_REVERB_TARGET_ROOM_FILTER_HF:
        return forge_clamp(value, FORGE_REVERB_MIN_ROOM_FILTER_HF, FORGE_REVERB_MAX_ROOM_FILTER_HF);
    }

    return value;
}

static float fa_reverb_target_value(const ForgeReverbTarget *target, uint32_t field) {
    switch (field) {
    case FORGE_REVERB_TARGET_WET_DRY_MIX:
        return target->wet_dry_mix;
    case FORGE_REVERB_TARGET_REFLECTIONS_GAIN:
        return target->reflections_gain;
    case FORGE_REVERB_TARGET_REVERB_GAIN:
        return target->reverb_gain;
    case FORGE_REVERB_TARGET_ROOM_FILTER_MAIN:
        return target->room_filter_main;
    case FORGE_REVERB_TARGET_ROOM_FILTER_HF:
        return target->room_filter_hf;
    }

    return 0.0f;
}

static void fa_reverb_clear_automation(ForgeReverb *effect) {
    forge_zero(&effect->automation, sizeof(effect->automation));
}

static uint8_t fa_reverb_automation_active(ForgeReverb *effect) {
    return effect->automation.wet_dry_mix.active || effect->automation.reflections_gain.active ||
           effect->automation.reverb_gain.active || effect->automation.room_filter_main.active ||
           effect->automation.room_filter_hf.active;
}

static void fa_reverb_apply_smooth_parameters(ForgeReverb *effect) {
    ForgeReverbParameters params;

    fa_reverb_get_standard_parameters(effect, &params);
    DspReverb_SetSmoothParameters(&effect->reverb, &params);
}

static uint8_t fa_reverb_advance_field_one_frame(ForgeReverbFieldAutomation *automation, float *value) {
    if (!automation->active) {
        return 0;
    }

    automation->remainingFrames -= 1;
    if (automation->remainingFrames == 0) {
        *value = automation->target;
        automation->active = 0;
    } else {
        *value += automation->step;
    }
    return 1;
}

static void fa_reverb_advance_automation_one_frame(ForgeReverb *effect) {
    uint8_t advanced = 0;

    advanced |= fa_reverb_advance_field_one_frame(&effect->automation.wet_dry_mix,
                                                  fa_reverb_parameter_ptr(effect, FORGE_REVERB_TARGET_WET_DRY_MIX));
    advanced |= fa_reverb_advance_field_one_frame(
        &effect->automation.reflections_gain, fa_reverb_parameter_ptr(effect, FORGE_REVERB_TARGET_REFLECTIONS_GAIN));
    advanced |= fa_reverb_advance_field_one_frame(&effect->automation.reverb_gain,
                                                  fa_reverb_parameter_ptr(effect, FORGE_REVERB_TARGET_REVERB_GAIN));
    advanced |= fa_reverb_advance_field_one_frame(
        &effect->automation.room_filter_main, fa_reverb_parameter_ptr(effect, FORGE_REVERB_TARGET_ROOM_FILTER_MAIN));
    advanced |= fa_reverb_advance_field_one_frame(&effect->automation.room_filter_hf,
                                                  fa_reverb_parameter_ptr(effect, FORGE_REVERB_TARGET_ROOM_FILTER_HF));

    if (advanced) {
        fa_reverb_apply_smooth_parameters(effect);
    }
}

static void fa_reverb_advance_automation(ForgeReverb *effect, uint32_t frame_count) {
    while (frame_count > 0 && fa_reverb_automation_active(effect)) {
        fa_reverb_advance_automation_one_frame(effect);
        frame_count -= 1;
    }
}

static void fa_reverb_set_field_automation(ForgeReverb *effect, uint32_t field, float target,
                                           uint32_t duration_frames) {
    ForgeReverbFieldAutomation *automation = fa_reverb_automation_ptr(effect, field);
    float *value = fa_reverb_parameter_ptr(effect, field);

    if (duration_frames == 0) {
        *value = target;
        automation->active = 0;
        automation->remainingFrames = 0;
        return;
    }

    automation->target = target;
    automation->step = (target - *value) / (float)duration_frames;
    automation->remainingFrames = duration_frames;
    automation->active = 1;
}

static ForgeResult fa_reverb_set_target(ForgeReverb *effect, const ForgeReverbTarget *target,
                                        uint32_t duration_frames) {
    static const uint32_t fields[] = {FORGE_REVERB_TARGET_WET_DRY_MIX, FORGE_REVERB_TARGET_REFLECTIONS_GAIN,
                                      FORGE_REVERB_TARGET_REVERB_GAIN, FORGE_REVERB_TARGET_ROOM_FILTER_MAIN,
                                      FORGE_REVERB_TARGET_ROOM_FILTER_HF};
    uint32_t unknown;

    if (target == NULL) {
        return ForgeResultInvalidCall;
    }
    unknown = target->field_mask & ~FORGE_REVERB_TARGET_ALL;
    if (target->field_mask == 0 || unknown != 0) {
        return ForgeResultInvalidArgument;
    }

    for (uint32_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i += 1) {
        uint32_t field = fields[i];

        if (target->field_mask & field) {
            float value = fa_reverb_clamp_target_field(field, fa_reverb_target_value(target, field));
            fa_reverb_set_field_automation(effect, field, value, duration_frames);
        }
    }

    if (duration_frames == 0) {
        fa_reverb_apply_smooth_parameters(effect);
    }
    return ForgeResultSuccess;
}

static void fa_reverb_on_set_parameters(ForgeEffectBase *effect, const void *parameters, uint32_t parametersSize) {
    (void)parameters;
    (void)parametersSize;
    fa_reverb_clear_automation((ForgeReverb *)effect);
}

static inline float DspReverb_INTERNAL_ProcessEarly(DspReverb *reverb, float sample_in) {
    float early;
    int32_t i;

    /* Pre-delay */
    early = DspDelay_Process(&reverb->early_delay, sample_in);

    /* Early reflections */
    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1) {
        early = DspAllPass_Process(&reverb->apf_in[i], early);
    }

    return early;
}

static inline float DspReverb_INTERNAL_ProcessChannel(DspReverb *reverb, DspReverbChannel *channel, float sample_in) {
    float revdelay, early_late, sample_out;
    int32_t i;

    revdelay = DspDelay_Process(&channel->reverb_delay, sample_in);

    sample_out = 0.0f;
    for (i = 0; i < REVERB_COUNT_COMB; i += 1) {
        sample_out += DspCombShelving_Process(&channel->lpf_comb[i], revdelay);
    }
    sample_out /= (float)REVERB_COUNT_COMB;

    /* Output diffusion */
    for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1) {
        sample_out = DspAllPass_Process(&channel->apf_out[i], sample_out);
    }

    /* Combine early reflections and reverberation */
    early_late = ((sample_in * channel->early_gain) + (sample_out * reverb->reverb_gain));

    /* room filter */
    sample_out = DspBiQuad_Process(&channel->room_high_shelf, early_late * reverb->room_gain);

    /* position_matrix_left/Right */
    return sample_out * channel->gain;
}

/* reverb process Functions */

static inline float DspReverb_INTERNAL_Process_1_to_1(DspReverb *reverb, float *restrict samples_in,
                                                      float *restrict samples_out, size_t sample_count) {
    const float *in_end = samples_in + sample_count;
    float in, early, late, out;
    float squared_sum = 0.0f;

    while (samples_in < in_end) {
        /* Input */
        in = *samples_in++;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation */
        late = DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[0], early);

        /* Wet/Dry Mix */
        out = (late * reverb->wet_ratio) + (in * reverb->dry_ratio);
        squared_sum += out * out;

        /* Output */
        *samples_out++ = out;
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_1_to_5p1(DspReverb *reverb, float *restrict samples_in,
                                                        float *restrict samples_out, size_t sample_count) {
    const float *in_end = samples_in + sample_count;
    float in, in_ratio, early, late[4];
    float squared_sum = 0.0f;
    int32_t c;

    while (samples_in < in_end) {
        /* Input */
        in = *samples_in++;
        in_ratio = in * reverb->dry_ratio;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        for (c = 0; c < 4; c += 1) {
            late[c] =
                (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[c], early) * reverb->wet_ratio) + in_ratio;
            squared_sum += late[c] * late[c];
        }

        /* Output */
        *samples_out++ = late[0]; /* Front Left */
        *samples_out++ = late[1]; /* Front Right */
        *samples_out++ = 0.0f;    /* Center */
        *samples_out++ = 0.0f;    /* LFE */
        *samples_out++ = late[2]; /* Rear Left */
        *samples_out++ = late[3]; /* Rear Right */
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_2_to_2(DspReverb *reverb, float *restrict samples_in,
                                                      float *restrict samples_out, size_t sample_count) {
    const float *in_end = samples_in + sample_count;
    float in, early, late[2];
    float squared_sum = 0;

    while (samples_in < in_end) {
        /* Input - Combine 2 channels into 1 */
        in = (samples_in[0] + samples_in[1]) / 2.0f;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        late[0] = (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[0], early) * reverb->wet_ratio) +
                  samples_in[0] * reverb->dry_ratio;
        late[1] = (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[1], early) * reverb->wet_ratio) +
                  samples_in[1] * reverb->dry_ratio;
        squared_sum += (late[0] * late[0]) + (late[1] * late[1]);

        /* Output */
        *samples_out++ = late[0];
        *samples_out++ = late[1];

        samples_in += 2;
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_2_to_5p1(DspReverb *reverb, float *restrict samples_in,
                                                        float *restrict samples_out, size_t sample_count) {
    const float *in_end = samples_in + sample_count;
    float in, in_ratio, early, late[4];
    float squared_sum = 0;
    int32_t c;

    while (samples_in < in_end) {
        /* Input - Combine 2 channels into 1 */
        in = (samples_in[0] + samples_in[1]) / 2.0f;
        in_ratio = in * reverb->dry_ratio;
        samples_in += 2;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        for (c = 0; c < 4; c += 1) {
            late[c] =
                (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[c], early) * reverb->wet_ratio) + in_ratio;
            squared_sum += late[c] * late[c];
        }

        /* Output */
        *samples_out++ = late[0]; /* Front Left */
        *samples_out++ = late[1]; /* Front Right */
        *samples_out++ = 0.0f;    /* Center */
        *samples_out++ = 0.0f;    /* LFE */
        *samples_out++ = late[2]; /* Rear Left */
        *samples_out++ = late[3]; /* Rear Right */
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_5p1_to_5p1(DspReverb *reverb, float *restrict samples_in,
                                                          float *restrict samples_out, size_t sample_count) {
    const float *in_end = samples_in + sample_count;
    float in, in_ratio, early, late[5];
    float squared_sum = 0;
    int32_t c;

    while (samples_in < in_end) {
        /* Input - Combine non-LFE channels into 1 */
        in = (samples_in[0] + samples_in[1] + samples_in[2] + samples_in[4] + samples_in[5]) / 5.0f;
        in_ratio = in * reverb->dry_ratio;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        for (c = 0; c < 5; c += 1) {
            late[c] =
                (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[c], early) * reverb->wet_ratio) + in_ratio;
            squared_sum += late[c] * late[c];
        }

        /* Output */
        *samples_out++ = late[0];       /* Front Left */
        *samples_out++ = late[1];       /* Front Right */
        *samples_out++ = late[2];       /* Center */
        *samples_out++ = samples_in[3]; /* LFE, pass through */
        *samples_out++ = late[3];       /* Rear Left */
        *samples_out++ = late[4];       /* Rear Right */

        samples_in += 6;
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_ProcessOneFrame(ForgeReverb *effect, float *restrict samples_in,
                                                       float *restrict samples_out) {
    DspReverb *reverb = &effect->reverb;

    switch (reverb->out_channels) {
    case 1: {
        float in = samples_in[0];
        float early = DspReverb_INTERNAL_ProcessEarly(reverb, in);
        float late = DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[0], early);
        float out = (late * reverb->wet_ratio) + (in * reverb->dry_ratio);
        samples_out[0] = out;
        return out * out;
    }
    case 2: {
        float in = (samples_in[0] + samples_in[1]) / 2.0f;
        float early = DspReverb_INTERNAL_ProcessEarly(reverb, in);
        float late0 = (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[0], early) * reverb->wet_ratio) +
                      samples_in[0] * reverb->dry_ratio;
        float late1 = (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[1], early) * reverb->wet_ratio) +
                      samples_in[1] * reverb->dry_ratio;
        samples_out[0] = late0;
        samples_out[1] = late1;
        return (late0 * late0) + (late1 * late1);
    }
    default:
        if (reverb->in_channels == 1) {
            float in = samples_in[0];
            float in_ratio = in * reverb->dry_ratio;
            float early = DspReverb_INTERNAL_ProcessEarly(reverb, in);
            float total = 0.0f;

            for (int32_t c = 0; c < 4; c += 1) {
                float late =
                    (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[c], early) * reverb->wet_ratio) +
                    in_ratio;
                samples_out[c < 2 ? c : c + 2] = late;
                total += late * late;
            }
            samples_out[2] = 0.0f;
            samples_out[3] = 0.0f;
            return total;
        }
        if (reverb->in_channels == 2) {
            float in = (samples_in[0] + samples_in[1]) / 2.0f;
            float in_ratio = in * reverb->dry_ratio;
            float early = DspReverb_INTERNAL_ProcessEarly(reverb, in);
            float total = 0.0f;

            for (int32_t c = 0; c < 4; c += 1) {
                float late =
                    (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[c], early) * reverb->wet_ratio) +
                    in_ratio;
                samples_out[c < 2 ? c : c + 2] = late;
                total += late * late;
            }
            samples_out[2] = 0.0f;
            samples_out[3] = 0.0f;
            return total;
        }
        {
            float in = (samples_in[0] + samples_in[1] + samples_in[2] + samples_in[4] + samples_in[5]) / 5.0f;
            float in_ratio = in * reverb->dry_ratio;
            float early = DspReverb_INTERNAL_ProcessEarly(reverb, in);
            float total = 0.0f;

            for (int32_t c = 0; c < 5; c += 1) {
                float late =
                    (DspReverb_INTERNAL_ProcessChannel(reverb, &reverb->channel[c], early) * reverb->wet_ratio) +
                    in_ratio;
                samples_out[c < 3 ? c : c + 1] = late;
                total += late * late;
            }
            samples_out[3] = samples_in[3];
            return total;
        }
    }
}

static inline float DspReverb_INTERNAL_ProcessAutomated(ForgeReverb *effect, float *restrict samples_in,
                                                       float *restrict samples_out, uint32_t frame_count) {
    float squared_sum = 0.0f;

    for (uint32_t frame = 0; frame < frame_count; frame += 1) {
        squared_sum += DspReverb_INTERNAL_ProcessOneFrame(effect, samples_in, samples_out);
        fa_reverb_advance_automation_one_frame(effect);
        samples_in += effect->reverb.in_channels;
        samples_out += effect->reverb.out_channels;
    }

    return squared_sum;
}

#undef OUTPUT_SAMPLE

/* reverb ForgeEffect Implementation */

static const ForgeEffectInfo ReverbInfo = {
    .flags = (FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH | FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH |
              FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH | FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED),
    .min_input_buffer_count = 1,
    .max_input_buffer_count = 1,
    .min_output_buffer_count = 1,
    .max_output_buffer_count = 1};

static inline int8_t is_float_format(const ForgeAudioFormat *format) {
    if (format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT) {
        /* Plain ol' WaveFormatEx */
        return 1;
    }

    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        if (fa_format_id_equals(((ForgeAudioFormatExtensible *)format)->format_id, fa_format_id_ieee_float)) {
            return 1;
        }
    }

    return 0;
}

static ForgeResult fa_reverb_is_input_format_supported(ForgeEffectBase *effect, const ForgeAudioFormat *output_format,
                                                       const ForgeAudioFormat *requested_input_format,
                                                       ForgeAudioFormat **supported_input_format) {
    ForgeResult result = ForgeResultSuccess;

#define SET_SUPPORTED_FIELD(field, value)                                                                              \
    result = ForgeResultFormatSuggested;                                                                               \
    if (supported_input_format && *supported_input_format) {                                                           \
        (*supported_input_format)->field = (value);                                                                    \
    }

    /* Sample Rate */
    if (output_format->sample_rate != requested_input_format->sample_rate) {
        SET_SUPPORTED_FIELD(sample_rate, output_format->sample_rate);
    }

    /* Data type */
    if (!is_float_format(requested_input_format)) {
        SET_SUPPORTED_FIELD(format_tag, FORGE_AUDIO_FORMAT_IEEE_FLOAT);
    }

    /* Input/Output Channel Count */
    if (output_format->channels == 1 || output_format->channels == 2) {
        if (requested_input_format->channels != output_format->channels) {
            SET_SUPPORTED_FIELD(channels, output_format->channels);
        }
    } else if (output_format->channels == 6) {
        if (requested_input_format->channels != 1 && requested_input_format->channels != 2 &&
            requested_input_format->channels != 6) {
            SET_SUPPORTED_FIELD(channels, 1);
        }
    } else {
        SET_SUPPORTED_FIELD(channels, 1);
    }

#undef SET_SUPPORTED_FIELD

    return result;
}

static ForgeResult fa_reverb_is_output_format_supported(ForgeEffectBase *effect, const ForgeAudioFormat *input_format,
                                                        const ForgeAudioFormat *requested_output_format,
                                                        ForgeAudioFormat **supported_output_format) {
    ForgeResult result = ForgeResultSuccess;

#define SET_SUPPORTED_FIELD(field, value)                                                                              \
    result = ForgeResultFormatSuggested;                                                                               \
    if (supported_output_format && *supported_output_format) {                                                         \
        (*supported_output_format)->field = (value);                                                                   \
    }

    /* Sample Rate */
    if (input_format->sample_rate != requested_output_format->sample_rate) {
        SET_SUPPORTED_FIELD(sample_rate, input_format->sample_rate);
    }

    /* Data type */
    if (!is_float_format(requested_output_format)) {
        SET_SUPPORTED_FIELD(format_tag, FORGE_AUDIO_FORMAT_IEEE_FLOAT);
    }

    /* Input/Output Channel Count */
    if (input_format->channels == 1 || input_format->channels == 2) {
        if (requested_output_format->channels != input_format->channels && requested_output_format->channels != 6) {
            SET_SUPPORTED_FIELD(channels, input_format->channels);
        }
    } else if (input_format->channels == 6) {
        if (requested_output_format->channels != 6) {
            SET_SUPPORTED_FIELD(channels, input_format->channels);
        }
    } else {
        SET_SUPPORTED_FIELD(channels, 1);
    }

#undef SET_SUPPORTED_FIELD

    return result;
}

static ForgeResult fa_reverb_initialize(ForgeReverb *effect, const void *data, uint32_t data_byte_size) {
    forge_assert(data_byte_size == effect->base.parameter_block_byte_size);
    if (data == NULL || data_byte_size != effect->base.parameter_block_byte_size) {
        return ForgeResultInvalidArgument;
    }

    forge_memcpy(effect->base.parameters, data, data_byte_size);
    effect->base.parameters_changed = 1;
    return 0;
}

static ForgeResult fa_reverb_lock_for_process(ForgeReverb *effect, uint32_t input_locked_parameter_count,
                                              const ForgeEffectLockBuffer *input_locked_parameters,
                                              uint32_t output_locked_parameter_count,
                                              const ForgeEffectLockBuffer *output_locked_parameters) {
    ForgeResult result;

    /* reverb specific validation */
    if (!is_float_format(input_locked_parameters->format)) {
        return ForgeResultEffectFormatUnsupported;
    }

    if (input_locked_parameters->format->sample_rate < FORGE_REVERB_MIN_SAMPLE_RATE ||
        input_locked_parameters->format->sample_rate > FORGE_REVERB_MAX_SAMPLE_RATE) {
        return ForgeResultEffectFormatUnsupported;
    }

    if (!((input_locked_parameters->format->channels == 1 &&
           (output_locked_parameters->format->channels == 1 || output_locked_parameters->format->channels == 6)) ||
          (input_locked_parameters->format->channels == 2 &&
           (output_locked_parameters->format->channels == 2 || output_locked_parameters->format->channels == 6)) ||
          (input_locked_parameters->format->channels == 6 && output_locked_parameters->format->channels == 6))) {
        return ForgeResultEffectFormatUnsupported;
    }

    result = fa_effect_base_lock_for_process(&effect->base, input_locked_parameter_count, input_locked_parameters,
                                             output_locked_parameter_count, output_locked_parameters);
    if (result != 0) {
        return result;
    }

    /* Save the things we care about */
    effect->inChannels = input_locked_parameters->format->channels;
    effect->outChannels = output_locked_parameters->format->channels;
    effect->sampleRate = output_locked_parameters->format->sample_rate;
    effect->inBlockAlign = input_locked_parameters->format->block_align;
    effect->outBlockAlign = output_locked_parameters->format->block_align;

    /* Create the network */
    DspReverb_Create(&effect->reverb, effect->sampleRate, effect->inChannels, effect->outChannels,
                     effect->base.malloc_func);

    /* initialize the effect to a default setting */
    if (effect->parameter_layout == FORGE_REVERB_PARAMETER_LAYOUT_7POINT1) {
        DspReverb_SetParameters7Point1(&effect->reverb, (ForgeReverbParameters7Point1 *)effect->base.parameters);
    } else {
        DspReverb_SetParameters(&effect->reverb, (ForgeReverbParameters *)effect->base.parameters);
    }

    return 0;
}

static void fa_reverb_unlock_for_process(ForgeReverb *effect) {
    DspReverb_Destroy(&effect->reverb, effect->base.free_func);
    forge_zero(&effect->reverb, sizeof(DspReverb));
    fa_effect_base_unlock_for_process(&effect->base);
}

static inline void copy_buffer(ForgeReverb *effect, float *restrict buffer_in, float *restrict buffer_out,
                                         size_t frames_in) {
    /* In-place processing? */
    if (buffer_in == buffer_out) {
        return;
    }

    /* equal channel count */
    if (effect->inBlockAlign == effect->outBlockAlign) {
        forge_memcpy(buffer_out, buffer_in, effect->inBlockAlign * frames_in);
        return;
    }

    /* 1 -> 5.1 */
    if (effect->inChannels == 1 && effect->outChannels == 6) {
        const float *in_end = buffer_in + frames_in;
        while (buffer_in < in_end) {
            *buffer_out++ = *buffer_in;
            *buffer_out++ = *buffer_in++;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
        }
        return;
    }

    /* 2 -> 5.1 */
    if (effect->inChannels == 2 && effect->outChannels == 6) {
        const float *in_end = buffer_in + (frames_in * 2);
        while (buffer_in < in_end) {
            *buffer_out++ = *buffer_in++;
            *buffer_out++ = *buffer_in++;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
        }
        return;
    }

    forge_assert(0 && "Unsupported channel combination");
    forge_zero(buffer_out, effect->outBlockAlign * frames_in);
}

static void fa_reverb_process(ForgeReverb *effect, uint32_t input_buffer_count,
                              const ForgeEffectProcessBuffer *input_buffers, uint32_t output_buffer_count,
                              ForgeEffectProcessBuffer *output_buffers, int32_t is_enabled) {
    ForgeReverbParameters *params;
    uint8_t update_params = fa_effect_base_parameters_changed(&effect->base);
    float total;

    params = (ForgeReverbParameters *)fa_effect_base_begin_process(&effect->base);

    /* Update parameters before doing anything else  */
    if (update_params) {
        if (effect->parameter_layout == FORGE_REVERB_PARAMETER_LAYOUT_7POINT1) {
            DspReverb_SetParameters7Point1(&effect->reverb, (ForgeReverbParameters7Point1 *)params);
        } else {
            DspReverb_SetParameters(&effect->reverb, params);
        }
    }

    /* Handle disabled filter */
    if (is_enabled == 0) {
        output_buffers->buffer_flags = input_buffers->buffer_flags;

        if (output_buffers->buffer_flags != FORGE_EFFECT_BUFFER_SILENT) {
            copy_buffer(effect, (float *)input_buffers->buffer, (float *)output_buffers->buffer,
                                  input_buffers->valid_frame_count);
        }

        fa_effect_base_end_process(&effect->base);
        return;
    }

    /* Use a silent buffer when no input buffer is available to play the effect tail. */
    if (input_buffers->buffer_flags == FORGE_EFFECT_BUFFER_SILENT) {
        /* Silent buffers may still be processed for effect tails; ensure input samples are zero. */
        forge_zero(input_buffers->buffer, input_buffers->valid_frame_count * effect->inBlockAlign);
    }

    /* Run reverb effect */
    if (fa_reverb_automation_active(effect)) {
        total = DspReverb_INTERNAL_ProcessAutomated(effect, (float *)input_buffers->buffer,
                                                    (float *)output_buffers->buffer,
                                                    input_buffers->valid_frame_count);
        goto processed;
    }

#define PROCESS(pin, pout)                                                                                             \
    DspReverb_INTERNAL_Process_##pin##_to_##pout(&effect->reverb, (float *)input_buffers->buffer,                      \
                                                 (float *)output_buffers->buffer,                                      \
                                                 input_buffers->valid_frame_count * effect->inChannels)
    switch (effect->reverb.out_channels) {
    case 1:
        total = PROCESS(1, 1);
        break;
    case 2:
        total = PROCESS(2, 2);
        break;
    default: /* 5.1 */
        if (effect->reverb.in_channels == 1) {
            total = PROCESS(1, 5p1);
        } else if (effect->reverb.in_channels == 2) {
            total = PROCESS(2, 5p1);
        } else /* 5.1 */
        {
            total = PROCESS(5p1, 5p1);
        }
        break;
    }
#undef PROCESS

processed:
    /* Set buffer_flags to silent so PLAY_TAILS knows when to stop */
    output_buffers->buffer_flags = (total < 0.0000001f) ? FORGE_EFFECT_BUFFER_SILENT : FORGE_EFFECT_BUFFER_VALID;

    fa_effect_base_end_process(&effect->base);
}

static void fa_reverb_reset(ForgeReverb *effect) {
    int32_t i, c;
    fa_effect_base_reset(&effect->base);

    /* reset the cached state of the reverb filter */
    DspDelay_Reset(&effect->reverb.early_delay);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1) {
        DspAllPass_Reset(&effect->reverb.apf_in[i]);
    }

    for (c = 0; c < effect->reverb.reverb_channels; c += 1) {
        DspDelay_Reset(&effect->reverb.channel[c].reverb_delay);

        for (i = 0; i < REVERB_COUNT_COMB; i += 1) {
            DspCombShelving_Reset(&effect->reverb.channel[c].lpf_comb[i]);
        }

        DspBiQuad_Reset(&effect->reverb.channel[c].room_high_shelf);

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1) {
            DspAllPass_Reset(&effect->reverb.channel[c].apf_out[i]);
        }
    }
}

static void fa_reverb_free(void *effect) {
    ForgeReverb *reverb = (ForgeReverb *)effect;
    DspReverb_Destroy(&reverb->reverb, reverb->base.free_func);
    reverb->base.free_func(reverb->base.parameters);
    reverb->base.free_func(effect);
}

/* Public standard reverb API */

ForgeResult forge_create_reverb(ForgeEffect **effect, uint32_t flags) {
    return forge_create_reverb_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

ForgeResult forge_create_reverb_with_allocator(ForgeEffect **effect, uint32_t flags, ForgeMallocFunc custom_malloc,
                                               ForgeFreeFunc custom_free, ForgeReallocFunc custom_realloc) {
    const ForgeReverbParameters default_parameters = {
        FORGE_REVERB_DEFAULT_WET_DRY_MIX,      FORGE_REVERB_DEFAULT_REFLECTIONS_DELAY,
        FORGE_REVERB_DEFAULT_REVERB_DELAY,     FORGE_REVERB_DEFAULT_REAR_DELAY,
        FORGE_REVERB_DEFAULT_POSITION,         FORGE_REVERB_DEFAULT_POSITION,
        FORGE_REVERB_DEFAULT_POSITION_MATRIX,  FORGE_REVERB_DEFAULT_POSITION_MATRIX,
        FORGE_REVERB_DEFAULT_EARLY_DIFFUSION,  FORGE_REVERB_DEFAULT_LATE_DIFFUSION,
        FORGE_REVERB_DEFAULT_LOW_EQ_GAIN,      FORGE_REVERB_DEFAULT_LOW_EQ_CUTOFF,
        FORGE_REVERB_DEFAULT_HIGH_EQ_GAIN,     FORGE_REVERB_DEFAULT_HIGH_EQ_CUTOFF,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_FREQ, FORGE_REVERB_DEFAULT_ROOM_FILTER_MAIN,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_HF,   FORGE_REVERB_DEFAULT_REFLECTIONS_GAIN,
        FORGE_REVERB_DEFAULT_REVERB_GAIN,      FORGE_REVERB_DEFAULT_DECAY_TIME,
        FORGE_REVERB_DEFAULT_DENSITY,          FORGE_REVERB_DEFAULT_ROOM_SIZE};

    /* Allocate... */
    ForgeReverb *result = (ForgeReverb *)custom_malloc(sizeof(ForgeReverb));
    uint8_t *params = (uint8_t *)custom_malloc(sizeof(ForgeReverbParameters));
    result->parameter_layout = FORGE_REVERB_PARAMETER_LAYOUT_STANDARD;

    fa_effect_base_init_with_allocator(&result->base, &ReverbInfo, params, sizeof(ForgeReverbParameters), custom_malloc,
                                       custom_free, custom_realloc);

    result->inChannels = 0;
    result->outChannels = 0;
    result->sampleRate = 0;
    forge_zero(&result->reverb, sizeof(DspReverb));
    forge_zero(&result->automation, sizeof(result->automation));

    /* Function table... */
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)fa_reverb_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)fa_reverb_unlock_for_process;
    result->base.base.is_input_format_supported =
        (ForgeEffectIsInputFormatSupportedFunc)fa_reverb_is_input_format_supported;
    result->base.base.is_output_format_supported =
        (ForgeEffectIsOutputFormatSupportedFunc)fa_reverb_is_output_format_supported;
    result->base.base.initialize = (ForgeEffectInitializeFunc)fa_reverb_initialize;
    result->base.base.reset = (ForgeEffectResetFunc)fa_reverb_reset;
    result->base.base.process = (ForgeEffectProcessFunc)fa_reverb_process;
    result->base.base.kind = ForgeEffectKindReverb;
    result->base.base.set_reverb_target = (ForgeEffectSetReverbTargetFunc)fa_reverb_set_target;
    result->base.base.advance_automation = (ForgeEffectAdvanceAutomationFunc)fa_reverb_advance_automation;
    result->base.on_set_parameters = fa_reverb_on_set_parameters;
    result->base.destructor = fa_reverb_free;

    /* Prepare the default parameters */
    result->base.base.initialize(result, &default_parameters, sizeof(ForgeReverbParameters));

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}

void forge_reverb_convert_i3dl2(const ForgeReverbI3DL2Parameters *i3dl2, ForgeReverbParameters *parameters) {
    float reflectionsDelay;
    float reverbDelay;

    parameters->rear_delay = FORGE_REVERB_DEFAULT_REAR_DELAY;
    parameters->position_left = FORGE_REVERB_DEFAULT_POSITION;
    parameters->position_right = FORGE_REVERB_DEFAULT_POSITION;
    parameters->position_matrix_left = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    parameters->position_matrix_right = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    parameters->room_size = FORGE_REVERB_DEFAULT_ROOM_SIZE;
    parameters->low_eq_cutoff = 4;
    parameters->high_eq_cutoff = 6;

    parameters->room_filter_main = (float)i3dl2->room / 100.0f;
    parameters->room_filter_hf = (float)i3dl2->room_hf / 100.0f;

    if (i3dl2->decay_hf_ratio >= 1.0f) {
        int32_t index = (int32_t)(-4.0 * forge_log10(i3dl2->decay_hf_ratio));
        if (index < -8) {
            index = -8;
        }
        parameters->low_eq_gain = (uint8_t)((index < 0) ? index + 8 : 8);
        parameters->high_eq_gain = 8;
        parameters->decay_time = i3dl2->decay_time * i3dl2->decay_hf_ratio;
    } else {
        int32_t index = (int32_t)(4.0 * forge_log10(i3dl2->decay_hf_ratio));
        if (index < -8) {
            index = -8;
        }
        parameters->low_eq_gain = 8;
        parameters->high_eq_gain = (uint8_t)((index < 0) ? index + 8 : 8);
        parameters->decay_time = i3dl2->decay_time;
    }

    reflectionsDelay = i3dl2->reflections_delay * 1000.0f;
    if (reflectionsDelay >= FORGE_REVERB_MAX_REFLECTIONS_DELAY) {
        reflectionsDelay = (float)(FORGE_REVERB_MAX_REFLECTIONS_DELAY - 1);
    } else if (reflectionsDelay <= 1) {
        reflectionsDelay = 1;
    }
    parameters->reflections_delay = (uint32_t)reflectionsDelay;

    reverbDelay = i3dl2->reverb_delay * 1000.0f;
    if (reverbDelay >= FORGE_REVERB_MAX_REVERB_DELAY) {
        reverbDelay = (float)(FORGE_REVERB_MAX_REVERB_DELAY - 1);
    }
    parameters->reverb_delay = (uint8_t)reverbDelay;

    parameters->reflections_gain = i3dl2->reflections / 100.0f;
    parameters->reverb_gain = i3dl2->reverb / 100.0f;
    parameters->early_diffusion = (uint8_t)(15.0f * i3dl2->diffusion / 100.0f);
    parameters->late_diffusion = parameters->early_diffusion;
    parameters->density = i3dl2->density;
    parameters->room_filter_freq = i3dl2->hf_reference;

    parameters->wet_dry_mix = i3dl2->wet_dry_mix;
}

/* Public 7.1 reverb API */

ForgeResult forge_create_reverb_7point1(ForgeEffect **effect, uint32_t flags) {
    return forge_create_reverb_7point1_with_allocator(effect, flags, forge_malloc, forge_free, forge_realloc);
}

ForgeResult forge_create_reverb_7point1_with_allocator(ForgeEffect **effect, uint32_t flags,
                                                       ForgeMallocFunc custom_malloc, ForgeFreeFunc custom_free,
                                                       ForgeReallocFunc custom_realloc) {
    const ForgeReverbParameters7Point1 default_parameters = {
        FORGE_REVERB_DEFAULT_WET_DRY_MIX,      FORGE_REVERB_DEFAULT_REFLECTIONS_DELAY,
        FORGE_REVERB_DEFAULT_REVERB_DELAY,     FORGE_REVERB_DEFAULT_7_1_REAR_DELAY,
        FORGE_REVERB_DEFAULT_7_1_SIDE_DELAY,   FORGE_REVERB_DEFAULT_POSITION,
        FORGE_REVERB_DEFAULT_POSITION,         FORGE_REVERB_DEFAULT_POSITION_MATRIX,
        FORGE_REVERB_DEFAULT_POSITION_MATRIX,  FORGE_REVERB_DEFAULT_EARLY_DIFFUSION,
        FORGE_REVERB_DEFAULT_LATE_DIFFUSION,   FORGE_REVERB_DEFAULT_LOW_EQ_GAIN,
        FORGE_REVERB_DEFAULT_LOW_EQ_CUTOFF,    FORGE_REVERB_DEFAULT_HIGH_EQ_GAIN,
        FORGE_REVERB_DEFAULT_HIGH_EQ_CUTOFF,   FORGE_REVERB_DEFAULT_ROOM_FILTER_FREQ,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_MAIN, FORGE_REVERB_DEFAULT_ROOM_FILTER_HF,
        FORGE_REVERB_DEFAULT_REFLECTIONS_GAIN, FORGE_REVERB_DEFAULT_REVERB_GAIN,
        FORGE_REVERB_DEFAULT_DECAY_TIME,       FORGE_REVERB_DEFAULT_DENSITY,
        FORGE_REVERB_DEFAULT_ROOM_SIZE};

    /* Allocate... */
    ForgeReverb *result = (ForgeReverb *)custom_malloc(sizeof(ForgeReverb));
    uint8_t *params = (uint8_t *)custom_malloc(sizeof(ForgeReverbParameters7Point1));
    result->parameter_layout = FORGE_REVERB_PARAMETER_LAYOUT_7POINT1;

    fa_effect_base_init_with_allocator(&result->base, &ReverbInfo, params, sizeof(ForgeReverbParameters7Point1),
                                       custom_malloc, custom_free, custom_realloc);

    result->inChannels = 0;
    result->outChannels = 0;
    result->sampleRate = 0;
    forge_zero(&result->reverb, sizeof(DspReverb));
    forge_zero(&result->automation, sizeof(result->automation));

    /* Function table... */
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)fa_reverb_lock_for_process;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)fa_reverb_unlock_for_process;
    result->base.base.is_input_format_supported =
        (ForgeEffectIsInputFormatSupportedFunc)fa_reverb_is_input_format_supported;
    result->base.base.is_output_format_supported =
        (ForgeEffectIsOutputFormatSupportedFunc)fa_reverb_is_output_format_supported;
    result->base.base.initialize = (ForgeEffectInitializeFunc)fa_reverb_initialize;
    result->base.base.reset = (ForgeEffectResetFunc)fa_reverb_reset;
    result->base.base.process = (ForgeEffectProcessFunc)fa_reverb_process;
    result->base.base.kind = ForgeEffectKindReverb7Point1;
    result->base.base.set_reverb_target = (ForgeEffectSetReverbTargetFunc)fa_reverb_set_target;
    result->base.base.advance_automation = (ForgeEffectAdvanceAutomationFunc)fa_reverb_advance_automation;
    result->base.on_set_parameters = fa_reverb_on_set_parameters;
    result->base.destructor = fa_reverb_free;

    /* Prepare the default parameters */
    result->base.base.initialize(result, &default_parameters, sizeof(ForgeReverbParameters7Point1));

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}

void forge_reverb_convert_i3dl2_7point1(const ForgeReverbI3DL2Parameters *i3dl2,
                                        ForgeReverbParameters7Point1 *parameters, int32_t use_7point1_rear_delay) {
    float reflectionsDelay;
    float reverbDelay;

    if (use_7point1_rear_delay) {
        parameters->rear_delay = FORGE_REVERB_DEFAULT_7_1_REAR_DELAY;
    } else {
        parameters->rear_delay = FORGE_REVERB_DEFAULT_REAR_DELAY;
    }
    parameters->side_delay = FORGE_REVERB_DEFAULT_7_1_SIDE_DELAY;
    parameters->position_left = FORGE_REVERB_DEFAULT_POSITION;
    parameters->position_right = FORGE_REVERB_DEFAULT_POSITION;
    parameters->position_matrix_left = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    parameters->position_matrix_right = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    parameters->room_size = FORGE_REVERB_DEFAULT_ROOM_SIZE;
    parameters->low_eq_cutoff = 4;
    parameters->high_eq_cutoff = 6;

    parameters->room_filter_main = (float)i3dl2->room / 100.0f;
    parameters->room_filter_hf = (float)i3dl2->room_hf / 100.0f;

    if (i3dl2->decay_hf_ratio >= 1.0f) {
        int32_t index = (int32_t)(-4.0 * forge_log10(i3dl2->decay_hf_ratio));
        if (index < -8) {
            index = -8;
        }
        parameters->low_eq_gain = (uint8_t)((index < 0) ? index + 8 : 8);
        parameters->high_eq_gain = 8;
        parameters->decay_time = i3dl2->decay_time * i3dl2->decay_hf_ratio;
    } else {
        int32_t index = (int32_t)(4.0 * forge_log10(i3dl2->decay_hf_ratio));
        if (index < -8) {
            index = -8;
        }
        parameters->low_eq_gain = 8;
        parameters->high_eq_gain = (uint8_t)((index < 0) ? index + 8 : 8);
        parameters->decay_time = i3dl2->decay_time;
    }

    reflectionsDelay = i3dl2->reflections_delay * 1000.0f;
    if (reflectionsDelay >= FORGE_REVERB_MAX_REFLECTIONS_DELAY) {
        reflectionsDelay = (float)(FORGE_REVERB_MAX_REFLECTIONS_DELAY - 1);
    } else if (reflectionsDelay <= 1) {
        reflectionsDelay = 1;
    }
    parameters->reflections_delay = (uint32_t)reflectionsDelay;

    reverbDelay = i3dl2->reverb_delay * 1000.0f;
    if (reverbDelay >= FORGE_REVERB_MAX_REVERB_DELAY) {
        reverbDelay = (float)(FORGE_REVERB_MAX_REVERB_DELAY - 1);
    }
    parameters->reverb_delay = (uint8_t)reverbDelay;

    parameters->reflections_gain = i3dl2->reflections / 100.0f;
    parameters->reverb_gain = i3dl2->reverb / 100.0f;
    parameters->early_diffusion = (uint8_t)(15.0f * i3dl2->diffusion / 100.0f);
    parameters->late_diffusion = parameters->early_diffusion;
    parameters->density = i3dl2->density;
    parameters->room_filter_freq = i3dl2->hf_reference;

    parameters->wet_dry_mix = i3dl2->wet_dry_mix;
}
