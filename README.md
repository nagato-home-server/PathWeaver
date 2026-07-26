# EventNet Controller C

VPP / strongSwan 連携を見据えた、第一段階 Controller の C 実装です。

## Scope

- Intent / Path / Segment / Tunnel のモデル
- YAML による IPsec route / Intent 定義
- YAML の `tunnels` から StrongSwan adapter へ渡す desired tunnel
- Desired / Observed / Applied / Transition / Health / Error state
- Explicit / Priority / Evaluated の基本 Path Selection
- Immediate / Graceful Transition
- Make Before Break 的な target path preparation
- rollback
- Explain / Audit log
- StrongSwan / VPP / Health Probe の mock adapter
- StrongSwan / VPP / Health Probe の command adapter 接合部

FRRouting、Flow Preserve、GUI は後段です。

## Build

```powershell
cd C:\Users\kazut\Downloads\mitou\controller-c
cmake -S . -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

## Demo

```powershell
.\build\Debug\eventnet_demo.exe
```

または single-config generator の場合:

```powershell
.\build\eventnet_demo.exe
```

## Scenario Harness

`eventnet_scenario` は、本番daemon化前にpath selection / fallback / evaluated policyを実験するためのCLIです。

```sh
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

例:

```sh
build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct \
  --expect path-via-hub
```

## YAML Route Config

`samples/ipsec-routes.yaml` で Tunnel、Path、Intent を定義できます。

```yaml
tunnels:
  - id: tun-a-b
    local_node: site-a
    remote_node: site-b
    local_endpoint: 203.0.113.10
    remote_endpoint: 203.0.113.20
    local_ts: 10.0.1.0/24
    remote_ts: 10.0.2.0/24

paths:
  - id: path-direct
    source: site-a
    destination: site-b
    segments:
      - id: seg-a-b
        from: site-a
        to: site-b
        tunnel_id: tun-a-b

intents:
  - id: intent-a-b
    traffic:
      source: site-a
      destination: site-b
    path_selection:
      mode: priority
      candidates:
        - path-direct
    transition:
      strategy: immediate
```

```powershell
.\build\Debug\eventnet_yaml_demo.exe samples\ipsec-routes.yaml
```

実行時はデフォルトで dry-run として、YAML から選択された Path に対する `swanctl` / `vppctl` コマンドを表示します。

```text
[dry-run] swanctl --initiate --child tun-a-b
[dry-run] vppctl ip route add 10.0.2.0/24 via 203.0.113.20
```

特定 Intent だけを plan 化する場合:

```powershell
.\build\Debug\eventnet_yaml_demo.exe --intent intent-a-b --conf out-swanctl.conf --emit-script apply-eventnet.sh samples\ipsec-routes.yaml
```

実適用する場合:

```powershell
.\build\Debug\eventnet_yaml_demo.exe --apply --intent intent-a-b --conf /tmp/eventnet-swanctl.conf samples/ipsec-routes.yaml
```

Linux 実機では `swanctl`、`vppctl`、root 権限、VPP 側の next-hop 到達性が必要です。VPP route は FD.io VPP CLI の `ip route add <prefix> via <next-hop>` 形式で生成します。

## Adapter Bridge

`include/eventnet/command_adapters.h` は実 OSS 接続前の薄い接合部です。固定テンプレートを渡さない場合、`include/eventnet/render_commands.h` の renderer が標準コマンドを生成します。

- StrongSwan: `ensure_tunnel_command` / `remove_tunnel_command`
- VPP: `install_path_command`
- Health Probe: `validate_path_command`

テンプレートでは `{tunnel_id}`、`{local_endpoint}`、`{remote_endpoint}`、`{local_ts}`、`{remote_ts}`、`{traffic_key}`、`{path_id}` などを展開できます。

## Linux VM Scripts

Linux VM の共有フォルダで試す場合は、`scripts/README-linux-vm.md` の順序で実行してください。

他作業者向けのファイル構成、主要関数、生成出力の説明は `docs/worker-guide.md` を参照してください。

scenario harness と本番 `eventnetd` の差分は `docs/scenario-vs-production.md` を参照してください。
