#!/usr/bin/env bash
# Refuse to start container work when the machine is already loaded.
#
# Checked before every case, not once per batch: what else runs on this machine
# changes while a batch is in flight, and the only crash this pipeline has
# actually caused came from starting heavy work without looking first.
MIN_AVAIL_GB=${MIN_AVAIL_GB:-24}
MIN_DISK_GB=${MIN_DISK_GB:-40}

avail=$(free -g | awk '/^Mem:/{print $7}')
disk=$(df -BG --output=avail / | tail -1 | tr -dc '0-9')
# Match the image by prefix, not by exact tag. A libpng replay running from the
# untagged gcr.io/oss-fuzz-base/base-runner slipped past an exact-tag filter for
# sixteen minutes: not our container, but its memory is just as real.
busy=$(docker ps --format '{{.Image}}' | grep -c '^gcr\.io/oss-fuzz-base/base-runner' || true)
mine=$(docker ps --format '{{.Image}} {{.Command}}' | grep '^gcr\.io/oss-fuzz-base/base-runner' | grep -c 'run_fuzzer\|/out/' || true)

[ "$avail" -lt "$MIN_AVAIL_GB" ] && {
  echo "HOLD: ${avail}GB available, need ${MIN_AVAIL_GB}GB" >&2
  ps -eo rss,args --sort=-rss | head -3 | tail -2 | \
    awk '{printf "      %.1fGB  %s\n", $1/1048576, substr($0, index($0,$2), 70)}' >&2
  exit 3; }
[ "$disk" -lt "$MIN_DISK_GB" ] && { echo "HOLD: ${disk}GB disk free, need ${MIN_DISK_GB}GB" >&2; exit 3; }
# Block on our own replay - two of ours stacked doubles peak memory for no gain.
# Someone else's runner is reported and counted against the memory floor, but not
# waited on: their libpng session may run for hours and this pipeline would stall.
[ "$mine" -gt 0 ] && { echo "HOLD: one of our replays is already running" >&2; exit 3; }
[ "$busy" -gt 0 ] && {
  echo "note: $busy foreign runner container(s) up, counted against the memory floor:" >&2
  docker ps --format '      {{.Image}}  {{.Status}}  {{.Command}}' | \
    grep 'gcr\.io/oss-fuzz-base/base-runner' >&2; }
echo "ok: ${avail}GB memory, ${disk}GB disk, no replay running"
