#!/usr/bin/env sh
set -eu

printf '== OS release ==\n'
cat /etc/os-release 2>/dev/null || true

printf '\n== kernel ==\n'
uname -a

printf '\n== apt package policy ==\n'
if command -v apt-cache >/dev/null 2>&1; then
  apt-cache policy vpp vpp-plugin-core 2>/dev/null || true
else
  printf 'missing: apt-cache\n'
fi

printf '\n== configured apt sources mentioning fd.io/packagecloud/vpp ==\n'
grep -R -n -E 'fd.io|packagecloud|vpp' \
  /etc/apt/sources.list /etc/apt/sources.list.d 2>/dev/null || true

printf '\n== installed VPP-related packages ==\n'
if command -v dpkg-query >/dev/null 2>&1; then
  dpkg-query -W 'vpp*' 2>/dev/null || true
else
  printf 'missing: dpkg-query\n'
fi

printf '\n== commands ==\n'
for cmd in curl gpg sudo vpp vppctl; do
  if command -v "$cmd" >/dev/null 2>&1; then
    printf 'ok: %s -> %s\n' "$cmd" "$(command -v "$cmd")"
  else
    printf 'missing: %s\n' "$cmd"
  fi
done
