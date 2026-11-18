#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "FLAC/stream_decoder.h"

extern "C" {
    int alloc_check_threshold = -1;
    int alloc_check_counter = 0;
    int alloc_check_keep_failing = 0;
}

static void dummy_write_callback_metadata(const FLAC__StreamDecoder *decoder,
                                           const FLAC__StreamMetadata *metadata,
                                           void *client_data) {
    (void)decoder;
    (void)metadata;
    (void)client_data;
}

static void dummy_error_callback(const FLAC__StreamDecoder *decoder,
                                  FLAC__StreamDecoderErrorStatus status,
                                  void *client_data) {
    (void)decoder;
    (void)status;
    (void)client_data;
}

static FLAC__StreamDecoderWriteStatus dummy_write_callback(
    const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame,
    const FLAC__int32 * const buffer[],
    void *client_data) {
    (void)decoder;
    (void)frame;
    (void)buffer;
    (void)client_data;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    const char *filename = "./dummy_file";
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        return 0;
    }
    fwrite(Data, 1, Size, fp);
    fclose(fp);

    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) {
        remove(filename);
        return 0;
    }

    FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_file(
        decoder,
        filename,
        dummy_write_callback,
        dummy_write_callback_metadata,
        dummy_error_callback,
        nullptr);

    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(decoder);
        remove(filename);
        return 0;
    }

    int max_iterations = 64;
    while (max_iterations-- > 0) {
        FLAC__bool result = FLAC__stream_decoder_process_single(decoder);

        FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(decoder);

        if (!result) {
            break;
        }

        if (state == FLAC__STREAM_DECODER_END_OF_STREAM ||
            state == FLAC__STREAM_DECODER_ABORTED ||
            state == FLAC__STREAM_DECODER_OGG_ERROR ||
            state == FLAC__STREAM_DECODER_SEEK_ERROR ||
            state == FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR) {
            break;
        }
    }

    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);

    remove(filename);

    return 0;
}