#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "git2.h"
#include "git2/sys/config.h"
#include "config.h"

static void try_backend_with_opts(const char *cfg, size_t len, git_config_backend_memory_options *opts)
{
    git_config_backend *backend = NULL;
    int error;

    error = git_config_backend_from_string(&backend, cfg, len, opts);
    if (error == 0 && backend != NULL) {

        if (backend->open) {
            backend->open(backend, GIT_CONFIG_LEVEL_LOCAL, NULL);
        }

        if (backend->iterator) {
            git_config_iterator *iter = NULL;
            if (backend->iterator(&iter, backend) == 0 && iter != NULL) {
                git_config_entry *entry = NULL;
                while (git_config_next(&entry, iter) == 0) {

                }
                git_config_iterator_free(iter);
            }
        }

        if (backend->get) {
            git_config_backend_entry *entry = NULL;
            backend->get(backend, "core.bare", &entry);
            if (entry && entry->free) {
                entry->free(entry);
            }
        }

        if (backend->snapshot) {
            git_config_backend *snap = NULL;
            if (backend->snapshot(&snap, backend) == 0 && snap != NULL) {
                if (snap->free) {
                    snap->free(snap);
                }
            }
        }

        if (backend->free) {
            backend->free(backend);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size == 0) {
        return 0;
    }

    git_libgit2_init();

    char *cfg_buf = (char *)malloc(Size + 1);
    if (cfg_buf == NULL) {
        git_libgit2_shutdown();
        return 0;
    }
    memcpy(cfg_buf, Data, Size);
    cfg_buf[Size] = '\0';

    try_backend_with_opts(cfg_buf, Size, NULL);

    git_config_backend_memory_options opts1 = GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT;
    try_backend_with_opts(cfg_buf, Size, &opts1);

    git_config_backend_memory_options opts2;
    memset(&opts2, 0, sizeof(opts2));
    if (git_config_backend_memory_options_init(&opts2, GIT_CONFIG_BACKEND_MEMORY_OPTIONS_VERSION) == 0) {
        try_backend_with_opts(cfg_buf, Size, &opts2);
    }

    if (Size > 1) {
        try_backend_with_opts(cfg_buf, Size / 2, NULL);
    }

    FILE *f = fopen("./dummy_file", "wb");
    if (f != NULL) {
        fwrite(Data, 1, Size, f);
        fclose(f);
    }

    free(cfg_buf);

    git_libgit2_shutdown();

    return 0;
}