/*
 * ForgeAudio
 * Forked from FAudio
 *
 * Copyright (c) 2026 ForgeAudio
 *
 * Licensed under the same terms as FAudio:
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

#include "forge_effects.h"
#include "forge_audio_internal.h"

/* #define DISABLE_SUBNORMALS */
#ifdef DISABLE_SUBNORMALS
#include <math.h> /* ONLY USE THIS FOR fpclassify/_fpclass! */
#define IS_SUBNORMAL(a) (fpclassify(a) == FP_SUBNORMAL)
#endif /* DISABLE_SUBNORMALS */

/* Utility Functions */

static inline float DbGainToFactor(float gain)
{
    return (float) ForgeAudio_pow(10, gain / 20.0f);
}

static inline uint32_t MsToSamples(float msec, int32_t sampleRate)
{
    return (uint32_t) ((sampleRate * msec) / 1000.0f);
}

#ifndef DISABLE_SUBNORMALS
#define Undenormalize(a) ((a))
#else /* DISABLE_SUBNORMALS */
static inline float Undenormalize(float sample_in)
{
    if (IS_SUBNORMAL(sample_in))
    {
        return 0.0f;
    }
    return sample_in;
}
#endif /* DISABLE_SUBNORMALS */

/* Component - delay */

#define DSP_DELAY_MAX_DELAY_MS 300

typedef struct DspDelay
{
    int32_t     sampleRate;
    uint32_t capacity;    /* In samples */
    uint32_t delay;        /* In samples */
    uint32_t read_idx;
    uint32_t write_idx;
    float *buffer;
} DspDelay;

static inline void DspDelay_Initialize(
    DspDelay *filter,
    int32_t sampleRate,
    float delay_ms,
    ForgeMallocFunc malloc_func
) {
    ForgeAudio_assert(delay_ms >= 0 && delay_ms <= DSP_DELAY_MAX_DELAY_MS);

    filter->sampleRate = sampleRate;
    filter->capacity = MsToSamples(DSP_DELAY_MAX_DELAY_MS, sampleRate);
    filter->delay = MsToSamples(delay_ms, sampleRate);
    filter->read_idx = 0;
    filter->write_idx = filter->delay;
    filter->buffer = (float*) malloc_func(filter->capacity * sizeof(float));
    ForgeAudio_zero(filter->buffer, filter->capacity * sizeof(float));
}

static inline void DspDelay_Change(DspDelay *filter, float delay_ms)
{
    ForgeAudio_assert(delay_ms >= 0 && delay_ms <= DSP_DELAY_MAX_DELAY_MS);

    /* Length */
    filter->delay = MsToSamples(delay_ms, filter->sampleRate);
    filter->read_idx = (filter->write_idx - filter->delay + filter->capacity) % filter->capacity;
}

static inline float DspDelay_Read(DspDelay *filter)
{
    float delay_out;

    ForgeAudio_assert(filter->read_idx < filter->capacity);

    delay_out = filter->buffer[filter->read_idx];
    filter->read_idx = (filter->read_idx + 1) % filter->capacity;
    return delay_out;
}

static inline void DspDelay_Write(DspDelay *filter, float sample)
{
    ForgeAudio_assert(filter->write_idx < filter->capacity);

    filter->buffer[filter->write_idx] = sample;
    filter->write_idx = (filter->write_idx + 1) % filter->capacity;
}

static inline float DspDelay_Process(DspDelay *filter, float sample_in)
{
    float delay_out = DspDelay_Read(filter);
    DspDelay_Write(filter, sample_in);
    return delay_out;
}

/* FIXME: This is currently unused! What was it for...? -flibit
static inline float DspDelay_Tap(DspDelay *filter, uint32_t delay)
{
    ForgeAudio_assert(delay <= filter->delay);
    return filter->buffer[(filter->write_idx - delay + filter->capacity) % filter->capacity];
}
*/

static inline void DspDelay_Reset(DspDelay *filter)
{
    filter->read_idx = 0;
    filter->write_idx = filter->delay;
    ForgeAudio_zero(filter->buffer, filter->capacity * sizeof(float));
}

static inline void DspDelay_Destroy(DspDelay *filter, ForgeFreeFunc free_func)
{
    free_func(filter->buffer);
}

static inline float DspComb_FeedbackFromRT60(DspDelay *delay, float rt60_ms)
{
    float exponent = (
        (-3.0f * delay->delay * 1000.0f) /
        (delay->sampleRate * rt60_ms)
    );
    return (float) ForgeAudio_pow(10.0f, exponent);
}

/* Component - Bi-Quad Filter */

typedef enum DspBiQuadType
{
    DSP_BIQUAD_LOWSHELVING,
    DSP_BIQUAD_HIGHSHELVING
} DspBiQuadType;

typedef struct DspBiQuad
{
    int32_t sampleRate;
    float a0, a1, a2;
    float b1, b2;
    float c0, d0;
    float delay0, delay1;
} DspBiQuad;

static inline void DspBiQuad_Change(
    DspBiQuad *filter,
    DspBiQuadType type,
    float frequency,
    float q,
    float gain
) {
    const float TWOPI = 6.283185307179586476925286766559005;
    float theta_c = (TWOPI * frequency) / (float) filter->sampleRate;
    float mu = DbGainToFactor(gain);
    float beta = (type == DSP_BIQUAD_LOWSHELVING) ?
        4.0f / (1 + mu) :
        (1 + mu) / 4.0f;
    float delta = beta * (float) ForgeAudio_tan(theta_c * 0.5f);
    float gamma = (1 - delta) / (1 + delta);

    if (type == DSP_BIQUAD_LOWSHELVING)
    {
        filter->a0 = (1 - gamma) * 0.5f;
        filter->a1 = filter->a0;
    }
    else
    {
        filter->a0 = (1 + gamma) * 0.5f;
        filter->a1 = -filter->a0;
    }

    filter->a2 = 0.0f;
    filter->b1 = -gamma;
    filter->b2 = 0.0f;
    filter->c0 = mu - 1.0f;
    filter->d0 = 1.0f;
}

static inline void DspBiQuad_Initialize(
    DspBiQuad *filter,
    int32_t sampleRate,
    DspBiQuadType type,
    float frequency,    /* Corner frequency */
    float q,        /* Only used by low/high-pass filters */
    float gain        /* Only used by low/high-shelving filters */
) {
    filter->sampleRate = sampleRate;
    filter->delay0 = 0.0f;
    filter->delay1 = 0.0f;
    DspBiQuad_Change(filter, type, frequency, q, gain);
}

static inline float DspBiQuad_Process(DspBiQuad *filter, float sample_in)
{
    /* Direct Form II Transposed:
     * - Less delay registers than Direct Form I
     * - More numerically stable than Direct Form II
     */
    float result = (filter->a0 * sample_in) + filter->delay0;
    filter->delay0 = (filter->a1 * sample_in) - (filter->b1 * result) + filter->delay1;
    filter->delay1 = (filter->a2 * sample_in) - (filter->b2 * result);

    return Undenormalize(
        (result * filter->c0) +
        (sample_in * filter->d0)
    );
}

