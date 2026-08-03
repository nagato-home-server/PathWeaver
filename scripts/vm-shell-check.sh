#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

failed=0

for script in "$ROOT_DIR"/scripts/*.sh; do
  [ -f "$script" ] || continue
  if sh -n "$script"; then
    printf 'ok: %s\n' "${script#$ROOT_DIR/}"
  else
    printf 'failed: %s\n' "${script#$ROOT_DIR/}" >&2
    failed=1
  fi
done

exit "$failed"
