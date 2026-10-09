# Experiment Protocol — QuartetFuzz Resubmission

**Status:** DRAFT — dataset drawn 2026-08-21 (`QuartetFuzz/dataset_v2/`), not yet frozen. Sections marked 🔒 are decisions we consider locked;
sections marked ❓ are blocking questions that must be resolved with real numbers
before the protocol is hashed and timestamped.

**Purpose.** This file is the single source of truth for how the resubmission's
experiments are run. It is written so that it can be published verbatim as a
pre-registration document. Every number here that is not yet measured is marked ❓.

**Related files:** [`reviews.md`](reviews.md) (CCS 2026 B #983 reviews + rebuttal).

---

## 0. Why the protocol is changing

The prior submission was rejected with the AC noting: *"it is unclear how it achieved
the results."* The root cause is that the paper's claim is about **harness correctness**
while every dependent variable in the evaluation was a behavioural proxy (coverage,
productive rate, bug counts) — the same proxies the paper faults prior work for using.

Two consequences drive this protocol:

1. **Correctness must become a measured dependent variable** (§5).
2. **Selection must never touch the dependent variable or the hypothesis** (§3, §4).

---

## 1. Locked decisions

| # | Decision | Rationale |
|---|---|---|
| 🔒 1.1 | Primary corpus condition is **empty corpus** | Isolates harness quality, which is the paper's dependent variable. The official OSS-Fuzz corpus was accumulated by fuzzing *the gold harness*, so it is not a neutral condition — it is shaped by gold's input handling. |
| 🔒 1.2 | Secondary condition is the **official global corpus** | Kept as a robustness check, and reframed: because the corpus favours gold, QF reaching parity *under gold's own seeds* is a stronger result than parity under empty corpus. |
| 🔒 1.3 | **30 trials** per (case × system) on the primary condition | Matches Klees et al. (CCS'18), who ran 30 trials. The "five trials" figure cited by Reviewer A is community convention, not Klees's number. Compute is not a constraint (HPRC). |
| 🔒 1.4 | **24 h** per trial | Klees et al. recommend 24 h and show shorter budgets are misleading. |
| 🔒 1.5 | Within-case aggregation across trials = **median** | Cross-trial coverage distributions are skewed; a single lucky breakthrough distorts the mean. Consistent with the rank-based statistics we report. |
| 🔒 1.6 | Cross-case headline = **paired difference distribution**, not two means | Gold coverage on the current dataset is heavily right-skewed (p50 = 9.7%, mean = 17.7%; the top 10 cases hold 39% of total coverage mass), so a mean-vs-mean headline is decided by ~10 cases. |
| 🔒 1.7 | Selection never uses coverage, crash behaviour, entry visibility, or P1–P4 compliance | These are outcomes, not admission criteria. Filtering gold on P1–P4 would certify the baseline with the instrument under evaluation. |
| 🔒 1.8 | Derivation and held-out sets must be **project-disjoint** | Harnesses from the same library share API conventions, author style, and defect patterns — harness-level disjointness alone leaks. |
| 🔒 1.9 | Mutation operators are derived from **observed defects**, never from our own checklist | Otherwise the experiment shows only that the implementation matches the specification, not that the specification reflects reality. |
| 🔒 1.10 | Gold enters the blind annotation study as a **fourth arm** | Gives a non-circular measurement of gold's own violation rate at zero extra design cost. |

---

## 2. Blocking questions (resolve before freezing)

| # | Question | Status | Answer / fallback |
|---|---|---|---|
| ❓ 2.1 | When did P1–P4 stabilize during the original audit? | **open** | Archaeology: commits, notes, chat logs, audit batch order. If lost, redo open coding on the derivation set (~1 week). |
| ❓ 2.2 | Is the historical split point also a project boundary? | **open** | Same archaeology. Audits ran project by project, so it probably is. |
| ✅ 2.3 | Frame size once corpus retrievability is required? | **resolved** | **3,392 targets / 352 projects.** Corpus availability alone removes only ~10%; it is a light constraint. Frame is 6× the previous 586-harness pool. |
| ✅ 2.4 | Are there ≥15 post-cutoff C/C++ harnesses? | **resolved** | **Yes** — 15 drawn, from targets absent at the 2025-07 anchor. Four anchors are recorded per target, so the cutoff can be moved without re-crawling. |
| ✅ 2.5 | How many does "180-day unmodified" remove? | **dropped** | The rule protected a historical-coverage-stability criterion this protocol no longer uses. Removing it eliminates a free parameter. Target age is recorded and reported, never used as a filter. |
| ❓ 2.6 | Does the effect size survive 24 h with real seeds? | **open** | **Pilot: 15 cases, full protocol, before committing the full sweep.** If it compresses, the headline moves to violation rate — which was the plan anyway. |
| ❓ 2.7 | Can an OSS-Fuzz ASan binary run under Apptainer on HPRC? | **open** | Convert `gcr.io/oss-fuzz-base/base-runner` to `.sif`, run one binary. Single point of failure for the whole compute plan. |
| ❓ 2.8 | Is the HPRC allocation large enough? | **open** | Check the SU balance. Reduce the secondary condition first; **the primary condition's trial count is not negotiable.** |

### Exclusions as actually applied

| Code | Criterion | Removed |
|---|---|---|
| `E1_build_failing` | Google's own most recent OSS-Fuzz build of the project failed | 843 |
| `E3_inactive_90d` | No coverage run in the past 90 days | 1,699 |
| `F_no_public_corpus` | No retrievable `public.zip` corpus | 663 |
| `E5_build_not_swappable` | Python *is* the build system: meson/scons/waf, or a `build.sh` that delegates to a `.py` and never invokes make/cmake/configure/`$CC` | 366 |

`E5` was added after drafting: the pipeline swaps a harness into the project's own
build, which needs per-project scaffolding for Python-driven build systems. It is
defined by a grep over the project's own `build.sh`, applied before sampling, and
costs 9.6% of the frame. It fires on genuine Python build systems only — `fwupd`,
`harfbuzz`, `cairo`, `glib` are out, while `binutils` (a `.py` that only generates
seeds) and `mbedtls` (a `.py` that only tweaks config) are kept. Large and complex
projects are **not** excluded: 37 of the 100 cases come from projects with more
than 15 targets.

### Two stratification dimensions were added

Neither existed in the draft; both are mechanical and fixed before the draw.

- **Project size.** 45 large projects hold 68% of the frame, so a draw uniform
  over targets would be two-thirds large-project cases. Bounded, not excluded.
- **API arity.** A single-call harness has almost no API-protocol surface — no
  ordering to get wrong, no lifetime to violate, no cleanup to omit. Without a
  floor on the composite and stateful strata, every system's violation rate is
  pushed toward the floor and the comparison loses the differences it exists to
  detect.

## 3. Dataset D1 — splitting the 586-harness audit

The audit is split once, into two halves that answer two different questions. **No harness
is re-audited.**

### 3.1 Derivation set (A)

- **Size:** ~200 harnesses, all drawn from those exhibiting abnormal coverage,
  false-positive crashes, or runtime errors — i.e. a **purposive sample of failures**.
- **Where it appears:** §3 Empirical Study. **It is not an RQ.**
- **What it produces:** the failure taxonomy → P1–P4, plus the saturation curve.
- **What it must NOT produce:** ⚠️ **a violation rate.** A purposive sample of failing
  harnesses has no valid denominator. The 9%-style figure comes only from the held-out set.
- **Reporting note:** violations found here are still reported upstream, still merged, and
  still counted in the maintainer-confirmation totals. The only prohibition is using
  derivation-set performance to claim checker accuracy.

### 3.2 Held-out set (B)

- **Size:** the remaining ~386 harnesses, **project-disjoint from A**.
- **Coverage of the run:** the checker runs on **all** of B, not only on suspicious ones —
  this is what makes the denominator valid.
- **Where it appears:** §7 RQ1.
- **What it produces:** violation rate · precision (maintainer confirmation) ·
  recall (via §5.1 mutation injection).

### 3.3 Open coding methodology (derivation set)

1. **Independent labelling.** 2–3 coders read each harness and its defect with **no
   pre-existing category scheme**, attaching free-text labels ("missing free", "init after
   use", "fuzz bytes never reach the API", "calls an internal entry").
2. **Axial coding.** Periodic reconciliation: merge synonyms, split conflated labels.
3. **Iterate to stability.** Repeat until the category set stops changing.
4. **Saturation.** Record, per batch of N harnesses, how many *previously unseen* categories
   appear. Plot cumulative categories against harnesses examined. A flat tail (e.g. the last
   50 harnesses produce no new category) is the empirical evidence that the taxonomy is
   complete — this is the only available answer to "why exactly four?"
5. **Report** coder count, qualifications, codebook, and inter-coder κ.

### 3.4 Generalization check (secondary validation set)

The taxonomy is derived from human-written production harnesses. Verify it also covers
LLM-generated failures by applying it to the OFG / PromeFuzz / QF outputs. Categories that
do not fit are themselves a reportable finding. (The prior submission asserts this
informally in intro line 29 — "informal reviews … showed the same patterns". Make it formal.)

---

## 4. Dataset D2 — reselecting the 100 gold harnesses

### 4.1 Sampling frame

All harnesses of all C/C++ projects on OSS-Fuzz whose **official global corpus is publicly
retrievable**.

Corpus retrievability is a prerequisite of the two-condition design, is mechanically
decidable, and applies identically to all four systems. It biases the frame toward mature,
long-running projects — whose gold harnesses are *stronger* — and is therefore
**conservative** with respect to our claims. Report the direction of this bias explicitly.

### 4.2 Exclusions (mechanical only; log every one)

| Code | Criterion |
|---|---|
| **E1** | Project fails to build at the pinned commit, judged by **the project's own `build.sh` / Dockerfile** — independent of any system's wrapper, so the exclusion set is neutral across the four systems |
| **E2** | Build does not emit the corresponding binary |
| **E3** | No execution record on ClusterFuzz in the preceding 90 days (abandoned harness) |
| **E4** | Harness source modified within the preceding 180 days ❓2.5 (historical data not comparable) |

**Deliberately NOT excluded** (all four are reported as results, never used as filters):

- low coverage
- crashing under ASan
- entry function is not a public API
- gold harness violates P1–P4

### 4.3 Sampling

From the resulting frame of ❓*M* harnesses, draw **100 uniformly at random** under a
published seed *S*, subject to:

- **≤ 2 harnesses per project.** (The prior dataset had ndpi at 11/100 and the top 5
  projects at 31/100.) Declare this as project-level clustering and use cluster-robust
  statistics throughout.
- **≥ 15 harnesses whose source file first entered the oss-fuzz repository after the
  generator model's training cutoff** ❓2.4 — retained as the **contamination control group**.

### 4.4 Contamination control group — scope and limits

- Controls for *"the model memorized this harness"*, **not** *"the model knows this library"*.
  State this scope limit in the paper; do not let a reviewer state it first.
- 15–20 cases lack the power to prove equivalence. Frame it as a **lower-bound check**.
- Pair it with a second independent signal: **code similarity (token- and AST-level) between
  each generated harness and its gold counterpart**. Low similarity plus coverage parity is
  the joint argument.

### 4.5 Freezing

Case list, random seed *S*, protocol text, and the complete exclusion log are hashed and
pushed to a public remote, **timestamped before the first experimental run**. A
self-generated hash with a self-generated timestamp is not sufficient — the timestamp must
be one we cannot forge.

### 4.6 Defects in the previous dataset (do not reproduce)

Found by inspecting the released artifact — a reviewer downloading it would find the same:

- **8 cases violate the paper's own stated criterion (iii) `>1% line coverage`** — lowest
  0.40% (`ndpi/fuzz_filecfg_malicious_sha1`), then 0.50% (`libcoap/get_asn1_tag_target`),
  0.50% (`ndpi/fuzz_ds_tree`), and five more below 1.0%. Protocol differences between the
  screening run and the reported baseline explain some drift, not 0.40%.
- **2 rows are internally inconsistent** — `openssh/authopt_fuzz` (lines 4.2, functions 0.0,
  regions 0.0) and `openssh/sig_fuzz` (lines 5.4, functions 0.0, regions 0.0). Non-zero line
  coverage with zero function coverage is impossible; the llvm-cov collection needs
  investigation.
- **Project concentration** — ndpi 11/100; top 5 projects 31/100.
- **Severe right skew** — line coverage p10 1.4 / p25 4.2 / p50 9.7 / p75 20.3 / p90 54.6,
  mean 17.7. Top 10 cases hold 39.2% of total coverage mass.

---

## 5. New experiments

### 5.1 E1 — Mutation injection (checker recall)

**Purpose.** Produce the recall figure that does not currently exist, and answer
"how do you know your checker works?" without relying on human judgment.

**Design.**

1. **Source harnesses:** drawn from held-out set B, restricted to harnesses the checker
   judged **clean** (so natural-defect measurement and injected-defect measurement do not
   interact).
2. **Mutation operators:** 🔒1.9 induced from the **53 confirmed violations (RQ1)** and the
   **44 harness-side crashes intercepted during generation (RQ5)**. Operators must be
   grounded in observed defects, never enumerated from our own 16 sub-checks.
3. **Validity constraint:** every mutant must still compile. Discard and regenerate
   otherwise; log the discard rate.
4. **Run** the checker on each mutant. Outcome is binary and reviewable: detected / missed.
5. **Report** detection rate **per sub-check** (a 16-row table), plus the aggregate.

**Companion change.** Switch AP from agent-selected sub-checks to **all 16 mandatory**
(promised in the rebuttal), and use this benchmark to quantify before/after.

**Cost:** 1–2 weeks engineering, negligible compute.

### 5.2 E2 — Blind annotation (correctness as the dependent variable)

**Purpose.** The headline table: violation rate of Gold / QF / OFG / PromeFuzz.

**Material.** 100 cases × 4 sources = 400 harnesses.

**De-identification.**
- strip license headers (gold's copyright block is the clearest tell)
- strip or normalize comments
- reformat everything with one shared `clang-format` config
- normalize filenames and include guards
- **leave variable naming untouched** — it is part of what is being judged

**Manipulation check.** Perfect blinding is impossible. After annotation, ask each annotator
to guess the provenance of each harness. Accuracy near chance ⇒ blinding held; significantly
above chance ⇒ report it and discuss the implication. Do not skip this.

**Randomization.** Independent random order per annotator. **The four versions of a single
case must never be adjacent** — otherwise annotators compare across them and infer which is
human-written.

**Annotators.** Three, **none of whom built the pipeline**. Report their qualifications
(years of C/C++, fuzz-harness authoring experience).

**Instrument.** 16 sub-checks + P3 + P4, each ternary: *violated / not violated / cannot
determine*. Every claimed violation **must carry `file:line` evidence**.

> ⚠️ Annotators may read the library source. Annotators must **never** see QF's generated P2
> protocol report — that would frame the judgment in our own terms.

**Calibration.** Before the main pass, all three annotate the same ~20 harnesses drawn from
**outside** the 400, reconcile the codebook, and compute κ. If κ falls short, revise the
codebook and repeat. Only then freeze the codebook and begin.

**Assignment and adjudication.** All 400 double-coded independently; disagreements resolved
by a **third annotator**, not by discussion-to-consensus (which inflates agreement). Report
**per-sub-check κ**, not only an aggregate, and name the sub-checks that prove subjective.

**A column not to omit.** OFG and PromeFuzz fail to produce a harness on many cases. Record
those as *no harness produced*, keep them out of the violation-rate denominator, and report
them separately — otherwise the obvious objection is that their low rate reflects producing
nothing.

**Cost.** 800 annotations × ~10 min ≈ 133 person-hours ≈ 45 h each across three people,
roughly two weeks part-time. Judging P2 compliance requires reading library source; do not
underestimate the per-item time.

### 5.3 Construct validity — the four converging sources

Correctness has no objective readout the way coverage does; it is a **construct**. We do not
claim ground truth. We establish construct validity by converging four *mutually independent*
sources, and we state each one's limits in the paper:

| Source | Strength | Limit |
|---|---|---|
| **Maintainer adjudication** (45/53 confirmed) | External, independent of our definitions | Precision only; covers the audit path, not the generation path |
| **Mutation injection** (§5.1) | Hard, constructed ground truth; sidesteps "what is correct" | The defects are synthetic |
| **Behavioural signatures** | Fully objective — leaks → LSan; harness crashes without a library frame in the stack; input never reaches the API → GDB breakpoint never hit | Covers only ~5–6 of the 16 sub-checks |
| **Blind annotation** (§5.2) | Covers the full instrument; κ answers "is this measurable at all?" | Most subjective; low κ is itself a finding |

**Paper stance to state explicitly:** *we do not claim objective ground truth for
correctness; we establish construct validity through convergent evidence from four
independent sources.*

### 5.4 P3/P4 — what measurement cannot settle

Reviewer C's objection (P3 is heuristic; P4 is target prioritization, not correctness) is
**definitional, not empirical**. None of the four sources resolves it — mutation injection
included, since injecting "call an internal entry instead" begs exactly the contested
question.

Two routes, to be decided before writing:

- **Ground P3/P4 in consequences.** Instead of arguing that violating P3 is wrong, measure
  what it *causes*: are crashes from P3-violating harnesses more likely to be rejected by
  maintainers? Computable from the 586 audited harnesses and the 81 RQ5 crashes. A real
  correlation converts P3 from a heuristic into a property with verifiable consequences.
- **Reclassify.** Present P1/P2 as harness-internal correctness (runtime-verifiable) and
  P3/P4 as target-adequacy preconditions (statically decidable) — two kinds of condition,
  not four peers. This follows C's own wording and preserves the framework's identity
  (C also wrote "the four principles are inspiring").

---

## 6. Measurement protocol (fuzzing)

### 6.1 Conditions

| | Corpus | Trials | Duration | Systems | Cases | core-hours |
|---|---|---|---|---|---|---|
| **Primary** | empty | **30** | 24 h | Gold, QF, OFG, PromeFuzz | 100 | 288,000 |
| **Secondary** | official global corpus | 10 ❓2.8 | 24 h | same | 100 | 96,000 |

Sanitizer: ASan + LSan throughout (LSan linked at build time, runs at process exit).
Engine: libFuzzer. Build-retry budget: 5, identical for all LLM-based systems.
Model: identical across all LLM-based systems.

### 6.2 Aggregation

1. **Within case, across trials → median.** 🔒1.5
2. **Also report cross-trial dispersion** (IQR or min–max per case). Klees et al. emphasize
   reporting variance; with 30 trials there is no excuse not to.
3. **Across cases → the paired difference distribution**, 🔒1.6 not two means:
   median Δ, IQR, count of cases within ±2 pp, Cliff's δ, TOST.
   Means may appear as secondary descriptive statistics **with the skew disclosed**.

### 6.3 Statistics

- **Paired Wilcoxon signed-rank** for system-vs-system differences.
- **Cliff's δ** for effect size.
- **TOST** within a ±2 pp band for the equivalence claim against gold.
- **Cluster-robust** treatment for the ≤2-per-project constraint (§4.3).

Add this sentence to §5.1 of the paper to pre-empt the Klees objection on statistics:

> Wilcoxon signed-rank is the paired analogue of the Mann–Whitney U test recommended by
> Klees et al.; our design is paired by construction, since all systems generate for the
> same target. Cliff's δ is a linear transform of their Â₁₂ (Â₁₂ = (δ + 1)/2).

### 6.4 Plots

- **Coverage over time**, median curve with an **IQR ribbon** across the 30 trials, one band
  per system. Klees et al. explicitly recommend plotting performance over time; the prior
  submission reports only endpoint values. The 24 h × 30 data produces these for free, and
  they visualize harness quality directly — under an empty corpus, a better harness climbs
  faster.
- **Paired scatter** (gold coverage on x, QF coverage on y, parity diagonal). 100 points show
  wins, losses, and spread at a glance — far more informative than two headline numbers.

### 6.5 Bug counts (RQ5) — do not average

Bug discovery is not a continuous quantity. Report the **deduplicated union** across runs
(dedup by ASan top-3 frame), or the per-run distribution. "On average 1.7 bugs" is not a
meaningful statement.

### 6.6 A note worth making in the paper

A 24 h empty-corpus run **self-seeds**: libFuzzer accumulates its own corpus over the run,
so by hour 24 the "empty" condition is no longer empty — but the corpus it holds was *earned
by the harness itself*. This narrows the gap between the two conditions at 24 h relative to
600 s, and is part of the answer to Reviewer C's empty-corpus objection. Watch for it in the
pilot ❓2.6.

---

## 7. Compute architecture (HPRC)

HPRC compute nodes run Apptainer/Singularity and provide **no Docker and no root**, while
`helper.py build_fuzzers` requires Docker. Split build from fuzz:

```
Local server (Docker)                    HPRC (Apptainer)
─────────────────────                    ────────────────
helper.py build_fuzzers          ──┐
  → ASan binaries                   │
  → coverage binaries               ├──▶  stage to /scratch
apptainer build base-runner.sif     │            │
  ← docker://oss-fuzz-base/...    ──┘            ▼
                                      apptainer exec base-runner.sif \
                                        ./fuzzer -max_total_time=...
                                                 │
                                                 ▼
                                      corpora + coverage data returned
```

Fuzzing itself needs no Docker — only the binary plus a matching runtime, which the
read-only `base-runner` `.sif` provides. Coverage rebuilds (`SANITIZER=coverage`) require
Docker and stay on the local server.

**Three HPRC-specific hazards:**

| Hazard | Handling |
|---|---|
| Job walltime caps (commonly 24–48 h) | 24 h sits at the edge. Use `-max_total_time=86000` for margin, or split into 2 × 12 h chained jobs |
| Compute nodes typically have no external network | Stage corpora, images, and binaries **in advance**; never fetch at runtime |
| Lustre/GPFS degrades badly under many small files | libFuzzer corpus directories **must** live on node-local `$TMPDIR`; archive back to shared storage at job end. Skipping this will degrade the shared filesystem for other users |

Verify ❓2.7 before scheduling anything at scale — it is the single point of failure for the
whole pipeline.

---

## 8. What each result is allowed to claim

| Evidence | Supports | Does NOT support |
|---|---|---|
| Derivation set (A) | The taxonomy; that its failure modes are real (maintainer-confirmed) | Any violation *rate*; any checker accuracy figure |
| Held-out set (B) | Violation rate; checker precision | Checker recall |
| Mutation injection | Checker recall, per sub-check | That the taxonomy is complete |
| Saturation curve | That the taxonomy is complete on the derivation sample | Completeness beyond C/C++ production harnesses |
| Blind annotation | Correctness comparison across the four systems; that correctness is reliably measurable (κ) | Objective ground truth |
| Coverage (primary) | That correctness gains cost no coverage | Harness correctness itself |
| Coverage (secondary) | Robustness of the ranking under seeds; the stronger parity claim | A neutral comparison — the corpus favours gold |
| RQ5 deployment | Real-world efficacy; FP rate | Superiority over baselines, until the OFG baseline is run |

---

## 9. Changelog

| Date | Change |
|---|---|
| 2026-08-18 | Initial draft. Locked §1.1–1.10; opened blocking questions §2.1–2.8. |
| 2026-08-21 | Frame built and benchmark drawn → `QuartetFuzz/dataset_v2/`. Resolved ❓2.3, ❓2.4; dropped ❓2.5's rule. Added exclusion `E5` (build not swappable) and two stratification dimensions (project size, API arity). Per-project cap set to 5, retaining 85 of the previous 100 cases; 15 newly drawn cases still need labels. |
