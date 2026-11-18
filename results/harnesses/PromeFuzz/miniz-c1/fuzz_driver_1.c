#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "build/amalgamation/miniz.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    void *pBuf = malloc(Size);
    if (!pBuf) {
        return 0;
    }
    memcpy(pBuf, Data, Size);

    {
        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));
        mz_bool status = mz_zip_reader_init_mem(&zip_archive, pBuf, Size, 0);
        if (status) {

            mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
            mz_uint i;
            for (i = 0; i < num_files; i++) {
                mz_zip_archive_file_stat file_stat;
                if (mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {

                    size_t extracted_size = 0;
                    void *p = mz_zip_reader_extract_to_heap(&zip_archive, i, &extracted_size, 0);
                    if (p) {
                        free(p);
                    }
                }
            }
            mz_zip_reader_end(&zip_archive);
        }
    }

    {
        mz_zip_archive zip_archive2;
        memset(&zip_archive2, 0, sizeof(zip_archive2));
        mz_bool status2 = mz_zip_reader_init_mem(&zip_archive2, pBuf, Size, MZ_ZIP_FLAG_CASE_SENSITIVE);
        if (status2) {
            mz_uint num_files2 = mz_zip_reader_get_num_files(&zip_archive2);
            mz_uint j;
            for (j = 0; j < num_files2; j++) {
                mz_zip_archive_file_stat file_stat2;
                if (mz_zip_reader_file_stat(&zip_archive2, j, &file_stat2)) {

                    if (file_stat2.m_filename[0] != '\0') {
                        int idx = mz_zip_reader_locate_file(&zip_archive2, file_stat2.m_filename, NULL, 0);
                        (void)idx;
                    }
                }
            }
            mz_zip_reader_end(&zip_archive2);
        }
    }

    {
        mz_zip_archive zip_archive3;
        memset(&zip_archive3, 0, sizeof(zip_archive3));
        mz_bool status3 = mz_zip_reader_init_mem(&zip_archive3, pBuf, Size, MZ_ZIP_FLAG_IGNORE_PATH);
        if (status3) {
            mz_uint num_files3 = mz_zip_reader_get_num_files(&zip_archive3);
            (void)num_files3;
            mz_zip_reader_end(&zip_archive3);
        }
    }

    {
        mz_zip_archive zip_archive4;
        memset(&zip_archive4, 0, sizeof(zip_archive4));
        mz_bool status4 = mz_zip_reader_init_mem(&zip_archive4, pBuf, 0, 0);
        if (status4) {
            mz_zip_reader_end(&zip_archive4);
        }
    }

    free(pBuf);
    return 0;
}