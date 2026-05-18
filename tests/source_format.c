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

static int expect_simple_format_result(const char *name, uint16_t format_tag, uint16_t bits_per_sample,
                                       ForgeResult expected) {
    ForgeAudioFormat format = make_format(format_tag, bits_per_sample);
    return expect_source_format_result(name, &format, expected);
}

static int expect_extensible_format_result(const char *name, uint16_t format_tag, uint16_t bits_per_sample,
                                           ForgeResult expected) {
    ForgeAudioFormatExtensible format = make_extensible_format(format_tag, bits_per_sample);
    return expect_source_format_result(name, &format.format, expected);
}

static int test_pcm16_format_is_accepted(void) {
    return expect_simple_format_result("pcm16_simple", FORGE_AUDIO_FORMAT_PCM, 16, ForgeResultSuccess);
}

static int test_float32_format_is_accepted(void) {
    return expect_simple_format_result("float32_simple", FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32, ForgeResultSuccess);
}

static int test_float16_format_is_rejected(void) {
    return expect_simple_format_result("float16_simple", FORGE_AUDIO_FORMAT_IEEE_FLOAT, 16, ForgeResultInvalidCall);
}

static int test_extensible_pcm16_format_is_accepted(void) {
    return expect_extensible_format_result("pcm16_extensible", FORGE_AUDIO_FORMAT_PCM, 16, ForgeResultSuccess);
}

static int test_extensible_float32_format_is_accepted(void) {
    return expect_extensible_format_result("float32_extensible", FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32, ForgeResultSuccess);
}

static int test_extensible_float16_format_is_rejected(void) {
    return expect_extensible_format_result("float16_extensible", FORGE_AUDIO_FORMAT_IEEE_FLOAT, 16,
                                           ForgeResultInvalidCall);
}

static int test_additional_pcm_bit_depths(void) {
    int failed = 0;

    failed |= expect_simple_format_result("pcm8_simple", FORGE_AUDIO_FORMAT_PCM, 8, ForgeResultSuccess);
    failed |= expect_simple_format_result("pcm24_simple", FORGE_AUDIO_FORMAT_PCM, 24, ForgeResultSuccess);
    failed |= expect_simple_format_result("pcm32_simple", FORGE_AUDIO_FORMAT_PCM, 32, ForgeResultSuccess);
    failed |= expect_simple_format_result("pcm12_simple", FORGE_AUDIO_FORMAT_PCM, 12, ForgeResultInvalidCall);
    failed |= expect_simple_format_result("pcm40_simple", FORGE_AUDIO_FORMAT_PCM, 40, ForgeResultInvalidCall);

    failed |= expect_extensible_format_result("pcm8_extensible", FORGE_AUDIO_FORMAT_PCM, 8, ForgeResultSuccess);
    failed |= expect_extensible_format_result("pcm24_extensible", FORGE_AUDIO_FORMAT_PCM, 24, ForgeResultSuccess);
    failed |= expect_extensible_format_result("pcm32_extensible", FORGE_AUDIO_FORMAT_PCM, 32, ForgeResultSuccess);
    failed |= expect_extensible_format_result("pcm12_extensible", FORGE_AUDIO_FORMAT_PCM, 12, ForgeResultInvalidCall);
    failed |= expect_extensible_format_result("pcm40_extensible", FORGE_AUDIO_FORMAT_PCM, 40, ForgeResultInvalidCall);

    return failed;
}

static int test_additional_float_bit_depths(void) {
    int failed = 0;

    failed |= expect_simple_format_result("float64_simple", FORGE_AUDIO_FORMAT_IEEE_FLOAT, 64, ForgeResultInvalidCall);
    failed |= expect_extensible_format_result("float64_extensible", FORGE_AUDIO_FORMAT_IEEE_FLOAT, 64,
                                              ForgeResultInvalidCall);

    return failed;
}

int main(void) {
    int failed = 0;

    failed |= test_pcm16_format_is_accepted();
    failed |= test_float32_format_is_accepted();
    failed |= test_float16_format_is_rejected();
    failed |= test_extensible_pcm16_format_is_accepted();
    failed |= test_extensible_float32_format_is_accepted();
    failed |= test_extensible_float16_format_is_rejected();
    failed |= test_additional_pcm_bit_depths();
    failed |= test_additional_float_bit_depths();

    return failed;
}
