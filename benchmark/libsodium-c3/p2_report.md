## target: crypto_auth_verify

### Overview / definitions
- Declaration: `src/libsodium/include/sodium/crypto_auth.h:33-36`:
  `SODIUM_EXPORT int crypto_auth_verify(const unsigned char *h, const unsigned char *in, unsigned long long inlen, const unsigned char *k) __attribute__((warn_unused_result)) __attribute__((nonnull(1, 4)));`
- Definition: `src/libsodium/crypto_auth/crypto_auth.c:30-35`:
  ```c
  int crypto_auth_verify(const unsigned char *h, const unsigned char *in,
                     unsigned long long inlen,const unsigned char *k)
  {
      return crypto_auth_hmacsha512256_verify(h, in, inlen, k);
  }
  ```
- Core dispatch: `src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:82-93`:
  ```c
  int crypto_auth_hmacsha512256_verify(...)
  {
      unsigned char correct[32];
      crypto_auth_hmacsha512256(correct, in, inlen, k);
      return crypto_verify_32(h, correct) | (-(h == correct)) | sodium_memcmp(correct, h, 32);
  }
  ```
  (`correct` is a local stack buffer, so `-(h == correct)` is always 0 in practice, it's an aliasing guard carried from the generic pattern used across libsodium's `*_verify` functions.)
- `crypto_auth_hmacsha512256()` (the MAC-computation side) itself does: init -> update -> final, at `auth_hmacsha512256.c:68-80`.

### P2.1 Init Sequence
**Claim:** No library-wide init is strictly required by `crypto_auth_verify` itself (it has no global state), but project fuzzers/tests always call `sodium_init()` first as a matter of project convention/thread-safety setup. No key-setup call is required before `crypto_auth_verify` (unlike streaming API, which needs `_init`); it is a one-shot function taking `h`, `in`, `inlen`, `k` directly.
**Evidence:**
> secretbox_easy_fuzzer.cc:22: `int initialized = sodium_init(); assert(initialized >= 0);` — pattern for every existing fuzzer (crypto_sign_fuzzer.cc:22 same)
> test/default/auth5.c:9-21: calls `crypto_auth_keygen`, `crypto_auth`, then `crypto_auth_verify` directly with no state object
> src/libsodium/crypto_auth/crypto_auth.c:30-35: `crypto_auth_verify` body only calls `crypto_auth_hmacsha512256_verify`, no init call required internally

### P2.2 Parameter Construction
**Claim:** `h` must point to a readable buffer of exactly `crypto_auth_BYTES` (32) bytes — the candidate tag to check (nonnull, required). `in`/`inlen` describe the message (`in` may be any valid pointer incl. a zero-length "guard page" style buffer when `inlen==0`, `in` is NOT marked nonnull so it may be a dummy non-NULL pointer for inlen=0, consistent with other libsodium primitives). `k` must point to a 32-byte key (`crypto_auth_KEYBYTES`), nonnull, any byte content is accepted (HMAC keys can be arbitrary).
**Evidence:**
> src/libsodium/include/sodium/crypto_auth.h:20: `#define crypto_auth_KEYBYTES crypto_auth_hmacsha512256_KEYBYTES` -> `crypto_auth_hmacsha512256.h:19`: `#define crypto_auth_hmacsha512256_KEYBYTES 32U`
> src/libsodium/include/sodium/crypto_auth.h:16: `#define crypto_auth_BYTES crypto_auth_hmacsha512256_BYTES` -> `crypto_auth_hmacsha512256.h:15`: `#define crypto_auth_hmacsha512256_BYTES 32U`
> src/libsodium/include/sodium/crypto_auth.h:34-36: `nonnull(1, 4)` attribute — only `h` (arg1) and `k` (arg4) are required non-NULL; `in` (arg2) can be NULL/dummy when `inlen==0`
> test/default/auth5.c:37-38: `crypto_auth(a, guard_page, 0U, key); assert(crypto_auth_verify(a, guard_page, 0U, key) == 0);` — exercises the zero-length message path with a non-NULL "guard_page" sentinel buffer

