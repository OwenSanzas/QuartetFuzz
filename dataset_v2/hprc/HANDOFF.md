# HPRC 侧任务：门二 —— 100 个 gold harness 的 24 小时空语料运行

你在 FASTER（或 Grace）登录节点上。目标：每个 gold harness 单核跑 24 小时空语料 fuzzing，
出现 crash/leak 即不合格并提前结束；oom/timeout 重启继续。结果同步回 VM。

## 0. 这次运行的定义（已写进协议，不要改）
- 镜像：gcr.io/oss-fuzz-base/base-runner@sha256:d2a23fde396b83aaa3d34dbfc0b1d73f0a2b114a90014ba07b32f9e723ed950b
- 参数：-rss_limit_mb=2560 -timeout=25，ASAN_OPTIONS 用镜像默认（detect_leaks=1），
  asan_overrides.json 里的 case 追加其项目自己的 [asan] 设置。
- 空语料、无字典、无 .options 的 libfuzzer 段。一个试验一个核，24 小时墙钟。
- 合格 = 24 小时内无 crash-/leak- artifact。oom-/timeout-/slow-unit 不算故障。

## 1. 从 VM 拉取材料（HPRC → VM 方向不需要 VPN）
    VM=ze@34.78.47.192      # 先把本机 ~/.ssh/id_ed25519.pub 加进 VM 的 ~/.ssh/authorized_keys
    mkdir -p $SCRATCH/qf-gate2 && cd $SCRATCH/qf-gate2
    rsync -avz --progress $VM:/home/ze/agf-fuzz-runs/hprc_bundle/ .
    sha256sum -c SHA256SUMS

bundle 内容：bins/<project>/<fuzzer>/<fuzzer>_asan（100 个 ASan 二进制 + llvm-symbolizer），
cases.txt，asan_overrides.json，fuzz_trial_apptainer.sh，gate2.slurm，本文件。

## 2. 镜像转 .sif（登录节点可出网）
    export APPTAINER_CACHEDIR=$SCRATCH/.apptainer APPTAINER_TMPDIR=$SCRATCH/.apptainer-tmp
    mkdir -p $APPTAINER_CACHEDIR $APPTAINER_TMPDIR
    apptainer pull base-runner.sif docker://gcr.io/oss-fuzz-base/base-runner@sha256:d2a23fde396b83aaa3d34dbfc0b1d73f0a2b114a90014ba07b32f9e723ed950b

## 3. 协议 2.7 单点验证（必须先做）
    MODE=full ./fuzz_trial_apptainer.sh base-runner.sif bins/zlib/zlib_uncompress2_fuzzer zlib_uncompress2_fuzzer /tmp/qf-smoke 0.05 1 ""
期望：results 里 fuzz.001.log 有 "INFO: Seed" 和 "DONE cov:" 行，exec/s 在 1e5 量级，无 GLIBC 报错。
失败常见原因：glibc 版本（镜像是 Ubuntu 24.04，二进制需要 2.38+，容器内应满足）、
--containall 下 /tmp 不可写（脚本只写绑定目录，应无问题）、apptainer 版本过旧。

## 4. 提交
    mkdir -p logs results
    sed -i 's/CHANGE_ME/<你的 allocation>/' gate2.slurm     # sinfo / myproject 查
    sbatch --array=0-99 gate2.slurm
每个作业 1 核 4 GB 25.5 小时；100 个合计 2,500 SU 以内。语料放节点本地 $TMPDIR，结束时拷回。

## 5. 结束后同步回 VM
    rsync -avz results/gate2/ $VM:/home/ze/agf-fuzz-runs/hprc_gate2/
    python3 - <<'PY'
    import glob,json
    for m in sorted(glob.glob('results/gate2/*/*/trial_01/meta.json')):
        d=json.load(open(m)); print(('PASS' if d.get('gate2_pass') else 'FAIL'), m.split('/')[2]+'/'+m.split('/')[3], d.get('stop_reason'), d.get('artifact_kinds'))
    PY

## 6. 不要做的事
- 不要改 fuzz_trial_apptainer.sh 里的 libFuzzer 参数或镜像 digest。
- 不要给任何 case 加字典、种子语料或 max_len。
- 不要在登录节点上跑 fuzzer（第 3 步的 3 分钟 smoke 除外，若登录节点限制 CPU 时间，用 srun 到计算节点跑）。
