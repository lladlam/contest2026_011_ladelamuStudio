#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
WORKSPACE=$(realpath "${1:-$REPO_ROOT/..}")
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
export PATH="$REPO_ROOT/scripts/tools:$PATH"

VERSION=$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: invalid four-part version: $VERSION" >&2
  exit 1
fi

if ! grep -q "\"version\": \"$VERSION\"" \
  "$REPO_ROOT/board/d13x-hengshan-pi/pack/image_cfg.json"; then
  echo "error: VERSION and image_cfg.json do not match" >&2
  exit 1
fi

if ! grep -q "version = \"$VERSION\";" \
  "$REPO_ROOT/board/d13x-hengshan-pi/pack/d13x_os.its"; then
  echo "error: VERSION and d13x_os.its do not match" >&2
  exit 1
fi

"$SCRIPT_DIR/integrate.sh" "$WORKSPACE"

cd "$WORKSPACE/nuttx"
export APPSDIR="$WORKSPACE/apps"
export APPSBINDIR="$WORKSPACE/apps"
export BINDIR="$WORKSPACE/nuttx"
./tools/configure.sh \
  ../vendor/artinchip/boards/d13x-hengshan-pi/configs/nsh
make -j"$JOBS"

cd "$WORKSPACE/vendor/artinchip/pack"
./pack.sh hengshan-pi d13x

IMAGE="$WORKSPACE/vendor/artinchip/pack/prebuilt/d13x_hengshan-pi_v$VERSION.img"
test -s "$IMAGE"
sha256sum "$WORKSPACE/nuttx/nuttx" "$IMAGE"
