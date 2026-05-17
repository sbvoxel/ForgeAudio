/* ForgeAudioEngine
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
DEFINE_GUID(IID_IAudioClient,         0x1CB9AD4C, 0xDBFA, 0x4c32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioRenderClient,   0xF294ACFC, 0x3146, 0x4483, 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2);
DEFINE_GUID(IID_IMMDeviceEnumerator,  0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
#endif

static CRITICAL_SECTION faudio_cs = { NULL, -1, 0, 0, 0, 0 };
static IMMDeviceEnumerator *device_enumerator;
static HRESULT init_hr;

struct ForgeAudioWin32PlatformData
{
    IAudioClient *client;
    HANDLE audioThread;
    HANDLE stopEvent;
};

struct ForgeAudioAudioClientThreadArgs
{
    WAVEFORMATEXTENSIBLE format;
    IAudioClient *client;
    HANDLE events[2];
    ForgeAudioEngine *audio;
    UINT updateSize;
};

void ForgeAudio_Log(char const *msg)
{
    OutputDebugStringA(msg);
}

static HMODULE kernelbase = NULL;
static HRESULT (WINAPI *my_SetThreadDescription)(HANDLE, PCWSTR) = NULL;

static void ForgeAudio_ResolveSetThreadDescription(void)
{
    kernelbase = LoadLibraryA("kernelbase.dll");
    if (!kernelbase)
        return;

    my_SetThreadDescription = (HRESULT (WINAPI *)(HANDLE, PCWSTR)) GetProcAddress(kernelbase, "SetThreadDescription");
    if (!my_SetThreadDescription)
    {
        FreeLibrary(kernelbase);
        kernelbase = NULL;
    }
}

static void ForgeAudio_SetThreadName(char const *name)
{
    int ret;
    WCHAR *nameW;

    if (!my_SetThreadDescription)
        return;

    ret = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);

    nameW = ForgeAudio_malloc(ret * sizeof(WCHAR));
    if (!nameW)
        return;

    ret = MultiByteToWideChar(CP_UTF8, 0, name, -1, nameW, ret);
    if (ret)
        my_SetThreadDescription(GetCurrentThread(), nameW);

    ForgeAudio_free(nameW);
}

static HRESULT ForgeAudio_FillAudioClientBuffer(
    struct ForgeAudioAudioClientThreadArgs *args,
    IAudioRenderClient *client,
    UINT frames,
    UINT padding
) {
    HRESULT hr = S_OK;
    BYTE *buffer;

    while (padding + args->updateSize <= frames)
    {
        hr = IAudioRenderClient_GetBuffer(
            client,
            frames - padding,
            &buffer
        );
        if (FAILED(hr)) return hr;

        ForgeAudio_zero(
            buffer,
            args->updateSize * args->format.Format.nBlockAlign
        );

        if (args->audio->active)
        {
            ForgeAudio_Internal_UpdateEngine(
                args->audio,
                (float*) buffer
            );
        }

        hr = IAudioRenderClient_ReleaseBuffer(
            client,
            args->updateSize,
            0
        );
        if (FAILED(hr)) return hr;

        padding += args->updateSize;
    }

    return hr;
}

static DWORD WINAPI ForgeAudio_AudioClientThread(void *user)
{
    struct ForgeAudioAudioClientThreadArgs *args = user;
    IAudioRenderClient *render_client;
    HRESULT hr = S_OK;
    UINT frames, padding = 0;

    ForgeAudio_SetThreadName(__func__);

    hr = IAudioClient_GetService(
        args->client,
        &IID_IAudioRenderClient,
        (void **)&render_client
    );
    ForgeAudio_assert(!FAILED(hr) && "Failed to get IAudioRenderClient service!");

    hr = IAudioClient_GetBufferSize(args->client, &frames);
    ForgeAudio_assert(!FAILED(hr) && "Failed to get IAudioClient buffer size!");

    hr = ForgeAudio_FillAudioClientBuffer(args, render_client, frames, 0);
    ForgeAudio_assert(!FAILED(hr) && "Failed to initialize IAudioClient buffer!");

    hr = IAudioClient_Start(args->client);
    ForgeAudio_assert(!FAILED(hr) && "Failed to start IAudioClient!");

	while (WaitForMultipleObjects(2, args->events, FALSE, INFINITE) == WAIT_OBJECT_0)
	{
		hr = IAudioClient_GetCurrentPadding(args->client, &padding);
		if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
		{
			/* Device was removed, just exit */
			break;
		}
		ForgeAudio_assert(!FAILED(hr) && "Failed to get IAudioClient current padding!");

        hr = ForgeAudio_FillAudioClientBuffer(args, render_client, frames, padding);
        ForgeAudio_assert(!FAILED(hr) && "Failed to fill IAudioClient buffer!");
    }

    hr = IAudioClient_Stop(args->client);
    ForgeAudio_assert(!FAILED(hr) && "Failed to stop IAudioClient!");

    IAudioRenderClient_Release(render_client);
    ForgeAudio_free(args);
    return 0;
}

