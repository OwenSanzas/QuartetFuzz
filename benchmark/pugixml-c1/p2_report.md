## target: pugi::xml_document::load_buffer

### P2.1 Init Sequence
**Claim:** No library-level global init is required. The only prerequisite is a constructed `pugi::xml_document` object (default-constructed; its constructor calls `_create()`). `load_buffer` itself begins by calling `reset()` on the document (which destroys any previous tree and recreates an empty one), so it is safe to call `load_buffer` repeatedly on the same `xml_document` instance without an explicit `reset()`/destroy in between.
**Evidence:**
> src/pugixml.cpp:7871-7876: `xml_document::load_buffer(...) { reset(); return impl::load_buffer_impl(...); }`
> src/pugixml.cpp:7583-7586: `xml_document::xml_document(): _buffer(NULL) { _create(); }`
> src/pugixml.cpp:7612-7616: `xml_document::reset() { _destroy(); _create(); }`

### P2.2 Parameter Construction
**Claim:** `contents` is a caller-owned, read-only `const void*` buffer of raw bytes; `size` is its length in bytes (not characters). `load_buffer` never requires NUL-termination and does not take ownership of/modify `contents` (it is immediately `const_cast` away internally only to satisfy the shared `load_buffer_impl` signature, but `is_mutable=false, own=false` are hard-coded so the function always makes a private copy before converting/parsing — see P2.11). `options` is a bitmask of `parse_xxx` flags declared in pugixml.hpp (default `parse_default`); `encoding` is one of the `xml_encoding` enumerators (default `encoding_auto`, which triggers sniffing).
**Evidence:**
> src/pugixml.hpp:1178: `xml_parse_result load_buffer(const void* contents, size_t size, unsigned int options = parse_default, xml_encoding encoding = encoding_auto);`
> src/pugixml.hpp:1177: "Load document from buffer. Copies/converts the buffer, so it may be deleted or changed after the function returns."
> src/pugixml.cpp:7875: `impl::load_buffer_impl(..., const_cast<void*>(contents), size, options, encoding, /*is_mutable=*/false, /*own=*/false, &_buffer);`
> src/pugixml.hpp:257-268: `enum xml_encoding { encoding_auto, encoding_utf8, ..., encoding_wchar, encoding_latin1 };`

### P2.3 Object Lifecycle
**Claim:** Create `xml_document doc;` → call `load_buffer` (or any other `load_*`) → use the resulting tree via `xml_node`/`xml_attribute` traversal APIs, `print`/`save`, `append_buffer`, XPath, etc. → destroy via the `xml_document` destructor (or call `reset()`/`load_*` again to recycle). `load_buffer` plays both "configure" (encoding/options) and "use" (parse) roles in one call; there is no separate "open" step.
**Evidence:**
> src/pugixml.cpp:7871-7876 (shown above)
> src/pugixml.cpp:7588-7591: `xml_document::~xml_document() { _destroy(); }`
> tests/test_write.cpp:207: `CHECK(doc.load_buffer(contents.c_str(), contents.size()));` followed later (line ~250) by `doc.print(writer, ...)`

### P2.4 Return Value Handling
**Claim:** `load_buffer` returns `xml_parse_result` by value. Callers must check `result.status` (enum `xml_parse_status`, `status_ok` = success) or use the `operator bool()` shortcut (`true` iff `status == status_ok`). On failure, `result.offset` gives the char-unit offset in the input where parsing stopped and `result.description()` gives a human string; the already-built partial tree (if any) remains attached to the document and is still safely traversable/destructible.
**Evidence:**
> src/pugixml.hpp:1105-1124: `struct xml_parse_result { xml_parse_status status; ptrdiff_t offset; xml_encoding encoding; xml_parse_result(); operator bool() const; const char* description() const; };`
> src/pugixml.cpp:7547-7550: `xml_parse_result::operator bool() const { return status == status_ok; }`
> tests/test_dom_traverse.cpp:1033: `CHECK(doc.load_buffer(buf, sizeof(buf)));`
> tests/test_document.cpp:895: `CHECK(doc.load_buffer_inplace_own(text2, 0).status == status_no_document_element);` (example of checking `.status` directly rather than the bool conversion)