static inline void DspBiQuad_Reset(DspBiQuad *filter)
{
    filter->delay0 = 0.0f;
    filter->delay1 = 0.0f;
}

static inline void DspBiQuad_Destroy(DspBiQuad *filter)
{
}

/* Component - Comb Filter with Integrated Low/High Shelving Filters */

typedef struct DspCombShelving
{
    DspDelay comb_delay;
    float comb_feedback_gain;

    DspBiQuad low_shelving;
    DspBiQuad high_shelving;
} DspCombShelving;

static inline void DspCombShelving_Initialize(
    DspCombShelving *filter,
    int32_t sampleRate,
    float delay_ms,
    float rt60_ms,
    float low_frequency,
    float low_gain,
    float high_frequency,
    float high_gain,
    ForgeMallocFunc malloc_func
) {
    DspDelay_Initialize(&filter->comb_delay, sampleRate, delay_ms, malloc_func);
    filter->comb_feedback_gain = DspComb_FeedbackFromRT60(
        &filter->comb_delay,
        rt60_ms
    );

    DspBiQuad_Initialize(
        &filter->low_shelving,
        sampleRate,
        DSP_BIQUAD_LOWSHELVING,
        low_frequency,
        0.0f,
        low_gain
    );
    DspBiQuad_Initialize(
        &filter->high_shelving,
        sampleRate,
        DSP_BIQUAD_HIGHSHELVING,
        high_frequency,
        0.0f,
        high_gain
    );
}

