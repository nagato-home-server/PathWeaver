#!/usr/bin/env sh
set -eu

printf '== /etc/strongswan.conf ==\n'
if [ -f /etc/strongswan.conf ]; then
  sed -n '1,160p' /etc/strongswan.conf
else
  printf 'missing: /etc/strongswan.conf\n'
fi

printf '\n== /etc/strongswan.d ==\n'
find /etc/strongswan.d -maxdepth 3 -type f 2>/dev/null | sort | sed -n '1,80p' || true