### P2.5 Cleanup Sequence
**Claim:** For `load_buffer` (copy variant) the internal converted buffer is owned by the `xml_document` (stored in its private `_buffer` member) regardless of parse success/failure, and is freed automatically by `xml_document::_destroy()`/the destructor or by the next `reset()`/`load_*` call — the caller never frees anything related to `load_buffer`'s internal copy, and the caller's original `contents` buffer is never touched/freed by pugixml (can be freed by the caller immediately after the call returns, even on failure). No other explicit free call is needed.
**Evidence:**
> src/pugixml.cpp:4841-4885 (`load_buffer_impl`): `doc->buffer = buffer;` and `*out_buffer = buffer;` tie the private converted buffer to the document's `_buffer` field (passed as `&_buffer` from `load_buffer`), so it is released whenever the document is destroyed/reset.
> src/pugixml.cpp:7612-7616: `reset()` calls `_destroy()` then `_create()`, which frees the previous buffer before building a new empty tree.
> src/pugixml.hpp:1177: "Copies/converts the buffer, so it may be deleted or changed after the function returns." (confirms caller's `contents` buffer is not owned/retained)

### P2.6 API Existence
**Claim:** All functions cited in this report exist in the current codebase at the given lines: `xml_document::load_buffer` (pugixml.cpp:7871), `load_buffer_inplace` (7878), `load_buffer_inplace_own` (7885), `load_string` (7834), `load_file` (7851, 7861), `xml_node::append_buffer` (6736), `xml_node::print` (6956, 6968, 6975), plus internal core `impl::load_buffer_impl` (4841), `impl::convert_buffer` (2212/2328 — two overloads selected by `PUGIXML_WCHAR_MODE`), `impl::get_buffer_encoding` (2103), `impl::guess_buffer_encoding` (2055), `impl::xml_parser::parse` (3608) and `parse_tree` (3327), `impl::get_strconv_attribute` (2962), `impl::get_strconv_pcdata` (2793).
**Evidence:** line numbers above, obtained via `Grep` over `src/pugixml.cpp` and `src/pugixml.hpp`.

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock or begin/commit pairing. The only "paired" relationship is: each `load_*`/`append_buffer` call implicitly tears down the previous tree via `reset()` (for the `xml_document::load_*` family) before loading a new one — so repeated `load_buffer` calls on the same `xml_document` are self-contained and do not need a manual cleanup in between. `append_buffer` does NOT reset — it's additive onto an existing tree (see P2.9/P2.10).
**Evidence:**
> src/pugixml.cpp:7871-7889: all three `load_buffer*` members call `reset();` first.
> src/pugixml.cpp:6736-6772: `xml_node::append_buffer` has no `reset()` call; it instead chains the new buffer onto `doc->extra_buffers`.

### P2.8 Prerequisite State
**Claim:** `load_buffer` only requires a constructed `xml_document`; no external context, config object, or connection is needed. For `append_buffer` the prerequisite is a valid non-empty tree already produced by a prior `load_*` call, and the target node must be an element or the document node (checked via `impl::allow_insert_child`).
**Evidence:**
> src/pugixml.cpp:6738-6739: `if (!impl::allow_insert_child(type(), node_element)) return impl::make_parse_result(status_append_invalid_root);`
> src/pugixml.hpp:1099: `status_append_invalid_root, // Unable to append nodes since root type is not node_element or node_document (exclusive to xml_node::append_buffer)`

### P2.9 Result Usage
**Claim:** After `load_buffer` returns (regardless of status, since a partial tree may exist), real callers use the resulting document tree through: (a) node/attribute traversal APIs (`xml_node::first_child()`, `child()`, etc. used e.g. via `CHECK_NODE` test helper), (b) `xml_node::print`/`xml_document::save*` to serialize the tree back out, (c) XPath evaluation (`xpath_query::evaluate_*(doc)`), (d) further mutation via `xml_node::append_buffer` to graft additional fragments onto the same document.
**Evidence:**
> tests/fuzz_xpath.cpp:31-38: `doc.load_buffer(xml_buffer.data(), xml_buffer.size(), pugi::parse_full); q.evaluate_boolean(doc); q.evaluate_number(doc); q.evaluate_string(doc); q.evaluate_node(doc); q.evaluate_node_set(doc);`
> tests/test_write.cpp:207-250: `CHECK(doc.load_buffer(contents.c_str(), contents.size())); ... doc.print(writer, STR(""), format_default, get_native_encoding());`
> tests/test_dom_traverse.cpp:1033-1059: `CHECK(doc.load_buffer(buf, sizeof(buf))); ... CHECK(doc.append_buffer("<node/>", 7));`
> tests/test_write.cpp:636: `CHECK(rdoc.load_buffer(&saved[0], saved.size(), flags_parse[i]));` (round-trip save→reload pattern)

### P2.10 Behaviour Controls
**Claim:** Two parameters of `load_buffer` select different processing paths, plus the raw bytes themselves:
- `options` bitmask selects which node kinds are retained/how text is processed: `parse_pi`(0x1), `parse_comments`(0x2), `parse_cdata`(0x4, on by default), `parse_ws_pcdata`(0x8), `parse_escapes`(0x10, on by default), `parse_eol`(0x20, on by default), `parse_wconv_attribute`(0x40, on by default), `parse_wnorm_attribute`(0x80), `parse_declaration`(0x100), `parse_doctype`(0x200), `parse_ws_pcdata_single`(0x400), `parse_trim_pcdata`(0x800), `parse_fragment`(0x1000, allows parsing without a document element / makes empty buffer "ok" instead of `status_no_document_element`). Combined presets `parse_default` and `parse_full` are provided. These flags are read inside `xml_parser::parse_tree` (via `PUGI_IMPL_OPTSET`) and select attribute/pcdata string-conversion function pointers via `get_strconv_attribute`/`get_strconv_pcdata`.
- `encoding` selects the byte-to-char_t conversion path in `get_buffer_encoding`/`convert_buffer`: `encoding_auto` triggers `guess_buffer_encoding` (BOM sniffing for UTF-32/16 LE/BE, UTF-8 BOM, `<?xml` declaration encoding sniffing for latin1/ISO-8859-1, default UTF-8 fallback); explicit values (`encoding_utf8`, `encoding_utf16_le/be`, `encoding_utf32_le/be`, `encoding_wchar`, `encoding_latin1`) force a specific conversion routine and skip sniffing.
- `is_mutable`/`own` (not exposed on `load_buffer` itself, hard-coded `false,false`; exposed via `load_buffer_inplace`/`load_buffer_inplace_own`) select whether `convert_buffer`/`get_mutable_buffer` copies the input or reuses it directly, and whether `load_buffer_impl` frees the original `contents` pointer.
**Evidence:**
> src/pugixml.hpp:192-254: full `parse_xxx` flag list and `parse_default`/`parse_full` combos.
> src/pugixml.cpp:3608-3612: `parse(...) { if (length==0) return make_parse_result(PUGI_IMPL_OPTSET(parse_fragment) ? status_ok : status_no_document_element); ... }`
> src/pugixml.cpp:2793 / 2962: `get_strconv_pcdata(unsigned int optmask)` / `get_strconv_attribute(unsigned int optmask)` dispatch on the options bitmask.
> src/pugixml.cpp:2055-2101: `guess_buffer_encoding` BOM/declaration sniffing logic.
> src/pugixml.cpp:2103-2121: `get_buffer_encoding` dispatch (`encoding_wchar`→native, `encoding_utf16/32`→LE/BE per host endianness, `encoding_auto`→`guess_buffer_encoding`, else pass-through).
> src/pugixml.cpp:7875/7882/7889: `is_mutable`/`own` hard-coded `false,false` / `true,false` / `true,true` for `load_buffer`/`load_buffer_inplace`/`load_buffer_inplace_own` respectively.

### P2.11 Input Expectations
**Claim:** `contents` may be any byte sequence (no required terminator, no required alignment); `size` is a byte count and `size==0` is explicitly handled as an early-out (returns `status_ok` if `parse_fragment` is set, else `status_no_document_element`) without allocating a buffer. If `contents` is NULL while `size` is nonzero, the function fails immediately with `status_io_error`. Encoding auto-detection requires at least 4 bytes to attempt BOM/declaration sniffing (`guess_buffer_encoding` falls back to UTF-8 if `size < 4`). After conversion, the parser null-terminates the buffer in place (saving/restoring the original last character) to speed up scanning; malformed/truncated XML (mismatched tags, bad PI/comment/CDATA/DOCTYPE, bad attributes, stray `<`, encoding conversion failures) is reported via specific `xml_parse_status` values with an `offset` into the (converted, char_t-unit) buffer, not into the raw bytes directly when encoding conversion changes the length.
**Evidence:**
> src/pugixml.cpp:4844: `if (!contents && size) return make_parse_result(status_io_error);`
> src/pugixml.cpp:4853-4858: `if (size == 0) { ... status_ok or status_no_document_element ...; return result; }`
> src/pugixml.cpp:2057-2058: `// skip encoding autodetection if input buffer is too small \n if (size < 4) return encoding_utf8;`
> src/pugixml.cpp:3620-3622: `char_t endch = buffer[length - 1]; buffer[length - 1] = 0;` (in-place NUL insertion for fast scanning)
> src/pugixml.cpp:3630-3650: various `status_unrecognized_tag`, `status_no_document_element`, offset roll-back logic for the null-terminator edge case.

## Other entries of the Logic Group (call protocol + combination with target)

### pugi::xml_document::load_buffer_inplace
**Declaration:** src/pugixml.hpp:1182 `xml_parse_result load_buffer_inplace(void* contents, size_t size, unsigned int options = parse_default, xml_encoding encoding = encoding_auto);`
**Prerequisite:** A constructed `xml_document`. `contents` must be a **writable** buffer (not `const`) that the caller guarantees stays alive for the lifetime of the document (the parser mutates it in-place and the document's internal string pointers point directly into it).
**Call:** `reset()` then `impl::load_buffer_impl(doc, root, contents, size, options, encoding, /*is_mutable=*/true, /*own=*/false, &_buffer)` — same core as target but skips the private copy (`get_mutable_buffer` just casts `contents` to `char_t*` directly when `is_mutable`).
**Return/cleanup:** Same `xml_parse_result` semantics as `load_buffer`. Because `own=false`, pugixml never frees `contents`; the **caller is responsible for freeing/keeping alive `contents`** for as long as the `xml_document` exists (per the header's doc comment), and must free it manually after the document is destroyed/reset.
**Combination with target:** Mutually exclusive alternative entry point into the exact same `load_buffer_impl`/`convert_buffer`/`xml_parser::parse`/`parse_tree` core as the target; a harness can call `load_buffer_inplace` on a heap-allocated, mutable copy of the fuzz input to exercise the no-copy branch of `convert_buffer`/`get_mutable_buffer` while the caller retains ownership/frees the buffer itself after the `xml_document` goes out of scope.
**Evidence:**
> src/pugixml.cpp:7878-7883: `load_buffer_inplace(...) { reset(); return impl::load_buffer_impl(..., contents, size, options, encoding, true, false, &_buffer); }`
> src/pugixml.cpp:2123-2140 (`get_mutable_buffer`): `if (is_mutable) { out_buffer = static_cast<char_t*>(const_cast<void*>(contents)); out_length = length; } else { ...allocate+copy... }`
> src/pugixml.hpp:1180-1181: "You should ensure that buffer data will persist throughout the document's lifetime, and free the buffer memory manually once document is destroyed."
> tests/test_document.cpp:860-866: `char_t text[] = STR("<?xml?><node/>"); CHECK(doc.load_buffer_inplace(text, sizeof(text)));`

### pugi::xml_document::load_buffer_inplace_own
**Declaration:** src/pugixml.hpp:1186 `xml_parse_result load_buffer_inplace_own(void* contents, size_t size, unsigned int options = parse_default, xml_encoding encoding = encoding_auto);`
**Prerequisite:** `contents` **must be allocated with pugixml's own allocator** (`pugi::get_memory_allocation_function()` / `xml_memory::allocate`), because the document (or `load_buffer_impl` on conversion-buffer-replace/failure paths) will deallocate it with `impl::xml_memory::deallocate` (`auto_deleter<void> contents_guard(own ? contents : NULL, xml_memory::deallocate)` at pugixml.cpp:4850, and `doc->buffer = buffer` assignment makes the (possibly same) buffer owned by the document thereafter).
**Call:** `reset()` then `load_buffer_impl(doc, root, contents, size, options, encoding, /*is_mutable=*/true, /*own=*/true, &_buffer)`.
**Return/cleanup:** Same `xml_parse_result`. The **document now owns `contents`** (or its converted replacement) and frees it automatically on destruction/`reset()`/next `load_*` call — caller must NOT free `contents` itself after a successful or failed call (ownership transfers regardless of parse status, including the `bad_alloc`-during-convert_buffer path, which is explicitly guarded by `contents_guard`).
**Combination with target:** Alternative to the target exercising the "ownership transfer / free-on-destruct" branch of `load_buffer_impl`; a harness should allocate the fuzz-input copy via pugixml's allocator (not plain `malloc`/`new`) before calling this, and must not also call `load_buffer`/`load_buffer_inplace` on the same raw pointer afterward.
**Evidence:**
> src/pugixml.cpp:7885-7889: `load_buffer_inplace_own(...) { reset(); return impl::load_buffer_impl(..., contents, size, options, encoding, true, true, &_buffer); }`
> src/pugixml.cpp:4849-4850: `auto_deleter<void> contents_guard(own ? contents : NULL, xml_memory::deallocate);`
> src/pugixml.cpp:4870: `if (own && buffer != contents && contents) impl::xml_memory::deallocate(contents);`
> tests/test_document.cpp:868-883: allocates via `get_memory_allocation_function()`, then `doc.load_buffer_inplace_own(text, size)`.

### pugi::xml_document::load_string
**Declaration:** src/pugixml.hpp:1171 `xml_parse_result load_string(const char_t* contents, unsigned int options = parse_default);`
**Prerequisite:** `contents` must be a NUL-terminated `char_t*` string (its length is derived via `impl::strlength`); no explicit size parameter.
**Call:** Forces native encoding (`encoding_wchar` in `PUGIXML_WCHAR_MODE`, else `encoding_utf8`) and delegates straight into `load_buffer(contents, strlength(contents)*sizeof(char_t), options, encoding)` — i.e. it is a thin wrapper reusing the **target function itself**, skipping `guess_buffer_encoding`.
**Return/cleanup:** Identical `xml_parse_result` semantics as `load_buffer`; like `load_buffer`, it copies the data so `contents` can be freed/changed by the caller right after the call.
**Combination with target:** Calls the target internally; a harness exercising `load_string` is implicitly exercising `load_buffer` with `encoding` forced rather than auto-detected — useful to cover the "no sniffing" branch of `get_buffer_encoding`.
**Evidence:**
> src/pugixml.cpp:7834-7844: `load_string(...) { ... xml_encoding encoding = encoding_wchar/encoding_utf8; return load_buffer(contents, impl::strlength(contents) * sizeof(char_t), options, encoding); }`

### pugi::xml_document::load_file (char* and wchar_t* overloads)
**Declaration:** src/pugixml.hpp:1174-1175.
**Prerequisite:** A readable filesystem path; internally opens with `impl::open_file(path_, "rb")`/`open_file_wide`, guarded by `auto_deleter<FILE>` so the `FILE*` is always closed.
**Call:** `reset()`, open file, then `impl::load_file_impl(doc, file.data, options, encoding, &_buffer)`, which reads the whole file into a pugixml-allocated buffer (via `get_file_size`) and then calls the same `load_buffer_impl` core internally (own=true path), i.e. `load_file` reaches the same core as the target but via file I/O instead of a caller-supplied pointer.
**Return/cleanup:** `xml_parse_result` as usual; status `status_file_not_found`/`status_io_error` for file-open/size errors. The `FILE*` is closed automatically by the `auto_deleter`; the internal read buffer is owned by the document (freed on destroy/reset), same as `load_buffer_inplace_own`.
**Combination with target:** Not typically combined in the same call chain as `load_buffer` in one harness invocation unless the harness first writes fuzz bytes to a temp file; if a harness needs in-memory-only fuzzing it should prefer `load_buffer`/`load_buffer_inplace(_own)` over `load_file`. Shares the exact same `load_buffer_impl`/`convert_buffer`/parse core, so it mainly adds filesystem-interaction coverage (encoding auto-detection from file bytes).
**Evidence:**
> src/pugixml.cpp:7851-7859 and 7861-7869: both overloads shown above.
> src/pugixml.hpp:1082: `status_file_not_found, // File was not found during load_file()`

### pugi::xml_node::append_buffer
**Declaration:** src/pugixml.hpp:699 `xml_parse_result append_buffer(const void* contents, size_t size, unsigned int options = parse_default, xml_encoding encoding = encoding_auto);`
**Prerequisite:** Must be called on an `xml_node` that is either `node_element` or the document node (`node_document`), already part of a tree produced by a prior successful `load_*` call (or a freshly-built tree via DOM-mutation APIs) — otherwise returns `status_append_invalid_root` without touching the buffer. If `parse_merge_pcdata` is set and the target node's last child is already `node_pcdata`, it also fails with `status_append_invalid_root` before parsing.
**Call:** Does NOT call `reset()`. It allocates a small `xml_extra_buffer` bookkeeping node from the document's allocator, links it into `doc->extra_buffers`, temporarily NULLs the node's name (`name_null_sentry`) to force closing-tag mismatch detection, then calls the same core: `impl::load_buffer_impl(doc, _root /* = this node */, contents, size, options, encoding, /*is_mutable=*/false, /*own=*/false, &extra->buffer)`. The parsed fragment's nodes are appended as children of `this` node rather than of a fresh document root.
**Return/cleanup:** Same `xml_parse_result`. The fragment's converted buffer is tracked in the document's `extra_buffers` singly-linked list and is freed automatically together with the owning `xml_document` (not by the caller); note per the header comment, "removing the appended nodes does not immediately reclaim that memory" — it's only freed at document teardown.
**Combination with target:** Natural second call in a harness after the target: `doc.load_buffer(fuzz_bytes_1, len1)` to build a base tree, then `root_or_any_element.append_buffer(fuzz_bytes_2, len2, ...)` to graft a second parse onto it, exercising multi-buffer bookkeeping and the `parse_fragment`/`parse_merge_pcdata` interplay. Requires the first `load_buffer` call to have produced a valid element/document node to call `append_buffer` on.
**Evidence:**
> src/pugixml.hpp:698-699: comment on buffer lifetime + declaration.
> src/pugixml.cpp:6736-6772 (full body quoted in P2.8/P2.9 above).
> src/pugixml.hpp:1099: `status_append_invalid_root` description.
> tests/test_dom_traverse.cpp:1033-1059: `doc.load_buffer(buf, sizeof(buf)); ... doc.append_buffer("<node/>", 7);`
> tests/test_dom_modify.cpp:1407-1497: multiple `append_buffer` calls on the same node, including failure-path checks (`status_append_invalid_root`, `status_end_element_mismatch`).

### pugi::xml_node::print
**Declaration:** src/pugixml.hpp:780 `void print(xml_writer& writer, const char_t* indent = "\t", unsigned int flags = format_default, xml_encoding encoding = encoding_auto, unsigned int depth = 0) const;` (plus STL-stream overloads at 784-785.)
**Prerequisite:** A valid `xml_node`/`xml_document` with `_root` set (i.e., the result of a prior `load_*`/`append_buffer`/manual-construction call); an `xml_writer` sink (e.g. `xml_writer_stream` wrapping a `std::ostream`, or a custom `xml_writer_file`/`xml_writer_buffer`-like implementation). If `_root` is null, `print` is a no-op (`if (!_root) return;`).
**Call:** Builds an `impl::xml_buffered_writer` around the given `xml_writer` + target `encoding`, then calls `impl::node_output(...)` to serialize the tree starting at this node, then flushes.
**Return/cleanup:** `void` return; no status to check (writer errors, if any, are the writer's own responsibility, e.g. `std::ostream` failbit). Nothing to free beyond whatever the caller's `xml_writer`/stream owns.
**Combination with target:** Typical post-target consumer: `doc.load_buffer(data, size, options, encoding); if (doc) doc.print(writer, ...);` to additionally exercise serialization/tree-traversal of whatever tree `load_buffer` built, which is good for catching tree-corruption bugs introduced by the parser. `flags`/`encoding` select `format_xxx` bit combinations (e.g. `format_indent`, `format_raw`, `format_no_declaration`) and output-encoding conversion paths respectively — both are print-side behaviour controls independent of the target's own `options`/`encoding`.
**Evidence:**
> src/pugixml.cpp:6956-6965: `print(xml_writer& writer, ...) { if (!_root) return; impl::xml_buffered_writer buffered_writer(writer, encoding); impl::node_output(buffered_writer, _root, indent, flags, depth); buffered_writer.flush(); }`
> tests/test_write.cpp:207 + 250: `CHECK(doc.load_buffer(contents.c_str(), contents.size())); ... doc.print(writer, STR(""), format_default, get_native_encoding());`

## Build notes

**Language/toolchain:** C++ (pugixml is a single translation unit `src/pugixml.cpp` implementing `src/pugixml.hpp`). OSS-Fuzz builds it with `$CXX`/`$CXXFLAGS` (no special C++ standard flag override visible in build.sh beyond what OSS-Fuzz's base image sets).
**Header include path:** The library header is included via a relative path from the `tests/` directory: `#include "../src/pugixml.hpp"` (see tests/fuzz_xpath.cpp:1). A new fuzz source placed under `tests/` (as the existing `fuzz_parse.cpp`/`fuzz_xpath.cpp` are) should use the same relative include; no special `-I` flag is set in build.sh besides compiling from the `pugixml` checkout root (`cd pugixml` then paths relative to that).
**Compilation/link commands (evidence):**
> ossfuzz_view/pugixml-c1/build.sh:17: `cd pugixml`
> ossfuzz_view/pugixml-c1/build.sh:23: `$CXX $CXXFLAGS -c src/pugixml.cpp -o src/pugixml.o`
> ossfuzz_view/pugixml-c1/build.sh:24-25: `$CXX $CXXFLAGS $LIB_FUZZING_ENGINE tests/fuzz_parse.cpp src/pugixml.o -o ${OUT}/fuzz_parse` and the analogous line for `fuzz_xpath.cpp`.
**Libraries:** Only `src/pugixml.o` (the single compiled TU) plus `$LIB_FUZZING_ENGINE` (libFuzzer or OSS-Fuzz's chosen engine) are linked in; no other third-party libraries are required (pugixml has no external dependencies by default; `PUGIXML_NO_STL`/`PUGIXML_NO_EXCEPTIONS` are not defined in this build, so `<iostream>`/exceptions remain available, matching `tests/fuzz_xpath.cpp`'s use of `pugi::xpath_exception` and `try/catch`).
**Existing harness evidence of target usage pattern:** `tests/fuzz_xpath.cpp:31-32` directly calls the target: `pugi::xml_document doc; doc.load_buffer(xml_buffer.data(), xml_buffer.size(), pugi::parse_full);` — no explicit error check before subsequent use (XPath evaluation on a possibly-empty/partial tree is tolerated because `evaluate_*` operate safely over any valid `xml_node`, including an empty document). Note `tests/fuzz_parse.cpp` in this checkout is a stub (link-only smoke test for baseline), not a functional fuzz entry point; `tests/fuzz_xpath.cpp` is the concrete, buildable example of calling the target from a libFuzzer-style harness in this project.