static inline float DspCombShelving_Process(
    DspCombShelving *filter,
    float sample_in
) {
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

static inline void DspCombShelving_Reset(DspCombShelving *filter)
{
    DspDelay_Reset(&filter->comb_delay);
    DspBiQuad_Reset(&filter->low_shelving);
    DspBiQuad_Reset(&filter->high_shelving);
}

static inline void DspCombShelving_Destroy(
    DspCombShelving *filter,
    ForgeFreeFunc free_func
) {
    DspDelay_Destroy(&filter->comb_delay, free_func);
    DspBiQuad_Destroy(&filter->low_shelving);
    DspBiQuad_Destroy(&filter->high_shelving);
}

/* Component - Delaying All-Pass Filter */

typedef struct DspAllPass
{
    DspDelay delay;
    float feedback_gain;
} DspAllPass;

static inline void DspAllPass_Initialize(
    DspAllPass *filter,
    int32_t sampleRate,
    float delay_ms,
    float gain,
    ForgeMallocFunc malloc_func
) {
    DspDelay_Initialize(&filter->delay, sampleRate, delay_ms, malloc_func);
    filter->feedback_gain = gain;
}

static inline void DspAllPass_Change(DspAllPass *filter, float delay_ms, float gain)
{
    DspDelay_Change(&filter->delay, delay_ms);
    filter->feedback_gain = gain;
}

static inline float DspAllPass_Process(DspAllPass *filter, float sample_in)
{
    float delay_out, to_buf;

    delay_out = DspDelay_Read(&filter->delay);

    to_buf = Undenormalize(sample_in + (filter->feedback_gain * delay_out));
    DspDelay_Write(&filter->delay, to_buf);

    return Undenormalize(delay_out - (filter->feedback_gain * to_buf));
}

static inline void DspAllPass_Reset(DspAllPass *filter)
{
    DspDelay_Reset(&filter->delay);
}

static inline void DspAllPass_Destroy(DspAllPass *filter, ForgeFreeFunc free_func)
{
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

#define REVERB_COUNT_COMB    8
#define REVERB_COUNT_APF_IN    1
#define REVERB_COUNT_APF_OUT    4

static float COMB_DELAYS[REVERB_COUNT_COMB] =
{
    25.31f,
    26.94f,
    28.96f,
    30.75f,
    32.24f,
    33.80f,
    35.31f,
    36.67f
};

static float APF_IN_DELAYS[REVERB_COUNT_APF_IN] =
{
    13.28f,
/*    28.13f */
};

static float APF_OUT_DELAYS[REVERB_COUNT_APF_OUT] =
{
    5.10f,
    12.61f,
    10.0f,
    7.73f
};

typedef enum ForgeAudio_ChannelPositionFlags
{
    Position_Left = 0x1,
    Position_Right = 0x2,
    Position_Center = 0x4,
    Position_Rear = 0x8,
} ForgeAudio_ChannelPositionFlags;

static ForgeAudio_ChannelPositionFlags ForgeAudio_GetChannelPositionFlags(int32_t total_channels, int32_t channel)
{
    switch (total_channels)
    {
        case 1:
            return Position_Center;

        case 2:
            return (channel == 0) ? Position_Left : Position_Right;

        case 4:
            switch (channel)
            {
                case 0:
                    return Position_Left;
                case 1:
                    return Position_Right;
                case 2:
                    return Position_Left | Position_Rear;
                case 3:
                    return Position_Right | Position_Rear;
            }
            ForgeAudio_assert(0 && "Unsupported channel count");
            break;
        case 5:
            switch (channel)
            {
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
            ForgeAudio_assert(0 && "Unsupported channel count");
            break;
        default:
            ForgeAudio_assert(0 && "Unsupported channel count");
            break;
    }

    /* shouldn't happen, but default to left speaker */
    return Position_Left;
}

float ForgeAudio_GetStereoSpreadDelayMS(int32_t total_channels, int32_t channel)
{
    ForgeAudio_ChannelPositionFlags flags = ForgeAudio_GetChannelPositionFlags(total_channels, channel);
    return (flags & Position_Right) ? 0.5216f : 0.0f;
}

typedef struct DspReverbChannel
{
    DspDelay reverb_delay;
    DspCombShelving    lpf_comb[REVERB_COUNT_COMB];
    DspAllPass apf_out[REVERB_COUNT_APF_OUT];
    DspBiQuad room_high_shelf;
    float early_gain;
    float gain;
} DspReverbChannel;

typedef struct DspReverb
{
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

static inline void DspReverb_Create(
    DspReverb *reverb,
    int32_t sampleRate,
    int32_t in_channels,
    int32_t out_channels,
    ForgeMallocFunc malloc_func
) {
    int32_t i, c;

    ForgeAudio_assert(in_channels == 1 || in_channels == 2 || in_channels == 6);
    ForgeAudio_assert(out_channels == 1 || out_channels == 2 || out_channels == 6);

    ForgeAudio_zero(reverb, sizeof(DspReverb));
    DspDelay_Initialize(&reverb->early_delay, sampleRate, 10, malloc_func);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
    {
        DspAllPass_Initialize(
            &reverb->apf_in[i],
            sampleRate,
            APF_IN_DELAYS[i],
            0.5f,
            malloc_func
        );
    }

    if (out_channels == 6)
    {
        reverb->reverb_channels = (in_channels == 6) ? 5 : 4;
    }
    else
    {
        reverb->reverb_channels = out_channels;
    }

    for (c = 0; c < reverb->reverb_channels; c += 1)
    {
        DspDelay_Initialize(
            &reverb->channel[c].reverb_delay,
            sampleRate,
            10,
            malloc_func
        );

        for (i = 0; i < REVERB_COUNT_COMB; i += 1)
        {
            DspCombShelving_Initialize(
                &reverb->channel[c].lpf_comb[i],
                sampleRate,
                COMB_DELAYS[i] + ForgeAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c),
                500,
                500,
                -6,
                5000,
                -6,
                malloc_func
            );
        }

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
        {
            DspAllPass_Initialize(
                &reverb->channel[c].apf_out[i],
                sampleRate,
                APF_OUT_DELAYS[i] + ForgeAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c),
                0.5f,
                malloc_func
            );
        }

        DspBiQuad_Initialize(
            &reverb->channel[c].room_high_shelf,
            sampleRate,
            DSP_BIQUAD_HIGHSHELVING,
            5000,
            0,
            -10
        );
        reverb->channel[c].gain = 1.0f;
    }

    reverb->early_gain = 1.0f;
    reverb->reverb_gain = 1.0f;
    reverb->dry_ratio = 0.0f;
    reverb->wet_ratio = 1.0f;
    reverb->in_channels = in_channels;
    reverb->out_channels = out_channels;
}

static inline void DspReverb_Destroy(DspReverb *reverb, ForgeFreeFunc free_func)
{
    int32_t i, c;

    DspDelay_Destroy(&reverb->early_delay, free_func);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
    {
        DspAllPass_Destroy(&reverb->apf_in[i], free_func);
    }

    for (c = 0; c < reverb->reverb_channels; c += 1)
    {
        DspDelay_Destroy(&reverb->channel[c].reverb_delay, free_func);

        for (i = 0; i < REVERB_COUNT_COMB; i += 1)
        {
            DspCombShelving_Destroy(
                &reverb->channel[c].lpf_comb[i],
                free_func
            );
        }

        DspBiQuad_Destroy(&reverb->channel[c].room_high_shelf);

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
        {
            DspAllPass_Destroy(
                &reverb->channel[c].apf_out[i],
                free_func
            );
        }
    }
}

static inline void DspReverb_SetParameters(
    DspReverb *reverb,
    ForgeReverbParameters *params
) {
    float early_diffusion, late_diffusion;
    DspCombShelving *comb;
    int32_t i, c;

    /* Pre-delay */
    DspDelay_Change(&reverb->early_delay, (float) params->reflections_delay);

    /* Early reflections - diffusion */
    early_diffusion = 0.6f - ((params->early_diffusion / 15.0f) * 0.2f);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
    {
        DspAllPass_Change(
            &reverb->apf_in[i],
            APF_IN_DELAYS[i],
            early_diffusion
        );
    }

    /* Reverberation */
    for (c = 0; c < reverb->reverb_channels; c += 1)
    {
        float channel_delay =
            (ForgeAudio_GetChannelPositionFlags(reverb->reverb_channels, c) & Position_Rear) ?
            params->rear_delay :
            0.0f;

        DspDelay_Change(
            &reverb->channel[c].reverb_delay,
            (float) params->reverb_delay + channel_delay
        );

        for (i = 0; i < REVERB_COUNT_COMB; i += 1)
        {
            comb = &reverb->channel[c].lpf_comb[i];

            /* Set decay time of comb filter */
            DspDelay_Change(
                &comb->comb_delay,
                COMB_DELAYS[i] + ForgeAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c)
            );
            comb->comb_feedback_gain = DspComb_FeedbackFromRT60(
                &comb->comb_delay,
                ForgeAudio_max(params->decay_time, FORGE_REVERB_MIN_DECAY_TIME) * 1000.0f
            );

            /* High/Low shelving */
            DspBiQuad_Change(
                &comb->low_shelving,
                DSP_BIQUAD_LOWSHELVING,
                50.0f + params->low_eq_cutoff * 50.0f,
                0.0f,
                params->low_eq_gain - 8.0f
            );
            DspBiQuad_Change(
                &comb->high_shelving,
                DSP_BIQUAD_HIGHSHELVING,
                1000 + params->high_eq_cutoff * 500.0f,
                0.0f,
                params->high_eq_gain - 8.0f
            );
        }
    }

    /* Gain */
    reverb->early_gain = DbGainToFactor(params->reflections_gain);
    reverb->reverb_gain = DbGainToFactor(params->reverb_gain);
    reverb->room_gain = DbGainToFactor(params->room_filter_main);

    /* Late diffusion */
    late_diffusion = 0.6f - ((params->late_diffusion / 15.0f) * 0.2f);

    for (c = 0; c < reverb->reverb_channels; c += 1)
    {
        ForgeAudio_ChannelPositionFlags position = ForgeAudio_GetChannelPositionFlags(reverb->reverb_channels, c);
        float gain;

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
        {
            DspAllPass_Change(
                &reverb->channel[c].apf_out[i],
                APF_OUT_DELAYS[i] + ForgeAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c),
                late_diffusion
            );
        }

        DspBiQuad_Change(
            &reverb->channel[c].room_high_shelf,
            DSP_BIQUAD_HIGHSHELVING,
            params->room_filter_freq,
            0.0f,
            params->room_filter_main + params->room_filter_hf
        );

        if (position & Position_Left)
        {
            gain = params->position_matrix_left;
        }
        else if (position & Position_Right)
        {
            gain = params->position_matrix_right;
        }
        else /*if (position & Position_Center) */
        {
            gain = (params->position_matrix_left + params->position_matrix_right) / 2.0f;
        }
        reverb->channel[c].gain = 1.5f - (gain / 27.0f) * 0.5f;

        if (position & Position_Rear)
        {
            /* Rear-channel Attenuation */
            reverb->channel[c].gain *= 0.75f;
        }

        if (position & Position_Left)
        {
            gain = params->position_left;
        }
        else if (position & Position_Right)
        {
            gain = params->position_right;
        }
        else /*if (position & Position_Center) */
        {
            gain = (params->position_left + params->position_right) / 2.0f;
        }
        reverb->channel[c].early_gain = 1.2f - (gain / 6.0f) * 0.2f;
        reverb->channel[c].early_gain = (
            reverb->channel[c].early_gain *
            reverb->early_gain
        );
    }

    /* Wet/Dry Mix (100 = fully wet, 0 = fully dry) */
    reverb->wet_ratio = params->wet_dry_mix / 100.0f;
    reverb->dry_ratio = 1.0f - reverb->wet_ratio;
}

static inline void DspReverb_SetParameters9(
    DspReverb *reverb,
    ForgeReverbParameters7Point1 *params
) {
    ForgeReverbParameters oldParams;
    oldParams.wet_dry_mix = params->wet_dry_mix;
    oldParams.reflections_delay = params->reflections_delay;
    oldParams.reverb_delay = params->reverb_delay;
    oldParams.rear_delay = params->rear_delay;
    oldParams.position_left = params->position_left;
    oldParams.position_right = params->position_right;
    oldParams.position_matrix_left = params->position_matrix_left;
    oldParams.position_matrix_right = params->position_matrix_right;
    oldParams.early_diffusion = params->early_diffusion;
    oldParams.late_diffusion = params->late_diffusion;
    oldParams.low_eq_gain = params->low_eq_gain;
    oldParams.low_eq_cutoff = params->low_eq_cutoff;
    oldParams.high_eq_gain = params->high_eq_gain;
    oldParams.high_eq_cutoff = params->high_eq_cutoff;
    oldParams.room_filter_freq = params->room_filter_freq;
    oldParams.room_filter_main = params->room_filter_main;
    oldParams.room_filter_hf = params->room_filter_hf;
    oldParams.reflections_gain = params->reflections_gain;
    oldParams.reverb_gain = params->reverb_gain;
    oldParams.decay_time = params->decay_time;
    oldParams.density = params->density;
    oldParams.room_size = params->room_size;
    DspReverb_SetParameters(reverb, &oldParams);
}

