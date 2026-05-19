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

typedef struct TestEffect {
    ForgeEffect effect;
    ForgeResult lock_result;
    uint32_t lock_count;
    uint32_t unlock_count;
    uint32_t destroy_count;
} TestEffect;

static int32_t tracked_allocations;

static void *FORGE_AUDIO_CALL tracking_malloc(size_t size) {
    void *ptr = forge_malloc(size);

    if (ptr != NULL) {
        tracked_allocations += 1;
    }

    return ptr;
}

static void FORGE_AUDIO_CALL tracking_free(void *ptr) {
    if (ptr != NULL) {
        tracked_allocations -= 1;
    }
    forge_free(ptr);
}

static void *FORGE_AUDIO_CALL tracking_realloc(void *ptr, size_t size) {
    void *result = forge_realloc(ptr, size);

    if (ptr == NULL && result != NULL) {
        tracked_allocations += 1;
    }

    return result;
}

static void use_tracking_allocator(ForgeAudioEngine *audio) {
    tracked_allocations = 0;
    audio->malloc_func = tracking_malloc;
    audio->free_func = tracking_free;
    audio->realloc_func = tracking_realloc;
}

static void use_default_allocator(ForgeAudioEngine *audio) {
    audio->malloc_func = forge_malloc;
    audio->free_func = forge_free;
    audio->realloc_func = forge_realloc;
}

static int expect_no_tracked_allocations(const char *name) {
    if (tracked_allocations != 0) {
        fprintf(stderr, "%s: %d tracked allocations remain\n", name, tracked_allocations);
        return 1;
    }

    return 0;
}

typedef struct FailingAllocator {
    uint32_t fail_on;
    uint32_t allocation_index;
    uint32_t allocation_attempts;
    int32_t active_allocations;
    ForgeAudioEngine *audio;
    ForgeMallocFunc saved_malloc;
    ForgeFreeFunc saved_free;
    ForgeReallocFunc saved_realloc;
} FailingAllocator;

static FailingAllocator *active_failing_allocator;

static int failing_allocator_should_fail(void) {
    FailingAllocator *state = active_failing_allocator;

    state->allocation_index += 1;
    state->allocation_attempts += 1;
    return state->fail_on != 0 && state->allocation_index == state->fail_on;
}

static void *FORGE_AUDIO_CALL failing_malloc(size_t size) {
    void *ptr;

    if (failing_allocator_should_fail()) {
        return NULL;
    }

    ptr = forge_malloc(size);
    if (ptr != NULL) {
        active_failing_allocator->active_allocations += 1;
    }
    return ptr;
}

static void FORGE_AUDIO_CALL failing_free(void *ptr) {
    if (ptr != NULL) {
        active_failing_allocator->active_allocations -= 1;
    }
    forge_free(ptr);
}

static void *FORGE_AUDIO_CALL failing_realloc(void *ptr, size_t size) {
    void *result;

    if (failing_allocator_should_fail()) {
        return NULL;
    }

    result = forge_realloc(ptr, size);
    if (ptr == NULL && result != NULL) {
        active_failing_allocator->active_allocations += 1;
    } else if (ptr != NULL && size == 0 && result == NULL) {
        active_failing_allocator->active_allocations -= 1;
    }
    return result;
}

static void use_failing_allocator(ForgeAudioEngine *audio, FailingAllocator *state, uint32_t fail_on) {
    forge_zero(state, sizeof(*state));
    state->fail_on = fail_on;
    state->audio = audio;
    state->saved_malloc = audio->malloc_func;
    state->saved_free = audio->free_func;
    state->saved_realloc = audio->realloc_func;
    active_failing_allocator = state;
    audio->malloc_func = failing_malloc;
    audio->free_func = failing_free;
    audio->realloc_func = failing_realloc;
}

static void restore_failing_allocator(FailingAllocator *state) {
    state->audio->malloc_func = state->saved_malloc;
    state->audio->free_func = state->saved_free;
    state->audio->realloc_func = state->saved_realloc;
    active_failing_allocator = NULL;
}

static int expect_no_failing_allocations(const char *name, const FailingAllocator *state) {
    if (state->active_allocations != 0) {
        fprintf(stderr, "%s: %d tracked allocations remain after %u attempts\n", name, state->active_allocations,
                state->allocation_attempts);
        return 1;
    }

    return 0;
}

static void FORGE_AUDIO_CALL test_effect_destroy(void *effect_ptr) {
    TestEffect *effect = (TestEffect *)effect_ptr;

    effect->destroy_count += 1;
}

