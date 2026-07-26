#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

ip netns exec site-a ip route del 10.10.2.0/24 2>/dev/null || true
ip netns exec hub-1 ip route del 10.10.1.0/24 2>/dev/null || true
ip netns exec hub-1 ip route del 10.10.2.0/24 2>/dev/null || true
ip netns exec site-b ip route del 10.10.1.0/24 2>/dev/null || true

for ns in site-a hub-1 site-b; do
  ip netns exec "$ns" ip link del xfrm-a-hub 2>/dev/null || true
  ip netns exec "$ns" ip link del xfrm-hub-b 2>/dev/null || true
  ip netns exec "$ns" ip xfrm state flush 2>/dev/null || true
  ip netns exec "$ns" ip xfrm policy flush 2>/dev/null || true
done

printf 'Flushed hub XFRM state, policy, routes, and xfrm interfaces.\n'