### P2.3 Object Lifecycle
**Claim:** `crypto_auth_verify` is stateless/one-shot: no create/destroy object lifecycle. The only "object" involved is the caller-managed key buffer (`k`) and tag buffer (`h`), both plain byte arrays with no allocation/free semantics beyond ordinary memory. Compare against the streaming variant (`crypto_auth_hmacsha512256_init/_update/_final`) which does have a create(init)->update*->final(destroy-by-use) lifecycle over a `crypto_auth_hmacsha512256_state` struct, but that state is stack/caller-allocated, no separate alloc/free API exists for it.
**Evidence:**
> src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:38-44 (init), 46-53 (update), 55-66 (final) — explicit 3-stage streaming lifecycle
> src/libsodium/include/sodium/crypto_auth_hmacsha512256.h:38: `typedef crypto_auth_hmacsha512_state crypto_auth_hmacsha512256_state;` — caller declares this as a plain struct (e.g. on stack, as in test/default/auth.c:24 `crypto_auth_hmacsha512256_state st512_256;`), no sodium_malloc/free counterpart found for this state type

### P2.4 Return Value Handling
**Claim:** `crypto_auth_verify` returns `0` on success (`h` matches the recomputed MAC) and a nonzero value (observed `-1` pattern from `crypto_verify_n`/`sodium_memcmp`) on mismatch/forgery. The `warn_unused_result` attribute signals the return value must always be checked by the caller — never treat as void. Real test code treats `== 0` as "verified"/accepted and nonzero as "rejected"/forged.
**Evidence:**
> src/libsodium/include/sodium/crypto_auth.h:33-36: `__attribute__((warn_unused_result))`
> test/default/auth5.c:18-20: `if (crypto_auth_verify(a, c, clen, key) != 0) { printf("fail ...` — nonzero on an expected-valid MAC is a test failure
> test/default/auth5.c:24-27: `if (crypto_auth_verify(a, c, clen, key) == 0) { printf("forgery ...` — zero on a tampered message is a test failure (forgery accepted)
> src/libsodium/crypto_verify/verify.c:93-97 and 62-83: the underlying `crypto_verify_32` returns `0` for equal buffers and a nonzero (effectively `-1`) sentinel for unequal

