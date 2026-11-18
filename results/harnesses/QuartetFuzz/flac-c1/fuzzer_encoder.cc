#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "FLAC++/encoder.h"
#include "FLAC/metadata.h"
#include "common.h"

namespace {

class FuzzEncoder : public FLAC::Encoder::Stream {
public:
	FuzzEncoder() : FLAC::Encoder::Stream() {}

	std::vector<uint8_t> out;

protected:
	::FLAC__StreamEncoderWriteStatus write_callback(const FLAC__byte buffer[], size_t bytes, uint32_t  , uint32_t  ) override
	{
		if (bytes > 0)
			out.insert(out.end(), buffer, buffer + bytes);
		return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
	}

	::FLAC__StreamEncoderSeekStatus seek_callback(FLAC__uint64 absolute_byte_offset) override
	{
		if (absolute_byte_offset > out.size())
			out.resize((size_t)absolute_byte_offset);
		seek_pos_ = absolute_byte_offset;
		return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
	}

	::FLAC__StreamEncoderTellStatus tell_callback(FLAC__uint64 *absolute_byte_offset) override
	{
		*absolute_byte_offset = seek_pos_ ? seek_pos_ : (FLAC__uint64)out.size();
		return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
	}

	void metadata_callback(const ::FLAC__StreamMetadata *  ) override
	{

	}

private:
	FLAC__uint64 seek_pos_ = 0;
};

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

	if (size < 26)
		return 0;

	size_t pos = 0;
	auto byte = [&](void) -> uint8_t { return data[pos++]; };

	uint32_t sample_rate = ((uint32_t)byte() << 16) | ((uint32_t)byte() << 8) | byte();
	if (sample_rate == 0)
		sample_rate = 44100;
	uint8_t channels_raw = byte();
	uint32_t channels = 1 + (channels_raw % 8);
	uint8_t bps_raw = byte();
	uint32_t bits_per_sample = 4 + (bps_raw % 29);
	uint8_t compression_level = byte() % 9;
	uint8_t blocksize_lo = byte();
	uint8_t blocksize_hi = byte();
	uint32_t blocksize = ((uint32_t)blocksize_hi << 8) | blocksize_lo;
	uint8_t num_threads_raw = byte();
	uint32_t num_threads = 1 + (num_threads_raw % 4);
	uint8_t flags1 = byte();
	uint8_t flags2 = byte();
	uint8_t input_width_raw = byte();
	uint32_t input_width = 1 + (input_width_raw % 4);
	uint8_t max_lpc_order = byte();
	uint8_t qlp_coeff_precision = byte();
	uint8_t partition_orders = byte();
	uint32_t min_partition_order = partition_orders & 0x0F;
	uint32_t max_partition_order = (partition_orders >> 4) & 0x0F;
	uint8_t apod_choice = byte() % 16;
	uint8_t apod_param = byte();
	uint8_t total_samples_bytes[5];
	for (int i = 0; i < 5; i++) total_samples_bytes[i] = byte();
	FLAC__uint64 total_samples_estimate = 0;
	for (int i = 0; i < 5; i++)
		total_samples_estimate = (total_samples_estimate << 8) | total_samples_bytes[i];

	bool do_verify = flags1 & 1;
	bool do_streamable_subset = flags1 & 2;
	bool do_limit_min_bitrate = flags1 & 4;
	bool use_ogg = flags1 & 8;
	bool set_blocksize_explicit = flags1 & 16;
	bool override_fine_grained = flags1 & 32;
	bool add_padding = flags1 & 64;
	bool add_vorbis_comment = flags1 & 128;

	bool do_mid_side = flags2 & 1;
	bool loose_mid_side = flags2 & 2;
	bool do_qlp_prec_search = flags2 & 4;
	bool do_escape_coding = flags2 & 8;
	bool do_exhaustive_search = flags2 & 16;
	bool second_call_non_interleaved = flags2 & 32;

	FuzzEncoder encoder;
	if (!encoder.is_valid())
		return 0;

