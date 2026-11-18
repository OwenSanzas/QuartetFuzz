#include "miniz.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EXTRACT_SIZE (1u << 20)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) {
        return 0;
    }

    mz_uint init_flags = (mz_uint)(data[size - 1] &
                                    (MZ_ZIP_FLAG_CASE_SENSITIVE |
                                     MZ_ZIP_FLAG_IGNORE_PATH |
                                     MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY));
    mz_uint8 ctrl = data[size - 2];
    mz_uint extract_flags = (mz_uint)(ctrl & MZ_ZIP_FLAG_COMPRESSED_DATA);
    size_t archive_size = size - 2;

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);

    if (mz_zip_reader_init_mem(&zip, data, archive_size, init_flags)) {
        mz_uint num_files = mz_zip_reader_get_num_files(&zip);
        mz_uint i;
        for (i = 0; i < num_files; i++) {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
                continue;
            }

            mz_uint32 found_index = 0;
            mz_zip_reader_locate_file_v2(&zip, stat.m_filename, NULL,
                                          init_flags, &found_index);

            mz_uint64 want_size = (extract_flags & MZ_ZIP_FLAG_COMPRESSED_DATA)
                                       ? stat.m_comp_size
                                       : stat.m_uncomp_size;

            if (stat.m_is_supported && !stat.m_is_directory &&
                want_size > 0 && want_size <= MAX_EXTRACT_SIZE) {
                if (ctrl & 0x08) {

                    size_t heap_size = 0;
                    void *hbuf = mz_zip_reader_extract_to_heap(&zip, i, &heap_size,
                                                                extract_flags);
                    if (hbuf) {
                        zip.m_pFree(zip.m_pAlloc_opaque, hbuf);
                    }
                } else {
                    void *buf = malloc((size_t)want_size);
                    if (buf) {

                        mz_zip_reader_extract_to_mem(&zip, i, buf, (size_t)want_size,
                                                      extract_flags);
                        free(buf);
                    }
                }
            }
        }

        mz_zip_reader_end(&zip);
    }

    {
        mz_zip_error verr = MZ_ZIP_NO_ERROR;
        mz_uint validate_flags =
            init_flags | (ctrl & (MZ_ZIP_FLAG_VALIDATE_LOCATE_FILE_FLAG |
                                   MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY));
        mz_zip_validate_mem_archive(data, archive_size, validate_flags, &verr);
    }

    return 0;
}
