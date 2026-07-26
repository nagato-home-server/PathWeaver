#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
YAML="${1:-samples/linux-vm-netns.yaml}"
RUN_RUNTIME="${RUN_RUNTIME:-0}"
RUN_FALLBACK="${RUN_FALLBACK:-1}"

cd "$ROOT_DIR"

section() {
  printf '\n============================================================\n'
  printf '%s\n' "$1"
  printf '============================================================\n'
}

section '1. Build C controller and tests'
sh scripts/vm-build-cc.sh

section '2. Scenario harness: selection, fallback, recovery, evaluated policy'
sh scripts/vm-eventnet-scenario-smoke.sh "$YAML"

section '3. Explain JSONL output'
printf 'Generated explain events:\n'
cat out/scenario/multistep-explain.jsonl

section '4. Generate controller runtime plan'
sh scripts/vm-generate-netns-runtime.sh "$YAML" --active-path path-direct --fail-path path-direct
printf '\nSelected path summary:\n'
cat out/netns-runtime/selected-path.txt
printf '\nGenerated integrated runtime:\n'
printf '  out/netns-runtime/apply-integrated.sh\n'

if [ "$RUN_RUNTIME" = "1" ]; then
  section '5. Run real integrated IPsec + VPP runtime'
  if [ "$(id -u)" != "0" ]; then
    printf 'RUN_RUNTIME=1 requires root. Re-run with sudo.\n' >&2
    exit 1
  fi
  sh scripts/vm-controller-integrated-runtime-smoke.sh "$YAML"
  if [ "$RUN_FALLBACK" = "1" ]; then
    MODE=fallback sh scripts/vm-controller-integrated-runtime-smoke.sh "$YAML"
  fi
else
  section '5. Runtime execution skipped'
  printf 'Set RUN_RUNTIME=1 and run with sudo to execute strongSwan + VPP runtime.\n'
  printf 'Example:\n'
  printf '  sudo RUN_RUNTIME=1 sh scripts/demo-mitou.sh %s\n' "$YAML"
fi

section 'Demo summary'
printf 'Prototype path completed:\n'
printf '  YAML -> controller selection -> scenario explain JSONL -> generated runtime plan\n'
if [ "$RUN_RUNTIME" = "1" ]; then
  printf '  + real strongSwan/VPP integrated runtime smoke\n'
fi
printf '\nDemo completed successfully.\n'