### P2.5 Cleanup Sequence
**Claim:** No library resources need freeing after `crypto_auth_verify` — it allocates nothing dynamically (`correct[32]` is on the callee's stack and is not zeroed/freed by the caller, it is scoped inside the core function). Caller-owned buffers (`h`, `in`, `k`) are freed/managed by the caller as ordinary memory; if `k` was allocated with `sodium_malloc`, caller should call `sodium_free(k)` as usual sodium_malloc discipline, but this is independent of crypto_auth_verify's own protocol. No explicit `sodium_memzero` of `h`/`in` is required by the API contract (callers may do so for the key as defense-in-depth, as is common for `k`).
**Evidence:**
> src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:88-93: `unsigned char correct[32];` local, not heap-allocated, not surfaced to caller — nothing to free
> test/default/keygen.c:45,53: `key = (unsigned char *) sodium_malloc(tv->key_len); ... sodium_free(key);` — shows the project's convention when keys ARE heap-allocated (not required by crypto_auth_verify itself, but relevant if harness uses sodium_malloc for `k`)

### P2.6 API Existence
**Claim:** All functions referenced in this report exist in the current codebase at the cited locations.
**Evidence:**
> crypto_auth_verify: src/libsodium/crypto_auth/crypto_auth.c:30
> crypto_auth: src/libsodium/crypto_auth/crypto_auth.c:23
> crypto_auth_keygen: src/libsodium/crypto_auth/crypto_auth.c:37
> crypto_auth_hmacsha512256_init/_update/_final: src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:38,46,55
> crypto_auth_hmacsha512256_verify (core): src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:82
> crypto_auth_hmacsha512_init/_update/_final (core): src/libsodium/crypto_auth/hmacsha512/auth_hmacsha512.c:37,76,85
> crypto_verify_32: src/libsodium/crypto_verify/verify.c:93
> sodium_memcmp: src/libsodium/sodium/utils.c:184

### P2.7 Co-call Constraints
**Claim:** `crypto_auth_verify` has no strict "paired" API of its own (no begin/commit pair), but in practice it is always the second half of a produce/verify pair with `crypto_auth`: `crypto_auth(tag, msg, msg_len, key)` then `crypto_auth_verify(tag, msg, msg_len, key)`, both keyed by the SAME `key` and operating over the SAME `(msg,msg_len)`. For the streaming entry points, `_init` must always precede any `_update`, and `_init`/`_update`* must always precede exactly one `_final` call per state object (classic begin/…/commit pattern); `_update` may be called zero or more times with different chunk boundaries that must sum to the same total message as a one-shot call would use.
**Evidence:**
> test/default/auth5.c:15-18: `crypto_auth_keygen(key); randombytes_buf(c, clen); crypto_auth(a, c, clen, key); if (crypto_auth_verify(a, c, clen, key) != 0) ...` — produce/verify pair, same key & message
> test/default/auth.c:89-90 vs 93-95: two equivalent ways to reach the same final tag — `_init` then immediately `_final` (empty message) vs `_init`,`_update(...,0U)`,`_final` — confirms `_update` is optional but `_init` before `_final` is mandatory
> src/libsodium/crypto_auth/hmacsha512/auth_hmacsha512.c:76-83 (`_update` just forwards to `crypto_hash_sha512_update(&state->ictx,...)`, requiring `ictx` set up by `_init` first at line 56)

### P2.8 Prerequisite State
**Claim:** No connection/context/global config object is needed; the only prerequisite "state" is the caller-supplied byte buffers. For the streaming (`_init/_update/_final`) variants, the prerequisite state is the `crypto_auth_hmacsha512256_state` (= `crypto_auth_hmacsha512_state`) struct that must be initialized by `_init` before any `_update`/`_final` call on it (see P2.7). `sodium_init()` is the general libsodium prerequisite before using ANY libsodium API per project convention (sets up misc. internal tables/RNG), though `crypto_auth_verify` does not itself call into anything requiring it based on its own source.
**Evidence:**
> secretbox_easy_fuzzer.cc:22-23, crypto_sign_fuzzer.cc:22-23: both existing fuzzers call `sodium_init()` as the very first statement
> src/libsodium/include/sodium/crypto_auth_hmacsha512256.h:38,44-55: state type and the three nonnull-marked streaming functions that operate on it

### P2.9 Result Usage
**Claim:** The integer return of `crypto_auth_verify` (0 vs nonzero) is itself the only "result" — it is a boolean-like accept/reject decision consumed directly by an `if`/`assert` in every real call site found; it is NOT further passed into another libsodium API (unlike, say, a handle or filled buffer). Contrast: `crypto_auth` fills an output tag buffer `a`/`out` which IS then passed as the first argument (`h`) into `crypto_auth_verify` in every caller found.
**Evidence:**
> test/default/auth5.c:17-18: `crypto_auth(a, c, clen, key); if (crypto_auth_verify(a, c, clen, key) != 0) {...}` — `a` filled by `crypto_auth` becomes `h` consumed by `crypto_auth_verify`
> test/default/auth5.c:24,29,38: return of `crypto_auth_verify` consumed only by `if (...== 0)`/`assert(...== 0)`, no further propagation
> test/default/auth7.c:17-18: analogous pattern one level down with `crypto_auth_hmacsha512` / `crypto_auth_hmacsha512_verify`
> No caller found anywhere in src/ or test/ that uses the return value of `crypto_auth_verify` for anything other than a direct zero/nonzero branch (`Grep` of test/default/auth*.c, keygen.c confirms this).

### P2.10 Behaviour Controls
**Claim:** `crypto_auth_verify` itself has no mode flags — the only "control" is implicit via `inlen` (0 selects the empty-message path, exercised explicitly in tests) and via the content of `h`/`k`/`in` (bit differences select the match vs mismatch/forgery branch of the constant-time compare). The sibling streaming API's behavior is controlled by `keylen` passed to `_init`: if `keylen > 128` the key is first re-hashed via SHA-512 and truncated to 64 bytes before HMAC padding; otherwise it's used directly (zero-padded to 128 bytes). This >128 branch is NOT reachable via `crypto_auth_verify`/`crypto_auth` (which hardcode `crypto_auth_hmacsha512256_KEYBYTES`=32 as keylen) but IS reachable via the lower-level `crypto_auth_hmacsha512256_init(state, key, keylen)` entry with an attacker/harness-controlled `keylen`.
**Evidence:**
> src/libsodium/crypto_auth/hmacsha512/auth_hmacsha512.c:45-55: `if (keylen > 128) { crypto_hash_sha512_init(...); ...; key = khash; keylen = 64; } else if (key == NULL) { if (keylen > 0) sodium_misuse(); }` — the keylen-dependent branch
> src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:74-75: `crypto_auth_hmacsha512256_init(&state, k, crypto_auth_hmacsha512256_KEYBYTES);` — hardcoded 32, so the one-shot `crypto_auth`/`crypto_auth_verify` path never triggers the >128 branch
> src/libsodium/include/sodium/crypto_auth_hmacsha512256.h:44-46: `crypto_auth_hmacsha512256_init(state, key, keylen)` exposes `keylen` as a free parameter to callers of the streaming API
> test/default/auth5.c:14,22: loop over `clen` from 0 to 999 — `inlen`/`clen` selects SHA-512 block-boundary behavior (one 128-byte block vs multiple) and the empty-message special case

### P2.11 Input Expectations
**Claim:** `in`/`inlen` is an arbitrary byte buffer of any length (including 0); there's no required header, magic, or terminator — it's hashed byte-for-byte via SHA-512 (`crypto_hash_sha512_update`), which itself handles arbitrary lengths including partial blocks internally. `h` must be exactly 32 bytes (`crypto_auth_BYTES`) — the function reads/writes/compares a fixed 32-byte buffer regardless of what length the caller actually allocated, so under-sized `h` buffers are a caller bug (out-of-bounds read), not something the API detects. `k` must be exactly 32 bytes (`crypto_auth_KEYBYTES`) for `crypto_auth`/`crypto_auth_verify` (keylen is hardcoded, not parameterized, at this API level). `inlen` is `unsigned long long` (so very large sizes are representable, bounded practically by `size_t`/memory, with SHA-512's internal 128-bit bit-length counter being the only intrinsic protocol limit, not checked at this level).
**Evidence:**
> src/libsodium/include/sodium/crypto_auth.h:33-36: parameter types — `h`,`k` nonnull fixed-size (32-byte, by convention/macro) buffers, `in`/`inlen` arbitrary
> src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:88-93: `unsigned char correct[32]; ...; crypto_verify_32(h, correct) ... sodium_memcmp(correct, h, 32);` — hardcoded 32-byte compare regardless of caller's actual `h` buffer size
> test/default/auth5.c:14: `for (clen = 0; clen < 1000; ++clen)` plus the explicit zero-length case at line 37-38 — demonstrates 0 is a valid `inlen`

## Other entries of this Logic Group (besides the target)

### crypto_auth
**Declaration:** `src/libsodium/include/sodium/crypto_auth.h:28-31`: `int crypto_auth(unsigned char *out, const unsigned char *in, unsigned long long inlen, const unsigned char *k) __attribute__((nonnull(1, 4)));`
**Definition:** `src/libsodium/crypto_auth/crypto_auth.c:23-28`, forwards to `crypto_auth_hmacsha512256(out, in, inlen, k)`.
**Call protocol:** No init required (one-shot). `out` must be a caller-allocated, writable buffer of `crypto_auth_BYTES` (32) bytes — it is NOT read, only written (nonnull arg1). `in`/`inlen` arbitrary message (same as target). `k` nonnull, 32-byte (`crypto_auth_KEYBYTES`) key — any byte content is valid; produced by `crypto_auth_keygen` or any 32 bytes of caller-supplied material (e.g. fuzz input bytes).
**Return value:** always `0` (`return 0;` at crypto_auth.c:27/hmacsha512256:79) — the function has no error path, return exists only for API symmetry with its ancestor variants; callers do not need to check it (no `warn_unused_result` attribute on `crypto_auth`, confirmed absent at crypto_auth.h:28-31, in contrast to `crypto_auth_verify`).
**Free:** nothing to free; `out` buffer lifetime is caller-managed plain memory.
**Combination with target:** `crypto_auth(out, in, inlen, k)` is called first to produce a genuine tag in `out`; the harness then passes `out` as `h` to `crypto_auth_verify(out, in, inlen, k)` (expect 0), and/or mutates `out`/`in`/`k` bytes before calling `crypto_auth_verify` again to exercise the forgery-rejection path (expect nonzero).
**Evidence:**
> test/default/auth5.c:17-18: `crypto_auth(a, c, clen, key); if (crypto_auth_verify(a, c, clen, key) != 0) {...}`
> src/libsodium/crypto_auth/crypto_auth.c:23-28

### crypto_auth_keygen
**Declaration:** `src/libsodium/include/sodium/crypto_auth.h:38-40`: `void crypto_auth_keygen(unsigned char k[crypto_auth_KEYBYTES]) __attribute__((nonnull));`
**Definition:** `src/libsodium/crypto_auth/crypto_auth.c:37-41`: `randombytes_buf(k, crypto_auth_KEYBYTES);`
**Call protocol:** No init besides general `sodium_init()` (randombytes subsystem is initialized as part of sodium_init). Caller must pass a pointer to a writable buffer of exactly `crypto_auth_KEYBYTES` (32) bytes; function only writes, does not read.
**Return value:** `void` — no error to check (randombytes_buf cannot "fail" from the caller's perspective in this API).
**Free:** nothing to free (plain memory); if the harness wants deterministic/fuzz-derived keys instead of OS randomness, it can skip `crypto_auth_keygen` and copy 32 bytes directly from the fuzz input into `k` — the API itself does not validate key provenance, only key length is implicit from the fixed-size array type.
**Combination with target:** typically called once per test iteration before `crypto_auth`/`crypto_auth_verify` to produce the key material consumed by both.
**Evidence:**
> test/default/auth5.c:15: `crypto_auth_keygen(key);` inside the fuzz loop, immediately before `crypto_auth(...)`
> test/default/keygen.c:14,45-53: generic keygen smoke test confirms `crypto_auth_keygen` writes `crypto_auth_KEYBYTES` bytes and is safe to call repeatedly

### crypto_auth_hmacsha512256_init / _update / _final
**Declarations:** `src/libsodium/include/sodium/crypto_auth_hmacsha512256.h:44-55`.
**Definitions:** `src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:38-66`.
**Call protocol (strict order):**
1. `crypto_auth_hmacsha512256_init(state, key, keylen)` — MUST be called first on a caller-allocated `crypto_auth_hmacsha512256_state state;` (plain struct, stack or heap, no special allocator). `key` nonnull overall (function itself is `__attribute__((nonnull))`, i.e. both `state` and `key` must be non-NULL — note this differs from the lower-level `crypto_auth_hmacsha512_init` which tolerates `key==NULL` when `keylen==0`, see that function's own nonnull-less declaration). `keylen` is a free `size_t` parameter (not fixed to 32 at this layer) — see P2.10 for the >128 branch.
2. `crypto_auth_hmacsha512256_update(state, in, inlen)` — zero or more times, nonnull(1) only (so `in` may be NULL when `inlen==0`, mirrors target's `in` laxity). Must occur only after `_init` and before `_final` on the SAME state object.
3. `crypto_auth_hmacsha512256_final(state, out)` — exactly once per init'd state, `out` nonnull, must point to >= 32 writable bytes (`crypto_auth_hmacsha512256_BYTES`). After `_final`, the `state` must be re-`_init`'d before reuse (it is not auto-reset).
**Return values:** all three return `int` but always return `0` (see auth_hmacsha512256.c:44,53,65 and the underlying auth_hmacsha512.c:73,82,97 all `return 0;` unconditionally) — no error path to check in current implementation, though the signature allows one.
**Free:** nothing to free; `state` and `out` are plain caller memory. Internal temporaries (`ihash`, `out0`) are `sodium_memzero`'d internally (auth_hmacsha512256.c:63, auth_hmacsha512.c:95) — not caller-visible.
**Combination with target:** this streaming trio produces the SAME 32-byte tag as `crypto_auth(out,in,inlen,k)` would for equivalent `(in,inlen,k)` split across update calls; a harness can use it to build a tag incrementally (e.g. message split into chunks) and then feed that resulting `out` into `crypto_auth_verify(out, in_concat, inlen_total, k)` for cross-checking one-shot vs streaming equivalence — this is exactly what test/default/auth.c:89-101 does for the empty-message case, and what general property-based libsodium tests do for chunked vs. whole-buffer hashing.
**Evidence:**
> src/libsodium/crypto_auth/hmacsha512256/auth_hmacsha512256.c:38-66
> test/default/auth.c:22-24 (state decl), 89-95 (init/update/final usage, two equivalent empty-message paths), 133 (`crypto_auth_hmacsha512256_statebytes() >= ...` sanity check on state size)
> src/libsodium/include/sodium/crypto_auth_hmacsha512256.h:44-55 (nonnull annotations differ per function — `_init` plain `nonnull` i.e. all pointer args; `_update` `nonnull(1)` only; `_final` plain `nonnull`)

### Not part of this Logic Group / non-existent as reported
**Claim:** All six listed entries (`crypto_auth_verify`, `crypto_auth`, `crypto_auth_keygen`, `crypto_auth_hmacsha512256_init`, `crypto_auth_hmacsha512256_update`, `crypto_auth_hmacsha512256_final`) exist exactly as declared. No additional/missing entry was found that should be added to the group based on real call sites searched (test/default/auth.c, auth2.c, auth3.c, auth5.c, auth6.c, auth7.c, keygen.c).
**Evidence:**
> `Glob`/`Grep` across `test/default/*.c` found no other distinct crypto_auth* function family beyond the hmacsha256 (different, 32-byte-key *different* primitive `crypto_auth_hmacsha256_*`, not in this group's default/portable primitive) and the hmacsha512/hmacsha512256 families documented above; `crypto_auth_hmacsha256_*` is a separate, non-default primitive not referenced by `crypto_auth.h`'s macros (`crypto_auth_PRIMITIVE` is `"hmacsha512256"`, crypto_auth.h:24) and is out of scope for this group.

## Build notes

**Claim:** Fuzzers for this project are compiled as C++ (`.cc` files, `-std=c++11`), linked against the static `libsodium.a` built by `./configure && make`, with the single public umbrella header `<sodium.h>` included (which itself pulls in `sodium/crypto_auth.h` and related headers) rather than the individual `sodium/crypto_auth*.h` files directly. Existing sibling fuzzers (`crypto_sign_fuzzer.cc`, `secretbox_easy_fuzzer.cc`) are the evidence pattern to follow; there is currently only a stub (non-functional) `secret_key_auth_fuzzer.cc` for this target in the OSS-Fuzz dir that just keeps the symbol `crypto_auth_verify` linked via `__asm__` aliasing, confirming the project's naming convention (`secret_key_auth_fuzzer`) for this feature but providing no real logic to imitate.

**Evidence:**
> ${QF_WORK}/ossfuzz_view/libsodium-c3/build.sh:19-27:
  ```
  ./configure --enable-static LDFLAGS="$CXXFLAGS"
  make -j$(nproc) all
  for f in $SRC/*_fuzzer.cc; do
      fuzzer=$(basename "$f" _fuzzer.cc)
      $CXX $CXXFLAGS -std=c++11 -I"$SRC/libsodium/src/libsodium/include" \
           "$f" -o "$OUT/${fuzzer}_fuzzer" \
           "$SRC/libsodium/src/libsodium/.libs/libsodium.a" $LIB_FUZZING_ENGINE
  done
  ```
  — C++ compile (`$CXX`), include path is `src/libsodium/include` (where `sodium.h` and the `sodium/` subdir with `crypto_auth.h` live, confirmed by `Glob` finding `src/libsodium/include/sodium/crypto_auth.h`), links the static archive plus `$LIB_FUZZING_ENGINE`.
> ${QF_WORK}/ossfuzz_view/libsodium-c3/Dockerfile:17-21: builder base image, `git clone ... libsodium`, then `COPY ... *.cc *.h $SRC/` — fuzzer sources are plain `.cc`/`.h` files dropped at `$SRC` root, picked up by the `*_fuzzer.cc` glob in build.sh.
> ${QF_WORK}/ossfuzz_view/libsodium-c3/crypto_sign_fuzzer.cc:17: `#include <sodium.h>` — sibling fuzzers include the umbrella header, not the per-primitive header directly.
> ${QF_WORK}/ossfuzz_view/libsodium-c3/secret_key_auth_fuzzer.cc:1-16: current stub for this exact feature — confirms the expected fuzzer filename for this Logic Group but contains no real call sequence (only an `__asm__`-aliased symbol reference to keep `crypto_auth_verify` linked, and a no-op `LLVMFuzzerTestOneInput`).
> ${QF_WORK}/ossfuzz_view/libsodium-c3/secretbox_easy_fuzzer.cc:22-23: `sodium_init()` called first in every real fuzzer — convention to replicate for the new harness.