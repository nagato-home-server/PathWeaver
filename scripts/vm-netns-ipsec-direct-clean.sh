#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

for ns in site-a site-b; do
  ip netns exec "$ns" ip xfrm state flush 2>/dev/null || true
  ip netns exec "$ns" ip xfrm policy flush 2>/dev/null || true
done

printf 'Flushed xfrm state and policy in site-a/site-b.\n'
