#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
YAML="${1:-samples/linux-vm-netns.yaml}"

cd "$ROOT_DIR"

printf '== VPP route plan: controller-selected default path ==\n'
sh scripts/vm-generate-netns-runtime.sh "$YAML"
cat out/netns-runtime/selected-path.txt
DRY_RUN=1 sh out/netns-runtime/vpp-route-plan.sh

printf '\n== VPP route plan: direct failure fallback hub path ==\n'
sh scripts/vm-generate-netns-runtime.sh "$YAML" --active-path path-direct --fail-path path-direct
cat out/netns-runtime/selected-path.txt
DRY_RUN=1 sh out/netns-runtime/vpp-route-plan.sh

printf '\nVPP route plan smoke passed: generated dry-run VPP commands for direct and hub fallback paths.\n'
