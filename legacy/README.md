# Legacy data — do not use

Superseded by the final 100-case dataset in [`../dataset_v2/`](../dataset_v2/)
(`benchmark_100.jsonl`, frozen in commit 251d983). Nothing in the code reads
these directories; they are kept only for the record.

- `dataset_v1/` — the earlier 100-case set (18 cases overlap with the final set).
- `subset_25_v1/` — the 25-case reproduction subset drawn from `dataset_v1`.
- `scripts_v1/` — reproduction scripts written for `dataset_v1` / `subset_25_v1`
  (they read the v1 `prompt` records and will not run on `dataset_v2`).
