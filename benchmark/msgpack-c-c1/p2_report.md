## target: msgpack::unpack

### P2.1 Init Sequence
**Claim:** No library-global init call is required. The only prerequisite is a valid, already-encoded MessagePack byte buffer (`data`/`len`), normally produced by `msgpack::pack()` into an `msgpack::sbuffer` (or any `Stream`). If the caller wants bound-size protection, an `msgpack::unpack_limit` object must be constructed (all fields default to `0xffffffff`) and passed as the last argument; no other setup object is required before calling `unpack`.
**Evidence:**
> include/msgpack/v1/unpack_decl.hpp:272-274: `object_handle unpack(const char* data, std::size_t len, unpack_reference_func f = MSGPACK_NULLPTR, void* user_data = MSGPACK_NULLPTR, unpack_limit const& limit = unpack_limit());` — all trailing parameters default, so `unpack(data, len)` alone is a legal, complete call.
> test/pack_unpack.cpp:56-63: `msgpack::sbuffer sbuf; msgpack::pack(sbuf, 1); msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size());` — the only thing done before `unpack` is building the buffer with `pack`.
> include/msgpack/v1/unpack_decl.hpp:87-116: `class unpack_limit` with default ctor giving `0xffffffff` for array/map/str/bin/ext/depth — no external init needed unless the harness wants to lower limits.

### P2.2 Parameter Construction
**Claim:** `data` is a `const char*` to raw bytes (any buffer: `sbuffer::data()`, `std::string::data()`, raw `uint8_t*` cast to `char*`), `len` is its length in bytes (`std::size_t`), both constructed directly from the fuzz input with no special allocation. Optional `unpack_reference_func f` is a C function pointer `bool(*)(msgpack::type::object_type, std::size_t, void*)`; `user_data` is any `void*` forwarded to `f`; `unpack_limit` is passed by const reference, constructed with up to 6 `std::size_t` values (array,map,str,bin,ext,depth) in that order.
**Evidence:**
> include/msgpack/v1/unpack_decl.hpp:74: `typedef bool (*unpack_reference_func)(msgpack::type::object_type type, std::size_t size, void* user_data);`
> include/msgpack/v1/unpack_decl.hpp:89-95: `unpack_limit(std::size_t array = 0xffffffff, std::size_t map = ..., std::size_t str = ..., std::size_t bin = ..., std::size_t ext = ..., std::size_t depth = ...)`.
> test/reference.cpp:332,350: `msgpack::unpacker unp(never_called, &sbuf);` and `msgpack::unpacker unp(sized_reference, &sbuf);` show a real reference-func/user_data pair (sbuf as user_data) used with the sibling `unpacker` class, same function-pointer type as `unpack`'s `f`.
> test/limit.cpp:554-560 (context around): `msgpack::unpacker u(MSGPACK_NULLPTR, MSGPACK_NULLPTR, MSGPACK_UNPACKER_INIT_BUFFER_SIZE, msgpack::unpack_limit(...))` shows constructing a non-default `unpack_limit`.

### P2.3 Object Lifecycle
**Claim:** `unpack()` is not an object with create/destroy semantics itself — it is a one-shot free function that internally allocates a `msgpack::zone` (heap), parses into it, and returns an `object_handle` that owns that zone via `unique_ptr`. Lifecycle is: call `unpack(data, len, ...)` → use returned `object_handle` (`oh.get()`, `oh->...`) → let `object_handle` go out of scope (its destructor frees the owned `zone`, which frees all string/bin/ext/array/map/ext payload allocations made during parsing). No explicit destroy call is needed by the caller.
**Evidence:**
> include/msgpack/v3/unpack.hpp:29-30,37: `msgpack::unique_ptr<msgpack::zone> z(new msgpack::zone); ... return msgpack::object_handle(obj, msgpack::move(z));` — zone created internally and ownership transferred into the returned handle.
> include/msgpack/v1/object.hpp:44-98: `class object_handle { ... msgpack::unique_ptr<msgpack::zone> m_zone; ... zone() { return m_zone; } ... }` — RAII owner of the zone; destructor (implicit, via unique_ptr) frees it.
> test/pack_unpack.cpp:61-62: `msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size()); BOOST_CHECK_EQUAL(1, oh.get().as<int>());` — use-after-call pattern, no manual free.

