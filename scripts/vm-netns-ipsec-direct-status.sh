#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out/netns-ipsec-direct}"
RUN_BASE="${RUN_BASE:-/run/eventnet-netns-ipsec-direct}"
SWANCTL_WORK_BASE="${SWANCTL_WORK_BASE:-/etc/swanctl/eventnet-netns-ipsec-direct}"

for ns in site-a site-b; do
  printf '\n== %s ==\n' "$ns"
  ip netns exec "$ns" ip addr show
  printf '\n-- xfrm state --\n'
  ip netns exec "$ns" ip xfrm state || true
  printf '\n-- xfrm policy --\n'
  ip netns exec "$ns" ip xfrm policy || true
  printf '\n-- swanctl sas --\n'
  if [ -s "$RUN_BASE/$ns/charon.pid" ]; then
    printf 'charon pid: %s\n' "$(cat "$RUN_BASE/$ns/charon.pid")"
  fi
  if [ -f "$RUN_BASE/$ns/swanctl.conf" ]; then
    printf 'runtime swanctl.conf: %s\n' "$RUN_BASE/$ns/swanctl.conf"
    ls -ld "$RUN_BASE" "$RUN_BASE/$ns" "$RUN_BASE/$ns/swanctl.conf"
  fi
  if [ -f "$SWANCTL_WORK_BASE/$ns/swanctl.conf" ]; then
    printf 'swanctl work config: %s\n' "$SWANCTL_WORK_BASE/$ns/swanctl.conf"
    ls -ld "$SWANCTL_WORK_BASE" "$SWANCTL_WORK_BASE/$ns" "$SWANCTL_WORK_BASE/$ns/swanctl.conf"
  fi
  printf '\n-- swanctl conns --\n'
  swanctl --list-conns --uri "unix://$RUN_BASE/$ns/charon.vici" 2>/dev/null || true
  printf '\n-- swanctl sas --\n'
  swanctl --list-sas --uri "unix://$RUN_BASE/$ns/charon.vici" 2>/dev/null || true
done
