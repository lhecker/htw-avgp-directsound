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

} // namespace winrt

namespace direct_sound {

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

class playable {
public:
	virtual void play(bool looping = false) = 0;
	virtual void stop() = 0;
};

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

class buffer_info {
public:
	constexpr buffer_info() : samples_per_secondond(0), samples(0) {
	}

	constexpr buffer_info(size_t samples_per_secondond, size_t samples) : samples_per_secondond(samples_per_secondond), samples(samples) {
	}

	size_t samples_per_secondond;
	size_t samples;
};

template<typename ValueType, size_t ChannelCount>
class buffer_base {
public:
	static_assert(sizeof(ValueType) <= 4);
	static_assert(ChannelCount <= 12);

	using SampleType = std::array<ValueType, ChannelCount>;
	using SpanPairType = std::array<gsl::span<SampleType>, 2>;
};

template<typename ValueType, size_t ChannelCount>
class single_buffer : public buffer_base<ValueType, ChannelCount>, public playable {
public:
	explicit single_buffer() noexcept {
	}

	explicit single_buffer(const context& context, size_t samples_per_second, size_t samples) {
		if (samples_per_second < 128 || samples_per_second > 192000) {
			throw std::invalid_argument(string_format("invalid argument for samples_per_second: %zu > 192000", samples_per_second));
		}

		const auto block_align = ChannelCount * sizeof(ValueType);
		const auto bytes_per_sec = samples_per_second * block_align;
		const auto buffer_bytes = samples * block_align;

		if (samples > std::numeric_limits<DWORD>::max() / block_align) {
			throw std::invalid_argument(string_format("invalid argument for samples (overflow): %zu", samples));
		}

		WAVEFORMATEX format = {};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = WORD(ChannelCount);
		format.wBitsPerSample = WORD(sizeof(ValueType) * 8);
		format.nSamplesPerSec = DWORD(samples_per_second);
		format.nBlockAlign = WORD(block_align);
		format.nAvgBytesPerSec = DWORD(bytes_per_sec);

		DSBUFFERDESC description = {};
		description.dwSize = sizeof(description);
		description.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
		description.dwBufferBytes = DWORD(buffer_bytes);
		description.lpwfxFormat = &format;

		m_com = context.create_sound_buffer(description);
		m_info = buffer_info(samples_per_second, samples);
	}

	void play(bool looping = false) override {
		winrt::check_hresult(m_com->Play(0, 0, looping ? DSBPLAY_LOOPING : 0));
	}

	void stop() override {
		winrt::check_hresult(m_com->Stop());
	}

	const winrt::com_ptr<IDirectSoundBuffer8>& com() const {
		return m_com;
	}

	buffer_info info() const {
		return m_info;
	}

	size_t bytes_per_second() const {
		return m_info.samples_per_secondond * sizeof(SampleType);
	}

	size_t buffer_bytes() const {
		return m_info.samples * sizeof(SampleType);
	}

	buffer_lock<SampleType> lock(size_t offset, size_t length) const {
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

	buffer_lock<SampleType> lock_duration(std::chrono::duration<double> offset, std::chrono::duration<double> seconds) const {
		const auto bytes_per_second = bytes_per_second();
		return lock(offset.count() * bytes_per_second, seconds.count() * bytes_per_second);
	}

private:
	winrt::com_ptr<IDirectSoundBuffer8> m_com;
	buffer_info m_info;
};

template<typename ValueType, size_t ChannelCount>
class double_buffer : public buffer_base<ValueType, ChannelCount>, public playable {
private:
	using Buffer = single_buffer<ValueType, ChannelCount>;

public:
	using ProviderFunction = std::function<void(SpanPairType spans, buffer_info info)>;

	explicit double_buffer() noexcept {
	}

	explicit double_buffer(const context& context, size_t samples_per_second, size_t samples, ProviderFunction provider) : m_shared(std::make_unique<shared>(Buffer(context, samples_per_second, samples * 2), std::move(provider))) {
		{
			HANDLE handle = CreateEvent(nullptr, false, false, nullptr);

			if (!handle) {
				winrt::throw_last_error();
			}

			m_notify_handle.reset(handle);
		}

		{
			HANDLE handle;

			if (!RegisterWaitForSingleObject(&handle, m_notify_handle.get(), &wait_callback, m_shared.get(), INFINITE, WT_EXECUTEDEFAULT)) {
				winrt::throw_last_error();
			}

			m_wait_handle.reset(handle);
		}

		{
			std::array<DSBPOSITIONNOTIFY, 2> positions{{
				{ 0, m_notify_handle.get() },
				{ DWORD(m_shared->buffer.buffer_bytes() / 2), m_notify_handle.get() },
			}};
			auto notify = m_shared->buffer.com().as<IDirectSoundNotify8>();
			winrt::check_hresult(notify->SetNotificationPositions(DWORD(positions.size()), positions.data()));
		}
	}

	void play(bool looping = false) override {
		m_shared->buffer.play(looping);
	}

	void stop() override {
		m_shared->buffer.stop();
	}

private:
	// Contains members shared between the double_buffer and the wait_callback().
	class shared {
	public:
		explicit shared(Buffer&& buffer, ProviderFunction&& provider) : buffer(std::forward<Buffer>(buffer)), provider(std::forward<ProviderFunction>(provider)) {
			swap_and_fill();
		}

