#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "miniz.h"

static mz_uint valid_flags_mask(void) {

    return (mz_uint)(MZ_ZIP_FLAG_CASE_SENSITIVE |
                      MZ_ZIP_FLAG_IGNORE_PATH |
                      MZ_ZIP_FLAG_COMPRESSED_DATA |
                      MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY);
}

static void exercise_reader(mz_zip_archive *zip, mz_uint init_flags, mz_uint extract_flags)
{
    mz_uint num_files = mz_zip_reader_get_num_files(zip);
    mz_uint i;
    mz_uint max_entries = (num_files < 64) ? num_files : 64;

    for (i = 0; i < max_entries; i++) {
        mz_zip_archive_file_stat file_stat;
        char filename_buf[512];

        if (mz_zip_reader_file_stat(zip, i, &file_stat)) {
            mz_zip_reader_get_filename(zip, i, filename_buf, sizeof(filename_buf));

            mz_uint32 located_index = 0;
            mz_zip_reader_locate_file_v2(zip, filename_buf, NULL, init_flags, &located_index);
        }

        size_t extracted_size = 0;
        void *p = mz_zip_reader_extract_to_heap(zip, i, &extracted_size, extract_flags);
        if (p) {
            free(p);
        }
    }
}

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void put_le64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

static size_t find_sig_forward(const uint8_t *buf, size_t size, uint32_t sig)
{
    size_t i;
    if (size < 4)
        return (size_t)-1;
    for (i = 0; i + 4 <= size; i++) {
        uint32_t v = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) |
                     ((uint32_t)buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
        if (v == sig)
            return i;
    }
    return (size_t)-1;
}

static void targeted_corrupt_archive(uint8_t *buf, size_t buf_size, size_t eocd_pos,
                                      uint8_t choice, uint8_t choice2)
{
    if (buf == NULL || buf_size < 22 || eocd_pos + 22 > buf_size)
        return;

    size_t cdh = find_sig_forward(buf, buf_size, 0x02014b50u);
    size_t z64h = find_sig_forward(buf, buf_size, 0x06064b50u);
    size_t z64l = find_sig_forward(buf, buf_size, 0x07064b50u);

    switch (choice % 16) {
        case 0:

            buf[eocd_pos + 4] ^= 0xFF;
            break;
        case 1:

            buf[eocd_pos + 6] ^= 0xFF;
            break;
        case 2:

            if (buf_size > 64)
                buf[buf_size / 2] ^= 0x01;
            break;
        case 3:

            buf[eocd_pos + 12] ^= 0x01;
            break;
        case 4:

            if (cdh != (size_t)-1 && cdh + 10 <= buf_size)
                buf[cdh + 9] |= 0x20;
            break;
        case 5:

            if (cdh != (size_t)-1 && cdh + 12 <= buf_size) {
                buf[cdh + 10] = 99;
                buf[cdh + 11] = 0;
            }
            break;
        case 6:

            if (cdh != (size_t)-1 && cdh + 36 <= buf_size) {
                buf[cdh + 34] = 0xFF;
                buf[cdh + 35] = 0xFF;
            }
            break;
        case 7:

            if (z64h != (size_t)-1 && z64h + 12 <= buf_size)
                buf[z64h + 4] ^= 0xFF;
            break;
        case 8:

            if (z64l != (size_t)-1 && z64l + 20 <= buf_size)
                buf[z64l + 16] ^= 0xFF;
            break;
        case 9:

            if (choice2 % 7 == 0 && cdh != (size_t)-1 && cdh + 36 <= buf_size)
                buf[cdh + 34] ^= 0x01;
            break;
        case 10:

            buf[eocd_pos + 8] ^= 0xFF;
            break;
        case 11:

            if (z64h != (size_t)-1 && z64h + 32 <= buf_size)
                buf[z64h + 31] |= 0x80;
            break;
        case 12:

            if (z64h != (size_t)-1 && z64h + 40 <= buf_size)
                buf[z64h + 39] |= 0x80;
            break;
        case 13:

            if (z64h != (size_t)-1 && z64h + 48 <= buf_size)
                buf[z64h + 47] |= 0x80;
            break;
        case 14:

            if (cdh != (size_t)-1 && cdh + 24 <= buf_size) {
                buf[cdh + 20] ^= 0x01;
                if (buf[cdh + 20] == 0xFF && buf[cdh + 21] == 0xFF &&
                    buf[cdh + 22] == 0xFF && buf[cdh + 23] == 0xFF)
                    buf[cdh + 20] ^= 0x02;
            }
            break;
        default:

            if (cdh != (size_t)-1 && cdh + 46 <= buf_size)
                put_le32(buf + cdh + 42, 0x7FFFFFFFu);
            break;
    }
}

