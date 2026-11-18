#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config_backend.h"

#include "git2/config.h"
#include "git2/sys/config.h"
#include "git2/transaction.h"
#include "git2/buffer.h"

#include "standalone_driver.h"

static int noop_foreach_cb(const git_config_entry *entry, void *payload)
{
	GIT_UNUSED(entry);
	GIT_UNUSED(payload);
	return 0;
}

static int abort_after_first_cb(const git_config_entry *entry, void *payload)
{
	GIT_UNUSED(entry);
	GIT_UNUSED(payload);
	return 1;
}

struct capture_ctx {
	char key[256];
	int captured;
};

static int capture_first_key_cb(const git_config_entry *entry, void *payload)
{
	struct capture_ctx *ctx = (struct capture_ctx *)payload;

	if (!ctx->captured && entry && entry->name) {
		size_t len = strlen(entry->name);
		if (len >= sizeof(ctx->key))
			len = sizeof(ctx->key) - 1;
		memcpy(ctx->key, entry->name, len);
		ctx->key[len] = '\0';
		ctx->captured = 1;
	}

	return 0;
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	GIT_UNUSED(argc);
	GIT_UNUSED(argv);

	if (git_libgit2_init() < 0) {
		fprintf(stderr, "Failed to initialize libgit2\n");
		abort();
	}
	return 0;
}

static void exercise_backend(git_config_backend *backend)
{
	struct capture_ctx ctx;

	if (!backend)
		return;

	memset(&ctx, 0, sizeof(ctx));

	git_config_backend_foreach(backend, capture_first_key_cb, &ctx);

	if (ctx.captured) {
		git_config_entry *entry = NULL;

		if (git_config_backend_get_string(&entry, backend, ctx.key) == 0)
			git_config_entry_free(entry);

		git_config_backend_foreach_match(backend, ".*", noop_foreach_cb, NULL);

		git_config_backend_foreach_match(backend, "^nonexistent-prefix$", noop_foreach_cb, NULL);

		git_config_backend_foreach_match(backend, "[", noop_foreach_cb, NULL);

		git_config_backend_foreach_match(backend, NULL, abort_after_first_cb, NULL);
	}

	{
		git_config_entry *entry = NULL;

		if (git_config_backend_get_string(&entry, backend, "this.key.should.not.exist") == 0)
			git_config_entry_free(entry);
	}

	git_config_backend_foreach(backend, noop_foreach_cb, NULL);

	git_config_backend_set_string(backend, "section.key", "value");
	git_config_backend_delete(backend, "section.key");
	git_config_backend_lock(backend);
	git_config_backend_unlock(backend, 0);
}

static const char *test_keys[] = {
	"section.key",
	"nodotkey",
	".leadingdot",
	"trailing.",
	"sec!tion.key",
	"section.key!bad",
	"SECTION.KEY",
	"a.b.c.d",
	"sec\ntion.key",
	"-section.key",
	"section.",
};

static void exercise_key_validation(git_config *cfg)
{
	size_t k;

	for (k = 0; k < sizeof(test_keys) / sizeof(test_keys[0]); k++) {
		int32_t i32;
		int b;
		git_config_entry *entry = NULL;

		git_config_get_int32(&i32, cfg, test_keys[k]);
		git_config_get_bool(&b, cfg, test_keys[k]);
		git_config_set_string(cfg, test_keys[k], "value");

		if (git_config_get_entry(&entry, cfg, test_keys[k]) == 0)
			git_config_entry_free(entry);
	}
}

