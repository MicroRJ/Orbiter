#include "os_win32_internal.h"
#include "os_audio.h"
#include <audioclient.h>
#include <mmdeviceapi.h>

typedef struct
{
	u32                 buffer_frame_count;
	u32                 bytes_per_frame;
	u32                 channel_count;
	IAudioClient       *client;
	IMMDevice          *device;
	IAudioRenderClient *renderer;
}
OS_Win32AudioState;

global OS_Win32AudioState os_audio;

global const CLSID os_clsid_device_enumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
global const IID   os_iid_device_enumerator   = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
global const IID   os_iid_audio_client        = { 0x1CB9AD4C, 0xDBFA, 0x4c32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
global const IID   os_iid_audio_renderer      = { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
global const GUID  os_subtype_ieee_float      = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

#define OS_RELEASE(value) do { if (value) { (value)->lpVtbl->Release(value); (value) = 0; } } while (0)

static b32 os_audio_format_is_native_float(const WAVEFORMATEX *format)
{
	if (format->wBitsPerSample != sizeof(f32) * 8) return false;
	if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
	if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
		format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
		return false;
	}
	const WAVEFORMATEXTENSIBLE *extended = (const WAVEFORMATEXTENSIBLE *)format;
	return memory_match(&extended->SubFormat, &os_subtype_ieee_float, sizeof(os_subtype_ieee_float));
}

b32 os_audio_init(OS_AudioInfo *info)
{
	Assert(info);
	memory_zero(&os_audio, sizeof(os_audio));
	memory_zero(info, sizeof(*info));

	WAVEFORMATEX *format = 0;

	IMMDeviceEnumerator *enumerator = 0;
	HRESULT hr = CoCreateInstance(&os_clsid_device_enumerator, 0, CLSCTX_ALL, &os_iid_device_enumerator, (void **)&enumerator);
	if (FAILED(hr)) goto failure;

	hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &os_audio.device);
	OS_RELEASE(enumerator);
	if (FAILED(hr)) goto failure;

	hr = os_audio.device->lpVtbl->Activate(os_audio.device, &os_iid_audio_client, CLSCTX_ALL, 0, (void **)&os_audio.client);
	if (FAILED(hr)) goto failure;

	hr = os_audio.client->lpVtbl->GetMixFormat(os_audio.client, &format);
	if (FAILED(hr)) goto failure;

	if (!os_audio_format_is_native_float(format) || !format->nChannels ||
		format->nBlockAlign != format->nChannels * sizeof(f32)) {
		goto failure;
	}

	u64 requested_time_ms = 50;
	#define NANOSECONDS_IN_A_MILLISECOND 10000
	REFERENCE_TIME requested_duration = (REFERENCE_TIME) (requested_time_ms * NANOSECONDS_IN_A_MILLISECOND);
	hr = os_audio.client->lpVtbl->Initialize(os_audio.client, AUDCLNT_SHAREMODE_SHARED, 0, requested_duration, 0, format, 0);
	if (FAILED(hr)) goto failure;

	UINT32 capacity = 0;
	hr = os_audio.client->lpVtbl->GetBufferSize(os_audio.client, &capacity);
	if (FAILED(hr)) goto failure;

	hr = os_audio.client->lpVtbl->GetService(os_audio.client, &os_iid_audio_renderer, (void **)&os_audio.renderer);
	if (FAILED(hr)) goto failure;

	BYTE *initial_buffer = 0;
	hr = os_audio.renderer->lpVtbl->GetBuffer(os_audio.renderer, capacity, &initial_buffer);
	if (FAILED(hr)) goto failure;

	hr = os_audio.renderer->lpVtbl->ReleaseBuffer(os_audio.renderer, capacity, AUDCLNT_BUFFERFLAGS_SILENT);
	if (FAILED(hr)) goto failure;

	hr = os_audio.client->lpVtbl->Start(os_audio.client);
	if (FAILED(hr)) goto failure;


	os_audio.buffer_frame_count = capacity;
	os_audio.bytes_per_frame    = format->nBlockAlign;
	os_audio.channel_count      = format->nChannels;
	info->buffer_frame_count    = capacity;
	info->sample_rate           = format->nSamplesPerSec;
	info->channel_count         = format->nChannels;
	CoTaskMemFree(format);
	return true;

failure:
	if (format) CoTaskMemFree(format);
	os_audio_shutdown();
	return false;
}

void os_audio_shutdown(void)
{
	if (os_audio.client) os_audio.client->lpVtbl->Stop(os_audio.client);
	OS_RELEASE(os_audio.renderer);
	OS_RELEASE(os_audio.client);
	OS_RELEASE(os_audio.device);
	memory_zero(&os_audio, sizeof(os_audio));
}

u32 os_audio_writable_frames(void)
{
	if (!os_audio.client) return 0;
	UINT32 padding = 0;
	HRESULT hr = os_audio.client->lpVtbl->GetCurrentPadding(os_audio.client,
		&padding);
	if (FAILED(hr)) return 0;
	return os_audio.buffer_frame_count - padding;
}

u32 os_audio_write_mono(const f32 *samples, u32 count)
{
	count = Min(count, os_audio_writable_frames());
	if (!count) return 0;
	BYTE *output = 0;
	HRESULT hr = os_audio.renderer->lpVtbl->GetBuffer(os_audio.renderer,
		count, &output);
	if (FAILED(hr)) return 0;
	for (u32 index = 0; index < count; ++index)
	{
		for (u32 channel = 0; channel < os_audio.channel_count; ++channel) {
			((f32 *)output)[index * os_audio.channel_count + channel] = samples[index];
		}
	}
	hr = os_audio.renderer->lpVtbl->ReleaseBuffer(os_audio.renderer, count, 0);
	return SUCCEEDED(hr) ? count : 0;
}

#undef OS_RELEASE
