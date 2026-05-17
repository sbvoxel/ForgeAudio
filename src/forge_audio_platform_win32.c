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
 * Copyright (c) 2011-2020 Ethan Lee, Luigi Auriemma, and the MonoGame Team
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

#ifdef FORGE_AUDIO_WIN32_PLATFORM

    #include <stddef.h>

    #define COBJMACROS
    #include <windows.h>
    #include <mfidl.h>
    #include <mfapi.h>
    #include <mferror.h>
    #include <mfreadwrite.h>
    #include <propvarutil.h>

    #include <initguid.h>
    #include <audioclient.h>
    #include <mmdeviceapi.h>
    #include <devpkey.h>

    #define _SPEAKER_POSITIONS_ /* Defined by SDK. */
    #include "forge_audio_internal.h"

    #ifdef _MSC_VER
DEFINE_GUID(IID_IAudioClient, 0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioRenderClient, 0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
    #endif

static CRITICAL_SECTION faudio_cs = {NULL, -1, 0, 0, 0, 0};
static IMMDeviceEnumerator *device_enumerator;
static HRESULT init_hr;

struct ForgeAudioWin32PlatformData {
    IAudioClient *client;
    HANDLE audioThread;
    HANDLE stopEvent;
};

struct ForgeAudioAudioClientThreadArgs {
    WAVEFORMATEXTENSIBLE format;
    IAudioClient *client;
    HANDLE events[2];
    ForgeAudioEngine *audio;
    UINT updateSize;
};

void forge_log_message(char const *msg) {
    OutputDebugStringA(msg);
}

static HMODULE kernelbase = NULL;
static HRESULT(WINAPI *my_SetThreadDescription)(HANDLE, PCWSTR) = NULL;

static void ForgeAudio_ResolveSetThreadDescription(void) {
    kernelbase = LoadLibraryA("kernelbase.dll");
    if (!kernelbase)
        return;

    my_SetThreadDescription = (HRESULT(WINAPI *)(HANDLE, PCWSTR))GetProcAddress(kernelbase, "SetThreadDescription");
    if (!my_SetThreadDescription) {
        FreeLibrary(kernelbase);
        kernelbase = NULL;
    }
}

static void ForgeAudio_SetThreadName(char const *name) {
    int ret;
    WCHAR *nameW;

    if (!my_SetThreadDescription)
        return;

    ret = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);

    nameW = forge_malloc(ret * sizeof(WCHAR));
    if (!nameW)
        return;

    ret = MultiByteToWideChar(CP_UTF8, 0, name, -1, nameW, ret);
    if (ret)
        my_SetThreadDescription(GetCurrentThread(), nameW);

    forge_free(nameW);
}

static HRESULT ForgeAudio_FillAudioClientBuffer(struct ForgeAudioAudioClientThreadArgs *args,
                                                IAudioRenderClient *client, UINT frames, UINT padding) {
    HRESULT hr = S_OK;
    BYTE *buffer;

    while (padding + args->updateSize <= frames) {
        hr = IAudioRenderClient_GetBuffer(client, frames - padding, &buffer);
        if (FAILED(hr))
            return hr;

        forge_zero(buffer, args->updateSize * args->format.format.block_align);

        if (args->audio->active) {
            forge_audio_update_engine(args->audio, (float *)buffer);
        }

        hr = IAudioRenderClient_ReleaseBuffer(client, args->updateSize, 0);
        if (FAILED(hr))
            return hr;

        padding += args->updateSize;
    }

    return hr;
}

