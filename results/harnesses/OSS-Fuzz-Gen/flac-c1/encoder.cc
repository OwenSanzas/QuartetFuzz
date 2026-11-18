#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "FLAC++/encoder.h"
#include "FLAC/metadata.h"

extern "C" {
#include "share/private.h"
}
#include "common.h"

class FuzzStreamEncoder : public FLAC::Encoder::Stream {
public:
	FuzzStreamEncoder(): FLAC::Encoder::Stream(), pos_(0) { }
	~FuzzStreamEncoder() { }

	::FLAC__StreamEncoder *raw() { return encoder_; }
protected:
	::FLAC__StreamEncoderWriteStatus write_callback(const FLAC__byte buffer[], size_t bytes, uint32_t samples, uint32_t current_frame)
	{
		(void)buffer;
		(void)samples;
		(void)current_frame;
		pos_ += bytes;
		return ::FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
	}
	::FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64 absolute_byte_offset)
	{
		pos_ = absolute_byte_offset;
		return ::FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
	}
	::FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64 *absolute_byte_offset)
	{
		*absolute_byte_offset = pos_;
		return ::FLAC__STREAM_ENCODER_TELL_STATUS_OK;
	}
private:
	FLAC__uint64 pos_;
	FuzzStreamEncoder(const FuzzStreamEncoder &);
	FuzzStreamEncoder &operator=(const FuzzStreamEncoder &);
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

	if (size < 21)
		return 0;

	size_t pos = 0;

	uint8_t channels_raw = data[pos++];
	uint32_t channels = 1u + (channels_raw % 8u);

	uint8_t bits_per_sample_raw = data[pos++];
	uint32_t bits_per_sample = 4u + (bits_per_sample_raw % 29u);

	uint32_t sample_rate_raw =
	    (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) |
	    ((uint32_t)data[pos + 2] << 16) | ((uint32_t)data[pos + 3] << 24);
	pos += 4;
	uint32_t sample_rate = 1u + (sample_rate_raw % 655350u);

	uint32_t raw_bs = (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8);
	pos += 2;

	uint32_t compression_level = data[pos++] % 9u;

	uint8_t flags1 = data[pos++];
	uint8_t apod_choice = data[pos++];
	uint8_t max_lpc_order_raw = data[pos++];
	uint8_t qlp_prec_raw = data[pos++];
	uint8_t min_res_raw = data[pos++];
	uint8_t max_res_raw = data[pos++];
	uint8_t rice_dist_raw = data[pos++];
	uint8_t num_threads_raw = data[pos++];
	uint8_t sample_mode_raw = data[pos++];
	uint8_t chunk_raw = data[pos++];
	uint8_t extra_byte = data[pos++];
	uint8_t extra_byte2 = data[pos++];

	uint32_t num_threads = 1u + (num_threads_raw % 4u);

	if (num_threads > 1 && (chunk_raw & 0x2))
		channels = 1u + (channels_raw % 2u);

	if ((extra_byte2 & 0x40) != 0) {
		channels = 2;
		bits_per_sample = 32;
	}

	FuzzStreamEncoder encoder;

	if (!encoder.is_valid())
		return 0;

	encoder.set_channels(channels);
	encoder.set_bits_per_sample(bits_per_sample);
	encoder.set_sample_rate(sample_rate);
	encoder.set_compression_level(compression_level);

	bool keep_subset = (flags1 % 5 == 0);
	if (!keep_subset)
		encoder.set_streamable_subset(false);

	uint32_t blocksize;
	if ((chunk_raw & 0x7) == 0)
		blocksize = raw_bs;
	else
		blocksize = 16u + (raw_bs % 256u);

	if (blocksize >= 16 && blocksize <= 65535) {
		encoder.set_blocksize(blocksize);

		uint32_t max_lpc_order = max_lpc_order_raw % 33u;
		if (max_lpc_order > blocksize)
			max_lpc_order = blocksize;
		encoder.set_max_lpc_order(max_lpc_order);
	} else {
		encoder.set_max_lpc_order(max_lpc_order_raw % 33u);
	}

	encoder.set_verify((flags1 & 0x01) != 0);
	encoder.set_do_mid_side_stereo((flags1 & 0x02) != 0);
	encoder.set_loose_mid_side_stereo((flags1 & 0x04) != 0);
	encoder.set_do_qlp_coeff_prec_search((flags1 & 0x08) != 0);
	encoder.set_do_escape_coding((flags1 & 0x10) != 0);
	encoder.set_do_exhaustive_model_search((flags1 & 0x20) != 0);
	encoder.set_limit_min_bitrate((flags1 & 0x80) != 0);

	if (qlp_prec_raw % 3 == 0)
		encoder.set_qlp_coeff_precision(0);
	else
		encoder.set_qlp_coeff_precision(5u + (qlp_prec_raw % 11u));

	uint32_t min_res = min_res_raw % 8u;
	uint32_t max_res = min_res + (max_res_raw % (16u - min_res));
	encoder.set_min_residual_partition_order(min_res);
	encoder.set_max_residual_partition_order(max_res);
	encoder.set_rice_parameter_search_dist(rice_dist_raw % 34u);
	encoder.set_num_threads(num_threads);

	static const char *apod_specs[] = {
		"tukey(0.5)",
		"subdivide_tukey(4)",
		"partial_tukey(2)",
		"punchout_tukey(3)",
		"bartlett;bartlett_hann;blackman;blackman_harris_4term_92db;"
		"connes;flattop;gauss(0.2);hamming;hann;kaiser_bessel;"
		"nuttall;rectangle;triangle;welch"
	};
	encoder.set_apodization(apod_specs[apod_choice % 5]);

	FLAC__stream_encoder_set_do_md5(encoder.raw(), (FLAC__bool)((extra_byte2 & 0x01) != 0));

	FLAC__stream_encoder_disable_constant_subframes(encoder.raw(), (FLAC__bool)((extra_byte2 & 0x02) != 0));
	FLAC__stream_encoder_disable_fixed_subframes(encoder.raw(),    (FLAC__bool)((extra_byte2 & 0x04) != 0));
	FLAC__stream_encoder_disable_verbatim_subframes(encoder.raw(), (FLAC__bool)((extra_byte2 & 0x08) != 0));

	FLAC__stream_encoder_disable_instruction_set(encoder.raw(), (FLAC__bool)(extra_byte2 & 0x7F));

	::FLAC__StreamMetadata *seektable = NULL;
	bool use_seektable = (extra_byte & 0x02) != 0;
	if (use_seektable) {
		seektable = FLAC__metadata_object_new(FLAC__METADATA_TYPE_SEEKTABLE);
		if (seektable) {
			uint32_t n_points = 1u + (extra_byte % 8u);
			FLAC__metadata_object_seektable_template_append_spaced_points(
			    seektable, n_points,  1000);
			FLAC__metadata_object_seektable_template_sort(seektable,  true);
			::FLAC__StreamMetadata *blocks[1];
			blocks[0] = seektable;
			encoder.set_metadata(blocks, 1);
		}
	}

	bool use_ogg = (extra_byte2 & 0x80) != 0;
	::FLAC__StreamEncoderInitStatus init_status =
	    use_ogg ? encoder.init_ogg() : encoder.init();
	if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
		if (seektable)
			FLAC__metadata_object_delete(seektable);
		return 0;
	}

	FLAC__int32 dummy = 0;
	(void)encoder.process_interleaved(&dummy, 0);

	int32_t sample_min = INT32_MIN >> (32 - bits_per_sample);
	int32_t sample_max = INT32_MAX >> (32 - bits_per_sample);
	int64_t range = (int64_t)sample_max - (int64_t)sample_min + 1;

	size_t remaining = (pos < size) ? (size - pos) : 0;
	const uint8_t *pcm_data = data + pos;

	uint32_t samples = 0;
	if (channels > 0)
		samples = (uint32_t)(remaining / sizeof(int32_t) / channels);

	if (samples > 0) {
		std::vector<FLAC__int32> buffer((size_t)samples * channels);
		uint32_t sample_mode = sample_mode_raw % 6u;

		if (sample_mode == 1) {

			std::vector<FLAC__int32> const_val(channels);
			for (uint32_t c = 0; c < channels; ++c) {
				int32_t raw;
				size_t off = (size_t)c * sizeof(int32_t);
				if (off + sizeof(int32_t) <= remaining)
					memcpy(&raw, pcm_data + off, sizeof(int32_t));
				else
					raw = 0;
				int64_t val = (int64_t)raw % range;
				if (val < 0) val += range;
				const_val[c] = (FLAC__int32)(sample_min + val);
			}
			for (size_t i = 0; i < buffer.size(); ++i)
				buffer[i] = const_val[i % channels];
		} else if (sample_mode == 2) {

			for (size_t i = 0; i < buffer.size(); ++i)
				buffer[i] = 0;
		} else if (sample_mode == 4) {

			uint32_t clear_bits = 0;
			if (bits_per_sample > 1)
				clear_bits = extra_byte % (bits_per_sample - 1);
			uint32_t mask = (clear_bits > 0)
			    ? ~((1u << clear_bits) - 1u)
			    : 0xFFFFFFFFu;
			for (size_t i = 0; i < buffer.size(); ++i) {
				int32_t raw;
				size_t off = (i * sizeof(int32_t)) % remaining;
				memcpy(&raw, pcm_data + off, sizeof(int32_t));
				int64_t val = (int64_t)raw % range;
				if (val < 0) val += range;
				FLAC__int32 v = (FLAC__int32)(sample_min + val);

				v = (FLAC__int32)(((uint32_t)v) & mask);
				buffer[i] = v;
			}
		} else if (sample_mode == 5 && channels >= 2) {

			for (uint32_t s = 0; s < samples; ++s) {
				int32_t raw;
				size_t off = (size_t)s * sizeof(int32_t);
				if (off + sizeof(int32_t) <= remaining)
					memcpy(&raw, pcm_data + off, sizeof(int32_t));
				else
					raw = 0;
				int64_t val = (int64_t)raw % range;
				if (val < 0) val += range;
				FLAC__int32 base = (FLAC__int32)(sample_min + val);
				buffer[(size_t)s * channels + 0] = base;
				for (uint32_t c = 1; c < channels; ++c) {
					int64_t off_val = (c == 1 && (extra_byte & 1)) ? 1 : 0;
					int64_t v = (int64_t)base + off_val;
					if (v > sample_max) v = sample_max;
					if (v < sample_min) v = sample_min;
					buffer[(size_t)s * channels + c] = (FLAC__int32)v;
				}
			}
		} else {
			for (size_t i = 0; i < buffer.size(); ++i) {
				int32_t raw;
				memcpy(&raw, pcm_data + i * sizeof(int32_t), sizeof(int32_t));
				if (sample_mode == 3 && (raw & 0x7) == 0) {

					buffer[i] = raw;
				} else {
					int64_t val = (int64_t)raw % range;
					if (val < 0)
						val += range;
					buffer[i] = (FLAC__int32)(sample_min + val);
				}
			}
		}

		uint32_t chunk = 1u + (chunk_raw % 251u);
		uint32_t done = 0;
		while (done < samples) {
			uint32_t n = samples - done;
			if (n > chunk)
				n = chunk;
			bool ok = encoder.process_interleaved(
			    buffer.data() + (size_t)done * channels, n);
			done += n;
			if (!ok)
				break;
		}
	}

	encoder.finish();

	if (seektable)
		FLAC__metadata_object_delete(seektable);

	return 0;
}