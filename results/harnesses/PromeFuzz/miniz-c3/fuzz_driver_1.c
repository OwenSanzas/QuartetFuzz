#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "build/amalgamation/miniz.h"

static void try_deflate_run(const uint8_t *Data, size_t Size, int level, int flush_mode, int windowBits, int memLevel, int strategy)
{
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));

    int init_ret = mz_deflateInit2(&stream, level, MZ_DEFLATED, windowBits, memLevel, strategy);
    if (init_ret != MZ_OK) {
        return;
    }

    unsigned char *in_buf = NULL;
    if (Size > 0) {
        in_buf = (unsigned char *)malloc(Size);
        if (!in_buf) {
            mz_deflateEnd(&stream);
            return;
        }
        memcpy(in_buf, Data, Size);
    }

    stream.next_in = in_buf;
    stream.avail_in = (unsigned int)Size;

    size_t out_cap = Size + 256;
    unsigned char *out_buf = (unsigned char *)malloc(out_cap);
    if (!out_buf) {
        if (in_buf) free(in_buf);
        mz_deflateEnd(&stream);
        return;
    }

    stream.next_out = out_buf;
    stream.avail_out = (unsigned int)out_cap;

    int flush_vals[] = {MZ_NO_FLUSH, MZ_PARTIAL_FLUSH, MZ_SYNC_FLUSH, MZ_FULL_FLUSH, MZ_FINISH};
    int chosen_flush = flush_vals[flush_mode % 5];

    int iterations = 0;
    int ret;
    do {

        if (iterations == 1 && out_cap > 8) {

            stream.avail_out = 8;
        }

        ret = mz_deflate(&stream, chosen_flush);

        if (ret == MZ_STREAM_END) {
            break;
        }
        if (ret == MZ_BUF_ERROR) {

            if (stream.avail_out == 0) {
                stream.next_out = out_buf;
                stream.avail_out = (unsigned int)out_cap;
            } else {
                break;
            }
        }
        if (ret == MZ_STREAM_ERROR || ret == MZ_PARAM_ERROR) {
            break;
        }

        iterations++;
    } while (iterations < 10);

    stream.next_out = out_buf;
    stream.avail_out = (unsigned int)out_cap;
    ret = mz_deflate(&stream, MZ_FINISH);
    (void)ret;

    mz_deflateEnd(&stream);

    if (in_buf) free(in_buf);
    free(out_buf);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0) {
        return 0;
    }

    uint8_t b0 = Data[0];
    uint8_t b1 = (Size > 1) ? Data[1] : 0;
    uint8_t b2 = (Size > 2) ? Data[2] : 0;
    uint8_t b3 = (Size > 3) ? Data[3] : 0;

    int level = (b0 % 11) - 1;
    int flush_mode = b1;
    int windowBits_choice = b2 % 4;
    int windowBits;
    switch (windowBits_choice) {
        case 0: windowBits = -15; break;
        case 1: windowBits = 15; break;
        case 2: windowBits = 47; break;
        default: windowBits = 9; break;
    }
    int memLevel = (b3 % 9) + 1;
    int strategy_choice = (Size > 4) ? Data[4] % 5 : 0;
    int strategy;
    switch (strategy_choice) {
        case 0: strategy = MZ_DEFAULT_STRATEGY; break;
        case 1: strategy = MZ_FILTERED; break;
        case 2: strategy = MZ_HUFFMAN_ONLY; break;
        case 3: strategy = MZ_RLE; break;
        default: strategy = MZ_FIXED; break;
    }

    const uint8_t *payload = Data;
    size_t payload_size = Size;
    if (Size > 5) {
        payload = Data + 5;
        payload_size = Size - 5;
    }

    try_deflate_run(payload, payload_size, level, flush_mode, windowBits, memLevel, strategy);

    {
        mz_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (mz_deflateInit(&stream, MZ_DEFAULT_COMPRESSION) == MZ_OK) {
            unsigned char out_buf[256];
            size_t offset = 0;
            size_t chunk = 7;
            int ret;
            while (offset < payload_size) {
                size_t this_chunk = (payload_size - offset < chunk) ? (payload_size - offset) : chunk;
                stream.next_in = payload + offset;
                stream.avail_in = (unsigned int)this_chunk;
                offset += this_chunk;

                int is_last = (offset >= payload_size);
                int flush_type = is_last ? MZ_FINISH : MZ_NO_FLUSH;

                do {
                    stream.next_out = out_buf;
                    stream.avail_out = sizeof(out_buf);
                    ret = mz_deflate(&stream, flush_type);
                } while (ret == MZ_OK && stream.avail_out == 0);

                if (ret == MZ_STREAM_END || ret == MZ_STREAM_ERROR || ret == MZ_PARAM_ERROR) {
                    break;
                }
            }
            mz_deflateEnd(&stream);
        }
    }

    return 0;
}