/* Sets `defaultDeviceIndex` to the default audio device index in
 * `deviceCollection`.
 * On failure, `defaultDeviceIndex` is not modified and the latest error is
 * returned. */
static HRESULT ForgeAudio_DefaultDeviceIndex(
	IMMDeviceCollection *deviceCollection,
	uint32_t* defaultDeviceIndex
) {
	IMMDevice *device;
	HRESULT hr;
	uint32_t i, count;
	WCHAR *default_guid;
	WCHAR *device_guid; 

	/* Open the default device and get its GUID. */
	hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
		device_enumerator,
		eRender,
		eConsole,
		&device
	);
	if (FAILED(hr))
	{
		return hr;
	}
	hr = IMMDevice_GetId(device, &default_guid);
	if (FAILED(hr))
	{
		IMMDevice_Release(device);
		return hr;
	}

	/* Free the default device. */
	IMMDevice_Release(device);

	hr = IMMDeviceCollection_GetCount(deviceCollection, &count);
	if (FAILED(hr))
	{
		CoTaskMemFree(default_guid);
		return hr;
	}

	for (i = 0; i < count; i += 1)
	{
		/* Open the device and get its GUID. */
		hr = IMMDeviceCollection_Item(deviceCollection, i, &device);
		if (FAILED(hr)) {
			CoTaskMemFree(default_guid);
			return hr;
		}
		hr = IMMDevice_GetId(device, &device_guid);
		if (FAILED(hr))
		{
			CoTaskMemFree(default_guid);
			IMMDevice_Release(device);
			return hr;
		}

		if (lstrcmpW(default_guid, device_guid) == 0)
		{
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
static HRESULT ForgeAudio_OpenDevice(uint32_t deviceIndex, IMMDevice **device)
{
	IMMDeviceCollection *deviceCollection;
	HRESULT hr;
	uint32_t defaultDeviceIndex;
	uint32_t actualIndex;

	*device = NULL;

	hr = IMMDeviceEnumerator_EnumAudioEndpoints(
		device_enumerator,
		eRender,
		DEVICE_STATE_ACTIVE,
		&deviceCollection
	);
	if (FAILED(hr))
	{
		return hr;
	}

	/* Get the default device index. */
	hr = ForgeAudio_DefaultDeviceIndex(deviceCollection, &defaultDeviceIndex);
	if (FAILED(hr))
	{
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
	if (FAILED(hr))
	{
		IMMDeviceCollection_Release(deviceCollection);
		return hr;
	}

	IMMDeviceCollection_Release(deviceCollection);

	return hr;
}

void ForgeAudio_PlatformInit(
    ForgeAudioEngine *audio,
    uint32_t flags,
    uint32_t deviceIndex,
    ForgeAudioFormatExtensible *mixFormat,
    uint32_t *updateSize,
    void** platformDevice
) {
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
    ForgeAudio_Internal_InitSIMDFunctions(has_sse2, has_neon);
    ForgeAudio_ResolveSetThreadDescription();

    ForgeAudio_PlatformAddRef();

	*platformDevice = NULL;

    args = ForgeAudio_malloc(sizeof(*args));
    ForgeAudio_assert(!!args && "Failed to allocate ForgeAudioEngine thread args!");

    data = ForgeAudio_malloc(sizeof(*data));
    ForgeAudio_assert(!!data && "Failed to allocate ForgeAudioEngine platform data!");
    ForgeAudio_zero(data, sizeof(*data));

    args->format.Format.wFormatTag = mixFormat->Format.wFormatTag;
    args->format.Format.nChannels = mixFormat->Format.nChannels;
    args->format.Format.nSamplesPerSec = mixFormat->Format.nSamplesPerSec;
    args->format.Format.nAvgBytesPerSec = mixFormat->Format.nAvgBytesPerSec;
    args->format.Format.nBlockAlign = mixFormat->Format.nBlockAlign;
    args->format.Format.wBitsPerSample = mixFormat->Format.wBitsPerSample;
    args->format.Format.cbSize = mixFormat->Format.cbSize;

    if (args->format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        args->format.Samples.wValidBitsPerSample = mixFormat->Samples.wValidBitsPerSample;
        args->format.dwChannelMask = mixFormat->dwChannelMask;
        ForgeAudio_memcpy(
            &args->format.SubFormat,
            &mixFormat->SubFormat,
            sizeof(GUID)
        );
    }

    audioEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    ForgeAudio_assert(!!audioEvent && "Failed to create ForgeAudioEngine thread buffer event!");

    data->stopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    ForgeAudio_assert(!!data->stopEvent && "Failed to create ForgeAudioEngine thread stop event!");

	hr = ForgeAudio_OpenDevice(deviceIndex, &device);
	ForgeAudio_assert(!FAILED(hr) && "Failed to get audio device!");

    hr = IMMDevice_Activate(
        device,
        &IID_IAudioClient,
        CLSCTX_ALL,
        NULL,
        (void **)&data->client
    );
    ForgeAudio_assert(!FAILED(hr) && "Failed to create audio client!");
    IMMDevice_Release(device);

    if (flags & FORGE_AUDIO_1024_QUANTUM) duration = 213333;
    else duration = 100000;

    hr = IAudioClient_Initialize(
        data->client,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        duration * 3,
        0,
        &args->format.Format,
        &GUID_NULL
    );
    ForgeAudio_assert(!FAILED(hr) && "Failed to initialize audio client!");

    hr = IAudioClient_SetEventHandle(data->client, audioEvent);
    ForgeAudio_assert(!FAILED(hr) && "Failed to set audio client event!");

    mixFormat->Format.wFormatTag = args->format.Format.wFormatTag;
    mixFormat->Format.nChannels = args->format.Format.nChannels;
    mixFormat->Format.nSamplesPerSec = args->format.Format.nSamplesPerSec;
    mixFormat->Format.nAvgBytesPerSec = args->format.Format.nAvgBytesPerSec;
    mixFormat->Format.nBlockAlign = args->format.Format.nBlockAlign;
    mixFormat->Format.wBitsPerSample = args->format.Format.wBitsPerSample;

    if (args->format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        mixFormat->Format.cbSize = sizeof(ForgeAudioFormatExtensible) - sizeof(ForgeAudioFormat);
        mixFormat->Samples.wValidBitsPerSample = args->format.Samples.wValidBitsPerSample;
        mixFormat->dwChannelMask = args->format.dwChannelMask;
        ForgeAudio_memcpy(
            &mixFormat->SubFormat,
            &args->format.SubFormat,
            sizeof(GUID)
        );
    }
    else
    {
        mixFormat->Format.cbSize = sizeof(ForgeAudioFormat);
    }

    args->client = data->client;
    args->events[0] = audioEvent;
    args->events[1] = data->stopEvent;
    args->audio = audio;
    if (flags & FORGE_AUDIO_1024_QUANTUM) args->updateSize = args->format.Format.nSamplesPerSec / (1000.0 / (64.0 / 3.0));
    else args->updateSize = args->format.Format.nSamplesPerSec / 100;

    data->audioThread = CreateThread(NULL, 0, &ForgeAudio_AudioClientThread, args, 0, NULL);
    ForgeAudio_assert(!!data->audioThread && "Failed to create audio client thread!");

    *updateSize = args->updateSize;
    *platformDevice = data;
    return;
}

void ForgeAudio_PlatformQuit(void* platformDevice)
{
    struct ForgeAudioWin32PlatformData *data = platformDevice;

    SetEvent(data->stopEvent);
    WaitForSingleObject(data->audioThread, INFINITE);
    if (data->client) IAudioClient_Release(data->client);
    if (kernelbase)
    {
        my_SetThreadDescription = NULL;
        FreeLibrary(kernelbase);
        kernelbase = NULL;
    }
    ForgeAudio_PlatformRelease();
}

void ForgeAudio_PlatformAddRef()
{
    HRESULT hr;
    EnterCriticalSection(&faudio_cs);
    if (!device_enumerator)
    {
        init_hr = CoInitialize(NULL);
        hr = CoCreateInstance(
            &CLSID_MMDeviceEnumerator,
            NULL,
            CLSCTX_INPROC_SERVER,
            &IID_IMMDeviceEnumerator,
            (void**)&device_enumerator
        );
        ForgeAudio_assert(!FAILED(hr) && "CoCreateInstance failed!");
    }
    else IMMDeviceEnumerator_AddRef(device_enumerator);
    LeaveCriticalSection(&faudio_cs);
}

void ForgeAudio_PlatformRelease()
{
    EnterCriticalSection(&faudio_cs);
    if (!IMMDeviceEnumerator_Release(device_enumerator))
    {
        device_enumerator = NULL;
        if (SUCCEEDED(init_hr)) CoUninitialize();
    }
    LeaveCriticalSection(&faudio_cs);
}

uint32_t ForgeAudio_PlatformGetDeviceCount(void)
{
	IMMDeviceCollection *device_collection;
	uint32_t count;
	HRESULT hr;

	ForgeAudio_PlatformAddRef();

	hr = IMMDeviceEnumerator_EnumAudioEndpoints(
		device_enumerator,
		eRender,
		DEVICE_STATE_ACTIVE,
		&device_collection
	);
	if (FAILED(hr)) {
		ForgeAudio_PlatformRelease();
		return 0;
	}

	hr = IMMDeviceCollection_GetCount(device_collection, &count);
	if (FAILED(hr)) {
		IMMDeviceCollection_Release(device_collection);
		ForgeAudio_PlatformRelease();
		return 0;
	}

	IMMDeviceCollection_Release(device_collection);

	ForgeAudio_PlatformRelease();

	return count;
}

uint32_t ForgeAudio_PlatformGetDeviceDetails(
    uint32_t index,
    ForgeDeviceDetails *details
) {
	WAVEFORMATEX *format, *obtained;
	WAVEFORMATEXTENSIBLE *ext;
	IAudioClient *client;
	IMMDevice *device;
	IPropertyStore* properties;
	PROPVARIANT deviceName;
	uint32_t count = 0;
	uint32_t ret = 0;
	HRESULT hr;
	WCHAR *str;
	GUID sub;

	ForgeAudio_memset(details, 0, sizeof(ForgeDeviceDetails));

    ForgeAudio_PlatformAddRef();

	count = ForgeAudio_PlatformGetDeviceCount();
	if (index >= count)
	{
		ForgeAudio_PlatformRelease();
		return FORGE_AUDIO_E_INVALID_CALL;
	}

	if (FAILED(hr = ForgeAudio_OpenDevice(index, &device)))
	{
		ForgeAudio_PlatformRelease();
		return hr;
	}

	if (index == 0)
	{
		details->Role = ForgeDeviceRoleDefault;
	}
	else
	{
		details->Role = ForgeDeviceRoleNone;
	}

	/* Set the Device Display Name */
	if (FAILED(hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &properties)))
	{
		IMMDevice_Release(device);
		ForgeAudio_PlatformRelease();
		return hr;
	}
	if (FAILED(hr = IPropertyStore_GetValue(properties, (PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &deviceName)))
	{
		IPropertyStore_Release(properties);
		IMMDevice_Release(device);
		ForgeAudio_PlatformRelease();
		return hr;
	}
	lstrcpynW((LPWSTR)details->DisplayName, deviceName.pwszVal, ARRAYSIZE(details->DisplayName) - 1);
	PropVariantClear(&deviceName);
	IPropertyStore_Release(properties);

	/* Set the Device ID */
	if (FAILED(hr = IMMDevice_GetId(device, &str)))
	{
		IMMDevice_Release(device);
		ForgeAudio_PlatformRelease();
		return hr;
	}
	lstrcpynW((LPWSTR)details->DeviceID, str, ARRAYSIZE(details->DeviceID) - 1);
	CoTaskMemFree(str);

    if (FAILED(hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client)))
    {
        IMMDevice_Release(device);
        ForgeAudio_PlatformRelease();
        return hr;
    }

    if (FAILED(hr = IAudioClient_GetMixFormat(client, &format)))
    {
        IAudioClient_Release(client);
        IMMDevice_Release(device);
        ForgeAudio_PlatformRelease();
        return hr;
    }

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        ext = (WAVEFORMATEXTENSIBLE *)format;
        sub = ext->SubFormat;
        ForgeAudio_memcpy(
            &ext->SubFormat,
            &FORGE_AUDIO_SUBTYPE_PCM,
            sizeof(GUID)
        );

        hr = IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_SHARED, format, &obtained);
        if (FAILED(hr))
        {
            ext->SubFormat = sub;
        }
        else if (obtained)
        {
            CoTaskMemFree(format);
            format = obtained;
        }
    }

    details->OutputFormat.Format.wFormatTag = format->wFormatTag;
    details->OutputFormat.Format.nChannels = format->nChannels;
    details->OutputFormat.Format.nSamplesPerSec = format->nSamplesPerSec;
    details->OutputFormat.Format.nAvgBytesPerSec = format->nAvgBytesPerSec;
    details->OutputFormat.Format.nBlockAlign = format->nBlockAlign;
    details->OutputFormat.Format.wBitsPerSample = format->wBitsPerSample;
    details->OutputFormat.Format.cbSize = format->cbSize;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        ext = (WAVEFORMATEXTENSIBLE *)format;
        details->OutputFormat.Samples.wValidBitsPerSample = ext->Samples.wValidBitsPerSample;
        details->OutputFormat.dwChannelMask = ext->dwChannelMask;
        ForgeAudio_memcpy(
            &details->OutputFormat.SubFormat,
            &ext->SubFormat,
            sizeof(GUID)
        );
    }
    else
    {
        details->OutputFormat.dwChannelMask = GetMask(format->nChannels);
    }

    CoTaskMemFree(format);

    IAudioClient_Release(client);

    IMMDevice_Release(device);

    ForgeAudio_PlatformRelease();

    return ret;
}

