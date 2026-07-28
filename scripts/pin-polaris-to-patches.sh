#!/usr/bin/env bash
# pin-polaris-to-patches.sh — set polaris-stream src rev (+ hash) in patches flake package.
#
# Usage:
#   ./scripts/pin-polaris-to-patches.sh [REV]
#   REV defaults to HEAD of $POLARIS_SRC or ~/projects/polaris
#
# Env:
#   POLARIS_SRC     checkout of luxus/polaris
#   PATCHES_SRC     checkout of luxus/polaris-hdr-linux-patches (default ~/projects/polaris-hdr-linux-patches)
#   UPDATE_HASH=1   run nix-prefetch and rewrite hash (default 1)
#
# Does not commit or push. Prints next steps for luxusAi lock + nh os switch.
#
set -euo pipefail

POLARIS_SRC="${POLARIS_SRC:-$HOME/projects/polaris}"
PATCHES_SRC="${PATCHES_SRC:-$HOME/projects/polaris-hdr-linux-patches}"
PKG="$PATCHES_SRC/pkgs/polaris-stream/default.nix"
UPDATE_HASH="${UPDATE_HASH:-1}"

if [[ ! -f "$PKG" ]]; then
  echo "missing $PKG" >&2
  exit 1
fi
if [[ ! -d "$POLARIS_SRC/.git" ]]; then
  echo "missing polaris git at $POLARIS_SRC" >&2
  exit 1
fi

REV="${1:-$(git -C "$POLARIS_SRC" rev-parse HEAD)}"
SHORT=$(git -C "$POLARIS_SRC" rev-parse --short "$REV")
MSG=$(git -C "$POLARIS_SRC" log -1 --oneline "$REV")

echo "pin polaris-stream → $REV ($MSG)"

# rewrite rev = "..."
if grep -qE 'rev = "[0-9a-f]{40}"' "$PKG"; then
  sed -i -E "s/rev = \"[0-9a-f]{40}\"/rev = \"$REV\"/" "$PKG"
else
  echo "could not find rev = \"…\" in $PKG" >&2
  exit 1
fi

if [[ "$UPDATE_HASH" == "1" ]]; then
  echo "prefetching hash (fetchSubmodules)…"
  PREFETCH_JSON=$(mktemp)
  if nix-prefetch-git --url "https://github.com/luxus/polaris.git" --rev "$REV" --fetch-submodules >"$PREFETCH_JSON" 2>/tmp/pin-prefetch.err; then
    HASH=$(python3 -c '
import json
d=json.load(open("'"$PREFETCH_JSON"'"))
h=d.get("hash") or d.get("sha256") or ""
if h and not h.startswith("sha256-") and not h.startswith("sha256:"):
    # raw base32/base64 from older nix-prefetch-git
    h="sha256-"+h
print(h)
')
  else
    HASH=""
    echo "WARN: nix-prefetch-git failed; see /tmp/pin-prefetch.err" >&2
  fi
  rm -f "$PREFETCH_JSON"
  if [[ -z "$HASH" ]]; then
    echo "WARN: could not prefetch hash; run: nix build $PATCHES_SRC#polaris-stream and paste wanted hash" >&2
  else
    sed -i -E "s|hash = \"sha256-[^\"]+\"|hash = \"$HASH\"|" "$PKG"
    echo "hash = $HASH"
  fi
fi

# bump version date comment lightly
TODAY=$(date +%Y-%m-%d)
sed -i -E "s/version = \"0-unstable-[0-9-]+\"/version = \"0-unstable-$TODAY\"/" "$PKG" || true

echo
echo "Next:"
echo "  cd $PATCHES_SRC && git diff pkgs/polaris-stream/default.nix"
echo "  nix build $PATCHES_SRC#polaris-stream   # must be CUDA (~30MB polaris-0 binary)"
echo "  git -C $PATCHES_SRC commit -am 'chore: pin polaris-stream to $SHORT'"
echo "  git -C $PATCHES_SRC push"
echo "  cd ~/projects/luxusAi && nix flake update polaris-hdr-linux-patches && nh os switch"
echo "  ~/projects/polaris/scripts/solid-base-gate.sh"
