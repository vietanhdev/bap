#!/usr/bin/env bash
# Recreate vendor/xiaozhi-esp32: clone upstream at the pinned commit (if missing) and apply
# firmware/patches/xiaozhi-esp32.patch. Safe to re-run: an already-applied patch is skipped.
#
#   firmware/scripts/apply_patch.sh
#
# Env: XIAOZHI_REPO, XIAOZHI_COMMIT to override the upstream source.
# Our own sources (firmware/src) are NOT part of the patch; build.sh copies them in at build time.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VENDOR="$ROOT/vendor/xiaozhi-esp32"
PATCH="$ROOT/firmware/patches/xiaozhi-esp32.patch"
REPO="${XIAOZHI_REPO:-https://github.com/78/xiaozhi-esp32.git}"
PINNED_COMMIT="${XIAOZHI_COMMIT:-aaf1eb7798cabee87a0c6a0649aabf4d17e9e383}"

[[ -f "$PATCH" ]] || { echo "error: $PATCH not found (run export_patch.sh first)" >&2; exit 1; }

if [[ ! -d "$VENDOR/.git" ]]; then
    echo "Cloning $REPO @ $PINNED_COMMIT into $VENDOR"
    mkdir -p "$VENDOR"
    git -C "$VENDOR" init -q
    git -C "$VENDOR" remote add origin "$REPO"
    # Shallow fetch of exactly the pinned commit (GitHub allows fetching reachable SHAs).
    if ! git -C "$VENDOR" fetch -q --depth 1 origin "$PINNED_COMMIT"; then
        echo "Shallow fetch by SHA failed, falling back to a full fetch" >&2
        git -C "$VENDOR" fetch -q origin
    fi
    git -C "$VENDOR" -c advice.detachedHead=false checkout -q "$PINNED_COMMIT"
fi

head_commit="$(git -C "$VENDOR" rev-parse HEAD)"
if [[ "$head_commit" != "$PINNED_COMMIT" ]]; then
    echo "warning: vendor HEAD is $head_commit, expected $PINNED_COMMIT" >&2
fi

if git -C "$VENDOR" apply --reverse --check "$PATCH" 2>/dev/null; then
    echo "Patch already applied, nothing to do"
    exit 0
fi

git -C "$VENDOR" apply --whitespace=nowarn "$PATCH"
echo "Applied $(grep -c '^diff --git ' "$PATCH") file patches to $VENDOR"
