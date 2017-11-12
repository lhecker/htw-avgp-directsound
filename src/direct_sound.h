#pragma once

#include "utils.h"

namespace winrt {

template<>
inline constexpr GUID const& guid_of<IDirectSoundBuffer8>() noexcept {
	return IID_IDirectSoundBuffer8;
}

template<>
inline constexpr GUID const& guid_of<IDirectSoundNotify8>() noexcept {
	return IID_IDirectSoundNotify8;
}

}

template<typename SampleType>
class buffer_lock {
public:
	using SpanPairType = std::array<gsl::span<SampleType>, 2>;

	explicit buffer_lock(winrt::com_ptr<IDirectSoundBuffer8> buffer, SpanPairType spans) noexcept : m_buffer(std::move(buffer)), m_spans(spans) {
	}

	buffer_lock(buffer_lock&&) = default;
	buffer_lock& operator=(buffer_lock&&) = default;

	buffer_lock(const buffer_lock&) = delete;
	buffer_lock& operator=(const buffer_lock&) = delete;

	~buffer_lock() {
		if (m_buffer) {
			m_buffer->Unlock(
				static_cast<void*>(m_spans[0].data()), static_cast<DWORD>(m_spans[0].size_bytes()),
				static_cast<void*>(m_spans[1].data()), static_cast<DWORD>(m_spans[1].size_bytes())
			);
		}
	}

	SpanPairType spans() {
		return m_spans;
	}

protected:
	winrt::com_ptr<IDirectSoundBuffer8> m_buffer;
	SpanPairType m_spans;
};

template<typename ValueType, size_t ChannelCount>
class buffer {
public:
	using SampleType = std::array<ValueType, ChannelCount>;

	explicit buffer() noexcept {
	}

	explicit buffer(winrt::com_ptr<IDirectSoundBuffer8> com, WAVEFORMATEX format, DSBUFFERDESC description) noexcept : m_com(std::move(com)), m_format(format), m_description(description) {
	}

	buffer(buffer&&) = default;
	buffer& operator=(buffer&&) = default;

	buffer(const buffer&) = delete;
	buffer& operator=(const buffer&) = delete;

	virtual ~buffer() {
	}

	void play(bool looping = false) {
		winrt::check_hresult(m_com->Play(0, 0, looping ? DSBPLAY_LOOPING : 0));
	}

	void stop() {
		winrt::check_hresult(m_com->Stop());
	}

	buffer_lock<SampleType> lock(size_t offset, size_t seconds) {
		if (offset > std::numeric_limits<WORD>::max()) {
			throw std::invalid_argument(string_format("invalid argument for offset (overflow): %zu", offset));
		}
		if (seconds > std::numeric_limits<WORD>::max()) {
			throw std::invalid_argument(string_format("invalid argument for seconds (overflow): %zu", seconds));
		}

		void* base1; DWORD len1;
		void* base2; DWORD len2;

		winrt::check_hresult(m_com->Lock(
			DWORD(offset * m_format.nAvgBytesPerSec),
			DWORD(seconds * m_format.nAvgBytesPerSec),
			&base1,
			&len1,
			&base2,
			&len2,
			0
		));

		// Initialize `lock` early in order for the buffer to be unlocked if any exceptions are thrown below.
		buffer_lock<SampleType> lock(m_com, {{
			{ static_cast<SampleType*>(base1), len1 / sizeof(SampleType) },
			{ static_cast<SampleType*>(base2), len2 / sizeof(SampleType)},
		}});

		constexpr auto align_mask = alignof(SampleType)-1;
		if ((size_t(base1) & align_mask) != 0 || (size_t(base2) & align_mask) != 0) {
			throw std::runtime_error("bad buffer alignment");
		}

		return lock;
	}

	void fill_with_sine_wave(size_t offset, size_t seconds, size_t frequency) {
		auto lock = this->lock(offset, seconds);

		constexpr double amplitude = std::numeric_limits<ValueType>::max();
		const auto radiant_periods_per_sample = 2.0 * M_PI * double(frequency) / double(m_format.nSamplesPerSec);
		size_t idx = 0;

		for (auto span : lock.spans()) {
			for (auto& sample : span) {
				const auto period = double(idx) * radiant_periods_per_sample;
				const auto value = ValueType(sin(period) * amplitude);

				std::fill(sample.begin(), sample.end(), value);

				++idx;
			}
		}
	}

protected:
	winrt::com_ptr<IDirectSoundBuffer8> m_com;
	WAVEFORMATEX m_format;
	DSBUFFERDESC m_description;
};

template<typename ValueType, size_t ChannelCount>
class double_buffer : public buffer<ValueType, ChannelCount> {
public:
	explicit double_buffer() noexcept {
	}

	double_buffer(winrt::com_ptr<IDirectSoundBuffer8> com, WAVEFORMATEX format, DSBUFFERDESC description) : buffer(std::move(com), format, description), m_handle(create_handle()) {
		const auto notify = m_com.as<IDirectSoundNotify8>();
		std::array<DSBPOSITIONNOTIFY, 2> positions{{
			{ 0, m_handle },
			{ description.dwBufferBytes / 2, m_handle },
		}};

		winrt::check_hresult(notify->SetNotificationPositions(DWORD(positions.size()), positions.data()));
	}

	double_buffer(double_buffer&&) = default;
	double_buffer& operator=(double_buffer&&) = default;

	double_buffer(const double_buffer&) = delete;
	double_buffer& operator=(const double_buffer&) = delete;

	~double_buffer() {
		if (m_handle) {
			CloseHandle(m_handle);
		}
	}

protected:
	static HANDLE create_handle() {
		return winrt::impl::check_pointer(CreateEvent(0, false, false, L"double_buffer"));
	}

	HANDLE m_handle;
};

class direct_sound {
public:
	explicit direct_sound();
	explicit direct_sound(HWND hwnd);

	direct_sound(direct_sound&&) = default;
	direct_sound& operator=(direct_sound&&) = default;

	direct_sound(const direct_sound&) = delete;
	direct_sound& operator=(const direct_sound&) = delete;

	~direct_sound();

	template<typename ValueType, size_t ChannelCount>
	buffer<ValueType, ChannelCount> create_buffer(size_t samples_per_sec, size_t seconds) {
		auto[sb, format, description] = create_pcm_buffer(ChannelCount, sizeof(ValueType) * 8, samples_per_sec, seconds);
		return buffer<ValueType, ChannelCount>(sb, format, description);
	}

	template<typename ValueType, size_t ChannelCount>
	double_buffer<ValueType, ChannelCount> create_double_buffer(size_t samples_per_sec, size_t seconds) {
		constexpr size_t size_t_highest_bit = (std::numeric_limits<size_t>::max() >> 1) + 1;

		// If the highest bit in seconds is set we can't safely multiply by 2 (or left-shift by 1) below anymore,
		// since by doing so that bit would be lost.
		if (seconds & size_t_highest_bit) {
			throw std::invalid_argument(string_format("invalid argument for seconds (overflow): %zu", seconds));
		}

		auto[sb, format, description] = create_pcm_buffer(ChannelCount, sizeof(ValueType) * 8, samples_per_sec, seconds << 1);
		return double_buffer<ValueType, ChannelCount>(sb, format, description);
	}

protected:
	std::tuple<winrt::com_ptr<IDirectSoundBuffer8>, WAVEFORMATEX, DSBUFFERDESC> create_pcm_buffer(size_t channels, size_t bits_per_sample, size_t samples_per_sec, size_t seconds);

	winrt::com_ptr<IDirectSound8> m_com;
	winrt::com_ptr<IDirectSoundBuffer> m_primary;
};