	bool ok = true;
	ok &= encoder.set_verify(do_verify);
	ok &= encoder.set_streamable_subset(do_streamable_subset);
	ok &= encoder.set_channels(channels);
	ok &= encoder.set_bits_per_sample(bits_per_sample);
	ok &= encoder.set_sample_rate(sample_rate);
	ok &= encoder.set_compression_level(compression_level);
	if (set_blocksize_explicit)
		ok &= encoder.set_blocksize(blocksize);
	ok &= encoder.set_total_samples_estimate(total_samples_estimate);
	(void)encoder.set_num_threads(num_threads);
	ok &= encoder.set_limit_min_bitrate(do_limit_min_bitrate);

	if (override_fine_grained) {
		ok &= encoder.set_do_mid_side_stereo(do_mid_side);
		ok &= encoder.set_loose_mid_side_stereo(loose_mid_side);
		ok &= encoder.set_max_lpc_order(max_lpc_order % 33);
		ok &= encoder.set_qlp_coeff_precision(qlp_coeff_precision % 16);
		ok &= encoder.set_do_qlp_coeff_prec_search(do_qlp_prec_search);
		ok &= encoder.set_do_escape_coding(do_escape_coding);
		ok &= encoder.set_do_exhaustive_model_search(do_exhaustive_search);
		ok &= encoder.set_min_residual_partition_order(min_partition_order);
		ok &= encoder.set_max_residual_partition_order(max_partition_order);

		char apod_buf[64];
		switch (apod_choice) {
			case 0:
				std::snprintf(apod_buf, sizeof apod_buf, "tukey(%0.3f)", (double)(apod_param % 101) / 100.0);
				break;
			case 1:
				std::snprintf(apod_buf, sizeof apod_buf, "partial_tukey(%d)", 1 + (apod_param % 6));
				break;
			case 2:
				std::snprintf(apod_buf, sizeof apod_buf, "punchout_tukey(%d)", 1 + (apod_param % 6));
				break;
			case 3:
				std::snprintf(apod_buf, sizeof apod_buf, "subdivide_tukey(%d)", 2 + (apod_param % 6));
				break;
			case 4:
				std::snprintf(apod_buf, sizeof apod_buf, "gauss(%0.3f)", 0.01 + (double)(apod_param % 49) / 100.0);
				break;
			case 5:
				std::snprintf(apod_buf, sizeof apod_buf, "kaiser_bessel");
				break;
			case 6:
				std::snprintf(apod_buf, sizeof apod_buf, "blackman");
				break;
			case 7:
				std::snprintf(apod_buf, sizeof apod_buf, "blackman_harris_4term_92db");
				break;
			case 8:
				std::snprintf(apod_buf, sizeof apod_buf, "connes");
				break;
			case 9:
				std::snprintf(apod_buf, sizeof apod_buf, "flattop");
				break;
			case 10:
				std::snprintf(apod_buf, sizeof apod_buf, "hamming");
				break;
			case 11:
				std::snprintf(apod_buf, sizeof apod_buf, "hann");
				break;
			case 12:
				std::snprintf(apod_buf, sizeof apod_buf, "nuttall");
				break;
			case 13:
				std::snprintf(apod_buf, sizeof apod_buf, "rectangle");
				break;
			case 14:
				std::snprintf(apod_buf, sizeof apod_buf, "triangle");
				break;
			default:
				std::snprintf(apod_buf, sizeof apod_buf, "welch");
				break;
		}
		ok &= encoder.set_apodization(apod_buf);
	}