### P2.4 Return Value Handling
**Claim:** `unpack()` returns an `object_handle`. On success (PARSE_SUCCESS or PARSE_EXTRA_BYTES internally) it holds the parsed `msgpack::object` + its zone; the handle must be checked indirectly by using `.get()`/`->`. On malformed input (`PARSE_PARSE_ERROR` from the dispatcher) it throws `msgpack::parse_error`; on truncated/short input it throws `msgpack::insufficient_bytes`; on oversized declared array/map/str/bin/ext or excessive nesting it throws `msgpack::array_size_overflow` / `map_size_overflow` / `str_size_overflow` / `bin_size_overflow` / `ext_size_overflow` / `depth_size_overflow` (all derive from `msgpack::unpack_error`/`size_overflow`). A harness must wrap the call in try/catch for these exception types (or a catch of the common base `msgpack::unpack_error`/`std::runtime_error`).
**Evidence:**
> include/msgpack/v3/unpack.hpp:35-43: `switch(ret) { case PARSE_SUCCESS: return object_handle(obj, move(z)); case PARSE_EXTRA_BYTES: return object_handle(obj, move(z)); default: break; } return object_handle();` — only two of four parse_return values map to a filled handle; however reaching `default` normally doesn't happen because parse_imp throws before returning PARSE_PARSE_ERROR/PARSE_CONTINUE from the top-level unpack (see below).
> include/msgpack/v2/create_object_visitor.hpp:255-259: `void parse_error(...) { throw msgpack::parse_error("parse error"); } void insufficient_bytes(...) { throw msgpack::insufficient_bytes("insufficient bytes"); }` — these visitor hooks are what actually throw; invoked from `detail::context::execute`/`parse_imp`.
> include/msgpack/v2/create_object_visitor.hpp:111,135,159,182-183: `if (size > m_limit.str()) throw msgpack::str_size_overflow(...)`, `... bin_size_overflow(...)`, `... ext_size_overflow(...)`, `if (num_elements > m_limit.array()) throw msgpack::array_size_overflow(...)`, `if (m_stack.size() > m_limit.depth()) throw msgpack::depth_size_overflow(...)`.
> test/pack_unpack.cpp:427-444 (`unpack_parse_error`), :362-382 (`unpack_insufficient_bytes_ref`): real test cases wrapping `msgpack::unpack(...)` in try/catch for `msgpack::parse_error` / `msgpack::insufficient_bytes`.
> include/msgpack/v1/parse_return.hpp:23-28: `PARSE_SUCCESS=2, PARSE_EXTRA_BYTES=1, PARSE_CONTINUE=0, PARSE_PARSE_ERROR=-1` — the internal parse_return enum.

### P2.5 Cleanup Sequence
**Claim:** No manual free is required for the one-shot `unpack(data,len,...)` overload: the returned `object_handle`'s destructor (RAII, via `unique_ptr<zone>`) frees the zone and everything allocated in it (deep-copied str/bin/ext payloads, array/map element arrays) when it goes out of scope. The input buffer (`data`) is owned by the caller and must outlive the `object_handle` only if `referenced==true` (buffer-aliasing path) — otherwise it can be freed/destroyed right after the call returns. For the `zone&`-taking overload (`msgpack::object unpack(zone&, ...)`), the caller owns the `msgpack::zone` and must keep it alive as long as the returned `msgpack::object` is used, then let it go out of scope itself.
**Evidence:**
> include/msgpack/v1/object.hpp:44-98: `object_handle` wraps `unique_ptr<zone>`; no explicit "close"/"free" API exists or is needed.
> test/reference.cpp:259-265 area (unpacker variant) and test/pack_unpack.cpp:263-266 (`msgpack::zone z; msgpack::object obj = msgpack::unpack(z, sbuf.data(), sbuf.size());`): caller-owned zone `z` lives on the stack and is implicitly destroyed at scope exit, after `obj` is used.
> include/msgpack/v2/create_object_visitor.hpp:114-130 (visit_str): when `m_func` returns true, the object references the *original* buffer (`obj->via.str.ptr = v;`) — in that case the original `data` buffer must stay alive at least as long as the `object_handle`/`zone`.

