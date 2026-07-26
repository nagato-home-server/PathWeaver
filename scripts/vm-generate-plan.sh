#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -n "${BUILD_DIR:-}" ]; then
  :
elif [ -x "$ROOT_DIR/build-linux/eventnet_yaml_demo" ]; then
  BUILD_DIR="$ROOT_DIR/build-linux"
else
  BUILD_DIR="$ROOT_DIR/build-linux-cc"
fi
YAML_FILE="${1:-$ROOT_DIR/samples/ipsec-routes.yaml}"
INTENT_ID="${INTENT_ID:-intent-a-b}"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out}"
CONF_FILE="$OUT_DIR/eventnet-swanctl.conf"
SCRIPT_FILE="$OUT_DIR/apply-eventnet.sh"

mkdir -p "$OUT_DIR"

if [ ! -x "$BUILD_DIR/eventnet_yaml_demo" ]; then
  printf 'eventnet_yaml_demo not found. Run scripts/vm-build.sh first.\n' >&2
  exit 1
fi

"$BUILD_DIR/eventnet_yaml_demo" \
  --dry-run \
  --intent "$INTENT_ID" \
  --conf "$CONF_FILE" \
  --emit-script "$SCRIPT_FILE" \
  "$YAML_FILE"

chmod +x "$SCRIPT_FILE" 2>/dev/null || true

printf '\nGenerated:\n'
printf '  %s\n' "$CONF_FILE"
printf '  %s\n' "$SCRIPT_FILE"
