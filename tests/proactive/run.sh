#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TMPDIR:-/tmp}/home-panel-proactive-test

cc -std=c11 -Wall -Wextra -Werror \
  -I"$root/app/home_panel" \
  "$root/app/home_panel/home_panel_proactive.c" \
  "$root/tests/proactive/test_proactive.c" \
  -o "$out"
"$out"
