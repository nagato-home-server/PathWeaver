#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
YAML="${1:-samples/linux-vm-netns.yaml}"

if [ "$(id -u)" = "0" ]; then
  printf 'Run this script as a normal user with sudo available, not as root.\n' >&2
  exit 1
fi

cd "$ROOT_DIR"

printf '== controller-selected default path ==\n'
sh scripts/vm-generate-netns-runtime.sh "$YAML"
cat out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh

printf '\n== controller-forced hub fallback path ==\n'
sh scripts/vm-generate-netns-runtime.sh "$YAML" --path path-via-hub
cat out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh

printf '\nController switch smoke passed: YAML-selected direct and forced hub runtimes both passed.\n'