static DWORD WINAPI ForgeAudio_AudioClientThread(void *user) {
    struct ForgeAudioAudioClientThreadArgs *args = user;
    IAudioRenderClient *render_client;
    HRESULT hr = S_OK;
    UINT frames, padding = 0;

    ForgeAudio_SetThreadName(__func__);

    hr = IAudioClient_GetService(args->client, &IID_IAudioRenderClient, (void **)&render_client);
    forge_assert(!FAILED(hr) && "Failed to get IAudioRenderClient service!");

    hr = IAudioClient_GetBufferSize(args->client, &frames);
    forge_assert(!FAILED(hr) && "Failed to get IAudioClient buffer size!");

    hr = ForgeAudio_FillAudioClientBuffer(args, render_client, frames, 0);
    forge_assert(!FAILED(hr) && "Failed to initialize IAudioClient buffer!");

    hr = IAudioClient_Start(args->client);
    forge_assert(!FAILED(hr) && "Failed to start IAudioClient!");

    while (WaitForMultipleObjects(2, args->events, FALSE, INFINITE) == WAIT_OBJECT_0) {
        hr = IAudioClient_GetCurrentPadding(args->client, &padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            /* Device was removed, just exit */
            break;
        }
        forge_assert(!FAILED(hr) && "Failed to get IAudioClient current padding!");

        hr = ForgeAudio_FillAudioClientBuffer(args, render_client, frames, padding);
        forge_assert(!FAILED(hr) && "Failed to fill IAudioClient buffer!");
    }

    hr = IAudioClient_Stop(args->client);
    forge_assert(!FAILED(hr) && "Failed to stop IAudioClient!");

    IAudioRenderClient_Release(render_client);
    forge_free(args);
    return 0;
}

/* Sets `defaultDeviceIndex` to the default audio device index in
 * `deviceCollection`.
 * On failure, `defaultDeviceIndex` is not modified and the latest error is
 * returned. */
static HRESULT ForgeAudio_DefaultDeviceIndex(IMMDeviceCollection *deviceCollection, uint32_t *defaultDeviceIndex) {
    IMMDevice *device;
    HRESULT hr;
    uint32_t i, count;
    WCHAR *default_guid;
    WCHAR *device_guid;

    /* Open the default device and get its GUID. */
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(device_enumerator, eRender, eConsole, &device);
    if (FAILED(hr)) {
        return hr;
    }
    hr = IMMDevice_GetId(device, &default_guid);
    if (FAILED(hr)) {
        IMMDevice_Release(device);
        return hr;
    }

    /* Free the default device. */
    IMMDevice_Release(device);

    hr = IMMDeviceCollection_GetCount(deviceCollection, &count);
    if (FAILED(hr)) {
        CoTaskMemFree(default_guid);
        return hr;
    }

    for (i = 0; i < count; i += 1) {
        /* Open the device and get its GUID. */
        hr = IMMDeviceCollection_Item(deviceCollection, i, &device);
        if (FAILED(hr)) {
            CoTaskMemFree(default_guid);
            return hr;
        }
        hr = IMMDevice_GetId(device, &device_guid);
        if (FAILED(hr)) {
            CoTaskMemFree(default_guid);
            IMMDevice_Release(device);
            return hr;
        }

        if (lstrcmpW(default_guid, device_guid) == 0) {
            /* Device found. */
            CoTaskMemFree(default_guid);
            CoTaskMemFree(device_guid);
            IMMDevice_Release(device);
            *defaultDeviceIndex = i;
            return S_OK;
        }

        CoTaskMemFree(device_guid);
        IMMDevice_Release(device);
    }

    /* This should probably never happen. Just in case, set
     * `defaultDeviceIndex` to 0 and return S_OK. */
    CoTaskMemFree(default_guid);
    *defaultDeviceIndex = 0;
    return S_OK;
}

/* Open `device`, corresponding to `deviceIndex`. `deviceIndex` 0 always
 * corresponds to the default device. ForgeAudio exposes the default device at
 * index 0 by swapping the devices at indexes 0 and `defaultDeviceIndex`.
 */
static HRESULT ForgeAudio_OpenDevice(uint32_t deviceIndex, IMMDevice **device) {
    IMMDeviceCollection *deviceCollection;
    HRESULT hr;
    uint32_t defaultDeviceIndex;
    uint32_t actualIndex;

    *device = NULL;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(device_enumerator, eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr)) {
        return hr;
    }

    /* Get the default device index. */
    hr = ForgeAudio_DefaultDeviceIndex(deviceCollection, &defaultDeviceIndex);
    if (FAILED(hr)) {
        IMMDeviceCollection_Release(deviceCollection);
        return hr;
    }

    if (deviceIndex == 0) {
        /* Default device. */
        actualIndex = defaultDeviceIndex;
    } else if (deviceIndex == defaultDeviceIndex) {
        /* Open the device at index 0 instead of the "correct" one. */
        actualIndex = 0;
    } else {
        /* Otherwise, just open the device. */
        actualIndex = deviceIndex;
    }
    hr = IMMDeviceCollection_Item(deviceCollection, actualIndex, device);
    if (FAILED(hr)) {
        IMMDeviceCollection_Release(deviceCollection);
        return hr;
    }

    IMMDeviceCollection_Release(deviceCollection);

    return hr;
}

