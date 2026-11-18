## target: git_config_backend_from_string

### Summary of behaviour
`git_config_backend_from_string()` (src/libgit2/config_mem.c:345-364) allocates a
`config_memory_backend` (via `config_backend_new()`), copies the raw bytes
`[cfg, cfg+len)` into `backend->cfg` (a `git_str`) with `git_str_set()`, and
returns the new backend through `*out`. **No parsing occurs here** — parsing is
deferred to the backend's `open` vtable entry (`config_memory_open`), which is
only invoked when the harness/caller calls `git_config_backend_open()` (or
`git_config_add_backend()`, which calls `open` internally).

```
src/libgit2/config_mem.c:345  int git_config_backend_from_string(
    git_config_backend **out, const char *cfg, size_t len,
    git_config_backend_memory_options *opts)
src/libgit2/config_mem.c:353  if ((backend = config_backend_new(opts)) == NULL) return -1;
src/libgit2/config_mem.c:356  if (git_str_set(&backend->cfg, cfg, len) < 0) { ...free...; return -1; }
src/libgit2/config_mem.c:362  *out = (git_config_backend *)backend;
```

### P2.1 Init Sequence
**Claim:** No library-wide init is required before the target beyond libgit2's
general `git_libgit2_init()` convention (standard for all libgit2 entry points,
not special to this feature). Before the target, a caller may optionally build a
`git_config_backend_memory_options` struct — either zero-initialized with the
`GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT` macro, or via
`git_config_backend_memory_options_init(&opts, GIT_CONFIG_BACKEND_MEMORY_OPTIONS_VERSION)`
— and pass its address (or NULL) as the 4th argument. No other backend/object
must exist first.
**Evidence:**
> tests/libgit2/config/memory.c:70-77 (setup_backend): `git_config_backend_memory_options opts = GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT; opts.backend_type = "test"; cl_git_pass(git_config_backend_from_string(&backend, cfg, strlen(cfg), &opts)); cl_git_pass(git_config_backend_open(backend, 0, NULL));`
> include/git2/sys/config.h:193-207: doc for the target says `opts` may be NULL.
> src/libgit2/config_mem.c:325-333 (config_backend_new): if `opts` is NULL, `backend_type` defaults to `"in-memory"` and `origin_path` stays NULL — the function tolerates a NULL opts pointer.