### P2.6 API Existence
**Claim:** All referenced functions/classes exist in the current codebase at the given paths.
**Evidence:**
> include/msgpack/v1/unpack_decl.hpp:272 — `msgpack::unpack` (the target, v1 declaration; v2/v3 declared in sibling `unpack_decl.hpp` files and defined in `v2/unpack.hpp`/`v3/unpack.hpp`).
> include/msgpack/v2/unpack.hpp:35-90 — `class unpacker` with `next`, `release_zone`, `reset_zone`, `flush_zone`.
> include/msgpack/v2/parse.hpp:626-641,659,667,688,701 — `class parser` (base of `unpacker`) with `reserve_buffer`, `buffer`, `buffer_consumed`, `next`.
> include/msgpack/v1/pack.hpp:659-676 — `msgpack::pack(Stream*, T const&)` / `msgpack::pack(Stream&, T const&)`.
> include/msgpack/v2/create_object_visitor.hpp:27-230 — `create_object_visitor::visit_str/visit_bin/visit_ext/start_array/start_map`.
> include/msgpack/v2/parse.hpp:1032-1058 and include/msgpack/v2/unpack.hpp:321-335 — `detail::parse_imp`, `detail::unpack_imp`.

### P2.7 Co-call Constraints
**Claim:** For the one-shot `unpack()` entry there is no strict paired API (it is a pure function, "call and use result"). For the streaming `unpacker` class the pairing is strict and ordered: `reserve_buffer(n)` must be called before writing into `buffer()`; after writing, `buffer_consumed(len)` must be called before `next()`/`next(result)` is called; `next()` must be called repeatedly in a loop until it returns false (buffer exhausted) before reserving/writing more. There is no explicit "close" call for `unpacker`; its destructor (`~parser()`) releases internal buffer memory.
**Evidence:**
> test/streaming.cpp:20-50: `msgpack::unpacker pac; ... while(count<3) { pac.reserve_buffer(32*1024); memcpy(pac.buffer(), input, len); input+=len; pac.buffer_consumed(len); while(pac.next(oh)) { ... } }` — canonical reserve→write→consume→next loop.
> include/msgpack/v2/parse.hpp:678-688: "After copying the data to the memory that is pointed by buffer(), you need to call the function to notify how many bytes are consumed. Then you can call next() functions." (doc comment on `buffer_consumed`).
> include/msgpack/v2/parse.hpp:648: `~parser();` — destructor exists, called implicitly at scope exit; no separate "unpacker_destroy" API.

### P2.8 Prerequisite State
**Claim:** The only state that must exist before calling `msgpack::unpack` is the encoded byte buffer itself (`data`,`len`), which is produced in-process by `msgpack::pack()`/`packer<Stream>::pack()` in essentially every real caller, or (for a fuzz harness) is the raw fuzzer input bytes treated directly as `data`/`len`. No network/file/global context is required. For the `unpacker`-based streaming entries, the prerequisite state is the `unpacker` object itself (constructed, optionally with `f`/`user_data`/`initial_buffer_size`/`limit`) plus a prior `reserve_buffer`+copy+`buffer_consumed` cycle before `next()` can produce anything.
**Evidence:**
> test/pack_unpack.cpp:56-63: pack-then-unpack round trip, no other prerequisite objects.
> include/msgpack/v2/unpack.hpp:39-49: `unpacker(unpack_reference_func f = &unpacker::default_reference_func, void* user_data = MSGPACK_NULLPTR, std::size_t initial_buffer_size = MSGPACK_UNPACKER_INIT_BUFFER_SIZE, unpack_limit const& limit = unpack_limit())` — construction parameters, all defaulted so `unpacker pac;` alone is enough to start.
> test/streaming.cpp:20-35: buffer must be reserved/filled/consumed before the first successful `next()`.