	FLAC__StreamMetadata *metadata[2] = {nullptr, nullptr};
	unsigned num_metadata = 0;
	if (ok && add_padding) {
		FLAC__StreamMetadata *pad = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
		if (pad) {
			pad->length = 37;
			metadata[num_metadata++] = pad;
		}
	}
	FLAC__StreamMetadata_VorbisComment_Entry vc_entry;
	if (ok && add_vorbis_comment) {
		FLAC__StreamMetadata *vc = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
		if (vc) {
			if (FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&vc_entry, "TITLE", "fuzz")) {
				if (FLAC__metadata_object_vorbiscomment_append_comment(vc, vc_entry,  false)) {
					metadata[num_metadata++] = vc;
				} else {
					free(vc_entry.entry);
					FLAC__metadata_object_delete(vc);
				}
			} else {
				FLAC__metadata_object_delete(vc);
			}
		}
	}

	if (ok && num_metadata > 0)
		ok &= encoder.set_metadata(metadata, num_metadata);

	if (ok) {
		::FLAC__StreamEncoderInitStatus init_status;
		if (use_ogg)
			init_status = encoder.init_ogg();
		else
			init_status = encoder.init();
		ok = (init_status == FLAC__STREAM_ENCODER_INIT_STATUS_OK);
	}

	if (ok) {

		const uint8_t *pcm_data = data + pos;
		size_t pcm_bytes = size - pos;
		size_t per_sample_bytes = (size_t)input_width * channels;
		size_t total_samples_avail = (per_sample_bytes > 0) ? (pcm_bytes / per_sample_bytes) : 0;

		const size_t kMaxSamples = 20000;
		if (total_samples_avail > kMaxSamples)
			total_samples_avail = kMaxSamples;

		if (total_samples_avail > 0) {
			std::vector<FLAC__int32> buf(total_samples_avail * channels);
			FLAC__int32 sample_max = (bits_per_sample >= 32) ? INT32_MAX : (INT32_MAX >> (32 - bits_per_sample));
			FLAC__int32 sample_min = (bits_per_sample >= 32) ? INT32_MIN : (INT32_MIN >> (32 - bits_per_sample));
			for (size_t i = 0; i < total_samples_avail * channels; i++) {
				int64_t raw = 0;
				for (uint32_t b = 0; b < input_width; b++)
					raw = (raw << 8) | pcm_data[i * input_width + b];

				uint32_t raw_bits = input_width * 8;
				if (raw_bits < 64) {
					int64_t signbit = (int64_t)1 << (raw_bits - 1);
					raw = (raw ^ signbit) - signbit;
				}
				if (raw > sample_max) raw = sample_max;
				if (raw < sample_min) raw = sample_min;
				buf[i] = (FLAC__int32)raw;
			}

			uint32_t samples_first = (uint32_t)total_samples_avail;
			ok = encoder.process_interleaved(buf.data(), samples_first);

			if (ok && total_samples_avail > 1) {
				size_t part_samples = total_samples_avail / 2;
				if (part_samples > 0) {
					if (second_call_non_interleaved && channels <= 8) {

						std::vector<FLAC__int32> chan_buf(part_samples * channels);
						const FLAC__int32 *chan_ptrs[8];
						for (uint32_t c = 0; c < channels; c++) {
							chan_ptrs[c] = chan_buf.data() + (size_t)c * part_samples;
						}
						for (size_t s = 0; s < part_samples; s++)
							for (uint32_t c = 0; c < channels; c++)
								chan_buf[(size_t)c * part_samples + s] = buf[s * channels + c];
						ok = encoder.process(chan_ptrs, (uint32_t)part_samples);
					} else {
						ok = encoder.process_interleaved(buf.data(), (uint32_t)part_samples);
					}
				}
			}
		} else {

			FLAC__int32 dummy = 0;
			ok = encoder.process_interleaved(&dummy, 0);
		}
	}

	ok &= encoder.finish();

	FLAC::Encoder::Stream::State state = encoder.get_state();
	const char *state_str = state.as_cstring();
	(void)state_str;
	const char *resolved = state.resolved_as_cstring(encoder);
	(void)resolved;

	if (!ok) {
		FLAC::Decoder::Stream::State verify_state = encoder.get_verify_decoder_state();
		(void)(::FLAC__StreamDecoderState)verify_state;
		FLAC__uint64 absolute_sample = 0;
		uint32_t frame_number = 0, vchannel = 0, vsample = 0;
		FLAC__int32 expected = 0, got = 0;
		encoder.get_verify_decoder_error_stats(&absolute_sample, &frame_number, &vchannel, &vsample, &expected, &got);
		(void)absolute_sample; (void)frame_number; (void)vchannel; (void)vsample; (void)expected; (void)got;
	}

	volatile uint32_t checksum = 0;
	for (uint8_t b : encoder.out)
		checksum += b;
	(void)checksum;

	for (unsigned i = 0; i < num_metadata; i++)
		if (metadata[i])
			FLAC__metadata_object_delete(metadata[i]);

	return 0;
}
