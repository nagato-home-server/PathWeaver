#!/usr/bin/env sh
set -eu

need() {
  if command -v "$1" >/dev/null 2>&1; then
    printf 'ok: %s\n' "$1"
  else
    printf 'missing: %s\n' "$1"
    missing=1
  fi
}

missing=0

need cmake
need cc
need ip
need ping
need unshare
need mount
need swanctl
need vppctl

if command -v systemctl >/dev/null 2>&1; then
  printf 'ok: systemctl\n'
else
  printf 'note: systemctl not found; service checks skipped\n'
fi

if [ "$missing" -ne 0 ]; then
  cat <<'MSG'

Install hints for Debian/Ubuntu:
  sudo apt-get update
  sudo apt-get install -y build-essential cmake iproute2 iputils-ping strongswan-swanctl strongswan-charon vpp vpp-plugin-core

Package names vary by distribution. If VPP packages are unavailable from the distro,
use FD.io VPP packages for your OS release.
MSG
  exit 1
fi

printf '\nAll required commands were found.\n'
