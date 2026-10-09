# Gold Harness 基准数据集 —— 选择说明

**状态：** 2026-10-07 定稿草案。所有数字从 `QuartetFuzz/dataset_v2/benchmark_100.jsonl`、
`_private/frame_census.json.gz`、`_private/tools_all/draw_final.json`、
`_private/replacements.json` 和各 case 的 `report/` 重新计算得出，不取自旧文档。

**用途：** 论文数据集小节的底稿，以及 `dataset_v2/_private/FREEZE.md`、`GOLD.md`、
`README.md`、`criteria.html` 和 `experiment-protocol.md` 同步修订的依据。

**相关文件：** [`experiment-protocol.md`](experiment-protocol.md) · [`reviews.md`](reviews.md)

---

## 1. 论文用说法（中文）

**Gold harness 基准数据集。**
我们以 100 个人类编写的 OSS-Fuzz harness 作为评估基准。它们从一个明确定义的抽样框中抽出，
所有规则在四个系统生成任何 harness 之前固定。每条规则都只依据公开数据或项目自己的构建与
安装文件判定，从不依据我们的检查器或任何系统的输出，因此排除集对所有被比较的系统完全相同。
逐 target 的排除记录、带种子的抽样过程以及其后的每一次替换都随 artifact 公开。

*抽样框。* 我们枚举了 OSS-Fuzz 发布覆盖率数据的 537 个 C/C++ 项目的全部 6,478 个 fuzz
target。一个 target 进入抽样框需满足：(i) Google 对其项目的最近一次构建成功；(ii) 过去
90 天内在 ClusterFuzz 上运行过；(iii) 定义 `LLVMFuzzerTestOneInput` 且源码可获取；(iv) 至少
有一个库调用接收 fuzz 输入；(v) 项目的构建能够接受替换进来的 harness 而无需逐项目脚手架；
(vi) 在连续十天中至少有一天覆盖了至少一行；(vii) 官方语料可公开获取。满足全部条件的有 444
个 target，分属 127 个项目。另有两条输入侧条件再移除 28 个：上游仓库仍在维护，且官方语料
至少含 50 个种子。50 这一阈值落在分布的空档上，被移除的最大为 25，保留的最小为 97。语料可
获取这一条件使抽样框偏向成熟、长期被 fuzz 的项目，其 harness 更强，我们明确指出这一偏向方向。

*抽样。* 从 416 个合格 target 中，我们以公开种子做两阶段聚类抽样抽出 100 个：先抽项目，再在
每个项目中抽至多五个 target，并对三个 API 调用复杂度层设置下限（单次调用；2 到 4 次调用；
5 次以上调用或显式的创建、使用、销毁生命周期），以防几乎没有 API 协议面的单次调用解析器主导
基准。项目级聚类在所有统计中都被考虑。

*目标函数与公开 API。* 每个 case 记录开发者为完成该 harness 所测任务会调用的库函数，以及项目
自己的安装规则和声明该函数的头文件与行号。最初抽中的 target 中有 24 个只在项目不安装的头文件中
声明。由于每个系统都会因与 harness 质量无关的原因在这些 case 上失败，它们在同一抽样框和同一
规则下被替换。

*资格门。* 每个被选中的 harness 在官方 OSS-Fuzz runner 镜像中分别以 AddressSanitizer 和
覆盖率插桩构建，然后以 `-runs=0` 确定性地回放其全部官方语料。回放以 exit 0 完成、执行次数为
种子数的 0.9 到 1.6 倍、去重后故障数为零、且在项目自身源码范围内产生非零覆盖，即为合格。语料
由 ClusterFuzz 积累，按构造不含崩溃输入，因此回放时出现故障，意味着 harness 自身存在缺陷，或
库中存在 ClusterFuzz 已经暴露而项目尚未修复的 bug。无论哪种状态，该 harness 都不是一个处于
维护中的参照点。这是唯一一条需要执行 harness 才能判定的条件。它在任何系统运行之前对每个 case
同样适用，且不涉及覆盖率。有 23 个候选因此被替换。

*从不用于选择的条件。* 覆盖率高低、harness 在 fuzzing 中是否崩溃、以及按我们的检查器判断是否
满足 P1 到 P4。覆盖率是因变量，对其设阈值会使结论按构造成立。让我们的检查器为基线背书则是
循环论证。集合中最低的行覆盖率为 1.20%，11 个 case 低于 5%，全部保留并如实报告。

*版本。* 所有 case 固定在同一个 OSS-Fuzz 提交上，上游则按项目固定在 OSS-Fuzz 于 2026 年
8 月 21 日构建所用的修订版本，取自其公开的 `srcmap`。

*构成。* 100 个 harness，48 个项目，94 个不同的目标函数（yara 的五个 harness 共享
`yr_rules_scan_mem`）。复杂度分层：有状态 54 个，复合 27 个，单次调用 19 个。9 个 target 在
生成模型的训练截止日期之后才加入 OSS-Fuzz，构成污染检查的下界。18 个 case 来自我们此前的
100 例数据集，保留组与新抽组的结果分别报告。Gold harness 在自身语料上的行覆盖率从 1.20% 到
97.43%，中位数 23.91%；785,729 个种子回放零故障。

*披露。* 维护状态、语料规模和公开 API 三条条件是在首次抽样之后制定并应用于已持有 case 的。
三者都在输入侧测量，均不涉及因变量。一轮早期替换因台账错误被整体作废并如实记录。每个 case 的
prompt 仅由目标函数、函数声明和项目名生成；此前 18 条描述了 gold harness 实现的手写 prompt
已全部弃用。

---

