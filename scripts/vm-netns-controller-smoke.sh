#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODE="${MODE:-switch}"

case "${1:-}" in
  switch|fallback|recovery)
    MODE="$1"
    shift
    ;;
  --mode)
    if [ $# -lt 2 ]; then
      printf 'missing value for --mode\n' >&2
      exit 1
    fi
    MODE="$2"
    shift 2
    ;;
esac

YAML="${1:-samples/linux-vm-netns.yaml}"

if [ "$(id -u)" = "0" ]; then
  printf 'Run this script as a normal user with sudo available, not as root.\n' >&2
  exit 1
fi

cd "$ROOT_DIR"

apply_selected() {
  cat out/netns-runtime/selected-path.txt
  sh out/netns-runtime/apply-selected.sh
}

expect_selected() {
  expected_path="$1"
  expected_kind="$2"
  grep -q "^selected_path: $expected_path$" out/netns-runtime/selected-path.txt
  grep -q "^runtime_kind: $expected_kind$" out/netns-runtime/selected-path.txt
}

run_direct() {
  printf '== controller-selected direct path ==\n'
  sh scripts/vm-generate-netns-runtime.sh "$YAML"
  expect_selected path-direct direct
  apply_selected
}

run_forced_hub() {
  printf '\n== controller-forced hub path ==\n'
  sh scripts/vm-generate-netns-runtime.sh "$YAML" --path path-via-hub
  expect_selected path-via-hub hub
  apply_selected
}

run_fallback() {
  printf '\n== event: active direct path failed ==\n'
  sh scripts/vm-generate-netns-runtime.sh "$YAML" --active-path path-direct --fail-path path-direct
  expect_selected path-via-hub hub
  apply_selected
}

run_recovery() {
  printf '\n== event: direct recovered, priority returns to direct ==\n'
  sh scripts/vm-generate-netns-runtime.sh "$YAML" --active-path path-via-hub
  expect_selected path-direct direct
  apply_selected
}

case "$MODE" in
  switch)
    run_direct
    run_forced_hub
    printf '\nController switch smoke passed: YAML-selected direct and forced hub runtimes both passed.\n'
    ;;
  fallback)
    run_direct
    run_fallback
    printf '\nController fallback smoke passed: direct failure event selected and validated hub fallback.\n'
    ;;
  recovery)
    run_direct
    run_fallback
    run_recovery
    printf '\nController recovery smoke passed: direct -> hub fallback -> direct recovery all validated.\n'
    ;;
  *)
    printf 'Usage: %s [switch|fallback|recovery] [yaml]\n' "$0" >&2
    printf '   or: MODE=switch|fallback|recovery %s [yaml]\n' "$0" >&2
    exit 1
    ;;
esac
