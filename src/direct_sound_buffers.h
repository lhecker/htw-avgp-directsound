#pragma once

namespace direct_sound {

namespace detail {

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

} // namespace detail

class playable {
public:
	virtual ~playable() {}

	virtual void play(bool looping = false) = 0;
	virtual void stop() = 0;
	virtual void set_volume(int volume) = 0;
	virtual void set_pan(int pan) = 0;
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
	constexpr buffer_info() : samples_per_second(0), samples(0) {
	}

	constexpr buffer_info(size_t samples_per_second, size_t samples) : samples_per_second(samples_per_second), samples(samples) {
	}

	size_t samples_per_second;
	size_t samples;
};

template<typename ValueType, size_t ChannelCount>
class buffer_trait {
public:
	static_assert(sizeof(ValueType) <= 4);
	static_assert(ChannelCount <= 12);

	using SampleType = std::array<ValueType, ChannelCount>;
	using SpanPairType = std::array<gsl::span<SampleType>, 2>;
	using ProviderFunction = std::function<void(SpanPairType spans, buffer_info info)>;
};

template<typename ValueType, size_t ChannelCount>
class single_buffer : public buffer_trait<ValueType, ChannelCount>, public playable {
public:
	explicit single_buffer() noexcept {
	}

	explicit single_buffer(const context& context, size_t samples_per_second, size_t samples, ProviderFunction provider = nullptr) {
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
		description.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS;
		description.dwBufferBytes = DWORD(buffer_bytes);
		description.lpwfxFormat = &format;

		m_com = context.create_sound_buffer(description);
		m_info = buffer_info(samples_per_second, samples);

		if (provider) {
			auto lock = lock_samples(0, samples);
			provider(lock.spans(), m_info);
		}
	}

	void play(bool looping = false) override {
		winrt::check_hresult(m_com->Play(0, 0, looping ? DSBPLAY_LOOPING : 0));
	}

	void stop() override {
		winrt::check_hresult(m_com->Stop());
	}

	void set_volume(int volume) override {
		if (volume < DSBVOLUME_MIN || volume > DSBVOLUME_MAX) {
			throw std::invalid_argument(string_format("invalid argument for volume: %i", volume));
		}
		winrt::check_hresult(m_com->SetVolume(LONG(volume)));
	}

	void set_pan(int pan) override {
		if (pan < DSBPAN_LEFT || pan > DSBPAN_RIGHT) {
			throw std::invalid_argument(string_format("invalid argument for pan: %i", pan));
		}
		winrt::check_hresult(m_com->SetPan(LONG(pan)));
	}

	const winrt::com_ptr<IDirectSoundBuffer8>& com() const {
		return m_com;
	}

	buffer_info info() const {
		return m_info;
	}

	size_t bytes_per_second() const {
		return m_info.samples_per_second * sizeof(SampleType);
	}

	size_t buffer_bytes() const {
		return m_info.samples * sizeof(SampleType);
	}

	buffer_lock<SampleType> lock_samples(size_t offset, size_t length) const {
		offset *= sizeof(SampleType);
		length *= sizeof(SampleType);

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
		return lock_samples(offset.count() * m_info.samples_per_second, seconds.count() * m_info.samples_per_second);
	}

private:
	winrt::com_ptr<IDirectSoundBuffer8> m_com;
	buffer_info m_info;
};

template<typename ValueType, size_t ChannelCount>
class double_buffer : public buffer_trait<ValueType, ChannelCount>, public playable {
private:
	using Buffer = single_buffer<ValueType, ChannelCount>;

public:
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
				{0, m_notify_handle.get()},
				{DWORD(m_shared->buffer.buffer_bytes() / 2), m_notify_handle.get()},
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

	void set_volume(int volume) override {
		m_shared->buffer.set_volume(volume);
	}

	void set_pan(int pan) override {
		m_shared->buffer.set_pan(pan);
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
			auto info = buffer.info();
			const auto half_width = info.samples / 2;

			auto lock = buffer.lock_samples(second_half ? half_width : 0, half_width);
			provider(lock.spans(), info);
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

	std::unique_ptr<shared> m_shared;

	// The order of these members is important:
	// The notify handle has to be initialized before the wait handle and they
	// must be destroyed in reverse order as the latter depends on the former.
	std::unique_ptr<void, detail::handle_deleter> m_notify_handle;
	std::unique_ptr<void, detail::wait_handle_deleter> m_wait_handle;
};

} // namespace direct_sound