ForgeAudioMutex ForgeAudio_PlatformCreateMutex(void)
{
    CRITICAL_SECTION *cs;

    cs = ForgeAudio_malloc(sizeof(CRITICAL_SECTION));
    if (!cs) return NULL;

    InitializeCriticalSection(cs);

    return cs;
}

void ForgeAudio_PlatformLockMutex(ForgeAudioMutex mutex)
{
    if (mutex) EnterCriticalSection(mutex);
}

void ForgeAudio_PlatformUnlockMutex(ForgeAudioMutex mutex)
{
    if (mutex) LeaveCriticalSection(mutex);
}

void ForgeAudio_PlatformDestroyMutex(ForgeAudioMutex mutex)
{
    if (mutex) DeleteCriticalSection(mutex);
    ForgeAudio_free(mutex);
}

struct ForgeAudioThreadArgs
{
    ForgeAudioThreadFunc func;
    const char *name;
    void* data;
};

static DWORD WINAPI FaudioThreadWrapper(void *user)
{
    struct ForgeAudioThreadArgs *args = user;
    DWORD ret;

    ForgeAudio_SetThreadName(args->name);
    ret = args->func(args->data);

    ForgeAudio_free(args);
    return ret;
}

ForgeAudioThread ForgeAudio_PlatformCreateThread(
    ForgeAudioThreadFunc func,
    const char *name,
    void* data
) {
    struct ForgeAudioThreadArgs *args;

    if (!(args = ForgeAudio_malloc(sizeof(*args)))) return NULL;
    args->func = func;
    args->name = name;
    args->data = data;

    return CreateThread(NULL, 0, &FaudioThreadWrapper, args, 0, NULL);
}

