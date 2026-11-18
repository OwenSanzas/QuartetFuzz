#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "zlib.h"

static const char *pick_mode(uint8_t b) {
    static const char *modes[] = {
        "rb", "wb", "ab", "rb9", "wb1", "wb6", "wb9",
        "r", "w", "a", "r+", "w+", "rt", "wt", "ab+", "wbT"
    };
    size_t n = sizeof(modes) / sizeof(modes[0]);
    return modes[b % n];
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    const char *filename = "./dummy_file";

    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        return 0;
    }
    fwrite(Data, 1, Size, f);
    fclose(f);

    uint8_t mode_selector = Data[0];
    const char *mode = pick_mode(mode_selector);

    gzFile gzf = gzopen(filename, mode);
    if (gzf != NULL) {
        char buf[256];

        if (mode[0] == 'r') {
            int read_bytes = gzread(gzf, buf, sizeof(buf));
            (void)read_bytes;
        } else if (mode[0] == 'w' || mode[0] == 'a') {

            size_t write_len = Size > sizeof(buf) ? sizeof(buf) : Size;
            memcpy(buf, Data, write_len);
            int written = gzwrite(gzf, buf, (unsigned)write_len);
            (void)written;
        }
        gzclose(gzf);
    }

    if (Size > 1) {
        char custom_mode[16];
        size_t mode_len = Size > (sizeof(custom_mode) - 1) ? (sizeof(custom_mode) - 1) : Size;
        memcpy(custom_mode, Data, mode_len);
        custom_mode[mode_len] = '\0';

        for (size_t i = 0; i < mode_len; i++) {
            if (custom_mode[i] == '\0') {
                custom_mode[i] = 'x';
            }
        }

        gzFile gzf2 = gzopen(filename, custom_mode);
        if (gzf2 != NULL) {
            char buf2[128];
            int read_bytes2 = gzread(gzf2, buf2, sizeof(buf2));
            (void)read_bytes2;
            gzclose(gzf2);
        }
    }

    gzFile gzf3 = gzopen("./nonexistent_dummy_file_for_fuzzing", mode);
    if (gzf3 != NULL) {
        gzclose(gzf3);
    }

    gzFile gzf4 = gzopen(filename, "");
    if (gzf4 != NULL) {
        gzclose(gzf4);
    }

    remove(filename);

    return 0;
}