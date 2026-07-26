#!/usr/bin/env sh
set -eu

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

for ns in site-a site-b; do
  ip netns exec "$ns" true >/dev/null 2>&1 || {
    printf 'namespace missing: %s. Run sudo sh scripts/vm-netns-setup.sh first.\n' "$ns" >&2
    exit 1
  }
done

printf '== site-a -> local VPP interface ==\n'
ip netns exec site-a ping -c 2 -I 172.16.1.2 172.16.1.1

printf '\n== site-b -> local VPP interface ==\n'
ip netns exec site-b ping -c 2 -I 172.16.2.2 172.16.2.1

printf '\n== site-a -> site-b via VPP ==\n'
ip netns exec site-a ping -c 3 -I 172.16.1.2 172.16.2.2

printf '\n== site-b -> site-a via VPP ==\n'
ip netns exec site-b ping -c 3 -I 172.16.2.2 172.16.1.2

printf '\nVPP netns smoke passed: site-a and site-b reached each other through VPP host interfaces.\n'
