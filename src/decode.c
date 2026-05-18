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

#include "simd_internal.h"

void fa_decode_pcm8(ForgeVoice *voice, const void *src, float *decodeCache, uint32_t samples) {
    LOG_FUNC_ENTER(voice->audio)
    fa_convert_u8_to_f32(src, decodeCache, samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

void fa_decode_pcm16(ForgeVoice *voice, const void *src, float *decodeCache, uint32_t samples) {
    LOG_FUNC_ENTER(voice->audio)
    fa_convert_s16_to_f32(src, decodeCache, samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

void fa_decode_pcm24(ForgeVoice *voice, const void *src, float *decodeCache, uint32_t samples) {
    const uint8_t *buf = src;
    LOG_FUNC_ENTER(voice->audio)

    /* Potential SIMD optimization for packed PCM24-to-float conversion. */
    for (uint32_t i = 0; i < samples; i += 1, buf += voice->src.format->block_align)
        for (uint32_t j = 0; j < voice->src.format->channels; j += 1) {
            *decodeCache++ = ((int32_t)(((uint32_t)buf[(j * 3) + 2] << 24) | ((uint32_t)buf[(j * 3) + 1] << 16) |
                                        ((uint32_t)buf[(j * 3) + 0] << 8)) >>
                              8) /
                             8388607.0f;
        }

    LOG_FUNC_EXIT(voice->audio)
}

void fa_decode_pcm32(ForgeVoice *voice, const void *src, float *decodeCache, uint32_t samples) {
    LOG_FUNC_ENTER(voice->audio)
    fa_convert_s32_to_f32(src, decodeCache, samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}

void fa_decode_pcm32f(ForgeVoice *voice, const void *src, float *decodeCache, uint32_t samples) {
    LOG_FUNC_ENTER(voice->audio)
    forge_memcpy(decodeCache, src, sizeof(float) * samples * voice->src.format->channels);
    LOG_FUNC_EXIT(voice->audio)
}
