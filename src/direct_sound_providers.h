#pragma once

namespace direct_sound {

namespace detail {

template<typename ValueType, size_t ChannelCount>
uint32_t fill_with_sine_wave(typename buffer_trait<ValueType, ChannelCount>::SpanPairType spans, buffer_info info, size_t frequency, uint32_t sample_number) {
	constexpr double amplitude = std::numeric_limits<ValueType>::max();
	const auto radiant_periods_per_sample = 2.0 * M_PI * double(frequency) / double(info.samples_per_second);

	for (const auto span : spans) {
		// HACK:
		//   Normally we'd iterate with for-each over the gsl::span,
		//   but that involves bounds checking which inhibits the optimizer.
		//   By using pointer arithmetic instead we, among others, remove 3
		//   heavy conditional jumps from this loop's assembly.
		//   See: https://github.com/Microsoft/GSL/issues/376
		for (auto sample = span.data(), end = sample + span.size(); sample != end; ++sample) {
			const auto period = double(sample_number) * radiant_periods_per_sample;
			const auto value = ValueType(std::sin(period) * amplitude);

			sample->fill(value);

			++sample_number;
		}
	}

	return sample_number % info.samples_per_second;
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

} // namespace detail

template<typename ValueType, size_t ChannelCount>
auto create_pcm_provider(std::vector<byte> pcm) {
	size_t pcm_pos = 0;

	return [pcm, pcm_pos](buffer_trait<ValueType, ChannelCount>::SpanPairType spans, buffer_info info) mutable {
		UNREFERENCED_PARAMETER(info);

		const auto pcm_data = pcm.data();
		const auto pcm_size = pcm.size();

		for (const auto span : spans) {
			const auto span_data = reinterpret_cast<byte*>(span.data());
			const auto span_size = size_t(span.size_bytes());
			size_t span_pos = 0;

			while (span_pos < span_size) {
				const auto pcm_remaining = pcm_size - pcm_pos;
				const auto span_remaining = span_size - span_pos;
				const auto remaining = std::min(pcm_remaining, span_remaining);
				
				memcpy(span_data, pcm_data, remaining);

				span_pos += remaining;
				pcm_pos = (pcm_pos + remaining) % pcm_size;
			}
		}
	};
}

template<typename ValueType, size_t ChannelCount>
auto create_sine_wave_provider(size_t frequency) {
	uint32_t sample_number = 0;

	return [frequency, sample_number](buffer_trait<ValueType, ChannelCount>::SpanPairType spans, buffer_info info) mutable {
		sample_number = detail::fill_with_sine_wave<ValueType, ChannelCount>(spans, info, frequency, sample_number);
	};
}

template<typename ValueType, size_t ChannelCount>
auto create_tone_ladder_provider(std::vector<size_t> frequencies) {
	if (frequencies.empty()) {
		throw std::invalid_argument("frequencies must not be empty");
	}

	size_t frequency_idx = 0;
	uint32_t sample_number = 0;

	return [frequencies, frequency_idx, sample_number](buffer_trait<ValueType, ChannelCount>::SpanPairType spans, buffer_info info) mutable {
		if (spans[0].size() + spans[1].size() < 2) {
			throw std::invalid_argument("spans contain less than 2 samples");
		}

		detail::fill_with_sine_wave<ValueType, ChannelCount>(spans, info, frequencies[frequency_idx], sample_number);
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

		const auto radiant_periods_per_sample = 2.0 * M_PI * double(frequencies[frequency_idx]) / double(info.samples_per_second);
		const auto sample1d = double(sample1) / double(std::numeric_limits<ValueType>::max());
		const auto new_sample_rad = detail::asin_2pi(sample1d, sample1 < sample2);
		const auto new_sample = uint32_t(round(new_sample_rad / radiant_periods_per_sample));

		sample_number = new_sample;
	};
}

} // namespace direct_sound