void ForgeAudio_PlatformWaitThread(ForgeAudioThread thread, int32_t *retval)
{
    WaitForSingleObject(thread, INFINITE);
    if (retval != NULL) GetExitCodeThread(thread, (DWORD *)retval);
}

void ForgeAudio_PlatformThreadPriority(ForgeAudioThreadPriority priority)
{
    /* FIXME */
}

uint64_t ForgeAudio_PlatformGetThreadID(void)
{
    return GetCurrentThreadId();
}

void ForgeAudio_sleep(uint32_t ms)
{
    Sleep(ms);
}

uint32_t ForgeAudio_timems()
{
    return GetTickCount();
}

/* ForgeAudioEngine I/O */

static size_t FORGE_AUDIO_CALL ForgeAudio_File_read(
    void *data,
    void *dst,
    size_t size,
    size_t count
) {
    if (!data) return 0;
    return fread(dst, size, count, data);
}

static int64_t FORGE_AUDIO_CALL ForgeAudio_File_seek(
    void *data,
    int64_t offset,
    int whence
) {
    if (!data) return -1;
    fseek(data, offset, whence);
    return ftell(data);
}

static int FORGE_AUDIO_CALL ForgeAudio_File_close(void *data)
{
    if (!data) return 0;
    fclose(data);
    return 0;
}

