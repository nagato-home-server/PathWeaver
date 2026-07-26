#!/usr/bin/env sh
set -eu

printf 'swanctl: '
command -v swanctl || true

printf 'ipsec: '
command -v ipsec || true

printf 'charon candidates:\n'
for path in \
  /usr/lib/ipsec/charon \
  /usr/lib/strongswan/charon \
  /usr/libexec/ipsec/charon \
  /usr/sbin/charon \
  /usr/bin/charon
do
  if [ -x "$path" ]; then
    printf '  executable: %s\n' "$path"
  elif [ -e "$path" ]; then
    printf '  exists-not-executable: %s\n' "$path"
  fi
done

printf '\nstrongSwan directories:\n'
for dir in /etc/swanctl /etc/strongswan.d /usr/lib/ipsec /usr/lib/strongswan /var/run/charon /run/charon; do
  if [ -e "$dir" ]; then
    ls -ld "$dir"
  fi
done

printf '\ninstalled packages matching strongswan:\n'
if command -v dpkg >/dev/null 2>&1; then
  dpkg -l | grep -i strongswan || true
fi
