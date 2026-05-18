/*
 * ForgeAudio
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#include "core_internal.h"

#include <stdio.h>

static ForgeAudioFormat make_format(uint16_t format_tag, uint16_t bits_per_sample) {
    ForgeAudioFormat format;

    forge_zero(&format, sizeof(format));
    format.format_tag = format_tag;
    format.channels = 1;
    format.sample_rate = 48000;
    format.bits_per_sample = bits_per_sample;
    format.block_align = bits_per_sample / 8;
    format.average_bytes_per_second = format.sample_rate * format.block_align;
    return format;
}

static void write_format_id(uint8_t format_id[16], uint16_t format_tag) {
    static const uint8_t tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                     0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

    format_id[0] = (uint8_t)(format_tag & 0xFF);
    format_id[1] = (uint8_t)((format_tag >> 8) & 0xFF);
    forge_memcpy(format_id + 2, tail, sizeof(tail));
}

static ForgeAudioFormatExtensible make_extensible_format(uint16_t format_tag, uint16_t bits_per_sample) {
    ForgeAudioFormatExtensible format;

    forge_zero(&format, sizeof(format));
    format.format = make_format(FORGE_AUDIO_FORMAT_EXTENSIBLE, bits_per_sample);
    format.format.extra_size = sizeof(format) - sizeof(format.format);
    format.samples.valid_bits_per_sample = bits_per_sample;
    format.channel_mask = FORGE_SPEAKER_MONO;
    write_format_id(format.format_id, format_tag);
    return format;
}

static int expect_source_format_result(const char *name, const ForgeAudioFormat *format, ForgeResult expected) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeResult result;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    result = forge_audio_create_source_voice(audio, &voice, format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != expected) {
        fprintf(stderr, "%s: got %d, expected %d\n", name, result, expected);
        return 1;
    }

    return 0;
}

static int test_pcm16_format_is_accepted(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_PCM, 16);
    return expect_source_format_result("pcm16_simple", &format, ForgeResultSuccess);
}

static int test_float32_format_is_accepted(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    return expect_source_format_result("float32_simple", &format, ForgeResultSuccess);
}

static int test_float16_format_is_rejected(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 16);
    return expect_source_format_result("float16_simple", &format, ForgeResultInvalidCall);
}

static int test_extensible_pcm16_format_is_accepted(void) {
    ForgeAudioFormatExtensible format = make_extensible_format(FORGE_AUDIO_FORMAT_PCM, 16);
    return expect_source_format_result("pcm16_extensible", &format.format, ForgeResultSuccess);
}

static int test_extensible_float32_format_is_accepted(void) {
    ForgeAudioFormatExtensible format = make_extensible_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    return expect_source_format_result("float32_extensible", &format.format, ForgeResultSuccess);
}

static int test_extensible_float16_format_is_rejected(void) {
    ForgeAudioFormatExtensible format = make_extensible_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 16);
    return expect_source_format_result("float16_extensible", &format.format, ForgeResultInvalidCall);
}

int main(void) {
    int failed = 0;

    failed |= test_pcm16_format_is_accepted();
    failed |= test_float32_format_is_accepted();
    failed |= test_float16_format_is_rejected();
    failed |= test_extensible_pcm16_format_is_accepted();
    failed |= test_extensible_float32_format_is_accepted();
    failed |= test_extensible_float16_format_is_rejected();

    return failed;
}
