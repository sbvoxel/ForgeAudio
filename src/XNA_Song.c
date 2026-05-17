/* ForgeAudioEngine
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

#include "FAudio_internal.h"

/* stb_vorbis */

#define malloc FAudio_malloc
#define realloc FAudio_realloc
#define free FAudio_free
#ifdef STB_MEMSET_OVERRIDE
#ifdef memset /* Thanks, Apple! */
#undef memset
#endif
#define memset FAudio_memset
#endif /* STB_MEMSET_OVERRIDE */
#ifdef STB_MEMCPY_OVERRIDE
#ifdef memcpy /* Thanks, Apple! */
#undef memcpy
#endif
#define memcpy FAudio_memcpy
#endif /* STB_MEMCPY_OVERRIDE */
#define memcmp FAudio_memcmp

#define pow FAudio_pow
#define log(x) FAudio_log(x)
#define sin(x) FAudio_sin(x)
#define cos(x) FAudio_cos(x)
#define floor FAudio_floor
#define abs(x) FAudio_abs(x)
#define ldexp(v, e) FAudio_ldexp((v), (e))
#define exp(x) FAudio_exp(x)

#define qsort FAudio_qsort

#define assert FAudio_assert

#define FILE ForgeIOStream
#ifdef SEEK_SET
#undef SEEK_SET
#endif
#ifdef SEEK_END
#undef SEEK_END
#endif
#ifdef EOF
#undef EOF
#endif
#define SEEK_SET FORGE_AUDIO_SEEK_SET
#define SEEK_END FORGE_AUDIO_SEEK_END
#define EOF FORGE_AUDIO_EOF
#define fopen(path, mode) forge_audio_fopen(path)
#define fopen_s(io, path, mode) (!(*io = forge_audio_fopen(path)))
#define fclose(io) forge_audio_close(io)
#define fread(dst, size, count, io) io->read(io->data, dst, size, count)
#define fseek(io, offset, whence) io->seek(io->data, offset, whence)
#define ftell(io) io->seek(io->data, 0, FORGE_AUDIO_SEEK_CUR)

#define STB_VORBIS_NO_PUSHDATA_API 1
#define STB_VORBIS_NO_INTEGER_CONVERSION 1
#include "stb_vorbis_modified.h"

#include "qoa_decoder.h"

/* Globals */

static float songVolume = 1.0f;
static ForgeAudioEngine *songAudio = NULL;
static ForgeMasterVoice *songMaster = NULL;
static unsigned int songLength = 0;
static unsigned int songOffset = 0;

static ForgeSourceVoice *songVoice = NULL;
static ForgeVoiceCallback callbacks;
static stb_vorbis *activeVorbisSong = NULL;
static stb_vorbis_info activeVorbisSongInfo;

static qoa *activeQoaSong = NULL;
static unsigned int qoaChannels = 0;
static unsigned int qoaSampleRate = 0;
static unsigned int qoaSamplesPerChannelPerFrame = 0;
static unsigned int qoaTotalSamplesPerChannel = 0;

static uint8_t *songCache;

/* Internal Functions */

static void XNA_SongSubmitBuffer(ForgeVoiceCallback *callback, void *pBufferContext)
{
    ForgeBuffer buffer;
    uint32_t decoded = 0;

	if (activeVorbisSong != NULL)
	{
		decoded = stb_vorbis_get_samples_float_interleaved(
			activeVorbisSong,
			activeVorbisSongInfo.channels,
			(float*) songCache,
			activeVorbisSongInfo.sample_rate * activeVorbisSongInfo.channels
		);
		buffer.AudioBytes = decoded * activeVorbisSongInfo.channels * sizeof(float);
	}
	else if (activeQoaSong != NULL)
	{
		/* TODO: decode multiple frames? */
		decoded = qoa_decode_next_frame(
			activeQoaSong,
			(short*) songCache
		);
		buffer.AudioBytes = decoded * qoaChannels * sizeof(short);
	}

    if (decoded == 0)
    {
        return;
    }

	songOffset += decoded;
	buffer.Flags = (songOffset >= songLength) ? FORGE_AUDIO_END_OF_STREAM : 0;
	buffer.pAudioData = songCache;
	buffer.PlayBegin = 0;
	buffer.PlayLength = decoded;
	buffer.LoopBegin = 0;
	buffer.LoopLength = 0;
	buffer.LoopCount = 0;
	buffer.pContext = NULL;
	forge_source_voice_submit_buffer(
		songVoice,
		&buffer,
		NULL
	);
}

static void XNA_SongKill()
{
    if (songVoice != NULL)
    {
        forge_source_voice_stop(songVoice, 0, 0);
        forge_voice_destroy(songVoice);
        songVoice = NULL;
    }
    if (songCache != NULL)
    {
        FAudio_free(songCache);
        songCache = NULL;
    }
    if (activeVorbisSong != NULL)
    {
        stb_vorbis_close(activeVorbisSong);
        activeVorbisSong = NULL;
    }
    if (activeQoaSong != NULL)
    {
        qoa_close(activeQoaSong);
        activeQoaSong = NULL;
    }
}

