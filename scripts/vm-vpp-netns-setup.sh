#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

need_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'missing: %s\n' "$1" >&2
    exit 1
  fi
}

need_cmd ip
need_cmd vppctl

for ns in site-a site-b; do
  ip netns exec "$ns" true >/dev/null 2>&1 || {
    printf 'namespace missing: %s. Run sudo sh scripts/vm-netns-setup.sh first.\n' "$ns" >&2
    exit 1
  }
done

sh "$(dirname -- "$0")/vm-vpp-netns-clean.sh"

create_vpp_veth() {
  ns="$1"
  host_if="$2"
  ns_if="$3"
  ns_addr="$4"
  vpp_addr="$5"

  ip link add "$host_if" type veth peer name "$ns_if"
  ip link set "$ns_if" netns "$ns"
  ip link set "$host_if" up
  ip netns exec "$ns" ip addr flush dev "$ns_if"
  ip netns exec "$ns" ip addr add "$ns_addr" dev "$ns_if"
  ip netns exec "$ns" ip link set "$ns_if" up

  vppctl create host-interface name "$host_if" >/dev/null
  vppctl set interface state "host-$host_if" up
  vppctl set interface ip address "host-$host_if" "$vpp_addr"
}

create_vpp_veth site-a vpp-site-a vpp-client 172.16.1.2/30 172.16.1.1/30
create_vpp_veth site-b vpp-site-b vpp-client 172.16.2.2/30 172.16.2.1/30

ip netns exec site-a ip route replace 172.16.2.0/30 via 172.16.1.1
ip netns exec site-b ip route replace 172.16.1.0/30 via 172.16.2.1

cat <<'MSG'
Created VPP netns links:
  site-a:vpp-client 172.16.1.2/30 <-> VPP host-vpp-site-a 172.16.1.1/30
  site-b:vpp-client 172.16.2.2/30 <-> VPP host-vpp-site-b 172.16.2.1/30

Next:
  sudo sh scripts/vm-vpp-netns-status.sh
  sudo sh scripts/vm-vpp-netns-smoke.sh
MSG
