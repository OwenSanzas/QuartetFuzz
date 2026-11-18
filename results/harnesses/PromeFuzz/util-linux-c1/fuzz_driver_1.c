#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libfdisk.h>

static const char *dummy_file = "./dummy_file";

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0)
        return 0;

    FILE *f = fopen(dummy_file, "wb");
    if (!f)
        return 0;

    fwrite(Data, 1, Size, f);
    fclose(f);

    f = fopen(dummy_file, "r");
    if (!f)
        return 0;

    struct fdisk_context *cxt = fdisk_new_context();
    if (!cxt) {
        fclose(f);
        return 0;
    }

    struct fdisk_script *dp = fdisk_new_script(cxt);
    if (!dp) {
        fdisk_unref_context(cxt);
        fclose(f);
        return 0;
    }

    int rc = fdisk_script_read_file(dp, f);
    (void) rc;

    rewind(f);
    rc = fdisk_script_read_file(dp, f);
    (void) rc;

    fdisk_apply_script(cxt, dp);

    fdisk_unref_script(dp);
    fdisk_unref_context(cxt);
    fclose(f);

    return 0;
}