static size_t build_zip64_extra_archive(uint8_t *buf, size_t buf_cap, uint8_t mode, char name_char)
{
    size_t pos = 0;
    size_t local_start;
    size_t local_len;
    size_t cdir_start;
    size_t cdir_len;
    uint8_t extra[32];
    size_t extra_actual_len;
    size_t extra_decl_len;
    size_t declared_cdir_size;

    if (buf_cap < 200)
        return 0;

    local_start = pos;

    put_le32(buf + pos, 0x04034b50u); pos += 4;
    put_le16(buf + pos, 20); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le32(buf + pos, 0); pos += 4;
    put_le32(buf + pos, 0); pos += 4;
    put_le32(buf + pos, 0); pos += 4;
    put_le16(buf + pos, 1); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    buf[pos++] = (uint8_t)name_char;
    local_len = pos - local_start;
    (void)local_len;

    cdir_start = pos;

    put_le16(extra + 0, 0x0001);

    switch (mode % 4) {
        case 0:

            put_le16(extra + 2, 16);
            put_le64(extra + 4, 0);
            put_le64(extra + 12, 0);
            extra_actual_len = 20;
            extra_decl_len = 20;
            break;
        case 1:

            extra_actual_len = 2;
            extra_decl_len = 2;
            break;
        case 2:

            put_le16(extra + 2, 0xFFFF);
            extra_actual_len = 20;
            extra_decl_len = 20;
            break;
        default:

            put_le16(extra + 2, 16);
            put_le64(extra + 4, 0);
            put_le64(extra + 12, 0);
            extra_actual_len = 20;
            extra_decl_len = 20;
            break;
    }

    put_le32(buf + pos, 0x02014b50u); pos += 4;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 20); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le32(buf + pos, 0); pos += 4;
    put_le32(buf + pos, 0xFFFFFFFFu); pos += 4;
    put_le32(buf + pos, 0); pos += 4;
    put_le16(buf + pos, 1); pos += 2;
    put_le16(buf + pos, (uint16_t)extra_decl_len); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le32(buf + pos, 0); pos += 4;
    put_le32(buf + pos, (uint32_t)local_start); pos += 4;
    buf[pos++] = (uint8_t)name_char;

    if (pos + extra_actual_len > buf_cap)
        return 0;
    memcpy(buf + pos, extra, extra_actual_len);
    pos += extra_actual_len;

    cdir_len = pos - cdir_start;

    declared_cdir_size = cdir_len;
    if ((mode % 4) == 3) {

        declared_cdir_size = cdir_len - extra_actual_len;
    }

    if (pos + 22 > buf_cap)
        return 0;

    put_le32(buf + pos, 0x06054b50u); pos += 4;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 0); pos += 2;
    put_le16(buf + pos, 1); pos += 2;
    put_le16(buf + pos, 1); pos += 2;
    put_le32(buf + pos, (uint32_t)declared_cdir_size); pos += 4;
    put_le32(buf + pos, (uint32_t)cdir_start); pos += 4;
    put_le16(buf + pos, 0); pos += 2;

    return pos;
}

static int g_fail_after = -1;
static int g_alloc_count = 0;