/* "Public" API */

FORGE_AUDIO_API void XNA_SongInit()
{
    forge_audio_create(&songAudio, 0);
    forge_audio_create_master_voice(
        songAudio,
        &songMaster,
        FORGE_AUDIO_DEFAULT_CHANNELS,
        FORGE_AUDIO_DEFAULT_SAMPLERATE,
        0,
        0,
        NULL
    );
}

FORGE_AUDIO_API void XNA_SongQuit()
{
    XNA_SongKill();
    forge_voice_destroy(songMaster);
    forge_audio_engine_release(songAudio);
}

FORGE_AUDIO_API float XNA_PlaySong(const char *name)
{
    ForgeAudioFormat format;
    XNA_SongKill();

    activeVorbisSong = stb_vorbis_open_filename(name, NULL, NULL);

	if (activeVorbisSong != NULL)
	{
		activeVorbisSongInfo = stb_vorbis_get_info(activeVorbisSong);
		format.wFormatTag = FORGE_AUDIO_FORMAT_IEEE_FLOAT;
		format.nChannels = activeVorbisSongInfo.channels;
		format.nSamplesPerSec = activeVorbisSongInfo.sample_rate;
		format.wBitsPerSample = sizeof(float) * 8;
		format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		format.cbSize = 0;

		songOffset = 0;
		songLength = stb_vorbis_stream_length_in_samples(activeVorbisSong);
	}
	else /* It's not vorbis, try qoa!*/
	{
		activeQoaSong = qoa_open_from_filename(name);

        if (activeQoaSong == NULL)
        {
            /* It's neither vorbis nor qoa, time to bail */
            return 0;
        }

        qoa_attributes(activeQoaSong, &qoaChannels, &qoaSampleRate, &qoaSamplesPerChannelPerFrame, &qoaTotalSamplesPerChannel);

		format.wFormatTag = FORGE_AUDIO_FORMAT_PCM;
		format.nChannels = qoaChannels;
		format.nSamplesPerSec = qoaSampleRate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		format.cbSize = 0;

		songOffset = 0;
		songLength = qoaTotalSamplesPerChannel;
	}

    /* Allocate decode cache */
    songCache = (uint8_t*) FAudio_malloc(format.nAvgBytesPerSec);

    /* Init voice */
    FAudio_zero(&callbacks, sizeof(ForgeVoiceCallback));
    callbacks.OnBufferEnd = XNA_SongSubmitBuffer;
    forge_audio_create_source_voice(
        songAudio,
        &songVoice,
        &format,
        0,
        1.0f, /* No pitch shifting here! */
        &callbacks,
        NULL,
        NULL
    );
    forge_voice_set_volume(songVoice, songVolume, 0);

    /* Okay, this song is decoding now */
    if (activeVorbisSong != NULL)
    {
        stb_vorbis_seek_start(activeVorbisSong);
    }
    else if (activeQoaSong != NULL)
    {
        qoa_seek_frame(activeQoaSong, 0);
    }

    XNA_SongSubmitBuffer(NULL, NULL);

    /* Finally. */
    forge_source_voice_start(songVoice, 0, 0);

    if (activeVorbisSong != NULL)
    {
        return stb_vorbis_stream_length_in_seconds(activeVorbisSong);
    }
    else if (activeQoaSong != NULL)
    {
        return qoaTotalSamplesPerChannel / (float) qoaSampleRate;
    }

    return 0;
}

FORGE_AUDIO_API void XNA_PauseSong()
{
    if (songVoice == NULL)
    {
        return;
    }
    forge_source_voice_stop(songVoice, 0, 0);
}

FORGE_AUDIO_API void XNA_ResumeSong()
{
    if (songVoice == NULL)
    {
        return;
    }
    forge_source_voice_start(songVoice, 0, 0);
}

FORGE_AUDIO_API void XNA_StopSong()
{
    XNA_SongKill();
}

FORGE_AUDIO_API void XNA_SetSongVolume(float volume)
{
    songVolume = volume;
    if (songVoice != NULL)
    {
        forge_voice_set_volume(songVoice, songVolume, 0);
    }
}

FORGE_AUDIO_API uint32_t XNA_GetSongEnded()
{
    ForgeVoiceState state;
    if (songVoice == NULL || (activeVorbisSong == NULL && activeQoaSong == NULL))
    {
        return 1;
    }
    forge_source_voice_get_state(songVoice, &state, 0);
    return state.BuffersQueued == 0 && state.SamplesPlayed == 0;
}

FORGE_AUDIO_API void XNA_EnableVisualization(uint32_t enable)
{
    /* TODO: Enable/Disable ForgeApo effect */
}

FORGE_AUDIO_API uint32_t XNA_VisualizationEnabled()
{
    /* TODO: Query ForgeApo effect enabled */
    return 0;
}

FORGE_AUDIO_API void XNA_GetSongVisualizationData(
    float *frequencies,
    float *samples,
    uint32_t count
) {
    /* TODO: Visualization ForgeApo that reads in Song samples, FFT analysis */
}
