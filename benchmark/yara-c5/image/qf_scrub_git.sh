#!/bin/bash
# Remove the gold harness from git history (runs after stub.sh).
# stub.sh replaces the gold in the working tree only; a repository that still
# holds it in its history lets a system read it back (git show, git diff,
# git log -p).  Every git repository under /src whose history holds the gold --
# a commit touching a file with the gold's basename, or the gold's exact blob
# -- gets its history replaced by one commit of the current (stubbed) tree.
#  - Nested repositories whose git directory lives inside the repository being
#    replaced (submodules) are first made standalone, so they keep working.
#  - Tags that `git describe` would report (exact and nearest, annotated or
#    not) are recreated as annotated tags on the new commit, so version scripts
#    (git-version-gen etc.) still find a release name.  The origin URL is kept.
# Repositories without the gold are left untouched.
# usage: scrub_git.sh <gold basename> <gold blob sha1>
set -u
B=$1; H=$2
repos=$(find /src -name .git \( -type d -o -type f \) 2>/dev/null | sed 's#/\.git$##' | awk '{print length, $0}' | sort -rn | cut -d' ' -f2-)
n=0
for r in $repos; do
  hit=$(git -C "$r" log --all --format=%H -- "*/$B" "$B" 2>/dev/null | head -1)
  git -C "$r" cat-file -e "$H" 2>/dev/null && hit=blob
  [ -z "$hit" ] && continue
  gd=$(git -C "$r" rev-parse --absolute-git-dir)
  tags=$( { git -C "$r" tag --points-at HEAD; git -C "$r" describe --tags --abbrev=0 HEAD 2>/dev/null; } | sort -u)
  url=$(git -C "$r" remote get-url origin 2>/dev/null)
  # Make nested repositories whose git dir lives inside $gd standalone.
  find "$r" -mindepth 2 -name .git -type f 2>/dev/null | while read f; do
    s=$(dirname "$f"); sgd=$(git -C "$s" rev-parse --absolute-git-dir 2>/dev/null) || continue
    case "$sgd" in "$gd"/*) rm -f "$f"; cp -a "$sgd" "$s/.git"; git -C "$s" config --unset core.worktree 2>/dev/null;; esac
  done
  rm -rf "$r/.git"
  git -C "$r" init -q
  git -C "$r" -c advice.addEmbeddedRepo=false add -A 2>/dev/null
  GIT_AUTHOR_DATE=2000-01-01T00:00:00Z GIT_COMMITTER_DATE=2000-01-01T00:00:00Z \
    git -C "$r" -c user.name=qf -c user.email=qf@localhost commit -q -m "source tree" || { echo "qf-scrub: FAILED commit $r"; exit 1; }
  for t in $tags; do git -C "$r" -c user.name=qf -c user.email=qf@localhost tag -a "$t" -m "$t"; done
  [ -n "$url" ] && git -C "$r" remote add origin "$url"
  echo "qf-scrub: $r (history replaced; tags: $(echo $tags))"
  n=$((n+1))
done
# Verify: no repository under /src may still hold the gold.
for r in $(find /src -name .git \( -type d -o -type f \) 2>/dev/null | sed 's#/\.git$##'); do
  if git -C "$r" cat-file -e "$H" 2>/dev/null || [ -n "$(git -C "$r" log --all --format=%H -- "*/$B" "$B" 2>/dev/null | head -1)" ]; then
    # A commit touching the basename is fine only if it is our own commit of the stubbed tree.
    [ "$(git -C "$r" rev-list --all --count 2>/dev/null)" = 1 ] && ! git -C "$r" cat-file -e "$H" 2>/dev/null && continue
    echo "qf-scrub: FAILED, gold still in history of $r"; exit 1
  fi
done
echo "qf-scrub: $n repositories rewritten"
