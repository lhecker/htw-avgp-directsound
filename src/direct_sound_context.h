#pragma once

namespace winrt {

template<>
inline constexpr GUID const& guid_of<IDirectSoundBuffer8>() noexcept {
	return IID_IDirectSoundBuffer8;
}

template<>
inline constexpr GUID const& guid_of<IDirectSoundNotify8>() noexcept {
	return IID_IDirectSoundNotify8;
}

} // namespace winrt

namespace direct_sound {

class context {
public:
	explicit context() {
	}

	explicit context(HWND hwnd) {
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

		m_primary->SetVolume(DSBVOLUME_MIN);
	}

	winrt::com_ptr<IDirectSoundBuffer8> create_sound_buffer(const DSBUFFERDESC& description) const {
		winrt::com_ptr<IDirectSoundBuffer> com;
		winrt::check_hresult(m_com->CreateSoundBuffer(&description, com.put(), nullptr));
		return com.as<IDirectSoundBuffer8>();
	}

private:
	winrt::com_ptr<IDirectSound8> m_com;
	winrt::com_ptr<IDirectSoundBuffer> m_primary;
};

} // namespace direct_sound
