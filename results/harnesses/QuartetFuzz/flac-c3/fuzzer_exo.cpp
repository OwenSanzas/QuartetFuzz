#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common.h"
#include "FLAC/stream_decoder.h"
#include "FLAC/metadata.h"

namespace {

struct DecodeContext {
	const uint8_t *data;
	size_t size;
	size_t pos;
	int write_aborts_left;
	uint64_t samples_seen;
};

extern "C" FLAC__StreamDecoderReadStatus read_cb(
	const FLAC__StreamDecoder *decoder,
	FLAC__byte buffer[],
	size_t *bytes,
	void *client_data)
{
	(void)decoder;
	DecodeContext *ctx = static_cast<DecodeContext *>(client_data);
	size_t remaining = ctx->size - ctx->pos;
	size_t want = *bytes;

	if (remaining == 0) {
		*bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	}

	size_t n = want < remaining ? want : remaining;
	if (n > 0)
		memcpy(buffer, ctx->data + ctx->pos, n);
	ctx->pos += n;
	*bytes = n;
	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

extern "C" FLAC__StreamDecoderSeekStatus seek_cb(
	const FLAC__StreamDecoder *decoder,
	FLAC__uint64 absolute_byte_offset,
	void *client_data)
{
	(void)decoder;
	DecodeContext *ctx = static_cast<DecodeContext *>(client_data);
	if (absolute_byte_offset > ctx->size)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	ctx->pos = static_cast<size_t>(absolute_byte_offset);
	return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

extern "C" FLAC__StreamDecoderTellStatus tell_cb(
	const FLAC__StreamDecoder *decoder,
	FLAC__uint64 *absolute_byte_offset,
	void *client_data)
{
	(void)decoder;
	DecodeContext *ctx = static_cast<DecodeContext *>(client_data);
	*absolute_byte_offset = ctx->pos;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

extern "C" FLAC__StreamDecoderLengthStatus length_cb(
	const FLAC__StreamDecoder *decoder,
	FLAC__uint64 *stream_length,
	void *client_data)
{
	(void)decoder;
	DecodeContext *ctx = static_cast<DecodeContext *>(client_data);
	*stream_length = ctx->size;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

extern "C" FLAC__bool eof_cb(const FLAC__StreamDecoder *decoder, void *client_data)
{
	(void)decoder;
	DecodeContext *ctx = static_cast<DecodeContext *>(client_data);
	return ctx->pos >= ctx->size;
}

extern "C" FLAC__StreamDecoderWriteStatus write_cb(
	const FLAC__StreamDecoder *decoder,
	const FLAC__Frame *frame,
	const FLAC__int32 * const buffer[],
	void *client_data)
{
	(void)decoder;
	DecodeContext *ctx = static_cast<DecodeContext *>(client_data);

	volatile FLAC__int32 acc = 0;
	uint32_t channels = frame->header.channels;
	uint32_t blocksize = frame->header.blocksize;
	for (uint32_t ch = 0; ch < channels; ch++) {
		const FLAC__int32 *samples = buffer[ch];
		if (samples == nullptr)
			continue;
		for (uint32_t i = 0; i < blocksize; i++)
			acc += samples[i];
	}
	(void)acc;

	ctx->samples_seen += blocksize;

	if (ctx->write_aborts_left == 0)
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	if (ctx->write_aborts_left > 0)
		ctx->write_aborts_left--;

	if (ctx->samples_seen > (1u << 20))
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

extern "C" void metadata_cb(
	const FLAC__StreamDecoder *decoder,
	const FLAC__StreamMetadata *metadata,
	void *client_data)
{
	(void)decoder;
	(void)client_data;

	FLAC__StreamMetadata *clone = FLAC__metadata_object_clone(metadata);
	if (clone != nullptr) {
		volatile uint32_t touch = clone->length;
		(void)touch;
		switch (clone->type) {
			case FLAC__METADATA_TYPE_SEEKTABLE: {
				volatile uint32_t n = clone->data.seek_table.num_points;
				for (uint32_t i = 0; i < n; i++) {
					volatile FLAC__uint64 sn = clone->data.seek_table.points[i].sample_number;
					(void)sn;
				}
				break;
			}
			case FLAC__METADATA_TYPE_VORBIS_COMMENT: {
				volatile FLAC__uint32 n = clone->data.vorbis_comment.num_comments;
				for (FLAC__uint32 i = 0; i < n; i++) {
					if (clone->data.vorbis_comment.comments[i].entry != nullptr) {
						volatile FLAC__byte b = clone->data.vorbis_comment.comments[i].entry[0];
						(void)b;
					}
				}
				break;
			}
			case FLAC__METADATA_TYPE_CUESHEET: {
				volatile uint32_t n = clone->data.cue_sheet.num_tracks;
				for (uint32_t i = 0; i < n; i++) {
					volatile FLAC__byte nidx = clone->data.cue_sheet.tracks[i].num_indices;
					(void)nidx;
				}
				break;
			}
			case FLAC__METADATA_TYPE_PICTURE: {
				volatile FLAC__uint32 n = clone->data.picture.data_length;
				if (n > 0 && clone->data.picture.data != nullptr) {
					volatile FLAC__byte b = clone->data.picture.data[0];
					(void)b;
				}
				break;
			}
			default:
				break;
		}
		FLAC__metadata_object_delete(clone);
	}
}

extern "C" void error_cb(
	const FLAC__StreamDecoder *decoder,
	FLAC__StreamDecoderErrorStatus status,
	void *client_data)
{
	(void)decoder;
	(void)client_data;
	(void)status;
}

}

#define CONFIG_LENGTH 2

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < CONFIG_LENGTH + 1)
		return 0;

	uint8_t config_byte = data[0];

	uint8_t command_byte_count = data[1] & 0x1F;

	bool use_ogg = (config_byte & 0x01) != 0;
	bool md5_checking = (config_byte & 0x02) != 0;
	bool respond_all = (config_byte & 0x04) != 0;
	bool ignore_all = (config_byte & 0x08) != 0;
	bool decode_chained = (config_byte & 0x10) != 0;

	size_t remaining = size - CONFIG_LENGTH;
	size_t command_length = command_byte_count;
	if (command_length > remaining)
		command_length = remaining;

	const uint8_t *commands = data + CONFIG_LENGTH;
	const uint8_t *stream_data = data + CONFIG_LENGTH + command_length;
	size_t stream_size = remaining - command_length;

	FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
	if (decoder == nullptr)
		return 0;

	FLAC__stream_decoder_set_md5_checking(decoder, md5_checking);
	if (respond_all)
		FLAC__stream_decoder_set_metadata_respond_all(decoder);
	else if (ignore_all)
		FLAC__stream_decoder_set_metadata_ignore_all(decoder);
	else

		FLAC__stream_decoder_set_metadata_respond(
			decoder, static_cast<FLAC__MetadataType>(config_byte & 0x7F));
	if (decode_chained)
		FLAC__stream_decoder_set_decode_chained_stream(decoder, true);

	DecodeContext ctx;
	ctx.data = stream_data;
	ctx.size = stream_size;
	ctx.pos = 0;
	ctx.write_aborts_left = -1;
	ctx.samples_seen = 0;

	FLAC__StreamDecoderInitStatus init_status;
	if (use_ogg) {
		init_status = FLAC__stream_decoder_init_ogg_stream(
			decoder, read_cb, seek_cb, tell_cb, length_cb, eof_cb,
			write_cb, metadata_cb, error_cb, &ctx);
	} else {
		init_status = FLAC__stream_decoder_init_stream(
			decoder, read_cb, seek_cb, tell_cb, length_cb, eof_cb,
			write_cb, metadata_cb, error_cb, &ctx);
	}

	bool decoder_valid = (init_status == FLAC__STREAM_DECODER_INIT_STATUS_OK);

	for (size_t i = 0; decoder_valid && i < command_length; i++) {
		uint8_t command = commands[i];
		uint8_t op = command & 0x0F;
		uint8_t shift_amount = command >> 4;

		switch (op) {
			case 0:
				decoder_valid = FLAC__stream_decoder_process_until_end_of_stream(decoder);
				break;
			case 1:
				decoder_valid = FLAC__stream_decoder_process_until_end_of_metadata(decoder);
				break;
			case 2:
				decoder_valid = FLAC__stream_decoder_process_single(decoder);
				break;
			case 3:
				decoder_valid = FLAC__stream_decoder_skip_single_frame(decoder);
				break;
			case 4:
				decoder_valid = FLAC__stream_decoder_reset(decoder);
				break;
			case 5:
				decoder_valid = FLAC__stream_decoder_flush(decoder);
				break;
			case 6: {
				FLAC__uint64 target = (FLAC__uint64)1u << shift_amount;
				decoder_valid = FLAC__stream_decoder_seek_absolute(decoder, target);
				break;
			}
			case 7: {

				FLAC__uint64 seekpos = 0;
				for (int b = 0; b < 8 && (i + 1) < command_length; b++) {
					i++;
					seekpos = (seekpos << 8) | commands[i];
				}
				decoder_valid = FLAC__stream_decoder_seek_absolute(decoder, seekpos);
				break;
			}
			case 8:
				ctx.write_aborts_left = shift_amount;
				break;
			case 9:
				decoder_valid = FLAC__stream_decoder_process_until_end_of_link(decoder);
				break;
			case 10:
				if (FLAC__stream_decoder_get_state(decoder) == FLAC__STREAM_DECODER_END_OF_LINK)
					FLAC__stream_decoder_finish_link(decoder);
				break;
			case 11:
				decoder_valid = FLAC__stream_decoder_skip_single_link(decoder);
				break;
			case 12: {
				FLAC__uint64 total = FLAC__stream_decoder_find_total_samples(decoder);
				(void)total;
				FLAC__StreamDecoderState st = FLAC__stream_decoder_get_state(decoder);
				if (st == FLAC__STREAM_DECODER_OGG_ERROR ||
				    st == FLAC__STREAM_DECODER_SEEK_ERROR ||
				    st == FLAC__STREAM_DECODER_ABORTED ||
				    st == FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR ||
				    st == FLAC__STREAM_DECODER_UNINITIALIZED)
					decoder_valid = false;
				break;
			}
			case 13: {
				FLAC__uint64 *link_lengths = nullptr;
				int32_t ret = FLAC__stream_decoder_get_link_lengths(decoder, &link_lengths);
				if (ret == FLAC__STREAM_DECODER_GET_LINK_LENGTHS_MEMORY_ALLOCATION_ERROR)
					decoder_valid = false;
				if (ret > 0 && link_lengths != nullptr) {
					for (int32_t j = 0; j < ret; j++) {
						volatile FLAC__uint64 v = link_lengths[j];
						(void)v;
					}
				}
				free(link_lengths);
				break;
			}
			case 14: {
				FLAC__uint64 decode_pos = 0;
				FLAC__bool got = FLAC__stream_decoder_get_decode_position(decoder, &decode_pos);
				(void)got;
				volatile FLAC__uint64 dp = decode_pos;
				(void)dp;
				const char *resolved = FLAC__stream_decoder_get_resolved_state_string(decoder);
				volatile char first = resolved != nullptr ? resolved[0] : 0;
				(void)first;
				break;
			}
			default:
				break;
		}

		if (!decoder_valid) {
			FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(decoder);
			if (state != FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR &&
			    state != FLAC__STREAM_DECODER_ABORTED) {
				decoder_valid = FLAC__stream_decoder_reset(decoder);
			}
		}
	}

	volatile FLAC__uint64 total_samples = FLAC__stream_decoder_get_total_samples(decoder);
	volatile uint32_t channels = FLAC__stream_decoder_get_channels(decoder);
	volatile uint32_t bps = FLAC__stream_decoder_get_bits_per_sample(decoder);
	volatile uint32_t rate = FLAC__stream_decoder_get_sample_rate(decoder);
	(void)total_samples;
	(void)channels;
	(void)bps;
	(void)rate;

	FLAC__stream_decoder_finish(decoder);
	FLAC__stream_decoder_delete(decoder);

	return 0;
}