static void FORGE_AUDIO_CALL test_effect_get_info(void *effect_ptr, ForgeEffectInfo *effect_info) {
    (void)effect_ptr;

    effect_info->flags = FORGE_EFFECT_FLAG_IN_PLACE_SUPPORTED;
    effect_info->min_input_buffer_count = 1;
    effect_info->max_input_buffer_count = 1;
    effect_info->min_output_buffer_count = 1;
    effect_info->max_output_buffer_count = 1;
}

static ForgeResult FORGE_AUDIO_CALL test_effect_lock_for_process(
    void *effect_ptr, uint32_t input_locked_parameter_count, const ForgeEffectLockBuffer *input_locked_parameters,
    uint32_t output_locked_parameter_count, const ForgeEffectLockBuffer *output_locked_parameters) {
    TestEffect *effect = (TestEffect *)effect_ptr;

    (void)input_locked_parameter_count;
    (void)input_locked_parameters;
    (void)output_locked_parameter_count;
    (void)output_locked_parameters;

    effect->lock_count += 1;
    return effect->lock_result;
}

static void FORGE_AUDIO_CALL test_effect_unlock_for_process(void *effect_ptr) {
    TestEffect *effect = (TestEffect *)effect_ptr;

    effect->unlock_count += 1;
}

static uint32_t FORGE_AUDIO_CALL test_effect_calc_frames(void *effect_ptr, uint32_t frame_count) {
    (void)effect_ptr;

    return frame_count;
}

static void init_test_effect(TestEffect *effect, ForgeResult lock_result) {
    forge_zero(effect, sizeof(*effect));
    effect->lock_result = lock_result;
    effect->effect.destroy = test_effect_destroy;
    effect->effect.get_info = test_effect_get_info;
    effect->effect.lock_for_process = test_effect_lock_for_process;
    effect->effect.unlock_for_process = test_effect_unlock_for_process;
    effect->effect.calc_input_frames = test_effect_calc_frames;
    effect->effect.calc_output_frames = test_effect_calc_frames;
}

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

enum {
    test_format_wmaudio2 = 0x0161,
    test_format_wmaudio3 = 0x0162,
    test_format_wmaudio_lossless = 0x0163,
    test_format_xmaudio2 = 0x0166
};

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

