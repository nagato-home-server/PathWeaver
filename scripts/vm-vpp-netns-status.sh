#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

printf '== Linux host links ==\n'
ip addr show vpp-site-a 2>/dev/null || true
ip addr show vpp-site-b 2>/dev/null || true

for ns in site-a site-b; do
  printf '\n== %s vpp-client ==\n' "$ns"
  ip netns exec "$ns" ip addr show vpp-client 2>/dev/null || true
  printf '\n== %s routes ==\n' "$ns"
  ip netns exec "$ns" ip route 2>/dev/null || true
done

printf '\n== VPP interfaces ==\n'
vppctl show interface

printf '\n== VPP interface addresses ==\n'
vppctl show interface address

printf '\n== VPP IPv4 FIB ==\n'
vppctl show ip fib