static void *fuzz_alloc(void *opaque, size_t items, size_t size) {
    (void)opaque;
    if (g_fail_after >= 0 && g_alloc_count++ >= g_fail_after)
        return NULL;
    return malloc(items * size);
}
static void fuzz_free(void *opaque, void *ptr) {
    (void)opaque;
    free(ptr);
}
static void *fuzz_realloc(void *opaque, void *ptr, size_t items, size_t size) {
    (void)opaque;
    if (g_fail_after >= 0 && g_alloc_count++ >= g_fail_after)
        return NULL;
    return realloc(ptr, items * size);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{

    if (data == NULL || size < 22)
        return 0;

    mz_uint valid_mask = valid_flags_mask();
    mz_uint init_flags = (mz_uint)data[0] & valid_mask;

    mz_uint extract_flags = (size > 1) ? ((mz_uint)data[1] & valid_mask) : 0;

    {
        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));
        mz_zip_zero_struct(&zip);

        mz_bool ok = mz_zip_reader_init_mem(&zip, data, size, init_flags);

        if (ok) {
            exercise_reader(&zip, init_flags, extract_flags);
        }

        mz_zip_reader_end(&zip);
    }

    if (size > 8) {
        mz_zip_archive wzip;
        memset(&wzip, 0, sizeof(wzip));
        mz_zip_zero_struct(&wzip);

        mz_uint writer_flags = (size > 3 && (data[3] & 1)) ? MZ_ZIP_FLAG_WRITE_ZIP64 : 0;

        if (mz_zip_writer_init_heap_v2(&wzip, 0, 0, writer_flags)) {
            size_t pos = 4;
            mz_uint num_entries = (mz_uint)((data[2 % size] % 8) + 1);
            mz_uint e;

            for (e = 0; e < num_entries && pos < size; e++) {
                size_t remaining = size - pos;
                size_t name_len = (remaining > 0) ? (size_t)((data[pos] % 16) + 1) : 1;
                pos++;

                char name[32];
                size_t n = 0;
                for (; n < name_len && n < sizeof(name) - 2 && pos < size; n++, pos++) {
                    char c = (char)((data[pos] % 26) + 'a');
                    name[n] = c;
                }
                if (n == 0) {
                    name[0] = 'f';
                    n = 1;
                }

                mz_bool as_dir = (pos < size) ? (mz_bool)(data[pos++] & 1) : MZ_FALSE;
                if (as_dir && n < sizeof(name) - 1) {
                    name[n] = '/';
                    n++;
                }
                name[n] = '\0';

                size_t content_len = (pos < size) ? (size - pos) : 0;
                if (content_len > 256)
                    content_len = 256;
                const void *content_ptr = (pos < size) ? (const void *)(data + pos) : (const void *)"";
                pos += content_len;

                mz_uint level_and_flags = (pos < size) ? (mz_uint)(data[pos++] % 11) : (mz_uint)MZ_DEFAULT_COMPRESSION;

                if (pos < size && (data[pos++] & 1)) {
                    level_and_flags |= MZ_ZIP_FLAG_COMPRESSED_DATA;
                }

                mz_zip_writer_add_mem_ex(&wzip, name, content_ptr, as_dir ? 0 : content_len,
                                          "c", 1, level_and_flags, 0, 0);
            }

            void *finalized_buf = NULL;
            size_t finalized_size = 0;
            if (mz_zip_writer_finalize_heap_archive(&wzip, &finalized_buf, &finalized_size)) {
                mz_zip_writer_end(&wzip);

                if (finalized_buf != NULL && finalized_size >= 22) {

                    size_t eocd_pos = finalized_size - 22;

                    if (size > 5 && (data[4] & 1)) {
                        size_t comment_len = 4096 + (size_t)(data[4] % 768);
                        if (comment_len <= 0xFFFFu) {
                            uint8_t *grown = (uint8_t *)realloc(finalized_buf, finalized_size + comment_len);
                            if (grown != NULL) {
                                size_t ci;
                                finalized_buf = grown;
                                for (ci = 0; ci < comment_len; ci++) {
                                    ((uint8_t *)finalized_buf)[finalized_size + ci] = data[ci % size];
                                }

                                uint8_t *eocd = (uint8_t *)finalized_buf + eocd_pos;
                                eocd[20] = (uint8_t)(comment_len & 0xFF);
                                eocd[21] = (uint8_t)((comment_len >> 8) & 0xFF);

                                finalized_size += comment_len;

                            }
                        }
                    }

                    uint8_t corrupt_choice = data[size - 1];
                    uint8_t corrupt_choice2 = (size > 1) ? data[size - 2] : corrupt_choice;
                    targeted_corrupt_archive((uint8_t *)finalized_buf, finalized_size, eocd_pos,
                                              corrupt_choice, corrupt_choice2);

                    mz_zip_archive rzip;
                    memset(&rzip, 0, sizeof(rzip));
                    mz_zip_zero_struct(&rzip);

                    if (mz_zip_reader_init_mem(&rzip, finalized_buf, finalized_size, init_flags)) {
                        exercise_reader(&rzip, init_flags, extract_flags);
                    }
                    mz_zip_reader_end(&rzip);

                    {
                        mz_zip_archive azip;
                        memset(&azip, 0, sizeof(azip));
                        mz_zip_zero_struct(&azip);
                        azip.m_pAlloc = fuzz_alloc;
                        azip.m_pFree = fuzz_free;
                        azip.m_pRealloc = fuzz_realloc;

                        g_alloc_count = 0;

                        g_fail_after = (size > 6) ? ((data[5] % 5) - 1) : -1;

                        if (mz_zip_reader_init_mem(&azip, finalized_buf, finalized_size, init_flags)) {
                            exercise_reader(&azip, init_flags, extract_flags);
                        }
                        mz_zip_reader_end(&azip);

                        g_fail_after = -1;
                        g_alloc_count = 0;
                    }
                }

                if (finalized_buf)
                    free(finalized_buf);
            } else {
                mz_zip_writer_end(&wzip);
            }
        }
    }

    {
        uint8_t zip64_buf[256];
        uint8_t mode = data[size - 1];
        char name_char = (char)((data[0] % 26) + 'a');
        size_t crafted_size = build_zip64_extra_archive(zip64_buf, sizeof(zip64_buf), mode, name_char);

        if (crafted_size > 0 && crafted_size <= sizeof(zip64_buf)) {
            mz_zip_archive zzip;
            memset(&zzip, 0, sizeof(zzip));
            mz_zip_zero_struct(&zzip);

            if (mz_zip_reader_init_mem(&zzip, zip64_buf, crafted_size, init_flags)) {
                exercise_reader(&zzip, init_flags, extract_flags);
            }
            mz_zip_reader_end(&zzip);
        }
    }

    return 0;
}