static int expect_buffer_submit_result(const char *name, const ForgeBuffer *buffer, ForgeResult expected) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
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

    result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (result != ForgeResultSuccess) {
        fprintf(stderr, "%s: source voice creation failed: %d\n", name, result);
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    result = forge_source_voice_submit_buffer(voice, buffer);
    if (result != expected) {
        fprintf(stderr, "%s: got %d, expected %d\n", name, result, expected);
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }
    if (expected == ForgeResultSuccess) {
        if (voice->src.queued_buffer_count != 1) {
            fprintf(stderr, "%s: expected one queued buffer, got %zu\n", name, voice->src.queued_buffer_count);
            forge_audio_test_destroy_offline_engine(audio);
            return 1;
        }
    } else if (voice->src.queued_buffer_count != 0) {
        fprintf(stderr, "%s: invalid submit queued %zu buffers\n", name, voice->src.queued_buffer_count);
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    forge_audio_test_destroy_offline_engine(audio);

    return 0;
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

static int test_compressed_formats_are_unsupported(void) {
    int failed = 0;

    failed |=
        expect_simple_format_result("wma2_simple_12bit", test_format_wmaudio2, 12, ForgeResultUnsupportedFormat);
    failed |=
        expect_simple_format_result("wma3_simple_12bit", test_format_wmaudio3, 12, ForgeResultUnsupportedFormat);
    failed |= expect_simple_format_result("wma_lossless_simple_12bit", test_format_wmaudio_lossless, 12,
                                          ForgeResultUnsupportedFormat);
    failed |=
        expect_simple_format_result("xma2_simple_12bit", test_format_xmaudio2, 12, ForgeResultUnsupportedFormat);

    failed |= expect_extensible_format_result("wma2_extensible_12bit", test_format_wmaudio2, 12,
                                              ForgeResultUnsupportedFormat);
    failed |= expect_extensible_format_result("wma3_extensible_12bit", test_format_wmaudio3, 12,
                                              ForgeResultUnsupportedFormat);
    failed |= expect_extensible_format_result("wma_lossless_extensible_12bit", test_format_wmaudio_lossless, 12,
                                              ForgeResultUnsupportedFormat);
    failed |= expect_extensible_format_result("xma2_extensible_12bit", test_format_xmaudio2, 12,
                                              ForgeResultUnsupportedFormat);

    return failed;
}

static int test_source_buffer_submit_validation(void) {
    static const float samples[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    ForgeBuffer buffer;
    int failed = 0;

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    failed |= expect_buffer_submit_result("buffer_submit_valid", &buffer, ForgeResultSuccess);

    failed |= expect_buffer_submit_result("buffer_submit_null", NULL, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples) - 1;
    failed |= expect_buffer_submit_result("buffer_submit_unaligned_bytes", &buffer, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    buffer.play_begin = 3;
    buffer.play_length = 2;
    failed |= expect_buffer_submit_result("buffer_submit_play_past_end", &buffer, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    buffer.play_begin = UINT32_MAX;
    buffer.play_length = 2;
    failed |= expect_buffer_submit_result("buffer_submit_play_overflow", &buffer, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    buffer.loop_begin = 1;
    failed |= expect_buffer_submit_result("buffer_submit_loop_without_loop_count", &buffer, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    buffer.play_begin = 1;
    buffer.play_length = 2;
    buffer.loop_begin = 3;
    buffer.loop_count = 1;
    failed |= expect_buffer_submit_result("buffer_submit_loop_begin_at_end", &buffer, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    buffer.play_begin = 1;
    buffer.play_length = 2;
    buffer.loop_begin = 0;
    buffer.loop_length = 1;
    buffer.loop_count = 1;
    failed |= expect_buffer_submit_result("buffer_submit_loop_before_play", &buffer, ForgeResultInvalidCall);

    forge_zero(&buffer, sizeof(buffer));
    buffer.audio_data = (const uint8_t *)samples;
    buffer.audio_bytes = sizeof(samples);
    buffer.play_begin = 1;
    buffer.play_length = 2;
    buffer.loop_begin = 1;
    buffer.loop_length = 3;
    buffer.loop_count = 1;
    failed |= expect_buffer_submit_result("buffer_submit_loop_past_play", &buffer, ForgeResultInvalidCall);

    return failed;
}

static int test_unknown_formats_are_rejected(void) {
    enum {
        unknown_format_tag = 0x1234
    };
    int failed = 0;

    failed |= expect_simple_format_result("unknown_simple", unknown_format_tag, 32, ForgeResultInvalidCall);
    failed |= expect_extensible_format_result("unknown_extensible", unknown_format_tag, 32, ForgeResultInvalidCall);

    return failed;
}

static int test_source_create_effect_failure_cleans_allocations(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    TestEffect effects[2];
    ForgeEffectDesc effect_descs[2];
    ForgeEffectChain effect_chain;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    init_test_effect(&effects[0], ForgeResultSuccess);
    init_test_effect(&effects[1], ForgeResultInvalidCall);
    effect_descs[0].effect = &effects[0].effect;
    effect_descs[0].initial_state = 1;
    effect_descs[0].output_channels = 1;
    effect_descs[1].effect = &effects[1].effect;
    effect_descs[1].initial_state = 1;
    effect_descs[1].output_channels = 1;
    effect_chain.effect_count = 2;
    effect_chain.effects = effect_descs;

    use_tracking_allocator(audio);
    result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL,
                                             &effect_chain);
    failed |= expect_no_tracked_allocations("source_create_effect_failure_cleanup");
    use_default_allocator(audio);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "source_create_effect_failure_cleanup: got %d, expected %d\n", result, ForgeResultInvalidCall);
        failed = 1;
    }
    if (voice != NULL) {
        fprintf(stderr, "source_create_effect_failure_cleanup: failed creation returned a voice\n");
        failed = 1;
    }
    if (effects[0].lock_count != 1 || effects[0].unlock_count != 1 || effects[0].destroy_count != 0 ||
        effects[1].lock_count != 1 || effects[1].unlock_count != 0 || effects[1].destroy_count != 0) {
        fprintf(stderr, "source_create_effect_failure_cleanup: unexpected effect ownership/lock state\n");
        failed = 1;
    }

    return failed;
}

static int test_source_create_invalid_outputs_cleans_allocations(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSubmixVoice *submix_a = NULL;
    ForgeSubmixVoice *submix_b = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }
    if (forge_audio_create_submix_voice(audio, &submix_a, 1, 48000, 0, 0, NULL, NULL) != ForgeResultSuccess ||
        forge_audio_create_submix_voice(audio, &submix_b, 1, 44100, 0, 0, NULL, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "source_create_invalid_outputs_cleanup: submix creation failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    sends[0].flags = 0;
    sends[0].output_voice = submix_a;
    sends[1].flags = 0;
    sends[1].output_voice = submix_b;
    send_list.send_count = 2;
    send_list.sends = sends;

    use_tracking_allocator(audio);
    result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list,
                                             NULL);
    failed |= expect_no_tracked_allocations("source_create_invalid_outputs_cleanup");
    use_default_allocator(audio);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "source_create_invalid_outputs_cleanup: got %d, expected %d\n", result, ForgeResultInvalidCall);
        failed = 1;
    }
    if (voice != NULL) {
        fprintf(stderr, "source_create_invalid_outputs_cleanup: failed creation returned a voice\n");
        failed = 1;
    }

    return failed;
}

static int test_source_create_empty_outputs_without_master_fails_before_allocation(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    ForgeAudioEngine *audio = NULL;
    ForgeSourceVoice *voice = NULL;
    ForgeSendList send_list = {0, NULL};
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    use_tracking_allocator(audio);
    result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, &send_list,
                                             NULL);
    failed |= expect_no_tracked_allocations("source_create_empty_outputs_without_master");
    use_default_allocator(audio);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "source_create_empty_outputs_without_master: got %d, expected %d\n", result,
                ForgeResultInvalidCall);
        failed = 1;
    }
    if (voice != NULL) {
        fprintf(stderr, "source_create_empty_outputs_without_master: failed creation returned a voice\n");
        failed = 1;
    }

    return failed;
}

