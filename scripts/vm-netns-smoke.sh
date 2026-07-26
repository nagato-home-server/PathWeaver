#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

ensure_dummy_lan() {
  ns="$1"
  addr="$2"

  ip netns exec "$ns" ip link show lan0 >/dev/null 2>&1 || \
    ip netns exec "$ns" ip link add lan0 type dummy
  ip netns exec "$ns" ip addr flush dev lan0
  ip netns exec "$ns" ip addr add "$addr" dev lan0
  ip netns exec "$ns" ip link set lan0 up
}

replace_route() {
  ns="$1"
  prefix="$2"
  via="$3"

  ip netns exec "$ns" ip route replace "$prefix" via "$via"
}

delete_route() {
  ns="$1"
  prefix="$2"

  ip netns exec "$ns" ip route del "$prefix" 2>/dev/null || true
}

for ns in site-a site-b hub-1 relay-c; do
  ip netns exec "$ns" true >/dev/null 2>&1 || {
    printf 'namespace missing: %s. Run sudo sh scripts/vm-netns-setup.sh first.\n' "$ns" >&2
    exit 1
  }
done

ensure_dummy_lan site-a 10.10.1.1/24
ensure_dummy_lan site-b 10.10.2.1/24

printf '== direct path ==\n'
replace_route site-a 10.10.2.0/24 203.0.113.9
replace_route site-b 10.10.1.0/24 203.0.113.10
ip netns exec site-a ping -c 2 -I 10.10.1.1 10.10.2.1

printf '\n== hub path ==\n'
replace_route site-a 10.10.2.0/24 203.0.113.13
replace_route site-b 10.10.1.0/24 203.0.113.17
ip netns exec hub-1 ip route replace 10.10.1.0/24 via 203.0.113.14
ip netns exec hub-1 ip route replace 10.10.2.0/24 via 203.0.113.18
ip netns exec site-a ping -c 2 -I 10.10.1.1 10.10.2.1

printf '\n== relay path ==\n'
replace_route site-a 10.10.2.0/24 203.0.113.21
replace_route site-b 10.10.1.0/24 203.0.113.25
ip netns exec relay-c ip route replace 10.10.1.0/24 via 203.0.113.22
ip netns exec relay-c ip route replace 10.10.2.0/24 via 203.0.113.26
ip netns exec site-a ping -c 2 -I 10.10.1.1 10.10.2.1

printf '\nSmoke test passed: direct, hub, and relay L3 paths are reachable.\n'

delete_route site-a 10.10.2.0/24
delete_route site-b 10.10.1.0/24
