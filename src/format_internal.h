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

#ifndef FORGE_FORMAT_INTERNAL_H
#define FORGE_FORMAT_INTERNAL_H

#include "common_internal.h"

#define FORGE_AUDIO_FORMAT_ID_SIZE 16
#define FA_AUDIO_FORMAT_WMAUDIO2 0x0161
#define FA_AUDIO_FORMAT_WMAUDIO3 0x0162
#define FA_AUDIO_FORMAT_WMAUDIO_LOSSLESS 0x0163
#define FA_AUDIO_FORMAT_XMAUDIO2 0x0166

FORGE_INTERNAL_API extern const uint8_t fa_format_id_pcm[FORGE_AUDIO_FORMAT_ID_SIZE];
FORGE_INTERNAL_API extern const uint8_t fa_format_id_ieee_float[FORGE_AUDIO_FORMAT_ID_SIZE];
FORGE_INTERNAL_API extern const uint8_t fa_format_id_xmaudio2[FORGE_AUDIO_FORMAT_ID_SIZE];
FORGE_INTERNAL_API extern const uint8_t fa_format_id_wmaudio2[FORGE_AUDIO_FORMAT_ID_SIZE];
FORGE_INTERNAL_API extern const uint8_t fa_format_id_wmaudio3[FORGE_AUDIO_FORMAT_ID_SIZE];
FORGE_INTERNAL_API extern const uint8_t fa_format_id_wmaudio_lossless[FORGE_AUDIO_FORMAT_ID_SIZE];

static inline bool fa_format_id_equals(const uint8_t format_id[FORGE_AUDIO_FORMAT_ID_SIZE],
                                       const uint8_t expected[FORGE_AUDIO_FORMAT_ID_SIZE]) {
    return forge_memcmp(format_id, expected, FORGE_AUDIO_FORMAT_ID_SIZE) == 0;
}

static inline uint16_t fa_format_id_tag(const uint8_t format_id[FORGE_AUDIO_FORMAT_ID_SIZE]) {
    return (uint16_t)(format_id[0] | (format_id[1] << 8));
}

static inline uint32_t fa_format_channel_mask_for_channels(uint16_t channels) {
    if (channels == 1)
        return FORGE_SPEAKER_MONO;
    if (channels == 2)
        return FORGE_SPEAKER_STEREO;
    if (channels == 3)
        return FORGE_SPEAKER_2POINT1;
    if (channels == 4)
        return FORGE_SPEAKER_QUAD;
    if (channels == 5)
        return FORGE_SPEAKER_4POINT1;
    if (channels == 6)
        return FORGE_SPEAKER_5POINT1;
    if (channels == 8)
        return FORGE_SPEAKER_7POINT1_SURROUND;
    forge_assert(0 && "Unrecognized speaker layout!");
    return 0;
}

static inline void fa_format_write_extensible(ForgeAudioFormatExtensible *fmt, int channels, int samplerate,
                                              const uint8_t format_id[FORGE_AUDIO_FORMAT_ID_SIZE]) {
    forge_assert(fmt != NULL);
    fmt->format.bits_per_sample = 32;
    fmt->format.format_tag = FORGE_AUDIO_FORMAT_EXTENSIBLE;
    fmt->format.channels = channels;
    fmt->format.sample_rate = samplerate;
    fmt->format.block_align = (fmt->format.channels * (fmt->format.bits_per_sample / 8));
    fmt->format.average_bytes_per_second = (fmt->format.sample_rate * fmt->format.block_align);
    fmt->format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
    fmt->samples.valid_bits_per_sample = 32;
    fmt->channel_mask = fa_format_channel_mask_for_channels(fmt->format.channels);
    forge_memcpy(fmt->format_id, format_id, FORGE_AUDIO_FORMAT_ID_SIZE);
}

#endif /* FORGE_FORMAT_INTERNAL_H */