static int test_submix_create_effect_failure_cleans_allocations(void) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSubmixVoice *submix = NULL;
    TestEffect effects[2];
    ForgeEffectDesc effect_descs[2];
    ForgeEffectChain effect_chain;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    init_test_effect(&effects[0], ForgeResultSuccess);
    init_test_effect(&effects[1], ForgeResultInvalidCall);
    effect_descs[0].effect = &effects[0].effect;
    effect_descs[0].initial_state = 1;
    effect_descs[0].output_channels = 1;
    effect_descs[1].effect = &effects[1].effect;
    effect_descs[1].initial_state = 1;
    effect_descs[1].output_channels = 1;
    effect_chain.effect_count = 2;
    effect_chain.effects = effect_descs;

    use_tracking_allocator(audio);
    result = forge_audio_create_submix_voice(audio, &submix, 1, 48000, 0, 0, NULL, &effect_chain);
    failed |= expect_no_tracked_allocations("submix_create_effect_failure_cleanup");
    use_default_allocator(audio);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "submix_create_effect_failure_cleanup: got %d, expected %d\n", result,
                ForgeResultInvalidCall);
        failed = 1;
    }
    if (submix != NULL) {
        fprintf(stderr, "submix_create_effect_failure_cleanup: failed creation returned a voice\n");
        failed = 1;
    }
    if (effects[0].lock_count != 1 || effects[0].unlock_count != 1 || effects[0].destroy_count != 0 ||
        effects[1].lock_count != 1 || effects[1].unlock_count != 0 || effects[1].destroy_count != 0) {
        fprintf(stderr, "submix_create_effect_failure_cleanup: unexpected effect ownership/lock state\n");
        failed = 1;
    }

    return failed;
}

static int test_submix_create_invalid_outputs_cleans_allocations(void) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSubmixVoice *submix_a = NULL;
    ForgeSubmixVoice *submix_b = NULL;
    ForgeSubmixVoice *submix = NULL;
    ForgeSend sends[2];
    ForgeSendList send_list;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_virtual_master_voice failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }
    if (forge_audio_create_submix_voice(audio, &submix_a, 1, 48000, 0, 0, NULL, NULL) != ForgeResultSuccess ||
        forge_audio_create_submix_voice(audio, &submix_b, 1, 44100, 0, 0, NULL, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "submix_create_invalid_outputs_cleanup: setup submix creation failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    sends[0].flags = 0;
    sends[0].output_voice = submix_a;
    sends[1].flags = 0;
    sends[1].output_voice = submix_b;
    send_list.send_count = 2;
    send_list.sends = sends;

    use_tracking_allocator(audio);
    result = forge_audio_create_submix_voice(audio, &submix, 1, 48000, 0, 0, &send_list, NULL);
    failed |= expect_no_tracked_allocations("submix_create_invalid_outputs_cleanup");
    use_default_allocator(audio);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "submix_create_invalid_outputs_cleanup: got %d, expected %d\n", result,
                ForgeResultInvalidCall);
        failed = 1;
    }
    if (submix != NULL) {
        fprintf(stderr, "submix_create_invalid_outputs_cleanup: failed creation returned a voice\n");
        failed = 1;
    }

    return failed;
}

