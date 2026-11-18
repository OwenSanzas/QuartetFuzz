#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

static int run_deflate_stream(mz_stream *strm, const uint8_t *payload, size_t payload_len,
                               unsigned num_chunks, size_t chunk_size, unsigned cfg2,
                               unsigned char *out_buf, size_t out_cap,
                               unsigned char **compressed, size_t *compressed_cap,
                               size_t *compressed_len)
{
    (void)num_chunks;
    const uint8_t *cursor = payload;
    size_t remaining = payload_len;
    int done = 0;
    int ended = 0;
    int iterations = 0;
    const int max_iterations = 100000;

    while (!done && iterations++ < max_iterations)
    {
        size_t this_chunk = remaining < chunk_size ? remaining : chunk_size;
        strm->next_in = cursor;
        strm->avail_in = (unsigned int)this_chunk;
        cursor += this_chunk;
        remaining -= this_chunk;

        int flush;
        if (remaining == 0)
        {

            flush = MZ_FINISH;
        }
        else
        {
            switch ((cfg2 + iterations) % 4)
            {
            case 0:
                flush = MZ_NO_FLUSH;
                break;
            case 1:
                flush = MZ_PARTIAL_FLUSH;
                break;
            case 2:
                flush = MZ_FULL_FLUSH;
                break;
            default:
                flush = MZ_SYNC_FLUSH;
                break;
            }
        }

        for (;;)
        {
            strm->next_out = out_buf;
            strm->avail_out = (unsigned int)out_cap;

            int status = mz_deflate(strm, flush);

            size_t produced = out_cap - strm->avail_out;
            if (produced > 0)
            {
                if (*compressed_len + produced > *compressed_cap)
                {
                    size_t new_cap = (*compressed_len + produced) * 2 + 64;
                    unsigned char *tmp = (unsigned char *)realloc(*compressed, new_cap);
                    if (!tmp)
                    {
                        done = 1;
                        break;
                    }
                    *compressed = tmp;
                    *compressed_cap = new_cap;
                }
                memcpy(*compressed + *compressed_len, out_buf, produced);
                *compressed_len += produced;
            }

            if (status == MZ_STREAM_END)
            {
                done = 1;
                ended = 1;
                break;
            }
            if (status != MZ_OK)
            {

                done = 1;
                break;
            }
            if (strm->avail_out != 0)
            {

                break;
            }

        }
    }
    return ended;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    uint8_t cfg0 = data[0];
    uint8_t cfg1 = size > 1 ? data[1] : 0;
    uint8_t cfg2 = size > 2 ? data[2] : 0;

    size_t header_len = size >= 3 ? 3 : size;
    const uint8_t *payload = data + header_len;
    size_t payload_len = size - header_len;

    int level = cfg0 % 11;
    int window_bits = (cfg1 & 1) ? MZ_DEFAULT_WINDOW_BITS : -MZ_DEFAULT_WINDOW_BITS;
    int strategy = (cfg1 >> 1) % 5;
    int mem_level = 1 + ((cfg1 >> 4) % 9);

    mz_stream strm;
    memset(&strm, 0, sizeof(strm));

    int ret = mz_deflateInit2(&strm, level, MZ_DEFLATED, window_bits, mem_level, strategy);
    if (ret != MZ_OK)
    {

        mz_deflateEnd(&strm);
        return 0;
    }

    unsigned num_chunks = 1 + (cfg2 % 4);
    size_t chunk_size = payload_len ? (payload_len + num_chunks - 1) / num_chunks : 1;
    if (chunk_size == 0)
        chunk_size = 1;

    mz_ulong bound = mz_deflateBound(&strm, (mz_ulong)payload_len);
    size_t divisor = 1 + ((cfg2 >> 3) % 4);
    size_t out_cap = 64 + (size_t)(bound / divisor);
    if (out_cap < 64)
        out_cap = 64;

    unsigned char *out_buf = (unsigned char *)malloc(out_cap);
    if (!out_buf)
    {
        mz_deflateEnd(&strm);
        return 0;
    }

    size_t compressed_cap = out_cap;
    unsigned char *compressed = (unsigned char *)malloc(compressed_cap);
    size_t compressed_len = 0;
    if (!compressed)
    {
        free(out_buf);
        mz_deflateEnd(&strm);
        return 0;
    }

    run_deflate_stream(&strm, payload, payload_len, num_chunks, chunk_size, cfg2,
                        out_buf, out_cap, &compressed, &compressed_cap, &compressed_len);

    if ((cfg2 & 0x80) && payload_len > 1)
    {
        size_t half = payload_len / 2;
        const uint8_t *second_half = payload + half;
        size_t second_len = payload_len - half;

        if (mz_deflateReset(&strm) == MZ_OK)
        {
            run_deflate_stream(&strm, second_half, second_len, num_chunks, chunk_size, cfg2,
                                out_buf, out_cap, &compressed, &compressed_cap, &compressed_len);
        }
    }

    mz_deflateEnd(&strm);

    if (compressed_len > 0)
    {
        size_t decompress_cap = payload_len + 1024;
        unsigned char *decompressed = (unsigned char *)malloc(decompress_cap);
        if (decompressed)
        {
            mz_ulong dest_len = (mz_ulong)decompress_cap;
            mz_uncompress(decompressed, &dest_len, compressed, (mz_ulong)compressed_len);
            free(decompressed);
        }
    }

    if ((cfg0 & 0x80) && payload_len > 0)
    {
        int level2 = (cfg0 >> 4) % 11;
        mz_ulong bound2 = mz_compressBound((mz_ulong)payload_len);
        unsigned char *dest = (unsigned char *)malloc(bound2);
        if (dest)
        {
            mz_ulong dest_len2 = bound2;
            int cstatus = mz_compress2(dest, &dest_len2, payload, (mz_ulong)payload_len, level2);
            if (cstatus == MZ_OK && dest_len2 > 0)
            {
                size_t unc_cap = payload_len + 1024;
                unsigned char *unc = (unsigned char *)malloc(unc_cap);
                if (unc)
                {
                    mz_ulong unc_len = (mz_ulong)unc_cap;
                    mz_uncompress(unc, &unc_len, dest, dest_len2);
                    free(unc);
                }
            }
            free(dest);
        }
    }

    free(compressed);
    free(out_buf);
    return 0;
}