static inline float DspReverb_INTERNAL_ProcessEarly(
    DspReverb *reverb,
    float sample_in
) {
    float early;
    int32_t i;

    /* Pre-delay */
    early = DspDelay_Process(&reverb->early_delay, sample_in);

    /* Early reflections */
    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
    {
        early = DspAllPass_Process(&reverb->apf_in[i], early);
    }

    return early;
}

static inline float DspReverb_INTERNAL_ProcessChannel(
    DspReverb *reverb,
    DspReverbChannel *channel,
    float sample_in
) {
    float revdelay, early_late, sample_out;
    int32_t i;

    revdelay = DspDelay_Process(&channel->reverb_delay, sample_in);

    sample_out = 0.0f;
    for (i = 0; i < REVERB_COUNT_COMB; i += 1)
    {
        sample_out += DspCombShelving_Process(
            &channel->lpf_comb[i],
            revdelay
        );
    }
    sample_out /= (float) REVERB_COUNT_COMB;

    /* Output diffusion */
    for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
    {
        sample_out = DspAllPass_Process(
            &channel->apf_out[i],
            sample_out
        );
    }

    /* Combine early reflections and reverberation */
    early_late = (
        (sample_in * channel->early_gain) +
        (sample_out * reverb->reverb_gain)
    );

    /* room filter */
    sample_out = DspBiQuad_Process(
        &channel->room_high_shelf,
        early_late * reverb->room_gain
    );

    /* position_matrix_left/Right */
    return sample_out * channel->gain;
}

/* reverb process Functions */

