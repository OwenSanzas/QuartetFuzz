# AP Probe Example

A real input/output pair for both Adversarial Probing operations
(see paper §3.4) captured from the live 25-case smoke run on
`yajl-ruby/json_fuzzer`.  This case finished at 78% line / 68%
branch coverage; the agent's adversarial blob passed the run check
and its reach blob hit the target API.

Source trajectory:
`subset_25/precomputed/yajl-ruby__json_fuzzer/trajectory.jsonl`
(single-shot probes; total wall-clock per probe ≤ 10 s).

---

## Probe 2 — `AP_Run_check` (P1.x sanitizer probe)

The agent writes a Python `generate() -> bytes` that produces one
attack blob.  The system feeds it to the ASan/LSan binary inside
the project's OSS-Fuzz Docker image and reports any sanitizer
fire.

### INPUT

```json
{
  "binary_name": "json_fuzzer",
  "generator_code": "
    def generate() -> bytes:
        # P1.x attack: oversize input with hostile bytes (embedded
        # nulls, high bytes, malformed UTF-8, deeply nested
        # structures).  Tests P1.1 (leaks), P1.5 (buffer safety),
        # P1.7 (UB).
        flags = bytes([0x03])  # allowComments=1, checkUTF8=1
        payload = (
            b'[' * 50 +
            b'{\"key\": \"\\u0000\\uD800\\uDFFF\\xFF\\xFE\", \"n\": 1e999},' +
            b'\"\\u00e9\\u00e0\\u00fc\",' +
            b'null, true, false, -0, 1.5e-10,' +
            b'/* comment */ 42,' +
            b'\"' + b'\\x' * 100 + b'\"' +
            b']' * 50
        )
        return flags + payload
  "
}
```

### OUTPUT

```
RUN PASS  binary=json_fuzzer  sanitizer=—  exit_code=0
```

The agent's adversarial input did not trigger ASan / LSan
during the single-shot run, so all P1.x sub-checks the blob aimed
at are considered satisfied for this attempt.

---

## Probe 1 — `AP_Reach_check` (P2.x reach probe)

The agent writes a Python `generate() -> bytes`, names a target
API, and the system runs the binary under GDB (also inside the
project's OSS-Fuzz container) with a breakpoint on the target,
reporting whether the breakpoint fired and which functions were
observed.

### INPUT

```json
{
  "binary_name": "json_fuzzer",
  "target_function": "yajl_parse",
  "generator_code": "
    def generate() -> bytes:
        # P2.x reach probe: minimal valid JSON object.
        flags = bytes([0x03])  # allowComments=1, checkUTF8=1
        return flags + b'{\"hello\": \"world\", \"num\": 42, \"arr\": [1, 2, 3]}'
  "
}
```

### OUTPUT

```
TARGET HIT: yajl_parse
Functions hit: yajl_parse
```

A minimal well-formed JSON document drives execution through the
harness's init / config / parse chain into the public `yajl_parse`
API, confirming that P2.1 (init order) and P2.2 (parameter
construction) are satisfied for this entry.

---

## Why two distinct blobs?

The two probes target orthogonal sub-check families and therefore
use blobs designed for different goals:

- **P1 (logic correctness, sanitizer-detected)** — adversarial
  bytes meant to surface buffer / lifetime / UB bugs in the
  *harness*, not the library.  A clean `RUN PASS` says the harness
  itself is robust under hostile input.
- **P2 (API protocol)** — minimal valid input that should
  unambiguously reach the documented entry function.  A `TARGET
  HIT` says the harness's init sequence and parameter construction
  are correct enough to drive the library.

Both blobs are produced by Python `generate()` callbacks the agent
writes itself; the system never imposes the blob shape.