### P2.9 Result Usage
**Claim:** The `object_handle`/`object` returned by `unpack()` (or produced by `unpacker::next()`) is consumed through `object_handle::get()` (returns `const msgpack::object&`), `operator*`/`operator->`, and `msgpack::object::as<T>()` for typed extraction, or `operator<<(std::ostream&, object const&)` for printing/traversal. Real callers also directly inspect `.type`/`.via` fields for containers (ARRAY/MAP) or read `oh.zone()` for ownership transfer (e.g., `unpacker::next` moving the released zone into the caller's `object_handle`).
**Evidence:**
> test/pack_unpack.cpp:61-62: `msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size()); BOOST_CHECK_EQUAL(1, oh.get().as<int>());` — `.get().as<T>()` consumption pattern.
> test/streaming.cpp:35-47: `msgpack::object obj = oh.get(); switch(count++) { case 0: BOOST_CHECK_EQUAL(1, obj.as<int>()); ... }` — same `.as<int>()` pattern applied to objects produced by `unpacker::next`.
> include/msgpack/v1/object.hpp:67-84: `const msgpack::object& get() const`, `operator*()`, `operator->()` — the accessor surface for `object_handle`.
> include/msgpack/v2/unpack.hpp:92-99 (`unpacker::next`): `result.zone().reset( release_zone() ); result.set(data());` — shows how the parsed object/zone pair is handed to the caller-supplied `object_handle& result` each iteration (lifecycle transfer, not a simple return value).

### P2.10 Behaviour Controls
**Claim:** Behaviour-selecting inputs/flags around `unpack`/`unpacker`:
- the leading type-tag byte of `data` (0x00-0xff) selects the decode path (fixint/nil/bool/float/uint/int/str/bin/ext/array/map) inside `create_object_visitor`'s `visit_*`/`start_*` methods;
- `unpack_reference_func f` (+`user_data`) toggles, per str/bin/ext occurrence, whether the object aliases the input buffer (`referenced=true`, zero-copy) or is deep-copied into the zone;
- `unpack_limit` (array/map/str/bin/ext/depth, each defaulting to `0xffffffff`) — lowering any of these makes the corresponding `*_size_overflow` exception reachable with small, cheaply-crafted inputs;
- `off`/`len` (on the `off`-taking overloads) select partial-buffer parsing: `off>0` resumes from a given position, `len<total` triggers `PARSE_CONTINUE`/`insufficient_bytes` or stops before trailing bytes (`PARSE_EXTRA_BYTES`);
- for `unpacker`: `initial_buffer_size` (ctor) and the argument to `reserve_buffer(size)` control how much contiguous space is available before `buffer_consumed`, affecting whether a large declared container/string can be parsed from one `next()` call or needs several `reserve_buffer`/`buffer_consumed` rounds.
**Evidence:**
> include/msgpack/v2/create_object_visitor.hpp:109-131,133-156,157-180,181-199: `visit_str`/`visit_bin`/`visit_ext`/`start_array` each check `m_func(...)` to decide `set_referenced(true)` (alias) vs `zone().allocate_align(...)` + `memcpy` (deep copy), and each compares the declared size against the corresponding `m_limit.*()` before building the object.
> include/msgpack/v2/parse.hpp:1037-1057 (`parse_imp`): `if(len <= noff) { v.insufficient_bytes(...); return PARSE_CONTINUE; }` and `if(noff < len) return PARSE_EXTRA_BYTES;` — `off`/`len` directly drive which `parse_return` is produced.
> test/limit.cpp:554-560 area: constructing `unpacker`/`unpack_limit` with non-default small values to force overflow exceptions with small inputs.
> include/msgpack/v2/unpack.hpp:39-49: `initial_buffer_size` ctor parameter; include/msgpack/v2/parse.hpp:659 `reserve_buffer(std::size_t size = MSGPACK_UNPACKER_RESERVE_SIZE)`.

### P2.11 Input Expectations
**Claim:** `unpack()` expects `data` to point to at least `len` readable bytes of raw MessagePack-encoded binary (no required null-termination, no separate header/magic — each message starts directly with a MessagePack type-tag byte). `len` is treated as an exact byte-count bound (`parse_imp` compares offsets against it, never reads past `len`). Multiple back-to-back messages in one buffer are supported via the `off`-taking overloads (`off` is read as the start position and overwritten with the new position) and are explicitly detected via `PARSE_EXTRA_BYTES` when `noff < len` at success. Declared container/string/bin/ext sizes embedded in the wire format are the attacker-controlled values checked against `unpack_limit` and against remaining buffer length (`len`); truncated trailers produce `PARSE_CONTINUE`/`insufficient_bytes`, and unknown/reserved tag bytes (e.g. `0xc1`) trigger `parse_error`.
**Evidence:**
> include/msgpack/v1/unpack_decl.hpp:214-220 (doc comment on the `off`,`referenced` overload): "@param data The pointer to the buffer. @param len The length of the buffer. @param off The offset position of the buffer. It is read and overwritten."
> include/msgpack/v2/parse.hpp:1037-1041: `if(len <= noff) { v.insufficient_bytes(noff, noff); return PARSE_CONTINUE; }` — `len` is the hard upper bound for reads.
> test/pack_unpack.cpp:427-444 (`unpack_parse_error`): `char c = '\xc1'; sbuf.write(&c,1); ... catch (msgpack::parse_error const&) { thrown = true; }` — unknown tag byte `0xc1` (reserved/never-used in MessagePack spec) is rejected with `parse_error`, confirming the tag-byte dispatch and no "magic header" beyond the tag itself.
> test/pack_unpack.cpp:483-489 (`unpack_extra_bytes`): `msgpack::unpack(sbuf.data(), sbuf.size() + 1)` on a 1-object buffer still succeeds and ignores the "extra" conceptual byte range because `len` is just an upper bound, not an exact-match requirement — note: real `sbuf.size()+1` reads one byte past the real payload only if the buffer backing actually has that byte allocated; the test exploits sbuffer's internal capacity, illustrating that `unpack` trusts `len` fully.

### Other Logic-Group entries: call protocol & combination with the target

**msgpack::pack** (producer, inverse of unpack)
- Declared/defined: `include/msgpack/v1/pack.hpp:660,673` — `template<typename Stream,typename T> void pack(Stream* s, T const& v)` and the `Stream&` overload; both simply do `packer<Stream>(*s).pack(v)`.
- Needs before it: a `Stream` object supporting `write(const char*, size_t)` (e.g. `msgpack::sbuffer`, `std::ostringstream`); no other init.
- Parameters/flags: `T` (any adapted type — scalars, strings, containers, user types via `MSGPACK_DEFINE`) selects which `packer<Stream>::pack_*` sequence of bytes is emitted; this is how real tests build inputs for `unpack`.
- Return: `void`. Nothing to free — the `Stream` (e.g. `sbuffer`) owns its own buffer and is read via `.data()`/`.size()`.
- Combination with target: in a harness, `pack` is only useful to build *valid* structured seed data; for a pure fuzz-unpack harness, raw fuzz bytes are normally fed straight to `unpack()` without using `pack` at all. If the harness wants structurally-valid-but-mutated inputs, call `pack(sbuf, someValue)` first, then feed `sbuf.data()/sbuf.size()` (or a mutated copy) into `unpack`.
- Evidence: `test/pack_unpack.cpp:8-12` `msgpack::sbuffer sbuf; msgpack::pack(sbuf, 1);` then (same file) `msgpack::unpack(sbuf.data(), sbuf.size())` immediately after at lines 58-61 — pack always precedes unpack in every round-trip test in this file.

**msgpack::v2::unpacker::next()** (entry, streaming decode)
- Signatures: `bool next(msgpack::object_handle& result, bool& referenced)` and `bool next(msgpack::object_handle& result)` — include/msgpack/v2/unpack.hpp:66,80,92,107. Also a zero-arg `bool next()` on the base `parser` (include/msgpack/v2/parse.hpp:701) used only when the subclass visitor holds its own result (not the typical harness entry).
- Needs before it: an `unpacker` instance constructed (`msgpack::unpacker pac;` or with `f`,`user_data`,`initial_buffer_size`,`limit`), and at least one `reserve_buffer`+copy-into-`buffer()`+`buffer_consumed` cycle so there are unparsed bytes available.
- Parameters: none for the input (it consumes whatever is in the internal buffer); `result` out-param receives the decoded `object_handle` (its zone is reset to the one just released from the unpacker, so ownership moves to `result`); optional `referenced` out-param set from `create_object_visitor::referenced()`.
- Return: `true` if one complete object was parsed (ready to use via `result`); `false` if the buffer contains no complete object yet (caller should `reserve_buffer`/copy more/`buffer_consumed` and retry). Throws the same `msgpack::parse_error`/`*_size_overflow`/`insufficient_bytes` family as `unpack()` on malformed/oversized data (same `create_object_visitor` is used underneath).
- What to free: nothing explicit — each successful `next()` call hands a fresh `object_handle` (with its own zone, moved out via `release_zone()`) to `result`; `result`'s own destructor cleans it up. The `unpacker` itself cleans its internal buffer in its destructor.
- Combination with target: `unpacker::next()` exercises the exact same `detail::context<VisitorHolder>::execute`/`create_object_visitor` core as `msgpack::unpack()`, but incrementally — useful in the same harness to additionally test fragmented/partial-feed decoding (PARSE_CONTINUE loop) that a single `unpack()` call cannot reach.
- Evidence: include/msgpack/v2/unpack.hpp:35-49 (class + ctor), :66-110 (`next` overloads and zone hand-off `result.zone().reset(release_zone()); result.set(data()); reset();`); test/streaming.cpp:20-50 (full use loop).

**msgpack::v2::unpacker::reserve_buffer(size)**
- Declared: include/msgpack/v2/parse.hpp:659 (`void reserve_buffer(std::size_t size = MSGPACK_UNPACKER_RESERVE_SIZE)`), implemented at :848-852 (`if(m_free >= size) return; expand_buffer(size);`).
- Needs before it: a constructed `unpacker`/`parser` only.
- Parameter: `size` — minimum number of free bytes guaranteed available at `buffer()` after the call; drives how much contiguous input the harness can memcpy in a single step (controls fragmentation granularity when fuzzing the streaming path).
- Return: `void`; nothing to free (internal buffer reallocation is managed by `parser`'s destructor).
- Combination: always called immediately before writing new input bytes into `buffer()` and before `buffer_consumed`; see streaming example above.

**msgpack::v2::unpacker::buffer_consumed(size)**
- Declared/implemented: include/msgpack/v2/parse.hpp:688, :940-944 (`m_used += size; m_free -= size;`).
- Needs before it: a prior `reserve_buffer` call and the caller having written `size` bytes into `buffer()`.
- Parameter: `size` — number of bytes actually written into `buffer()`; in a harness, this is the chunk-size dimension (how the fuzz input is split into feed increments), itself a behaviour-control knob affecting how many `PARSE_CONTINUE` boundaries get exercised.
- Return: `void`. Nothing to free.
- Combination: must be called after each buffer fill and before the following `next()` call(s); this is the co-call constraint documented in P2.7.

### Build notes
**Claim:** The project's own OSS-Fuzz fuzzers are plain C++ (not C), built with `-std=c++11`, including the target via the project's own top-level `-Iinclude`/`-I$SRC/msgpack-c/include` search path (i.e. `#include <msgpack.hpp>` or a specific `#include "msgpack/unpack.hpp"` resolves through these `-I` flags), and linked only against `$LIB_FUZZING_ENGINE` (header-only library, no separate `libmsgpack` to link). Boost is built in the Docker image but only used by the project's Boost-Test-based unit tests, not required by the fuzzer binaries themselves unless a harness specifically includes Boost adaptors.
**Evidence:**
> ${QF_WORK}/ossfuzz_view/msgpack-c-c1/build.sh:18-25:
> ```
> for f in $SRC/msgpack-c/fuzz/*_fuzzer.cpp; do
>     fuzzer=$(basename "$f" .cpp)
>     $CXX $CXXFLAGS -std=c++11 -Iinclude -I"$SRC/msgpack-c/include" \
>          "$f" -o "$OUT/${fuzzer}" \
>          $LIB_FUZZING_ENGINE
>     ...
> done
> ```
> ${QF_WORK}/ossfuzz_view/msgpack-c-c1/Dockerfile:19,27: `git clone ... msgpack-c.git msgpack-c` then `WORKDIR msgpack-c` — confirms `$SRC/msgpack-c/include` is the installed-header tree containing `msgpack.hpp`/`msgpack/unpack.hpp` etc.
> fuzz/unpack_pack_fuzzer.cpp:1-13 — existing fuzzer file is currently a stub (`/* Gold harness replaced by an empty stub for baseline generation. */`), confirming the naming convention `*_fuzzer.cpp` picked up by `build.sh`'s glob, but it no longer contains real `unpack` usage; `test/pack_unpack.cpp` and `test/streaming.cpp` (Boost-Test, not shipped to OSS-Fuzz) are the real source of call-site evidence used throughout this report.
> project.yaml:2: `language: c++` and :7-10 `sanitizers: [address, memory, undefined]` — confirms C++ build with ASan/MSan/UBSan variants, all using the same `build.sh`.