static inline float DspReverb_INTERNAL_Process_1_to_1(
    DspReverb *reverb,
    float *restrict samples_in,
    float *restrict samples_out,
    size_t sample_count
) {
    const float *in_end = samples_in + sample_count;
    float in, early, late, out;
    float squared_sum = 0.0f;

    while (samples_in < in_end)
    {
        /* Input */
        in = *samples_in++;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation */
        late = DspReverb_INTERNAL_ProcessChannel(
            reverb,
            &reverb->channel[0],
            early
        );

        /* Wet/Dry Mix */
        out = (late * reverb->wet_ratio) + (in * reverb->dry_ratio);
        squared_sum += out * out;

        /* Output */
        *samples_out++ = out;
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_1_to_5p1(
    DspReverb *reverb,
    float *restrict samples_in,
    float *restrict samples_out,
    size_t sample_count
) {
    const float *in_end = samples_in + sample_count;
    float in, in_ratio, early, late[4];
    float squared_sum = 0.0f;
    int32_t c;

    while (samples_in < in_end)
    {
        /* Input */
        in = *samples_in++;
        in_ratio = in * reverb->dry_ratio;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        for (c = 0; c < 4; c += 1)
        {
            late[c] = (DspReverb_INTERNAL_ProcessChannel(
                reverb,
                &reverb->channel[c],
                early
            ) * reverb->wet_ratio) + in_ratio;
            squared_sum += late[c] * late[c];
        }

        /* Output */
        *samples_out++ = late[0];    /* Front Left */
        *samples_out++ = late[1];    /* Front Right */
        *samples_out++ = 0.0f;        /* Center */
        *samples_out++ = 0.0f;        /* LFE */
        *samples_out++ = late[2];    /* Rear Left */
        *samples_out++ = late[3];    /* Rear Right */
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_2_to_2(
    DspReverb *reverb,
    float *restrict samples_in,
    float *restrict samples_out,
    size_t sample_count
) {
    const float *in_end = samples_in + sample_count;
    float in, early, late[2];
    float squared_sum = 0;

    while (samples_in < in_end)
    {
        /* Input - Combine 2 channels into 1 */
        in = (samples_in[0] + samples_in[1]) / 2.0f;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        late[0] = (DspReverb_INTERNAL_ProcessChannel(
            reverb,
            &reverb->channel[0],
            early
        ) * reverb->wet_ratio) + samples_in[0] * reverb->dry_ratio;
        late[1] = (DspReverb_INTERNAL_ProcessChannel(
            reverb,
            &reverb->channel[1],
            early
        ) * reverb->wet_ratio) + samples_in[1] * reverb->dry_ratio;
        squared_sum += (late[0] * late[0]) + (late[1] * late[1]);

        /* Output */
        *samples_out++ = late[0];
        *samples_out++ = late[1];

        samples_in += 2;
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_2_to_5p1(
    DspReverb *reverb,
    float *restrict samples_in,
    float *restrict samples_out,
    size_t sample_count
) {
    const float *in_end = samples_in + sample_count;
    float in, in_ratio, early, late[4];
    float squared_sum = 0;
    int32_t c;

    while (samples_in < in_end)
    {
        /* Input - Combine 2 channels into 1 */
        in = (samples_in[0] + samples_in[1]) / 2.0f;
        in_ratio = in * reverb->dry_ratio;
        samples_in += 2;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        for (c = 0; c < 4; c += 1)
        {
            late[c] = (DspReverb_INTERNAL_ProcessChannel(
                reverb,
                &reverb->channel[c],
                early
            ) * reverb->wet_ratio) + in_ratio;
            squared_sum += late[c] * late[c];
        }

        /* Output */
        *samples_out++ = late[0];    /* Front Left */
        *samples_out++ = late[1];    /* Front Right */
        *samples_out++ = 0.0f;        /* Center */
        *samples_out++ = 0.0f;        /* LFE */
        *samples_out++ = late[2];    /* Rear Left */
        *samples_out++ = late[3];    /* Rear Right */
    }

    return squared_sum;
}

static inline float DspReverb_INTERNAL_Process_5p1_to_5p1(
    DspReverb *reverb,
    float *restrict samples_in,
    float *restrict samples_out,
    size_t sample_count
) {
    const float *in_end = samples_in + sample_count;
    float in, in_ratio, early, late[5];
    float squared_sum = 0;
    int32_t c;

    while (samples_in < in_end)
    {
        /* Input - Combine non-LFE channels into 1 */
        in = (samples_in[0] + samples_in[1] + samples_in[2] +
                samples_in[4] + samples_in[5]) / 5.0f;
        in_ratio = in * reverb->dry_ratio;

        /* Early reflections */
        early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

        /* Reverberation with Wet/Dry Mix */
        for (c = 0; c < 5; c += 1)
        {
            late[c] = (DspReverb_INTERNAL_ProcessChannel(
                reverb,
                &reverb->channel[c],
                early
            ) * reverb->wet_ratio) + in_ratio;
            squared_sum += late[c] * late[c];
        }

        /* Output */
        *samples_out++ = late[0];    /* Front Left */
        *samples_out++ = late[1];    /* Front Right */
        *samples_out++ = late[2];    /* Center */
        *samples_out++ = samples_in[3];    /* LFE, pass through */
        *samples_out++ = late[3];    /* Rear Left */
        *samples_out++ = late[4];    /* Rear Right */

        samples_in += 6;
    }

    return squared_sum;
}

#undef OUTPUT_SAMPLE

/* reverb ForgeEffect Implementation */

static ForgeEffectInfo ReverbInfo =
{
    /*.flags = */ (
        FORGE_EFFECT_FLAG_SAMPLE_RATE_MUST_MATCH |
        FORGE_EFFECT_FLAG_BITS_PER_SAMPLE_MUST_MATCH |
        FORGE_EFFECT_FLAG_BUFFER_COUNT_MUST_MATCH |
        FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED
    ),
    /*.min_input_buffer_count = */ 1,
    /*.max_input_buffer_count = */ 1,
    /*.min_output_buffer_count = */ 1,
    /*.max_output_buffer_count = */ 1
};

typedef struct ForgeReverb
{
    ForgeEffectBase base;

    uint16_t inChannels;
    uint16_t outChannels;
    uint32_t sampleRate;
    uint16_t inBlockAlign;
    uint16_t outBlockAlign;

    uint8_t apiVersion;
    DspReverb reverb;
} ForgeReverb;

static inline int8_t IsFloatFormat(const ForgeAudioFormat *format)
{
    if (format->format_tag == FORGE_AUDIO_FORMAT_IEEE_FLOAT)
    {
        /* Plain ol' WaveFormatEx */
        return 1;
    }

    if (format->format_tag == FORGE_AUDIO_FORMAT_EXTENSIBLE)
    {
        /* WaveFormatExtensible, match GUID */
        #define MAKE_SUBFORMAT_GUID(guid, fmt) \
            static ForgeGuid KSDATAFORMAT_SUBTYPE_##guid = \
            { \
                (uint16_t) (fmt), 0x0000, 0x0010, \
                { \
                    0x80, 0x00, 0x00, 0xaa, \
                    0x00, 0x38, 0x9b, 0x71 \
                } \
            }
        MAKE_SUBFORMAT_GUID(IEEE_FLOAT, 3);
        #undef MAKE_SUBFORMAT_GUID

        if (ForgeAudio_memcmp(
            &((ForgeAudioFormatExtensible*) format)->sub_format,
            &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,
            sizeof(ForgeGuid)
        ) == 0) {
            return 1;
        }
    }

    return 0;
}

ForgeResult ForgeReverb_IsInputFormatSupported(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *output_format,
    const ForgeAudioFormat *requested_input_format,
    ForgeAudioFormat **supported_input_format
) {
    ForgeResult result = ForgeResultSuccess;

#define SET_SUPPORTED_FIELD(field, value)    \
    result = ForgeResultFormatSuggested;    \
    if (supported_input_format && *supported_input_format)    \
    {    \
        (*supported_input_format)->field = (value);    \
    }

    /* Sample Rate */
    if (output_format->sample_rate != requested_input_format->sample_rate)
    {
        SET_SUPPORTED_FIELD(sample_rate, output_format->sample_rate);
    }

    /* Data type */
    if (!IsFloatFormat(requested_input_format))
    {
        SET_SUPPORTED_FIELD(format_tag, FORGE_AUDIO_FORMAT_IEEE_FLOAT);
    }

    /* Input/Output Channel Count */
    if (output_format->channels == 1 || output_format->channels == 2)
    {
        if (requested_input_format->channels != output_format->channels)
        {
            SET_SUPPORTED_FIELD(channels, output_format->channels);
        }
    }
    else if (output_format->channels == 6)
    {
        if (    requested_input_format->channels != 1 &&
            requested_input_format->channels != 2 &&
            requested_input_format->channels != 6    )
        {
            SET_SUPPORTED_FIELD(channels, 1);
        }
    }
    else
    {
        SET_SUPPORTED_FIELD(channels, 1);
    }

#undef SET_SUPPORTED_FIELD

    return result;
}


ForgeResult ForgeReverb_IsOutputFormatSupported(
    ForgeEffectBase *effect,
    const ForgeAudioFormat *input_format,
    const ForgeAudioFormat *requested_output_format,
    ForgeAudioFormat **supported_output_format
) {
    ForgeResult result = ForgeResultSuccess;

#define SET_SUPPORTED_FIELD(field, value)    \
    result = ForgeResultFormatSuggested;    \
    if (supported_output_format && *supported_output_format)    \
    {    \
        (*supported_output_format)->field = (value);    \
    }

    /* Sample Rate */
    if (input_format->sample_rate != requested_output_format->sample_rate)
    {
        SET_SUPPORTED_FIELD(sample_rate, input_format->sample_rate);
    }

    /* Data type */
    if (!IsFloatFormat(requested_output_format))
    {
        SET_SUPPORTED_FIELD(format_tag, FORGE_AUDIO_FORMAT_IEEE_FLOAT);
    }

    /* Input/Output Channel Count */
    if (input_format->channels == 1 || input_format->channels == 2)
    {
        if (    requested_output_format->channels != input_format->channels &&
            requested_output_format->channels != 6)
        {
            SET_SUPPORTED_FIELD(channels, input_format->channels);
        }
    }
    else if (input_format->channels == 6)
    {
        if (requested_output_format->channels != 6)
        {
            SET_SUPPORTED_FIELD(channels, input_format->channels);
        }
    }
    else
    {
        SET_SUPPORTED_FIELD(channels, 1);
    }

#undef SET_SUPPORTED_FIELD

    return result;
}

ForgeResult ForgeReverb_Initialize(
    ForgeReverb *effect,
    const void* data,
    uint32_t data_byte_size
) {
    #define INITPARAMS(offset) \
        ForgeAudio_memcpy( \
            effect->base.parameter_blocks + data_byte_size * offset, \
            data, \
            data_byte_size \
        );
    INITPARAMS(0)
    INITPARAMS(1)
    INITPARAMS(2)
    #undef INITPARAMS
    return 0;
}

ForgeResult ForgeReverb_LockForProcess(
    ForgeReverb *effect,
    uint32_t input_locked_parameter_count,
    const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t output_locked_parameter_count,
    const ForgeEffectLockBuffer *output_locked_parameters
) {
    ForgeResult result;

    /* reverb specific validation */
    if (!IsFloatFormat(input_locked_parameters->format))
    {
        return ForgeResultEffectFormatUnsupported;
    }

    if (    input_locked_parameters->format->sample_rate < FORGE_REVERB_MIN_SAMPLE_RATE ||
        input_locked_parameters->format->sample_rate > FORGE_REVERB_MAX_SAMPLE_RATE    )
    {
        return ForgeResultEffectFormatUnsupported;
    }

    if (!(    (input_locked_parameters->format->channels == 1 &&
            (output_locked_parameters->format->channels == 1 ||
             output_locked_parameters->format->channels == 6)) ||
        (input_locked_parameters->format->channels == 2 &&
            (output_locked_parameters->format->channels == 2 ||
             output_locked_parameters->format->channels == 6)) ||
        (input_locked_parameters->format->channels == 6 &&
            output_locked_parameters->format->channels == 6)))
    {
        return ForgeResultEffectFormatUnsupported;
    }

    result = forge_effect_base_lock_for_process(
        &effect->base,
        input_locked_parameter_count,
        input_locked_parameters,
        output_locked_parameter_count,
        output_locked_parameters
    );
    if (result != 0)
    {
        return result;
    }

    /* Save the things we care about */
    effect->inChannels = input_locked_parameters->format->channels;
    effect->outChannels = output_locked_parameters->format->channels;
    effect->sampleRate = output_locked_parameters->format->sample_rate;
    effect->inBlockAlign = input_locked_parameters->format->block_align;
    effect->outBlockAlign = output_locked_parameters->format->block_align;

    /* Create the network */
    DspReverb_Create(
        &effect->reverb,
        effect->sampleRate,
        effect->inChannels,
        effect->outChannels,
        effect->base.malloc_func
    );

    /* initialize the effect to a default setting */
    if (effect->apiVersion == 9)
    {
        DspReverb_SetParameters9(
            &effect->reverb,
            (ForgeReverbParameters7Point1*) effect->base.parameter_blocks
        );
    }
    else
    {
        DspReverb_SetParameters(
            &effect->reverb,
            (ForgeReverbParameters*) effect->base.parameter_blocks
        );
    }

    return 0;
}

void ForgeReverb_UnlockForProcess(ForgeReverb *effect)
{
    DspReverb_Destroy(&effect->reverb, effect->base.free_func);
    ForgeAudio_zero(&effect->reverb, sizeof(DspReverb));
    forge_effect_base_unlock_for_process(&effect->base);
}

static inline void ForgeReverb_CopyBuffer(
    ForgeReverb *effect,
    float *restrict buffer_in,
    float *restrict buffer_out,
    size_t frames_in
) {
    /* In-place processing? */
    if (buffer_in == buffer_out)
    {
        return;
    }

    /* equal channel count */
    if (effect->inBlockAlign == effect->outBlockAlign)
    {
        ForgeAudio_memcpy(
            buffer_out,
            buffer_in,
            effect->inBlockAlign * frames_in
        );
        return;
    }

    /* 1 -> 5.1 */
    if (effect->inChannels == 1 && effect->outChannels == 6)
    {
        const float *in_end = buffer_in + frames_in;
        while (buffer_in < in_end)
        {
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
    if (effect->inChannels == 2 && effect->outChannels == 6)
    {
        const float *in_end = buffer_in + (frames_in * 2);
        while (buffer_in < in_end)
        {
            *buffer_out++ = *buffer_in++;
            *buffer_out++ = *buffer_in++;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
            *buffer_out++ = 0.0f;
        }
        return;
    }

    ForgeAudio_assert(0 && "Unsupported channel combination");
    ForgeAudio_zero(buffer_out, effect->outBlockAlign * frames_in);
}

void ForgeReverb_Process(
    ForgeReverb *effect,
    uint32_t input_process_parameter_count,
    const ForgeEffectProcessBuffer* input_process_parameters,
    uint32_t output_process_parameter_count,
    ForgeEffectProcessBuffer* output_process_parameters,
    int32_t is_enabled
) {
    ForgeReverbParameters *params;
    uint8_t update_params = forge_effect_base_parameters_changed(&effect->base);
    float total;

    params = (ForgeReverbParameters*) forge_effect_base_begin_process(&effect->base);

    /* Update parameters before doing anything else  */
    if (update_params)
    {
        if (effect->apiVersion == 9)
        {
            DspReverb_SetParameters9(
                &effect->reverb,
                (ForgeReverbParameters7Point1*) params
            );
        }
        else
        {
            DspReverb_SetParameters(&effect->reverb, params);
        }
    }

    /* Handle disabled filter */
    if (is_enabled == 0)
    {
        output_process_parameters->buffer_flags = input_process_parameters->buffer_flags;

        if (output_process_parameters->buffer_flags != FORGE_EFFECT_BUFFER_SILENT)
        {
            ForgeReverb_CopyBuffer(
                effect,
                (float*) input_process_parameters->buffer,
                (float*) output_process_parameters->buffer,
                input_process_parameters->valid_frame_count
            );
        }

        forge_effect_base_end_process(&effect->base);
        return;
    }

    /* Use a silent buffer when no input buffer is available to play the effect tail. */
    if (input_process_parameters->buffer_flags == FORGE_EFFECT_BUFFER_SILENT)
    {
        /* Make sure input data is usable. FIXME: Is this required? */
        ForgeAudio_zero(
            input_process_parameters->buffer,
            input_process_parameters->valid_frame_count * effect->inBlockAlign
        );
    }

    /* Run reverb effect */
    #define PROCESS(pin, pout) \
        DspReverb_INTERNAL_Process_##pin##_to_##pout( \
            &effect->reverb, \
            (float*) input_process_parameters->buffer, \
            (float*) output_process_parameters->buffer, \
            input_process_parameters->valid_frame_count * effect->inChannels \
        )
    switch (effect->reverb.out_channels)
    {
        case 1:
            total = PROCESS(1, 1);
            break;
        case 2:
            total = PROCESS(2, 2);
            break;
        default: /* 5.1 */
            if (effect->reverb.in_channels == 1)
            {
                total = PROCESS(1, 5p1);
            }
            else if (effect->reverb.in_channels == 2)
            {
                total = PROCESS(2, 5p1);
            }
            else /* 5.1 */
            {
                total = PROCESS(5p1, 5p1);
            }
            break;
    }
    #undef PROCESS

    /* Set buffer_flags to silent so PLAY_TAILS knows when to stop */
    output_process_parameters->buffer_flags = (total < 0.0000001f) ?
        FORGE_EFFECT_BUFFER_SILENT :
        FORGE_EFFECT_BUFFER_VALID;

    forge_effect_base_end_process(&effect->base);
}

void ForgeReverb_Reset(ForgeReverb *effect)
{
    int32_t i, c;
    forge_effect_base_reset(&effect->base);

    /* reset the cached state of the reverb filter */
    DspDelay_Reset(&effect->reverb.early_delay);

    for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
    {
        DspAllPass_Reset(&effect->reverb.apf_in[i]);
    }

    for (c = 0; c < effect->reverb.reverb_channels; c += 1)
    {
        DspDelay_Reset(&effect->reverb.channel[c].reverb_delay);

        for (i = 0; i < REVERB_COUNT_COMB; i += 1)
        {
            DspCombShelving_Reset(&effect->reverb.channel[c].lpf_comb[i]);
        }

        DspBiQuad_Reset(&effect->reverb.channel[c].room_high_shelf);

        for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
        {
            DspAllPass_Reset(&effect->reverb.channel[c].apf_out[i]);
        }
    }
}

void ForgeReverb_Free(void* effect)
{
    ForgeReverb *reverb = (ForgeReverb*) effect;
    DspReverb_Destroy(&reverb->reverb, reverb->base.free_func);
    reverb->base.free_func(reverb->base.parameter_blocks);
    reverb->base.free_func(effect);
}

/* Public API (Version 7) */

ForgeResult forge_create_reverb(ForgeEffect** effect, uint32_t flags)
{
    return forge_create_reverb_with_allocator(
        effect,
        flags,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

ForgeResult forge_create_reverb_with_allocator(
    ForgeEffect** effect,
    uint32_t flags,
    ForgeMallocFunc custom_malloc,
    ForgeFreeFunc custom_free,
    ForgeReallocFunc custom_realloc
) {
    const ForgeReverbParameters fxdefault =
    {
        FORGE_REVERB_DEFAULT_WET_DRY_MIX,
        FORGE_REVERB_DEFAULT_REFLECTIONS_DELAY,
        FORGE_REVERB_DEFAULT_REVERB_DELAY,
        FORGE_REVERB_DEFAULT_REAR_DELAY,
        FORGE_REVERB_DEFAULT_POSITION,
        FORGE_REVERB_DEFAULT_POSITION,
        FORGE_REVERB_DEFAULT_POSITION_MATRIX,
        FORGE_REVERB_DEFAULT_POSITION_MATRIX,
        FORGE_REVERB_DEFAULT_EARLY_DIFFUSION,
        FORGE_REVERB_DEFAULT_LATE_DIFFUSION,
        FORGE_REVERB_DEFAULT_LOW_EQ_GAIN,
        FORGE_REVERB_DEFAULT_LOW_EQ_CUTOFF,
        FORGE_REVERB_DEFAULT_HIGH_EQ_GAIN,
        FORGE_REVERB_DEFAULT_HIGH_EQ_CUTOFF,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_FREQ,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_MAIN,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_HF,
        FORGE_REVERB_DEFAULT_REFLECTIONS_GAIN,
        FORGE_REVERB_DEFAULT_REVERB_GAIN,
        FORGE_REVERB_DEFAULT_DECAY_TIME,
        FORGE_REVERB_DEFAULT_DENSITY,
        FORGE_REVERB_DEFAULT_ROOM_SIZE
    };

    /* Allocate... */
    ForgeReverb *result = (ForgeReverb*) custom_malloc(sizeof(ForgeReverb));
    uint8_t *params = (uint8_t*) custom_malloc(
        sizeof(ForgeReverbParameters) * 3
    );
    result->apiVersion = 7;

    forge_effect_base_init_with_allocator(
        &result->base,
        &ReverbInfo,
        params,
        sizeof(ForgeReverbParameters),
        0,
        custom_malloc,
        custom_free,
        custom_realloc
    );

    result->inChannels = 0;
    result->outChannels = 0;
    result->sampleRate = 0;
    ForgeAudio_zero(&result->reverb, sizeof(DspReverb));

    /* Function table... */
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)
        ForgeReverb_LockForProcess;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)
        ForgeReverb_UnlockForProcess;
    result->base.base.is_input_format_supported = (ForgeEffectIsInputFormatSupportedFunc)
        ForgeReverb_IsInputFormatSupported;
    result->base.base.is_output_format_supported = (ForgeEffectIsOutputFormatSupportedFunc)
        ForgeReverb_IsOutputFormatSupported;
    result->base.base.initialize = (ForgeEffectInitializeFunc)
        ForgeReverb_Initialize;
    result->base.base.reset = (ForgeEffectResetFunc) ForgeReverb_Reset;
    result->base.base.process = (ForgeEffectProcessFunc) ForgeReverb_Process;
    result->base.destructor = ForgeReverb_Free;

    /* Prepare the default parameters */
    result->base.base.initialize(
        result,
        &fxdefault,
        sizeof(ForgeReverbParameters)
    );

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}

void forge_reverb_convert_i3dl2(
    const ForgeReverbI3DL2Parameters *i3dl2,
    ForgeReverbParameters *native
) {
    float reflectionsDelay;
    float reverbDelay;

    native->rear_delay = FORGE_REVERB_DEFAULT_REAR_DELAY;
    native->position_left = FORGE_REVERB_DEFAULT_POSITION;
    native->position_right = FORGE_REVERB_DEFAULT_POSITION;
    native->position_matrix_left = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    native->position_matrix_right = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    native->room_size = FORGE_REVERB_DEFAULT_ROOM_SIZE;
    native->low_eq_cutoff = 4;
    native->high_eq_cutoff = 6;

    native->room_filter_main = (float) i3dl2->room / 100.0f;
    native->room_filter_hf = (float) i3dl2->room_hf / 100.0f;

    if (i3dl2->decay_hf_ratio >= 1.0f)
    {
        int32_t index = (int32_t) (-4.0 * ForgeAudio_log10(i3dl2->decay_hf_ratio));
        if (index < -8)
        {
            index = -8;
        }
        native->low_eq_gain = (uint8_t) ((index < 0) ? index + 8 : 8);
        native->high_eq_gain = 8;
        native->decay_time = i3dl2->decay_time * i3dl2->decay_hf_ratio;
    }
    else
    {
        int32_t index = (int32_t) (4.0 * ForgeAudio_log10(i3dl2->decay_hf_ratio));
        if (index < -8)
        {
            index = -8;
        }
        native->low_eq_gain = 8;
        native->high_eq_gain = (uint8_t) ((index < 0) ? index + 8 : 8);
        native->decay_time = i3dl2->decay_time;
    }

    reflectionsDelay = i3dl2->reflections_delay * 1000.0f;
    if (reflectionsDelay >= FORGE_REVERB_MAX_REFLECTIONS_DELAY)
    {
        reflectionsDelay = (float) (FORGE_REVERB_MAX_REFLECTIONS_DELAY - 1);
    }
    else if (reflectionsDelay <= 1)
    {
        reflectionsDelay = 1;
    }
    native->reflections_delay = (uint32_t) reflectionsDelay;

    reverbDelay = i3dl2->reverb_delay * 1000.0f;
    if (reverbDelay >= FORGE_REVERB_MAX_REVERB_DELAY)
    {
        reverbDelay = (float) (FORGE_REVERB_MAX_REVERB_DELAY - 1);
    }
    native->reverb_delay = (uint8_t) reverbDelay;

    native->reflections_gain = i3dl2->reflections / 100.0f;
    native->reverb_gain = i3dl2->reverb / 100.0f;
    native->early_diffusion = (uint8_t) (15.0f * i3dl2->diffusion / 100.0f);
    native->late_diffusion = native->early_diffusion;
    native->density = i3dl2->density;
    native->room_filter_freq = i3dl2->hf_reference;

    native->wet_dry_mix = i3dl2->wet_dry_mix;
}

/* Public API (Version 9) */

ForgeResult forge_create_reverb_7point1(ForgeEffect** effect, uint32_t flags)
{
    return forge_create_reverb_7point1_with_allocator(
        effect,
        flags,
        ForgeAudio_malloc,
        ForgeAudio_free,
        ForgeAudio_realloc
    );
}

ForgeResult forge_create_reverb_7point1_with_allocator(
    ForgeEffect** effect,
    uint32_t flags,
    ForgeMallocFunc custom_malloc,
    ForgeFreeFunc custom_free,
    ForgeReallocFunc custom_realloc
) {
    const ForgeReverbParameters7Point1 fxdefault =
    {
        FORGE_REVERB_DEFAULT_WET_DRY_MIX,
        FORGE_REVERB_DEFAULT_REFLECTIONS_DELAY,
        FORGE_REVERB_DEFAULT_REVERB_DELAY,
        FORGE_REVERB_DEFAULT_REAR_DELAY, /* FIXME: 7POINT1? */
        FORGE_REVERB_DEFAULT_7_1_SIDE_DELAY,
        FORGE_REVERB_DEFAULT_POSITION,
        FORGE_REVERB_DEFAULT_POSITION,
        FORGE_REVERB_DEFAULT_POSITION_MATRIX,
        FORGE_REVERB_DEFAULT_POSITION_MATRIX,
        FORGE_REVERB_DEFAULT_EARLY_DIFFUSION,
        FORGE_REVERB_DEFAULT_LATE_DIFFUSION,
        FORGE_REVERB_DEFAULT_LOW_EQ_GAIN,
        FORGE_REVERB_DEFAULT_LOW_EQ_CUTOFF,
        FORGE_REVERB_DEFAULT_HIGH_EQ_GAIN,
        FORGE_REVERB_DEFAULT_HIGH_EQ_CUTOFF,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_FREQ,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_MAIN,
        FORGE_REVERB_DEFAULT_ROOM_FILTER_HF,
        FORGE_REVERB_DEFAULT_REFLECTIONS_GAIN,
        FORGE_REVERB_DEFAULT_REVERB_GAIN,
        FORGE_REVERB_DEFAULT_DECAY_TIME,
        FORGE_REVERB_DEFAULT_DENSITY,
        FORGE_REVERB_DEFAULT_ROOM_SIZE
    };

    /* Allocate... */
    ForgeReverb *result = (ForgeReverb*) custom_malloc(sizeof(ForgeReverb));
    uint8_t *params = (uint8_t*) custom_malloc(
        sizeof(ForgeReverbParameters7Point1) * 3
    );
    result->apiVersion = 9;

    forge_effect_base_init_with_allocator(
        &result->base,
        &ReverbInfo,
        params,
        sizeof(ForgeReverbParameters7Point1),
        0,
        custom_malloc,
        custom_free,
        custom_realloc
    );

    result->inChannels = 0;
    result->outChannels = 0;
    result->sampleRate = 0;
    ForgeAudio_zero(&result->reverb, sizeof(DspReverb));

    /* Function table... */
    result->base.base.lock_for_process = (ForgeEffectLockForProcessFunc)
        ForgeReverb_LockForProcess;
    result->base.base.unlock_for_process = (ForgeEffectUnlockForProcessFunc)
        ForgeReverb_UnlockForProcess;
    result->base.base.is_input_format_supported = (ForgeEffectIsInputFormatSupportedFunc)
        ForgeReverb_IsInputFormatSupported;
    result->base.base.is_output_format_supported = (ForgeEffectIsOutputFormatSupportedFunc)
        ForgeReverb_IsOutputFormatSupported;
    result->base.base.initialize = (ForgeEffectInitializeFunc)
        ForgeReverb_Initialize;
    result->base.base.reset = (ForgeEffectResetFunc) ForgeReverb_Reset;
    result->base.base.process = (ForgeEffectProcessFunc) ForgeReverb_Process;
    result->base.destructor = ForgeReverb_Free;

    /* Prepare the default parameters */
    result->base.base.initialize(
        result,
        &fxdefault,
        sizeof(ForgeReverbParameters7Point1)
    );

    /* Finally. */
    *effect = &result->base.base;
    return 0;
}

void forge_reverb_convert_i3dl2_7point1(
    const ForgeReverbI3DL2Parameters *i3dl2,
    ForgeReverbParameters7Point1 *native,
    int32_t seven_point_one_reverb
) {
    float reflectionsDelay;
    float reverbDelay;

    if (seven_point_one_reverb)
    {
        native->rear_delay = FORGE_REVERB_DEFAULT_7_1_REAR_DELAY;
    }
    else
    {
        native->rear_delay = FORGE_REVERB_DEFAULT_REAR_DELAY;
    }
    native->side_delay = FORGE_REVERB_DEFAULT_7_1_SIDE_DELAY;
    native->position_left = FORGE_REVERB_DEFAULT_POSITION;
    native->position_right = FORGE_REVERB_DEFAULT_POSITION;
    native->position_matrix_left = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    native->position_matrix_right = FORGE_REVERB_DEFAULT_POSITION_MATRIX;
    native->room_size = FORGE_REVERB_DEFAULT_ROOM_SIZE;
    native->low_eq_cutoff = 4;
    native->high_eq_cutoff = 6;

    native->room_filter_main = (float) i3dl2->room / 100.0f;
    native->room_filter_hf = (float) i3dl2->room_hf / 100.0f;

    if (i3dl2->decay_hf_ratio >= 1.0f)
    {
        int32_t index = (int32_t) (-4.0 * ForgeAudio_log10(i3dl2->decay_hf_ratio));
        if (index < -8)
        {
            index = -8;
        }
        native->low_eq_gain = (uint8_t) ((index < 0) ? index + 8 : 8);
        native->high_eq_gain = 8;
        native->decay_time = i3dl2->decay_time * i3dl2->decay_hf_ratio;
    }
    else
    {
        int32_t index = (int32_t) (4.0 * ForgeAudio_log10(i3dl2->decay_hf_ratio));
        if (index < -8)
        {
            index = -8;
        }
        native->low_eq_gain = 8;
        native->high_eq_gain = (uint8_t) ((index < 0) ? index + 8 : 8);
        native->decay_time = i3dl2->decay_time;
    }

    reflectionsDelay = i3dl2->reflections_delay * 1000.0f;
    if (reflectionsDelay >= FORGE_REVERB_MAX_REFLECTIONS_DELAY)
    {
        reflectionsDelay = (float) (FORGE_REVERB_MAX_REFLECTIONS_DELAY - 1);
    }
    else if (reflectionsDelay <= 1)
    {
        reflectionsDelay = 1;
    }
    native->reflections_delay = (uint32_t) reflectionsDelay;

    reverbDelay = i3dl2->reverb_delay * 1000.0f;
    if (reverbDelay >= FORGE_REVERB_MAX_REVERB_DELAY)
    {
        reverbDelay = (float) (FORGE_REVERB_MAX_REVERB_DELAY - 1);
    }
    native->reverb_delay = (uint8_t) reverbDelay;

    native->reflections_gain = i3dl2->reflections / 100.0f;
    native->reverb_gain = i3dl2->reverb / 100.0f;
    native->early_diffusion = (uint8_t) (15.0f * i3dl2->diffusion / 100.0f);
    native->late_diffusion = native->early_diffusion;
    native->density = i3dl2->density;
    native->room_filter_freq = i3dl2->hf_reference;

    native->wet_dry_mix = i3dl2->wet_dry_mix;
}
