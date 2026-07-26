#!/usr/bin/env sh
set -eu

RUN_BASE="${RUN_BASE:-/run/eventnet-netns-ipsec-hub}"

for ns in site-a hub-1 site-b; do
  printf '\n== %s charon.log ==\n' "$ns"
  if [ -f "$RUN_BASE/$ns/charon.log" ]; then
    tail -n 160 "$RUN_BASE/$ns/charon.log"
  else
    printf 'missing: %s\n' "$RUN_BASE/$ns/charon.log"
  fi
done