static void exercise_config_object(git_config_backend *backend)
{
	git_config *cfg = NULL;
	git_config *snap = NULL;

	if (!backend)
		return;

	if (git_config_new(&cfg) < 0) {
		backend->free(backend);
		return;
	}

	if (git_config_add_backend(cfg, backend, GIT_CONFIG_LEVEL_LOCAL, NULL, 1) < 0) {
		backend->free(backend);
		git_config_free(cfg);
		return;
	}

	{
		git_config_entry *entry = NULL;

		if (git_config_get_entry(&entry, cfg, "this.key.should.not.exist") == 0)
			git_config_entry_free(entry);

		entry = NULL;
		if (git_config_get_entry(&entry, cfg, "section.key") == 0)
			git_config_entry_free(entry);
	}

	{
		int32_t i32;
		int64_t i64;
		int b;
		git_buf buf = GIT_BUF_INIT;

		git_config_get_int32(&i32, cfg, "section.key");
		git_config_get_int64(&i64, cfg, "section.key");
		git_config_get_bool(&b, cfg, "section.key");
		git_config_get_path(&buf, cfg, "section.key");
		git_buf_dispose(&buf);
	}

	exercise_key_validation(cfg);

	if (git_config_snapshot(&snap, cfg) == 0) {
		const char *str = NULL;
		git_buf buf = GIT_BUF_INIT;

		git_config_get_string(&str, snap, "section.key");
		git_config_get_string_buf(&buf, snap, "section.key");
		git_buf_dispose(&buf);
		git_config_free(snap);
	}

	git_config_get_multivar_foreach(cfg, "section.key", NULL, noop_foreach_cb, NULL);
	{
		git_config_iterator *mviter = NULL;

		if (git_config_multivar_iterator_new(&mviter, cfg, "section.key", NULL) == 0) {
			git_config_entry *e;

			while (git_config_next(&e, mviter) == 0)
				;
			git_config_iterator_free(mviter);
		}
	}

	git_config_foreach(cfg, noop_foreach_cb, NULL);
	git_config_foreach_match(cfg, ".*", noop_foreach_cb, NULL);

	{
		git_config_iterator *it = NULL;

		if (git_config_iterator_new(&it, cfg) == 0) {
			git_config_entry *e;

			while (git_config_next(&e, it) == 0)
				;
			git_config_iterator_free(it);
		}
	}
	{
		git_config_iterator *it = NULL;

		if (git_config_iterator_glob_new(&it, cfg, ".*") == 0) {
			git_config_entry *e;

			while (git_config_next(&e, it) == 0)
				;
			git_config_iterator_free(it);
		}
	}

	git_config_set_string(cfg, "section.key", "value");
	git_config_set_int32(cfg, "section.key", 1);
	git_config_set_int64(cfg, "section.key", 1);
	git_config_set_bool(cfg, "section.key", 1);
	git_config_set_multivar(cfg, "section.key", ".*", "value2");
	git_config_delete_entry(cfg, "section.key");
	git_config_delete_multivar(cfg, "section.key", ".*");

	{
		static const git_configmap maps[] = {
			{ GIT_CONFIGMAP_FALSE, NULL, 0 },
			{ GIT_CONFIGMAP_TRUE, NULL, 1 },
			{ GIT_CONFIGMAP_STRING, "input", 2 },
			{ GIT_CONFIGMAP_STRING, "default", 3 },
		};
		int mapped_out;

		git_config_get_mapped(&mapped_out, cfg, "section.key", maps, 4);
		git_config_lookup_map_value(&mapped_out, maps, 4, "input");
		git_config_lookup_map_value(&mapped_out, maps, 4, "nonexistent-value");
	}

	{
		git_transaction *tx = NULL;

		if (git_config_lock(&tx, cfg) == 0) {
			git_transaction_commit(tx);
			git_transaction_free(tx);
		}
	}

	{
		int out_b;
		int32_t out_i32;
		int64_t out_i64;
		git_buf path_buf = GIT_BUF_INIT;

		git_config_parse_bool(&out_b, "true");
		git_config_parse_bool(&out_b, "not-a-bool");
		git_config_parse_int32(&out_i32, "123k");
		git_config_parse_int64(&out_i64, "123g");
		git_config_parse_path(&path_buf, "~/foo");
		git_buf_dispose(&path_buf);
	}

	git_config_free(cfg);
}

static void exercise_parser_edge_cases(void)
{
	static const struct {
		const char *data;
		size_t len;
	} cases[] = {

		{ "\xEF\xBB\xBF[core]\n\tbare = true\n", 20 },

		{ "[sec]\n\tkey = part1\\\npart2\n", 27 },

		{ "[sec]\n\tkey = \"bad\\q\"\n", 22 },

		{ "[remote \"origin]\n\turl = x\n", 27 },

		{ "[remote \"origin\"x]\n\turl = x\n", 29 },

		{ "[sec]\n\tkey = a\n\tkey = b\n", 25 },

		{ "[sec]\n\tkey = a\\\n; comment\nb\n", 29 },

		{ "[sec]\n\tflag\n", 12 },

		{ "[remote \"\"]\n\turl = x\n", 21 },

		{ "[remote origin]\n\turl = x\n", 26 },
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		git_config_backend *b = NULL;

		if (git_config_backend_from_string(&b, cases[i].data, cases[i].len, NULL) == 0) {
			git_config_backend_open(b, 0, NULL);
			exercise_backend(b);
			git_config_backend_free(b);
		}
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	git_config_backend *backend = NULL;
	git_config_backend_memory_options opts =
		GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT;
	git_config_backend_memory_options *opts_ptr = &opts;
	const char *cfg;
	unsigned char variant = 3;

	exercise_parser_edge_cases();

	if (size > 0)
		variant = data[0] & 0x3;

	switch (variant) {
	case 0:
		opts_ptr = NULL;
		break;
	case 1:
		opts.backend_type = NULL;
		opts.origin_path  = "fuzz-origin";
		break;
	case 2:
		opts.backend_type = "fuzz-backend";
		opts.origin_path  = NULL;
		break;
	default:
		opts.backend_type = "fuzz-backend";
		opts.origin_path  = "fuzz-origin";
		break;
	}

	if (size == 0)
		cfg = NULL;
	else
		cfg = (const char *)data;

	if (git_config_backend_from_string(&backend, cfg, size, opts_ptr) < 0)
		return 0;

	git_config_backend_open(backend, 0, NULL);

	git_config_backend_open(backend, 0, NULL);

	exercise_backend(backend);

	{
		git_config_backend *snapshot = NULL;

		if (backend->snapshot &&
		    backend->snapshot(&snapshot, backend) == 0 && snapshot) {
			git_config_backend_open(snapshot, 0, NULL);
			exercise_backend(snapshot);
			git_config_backend_free(snapshot);
		}
	}

	git_config_backend_free(backend);

	{
		git_config_backend *cfg_backend = NULL;

		if (git_config_backend_from_string(&cfg_backend, cfg, size, opts_ptr) == 0)
			exercise_config_object(cfg_backend);
	}

	return 0;
}