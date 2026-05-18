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

#ifndef FORGE_SIMD_INTERNAL_H
#define FORGE_SIMD_INTERNAL_H

#include "core_internal.h"

/* Callbacks declared as functions (rather than function pointers) are
 * scalar-only, for now. SIMD versions should be possible for these.
 */

FORGE_INTERNAL_API extern void (*fa_convert_u8_to_f32)(const uint8_t *restrict src, float *restrict dst, uint32_t len);
FORGE_INTERNAL_API extern void (*fa_convert_s16_to_f32)(const int16_t *restrict src, float *restrict dst, uint32_t len);
FORGE_INTERNAL_API extern void (*fa_convert_s32_to_f32)(const int32_t *restrict src, float *restrict dst, uint32_t len);

FORGE_INTERNAL_API extern ForgeAudioResampleCallback fa_resample_mono;
FORGE_INTERNAL_API extern ForgeAudioResampleCallback fa_resample_stereo;
FORGE_INTERNAL_API extern void fa_resample_generic(float *restrict d_cache, float *restrict resample_cache,
                                                   uint64_t *resample_offset, uint64_t resample_step,
                                                   uint64_t to_resample, uint8_t channels);

FORGE_INTERNAL_API extern void (*fa_mix_amplify)(float *output, uint32_t total_samples, float volume);

FORGE_INTERNAL_API extern ForgeAudioMixCallback fa_mix_generic;

#define MIX_FUNC(name)                                                                                                 \
    FORGE_INTERNAL_API extern void fa_mix_##name##_scalar(uint32_t to_mix, uint32_t src_chans, uint32_t dst_chans,     \
                                                          float *restrict src_data, float *restrict dst_data,          \
                                                          float *restrict coefficients);
MIX_FUNC(generic)
MIX_FUNC(1in_1out)
MIX_FUNC(1in_2out)
MIX_FUNC(1in_6out)
MIX_FUNC(1in_8out)
MIX_FUNC(2in_1out)
MIX_FUNC(2in_2out)
MIX_FUNC(2in_6out)
MIX_FUNC(2in_8out)
#undef MIX_FUNC

FORGE_INTERNAL_API void fa_simd_init_functions(uint8_t has_sse2, uint8_t has_neon);

#define DECODE_FUNC(name)                                                                                              \
    FORGE_INTERNAL_API extern void fa_decode_##name(ForgeVoice *voice, const void *src, float *decode_cache,           \
                                                    uint32_t samples);
DECODE_FUNC(pcm8)
DECODE_FUNC(pcm16)
DECODE_FUNC(pcm24)
DECODE_FUNC(pcm32)
DECODE_FUNC(pcm32f)
#undef DECODE_FUNC

/* Resampling fixed-point helpers. */
#define FIXED_PRECISION 32
#define FIXED_ONE (1LL << FIXED_PRECISION)
#define FIXED_FRACTION_MASK (FIXED_ONE - 1)
#define FIXED_INTEGER_MASK ~FIXED_FRACTION_MASK
#define DOUBLE_TO_FIXED(dbl) ((uint64_t)(dbl * FIXED_ONE + 0.5))
#define FIXED_TO_DOUBLE(fxd)                                                                                           \
    ((double)(fxd >> FIXED_PRECISION) +                /* Integer part */                                              \
     ((fxd & FIXED_FRACTION_MASK) * (1.0 / FIXED_ONE)) /* Fraction part */                                             \
    )
#define FIXED_TO_FLOAT(fxd)                                                                                            \
    ((float)(fxd >> FIXED_PRECISION) +                  /* Integer part */                                             \
     ((fxd & FIXED_FRACTION_MASK) * (1.0f / FIXED_ONE)) /* Fraction part */                                            \
    )

#endif /* FORGE_SIMD_INTERNAL_H */