ForgeIOStream* forge_audio_fopen(const char *path)
{
    ForgeIOStream *io;

    io = (ForgeIOStream*) ForgeAudio_malloc(sizeof(ForgeIOStream));
    if (!io) return NULL;

    io->data = fopen(path, "rb");
    io->read = ForgeAudio_File_read;
    io->seek = ForgeAudio_File_seek;
    io->close = ForgeAudio_File_close;
    io->lock = ForgeAudio_PlatformCreateMutex();

    return io;
}

struct ForgeAudioMemStream
{
    char *mem;
    int64_t len;
    int64_t pos;
};

static size_t FORGE_AUDIO_CALL ForgeAudio_MemRead(
    void *data,
    void *dst,
    size_t size,
    size_t count
) {
    struct ForgeAudioMemStream *io = data;
    size_t len = size * count;

    if (!data) return 0;

    while (len && len > (io->len - io->pos)) len -= size;
    ForgeAudio_memcpy(dst, io->mem + io->pos, len);
    io->pos += len;

    return len;
}

static int64_t FORGE_AUDIO_CALL ForgeAudio_MemSeek(
    void *data,
    int64_t offset,
    int whence
) {
    struct ForgeAudioMemStream *io = data;
    if (!data) return -1;

    if (whence == SEEK_SET)
    {
        if (io->len > offset) io->pos = offset;
        else io->pos = io->len;
    }
    if (whence == SEEK_CUR)
    {
        if (io->len > io->pos + offset) io->pos += offset;
        else io->pos = io->len;
    }
    if (whence == SEEK_END)
    {
        if (io->len > offset) io->pos = io->len - offset;
        else io->pos = 0;
    }

    return io->pos;
}

