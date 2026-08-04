# Ibuki

PathWeaver is an event-driven IPsec path controller written in C.

It reads YAML intents, selects a usable VPN path, and generates runtime plans for
strongSwan and VPP.  The current prototype can reproduce direct IPsec, hub
fallback, relay path selection, VPP forwarding, and integrated controller-driven
runtime switching in a single Linux VM.

## What Works Today

- YAML-based `Intent` / `Path` / `Tunnel` / VPP edge parsing
- Priority, fallback, and evaluated path selection
- Explain JSONL output for selected paths, excluded paths, and health inputs
- strongSwan `swanctl.conf` and apply script generation
- VPP route plan and VPP netns runtime generation
- Linux network namespace smoke tests for direct, hub, and relay paths
- Integrated IPsec + VPP runtime smoke driven by controller-generated plans
- Scenario harness for direct failure, hub fallback, recovery, and relay-best cases

## Quick Start

### Linux VM Demo

Use this first when running from the shared Linux VM folder.

```sh
cd controller
sh scripts/vm-shell-check.sh
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

One-shot demo:

```sh
sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
```

Full runtime demo with IPsec/VPP requires root and the runtime dependencies:

```sh
sudo RUN_RUNTIME=1 sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
```

### Windows Build

```powershell
cd controller
cmake -S . -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

## Basic Usage

Generate a controller-selected plan from YAML:

```sh
sh scripts/vm-generate-plan.sh samples/linux-vm-netns.yaml
```

Run scenario tests:

```sh
build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct \
  --expect path-via-hub
```

Generate netns runtime files for the selected path:

```sh
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml
```

## Repository Map

- `include/eventnet/` - public C headers and controller model definitions
- `src/` - controller core, YAML parser, adapters, renderers, transition logic
- `examples/` - CLI/demo entry points and scenario harness
- `tests/` - C tests for controller behavior
- `samples/` - sample YAML configs
- `scripts/` - Linux VM setup, build, smoke, IPsec, and VPP helpers
- `docs/` - implementation status, roadmap, worker guide, and security notes
- `daily/` - daily development notes

## Documentation

- `docs/mitou-submission-status.md` - current implementation status for MITOU submission
- `docs/future-implementation-map.md` - next implementation targets and file/function map
- `docs/worker-guide.md` - guide for other contributors, functions, files, and outputs
- `docs/scenario-vs-production.md` - scenario harness vs future production `eventnetd`
- `docs/security-audit-notes.md` - local security audit notes and priority fixes
- `docs/awesome-mitou-comparison.md` - comparison with `awesome-mitou`
- `scripts/README-linux-vm.md` - full Linux VM smoke-test procedure

## Current Scope

PathWeaver currently focuses on the controller layer:

- intent and path selection
- YAML route definition
- strongSwan/VPP command and runtime generation
- Linux VM reproducible testing
- event/scenario experimentation

GUI, FRRouting integration, full production daemonization, and advanced flow
preservation are planned for later stages.

## Status

This is an early prototype for research and demonstration.  It is not yet a
production-ready network controller.

Before production use, the project still needs stronger input validation, safer
command execution, secret handling improvements, CI, and a minimal `eventnetd`
entry point.