static int test_virtual_master_create_effect_failure_cleans_allocations(void) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    TestEffect effects[2];
    ForgeEffectDesc effect_descs[2];
    ForgeEffectChain effect_chain;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    init_test_effect(&effects[0], ForgeResultSuccess);
    init_test_effect(&effects[1], ForgeResultInvalidCall);
    effect_descs[0].effect = &effects[0].effect;
    effect_descs[0].initial_state = 1;
    effect_descs[0].output_channels = 1;
    effect_descs[1].effect = &effects[1].effect;
    effect_descs[1].initial_state = 1;
    effect_descs[1].output_channels = 1;
    effect_chain.effect_count = 2;
    effect_chain.effects = effect_descs;

    use_tracking_allocator(audio);
    result = forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, &effect_chain);
    failed |= expect_no_tracked_allocations("virtual_master_create_effect_failure_cleanup");
    use_default_allocator(audio);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "virtual_master_create_effect_failure_cleanup: got %d, expected %d\n", result,
                ForgeResultInvalidCall);
        failed = 1;
    }
    if (master != NULL) {
        fprintf(stderr, "virtual_master_create_effect_failure_cleanup: failed creation returned a voice\n");
        failed = 1;
    }
    if (effects[0].lock_count != 1 || effects[0].unlock_count != 1 || effects[0].destroy_count != 0 ||
        effects[1].lock_count != 1 || effects[1].unlock_count != 0 || effects[1].destroy_count != 0) {
        fprintf(stderr, "virtual_master_create_effect_failure_cleanup: unexpected effect ownership/lock state\n");
        failed = 1;
    }

    return failed;
}

static int test_offline_engine_rejects_real_master_voice(void) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "offline_real_master: forge_audio_test_create_offline_engine failed\n");
        return 1;
    }

    result = forge_audio_create_master_voice(audio, &master, 1, 48000, 0, 0, NULL);
    forge_audio_test_destroy_offline_engine(audio);

    if (result != ForgeResultInvalidCall) {
        fprintf(stderr, "offline_real_master: got %d, expected %d\n", result, ForgeResultInvalidCall);
        failed = 1;
    }
    if (master != NULL) {
        fprintf(stderr, "offline_real_master: failed creation returned a voice\n");
        failed = 1;
    }

    return failed;
}

typedef enum AllocationFailureTarget {
    AllocationFailureTargetSourceDefault,
    AllocationFailureTargetSourceEffectChain,
    AllocationFailureTargetSourceSendList,
    AllocationFailureTargetSubmixDefault,
    AllocationFailureTargetSubmixEffectChain,
    AllocationFailureTargetVirtualMasterEffectChain
} AllocationFailureTarget;

static void make_success_effect_chain(TestEffect effects[2], ForgeEffectDesc effect_descs[2],
                                      ForgeEffectChain *effect_chain) {
    init_test_effect(&effects[0], ForgeResultSuccess);
    init_test_effect(&effects[1], ForgeResultSuccess);
    effect_descs[0].effect = &effects[0].effect;
    effect_descs[0].initial_state = 1;
    effect_descs[0].output_channels = 1;
    effect_descs[1].effect = &effects[1].effect;
    effect_descs[1].initial_state = 1;
    effect_descs[1].output_channels = 1;
    effect_chain->effect_count = 2;
    effect_chain->effects = effect_descs;
}

static int expect_failed_effect_chain_released(const char *name, uint32_t fail_on, const TestEffect effects[2]) {
    for (uint32_t i = 0; i < 2; i += 1) {
        if (effects[i].lock_count != effects[i].unlock_count || effects[i].destroy_count != 0) {
            fprintf(stderr,
                    "%s fail %u: effect %u lock/unlock/destroy mismatch (%u/%u/%u)\n",
                    name, fail_on, i, effects[i].lock_count, effects[i].unlock_count, effects[i].destroy_count);
            return 1;
        }
    }

    return 0;
}