## 2. 论文用说法（英文）

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
own sources. The corpus is accumulated by ClusterFuzz and by construction contains no crashing
input, so a fault on replay indicates either a defect in the harness itself or a library bug
that ClusterFuzz has already surfaced and the project has not fixed; in neither state is the
harness a maintained reference point. This is the only criterion decided by executing the
harness. It is applied identically to every case before any system runs and does not involve
coverage. Twenty-three candidates were replaced on this ground.

*Never used for selection.* Coverage level, whether the harness crashes under fuzzing, and
whether it satisfies P1–P4 as judged by our checker. Coverage is the dependent variable; a
threshold on it would make the result true by construction. Letting our checker certify the
baseline would be circular. The lowest line coverage in the set is 1.20% and eleven cases lie
below 5%; all are kept and reported.

*Versions.* All cases are pinned to one OSS-Fuzz commit and, per project, to the upstream
revision OSS-Fuzz built on 2026-08-21, taken from its published `srcmap`.

*Composition.* 100 harnesses, 48 projects, 94 distinct target functions (five yara harnesses
share `yr_rules_scan_mem`). Arity strata: 54 stateful, 27 composite, 19 single-call. Nine
targets entered OSS-Fuzz after the generator model's training cutoff and form a contamination
lower-bound check. Eighteen cases survive from our earlier 100-case set; results are reported
separately for retained and newly drawn cases. Line coverage of the gold harnesses on their own
corpora ranges from 1.20% to 97.43% (median 23.91%); the 785,729 seeds replay with zero faults.

*Disclosure.* The maintenance, corpus-size, and public-API conditions were formulated after the
first draw and applied to cases already held; each is measured on the input side and none
touches the dependent variable. One early replacement round was voided in full for a ledger
error and is recorded as such. Each case's prompt is generated from its target function,
declaration, and project name alone; eighteen earlier hand-written prompts that described the
gold harness were discarded.

---

## 3. 数字来源

| 数字 | 来源 |
|---|---|
| 6,478 targets / 537 projects | `frame_census.json.gz`，全部记录 |
| 444 / 127 | `frame_census.json.gz`，`exclusions` 为空的记录 |
| 九条框排除及计数 | `frame_census.json.gz` 的 `exclusions` 字段：E3 1,699 · E1 843 · E10 834 · E9_entry 737 · E9_no_source 715 · F 663 · E11 593 · E5 366 · E7 69 |
| E12 13 · E13 15 · 池 416 | `tools_all/draw_final.json` 的 `dropped` 与 `pool_size` |
| 抽样种子 20260829 | `tools_all/draw_final.json`（首次抽样种子 20260821 见 `selection_report.json`） |
| E14 24 个 | `replacements.json` 第 10 轮 |
| 资格门替换 23 个 | `draw_final.json` 的 `skipped_known_fail` 12 个 + `replacements.json` 第 10 轮 `rejected_by_the_qualification_gate` 11 个 |
| 100 / 48 / 94 / 54·27·19 / 9 / 18 | `benchmark_100_full.jsonl` 的 `project`、`target_function`、`stratum_arity`、`is_contamination_control`、`origin` |
| 覆盖率 1.20–97.43%，中位 23.91%，11 个 < 5% | 各 case `report/coverage.json` 的 `lines.percent` |
| 785,729 种子，100 个 exit 0，0 故障 | 各 case `report/replay.json` |
| OSS-Fuzz pin 955c6866 | `benchmark_100.jsonl` 的 `version_pins.oss_fuzz_revision` |

---

## 4. 定稿前必须核实

1. **"ClusterFuzz 已经暴露"这句话需要证据。** 在 OSS-Fuzz 的 issue tracker 上为 draco
   （4 个）和 quickjs（2 个）各找一条对应的未关闭 crash 报告。找到即保留原文；找不到，改为
   "库中存在在项目已 fuzz 过的输入上触发的 bug"（英文：*a library bug present on inputs the
   project has already fuzzed*）。
2. **资格门替换数的口径。** 23 = 已选 case 中被删的 12 + 第 10 轮候选中被拒的 11。若将第 9 轮
   的 unicorn 与自查的 libucl 也计入，则为 25。论文与 `replacements.json` 必须用同一口径。

---

## 5. 需要同步修订的文档

| 文件 | 现状 | 改为 |
|---|---|---|
| `_private/replacements.json` | 缺重抽时 skipped_known_fail 的 12 个，缺第 11–13 轮 | 并入主台账 |
| `_private/criteria.html`、`GOLD.md`、`README.md` | "ASan 下崩溃从不排除" | 改为本文第 1 节"资格门"与"从不用于选择的条件"两段的表述 |
| `experiment-protocol.md` 🔒1.7 | "选择从不使用崩溃行为" | 改为"从不使用 fuzzing 中的崩溃行为；官方语料回放是唯一的执行侧条件" |
| `experiment-protocol.md` ❓2.4 | "已解决，15 个" | 9 个 |
| `_private/README.md` | 25 项目 / 保留 33 / 种子 20260821 | 48 / 18 / 20260829（20260821 为首次抽样种子） |
| `_private/selection_report.json` | 38 项目 / 保留 22 | 48 / 18 |
| `_private/FREEZE.md` | 47 项目；SHA256SUMS 待生成 | 48 项目；生成 SHA256SUMS，推公开仓库，取外部时间戳 |
| `_private/GOLD.md` | 95 个不同目标函数；上限 87.32% | 94；97.43% |
| `_private/TASKS.md` | "只替换 flatbuffers 一个" | 补充资格门政策的变更及日期 |

---

## 6. 变更记录

| 日期 | 变更 |
|---|---|
| 2026-10-07 | 初稿。从数据文件重算全部数字；确定资格门的论文表述；列出待核实项与文档修订清单。 |
