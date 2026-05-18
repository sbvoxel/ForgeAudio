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

#include "core_internal.h"

#ifdef FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION
void fa_debug_log(ForgeAudioEngine *audio, const char *file, uint32_t line, const char *func, const char *fmt, ...) {
    char output[1024];
    char *out = output;
    va_list va;
    out[0] = '\0';

    /* Logging extras */
    if (audio->debug.log_thread_id) {
        out +=
            forge_snprintf(out, sizeof(output) - (out - output), "0x%" FORGE_PRIx64 " ", fa_platform_get_thread_id());
    }
    if (audio->debug.log_fileline) {
        out += forge_snprintf(out, sizeof(output) - (out - output), "%s:%u ", file, line);
    }
    if (audio->debug.log_function_name) {
        out += forge_snprintf(out, sizeof(output) - (out - output), "%s ", func);
    }
    if (audio->debug.log_timing) {
        out += forge_snprintf(out, sizeof(output) - (out - output), "%dms ", fa_platform_time_ms());
    }

    /* The actual message... */
    va_start(va, fmt);
    forge_vsnprintf(out, sizeof(output) - (out - output), fmt, va);
    va_end(va);

    /* Print, finally. */
    fa_platform_log_message(output);
}

static const char *get_wformattag_string(const ForgeAudioFormat *fmt) {
#define FMT_STRING(suffix)                                                                                             \
    if (fmt->format_tag == FORGE_AUDIO_FORMAT_##suffix) {                                                              \
        return #suffix;                                                                                                \
    }
    FMT_STRING(PCM)
    FMT_STRING(IEEE_FLOAT)
    FMT_STRING(EXTENSIBLE)
#undef FMT_STRING
    return "UNKNOWN!";
}

static const char *get_format_id_string(const ForgeAudioFormat *fmt) {
    const ForgeAudioFormatExtensible *fmtex = (const ForgeAudioFormatExtensible *)fmt;

    if (fmt->format_tag != FORGE_AUDIO_FORMAT_EXTENSIBLE) {
        return "N/A";
    }
    if (fa_format_id_equals(fmtex->format_id, fa_format_id_ieee_float)) {
        return "IEEE_FLOAT";
    }
    if (fa_format_id_equals(fmtex->format_id, fa_format_id_pcm)) {
        return "PCM";
    }
    return "UNKNOWN!";
}

void fa_debug_log_format(ForgeAudioEngine *audio, const char *file, uint32_t line, const char *func,
                         const ForgeAudioFormat *fmt) {
    fa_debug_log(audio, file, line, func,
                 ("{"
                  "format_tag: 0x%x %s, "
                  "channels: %u, "
                  "sample_rate: %u, "
                  "bits_per_sample: %u, "
                  "block_align: %u, "
                  "format_id: %s"
                  "}"),
                 fmt->format_tag, get_wformattag_string(fmt), fmt->channels, fmt->sample_rate, fmt->bits_per_sample,
                 fmt->block_align, get_format_id_string(fmt));
}
#endif /* FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION */
