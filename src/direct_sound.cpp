#include "stdafx.h"

#include "direct_sound.h"

direct_sound::direct_sound() {
}

direct_sound::direct_sound(HWND hwnd) {
	{
		winrt::check_hresult(CoCreateInstance(CLSID_DirectSound8, nullptr, CLSCTX_INPROC_SERVER, IID_IDirectSound8, m_com.put_void()));
		winrt::check_hresult(m_com->Initialize(nullptr));
		winrt::check_hresult(m_com->SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
	}

	{
		DSBUFFERDESC description = {};
		description.dwSize = sizeof(description);
		description.dwFlags = DSBCAPS_PRIMARYBUFFER;
		winrt::check_hresult(m_com->CreateSoundBuffer(&description, m_primary.put(), nullptr));
	}
}

direct_sound::~direct_sound() {
}

std::tuple<winrt::com_ptr<IDirectSoundBuffer8>, buffer_info> direct_sound::create_pcm_buffer(size_t channels, size_t bits_per_sample, size_t samples_per_sec, size_t samples) {
	if (channels > 12) {
		throw std::invalid_argument(string_format("invalid argument for channel: %zu > 12", channels));
	}
	if (bits_per_sample > 32) {
		throw std::invalid_argument(string_format("invalid argument for bits_per_sample: %zu > 32", bits_per_sample));
	}
	if (samples_per_sec > 192000) {
		throw std::invalid_argument(string_format("invalid argument for samples_per_sec: %zu > 192000", samples_per_sec));
	}

	const auto block_align = (channels * bits_per_sample) / 8;
	const auto bytes_per_sec = samples_per_sec * block_align;

	if (samples > std::numeric_limits<DWORD>::max() / block_align) {
		throw std::invalid_argument(string_format("invalid argument for samples (overflow): %zu", samples));
	}

	const auto buffer_bytes = samples * block_align;

	WAVEFORMATEX format = {};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = WORD(channels);
	format.wBitsPerSample = WORD(bits_per_sample);
	format.nSamplesPerSec = DWORD(samples_per_sec);
	format.nBlockAlign = WORD(block_align);
	format.nAvgBytesPerSec = DWORD(bytes_per_sec);

	DSBUFFERDESC description = {};
	description.dwSize = sizeof(description);
	description.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
	description.dwBufferBytes = DWORD(buffer_bytes);
	description.lpwfxFormat = &format;

	winrt::com_ptr<IDirectSoundBuffer> com;
	winrt::check_hresult(m_com->CreateSoundBuffer(&description, com.put(), nullptr));

	return {
		com.as<IDirectSoundBuffer8>(),
		buffer_info(samples_per_sec, samples),
	};
}