static int run_lifecycle_allocation_failure_target(const char *name, AllocationFailureTarget target) {
    enum {
        hard_fail_cap = 64
    };
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    int uses_effect_chain = target == AllocationFailureTargetSourceEffectChain ||
                            target == AllocationFailureTargetSubmixEffectChain ||
                            target == AllocationFailureTargetVirtualMasterEffectChain;

    for (uint32_t fail_on = 1; fail_on <= hard_fail_cap; fail_on += 1) {
        ForgeAudioEngine *audio = NULL;
        ForgeMasterVoice *master = NULL;
        ForgeSubmixVoice *submix_a = NULL;
        ForgeSubmixVoice *submix_b = NULL;
        ForgeVoice *created_voice = NULL;
        TestEffect effects[2];
        ForgeEffectDesc effect_descs[2];
        ForgeEffectChain effect_chain;
        const ForgeEffectChain *effect_chain_ptr = NULL;
        ForgeSend sends[2];
        ForgeSendList send_list;
        const ForgeSendList *send_list_ptr = NULL;
        FailingAllocator allocator;
        ForgeResult result;
        int failed = 0;

        forge_zero(effects, sizeof(effects));
        forge_zero(effect_descs, sizeof(effect_descs));
        forge_zero(&effect_chain, sizeof(effect_chain));
        forge_zero(sends, sizeof(sends));
        forge_zero(&send_list, sizeof(send_list));

        if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
            fprintf(stderr, "%s fail %u: forge_audio_test_create_offline_engine failed\n", name, fail_on);
            return 1;
        }

        if (target != AllocationFailureTargetVirtualMasterEffectChain) {
            if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) !=
                ForgeResultSuccess) {
                fprintf(stderr, "%s fail %u: forge_audio_test_create_virtual_master_voice failed\n", name, fail_on);
                forge_audio_test_destroy_offline_engine(audio);
                return 1;
            }
        }

        if (target == AllocationFailureTargetSourceSendList) {
            if (forge_audio_create_submix_voice(audio, &submix_a, 1, 48000, 0, 0, NULL, NULL) != ForgeResultSuccess ||
                forge_audio_create_submix_voice(audio, &submix_b, 1, 48000, 0, 0, NULL, NULL) != ForgeResultSuccess) {
                fprintf(stderr, "%s fail %u: send-list setup submix creation failed\n", name, fail_on);
                forge_audio_test_destroy_offline_engine(audio);
                return 1;
            }

            sends[0].flags = 0;
            sends[0].output_voice = submix_a;
            sends[1].flags = FORGE_AUDIO_SEND_USEFILTER;
            sends[1].output_voice = submix_b;
            send_list.send_count = 2;
            send_list.sends = sends;
            send_list_ptr = &send_list;
        }

        if (uses_effect_chain) {
            make_success_effect_chain(effects, effect_descs, &effect_chain);
            effect_chain_ptr = &effect_chain;
        }

        use_failing_allocator(audio, &allocator, fail_on);
        switch (target) {
            case AllocationFailureTargetSourceDefault: {
                ForgeSourceVoice *voice = NULL;
                result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                                         NULL, NULL, NULL);
                created_voice = (ForgeVoice *)voice;
                break;
            }
            case AllocationFailureTargetSourceEffectChain: {
                ForgeSourceVoice *voice = NULL;
                result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                                         NULL, NULL, effect_chain_ptr);
                created_voice = (ForgeVoice *)voice;
                break;
            }
            case AllocationFailureTargetSourceSendList: {
                ForgeSourceVoice *voice = NULL;
                result = forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                                         NULL, send_list_ptr, NULL);
                created_voice = (ForgeVoice *)voice;
                break;
            }
            case AllocationFailureTargetSubmixDefault: {
                ForgeSubmixVoice *voice = NULL;
                result = forge_audio_create_submix_voice(audio, &voice, 1, 48000, 0, 0, NULL, NULL);
                created_voice = (ForgeVoice *)voice;
                break;
            }
            case AllocationFailureTargetSubmixEffectChain: {
                ForgeSubmixVoice *voice = NULL;
                result = forge_audio_create_submix_voice(audio, &voice, 1, 48000, 0, 0, NULL, effect_chain_ptr);
                created_voice = (ForgeVoice *)voice;
                break;
            }
            case AllocationFailureTargetVirtualMasterEffectChain: {
                ForgeMasterVoice *voice = NULL;
                result = forge_audio_test_create_virtual_master_voice(audio, &voice, 1, 48000, 64, effect_chain_ptr);
                created_voice = (ForgeVoice *)voice;
                break;
            }
            default:
                result = ForgeResultInvalidCall;
                break;
        }

        if (result == ForgeResultSuccess) {
            if (created_voice == NULL) {
                fprintf(stderr, "%s fail %u: successful creation returned NULL\n", name, fail_on);
                failed = 1;
            } else {
                forge_voice_destroy(created_voice);
            }
            failed |= expect_no_failing_allocations(name, &allocator);
            restore_failing_allocator(&allocator);
            forge_audio_test_destroy_offline_engine(audio);
            return failed;
        }

        if (created_voice != NULL) {
            fprintf(stderr, "%s fail %u: failed creation returned a voice\n", name, fail_on);
            failed = 1;
        }
        failed |= expect_no_failing_allocations(name, &allocator);
        if (uses_effect_chain) {
            failed |= expect_failed_effect_chain_released(name, fail_on, effects);
        }
        restore_failing_allocator(&allocator);
        forge_audio_test_destroy_offline_engine(audio);

        if (failed) {
            return failed;
        }
    }

    fprintf(stderr, "%s: creation never succeeded within %u allocation failures\n", name, hard_fail_cap);
    return 1;
}