		void swap_and_fill() {
			bool second_half = state.fetch_xor(1);

			const auto half_width = buffer.buffer_bytes() / 2;
			auto lock = buffer.lock(second_half ? half_width : 0, half_width);

			provider(lock.spans(), buffer.info());
		}

		Buffer buffer;
		ProviderFunction provider;

		// Always contains the half that should be filled *next*.
		// 0 = first half
		// 1 = second half
		// Since it's assumed that the constructor will call fill_half(false)
		// this is already set to 1
		std::atomic<uint_fast8_t> state = 0;
	};

	static void wait_callback(PVOID context, BOOLEAN) noexcept {
		static_cast<shared*>(context)->swap_and_fill();
	}

	// The order of these members is important:
	// The notify handle has to be initialized before the wait handle and they
	// must be destroyed in reverse order as the latter depends on the former.
	std::unique_ptr<void, handle_deleter> m_notify_handle;
	std::unique_ptr<void, wait_handle_deleter> m_wait_handle;
	std::unique_ptr<shared> m_shared;
};

template<typename ValueType, size_t ChannelCount, typename SampleType = std::array<ValueType, ChannelCount>, typename SpanPairType = std::array<gsl::span<SampleType>, 2>>
size_t fill_with_sine_wave(SpanPairType spans, buffer_info info, size_t frequency, size_t sample_number) {
	constexpr double amplitude = std::numeric_limits<ValueType>::max();
	const auto radiant_periods_per_sample = 2.0 * M_PI * double(frequency) / double(info.samples_per_secondond);

	for (const auto span : spans) {
		// Normally we'd iterate with begin()/end() over the gsl::span,
		// but that involves bounds checking which inhibits the optimizer.
		// -> Use pointer arithmetic instead.
		// See: https://github.com/Microsoft/GSL/issues/376
		for (auto sample = span.data(), end = sample + span.size(); sample != end; ++sample) {
			const auto period = double(sample_number) * radiant_periods_per_sample;
			const auto value = ValueType(sin(period) * amplitude);

			std::fill(sample->begin(), sample->end(), value);

			++sample_number;
		}
	}

	return sample_number % info.samples_per_secondond;
}

template<typename ValueType, size_t ChannelCount>
auto create_sine_wave_provider(size_t frequency) {
	using SampleType = std::array<ValueType, ChannelCount>;
	using SpanPairType = std::array<gsl::span<SampleType>, 2>;

	size_t sample_number = 0;

	return [frequency, sample_number](SpanPairType spans, buffer_info info) mutable {
		sample_number = fill_with_sine_wave<ValueType, ChannelCount>(spans, info, frequency, sample_number);
	};
}

template<typename ValueType, size_t ChannelCount>
auto create_tone_ladder_provider(std::vector<size_t> frequencies) {
	using SampleType = std::array<ValueType, ChannelCount>;
	using SpanPairType = std::array<gsl::span<SampleType>, 2>;

	if (frequencies.empty()) {
		throw std::invalid_argument("frequencies must not be empty");
	}

	size_t frequency_idx = 0;
	size_t sample_number = 0;

	return [frequencies, frequency_idx, sample_number](SpanPairType spans, buffer_info info) mutable {
		if (spans[0].size() + spans[1].size() < 2) {
			throw std::invalid_argument("spans contain less than 2 samples");
		}

		fill_with_sine_wave<ValueType, ChannelCount>(spans, info, frequencies[frequency_idx], sample_number);
		frequency_idx = (frequency_idx + 1) % frequencies.size();

		// Normally we'd be done here but we have a bit of a problem:
		// If the samples count is not a multiple of the samples_per_second it means
		// that our current sine wave will have finished before making a full period.
		//
		// The rest of the code in this function will now try and calculate a wave offset to match
		// and continue with the current sample amplitude and function trend (rising/falling).
		// To do this we map the sample amplitude back to a matching sample_number using asin().
		//
		// You can try out the difference this makes by using this provider with
		// a samples count that's not a multiple of samples_per_second and removing
		// the code below or simply setting the sample_number to 0 above.

		// Retrieve the last 2 samples to detect wether our trend is falling or rising.
		ValueType sample1; // last sample value ("end minus 1")
		ValueType sample2; // next to last sample value ("end minus 2")
		{
			auto it0 = spans[0].end();
			auto it1 = spans[1].end();

			switch (spans[1].size()) {
			case 0:
				sample1 = *(--it0)->data();
				sample2 = *(--it0)->data();
				break;
			case 1:
				sample1 = *(--it1)->data();
				sample2 = *(--it0)->data();
				break;
			default:
				sample1 = *(--it1)->data();
				sample2 = *(--it1)->data();
				break;
			}
		}

		const auto radiant_periods_per_sample = 2.0 * M_PI * double(frequencies[frequency_idx]) / double(info.samples_per_secondond);
		const auto sample1d = double(sample1) / double(std::numeric_limits<ValueType>::max());
		const auto new_sample_rad = asin_2pi(sample1d, sample1 < sample2);
		const auto new_sample = size_t(round(new_sample_rad / radiant_periods_per_sample));

		sample_number = new_sample;
	};
}

// Maps asin's result space from [-M_PI_2, +M_PI_2] to [0, 2*M_PI]
inline double asin_2pi(double value, bool is_falling) {
	auto result = asin(value);

	if (is_falling) {
		result = M_PI - result;
	} else if (std::signbit(value)) {
		result = 2.0 * M_PI + result;
	}

	return result;
}

} // namespace direct_sound
