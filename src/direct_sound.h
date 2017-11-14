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

	SpanPairType spans() const {
		return m_spans;
	}

private:
	winrt::com_ptr<IDirectSoundBuffer8> m_buffer;
	SpanPairType m_spans;
};

struct buffer_info {
public:
	constexpr buffer_info() : samples_per_second(0), samples(0) {
	}

	constexpr buffer_info(size_t samples_per_second, size_t samples) : samples_per_second(samples_per_second), samples(samples) {
	}

	buffer_info(buffer_info&&) = default;
	buffer_info& operator=(buffer_info&&) = default;
	buffer_info(const buffer_info&) = default;
	buffer_info& operator=(const buffer_info&) = default;

	size_t samples_per_second;
	size_t samples;
};

template<typename ValueType, size_t ChannelCount>
class buffer {
public:
	using SampleType = std::array<ValueType, ChannelCount>;
	using SpanPairType = std::array<gsl::span<SampleType>, 2>;

	explicit buffer() noexcept {
	}

	explicit buffer(winrt::com_ptr<IDirectSoundBuffer8> com, buffer_info info) noexcept : m_com(std::move(com)), m_info(info) {
	}

	buffer(buffer&& other) : m_com(std::move(other.m_com)), m_info(other.m_info) {
	}

	buffer& operator=(buffer&& other) {
		m_com = std::move(other.m_com);
		m_info = other.m_info;
		return *this;
	}

	buffer(const buffer&) = delete;
	buffer& operator=(const buffer&) = delete;

	virtual ~buffer() {
	}

	size_t bytes_per_second() const {
		return m_info.samples_per_second * sizeof(SampleType);
	}

	size_t buffer_bytes() const {
		return m_info.samples * sizeof(SampleType);
	}

	void play(bool looping = false) {
		winrt::check_hresult(m_com->Play(0, 0, looping ? DSBPLAY_LOOPING : 0));
	}

	void stop() {
		winrt::check_hresult(m_com->Stop());
	}

	buffer_lock<SampleType> lock(size_t offset, size_t length) {
		if (offset > std::numeric_limits<DWORD>::max()) {
			throw std::invalid_argument(string_format("invalid argument for offset (overflow): %zu", offset));
		}
		if (length > std::numeric_limits<DWORD>::max()) {
			throw std::invalid_argument(string_format("invalid argument for length (overflow): %zu", length));
		}

		void* base1; DWORD len1;
		void* base2; DWORD len2;
		winrt::check_hresult(m_com->Lock(DWORD(offset), DWORD(length), &base1, &len1, &base2, &len2, 0));

		// Initialize `lock` early in order for the buffer to be unlocked if any exceptions are thrown below.
		buffer_lock<SampleType> lock(m_com, {{
			{static_cast<SampleType*>(base1), len1 / sizeof(SampleType)},
			{static_cast<SampleType*>(base2), len2 / sizeof(SampleType)},
		}});

		constexpr auto align_mask = alignof(SampleType)-1;
		if ((size_t(base1) & align_mask) != 0 || (size_t(base2) & align_mask) != 0) {
			throw std::runtime_error("bad buffer alignment");
		}

		return lock;
	}

	buffer_lock<SampleType> lock_sec(size_t offset, size_t seconds) {
		// TODO: Overflow
		const auto bytes_per_second = bytes_per_second();
		return lock(offset * bytes_per_second, seconds * bytes_per_second);
	}

protected:
	winrt::com_ptr<IDirectSoundBuffer8> m_com;
	buffer_info m_info;
};

template<typename ValueType, size_t ChannelCount>
class double_buffer : public buffer<ValueType, ChannelCount> {
public:
	using provider_function = std::function<void(SpanPairType spans, buffer_info info)>;

	explicit double_buffer() noexcept {
	}

	explicit double_buffer(winrt::com_ptr<IDirectSoundBuffer8> com, buffer_info info, provider_function provider) : buffer(std::move(com), info), m_provider(std::move(provider)) {
		m_notify_handle = CreateEvent(0, false, false, nullptr);
		if (!m_notify_handle) {
			winrt::throw_last_error();
		}

		std::array<DSBPOSITIONNOTIFY, 2> positions{{
			{ 0, m_notify_handle },
			{ DWORD(buffer_bytes() / 2), m_notify_handle },
		}};
		set_notify(positions);

		fill_half(false);
	}

	double_buffer(double_buffer&& other) : other.expect_move_allowed(), buffer(std::forward(other)), m_notify_handle(other.m_notify_handle), m_wait_handle(other.m_wait_handle), m_provider(std::move(provider)) {
		other.m_notify_handle = nullptr;
		other.m_wait_handle = nullptr;
	}

