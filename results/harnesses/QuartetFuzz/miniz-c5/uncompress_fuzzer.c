#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

static int pick_level(uint8_t b)
{

    static const int levels[] = {MZ_DEFAULT_COMPRESSION, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    return levels[b % (sizeof(levels) / sizeof(levels[0]))];
}

static void write_zlib_header(uint8_t *s)
{
    unsigned level_flags = 0;
    unsigned w_bits = 8;
    unsigned header = (MZ_DEFLATED + ((w_bits - 8) << 4)) << 8;
    header |= (level_flags << 6);
    header += 31 - (header % 31);
    s[0] = (uint8_t)(header >> 8);
    s[1] = (uint8_t)(header & 0xff);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 4)
        return 0;

    uint8_t level_byte = data[0];
    uint8_t dest_factor = data[1];
    uint8_t window_byte = data[2];
    uint8_t flush_byte = data[3];

    const uint8_t *payload = data + 4;
    size_t payload_len = size - 4;

    {
        static const size_t kMaxSize = 1 << 20;
        if (payload_len > kMaxSize)
            payload_len = kMaxSize;
    }

    int level = pick_level(level_byte);

    {
        mz_ulong comp_cap = (mz_ulong)(128 + mz_compressBound((mz_ulong)payload_len));
        unsigned char *comp_buf = (unsigned char *)malloc(comp_cap);
        if (comp_buf)
        {
            mz_ulong comp_len = comp_cap;
            int cstatus = mz_compress2(comp_buf, &comp_len, payload, (mz_ulong)payload_len, level);
            if (cstatus == MZ_OK)
            {

                size_t dest_cap = payload_len;
                if ((dest_factor & 1) && payload_len > 0)
                    dest_cap = payload_len / 2;
                unsigned char *dest_buf = (unsigned char *)malloc(dest_cap ? dest_cap : 1);
                if (dest_buf)
                {
                    mz_ulong dest_len = (mz_ulong)dest_cap;
                    int ustatus = mz_uncompress(dest_buf, &dest_len, comp_buf, comp_len);
                    if (ustatus == MZ_OK && dest_len == payload_len)
                    {

                        memcmp(dest_buf, payload, dest_len);
                    }
                    free(dest_buf);
                }

                unsigned char *dest_buf2 = (unsigned char *)malloc(payload_len ? payload_len : 1);
                if (dest_buf2)
                {
                    mz_ulong dest_len2 = (mz_ulong)payload_len;
                    mz_ulong src_len2 = comp_len;
                    int ustatus2 = mz_uncompress2(dest_buf2, &dest_len2, comp_buf, &src_len2);
                    if (ustatus2 == MZ_OK && dest_len2 == payload_len)
                        memcmp(dest_buf2, payload, dest_len2);
                    free(dest_buf2);
                }
            }
            free(comp_buf);
        }
    }

    {
        size_t copy_len = payload_len + 2;
        uint8_t *copy = (uint8_t *)malloc(copy_len);
        if (copy)
        {
            if (payload_len > 0)
                memcpy(copy + 2, payload, payload_len);
            write_zlib_header(copy);

            size_t dest_cap2 = payload_len ? payload_len : 1;
            unsigned char *dest_buf3 = (unsigned char *)malloc(dest_cap2);
            if (dest_buf3)
            {
                mz_ulong dest_len3 = (mz_ulong)dest_cap2;
                int ustatus3 = mz_uncompress(dest_buf3, &dest_len3, copy, (mz_ulong)copy_len);
                if (ustatus3 == MZ_OK)
                {
                    size_t n = dest_len3 < payload_len ? dest_len3 : payload_len;

                    memcmp(dest_buf3, payload, n);
                }
                free(dest_buf3);
            }
            free(copy);
        }
    }

    {
        int window_bits = (window_byte & 1) ? -MZ_DEFAULT_WINDOW_BITS : MZ_DEFAULT_WINDOW_BITS;
        int flush_mode;
        switch (flush_byte % 3)
        {
        case 0:
            flush_mode = MZ_NO_FLUSH;
            break;
        case 1:
            flush_mode = MZ_SYNC_FLUSH;
            break;
        default:
            flush_mode = MZ_FINISH;
            break;
        }

        mz_stream c_stream;
        memset(&c_stream, 0, sizeof(c_stream));
        mz_ulong comp_cap2 = (mz_ulong)(128 + mz_compressBound((mz_ulong)payload_len));
        unsigned char *comp_buf2 = (unsigned char *)malloc(comp_cap2);
        if (comp_buf2)
        {
            if (mz_deflateInit2(&c_stream, level, MZ_DEFLATED, window_bits, 8, MZ_DEFAULT_STRATEGY) == MZ_OK)
            {
                c_stream.next_in = payload;
                c_stream.avail_in = (unsigned int)payload_len;
                c_stream.next_out = comp_buf2;
                c_stream.avail_out = (unsigned int)comp_cap2;
                int dstatus = mz_deflate(&c_stream, MZ_FINISH);
                size_t produced = c_stream.total_out;
                mz_deflateEnd(&c_stream);

                if (dstatus == MZ_STREAM_END && produced > 0)
                {
                    mz_stream d_stream;
                    memset(&d_stream, 0, sizeof(d_stream));
                    if (mz_inflateInit2(&d_stream, window_bits) == MZ_OK)
                    {
                        size_t dest_cap3 = payload_len ? payload_len : 1;
                        unsigned char *dest_buf4 = (unsigned char *)malloc(dest_cap3);
                        if (dest_buf4)
                        {
                            d_stream.next_in = comp_buf2;
                            d_stream.avail_in = (unsigned int)produced;
                            d_stream.next_out = dest_buf4;
                            d_stream.avail_out = (unsigned int)dest_cap3;

                            int istatus;
                            int iterations = 0;
                            do
                            {
                                istatus = mz_inflate(&d_stream, flush_mode);
                                iterations++;
                            } while (istatus == MZ_OK && d_stream.avail_in > 0 && d_stream.avail_out > 0 && iterations < 10000);

                            if (istatus == MZ_STREAM_END && d_stream.total_out == payload_len)
                                memcmp(dest_buf4, payload, d_stream.total_out);

                            free(dest_buf4);
                        }
                        mz_inflateEnd(&d_stream);
                    }
                }
            }
            free(comp_buf2);
        }
    }

    return 0;
}
