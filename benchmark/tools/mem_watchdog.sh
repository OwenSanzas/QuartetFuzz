#!/usr/bin/env bash
# Watch memory while a build runs and stop it before the machine is starved.
#
# The pre-flight guard only sees the moment before a case starts; a build's peak
# arrives minutes later, and other work on this machine grows in the meantime.
FLOOR_GB=${FLOOR_GB:-8}
while true; do
  avail=$(free -g | awk '/^Mem:/{print $7}')
  if [ "$avail" -lt "$FLOOR_GB" ]; then
    echo "$(date +%T) watchdog: ${avail}GB left, stopping build containers" >&2
    docker ps -q --filter "ancestor=gcr.io/oss-fuzz-base/base-builder" | xargs -r docker kill
    pkill -f onboard_batch.py
    exit 1
  fi
  read -t 10 </dev/null 2>/dev/null || true
done
