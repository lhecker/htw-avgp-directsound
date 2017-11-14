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

class handle_deleter {
public:
	constexpr handle_deleter() {}
	handle_deleter(handle_deleter&&) = default;
	handle_deleter& operator=(handle_deleter&&) = default;
	handle_deleter(const handle_deleter&) = default;
	handle_deleter& operator=(const handle_deleter&) = default;

	void operator()(HANDLE handle) const {
		if (!CloseHandle(handle)) {
			winrt::throw_last_error();
		}
	};
};

class wait_handle_deleter {
public:
	constexpr wait_handle_deleter() {}
	wait_handle_deleter(wait_handle_deleter&&) = default;
	wait_handle_deleter& operator=(wait_handle_deleter&&) = default;
	wait_handle_deleter(const wait_handle_deleter&) = default;
	wait_handle_deleter& operator=(const wait_handle_deleter&) = default;

	void operator()(HANDLE handle) const {
		if (!UnregisterWaitEx(handle, INVALID_HANDLE_VALUE)) {
			winrt::throw_last_error();
		}
	};
};

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

	buffer(buffer&&) = default;
	buffer& operator=(buffer&&) = default;

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

	buffer_lock<SampleType> lock_sec(std::chrono::duration<double> offset, std::chrono::duration<double> seconds) {
		// TODO: Overflow
		const auto bytes_per_second = bytes_per_second();
		return lock(offset.count() * bytes_per_second, seconds.count() * bytes_per_second);
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

	explicit double_buffer(winrt::com_ptr<IDirectSoundBuffer8> com, buffer_info info, provider_function provider) : buffer(std::move(com), info) {
		m_provider = std::move(provider);
		m_state = std::make_unique<std::atomic<uint_fast8_t>>(uint_fast8_t(1));

		{
			HANDLE handle = CreateEvent(nullptr, false, false, nullptr);

			if (!handle) {
				winrt::throw_last_error();
			}

			m_notify_handle.reset(handle);
		}

		{
			std::array<DSBPOSITIONNOTIFY, 2> positions{{
				{ 0, m_notify_handle.get() },
				{ DWORD(buffer_bytes() / 2), m_notify_handle.get() },
			}};
			auto notify = m_com.as<IDirectSoundNotify8>();
			winrt::check_hresult(notify->SetNotificationPositions(DWORD(positions.size()), positions.data()));
		}

		fill_half(false);
	}

	double_buffer(double_buffer&&) = default;
	double_buffer& operator=(double_buffer&&) = default;

	double_buffer(const double_buffer&) = delete;
	double_buffer& operator=(const double_buffer&) = delete;

	void play(bool looping = false) {
		if (m_wait_handle) {
			return;
		}

		create_wait_handle();
		buffer::play(looping);
	}

	void stop() {
		if (!m_wait_handle) {
			return;
		}

		buffer::stop();
		m_wait_handle.reset();
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
			frequency += 100;
		};
	}

private:
	static void wait_callback(PVOID context, BOOLEAN) noexcept {
		static_cast<double_buffer*>(context)->swap_and_fill();
	}

	void swap_and_fill() {
		fill_half(m_state->fetch_xor(1));
	}

	void fill_half(bool second_half) {
		const auto half_width = buffer_bytes() / 2;
		auto lock = this->lock(second_half ? half_width : 0, half_width);
		m_provider(lock.spans(), m_info);
	}

	void expect_move_allowed() {
		Expects(!m_wait_handle);
	}

	void create_wait_handle() {
		m_wait_handle.release();

		HANDLE handle;

		if (!RegisterWaitForSingleObject(&handle, m_notify_handle.get(), &wait_callback, this, INFINITE, WT_EXECUTEDEFAULT)) {
			winrt::throw_last_error();
		}

		m_wait_handle.reset(handle);
	}

	provider_function m_provider;

	// The order of these members is important:
	// The notify handle has to be initialized before the wait handle and they
	// must be destroyed in reverse order as the latter depends on the former.
	std::unique_ptr<void, handle_deleter> m_notify_handle;
	std::unique_ptr<void, wait_handle_deleter> m_wait_handle;

	// Always contains the half that should be filled *next*.
	// 0 = first half
	// 1 = second half
	std::unique_ptr<std::atomic<uint_fast8_t>> m_state;
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
	buffer<ValueType, ChannelCount> create_buffer(size_t samples_per_sec, size_t samples) {
		const auto[com, info] = create_pcm_buffer(ChannelCount, sizeof(ValueType) * 8, samples_per_sec, samples);
		return buffer<ValueType, ChannelCount>(com, info);
	}

	template<typename ValueType, size_t ChannelCount, typename DurationRep, typename DurationPeriod>
	buffer<ValueType, ChannelCount> create_buffer(size_t samples_per_sec, std::chrono::duration<DurationRep, DurationPeriod> duration) {
		const size_t samples = duration_to_samples(samples_per_sec, duration);
		return create_buffer<ValueType, ChannelCount>(samples_per_sec, samples);
	}

	// NOTE: Callbacks to `provider` happen from background threads!
	template<typename ValueType, size_t ChannelCount>
	double_buffer<ValueType, ChannelCount> create_double_buffer(size_t samples_per_sec, size_t samples, typename double_buffer<ValueType, ChannelCount>::provider_function provider) {
		const auto[com, info] = create_pcm_buffer(ChannelCount, sizeof(ValueType) * 8, samples_per_sec, samples * 2);
		return double_buffer<ValueType, ChannelCount>(com, info, std::move(provider));
	}

	template<typename ValueType, size_t ChannelCount, typename DurationRep, typename DurationPeriod>
	double_buffer<ValueType, ChannelCount> create_double_buffer(size_t samples_per_sec, std::chrono::duration<DurationRep, DurationPeriod> duration, typename double_buffer<ValueType, ChannelCount>::provider_function provider) {
		const size_t samples = duration_to_samples(samples_per_sec, duration);
		return create_double_buffer<ValueType, ChannelCount>(samples_per_sec, samples, provider);
	}

	template<typename Rep, class Period>
	static inline size_t duration_to_samples(size_t samples_per_sec, std::chrono::duration<Rep, Period> duration) {
		return size_t(static_cast<std::chrono::duration<double>>(duration).count() * samples_per_sec);
	}

private:

	std::tuple<winrt::com_ptr<IDirectSoundBuffer8>, buffer_info> create_pcm_buffer(size_t channels, size_t bits_per_sample, size_t samples_per_sec, size_t samples);

	winrt::com_ptr<IDirectSound8> m_com;
	winrt::com_ptr<IDirectSoundBuffer> m_primary;
};