	double_buffer& operator=(double_buffer&& other) {
		expect_move_allowed();
		other.expect_move_allowed();

		set_notify(nullptr);
		close_notify_handle();
		m_com = nullptr;

		buffer::operator=(std::forward<double_buffer>(other));

		m_notify_handle = other.m_notify_handle;
		m_wait_handle = other.m_wait_handle;
		other.m_notify_handle = nullptr;
		other.m_wait_handle = nullptr;

		m_provider = std::move(other.m_provider);

		return *this;
	}

	double_buffer(const double_buffer&) = delete;
	double_buffer& operator=(const double_buffer&) = delete;

	~double_buffer() {
		unregister_wait();
		close_notify_handle();
	}

	void play(bool looping = false) {
		register_wait();
		buffer::play(looping);
	}

	void stop() {
		buffer::stop();
		unregister_wait();
	}

	static auto create_sine_wave_provider(size_t frequency) {
		size_t idx = 0;

		return [frequency, idx](SpanPairType spans, buffer_info info) mutable {
			constexpr double amplitude = std::numeric_limits<ValueType>::max();
			const auto radiant_periods_per_sample = 2.0 * M_PI * double(frequency) / double(info.samples_per_second);

			for (auto span : spans) {
				for (auto& sample : span) {
					const auto period = double(idx) * radiant_periods_per_sample;
					const auto value = ValueType(sin(period) * amplitude);

					std::fill(sample.begin(), sample.end(), value);

					++idx;
				}
			}

			idx %= info.samples_per_second;
			frequency += 10;
		};
	}

private:
	static void wait_callback(PVOID context, BOOLEAN) noexcept {
		try {
			static_cast<double_buffer*>(context)->swap_and_fill();
		} catch (const std::exception& e) {
			debug_print(L"%s\n", string_utf8_to_wide(e.what()).c_str());
			std::terminate();
		}
	}

	void swap_and_fill() {
		fill_half(m_state.fetch_xor(1));
	}

	void fill_half(bool second_half) {
		const auto half_width = buffer_bytes() / 2;
		auto lock = this->lock(second_half ? half_width : 0, half_width);
		SpanPairType spans = lock.spans();
		buffer_info info = m_info;

		m_provider(spans, m_info);
	}

	void expect_move_allowed() {
		Expects(!m_wait_handle);
	}

	void set_notify(gsl::span<DSBPOSITIONNOTIFY> positions) {
		if (m_com) {
			auto notify = m_com.as<IDirectSoundNotify8>();
			winrt::check_hresult(notify->SetNotificationPositions(DWORD(positions.size()), positions.data()));
		}
	}

	void register_wait() {
		if (!m_wait_handle) {
			if (!RegisterWaitForSingleObject(&m_wait_handle, m_notify_handle, &wait_callback, this, INFINITE, WT_EXECUTEDEFAULT)) {
				winrt::throw_last_error();
			}
		}
	}

	void unregister_wait() noexcept {
		if (m_wait_handle) {
#pragma warning(suppress:6031)
			UnregisterWaitEx(m_wait_handle, INVALID_HANDLE_VALUE);
			m_wait_handle = nullptr;
		}
	}

	void close_notify_handle() {
		if (m_notify_handle) {
			CloseHandle(m_notify_handle);
		}
	}

	provider_function m_provider;
	HANDLE m_notify_handle = nullptr;
	HANDLE m_wait_handle = nullptr;

	// Always contains the half that should be filled *next*.
	// 0 = first half
	// 1 = second half
	std::atomic<std::uint_fast8_t> m_state = 1;
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
		auto[com, info] = create_pcm_buffer(ChannelCount, sizeof(ValueType) * 8, samples_per_sec, seconds);
		return buffer<ValueType, ChannelCount>(com, info);
	}

	// NOTE: Callbacks to `provider` happen from background threads!
	template<typename ValueType, size_t ChannelCount>
	double_buffer<ValueType, ChannelCount> create_double_buffer(size_t samples_per_sec, size_t seconds, typename double_buffer<ValueType, ChannelCount>::provider_function provider) {
		constexpr size_t size_t_highest_bit = (std::numeric_limits<size_t>::max() >> 1) + 1;

		// If the highest bit in seconds is set we can't safely multiply by 2 (or left-shift by 1) below anymore,
		// since by doing so that bit would be lost.
		if (seconds & size_t_highest_bit) {
			throw std::invalid_argument(string_format("invalid argument for seconds (overflow): %zu", seconds));
		}

		auto[com, info] = create_pcm_buffer(ChannelCount, sizeof(ValueType) * 8, samples_per_sec, seconds << 1);
		return double_buffer<ValueType, ChannelCount>(com, info, std::move(provider));
	}

private:

	std::tuple<winrt::com_ptr<IDirectSoundBuffer8>, buffer_info> create_pcm_buffer(size_t channels, size_t bits_per_sample, size_t samples_per_sec, size_t seconds);

	winrt::com_ptr<IDirectSound8> m_com;
	winrt::com_ptr<IDirectSoundBuffer> m_primary;
};
