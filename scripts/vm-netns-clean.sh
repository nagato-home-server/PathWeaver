#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

for ns in site-a site-b hub-1 relay-c; do
  ip netns del "$ns" 2>/dev/null || true
done

printf 'Removed eventnet test namespaces.\n'
