#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-linux-cc}"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--std=c11 -Wall -Wextra -Wpedantic -g}"

mkdir -p "$BUILD_DIR"

COMMON_SRCS="
src/apply_plan.c
src/audit.c
src/command_adapters.c
src/controller.c
src/health_probe.c
src/path_selection.c
src/render_commands.c
src/state.c
src/strongswan_adapter_mock.c
src/topology.c
src/transition.c
src/vpp_adapter_mock.c
src/yaml_config.c
"

cd "$ROOT_DIR"

$CC $CFLAGS -Iinclude $COMMON_SRCS examples/yaml_demo.c -o "$BUILD_DIR/eventnet_yaml_demo"
$CC $CFLAGS -Iinclude $COMMON_SRCS examples/netns_plan.c -o "$BUILD_DIR/eventnet_netns_plan"
$CC $CFLAGS -Iinclude $COMMON_SRCS examples/eventnet_scenario.c -o "$BUILD_DIR/eventnet_scenario"
$CC $CFLAGS -Iinclude $COMMON_SRCS examples/demo.c -o "$BUILD_DIR/eventnet_demo"
$CC $CFLAGS -Iinclude $COMMON_SRCS tests/test_controller.c -o "$BUILD_DIR/eventnet_tests"

"$BUILD_DIR/eventnet_tests"

printf '\nBuilt without CMake: %s\n' "$BUILD_DIR"
