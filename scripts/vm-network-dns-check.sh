#!/usr/bin/env sh
set -eu

host="${1:-packagecloud.io}"

printf '== resolver config ==\n'
cat /etc/resolv.conf 2>/dev/null || true

printf '\n== default route ==\n'
ip route show default 2>/dev/null || true

printf '\n== DNS lookup: %s ==\n' "$host"
if command -v getent >/dev/null 2>&1; then
  getent hosts "$host" || true
else
  printf 'missing: getent\n'
fi

if command -v resolvectl >/dev/null 2>&1; then
  printf '\n== resolvectl query: %s ==\n' "$host"
  resolvectl query "$host" 2>/dev/null || true
fi

printf '\n== raw IP connectivity ==\n'
if command -v ping >/dev/null 2>&1; then
  ping -c 1 -W 2 1.1.1.1 || true
else
  printf 'missing: ping\n'
fi

printf '\n== HTTPS by hostname ==\n'
if command -v curl >/dev/null 2>&1; then
  curl -I --connect-timeout 5 "https://$host" 2>&1 || true
else
  printf 'missing: curl\n'
fi

cat <<'MSG'

Interpretation:
  - If ping 1.1.1.1 fails, the VM has no working outbound network path.
  - If ping 1.1.1.1 works but DNS lookup fails, fix DNS/resolv.conf/NetworkManager.
  - If DNS works but HTTPS fails, check proxy/firewall/TLS interception.
MSG
