#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

apt-get update
apt-get install -y \
  build-essential \
  cmake \
  iproute2 \
  iputils-ping \
  tcpdump \
  strongswan \
  strongswan-swanctl \
  strongswan-charon

if apt-cache show vpp >/dev/null 2>&1; then
  apt-get install -y vpp vpp-plugin-core || true
else
  cat <<'MSG'

VPP packages were not found in the current apt repositories.
Continue with strongSwan / namespace tests first, then install VPP from FD.io
packages for your distribution.
MSG
fi

printf '\nDependency installation attempt completed.\n'