void forge_platform_init(ForgeAudioEngine *audio, uint32_t flags, uint32_t deviceIndex,
                         ForgeAudioFormatExtensible *mixFormat, uint32_t *updateSize, void **platformDevice) {
    struct ForgeAudioAudioClientThreadArgs *args;
    struct ForgeAudioWin32PlatformData *data;
    REFERENCE_TIME duration;
    IMMDevice *device = NULL;
    HRESULT hr;
    HANDLE audioEvent = NULL;
    BOOL has_sse2 = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE);
    #if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64ec__) || defined(_M_ARM64EC)
    BOOL has_neon = TRUE;
    #elif defined(__arm__) || defined(_M_ARM)
    BOOL has_neon = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);
    #else
    BOOL has_neon = FALSE;
    #endif
    forge_audio_init_simd_functions(has_sse2, has_neon);
    ForgeAudio_ResolveSetThreadDescription();

    forge_platform_add_ref();

    *platformDevice = NULL;

    args = forge_malloc(sizeof(*args));
    forge_assert(!!args && "Failed to allocate ForgeAudio thread args!");

    data = forge_malloc(sizeof(*data));
    forge_assert(!!data && "Failed to allocate ForgeAudio platform data!");
    forge_zero(data, sizeof(*data));

    args->format.format.format_tag = mixFormat->format.format_tag;
    args->format.format.channels = mixFormat->format.channels;
    args->format.format.sample_rate = mixFormat->format.sample_rate;
    args->format.format.average_bytes_per_second = mixFormat->format.average_bytes_per_second;
    args->format.format.block_align = mixFormat->format.block_align;
    args->format.format.bits_per_sample = mixFormat->format.bits_per_sample;
    args->format.format.extra_size = mixFormat->format.extra_size;

    if (args->format.format.format_tag == WAVE_FORMAT_EXTENSIBLE) {
        args->format.samples.valid_bits_per_sample = mixFormat->samples.valid_bits_per_sample;
        args->format.channel_mask = mixFormat->channel_mask;
        forge_memcpy(&args->format.sub_format, &mixFormat->sub_format, sizeof(GUID));
    }

    audioEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    forge_assert(!!audioEvent && "Failed to create ForgeAudio thread buffer event!");

    data->stopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    forge_assert(!!data->stopEvent && "Failed to create ForgeAudio thread stop event!");

    hr = ForgeAudio_OpenDevice(deviceIndex, &device);
    forge_assert(!FAILED(hr) && "Failed to get audio device!");

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&data->client);
    forge_assert(!FAILED(hr) && "Failed to create audio client!");
    IMMDevice_Release(device);

    if (flags & FORGE_AUDIO_1024_QUANTUM)
        duration = 213333;
    else
        duration = 100000;

    hr = IAudioClient_Initialize(data->client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, duration * 3,
                                 0, &args->format.format, &GUID_NULL);
    forge_assert(!FAILED(hr) && "Failed to initialize audio client!");

    hr = IAudioClient_SetEventHandle(data->client, audioEvent);
    forge_assert(!FAILED(hr) && "Failed to set audio client event!");

    mixFormat->format.format_tag = args->format.format.format_tag;
    mixFormat->format.channels = args->format.format.channels;
    mixFormat->format.sample_rate = args->format.format.sample_rate;
    mixFormat->format.average_bytes_per_second = args->format.format.average_bytes_per_second;
    mixFormat->format.block_align = args->format.format.block_align;
    mixFormat->format.bits_per_sample = args->format.format.bits_per_sample;

    if (args->format.format.format_tag == WAVE_FORMAT_EXTENSIBLE) {
        mixFormat->format.extra_size = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        mixFormat->samples.valid_bits_per_sample = args->format.samples.valid_bits_per_sample;
        mixFormat->channel_mask = args->format.channel_mask;
        forge_memcpy(&mixFormat->sub_format, &args->format.sub_format, sizeof(GUID));
    } else {
        mixFormat->format.extra_size = sizeof(ForgeAudioFormat);
    }

    args->client = data->client;
    args->events[0] = audioEvent;
    args->events[1] = data->stopEvent;
    args->audio = audio;
    if (flags & FORGE_AUDIO_1024_QUANTUM)
        args->updateSize = args->format.format.sample_rate / (1000.0 / (64.0 / 3.0));
    else
        args->updateSize = args->format.format.sample_rate / 100;

    data->audioThread = CreateThread(NULL, 0, &ForgeAudio_AudioClientThread, args, 0, NULL);
    forge_assert(!!data->audioThread && "Failed to create audio client thread!");

    *updateSize = args->updateSize;
    *platformDevice = data;
    return;
}

