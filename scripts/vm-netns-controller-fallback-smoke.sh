#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
YAML="${1:-samples/linux-vm-netns.yaml}"

if [ "$(id -u)" = "0" ]; then
  printf 'Run this script as a normal user with sudo available, not as root.\n' >&2
  exit 1
fi

cd "$ROOT_DIR"

printf '== initial controller-selected path ==\n'
sh scripts/vm-generate-netns-runtime.sh "$YAML"
cat out/netns-runtime/selected-path.txt
grep -q '^selected_path: path-direct$' out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh

printf '\n== event: active direct path failed ==\n'
sh scripts/vm-generate-netns-runtime.sh "$YAML" --active-path path-direct --fail-path path-direct
cat out/netns-runtime/selected-path.txt
grep -q '^selected_path: path-via-hub$' out/netns-runtime/selected-path.txt
grep -q '^runtime_kind: hub$' out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh

printf '\nController fallback smoke passed: direct failure event selected and validated hub fallback.\n'
