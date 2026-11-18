#!/bin/bash
# Check out every srcmap repository at its pinned revision.  Fails the image
# build if a repository that exists cannot be pinned.
set -u
while read -r path url rev; do
  [ -z "$path" ] && continue
  if [ ! -d "$path/.git" ] && [ ! -f "$path/.git" ]; then
    echo "qf-pin: $path absent at image build time (cloned later or not used); skipped"
    continue
  fi
  cd "$path" || exit 1
  if ! git cat-file -e "$rev^{commit}" 2>/dev/null; then
    git fetch -q --depth=1 origin "$rev" 2>/dev/null || git fetch -q --unshallow origin 2>/dev/null || git fetch -q origin 2>/dev/null
  fi
  git -c advice.detachedHead=false checkout -q -f "$rev" || { echo "qf-pin: FAILED $path $rev"; exit 1; }
  git submodule update -q --init --recursive 2>/dev/null || echo "qf-pin: submodule update failed in $path (left as is)"
  echo "qf-pin: $path -> $(git rev-parse HEAD)"
done < /qf/pins.txt
