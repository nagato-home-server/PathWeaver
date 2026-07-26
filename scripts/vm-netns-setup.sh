#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

cleanup_link() {
  ip link del "$1" 2>/dev/null || true
}

create_ns() {
  ns="$1"
  ip netns add "$ns" 2>/dev/null || true
  ip netns exec "$ns" ip link set lo up
}

create_veth_pair() {
  left_ns="$1"
  left_if="$2"
  left_ip="$3"
  right_ns="$4"
  right_if="$5"
  right_ip="$6"

  cleanup_link "$left_if"
  ip link add "$left_if" type veth peer name "$right_if"
  ip link set "$left_if" netns "$left_ns"
  ip link set "$right_if" netns "$right_ns"
  ip netns exec "$left_ns" ip addr add "$left_ip" dev "$left_if"
  ip netns exec "$right_ns" ip addr add "$right_ip" dev "$right_if"
  ip netns exec "$left_ns" ip link set "$left_if" up
  ip netns exec "$right_ns" ip link set "$right_if" up
}

for ns in site-a site-b hub-1 relay-c; do
  create_ns "$ns"
done

create_veth_pair site-a a-direct 203.0.113.10/30 site-b b-direct 203.0.113.9/30
create_veth_pair site-a a-hub 203.0.113.14/30 hub-1 hub-a 203.0.113.13/30
create_veth_pair hub-1 hub-b 203.0.113.17/30 site-b b-hub 203.0.113.18/30
create_veth_pair site-a a-relay 203.0.113.22/30 relay-c relay-a 203.0.113.21/30
create_veth_pair relay-c relay-b 203.0.113.25/30 site-b b-relay 203.0.113.26/30

ip netns exec hub-1 sysctl -w net.ipv4.ip_forward=1 >/dev/null
ip netns exec relay-c sysctl -w net.ipv4.ip_forward=1 >/dev/null

cat <<'MSG'
Created namespaces:
  site-a
  site-b
  hub-1
  relay-c

Basic checks:
  sudo ip netns exec site-a ping -c 1 203.0.113.9
  sudo ip netns exec site-a ping -c 1 203.0.113.13
  sudo ip netns exec site-b ping -c 1 203.0.113.17

Note:
  This sets up L3 namespace links only. Running strongSwan and VPP inside
  namespaces needs per-namespace daemon/process wiring, which is the next step.
MSG