void forge_platform_quit(void *platformDevice) {
    struct ForgeAudioWin32PlatformData *data = platformDevice;

    SetEvent(data->stopEvent);
    WaitForSingleObject(data->audioThread, INFINITE);
    if (data->client)
        IAudioClient_Release(data->client);
    if (kernelbase) {
        my_SetThreadDescription = NULL;
        FreeLibrary(kernelbase);
        kernelbase = NULL;
    }
    forge_platform_release();
}

void forge_platform_add_ref() {
    HRESULT hr;
    EnterCriticalSection(&faudio_cs);
    if (!device_enumerator) {
        init_hr = CoInitialize(NULL);
        hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
                              (void **)&device_enumerator);
        forge_assert(!FAILED(hr) && "CoCreateInstance failed!");
    } else
        IMMDeviceEnumerator_AddRef(device_enumerator);
    LeaveCriticalSection(&faudio_cs);
}

void forge_platform_release() {
    EnterCriticalSection(&faudio_cs);
    if (!IMMDeviceEnumerator_Release(device_enumerator)) {
        device_enumerator = NULL;
        if (SUCCEEDED(init_hr))
            CoUninitialize();
    }
    LeaveCriticalSection(&faudio_cs);
}

uint32_t forge_platform_get_device_count(void) {
    IMMDeviceCollection *device_collection;
    uint32_t count;
    HRESULT hr;

    forge_platform_add_ref();

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(device_enumerator, eRender, DEVICE_STATE_ACTIVE, &device_collection);
    if (FAILED(hr)) {
        forge_platform_release();
        return 0;
    }

    hr = IMMDeviceCollection_GetCount(device_collection, &count);
    if (FAILED(hr)) {
        IMMDeviceCollection_Release(device_collection);
        forge_platform_release();
        return 0;
    }

    IMMDeviceCollection_Release(device_collection);

    forge_platform_release();

    return count;
}

