#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

static void try_deflate_config(const uint8_t *Data, size_t Size,
                                int level, int method, int windowBits,
                                int memLevel, int strategy) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit2(&strm, level, method, windowBits, memLevel, strategy);
    if (ret != Z_OK) {
        return;
    }

    uInt avail_in = (uInt)Size;
    Bytef *input_buf = (Bytef *)malloc(Size > 0 ? Size : 1);
    if (input_buf == NULL) {
        deflateEnd(&strm);
        return;
    }
    if (Size > 0) {
        memcpy(input_buf, Data, Size);
    }

    strm.next_in = input_buf;
    strm.avail_in = avail_in;

    uLong out_bound = deflateBound(&strm, avail_in);
    if (out_bound < 64) {
        out_bound = 64;
    }
    Bytef *output_buf = (Bytef *)malloc(out_bound);
    if (output_buf == NULL) {
        free(input_buf);
        deflateEnd(&strm);
        return;
    }

    strm.next_out = output_buf;
    strm.avail_out = (uInt)out_bound;

    int flush_options[] = {Z_NO_FLUSH, Z_PARTIAL_FLUSH, Z_SYNC_FLUSH,
                            Z_FULL_FLUSH, Z_FINISH, Z_BLOCK};
    int num_flush_options = sizeof(flush_options) / sizeof(flush_options[0]);

    int flush_idx = 0;
    int iterations = 0;
    int max_iterations = 20;

    do {
        int flush = flush_options[flush_idx % num_flush_options];
        flush_idx++;

        ret = deflate(&strm, flush);

        if (ret == Z_STREAM_ERROR) {
            break;
        }

        if (strm.avail_out == 0) {
            strm.next_out = output_buf;
            strm.avail_out = (uInt)out_bound;
        }

        iterations++;

        if (ret == Z_STREAM_END) {
            break;
        }

    } while (iterations < max_iterations);

    if (ret != Z_STREAM_END && ret != Z_STREAM_ERROR) {
        strm.next_out = output_buf;
        strm.avail_out = (uInt)out_bound;
        deflate(&strm, Z_FINISH);
    }

    free(input_buf);
    free(output_buf);
    deflateEnd(&strm);
}

static void fuzz_deflate_basic(const uint8_t *Data, size_t Size) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        return;
    }

    uInt avail_in = (uInt)Size;
    Bytef *input_buf = (Bytef *)malloc(Size > 0 ? Size : 1);
    if (input_buf == NULL) {
        deflateEnd(&strm);
        return;
    }
    if (Size > 0) {
        memcpy(input_buf, Data, Size);
    }

    strm.next_in = input_buf;
    strm.avail_in = avail_in;

    uLong out_bound = deflateBound(&strm, avail_in);
    if (out_bound < 64) {
        out_bound = 64;
    }
    Bytef *output_buf = (Bytef *)malloc(out_bound);
    if (output_buf == NULL) {
        free(input_buf);
        deflateEnd(&strm);
        return;
    }

    strm.next_out = output_buf;
    strm.avail_out = (uInt)out_bound;

    ret = deflate(&strm, Z_FINISH);

    if (ret != Z_STREAM_END) {

        while (ret == Z_OK) {
            if (strm.avail_out == 0) {
                strm.next_out = output_buf;
                strm.avail_out = (uInt)out_bound;
            }
            ret = deflate(&strm, Z_FINISH);
        }
    }

    free(input_buf);
    free(output_buf);
    deflateEnd(&strm);
}

static void fuzz_deflate_small_output(const uint8_t *Data, size_t Size) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    int ret = deflateInit(&strm, Z_BEST_COMPRESSION);
    if (ret != Z_OK) {
        return;
    }

    uInt avail_in = (uInt)Size;
    Bytef *input_buf = (Bytef *)malloc(Size > 0 ? Size : 1);
    if (input_buf == NULL) {
        deflateEnd(&strm);
        return;
    }
    if (Size > 0) {
        memcpy(input_buf, Data, Size);
    }

    strm.next_in = input_buf;
    strm.avail_in = avail_in;

    uInt small_out_size = 16;
    Bytef output_buf[16];

    int max_loops = 1000;
    int loop_count = 0;

    do {
        strm.next_out = output_buf;
        strm.avail_out = small_out_size;

        ret = deflate(&strm, Z_FINISH);

        loop_count++;

        if (ret == Z_STREAM_ERROR) {
            break;
        }

    } while (ret != Z_STREAM_END && loop_count < max_loops);

    free(input_buf);
    deflateEnd(&strm);
}

static void fuzz_deflate_null_checks(const uint8_t *Data, size_t Size) {

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = deflate(&strm, Z_NO_FLUSH);
    (void)ret;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        return;
    }

    strm.next_in = (Bytef *)Data;
    strm.avail_in = 0;

    Bytef output_buf[64];
    strm.next_out = output_buf;
    strm.avail_out = sizeof(output_buf);

    deflate(&strm, Z_NO_FLUSH);
    deflate(&strm, Z_FINISH);

    deflateEnd(&strm);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    fuzz_deflate_basic(Data, Size);

    fuzz_deflate_small_output(Data, Size);

    fuzz_deflate_null_checks(Data, Size);

    if (Size >= 5) {
        uint8_t config_byte = Data[0];

        int level = (config_byte % 10) - 1;
        if (level < -1) level = -1;
        if (level > 9) level = 9;

        int method = Z_DEFLATED;

        int windowBits = 9 + (Data[1] % 7);

        int memLevel = 1 + (Data[2] % 9);

        int strategy_options[] = {Z_DEFAULT_STRATEGY, Z_FILTERED,
                                   Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED};
        int strategy = strategy_options[Data[3] % 5];

        const uint8_t *remaining_data = Data + 5;
        size_t remaining_size = Size - 5;

        try_deflate_config(remaining_data, remaining_size, level, method,
                           windowBits, memLevel, strategy);

        int raw_windowBits = -(9 + (Data[1] % 7));
        try_deflate_config(remaining_data, remaining_size, level, method,
                           raw_windowBits, memLevel, strategy);

        int gzip_windowBits = 16 + (9 + (Data[1] % 7));
        try_deflate_config(remaining_data, remaining_size, level, method,
                           gzip_windowBits, memLevel, strategy);
    }

    return 0;
}