static int FORGE_AUDIO_CALL ForgeAudio_MemClose(void *data)
{
    if (!data) return 0;
    ForgeAudio_free(data);
    return 0;
}

ForgeIOStream* forge_audio_memopen(void *mem, int len)
{
    struct ForgeAudioMemStream *data;
    ForgeIOStream *io;

    io = (ForgeIOStream*) ForgeAudio_malloc(sizeof(ForgeIOStream));
    if (!io) return NULL;

    data = ForgeAudio_malloc(sizeof(struct ForgeAudioMemStream));
    if (!data)
    {
        ForgeAudio_free(io);
        return NULL;
    }

    data->mem = mem;
    data->len = len;
    data->pos = 0;

    io->data = data;
    io->read = ForgeAudio_MemRead;
    io->seek = ForgeAudio_MemSeek;
    io->close = ForgeAudio_MemClose;
    io->lock = ForgeAudio_PlatformCreateMutex();
    return io;
}

uint8_t* forge_audio_memptr(ForgeIOStream *io, size_t offset)
{
    struct ForgeAudioMemStream *memio = io->data;
    return memio->mem + offset;
}

void forge_audio_close(ForgeIOStream *io)
{
    io->close(io->data);
    ForgeAudio_PlatformDestroyMutex((ForgeAudioMutex) io->lock);
    ForgeAudio_free(io);
}

#else

extern int this_tu_is_empty;

#endif /* FORGE_AUDIO_WIN32_PLATFORM */