ForgeResult forge_platform_get_device_details(uint32_t index, ForgeDeviceDetails *details) {
    WAVEFORMATEX *format, *obtained;
    WAVEFORMATEXTENSIBLE *ext;
    IAudioClient *client;
    IMMDevice *device;
    IPropertyStore *properties;
    PROPVARIANT deviceName;
    uint32_t count = 0;
    ForgeResult ret = ForgeResultSuccess;
    HRESULT hr;
    WCHAR *str;
    GUID sub;

    forge_memset(details, 0, sizeof(ForgeDeviceDetails));

    forge_platform_add_ref();

    count = forge_platform_get_device_count();
    if (index >= count) {
        forge_platform_release();
        return ForgeResultInvalidCall;
    }

    if (FAILED(hr = ForgeAudio_OpenDevice(index, &device))) {
        forge_platform_release();
        return hr;
    }

    if (index == 0) {
        details->role = ForgeDeviceRoleDefault;
    } else {
        details->role = ForgeDeviceRoleNone;
    }

    /* Set the Device Display Name */
    if (FAILED(hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &properties))) {
        IMMDevice_Release(device);
        forge_platform_release();
        return hr;
    }
    if (FAILED(hr = IPropertyStore_GetValue(properties, (PROPERTYKEY *)&DEVPKEY_Device_FriendlyName, &deviceName))) {
        IPropertyStore_Release(properties);
        IMMDevice_Release(device);
        forge_platform_release();
        return hr;
    }
    lstrcpynW((LPWSTR)details->display_name, deviceName.pwszVal, ARRAYSIZE(details->display_name) - 1);
    PropVariantClear(&deviceName);
    IPropertyStore_Release(properties);

    /* Set the Device ID */
    if (FAILED(hr = IMMDevice_GetId(device, &str))) {
        IMMDevice_Release(device);
        forge_platform_release();
        return hr;
    }
    lstrcpynW((LPWSTR)details->device_id, str, ARRAYSIZE(details->device_id) - 1);
    CoTaskMemFree(str);

    if (FAILED(hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client))) {
        IMMDevice_Release(device);
        forge_platform_release();
        return hr;
    }

    if (FAILED(hr = IAudioClient_GetMixFormat(client, &format))) {
        IAudioClient_Release(client);
        IMMDevice_Release(device);
        forge_platform_release();
        return hr;
    }

    if (format->format_tag == WAVE_FORMAT_EXTENSIBLE) {
        ext = (WAVEFORMATEXTENSIBLE *)format;
        sub = ext->sub_format;
        forge_memcpy(&ext->sub_format, &forge_audio_format_subtype_pcm, sizeof(GUID));

        hr = IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_SHARED, format, &obtained);
        if (FAILED(hr)) {
            ext->sub_format = sub;
        } else if (obtained) {
            CoTaskMemFree(format);
            format = obtained;
        }
    }

    details->output_format.format.format_tag = format->format_tag;
    details->output_format.format.channels = format->channels;
    details->output_format.format.sample_rate = format->sample_rate;
    details->output_format.format.average_bytes_per_second = format->average_bytes_per_second;
    details->output_format.format.block_align = format->block_align;
    details->output_format.format.bits_per_sample = format->bits_per_sample;
    details->output_format.format.extra_size = format->extra_size;

    if (format->format_tag == WAVE_FORMAT_EXTENSIBLE) {
        ext = (WAVEFORMATEXTENSIBLE *)format;
        details->output_format.samples.valid_bits_per_sample = ext->samples.valid_bits_per_sample;
        details->output_format.channel_mask = ext->channel_mask;
        forge_memcpy(&details->output_format.sub_format, &ext->sub_format, sizeof(GUID));
    } else {
        details->output_format.channel_mask = GetMask(format->channels);
    }

    CoTaskMemFree(format);

    IAudioClient_Release(client);

    IMMDevice_Release(device);

    forge_platform_release();

    return ret;
}

ForgeAudioMutex forge_platform_create_mutex(void) {
    CRITICAL_SECTION *cs;

    cs = forge_malloc(sizeof(CRITICAL_SECTION));
    if (!cs)
        return NULL;

    InitializeCriticalSection(cs);

    return cs;
}

void forge_platform_lock_mutex(ForgeAudioMutex mutex) {
    if (mutex)
        EnterCriticalSection(mutex);
}

void forge_platform_unlock_mutex(ForgeAudioMutex mutex) {
    if (mutex)
        LeaveCriticalSection(mutex);
}

void forge_platform_destroy_mutex(ForgeAudioMutex mutex) {
    if (mutex)
        DeleteCriticalSection(mutex);
    forge_free(mutex);
}

struct ForgeAudioThreadArgs {
    ForgeAudioThreadFunc func;
    const char *name;
    void *data;
};

static DWORD WINAPI FaudioThreadWrapper(void *user) {
    struct ForgeAudioThreadArgs *args = user;
    DWORD ret;

    ForgeAudio_SetThreadName(args->name);
    ret = args->func(args->data);

    forge_free(args);
    return ret;
}

ForgeAudioThread forge_platform_create_thread(ForgeAudioThreadFunc func, const char *name, void *data) {
    struct ForgeAudioThreadArgs *args;

    if (!(args = forge_malloc(sizeof(*args))))
        return NULL;
    args->func = func;
    args->name = name;
    args->data = data;

    return CreateThread(NULL, 0, &FaudioThreadWrapper, args, 0, NULL);
}

