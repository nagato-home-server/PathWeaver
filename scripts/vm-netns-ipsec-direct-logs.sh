#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out/netns-ipsec-direct}"
RUN_BASE="${RUN_BASE:-/run/eventnet-netns-ipsec-direct}"

for ns in site-a site-b; do
  printf '\n== %s charon.log ==\n' "$ns"
  if [ -f "$RUN_BASE/$ns/charon.log" ]; then
    tail -n 120 "$RUN_BASE/$ns/charon.log"
  else
    printf 'missing: %s\n' "$RUN_BASE/$ns/charon.log"
  fi
done
