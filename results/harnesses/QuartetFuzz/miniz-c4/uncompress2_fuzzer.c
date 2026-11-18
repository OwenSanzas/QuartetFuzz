#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1)
        return 0;

    uint8_t control = data[0];
    const unsigned char *src = data + 1;
    size_t src_len = size - 1;

    size_t dest_cap = src_len * 4 + 256;
    if (dest_cap > (16 * 1024 * 1024))
        dest_cap = 16 * 1024 * 1024;

    unsigned char *dest = (unsigned char *)malloc(dest_cap);
    if (dest != NULL)
    {
        mz_ulong dest_len = (mz_ulong)dest_cap;
        mz_ulong source_len = (mz_ulong)src_len;

        int status = mz_uncompress2(dest, &dest_len, src, &source_len);
        if (status == MZ_OK)
        {

            unsigned long sum = 0;
            for (mz_ulong i = 0; i < dest_len; i++)
                sum = sum * 131u + dest[i];

            if (sum == 0xdeadbeefUL)
                fprintf(stderr, ".");
        }
        free(dest);
    }

    if (control & 1)
    {
        mz_stream stream;
        memset(&stream, 0, sizeof(stream));

        int window_bits = (control & 2) ? -MZ_DEFAULT_WINDOW_BITS : MZ_DEFAULT_WINDOW_BITS;
        if (mz_inflateInit2(&stream, window_bits) == MZ_OK)
        {
            unsigned char outbuf[256];
            int flush = (control & 4) ? MZ_SYNC_FLUSH : MZ_NO_FLUSH;

            stream.next_in = src;
            stream.avail_in = (mz_uint32)src_len;

            int st;
            int iter = 0;
            do
            {
                stream.next_out = outbuf;
                stream.avail_out = (mz_uint32)sizeof(outbuf);
                st = mz_inflate(&stream, flush);
                iter++;
            } while (st == MZ_OK && iter < 64);

            mz_inflateEnd(&stream);
        }
    }

    if (src_len > 0 && src_len <= (1u << 20))
    {
        int level = (int)(control % 11);
        size_t comp_cap = 100 + src_len + (src_len / 2);
        unsigned char *comp = (unsigned char *)malloc(comp_cap);
        unsigned char *plain_out = (unsigned char *)malloc(src_len);

        if (comp != NULL && plain_out != NULL)
        {
            mz_ulong comp_len = (mz_ulong)comp_cap;
            int cstatus = mz_compress2(comp, &comp_len, src, (mz_ulong)src_len, level);
            if (cstatus == MZ_OK)
            {
                mz_ulong plain_len = (mz_ulong)src_len;
                mz_ulong comp_src_len = comp_len;
                int dstatus = mz_uncompress2(plain_out, &plain_len, comp, &comp_src_len);
                if (dstatus == MZ_OK)
                {

                    if (plain_len == src_len && memcmp(plain_out, src, src_len) != 0)
                    {
                        fprintf(stderr, "round-trip mismatch\n");
                    }
                }
            }
        }
        free(comp);
        free(plain_out);
    }

    if (src_len > 0 && src_len <= (1u << 20))
    {
        size_t hdr_len = src_len + 2;
        unsigned char *hdr_buf = (unsigned char *)malloc(hdr_len);
        size_t hdr_dest_cap = src_len * 4 + 256;
        if (hdr_dest_cap > (16 * 1024 * 1024))
            hdr_dest_cap = 16 * 1024 * 1024;
        unsigned char *hdr_dest = (unsigned char *)malloc(hdr_dest_cap);

        if (hdr_buf != NULL && hdr_dest != NULL)
        {

            hdr_buf[0] = 0x78;
            hdr_buf[1] = 0x9c;
            memcpy(hdr_buf + 2, src, src_len);

            mz_ulong hdest_len = (mz_ulong)hdr_dest_cap;
            mz_ulong hsource_len = (mz_ulong)hdr_len;
            int hstatus = mz_uncompress2(hdr_dest, &hdest_len, hdr_buf, &hsource_len);
            if (hstatus == MZ_OK)
            {
                unsigned long sum = 0;
                for (mz_ulong i = 0; i < hdest_len; i++)
                    sum = sum * 131u + hdr_dest[i];
                if (sum == 0xdeadbeefUL)
                    fprintf(stderr, ".");
            }
        }
        free(hdr_buf);
        free(hdr_dest);
    }

    return 0;
}
