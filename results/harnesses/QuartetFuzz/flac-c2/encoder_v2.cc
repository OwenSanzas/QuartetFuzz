#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "FLAC/stream_encoder.h"
#include "FLAC/metadata.h"
#include "common.h"

namespace {

struct WriteCtx {
	std::vector<uint8_t> buf;
	size_t offset = 0;
	uint64_t checksum = 0;

	FLAC__uint64 reported_total_samples = 0;
	uint32_t reported_min_framesize = 0;
	uint32_t reported_max_framesize = 0;
	bool got_metadata_callback = false;
};

FLAC__StreamEncoderWriteStatus write_cb(
	const FLAC__StreamEncoder *  ,
	const FLAC__byte buffer[],
	size_t bytes,
	uint32_t  ,
	uint32_t  ,
	void *client_data)
{
	WriteCtx *ctx = static_cast<WriteCtx *>(client_data);
	if (ctx->offset + bytes > ctx->buf.size())
		ctx->buf.resize(ctx->offset + bytes);
	if (bytes > 0)
		memcpy(ctx->buf.data() + ctx->offset, buffer, bytes);

	for (size_t i = 0; i < bytes; i++)
		ctx->checksum += buffer[i];
	ctx->offset += bytes;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

FLAC__StreamEncoderSeekStatus seek_cb(
	const FLAC__StreamEncoder *  ,
	FLAC__uint64 absolute_byte_offset,
	void *client_data)
{
	WriteCtx *ctx = static_cast<WriteCtx *>(client_data);
	if (absolute_byte_offset > ctx->buf.size())
		return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
	ctx->offset = static_cast<size_t>(absolute_byte_offset);
	return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderTellStatus tell_cb(
	const FLAC__StreamEncoder *  ,
	FLAC__uint64 *absolute_byte_offset,
	void *client_data)
{
	WriteCtx *ctx = static_cast<WriteCtx *>(client_data);
	*absolute_byte_offset = ctx->offset;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

FLAC__StreamEncoderReadStatus read_cb(
	const FLAC__StreamEncoder *  ,
	FLAC__byte buffer[],
	size_t *bytes,
	void *client_data)
{
	WriteCtx *ctx = static_cast<WriteCtx *>(client_data);
	if (ctx->offset >= ctx->buf.size()) {
		*bytes = 0;
		return FLAC__STREAM_ENCODER_READ_STATUS_END_OF_STREAM;
	}
	size_t avail = ctx->buf.size() - ctx->offset;
	size_t n = std::min(avail, *bytes);
	memcpy(buffer, ctx->buf.data() + ctx->offset, n);
	ctx->offset += n;
	*bytes = n;
	return FLAC__STREAM_ENCODER_READ_STATUS_CONTINUE;
}

void metadata_cb(
	const FLAC__StreamEncoder *  ,
	const FLAC__StreamMetadata *metadata,
	void *client_data)
{
	WriteCtx *ctx = static_cast<WriteCtx *>(client_data);
	ctx->got_metadata_callback = true;
	if (metadata && metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		ctx->reported_total_samples = metadata->data.stream_info.total_samples;
		ctx->reported_min_framesize = metadata->data.stream_info.min_framesize;
		ctx->reported_max_framesize = metadata->data.stream_info.max_framesize;
	}
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

	if (size < 18)
		return 0;

	size_t pos = 0;
	auto next_u8 = [&]() -> uint8_t { return data[pos++]; };

	const uint8_t channels_raw   = next_u8();
	const uint8_t bps_raw        = next_u8();
	const uint8_t comp_raw       = next_u8();
	const uint8_t flags          = next_u8();
	const uint8_t blocksize_lo   = next_u8();
	const uint8_t blocksize_hi   = next_u8();
	const uint8_t max_lpc_raw    = next_u8();
	const uint8_t min_part_raw   = next_u8();
	const uint8_t max_part_raw   = next_u8();
	const uint8_t apod_choice    = next_u8();
	const uint8_t sr_b0          = next_u8();
	const uint8_t sr_b1          = next_u8();
	const uint8_t is_ogg_byte    = next_u8();
	const uint8_t mask_byte      = next_u8();
	const uint8_t chunk_raw      = next_u8();
	const uint8_t qlp_prec_raw   = next_u8();
	const uint8_t num_threads_raw = next_u8();
	const uint8_t seektable_raw  = next_u8();

	const uint32_t channels = 1u + (channels_raw % 8u);
	const uint32_t bps = 4u + (bps_raw % 29u);
	const uint32_t compression = comp_raw % 9u;
	const bool do_mss = (flags & 0x01) != 0;
	const bool loose_mss = (flags & 0x02) != 0;
	const bool verify = (flags & 0x04) != 0;
	const bool do_exhaustive = (flags & 0x08) != 0;
	const bool use_seek_tell = (flags & 0x10) != 0;
	const bool use_seektable = (flags & 0x20) != 0;
	const bool is_ogg = (is_ogg_byte & 0x01) != 0;
	const bool mask_samples = (mask_byte & 0x01) != 0;

	uint32_t blocksize = (static_cast<uint32_t>(blocksize_hi) << 8) | blocksize_lo;
	if (blocksize != 0)
		blocksize = 16u + (blocksize % (65535u - 16u + 1u));

	const uint32_t max_lpc_order = max_lpc_raw % 33u;
	const uint32_t min_part = min_part_raw % 9u;
	const uint32_t max_part = max_part_raw % 9u;
	const uint32_t sample_rate = (static_cast<uint32_t>(sr_b1) << 8) | sr_b0;
	const uint32_t qlp_precision = qlp_prec_raw % 33u;
	const uint32_t num_threads = 1u + (num_threads_raw % 4u);
	const uint32_t num_seekpoints = seektable_raw % 8u;

	FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
	if (!encoder)
		return 0;

	FLAC__stream_encoder_set_channels(encoder, channels);
	FLAC__stream_encoder_set_bits_per_sample(encoder, bps);

	FLAC__stream_encoder_set_compression_level(encoder, compression);
	FLAC__stream_encoder_set_do_mid_side_stereo(encoder, do_mss);
	FLAC__stream_encoder_set_loose_mid_side_stereo(encoder, loose_mss);
	FLAC__stream_encoder_set_verify(encoder, verify);
	FLAC__stream_encoder_set_do_exhaustive_model_search(encoder, do_exhaustive);
	FLAC__stream_encoder_set_blocksize(encoder, blocksize);
	FLAC__stream_encoder_set_max_lpc_order(encoder, max_lpc_order);
	FLAC__stream_encoder_set_min_residual_partition_order(encoder, min_part);
	FLAC__stream_encoder_set_max_residual_partition_order(encoder, max_part);
	FLAC__stream_encoder_set_sample_rate(encoder, sample_rate);
	FLAC__stream_encoder_set_qlp_coeff_precision(encoder, qlp_precision);
	FLAC__stream_encoder_set_num_threads(encoder, num_threads);

	static const char *apods[] = {
		"tukey(0.5)", "partial_tukey(2)", "punchout_tukey(3)",
		"hann", "flattop", "", "subdivide_tukey(3)", "welch",
	};
	FLAC__stream_encoder_set_apodization(
		encoder, apods[apod_choice % (sizeof(apods) / sizeof(apods[0]))]);

	FLAC__StreamMetadata *seektable = nullptr;
	FLAC__StreamMetadata *metadata_blocks[1];
	uint32_t num_metadata_blocks = 0;
	if (use_seektable && num_seekpoints > 0) {
		seektable = FLAC__metadata_object_new(FLAC__METADATA_TYPE_SEEKTABLE);
		if (seektable) {
			FLAC__metadata_object_seektable_template_append_spaced_points(
				seektable, num_seekpoints, 1u << 20  );
			FLAC__metadata_object_seektable_template_sort(seektable,  true);
			metadata_blocks[0] = seektable;
			num_metadata_blocks = 1;
			FLAC__stream_encoder_set_metadata(encoder, metadata_blocks, num_metadata_blocks);
		}
	}

	WriteCtx ctx;
	FLAC__StreamEncoderInitStatus init_status;
	FLAC__StreamEncoderSeekCallback seek_callback = use_seek_tell ? seek_cb : nullptr;
	FLAC__StreamEncoderTellCallback tell_callback = use_seek_tell ? tell_cb : nullptr;
	if (is_ogg) {

		FLAC__StreamEncoderReadCallback read_callback = use_seek_tell ? read_cb : nullptr;
		init_status = FLAC__stream_encoder_init_ogg_stream(
			encoder, read_callback, write_cb,
			seek_callback, tell_callback,
			metadata_cb, &ctx);
	} else {
		init_status = FLAC__stream_encoder_init_stream(
			encoder, write_cb, seek_callback, tell_callback,
			metadata_cb, &ctx);
	}

	bool any_process_failed = false;
	if (init_status == FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
		const size_t remaining = size - pos;
		const size_t nvals = remaining / sizeof(FLAC__int32);
		std::vector<FLAC__int32> samples(nvals);
		if (nvals > 0)
			memcpy(samples.data(), data + pos, nvals * sizeof(FLAC__int32));

		if (mask_samples && bps > 0 && bps <= 32) {
			const uint32_t shift = 32u - bps;
			for (auto &v : samples)
				v = static_cast<FLAC__int32>(
					(static_cast<uint32_t>(v) << shift) >> shift);
		}

		const size_t total_samples_per_channel = samples.size() / channels;

		if (total_samples_per_channel > 0) {

			const size_t chunk = 1u + (chunk_raw % 64u);
			size_t offset = 0;
			while (offset < total_samples_per_channel) {
				const size_t this_count =
					std::min(chunk, total_samples_per_channel - offset);
				const FLAC__int32 *ptr = samples.data() + offset * channels;
				const FLAC__bool ok = FLAC__stream_encoder_process_interleaved(
					encoder, ptr, static_cast<uint32_t>(this_count));
				offset += this_count;
				if (!ok) {

					any_process_failed = true;
					volatile FLAC__StreamEncoderState err_state =
						FLAC__stream_encoder_get_state(encoder);
					(void)err_state;
					break;
				}
			}

			if (FLAC__stream_encoder_get_state(encoder) == FLAC__STREAM_ENCODER_OK) {
				std::vector<std::vector<FLAC__int32>> per_channel(channels);
				std::vector<const FLAC__int32 *> ptrs(channels);
				for (uint32_t c = 0; c < channels; c++) {
					per_channel[c].resize(total_samples_per_channel);
					for (size_t i = 0; i < total_samples_per_channel; i++)
						per_channel[c][i] = samples[(i * channels + c) % samples.size()];
					ptrs[c] = per_channel[c].data();
				}
				const FLAC__bool ok2 = FLAC__stream_encoder_process(
					encoder, ptrs.data(), static_cast<uint32_t>(total_samples_per_channel));
				if (!ok2) {
					any_process_failed = true;
					volatile FLAC__StreamEncoderState err_state2 =
						FLAC__stream_encoder_get_state(encoder);
					(void)err_state2;
				}
			}
		}
	}
	(void)any_process_failed;

	const FLAC__bool fin = FLAC__stream_encoder_finish(encoder);
	if (!fin) {

		volatile FLAC__StreamEncoderState fin_state =
			FLAC__stream_encoder_get_state(encoder);
		(void)fin_state;
	}
	FLAC__stream_encoder_delete(encoder);

	if (seektable)
		FLAC__metadata_object_delete(seektable);

	volatile uint64_t sink = ctx.checksum ^ static_cast<uint64_t>(ctx.buf.size()) ^
		ctx.reported_total_samples ^
		static_cast<uint64_t>(ctx.reported_min_framesize) ^
		static_cast<uint64_t>(ctx.reported_max_framesize) ^
		static_cast<uint64_t>(ctx.got_metadata_callback);
	(void)sink;

	return 0;
}