### P2.2 Parameter Construction
**Claim:**
- `out`: non-NULL `git_config_backend **`, receives the new backend pointer on success; left untouched on failure (caller must not assume it's NULL on error — see P2.4).
- `cfg`: pointer to `len` bytes; need **not** be NUL-terminated (the bytes are copied with `git_str_set(&backend->cfg, cfg, len)`, a raw memcpy-based copy, not `strdup`). `len` may be 0, in which case `open()` later skips parsing entirely.
- `len`: `size_t`, independent of `strlen(cfg)` — embedded NULs are copied verbatim and will show up to the line-based parser as data (not necessarily terminators, see P2.11).
- `opts`: either NULL or a pointer to a `git_config_backend_memory_options` whose `version` field should be `GIT_CONFIG_BACKEND_MEMORY_OPTIONS_VERSION` (1). `backend_type` and `origin_path` are `const char *` C-strings (NUL-terminated, since they're `git__strdup`'d) or NULL.
**Evidence:**
> src/libgit2/config_mem.c:356: `git_str_set(&backend->cfg, cfg, len)` — raw length-based copy, no `strlen` call on `cfg`.
> src/libgit2/config_mem.c:325-333: `backend->backend_type = git__strdup(opts && opts->backend_type ? opts->backend_type : "in-memory");` and `backend->origin_path = git__strdup(opts->origin_path)` — both `git__strdup`, i.e. expect NUL-terminated strings.
> include/git2/sys/config.h:153-167: struct field doc, `version` field present but **not actually version-checked** inside `config_backend_new()`/`git_config_backend_from_string()` (no `GIT_ERROR_CHECK_VERSION` call found in config_mem.c).

### P2.3 Object Lifecycle
**Claim:** Lifecycle is **create (target) → open → use (get/foreach, or add to a `git_config` and use generic config API) → free**.
- Create: `git_config_backend_from_string()` (or `_from_values`).
- Open: `git_config_backend_open(backend, level, repo)` — required before any query; `repo` may be NULL (only used by non-memory backends for conditional includes; `config_memory_open` does `GIT_UNUSED(repo)`).
- Use: `git_config_backend_get_string()`, `git_config_backend_foreach()` (direct backend API) — OR register with `git_config_add_backend()` and then use `git_config_get_string`/`git_config_get_multivar`/`git_config_foreach` (generic config API), which calls `backend->open` itself.
- Destroy: `git_config_backend_free(backend)` — **only if the backend was never handed to `git_config_add_backend()`**; once added to a `git_config`, the config object owns and frees it (see P2.5/P2.7).
**Evidence:**
> tests/libgit2/config/memory.c:68-77 (setup_backend): create → open → (later) get_string/foreach.
> tests/libgit2/config/memory.c:12-15 (test_config_memory__cleanup): `git_config_backend_free(backend);` — direct free when never added to a `git_config`.
> src/libgit2/config.c:377 (git_config_add_backend): `if ((result = backend->open(backend, level, repo)) < 0) return result;` — add_backend calls open for you.
> include/git2/sys/config.h:120-124: "Note that the configuration object will free the file automatically" (doc for `git_config_add_backend`).

### P2.4 Return Value Handling
**Claim:** `git_config_backend_from_string()` returns `0` on success, `-1` on failure (OOM during `config_backend_new()` or `git_str_set()`); it does **not** use the richer libgit2 error-code enum (`GIT_EUSER` etc.), just 0/-1. On failure it fully cleans up its own partial allocation (frees `config_list`/`backend_type`/`origin_path`/backend struct) — the caller does not need to free anything if the call failed. `git_config_backend_open()` (`cfg->open`) returns 0 on success and a negative error code if the embedded config text fails to parse (e.g. malformed section header); in that case the backend object itself is still valid and must still be freed by the caller.
**Evidence:**
> src/libgit2/config_mem.c:353-359: `if ((backend = config_backend_new(opts)) == NULL) return -1;` and `if (git_str_set(...) < 0) { git_config_list_free(backend->config_list); git__free(backend); return -1; }`
> src/libgit2/config_mem.c:337-342 (config_backend_new on_error path): frees config_list/origin_path/backend_type/backend on allocation failure.
> tests/libgit2/config/memory.c:108-121 (test_config_memory__malformed_fails_to_open): `cl_git_pass(git_config_backend_from_string(...))` (create succeeds) then `cl_git_fail(git_config_backend_open(backend, 0, NULL))` (open fails due to malformed `[general` section) — backend created successfully even though later open fails; test's own `cleanup` still frees it via `git_config_backend_free(backend)`.

### P2.5 Cleanup Sequence
**Claim:**
- If the backend is **never** added to a `git_config` object (used standalone via `git_config_backend_open` + `git_config_backend_get_string`/`foreach`): caller must call `git_config_backend_free(backend)` exactly once, regardless of whether `open()` succeeded or failed.
- If the backend **is** added via `git_config_add_backend(cfg, backend, level, repo, force)`: ownership transfers to `cfg`; caller must **not** call `git_config_backend_free()` on it directly — instead call `git_config_free(cfg)`, which frees all attached backends. Calling `git_config_backend_free` after a successful `add_backend` would double free.
- `config_memory_free()` (the backend's `free` vtable fn) itself frees `origin_path`, `backend_type`, `config_list` (via `git_config_list_free`, which is refcounted), the `values` array (`git_strlist_free`), and `cfg` (`git_str_dispose`), then the backend struct.
- Any `git_config_entry*` obtained from `git_config_backend_get_string()` (wraps `git_config_backend_entry`) must be released with `git_config_entry_free()` separately from the backend.
**Evidence:**
> src/libgit2/config_mem.c:273-286 (config_memory_free): frees origin_path, backend_type, config_list, values, cfg, backend — in that order.
> src/libgit2/config_backend.h:45-49 (git_config_backend_free): `if (cfg) cfg->free(cfg);`
> tests/libgit2/config/memory.c:12-15 and :5-15: `static git_config_backend *backend;` freed once per test in `cleanup`.
> tests/libgit2/config/backend.c:12-24 (checks_version test): creates cfg with `git_config_new`, calls `git_config_add_backend` (which fails due to bad version, so ownership is NOT transferred), then `git_config_free(cfg)` — note here `backend` itself is a stack struct, not heap, illustrating add_backend failure leaves ownership with caller.
> tests/libgit2/config/memory.c:17-24 (assert_config_contains): `git_config_entry_free(entry);` after `git_config_backend_get_string`.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations.
**Evidence:**
> `git_config_backend_from_string` — include/git2/sys/config.h:203; src/libgit2/config_mem.c:345
> `git_config_backend_from_values` — include/git2/sys/config.h:219; src/libgit2/config_mem.c:366
> `git_config_backend_memory_options_init` — include/git2/sys/config.h:188; src/libgit2/config_mem.c:288
> `git_config_backend_open` (inline) — src/libgit2/config_backend.h:40
> `git_config_backend_get_string` (inline) — src/libgit2/config_backend.h:51
> `git_config_backend_foreach` (inline, wraps `git_config_backend_foreach_match`) — src/libgit2/config_backend.h:76; declared in include/git2/config.h:852
> `git_config_backend_free` (inline) — src/libgit2/config_backend.h:45
> `git_config_backend_set_string` / `_delete` / `_lock` / `_unlock` (inline) — src/libgit2/config_backend.h:64,70,84,89 (always fail for this backend, see below)
> `git_config_add_backend` — include/git2/sys/config.h:140; src/libgit2/config.c:362
> `git_config_entry_free` — include/git2/config.h:126
> Core: `config_memory_open` src/libgit2/config_mem.c:176; `parse_config` :95; `parse_values` :125; `read_variable_cb` :44; `git_config_list_append` (declared in src/libgit2/config_list.h, used at config_mem.c:89,169).

### P2.7 Co-call Constraints
**Claim:**
- `git_config_backend_from_string`/`_from_values` (create) must be paired with exactly one `git_config_backend_free` **unless** ownership is transferred via `git_config_add_backend` (then pair with `git_config_free` on the owning `git_config`, not `git_config_backend_free`).
- `git_config_backend_open(backend, level, repo)` must be called (directly, or implicitly inside `git_config_add_backend`) before `git_config_backend_get_string`/`git_config_backend_foreach`/generic `git_config_get_*` calls — the memory backend's `config_list` is empty until `open` runs `parse_config`/`parse_values`.
- `git_config_backend_get_string` → `git_config_entry_free` is a required pair (each successful get must be matched by one free of the returned entry).
- Write-side vtable entries (`set`, `set_multivar`, `del`, `del_multivar`, `lock`, `unlock`) are **always** `config_error_readonly()` for this backend (lines 227-271 config_mem.c) — they exist on the vtable (so can be "paired"-called by generic code) but there is no valid lock/unlock or set/commit sequence to exercise; every call returns -1 and sets `GIT_ERROR_CONFIG, "this backend is read-only"`.
**Evidence:**
> src/libgit2/config_mem.c:176-191 (config_memory_open): only this populates `memory_backend->config_list`; `config_memory_get`/`config_memory_iterator` (lines 193-225) simply query `memory_backend->config_list` with no fallback parse.
> src/libgit2/config_mem.c:227-271: `config_memory_set`, `config_memory_set_multivar`, `config_memory_delete`, `config_memory_delete_multivar`, `config_memory_lock`, `config_memory_unlock` all `return config_error_readonly();`
> tests/libgit2/config/memory.c:90-97 (test_config_memory__write_operations_fail): confirms all four of set_string/delete/lock/unlock fail after `setup_backend` (create+open).
> src/libgit2/config.c:377-384 (git_config_add_backend): open called once, then backend attached to `config` (`instance->backend->cfg = config`) — subsequent `git_config_free(config)` frees it (see config.c:86 `git_config_free`).

### P2.8 Prerequisite State
**Claim:** No repository, index, or other libgit2 object must exist first for `git_config_backend_from_string`/`open`/`get`/`foreach` to work standalone — `repo` argument to `open` can be NULL and is unused by the memory backend (`GIT_UNUSED(repo)` in `config_memory_open`). A `git_repository*` is only meaningful if the backend is later added via `git_config_add_backend(cfg, backend, level, repo, force)` where `repo` enables conditional includes for other backend types (not used by memory backend). To use the generic `git_config_get_string` etc. path, a `git_config*` object must first exist (`git_config_new(&cfg)`).
**Evidence:**
> src/libgit2/config_mem.c:176-180 (config_memory_open): `GIT_UNUSED(repo);`
> tests/libgit2/config/memory.c:76: `git_config_backend_open(backend, 0, NULL)` — repo passed as NULL in all tests.
> tests/libgit2/config/backend.c:12 (`cl_git_pass(git_config_new(&cfg));`) — required before `git_config_add_backend`.
> src/libgit2/config.c:372-373: `GIT_ASSERT_ARG(config); GIT_ASSERT_ARG(backend);` in `git_config_add_backend` — both must be non-NULL.

### P2.9 Result Usage
**Claim:** The backend object produced by the target (after `open`) is consumed in two ways by real callers:
1. Directly via the backend-level API: `git_config_backend_get_string(&entry, backend, name)` → returns a `git_config_entry*` whose `->value`/`->name` are read then released with `git_config_entry_free(entry)`; and `git_config_backend_foreach(backend, cb, payload)` → invokes `cb(const git_config_entry *entry, void *payload)` once per stored (lower-cased, dotted) key, including multivar duplicates.
2. Indirectly via `git_config_add_backend(cfg, backend, level, repo, force)`, after which the generic `git_config_get_string()`, `git_config_get_multivar()`, `git_config_foreach()` (operating on `git_config *cfg`, not the backend directly) can query it, going through `config_memory_get`/`config_memory_iterator` → `git_config_list_get`/`git_config_list_iterator_new`.
**Evidence:**
> tests/libgit2/config/memory.c:17-24 (assert_config_contains): `git_config_backend_get_string(&entry, backend, name)` then `cl_assert_equal_s(entry->value, value); git_config_entry_free(entry);`
> tests/libgit2/config/memory.c:32-66 (contains_all_cb / assert_config_contains_all): `git_config_backend_foreach(backend, contains_all_cb, entries)` — iterates every stored entry, checks name/value, including the duplicate-key ("multivar") case at lines 154-167.
> src/libgit2/config_mem.c:193-205 (config_memory_get): `git_config_list_get(&entry, memory_backend->config_list, key)` then `*out = &entry->base;` — this is what backs `git_config_backend_get_string`.
> src/libgit2/config_mem.c:207-225 (config_memory_iterator): `git_config_list_dup` + `git_config_list_iterator_new` — backs generic iteration/foreach.
> tests/libgit2/config/backend.c (checks_version) and tests/libgit2/config/readonly.c:19-29 show the `git_config_add_backend` + generic `git_config_set_string`/`git_config_get_*` pattern (for the on-disk file backend, same `git_config_add_backend` contract applies to the memory backend).

### P2.10 Behaviour Controls
**Claim:**
- `len == 0` on the target (`git_config_backend_from_string`) selects a no-op `open()`: `config_memory_open` only calls `parse_config()` `if (memory_backend->cfg.size > 0)` (config_mem.c:182-184) — size-0 input never reaches the parser.
- Malformed config text (e.g. missing `]` in a section header) makes `parse_config`/`git_config_parse` return < 0, which makes `open()`/`config_memory_open` return -1, which (if used via `git_config_add_backend`) makes the whole `add_backend` call fail — demonstrated by `test_config_memory__malformed_fails_to_open` (tests/libgit2/config/memory.c:108-121).
- Empty/garbage "key" lines for `git_config_backend_from_values` (variable with no `=`, i.e. `name_len==0` after `strchr` returns NULL... actually note `eql - memory_backend->values[i]` when `eql` is NULL is undefined pointer arithmetic — see parse_values at config_mem.c:142-149) can make `parse_values` fail (`git_error_set(... "empty config key")`) — demonstrated by `test_config_memory__valid_values` (memory.c:192-207) which expects `git_config_backend_open` to fail for `"=bar2"`.
- `opts == NULL` vs populated `opts` selects default `"in-memory"` backend_type/no origin_path vs custom values that propagate into every entry's `backend_type`/`origin_path` fields (config_mem.c:325-333), later readable via `entry->backend_type`/`entry->origin_path` (`git_config_entry` fields, consumed by callers of `get`/`foreach`).
- `level` parameter to `git_config_backend_open`/`git_config_add_backend` (a `git_config_level_t`) is stamped on every produced entry (`entry->base.entry.level = parse_data->level` / `= level`) and controls priority ordering when later queried through a `git_config` with multiple backends (see `git_config_add_backend` doc, include/git2/sys/config.h:126-128).
**Evidence:**
> src/libgit2/config_mem.c:182-188 (config_memory_open): `if (memory_backend->cfg.size > 0 && parse_config(...) < 0) return -1; if (memory_backend->values_len > 0 && parse_values(...) < 0) return -1;` — text-parse and values-parse paths are mutually selected by which constructor was used (from_string populates `cfg`, from_values populates `values`).
> tests/libgit2/config/memory.c:108-121 (malformed_fails_to_open).
> tests/libgit2/config/memory.c:192-207 (valid_values: `"=bar2"` with empty key -> open fails).
> src/libgit2/config_mem.c:142-149 (parse_values): `eql = strchr(...,'='); name_len = eql - memory_backend->values[i]; if (name_len == 0) { ...error...}` — note no NULL-check on `eql` before the subtraction, so a value string with **no** `=` at all would compute `name_len` from a NULL `eql`, which is undefined behavior; the `git_config_backend_from_values` input (not the fuzz target itself, but reachable from the harness if it also drives `_from_values`) must therefore always contain `=` in every element, OR this is itself a bug worth noting to the harness author as a crash path distinct from the string-based target.
> src/libgit2/config_mem.c:325-333 (backend_type/origin_path defaulting).

### P2.11 Input Expectations
**Claim:** `cfg`/`len` are expected to be the raw bytes of a git config file (INI-like format), **not required to be NUL-terminated** at `cfg[len]` (the library makes its own copy via `git_str_set`, which internally NUL-terminates its own buffer after copying `len` bytes — see git_str semantics). The parser (`git_config_parser_init`/`git_config_parse`, invoked only from `config_memory_open`/`parse_config`) is a line-oriented state machine; per `parse_config`:
- It is initialized with `git_config_parser_init(&parser, "in-memory", memory_backend->cfg.ptr, memory_backend->cfg.size)` — the name `"in-memory"` is used only for error messages/origin, not semantics.
- Embedded NUL bytes inside `[0, len)` are part of the data the parser walks over (the API accepts `len` distinct from `strlen`), so premature NULs can be embedded to test parser robustness around pointer arithmetic (per the feature description: section headers, quoting, escapes, line continuation, UTF-8 BOM).
- Zero-length input (`len == 0`) is valid and produces an empty, successfully-openable config with no entries (`cfg.size > 0` guard in `config_memory_open`).
**Evidence:**
> src/libgit2/config_mem.c:103-104 (parse_config): `git_config_parser_init(&parser, "in-memory", memory_backend->cfg.ptr, memory_backend->cfg.size)`.
> src/libgit2/config_mem.c:182: `if (memory_backend->cfg.size > 0 && parse_config(...) < 0)` — size 0 bypasses the parser entirely, i.e. is explicitly valid/cheap input.
> src/libgit2/config_mem.c:356: `git_str_set(&backend->cfg, cfg, len)` copies exactly `len` bytes regardless of embedded NULs (no `strlen` call on `cfg` anywhere in `git_config_backend_from_string`).
> include/git2/sys/config.h:193-202 (doc comment on target): "Create an in-memory configuration backend from a string in standard git configuration file format." / "@param len the length of the string pointed to by `cfg`" — confirms length-based, not NUL-based, semantics.

## Other entries of the Logic Group

### git_config_backend_open (entry: cfg->open -> config_memory_open)
**Call protocol:** Call once, directly after the target (or implicitly via `git_config_add_backend`). Signature: `GIT_INLINE(int) git_config_backend_open(git_config_backend *cfg, unsigned int level, const git_repository *repo)` (src/libgit2/config_backend.h:40), simply forwards to `cfg->open(cfg, level, repo)`. `level` is a `git_config_level_t` value (e.g. `0`/`GIT_CONFIG_LEVEL_LOCAL` used in tests); `repo` may be NULL for the memory backend (unused). Returns 0 on success, <0 if the embedded data fails to parse (malformed section headers, empty keys in values array, etc. — see P2.10). Must be called exactly once per backend before any `get`/`foreach`/`iterator` call; calling it twice is not exercised/guarded against in config_mem.c (idempotency not guaranteed — second call would re-run `parse_config`, appending duplicate entries into the already-populated `config_list`). Nothing to free specific to `open()` itself; cleanup is via `git_config_backend_free`/`git_config_free` as in P2.5.
**Evidence:** src/libgit2/config_backend.h:40-43; src/libgit2/config_mem.c:176-191; tests/libgit2/config/memory.c:75-76, 119-120.
**Combination with target:** `git_config_backend_from_string(&backend, data, len, &opts)` → `git_config_backend_open(backend, level, repo)` is the minimal two-call sequence to actually exercise the parser (per the feature description, `open` is the real trigger of the core parsing code).

### git_config_backend_from_values (entry, alternate source)
**Call protocol:** Same constructor shape as the target but takes a `const char **values` array of `len` elements, each expected to be a NUL-terminated `"name=value"` or `"name"` string (`git_strlist_copy` just copies the pointers/strings, it does not validate format — format validation/splitting happens lazily in `parse_values`, called from `config_memory_open`, not at construction time). Returns 0/-1 like the target (OOM on `git_strlist_copy` is the only failure at construction). Must be followed by `git_config_backend_open` to actually populate the list (parse_values runs here); an entry with no `=` char (`name_len==0` from empty key, or worse, `eql==NULL` entirely — see P2.10) is a **distinct** malformed-input path from the target's own grammar errors. Freed the same way as the target's output (`git_config_backend_free` or via owning `git_config`).
**Evidence:** src/libgit2/config_mem.c:366-386 (git_config_backend_from_values); src/libgit2/config_mem.c:125-174 (parse_values); src/util/strlist.c:14 (git_strlist_copy); tests/libgit2/config/memory.c:79-88,169-207.
**Combination with target:** Not called together with the target to build a single backend (they are alternative constructors for two different `config_memory_backend` instances); a harness could drive either/both to exercise `parse_config` (text grammar) vs `parse_values` (pre-split pairs) independently — both still require `git_config_backend_open` afterward.

### git_config_backend_memory_options_init (entry, configuration helper)
**Call protocol:** `int git_config_backend_memory_options_init(git_config_backend_backend_memory_options *opts, unsigned int version)` — call with `version = GIT_CONFIG_BACKEND_MEMORY_OPTIONS_VERSION` (1) before filling in `backend_type`/`origin_path`, as an alternative to the static initializer macro `GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT`. `GIT_ASSERT_ARG(opts)` — `opts` must not be NULL (will assert/return error otherwise). Returns 0 on success, -1 on failure (bad version via `GIT_INIT_STRUCTURE_FROM_TEMPLATE`). No resources to free (plain struct init, no heap allocation). Not required before calling the target — target accepts `opts == NULL` or a raw `GIT_CONFIG_BACKEND_MEMORY_OPTIONS_INIT`-initialized struct equally well; this helper exists mainly for ABI-stable struct versioning when `backend_type`/`origin_path` need to be customized.
**Evidence:** include/git2/sys/config.h:175-190; src/libgit2/config_mem.c:288-298; src/libgit2/common.h:29-31 (GIT_INIT_STRUCTURE_FROM_TEMPLATE); tests/libgit2/core/structinit.c:310 (exercises it generically for struct-version regression testing, not combined with the memory backend there).
**Combination with target:** Optional pre-step: `git_config_backend_memory_options_init(&opts, GIT_CONFIG_BACKEND_MEMORY_OPTIONS_VERSION); opts.backend_type = "..."; opts.origin_path = "..."; git_config_backend_from_string(&backend, cfg, len, &opts);`

### git_config_backend_get_string (entry, result consumer)
**Call protocol:** `GIT_INLINE(int) git_config_backend_get_string(git_config_entry **out, git_config_backend *cfg, const char *name)` (src/libgit2/config_backend.h:51-62). Requires the backend to already be **open** (see P2.1/P2.7). `name` is a dotted lower-case key (e.g. `"general.foo"`) matched exactly against what `read_variable_cb`/`parse_values` stored (case-folded section+key). Returns 0 and fills `*out` on success, or the error from `cfg->get` (e.g. `GIT_ENOTFOUND`) on failure, in which case `*out` is untouched. On success, caller must call `git_config_entry_free(*out)` after reading `entry->value`/`entry->name` (freed independently of the backend).
**Evidence:** src/libgit2/config_backend.h:51-62; src/libgit2/config_mem.c:193-205 (config_memory_get, backs `cfg->get`); tests/libgit2/config/memory.c:17-24.
**Combination with target:** create (target) → open → `get_string(name)` per key of interest → `git_config_entry_free` each → eventually `git_config_backend_free(backend)`.

### git_config_backend_foreach (entry, result consumer)
**Call protocol:** `GIT_INLINE(int) git_config_backend_foreach(git_config_backend *cfg, int (*fn)(const git_config_entry *entry, void *data), void *data)` (src/libgit2/config_backend.h:76-82), wraps `git_config_backend_foreach_match(cfg, NULL, fn, data)` (declared include/git2/config.h:852). Requires backend already open. `fn` is invoked once per stored entry (including duplicates for multivar keys); returning non-zero from `fn` aborts iteration early and that return value propagates out (per standard libgit2 foreach convention — see test's `cl_fail`/`return -1` pattern used as an abort signal in contains_all_cb). Entries passed to `fn` are **not** owned by the caller — must not be freed inside the callback (they are freed internally through the iterator machinery / backend teardown).
**Evidence:** src/libgit2/config_backend.h:76-82; include/git2/config.h:852 (git_config_backend_foreach_match decl); tests/libgit2/config/memory.c:32-66,154-167.
**Combination with target:** create (target) → open → `foreach(cb, payload)` to enumerate all parsed entries in one pass — a good adequacy check for a fuzz harness (exercises iterator/dup path via `config_memory_iterator`→`git_config_list_dup`/`git_config_list_iterator_new`).

### git_config_add_backend (entry, integration consumer)
**Call protocol:** `int git_config_add_backend(git_config *cfg, git_config_backend *file, git_config_level_t level, const git_repository *repo, int force)` (src/libgit2/config.c:362). Preconditions: `cfg` and `file` non-NULL (`GIT_ASSERT_ARG`, config.c:372-373), `file->version == GIT_CONFIG_BACKEND_VERSION` (checked via `GIT_ERROR_CHECK_VERSION`, config.c:375 — a bad version returns `GIT_ERROR_INVALID` as demonstrated by tests/libgit2/config/backend.c:4-24). Internally calls `backend->open(backend, level, repo)` (config.c:377) — **do not** call `git_config_backend_open` separately before this; it would double-run the parser/populate duplicate entries (open is not idempotent, see above). On success, wraps `backend` in an internal `backend_instance` and adds it to `cfg`'s reader/writer vectors (`git_config__add_instance`, config.c:386); `cfg` now **owns** `backend` — freed automatically when `git_config_free(cfg)` runs. Returns 0 on success, `GIT_EEXISTS` if a backend already occupies `level` and `force==0`, or the propagated `open()` error (e.g. parse failure) otherwise — in the open-failure case, `backend` is **not** added and ownership stays with the caller (caller must still free it with `git_config_backend_free`).
**Evidence:** src/libgit2/config.c:362-392; tests/libgit2/config/backend.c:1-24 (version-check failure path, backend stays caller-owned — a stack `git_config_backend` here, illustrating non-transfer); tests/libgit2/config/readonly.c:19-29 (successful add_backend + readonly flag + later queried via generic `git_config_set_string`/entries exist until `git_config_free(cfg)`); include/git2/sys/config.h:120-145 (doc: "the configuration object will free the file automatically").
**Combination with target:** `git_config_new(&cfg)` → `git_config_backend_from_string(&backend, data, len, &opts)` → `git_config_add_backend(cfg, backend, level, NULL, force)` (do **not** also call `git_config_backend_open` on `backend` beforehand) → use generic `git_config_get_string(cfg, key)`/`git_config_get_multivar_foreach`/`git_config_foreach` → `git_config_free(cfg)` (frees `backend` too). This is the path that additionally exercises `git_config_list` regexp/multivar matching per the feature description.

## Build notes
**Claim:** Fuzzers for this project are plain C, built against the static library `libgit2.a`, with the target's header reachable via `-I../include` (which contains `git2/sys/config.h`) plus internal `-I../src/libgit2 -I../src/util` for any harness that wants direct access to internal headers like `config_backend.h`/`config_mem.c`'s peer headers. The existing `config_file_fuzzer.c` in the repo has been replaced with an empty stub (not usable as a model) — a new harness must be written from scratch following this report, but will be compiled the same way as the other `*_fuzzer.c` files.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/libgit2-c1/build.sh:40-54: compiles each `../fuzzers/*_fuzzer.c` with
> `$CC $CFLAGS -c -I./src -I./src/util -I./include/ -I./gen_headers -I../src/libgit2 -I../src/util -I../include -I../fuzzers "$fuzzer" -o "$WORK/$fuzzer_name.o"`
> then links with `$CXX $CXXFLAGS -std=c++11 -o "$OUT/$fuzzer_name" $LIB_FUZZING_ENGINE "$WORK/$fuzzer_name.o" "$WORK/fuzzer_utils.o" "$WORK/lib/libgit2.a"` — i.e. C source compiled with `$CC`, linked with `$CXX`/libFuzzer against the static `libgit2.a`.
> ${QF_WORK}/src/libgit2-c1/libgit2/fuzzers/config_file_fuzzer.c:1-13: "Gold harness replaced by an empty stub for baseline generation." — confirms no usable existing example harness for this feature; `git2/sys/config.h` (public header declaring the target) is reachable via `-I../include`.
> include/git2/sys/config.h is the public installed header path (`include/git2/sys/config.h` relative to repo root) matching the `-I../include` build flag.