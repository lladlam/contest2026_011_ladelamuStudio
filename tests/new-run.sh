#!/bin/sh

set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: $0 <firmware.img> [artifact-root]" >&2
  exit 2
fi

image=$1
artifact_root=${2:-validation-artifacts}

if [ ! -f "$image" ]; then
  echo "Firmware image not found: $image" >&2
  exit 1
fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=$(cat "$repo/VERSION")
run_id="${version}-$(date '+%Y%m%d-%H%M%S')"
case "$artifact_root" in
  /*) output="$artifact_root/$version/$run_id" ;;
  *)  output="$repo/$artifact_root/$version/$run_id" ;;
esac

mkdir -p "$output"
cp "$repo/tests/RESULT_TEMPLATE.md" "$output/result.md"
sha256sum "$image" > "$output/image.sha256"

{
  printf 'run_id=%s\n' "$run_id"
  printf 'version=%s\n' "$version"
  printf 'image=%s\n' "$(realpath "$image")"
  printf 'git_commit=%s\n' "$(git -C "$repo" rev-parse HEAD)"
  printf 'git_branch=%s\n' "$(git -C "$repo" branch --show-current)"
  printf 'created_at=%s\n' "$(date --iso-8601=seconds)"
  printf '\n[git status --short]\n'
  git -C "$repo" status --short
} > "$output/source.txt"

printf 'Created validation run: %s\n' "$output"
printf 'Fill in: %s\n' "$output/result.md"