void forge_platform_wait_thread(ForgeAudioThread thread, int32_t *retval) {
    WaitForSingleObject(thread, INFINITE);
    if (retval != NULL)
        GetExitCodeThread(thread, (DWORD *)retval);
}

void forge_platform_set_thread_priority(ForgeAudioThreadPriority priority) {
    /* FIXME */
}

uint64_t forge_platform_get_thread_id(void) {
    return GetCurrentThreadId();
}

void forge_audio_sleep(uint32_t ms) {
    Sleep(ms);
}

uint32_t forge_audio_time_ms() {
    return GetTickCount();
}

/* ForgeAudio I/O */

static size_t FORGE_AUDIO_CALL ForgeAudio_File_read(void *data, void *dst, size_t size, size_t count) {
    if (!data)
        return 0;
    return fread(dst, size, count, data);
}

static int64_t FORGE_AUDIO_CALL ForgeAudio_File_seek(void *data, int64_t offset, int whence) {
    if (!data)
        return -1;
    fseek(data, offset, whence);
    return ftell(data);
}

static int FORGE_AUDIO_CALL ForgeAudio_File_close(void *data) {
    if (!data)
        return 0;
    fclose(data);
    return 0;
}

ForgeIOStream *forge_audio_fopen(const char *path) {
    ForgeIOStream *io;

    io = (ForgeIOStream *)forge_malloc(sizeof(ForgeIOStream));
    if (!io)
        return NULL;

    io->data = fopen(path, "rb");
    io->read = ForgeAudio_File_read;
    io->seek = ForgeAudio_File_seek;
    io->close = ForgeAudio_File_close;
    io->lock = forge_platform_create_mutex();

    return io;
}

struct ForgeAudioMemStream {
    char *mem;
    int64_t len;
    int64_t pos;
};

static size_t FORGE_AUDIO_CALL ForgeAudio_MemRead(void *data, void *dst, size_t size, size_t count) {
    struct ForgeAudioMemStream *io = data;
    size_t len = size * count;

    if (!data)
        return 0;

    while (len && len > (io->len - io->pos))
        len -= size;
    forge_memcpy(dst, io->mem + io->pos, len);
    io->pos += len;

    return len;
}

static int64_t FORGE_AUDIO_CALL ForgeAudio_MemSeek(void *data, int64_t offset, int whence) {
    struct ForgeAudioMemStream *io = data;
    if (!data)
        return -1;

    if (whence == SEEK_SET) {
        if (io->len > offset)
            io->pos = offset;
        else
            io->pos = io->len;
    }
    if (whence == SEEK_CUR) {
        if (io->len > io->pos + offset)
            io->pos += offset;
        else
            io->pos = io->len;
    }
    if (whence == SEEK_END) {
        if (io->len > offset)
            io->pos = io->len - offset;
        else
            io->pos = 0;
    }

    return io->pos;
}

static int FORGE_AUDIO_CALL ForgeAudio_MemClose(void *data) {
    if (!data)
        return 0;
    forge_free(data);
    return 0;
}

ForgeIOStream *forge_audio_memopen(void *mem, int len) {
    struct ForgeAudioMemStream *data;
    ForgeIOStream *io;

    io = (ForgeIOStream *)forge_malloc(sizeof(ForgeIOStream));
    if (!io)
        return NULL;

    data = forge_malloc(sizeof(struct ForgeAudioMemStream));
    if (!data) {
        forge_free(io);
        return NULL;
    }

    data->mem = mem;
    data->len = len;
    data->pos = 0;

    io->data = data;
    io->read = ForgeAudio_MemRead;
    io->seek = ForgeAudio_MemSeek;
    io->close = ForgeAudio_MemClose;
    io->lock = forge_platform_create_mutex();
    return io;
}

uint8_t *forge_audio_memptr(ForgeIOStream *io, size_t offset) {
    struct ForgeAudioMemStream *memio = io->data;
    return memio->mem + offset;
}

void forge_audio_close(ForgeIOStream *io) {
    io->close(io->data);
    forge_platform_destroy_mutex((ForgeAudioMutex)io->lock);
    forge_free(io);
}

#else

extern int this_tu_is_empty;

#endif /* FORGE_AUDIO_WIN32_PLATFORM */