static int test_lifecycle_allocation_failure_cleanup_paths(void) {
    int failed = 0;

    failed |= run_lifecycle_allocation_failure_target("alloc_fail_source_default",
                                                      AllocationFailureTargetSourceDefault);
    failed |= run_lifecycle_allocation_failure_target("alloc_fail_source_effect_chain",
                                                      AllocationFailureTargetSourceEffectChain);
    failed |= run_lifecycle_allocation_failure_target("alloc_fail_source_send_list",
                                                      AllocationFailureTargetSourceSendList);
    failed |= run_lifecycle_allocation_failure_target("alloc_fail_submix_default",
                                                      AllocationFailureTargetSubmixDefault);
    failed |= run_lifecycle_allocation_failure_target("alloc_fail_submix_effect_chain",
                                                      AllocationFailureTargetSubmixEffectChain);
    failed |= run_lifecycle_allocation_failure_target("alloc_fail_virtual_master_effect_chain",
                                                      AllocationFailureTargetVirtualMasterEffectChain);

    return failed;
}

static int test_source_sample_rate_resize_failure_preserves_state(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    FailingAllocator allocator;
    ForgeResult result;
    uint32_t old_sample_rate;
    uint32_t old_resample_samples;
    uint32_t old_decode_samples;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "sample_rate_resize_failure: forge_audio_test_create_offline_engine failed\n");
        return 1;
    }
    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess ||
        forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL) !=
            ForgeResultSuccess) {
        fprintf(stderr, "sample_rate_resize_failure: setup failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    old_sample_rate = voice->src.format->sample_rate;
    old_resample_samples = voice->src.resampleSamples;
    old_decode_samples = voice->src.decodeSamples;

    use_failing_allocator(audio, &allocator, 1);
    result = forge_source_voice_set_sample_rate(voice, 96000);
    failed |= expect_no_failing_allocations("sample_rate_resize_failure", &allocator);
    restore_failing_allocator(&allocator);

    if (result != ForgeResultOutOfMemory) {
        fprintf(stderr, "sample_rate_resize_failure: got %d, expected %d\n", result, ForgeResultOutOfMemory);
        failed = 1;
    }
    if (voice->src.format->sample_rate != old_sample_rate || voice->src.resampleSamples != old_resample_samples ||
        voice->src.decodeSamples != old_decode_samples) {
        fprintf(stderr, "sample_rate_resize_failure: source state changed after OOM\n");
        failed = 1;
    }

    forge_audio_test_destroy_offline_engine(audio);
    return failed;
}

static int test_effect_chain_replacement_allocation_failure_preserves_old_chain(void) {
    ForgeAudioFormat format = make_format(FORGE_AUDIO_FORMAT_IEEE_FLOAT, 32);
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSourceVoice *voice = NULL;
    TestEffect old_effect;
    TestEffect new_effect;
    ForgeEffectDesc old_desc;
    ForgeEffectDesc new_desc;
    ForgeEffectChain old_chain;
    ForgeEffectChain new_chain;
    FailingAllocator allocator;
    ForgeResult result;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "effect_chain_replacement_oom: forge_audio_test_create_offline_engine failed\n");
        return 1;
    }
    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 64, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "effect_chain_replacement_oom: master setup failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    init_test_effect(&old_effect, ForgeResultSuccess);
    old_desc.effect = &old_effect.effect;
    old_desc.initial_state = 1;
    old_desc.output_channels = 1;
    old_chain.effect_count = 1;
    old_chain.effects = &old_desc;

    if (forge_audio_create_source_voice(audio, &voice, &format, 0, FORGE_AUDIO_DEFAULT_FREQ_RATIO, NULL, NULL,
                                        &old_chain) != ForgeResultSuccess) {
        fprintf(stderr, "effect_chain_replacement_oom: source setup failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    init_test_effect(&new_effect, ForgeResultSuccess);
    new_desc.effect = &new_effect.effect;
    new_desc.initial_state = 1;
    new_desc.output_channels = 1;
    new_chain.effect_count = 1;
    new_chain.effects = &new_desc;

    use_failing_allocator(audio, &allocator, 1);
    result = forge_voice_set_effect_chain(voice, &new_chain);
    failed |= expect_no_failing_allocations("effect_chain_replacement_oom", &allocator);
    restore_failing_allocator(&allocator);

    if (result != ForgeResultOutOfMemory) {
        fprintf(stderr, "effect_chain_replacement_oom: got %d, expected %d\n", result, ForgeResultOutOfMemory);
        failed = 1;
    }
    if (old_effect.lock_count != 1 || old_effect.unlock_count != 0 || old_effect.destroy_count != 0 ||
        new_effect.lock_count != 1 || new_effect.unlock_count != 1 || new_effect.destroy_count != 0) {
        fprintf(stderr, "effect_chain_replacement_oom: unexpected effect state before destroy\n");
        failed = 1;
    }

    forge_voice_destroy(voice);
    if (old_effect.lock_count != 1 || old_effect.unlock_count != 1 || old_effect.destroy_count != 1 ||
        new_effect.destroy_count != 0) {
        fprintf(stderr, "effect_chain_replacement_oom: old chain was not preserved until destroy\n");
        failed = 1;
    }

    forge_audio_test_destroy_offline_engine(audio);
    return failed;
}

static int test_submix_set_outputs_resize_failure_preserves_state(void) {
    ForgeAudioEngine *audio = NULL;
    ForgeMasterVoice *master = NULL;
    ForgeSubmixVoice *routed = NULL;
    ForgeSubmixVoice *downstream = NULL;
    ForgeSend send;
    ForgeSendList send_list;
    FailingAllocator allocator;
    ForgeResult result;
    uint32_t old_output_samples;
    uint64_t old_resample_step;
    uint32_t old_input_frames;
    uint32_t old_input_samples;
    int failed = 0;

    if (forge_audio_test_create_offline_engine(&audio) != ForgeResultSuccess) {
        fprintf(stderr, "submix_set_outputs_resize_failure: forge_audio_test_create_offline_engine failed\n");
        return 1;
    }
    if (forge_audio_test_create_virtual_master_voice(audio, &master, 1, 48000, 4, NULL) != ForgeResultSuccess ||
        forge_audio_create_submix_voice(audio, &routed, 1, 48000, 0, 0, NULL, NULL) != ForgeResultSuccess ||
        forge_audio_create_submix_voice(audio, &downstream, 1, 3000, 0, 1, NULL, NULL) != ForgeResultSuccess) {
        fprintf(stderr, "submix_set_outputs_resize_failure: setup failed\n");
        forge_audio_test_destroy_offline_engine(audio);
        return 1;
    }

    old_output_samples = routed->mix.outputSamples;
    old_resample_step = routed->mix.resampleStep;
    old_input_frames = routed->mix.inputFrames;
    old_input_samples = routed->mix.inputSamples;

    send.flags = 0;
    send.output_voice = downstream;
    send_list.send_count = 1;
    send_list.sends = &send;

    use_failing_allocator(audio, &allocator, 1);
    result = forge_voice_set_outputs(routed, &send_list);
    failed |= expect_no_failing_allocations("submix_set_outputs_resize_failure", &allocator);
    restore_failing_allocator(&allocator);

    if (result != ForgeResultOutOfMemory) {
        fprintf(stderr, "submix_set_outputs_resize_failure: got %d, expected %d\n", result,
                ForgeResultOutOfMemory);
        failed = 1;
    }
    if (routed->mix.outputSamples != old_output_samples || routed->mix.resampleStep != old_resample_step ||
        routed->mix.inputFrames != old_input_frames || routed->mix.inputSamples != old_input_samples) {
        fprintf(stderr, "submix_set_outputs_resize_failure: submix state changed after OOM\n");
        failed = 1;
    }

    forge_audio_test_destroy_offline_engine(audio);
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
    failed |= test_compressed_formats_are_unsupported();
    failed |= test_source_buffer_submit_validation();
    failed |= test_unknown_formats_are_rejected();
    failed |= test_source_create_effect_failure_cleans_allocations();
    failed |= test_source_create_invalid_outputs_cleans_allocations();
    failed |= test_source_create_empty_outputs_without_master_fails_before_allocation();
    failed |= test_submix_create_effect_failure_cleans_allocations();
    failed |= test_submix_create_invalid_outputs_cleans_allocations();
    failed |= test_virtual_master_create_effect_failure_cleans_allocations();
    failed |= test_offline_engine_rejects_real_master_voice();
    failed |= test_lifecycle_allocation_failure_cleanup_paths();
    failed |= test_source_sample_rate_resize_failure_preserves_state();
    failed |= test_effect_chain_replacement_allocation_failure_preserves_old_chain();
    failed |= test_submix_set_outputs_resize_failure_preserves_state();

    return failed;
}
