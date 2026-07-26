#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
YAML="${1:-samples/linux-vm-netns.yaml}"
MODE="${MODE:-direct}"

if [ "$(id -u)" != "0" ]; then
  printf 'Please run as root: sudo %s\n' "$0" >&2
  exit 1
fi

cd "$ROOT_DIR"

run_selected() {
  label="$1"
  shift

  printf '\n== integrated controller runtime: %s ==\n' "$label"
  sh scripts/vm-generate-netns-runtime.sh "$YAML" "$@"
  cat out/netns-runtime/selected-path.txt
  sh out/netns-runtime/apply-integrated.sh
}

case "$MODE" in
  direct)
    run_selected "controller-selected direct"
    ;;
  hub)
    run_selected "forced hub" --path path-via-hub
    ;;
  fallback)
    run_selected "direct failure fallback to hub" --active-path path-direct --fail-path path-direct
    ;;
  both)
    run_selected "controller-selected direct"
    run_selected "direct failure fallback to hub" --active-path path-direct --fail-path path-direct
    ;;
  *)
    printf 'unknown MODE: %s\n' "$MODE" >&2
    printf 'valid MODE values: direct, hub, fallback, both\n' >&2
    exit 1
    ;;
esac

printf '\nController integrated runtime smoke passed: MODE=%s\n' "$MODE"
