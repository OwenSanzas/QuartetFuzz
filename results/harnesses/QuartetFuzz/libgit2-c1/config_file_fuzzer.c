#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "git2.h"
#include "git2/sys/config.h"
#include "config_backend.h"

#include "standalone_driver.h"

#define UNUSED(x) (void)(x)

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	UNUSED(argc);
	UNUSED(argv);

	if (git_libgit2_init() < 0)
		abort();

	return 0;
}

static int config_foreach_cb(const git_config_entry *entry, void *payload)
{
	size_t *count = (size_t *)payload;

	if (entry->name)
		*count += strlen(entry->name);
	if (entry->value)
		*count += strlen(entry->value);

	return 0;
}

static void drive_backend(git_config_backend *backend, git_config_level_t level,
	int via_add_backend)
{
	size_t touched = 0;

	if (via_add_backend) {

		git_config *cfg = NULL;

		if (git_config_new(&cfg) == 0) {
			if (git_config_add_backend(cfg, backend, level, NULL, 1) == 0) {
				const char *val = NULL;
				git_config_entry *entry = NULL;

				git_config_get_string(&val, cfg, "section.key");
				if (git_config_get_entry(&entry, cfg, "section.key") == 0)
					git_config_entry_free(entry);

				git_config_foreach(cfg, config_foreach_cb, &touched);

				git_config_get_multivar_foreach(cfg, "general.foo", NULL,
					config_foreach_cb, &touched);
				git_config_foreach_match(cfg, "^general\\.", config_foreach_cb,
					&touched);
			} else {

				git_config_backend_free(backend);
			}
			git_config_free(cfg);
		} else {
			git_config_backend_free(backend);
		}
	} else {

		if (git_config_backend_open(backend, level, NULL) == 0) {
			git_config_entry *entry = NULL;

			if (git_config_backend_get_string(&entry, backend, "section.key") == 0)
				git_config_entry_free(entry);

			git_config_backend_foreach(backend, config_foreach_cb, &touched);

			git_config_backend_foreach_match(backend, "^general\\.",
				config_foreach_cb, &touched);
		}

		git_config_backend_free(backend);
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	uint8_t ctrl;
	git_config_backend_memory_options opts = GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT;
	git_config_backend_memory_options *opts_ptr = NULL;
	char backend_type_buf[32];
	char origin_path_buf[32];
	git_config_level_t level;
	git_config_backend *backend = NULL;

	if (size < 1)
		return 0;

	ctrl = data[0];
	data += 1;
	size -= 1;

	if (ctrl & 0x01) {
		size_t bt_len = 0, op_len = 0;

		if (size >= 1) {
			bt_len = data[0] % sizeof(backend_type_buf);
			data += 1;
			size -= 1;
		}
		if (size >= bt_len) {
			memcpy(backend_type_buf, data, bt_len);
			backend_type_buf[bt_len] = '\0';
			data += bt_len;
			size -= bt_len;
			opts.backend_type = backend_type_buf;
		}

		if (size >= 1) {
			op_len = data[0] % sizeof(origin_path_buf);
			data += 1;
			size -= 1;
		}
		if (size >= op_len) {
			memcpy(origin_path_buf, data, op_len);
			origin_path_buf[op_len] = '\0';
			data += op_len;
			size -= op_len;
			opts.origin_path = origin_path_buf;
		}

		opts_ptr = &opts;
	}

	level = (git_config_level_t)(GIT_CONFIG_LEVEL_SYSTEM +
		((ctrl >> 1) % (GIT_CONFIG_LEVEL_APP - GIT_CONFIG_LEVEL_SYSTEM + 1)));

	if ((ctrl & 0x08) && size > 0) {
		size_t chunk_len = size < 64 ? size : 64;
		char chunk[65];
		char *values_buf;
		const char *values[16];
		size_t n_values = 0;
		size_t i, start;
		git_config_backend *vbackend = NULL;

		memcpy(chunk, data, chunk_len);
		chunk[chunk_len] = '\0';

		values_buf = (char *)malloc(chunk_len + 1);
		if (values_buf) {
			memcpy(values_buf, chunk, chunk_len + 1);

			start = 0;
			for (i = 0; i <= chunk_len && n_values < 16; i++) {
				if (i == chunk_len || values_buf[i] == '\n') {
					values_buf[i] = '\0';
					if (i > start)
						values[n_values++] = &values_buf[start];
					start = i + 1;
				}
			}

			if (n_values > 0 &&
			    git_config_backend_from_values(&vbackend, values, n_values, opts_ptr) == 0)
				drive_backend(vbackend, level, (ctrl & 0x10) != 0);

			free(values_buf);
		}

		data += chunk_len;
		size -= chunk_len;
	}

	if (git_config_backend_from_string(&backend, (const char *)data, size, opts_ptr) < 0)
		return 0;

	drive_backend(backend, level, (ctrl & 0x04) != 0);

	return 0;
}
