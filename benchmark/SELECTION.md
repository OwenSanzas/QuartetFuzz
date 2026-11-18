# Benchmark selection

How the 100 tasks were sampled and admitted. The numbers are those of the
final benchmark (`benchmark_100.jsonl`, per-task `gold/report/`).

**Gold-harness benchmark.**
We evaluate against 100 human-written OSS-Fuzz harnesses drawn from a defined sampling frame
under rules fixed before any of the four systems produced a harness. Every rule is decided from
public data or from the project's own build and install files, never from our checker or from
any system's output, so the exclusion set is identical for all systems compared. The complete
per-target exclusion record, the seeded draw, and every subsequent replacement are released with
the artifact.

*Frame.* We enumerated all 6,478 fuzz targets of the 537 C/C++ projects for which OSS-Fuzz
publishes coverage data. A target enters the frame if (i) Google's most recent build of its
project succeeded, (ii) it ran on ClusterFuzz within the preceding 90 days, (iii) it defines
`LLVMFuzzerTestOneInput` and its source is retrievable, (iv) at least one library call receives
the fuzz input, (v) its project's build accepts a swapped-in harness without per-project
scaffolding, (vi) it covered at least one line on at least one of ten consecutive days, and
(vii) its official corpus is publicly retrievable. These leave 444 targets in 127 projects. Two
further input-side conditions remove 28 more: the upstream repository must still be maintained,
and the official corpus must hold at least 50 seeds, a threshold that falls in a gap of the
distribution (largest removed 25, smallest retained 97). Corpus retrievability biases the frame
toward mature, long-fuzzed projects whose harnesses are stronger; we note this direction
explicitly.

*Draw.* From the 416 eligible targets we drew 100 by two-stage cluster sampling with a published
seed: projects first, then at most five targets per project, with floors on three API-arity
strata (single call; 2–4 calls; ≥5 calls or an explicit create/use/destroy lifecycle) so that
single-call parsers, which present almost no API-protocol surface, cannot dominate.
Project-level clustering is carried through to all statistics.

*Target function and public API.* Each case names the library function a developer would call
to perform the task the harness tests, together with the project's own install rule and the
header and line that declare it. Twenty-four initially drawn targets were declared only in
headers the project does not install; because every system would fail on them for reasons
unrelated to harness quality, they were replaced from the same frame under the same rules.

*Qualification.* Each selected harness is built in the official OSS-Fuzz runner image under
AddressSanitizer and under coverage instrumentation, then replays its entire official corpus
deterministically (`-runs=0`). It qualifies if the replay completes at exit 0, executes 0.9–1.6×
the seed count, reports zero distinct faults, and yields non-zero coverage within the project's
own sources. The corpus is maintained by ClusterFuzz: the fuzzing engine writes an input to the corpus only after it has executed and added coverage, and the pruning task moves inputs that crash, time out or exceed the memory limit into a quarantine corpus that is re-tested periodically. The main corpus therefore holds no known deterministically crashing input under ClusterFuzz's own build. We replay it at the pinned upstream revision in the official runner image, so a fault on replay means that *our* build cannot execute inputs ClusterFuzz currently considers good: a defect in the harness, an upstream regression since the corpus was last pruned, or an environment incompatibility. Such a harness cannot serve as a reference point, because its coverage measurement would be truncated at the fault. This criterion establishes the soundness of the measurement setup; it does *not* establish the absence of unfixed bugs reachable from the harness, since the inputs that trigger those are exactly what ClusterFuzz keeps out of the corpus. This is the only criterion decided by executing the harness. It is applied identically to every case before any system runs and does not involve
coverage. Twenty-three candidates were replaced on this ground.

*Health gate.* A harness that passes replay must also fuzz for 24 hours under exactly the main
experiment's conditions (same image, `-rss_limit_mb=2560 -timeout=25`, the image's default
`ASAN_OPTIONS`, empty corpus, one core) without producing any `crash-` or `leak-` artifact, and with fewer than 24 `oom-` artifacts
(on average under one per hour); isolated `oom-` and `timeout-` artifacts are resource events,
not faults, but a harness that exhausts memory every hour spends its budget restarting. The replay gate shows that the reference
can execute in our measurement setup; the health gate shows that it is in a usable state at the
pinned revision. ClusterFuzz quarantines crash-triggering inputs out of the corpus, so a harness
with a known, unfixed crash can replay cleanly and still crash within seconds from an empty
corpus. Both gates look only at the gold harness, run before any system generates a harness,
apply identically to every case, and make the baseline stronger. Cases that fail are replaced
from the same frame under the same seed and are retained as a reported secondary set. Twelve
cases were replaced on this ground. One run was disrupted by our own tooling: the log pipe of
`util-linux/test_fdisk_script_fuzz` broke after 77,064 s and the container ran on detached and
unlogged, leaving a `crash-` artifact that has no report and does not reproduce on the pinned
build in 5,000 executions; the run's first 77,064 s were kept and the remaining 9,336 s were
fuzzed on its corpus, without a fault, and a separate uninterrupted 24-hour rerun is recorded.

*Never used for selection.* Coverage level, whether the harness crashes under fuzzing, and
whether it satisfies P1–P4 as judged by our checker. Coverage is the dependent variable; a
threshold on it would make the result true by construction. Letting our checker certify the
baseline would be circular. The lowest line coverage in the set is 1.20% and nine cases lie
below 5%; all are kept and reported.

*Versions.* All cases are pinned to one OSS-Fuzz commit and, per project, to the upstream
revision OSS-Fuzz built on 2026-08-21, taken from its published `srcmap`.

*Composition.* 100 harnesses, 46 projects, 93 distinct target functions (five yara harnesses
share `yr_rules_scan_mem`). Arity strata: 56 stateful, 27 composite, 17 single-call; the
single-call reserve was exhausted, so two single-call slots were filled from the overall reserve
order. Eleven targets entered OSS-Fuzz after the generator model's training cutoff and form a
contamination lower-bound check. Eighteen cases survive from our earlier 100-case set; results are
reported separately for retained and newly drawn cases. Line coverage of the gold harnesses on
their own corpora ranges from 1.20% to 97.43% (median 23.57%); the 688,672 seeds replay with zero
faults.

*Disclosure.* The maintenance, corpus-size, and public-API conditions were formulated after the
first draw and applied to cases already held; each is measured on the input side and none
touches the dependent variable. One early replacement round was voided in full for a ledger
error and is recorded as such. Each case's prompt is generated from its target function,
declaration, and project name alone; eighteen earlier hand-written prompts that described the
gold harness were discarded.

