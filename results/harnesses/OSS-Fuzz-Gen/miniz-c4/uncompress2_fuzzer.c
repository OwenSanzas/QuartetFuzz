#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

static void try_forced_block_type(const uint8_t *body, size_t body_size, int tdefl_flags)
{
    if (body_size == 0)
        return;

    size_t bound = body_size + 512;
    unsigned char *comp = (unsigned char *)malloc(bound);
    if (!comp)
        return;

    size_t comp_len = tdefl_compress_mem_to_mem(comp, bound, body, body_size,
                                                 TDEFL_WRITE_ZLIB_HEADER | tdefl_flags);
    if (comp_len > 0)
    {

        mz_ulong out_len = (mz_ulong)body_size;
        unsigned char *out = (unsigned char *)malloc(out_len ? out_len : 1);
        if (out)
        {
            mz_ulong src_len = (mz_ulong)comp_len;
            int st = mz_uncompress2(out, &out_len, comp, &src_len);
            (void)st;
            free(out);
        }

        if (comp_len > 4)
        {
            unsigned char *corrupt = (unsigned char *)malloc(comp_len);
            if (corrupt)
            {
                memcpy(corrupt, comp, comp_len);
                corrupt[comp_len / 2] ^= 0xFF;

                mz_ulong out_len2 = (mz_ulong)body_size;
                unsigned char *out2 = (unsigned char *)malloc(out_len2 ? out_len2 : 1);
                if (out2)
                {
                    mz_ulong src_len2 = (mz_ulong)comp_len;
                    int st2 = mz_uncompress2(out2, &out_len2, corrupt, &src_len2);
                    (void)st2;
                    free(out2);
                }
                free(corrupt);
            }
        }
    }
    free(comp);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 3)
        return 0;

    uint8_t mode = data[0];
    const uint8_t *body = data + 1;
    size_t body_size = size - 1;

    if (body_size < 2)
        return 0;

    mz_ulong dest_len = 1;
    dest_len *= body[0];
    dest_len *= body[1];

    const mz_ulong kMaxDestLen = 1024 * 1024;
    if (dest_len == 0 || dest_len > kMaxDestLen)
        dest_len = kMaxDestLen;

    if ((mz_uint64)body_size > 0xFFFFFFFFU)
        return 0;

    unsigned char *dest_buffer = (unsigned char *)malloc(dest_len);
    if (dest_buffer == NULL && dest_len != 0)
        return 0;

    uint8_t selector = mode % 8;

    if (selector == 0)
    {

        mz_ulong source_len = (mz_ulong)body_size;
        int status = mz_uncompress2(dest_buffer, &dest_len, body, &source_len);
        (void)status;
    }
    else if (selector == 1)
    {

        unsigned char *forced = (unsigned char *)malloc(body_size);
        if (forced)
        {
            memcpy(forced, body, body_size);
            if (mode & 0x4)
            {
                forced[0] = 0x78;
                forced[1] = 0xBB;
            }
            else
            {
                forced[0] = 0x78;
                forced[1] = 0x9C;
            }
            mz_ulong source_len = (mz_ulong)body_size;
            int status = mz_uncompress2(dest_buffer, &dest_len, forced, &source_len);
            (void)status;
            free(forced);
        }
    }
    else if (selector == 2)
    {

        int level = mode % 11;
        if (level > MZ_BEST_COMPRESSION)
            level = MZ_BEST_COMPRESSION;

        mz_ulong comp_bound = mz_compressBound((mz_ulong)body_size);
        if (comp_bound == 0)
            comp_bound = 1;
        unsigned char *comp_buf = (unsigned char *)malloc(comp_bound);
        if (comp_buf)
        {
            mz_ulong comp_len = comp_bound;
            if (mz_compress2(comp_buf, &comp_len, body, (mz_ulong)body_size,
                              level) == MZ_OK &&
                comp_len > 0)
            {

                mz_ulong out_len_ok = (mz_ulong)(body_size ? body_size : 1);
                unsigned char *out_ok = (unsigned char *)malloc(out_len_ok);
                if (out_ok)
                {
                    mz_ulong src_len_ok = comp_len;
                    int st = mz_uncompress2(out_ok, &out_len_ok, comp_buf, &src_len_ok);
                    (void)st;
                    free(out_ok);
                }

                unsigned char *corrupt_buf = (unsigned char *)malloc(comp_len);
                if (corrupt_buf)
                {
                    memcpy(corrupt_buf, comp_buf, comp_len);

                    if (comp_len >= 4)
                        corrupt_buf[comp_len - 1 - (body[0] % 4)] ^= body[body_size - 1];

                    if (body_size > 2)
                        corrupt_buf[body[1] % comp_len] ^= body[body_size - 2];

                    mz_ulong out_len = (mz_ulong)(body_size ? body_size : 1);
                    unsigned char *out_buf = (unsigned char *)malloc(out_len);
                    if (out_buf)
                    {
                        mz_ulong src_len = comp_len;
                        int status = mz_uncompress2(out_buf, &out_len, corrupt_buf, &src_len);
                        (void)status;
                        free(out_buf);
                    }
                    free(corrupt_buf);
                }
            }
            free(comp_buf);
        }
    }
    else if (selector == 3)
    {

        mz_ulong huge_len = 0x100000000ULL + body[0];
        mz_ulong small_dest_len = dest_len;
        mz_ulong source_len = (mz_ulong)body_size;

        mz_ulong dest_len_copy = huge_len;
        int status = mz_uncompress2(dest_buffer, &dest_len_copy, body, &source_len);
        (void)status;

        mz_ulong src_len2 = huge_len;
        status = mz_uncompress2(dest_buffer, &small_dest_len, body, &src_len2);
        (void)status;
    }
    else if (selector == 4)
    {

        mz_ulong zero_len = 0;
        int status = mz_uncompress2(dest_buffer, &dest_len, NULL, &zero_len);
        (void)status;

        mz_ulong zero_len2 = 0;
        mz_ulong source_len = (mz_ulong)body_size;
        status = mz_uncompress2(NULL, &zero_len2, body, &source_len);
        (void)status;

        mz_ulong zero_len3 = 0;
        mz_ulong zero_len4 = 0;
        status = mz_uncompress2(NULL, &zero_len3, NULL, &zero_len4);
        (void)status;
    }
    else if (selector == 5)
    {

        try_forced_block_type(body, body_size, TDEFL_FORCE_ALL_STATIC_BLOCKS);
    }
    else if (selector == 6)
    {

        try_forced_block_type(body, body_size, TDEFL_FORCE_ALL_RAW_BLOCKS);

        size_t rep_count = 4;
        size_t rep_size = body_size * rep_count;
        if (rep_size > 0 && rep_size < (16 * 1024 * 1024))
        {
            unsigned char *rep_buf = (unsigned char *)malloc(rep_size);
            if (rep_buf)
            {
                for (size_t i = 0; i < rep_count; i++)
                    memcpy(rep_buf + i * body_size, body, body_size);
                try_forced_block_type(rep_buf, rep_size, 0  );
                free(rep_buf);
            }
        }
    }
    else
    {

        unsigned char *forced = (unsigned char *)malloc(body_size);
        if (forced)
        {
            memcpy(forced, body, body_size);
            forced[0] = 0x78;
            forced[1] = 0x9C;

            mz_ulong tiny_dest_len = (body_size > 1) ? (body[0] % 4) : 0;
            unsigned char *tiny_buf = (unsigned char *)malloc(tiny_dest_len ? tiny_dest_len : 1);
            if (tiny_buf)
            {
                mz_ulong source_len = (mz_ulong)body_size;
                int status = mz_uncompress2(tiny_buf, &tiny_dest_len, forced, &source_len);
                (void)status;
                free(tiny_buf);
            }
            free(forced);
        }
    }

    free(dest_buffer);

    return 0;
}