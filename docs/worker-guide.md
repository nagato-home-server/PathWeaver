# PathWeaver Worker Guide

この文書は、別作業者が現時点の実装を読むための引き継ぎ資料です。

目的は、`YAML -> Controller -> generated runtime -> strongSwan/VPP smoke` の流れを追えるようにすることです。

## 1. 現在の到達点

現在の実装は、未踏提出向けプロトタイプの第2段階です。

できていること:

- YAMLで `tunnels` / `paths` / `intents` / `vpp_edges` を定義する。
- C controllerがIntentからPathを選択する。
- direct / hub fallback のruntime scriptを生成する。
- strongSwanをLinux network namespace内で起動し、IPsec ESP counter増加を確認する。
- VPPをhost-interface経由でnamespaceへ接続し、controller生成routeを実適用する。
- `MODE=direct` / `MODE=fallback` の統合smokeがVMで成功済み。

まだできていないこと:

- daemonとして常駐し、自動でイベントを監視してreconcileすること。
- VICIイベント購読によるstrongSwan状態同期。
- VPP API/binary APIによる本格統合。
- 同一packetがIPsec pipelineとVPP forwarding pipelineを連続通過するgateway構成。
- FRRouting統合。
- GUI。

## 2. 主要ディレクトリ

```text
include/eventnet/     public headers
src/                  controller core implementation
examples/             CLI/demo entry points
samples/              YAML examples
scripts/              Linux VM setup/smoke scripts
tests/                C unit-ish tests
daily/                作業日誌
docs/                 作業者向け補助資料
```

## 3. 入力YAML

主に使うサンプル:

- `samples/linux-vm-netns.yaml`

主なtop-level key:

- `tunnels`: IPsec tunnel定義。
- `vpp_edges`: VPP host-interfaceとnamespace側next-hopの定義。
- `paths`: direct / hub / relayなどの候補経路。
- `intents`: どのtrafficにどのpath selection policyを適用するか。

例:

```yaml
path_selection:
  mode: priority
  candidates:
    - path-direct
    - path-via-relay-c
    - path-via-hub
fallback:
  enabled: true
  path_id: path-via-hub
```

この例では、通常は `path-direct` が選ばれ、direct障害時は `path-via-hub` がfallbackとして選ばれます。

## 4. C実装の入口

### YAML parser

- Header: `include/eventnet/yaml_config.h`
- Source: `src/yaml_config.c`

主要関数:

- `en_yaml_config_load_file`
  - YAMLファイルを読み、`en_yaml_config_t` に展開する。
  - 読み込み後に `yaml_normalize` と `en_yaml_config_validate` を呼ぶ。
- `en_yaml_config_validate`
  - tunnel/path/intent/vpp_edgeの最低限の整合性を検証する。
- `parse_line`
  - indentationベースの簡易YAML parser本体。
- `parse_vpp_edge_kv`
  - `vpp_edges:` の各fieldを `en_vpp_edge_t` に入れる。
- `yaml_normalize`
  - 未指定の `route_next_hop` や `vpp_interface` / `next_hop` を補完する。

### Data model

- Header: `include/eventnet/types.h`

重要なstruct:

- `en_tunnel_t`
  - IPsec tunnel定義。
- `en_segment_t`
  - Pathを構成する1区間。
- `en_path_t`
  - source/destination/waypoints/segments/priority/route情報。
- `en_intent_t`
  - traffic、path selection、transition、fallback policy。
- `en_vpp_edge_t`
  - `node_id` と VPP/namespace interface、VPP route next-hopの対応。
- `en_selection_result_t`
  - 選択結果と除外理由。
- `en_reconcile_result_t`
  - controller submit結果。

### Controller

- Header: `include/eventnet/controller.h`
- Source: `src/controller.c`

主要関数:

- `en_controller_create`
  - path配列とadapterを渡してcontrollerを作る。
- `en_controller_create_with_tunnels`
  - pathに加えてdesired tunnelも渡す。YAML runtimeではこちらを使う。
- `en_controller_submit_intent`
  - 現在の中心関数。
  - candidate health観測、path選択、transition実行、結果説明の生成を行う。
- `en_controller_applied_path`
  - traffic keyに対してcontrollerが適用済みと認識しているpathを返す。
- `en_controller_audit_events`
  - audit event配列を返す。

### Path selection

- Source: `src/path_selection.c`

主要関数:

- `en_select_path`
  - `EN_SELECT_EXPLICIT` / `EN_SELECT_PRIORITY` / `EN_SELECT_EVALUATED` を処理する。
- `exclusion_reason`
  - failed/unhealthy、RTT、packet loss、required/forbidden waypointで候補除外する。
- `compare_paths`
  - evaluated modeで比較順序に従ってpathを比較する。

現在の注意:

- priority selectionは「候補順に最初の利用可能path」を選ぶ。
- evaluatedは基本形で、安定化制御やヒステリシスは未実装。

### Transition

- Source: `src/transition.c`

主要関数:

- `en_transition_path`
  - prepare -> ready/validate -> commit -> confirm の骨格。
- `transition_prepare`
  - pathのsegmentに必要なtunnelをadapterへensureする。
- `transition_ready`
  - health probeでpathを検証する。
- `transition_commit`
  - VPP adapterへforwarding切替を依頼する。
  - gracefulの場合は短いpause/drainを挟む。
- `transition_confirm`
  - adapterがactive pathを報告できる場合、切替結果を確認する。
- `rollback`
  - commit/confirm失敗時に前pathへ戻す。

現在の注意:

- これはcontroller内部モデルの状態遷移であり、VM上の実IPsec/VPP runtimeは主に生成scriptが担っています。

### Command rendering / apply plan

- Headers:
  - `include/eventnet/render_commands.h`
  - `include/eventnet/apply_plan.h`
- Sources:
  - `src/render_commands.c`
  - `src/apply_plan.c`

主要関数:

- `en_render_swanctl_conf`
  - tunnel定義から `swanctl.conf` 断片を生成する。
- `en_render_swanctl_initiate`
  - `swanctl --initiate --child ...` を生成する。
- `en_render_swanctl_terminate`
  - `swanctl --terminate --child ...` を生成する。
- `en_render_vpp_route_replace`
  - `vppctl ip route add ... via ...` を生成する。
- `en_render_vpp_route_delete`
  - `vppctl ip route del ...` を生成する。
- `en_apply_plan_from_config`
  - YAML config + selected pathから、swanctl conf / apply commands / rollback commandsを組み立てる。

### Netns runtime generator

- Source: `examples/netns_plan.c`
- Binary: `build-linux-cc/eventnet_netns_plan`
- Wrapper: `scripts/vm-generate-netns-runtime.sh`

主要関数:

- `find_path`
  - path idから `en_path_t` を探す。
- `find_tunnel`
  - tunnel idから `en_tunnel_t` を探す。
- `find_intent`
  - intent idから `en_intent_t` を探す。未指定なら先頭intent。
- `find_vpp_edge`
  - node idから `en_vpp_edge_t` を探す。
- `runtime_kind`
  - 現在のVM runtimeが対応する `direct` / `hub` / `unsupported` を判定する。
- `write_apply_script`
  - IPsecのみの選択path適用scriptを生成する。
- `write_integrated_script`
  - IPsec smokeとVPP forwarding smokeを一つにまとめた `apply-integrated.sh` を生成する。
- `write_summary`
  - 選択pathの説明を `selected-path.txt` に出す。
- `write_vpp_route_plan`
  - 一般VPP向けroute planを生成する。
- `write_vpp_netns_route_plan`
  - Linux VM netns/VPP host-interface構成向けroute planを生成する。
- `set_failed_health`
  - CLIの `--fail-path` をmock health failureへ変換する。

CLI例:

```sh
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml --active-path path-direct --fail-path path-direct
```

## 5. 生成される出力

`scripts/vm-generate-netns-runtime.sh` を実行すると、`out/netns-runtime/` に以下が生成されます。

### `selected-path.txt`

選択されたpathの説明です。

主なfield:

- `selected_path`
  - controllerが選んだpath id。
- `runtime_kind`
  - VM runtime上の分類。現在は `direct` / `hub` / `unsupported`。
- `source` / `destination`
  - pathの始点/終点node。
- `route_destination_prefix`
  - destination側LAN prefix。
- `route_next_hop`
  - path定義上のnext-hop。
- `vpp_edges`
  - YAMLで読んだVPP edge mapping。
- `segments`
  - pathを構成するsegmentとtunnel情報。

### `apply-selected.sh`

選択pathのIPsec runtimeだけを起動してsmokeするscriptです。

- directなら:
  - `vm-netns-ipsec-direct-start.sh`
  - `vm-netns-ipsec-direct-smoke.sh`
- hubなら:
  - `vm-netns-ipsec-hub-start.sh`
  - `vm-netns-ipsec-hub-smoke.sh`

### `apply-integrated.sh`

現在のデモ用の主役です。

同じ生成plan内で次を連続実行します。

1. dummy LAN作成。
2. selected pathに応じたIPsec runtime起動。
3. IPsec smokeでESP counter増加確認。
4. VPP netns host-interface setup。
5. `vpp-netns-route-plan.sh` を `DRY_RUN=0` で適用。
6. VPP forwarding smoke。

成功時の代表出力:

```text
Integrated controller runtime passed: IPsec path and VPP forwarding were controlled from one generated plan.
```

注意:

- 現段階では「同じ生成planでIPsecとVPPを連続制御する」統合です。
- 同一packetがIPsec復号後にVPP forwardingへ入る本番pipelineは次段階です。

### `vpp-route-plan.sh`

一般的なVPP route planです。

出力例:

```text
[dry-run] vppctl ip route add 10.10.2.0/24 via 203.0.113.9
```

### `vpp-netns-route-plan.sh`

Linux VMのVPP host-interface構成向けroute planです。

`samples/linux-vm-netns.yaml` の `vpp_edges` からnext-hopを生成します。

出力例:

```text
[dry-run] vppctl ip route add 10.10.1.0/24 via 172.16.1.2
[dry-run] vppctl ip route add 10.10.2.0/24 via 172.16.2.2
```

`DRY_RUN=0` を指定すると実際に `vppctl` を実行します。

## 6. 重要なVM scripts

### Build

- `scripts/vm-build-cc.sh`
  - CMakeなしで `cc` だけでbuildする。
  - unit-ish testsも実行する。
- `scripts/vm-build.sh`
  - CMake build用。

### Namespace underlay

- `scripts/vm-netns-setup.sh`
  - `site-a` / `site-b` / `hub-1` / `relay-c` namespaceとveth underlayを作る。
- `scripts/vm-netns-smoke.sh`
  - IPsec/VPPなしでdirect/hub/relay L3疎通を確認する。
- `scripts/vm-netns-clean.sh`
  - namespaceを削除する。

### strongSwan direct

- `scripts/vm-netns-ipsec-direct-generate.sh`
  - direct用 `swanctl.conf` を生成する。
- `scripts/vm-netns-ipsec-direct-start.sh`
  - `site-a` / `site-b` でcharonを起動し、direct CHILD SAを確立する。
- `scripts/vm-netns-ipsec-direct-smoke.sh`
  - pingとESP counter増加を確認する。
- `scripts/vm-netns-ipsec-direct-status.sh`
  - interface / XFRM / SA状態を見る。
- `scripts/vm-netns-ipsec-direct-stop.sh`
  - direct用charonとXFRM stateを止める。

### strongSwan hub

- `scripts/vm-netns-ipsec-hub-generate.sh`
  - hub用 `swanctl.conf` を生成する。
- `scripts/vm-netns-ipsec-hub-start.sh`
  - `site-a` / `hub-1` / `site-b` でcharonを起動し、route-based IPsecを構成する。
- `scripts/vm-netns-ipsec-hub-smoke.sh`
  - hub経由pingと両segmentのESP counter増加を確認する。
- `scripts/vm-netns-ipsec-hub-status.sh`
  - hub pathの状態を見る。
- `scripts/vm-netns-ipsec-hub-stop.sh`
  - hub用charonとXFRM stateを止める。

### VPP

- `scripts/vm-vpp-preflight.sh`
  - `vpp` / `vppctl` / service状態を見る。
- `scripts/vm-install-vpp-fdio.sh`
  - FD.io packagecloud repositoryからVPPをinstallするhelper。
- `scripts/vm-vpp-netns-setup.sh`
  - VPP host-interfaceとnamespace側vethを作る。
- `scripts/vm-vpp-netns-smoke.sh`
  - VPP forwarding単体を確認する。
- `scripts/vm-vpp-controller-netns-smoke.sh`
  - controller生成VPP route planを実VPPに適用して疎通確認する。

### Integrated smoke

- `scripts/vm-controller-integrated-runtime-smoke.sh`
  - 現在の一番見せやすいデモ入口。

実行例:

```sh
sudo sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
sudo MODE=fallback sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
```

mode:

- `MODE=direct`
  - 通常のpriority selectionで `path-direct` を使う。
- `MODE=hub`
  - `path-via-hub` を強制する。
- `MODE=fallback`
  - active `path-direct` failureを注入し、YAML fallback policyで `path-via-hub` を選ぶ。
- `MODE=both`
  - directとfallbackを連続実行する。

成功時の代表出力:

```text
Controller integrated runtime smoke passed: MODE=direct
Controller integrated runtime smoke passed: MODE=fallback
```

### Scenario harness

- `examples/eventnet_scenario.c`
  - 本番daemon化前に、path selection / fallback / evaluated policyをCLI引数で実験する。
- `scripts/vm-eventnet-scenario-smoke.sh`
  - scenario harnessの代表ケースをまとめて確認する。

実行例:

```sh
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

個別例:

```sh
build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct \
  --expect path-via-hub

build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --mode evaluated \
  --health path-direct=healthy,rtt=80,loss=0.5 \
  --health path-via-relay-c=healthy,rtt=30,loss=0.1 \
  --compare packet_loss,latency,hop_count,path_id \
  --expect path-via-relay-c

build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --step direct-ok \
  --step direct-failed \
  --step direct-recovered \
  --step relay-best \
  --explain-json out/scenario/multistep-explain.jsonl
```

成功時の代表出力:

```text
result: pass
EventNet scenario smoke passed.
```

`--step` は複数指定でき、代表的な連続イベントを再現する。

- `direct-ok`: 通常時に `path-direct` を選ぶ。
- `direct-failed`: active direct failureを注入し、`path-via-hub` fallbackを選ぶ。
- `direct-recovered`: hub active状態からpriorityに従って `path-direct` へ戻る。
- `relay-best`: injected healthでrelayが最良になるevaluated selectionを試す。

`--explain-json FILE` を指定すると、各scenario判断をJSON Lines形式で追記する。

主なfield:

- `schema`
- `scenario_step`
- `yaml`
- `intent`
- `selection_mode`
- `active_path`
- `failed_path`
- `selected_path`
- `transition_state`
- `reason`
- `expect`
- `result`
- `excluded`
- `injected_health`

## 7. 作業時の推奨確認順

Linux VMでは以下の順が安全です。

```sh
cd /mnt/hgfs/mitou/controller-c
git pull
sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
sh scripts/vm-build-cc.sh
sudo sh scripts/vm-netns-setup.sh
sudo sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
sudo MODE=fallback sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
```

VPPやstrongSwanが怪しい場合:

```sh
sh scripts/vm-runtime-status.sh
sh scripts/vm-vpp-preflight.sh
sudo sh scripts/vm-netns-ipsec-direct-status.sh
sudo sh scripts/vm-netns-ipsec-hub-status.sh
```

## 8. よくある詰まりどころ

### `vpp` / `vppctl` が無い

Ubuntu標準aptに無い場合があります。

```sh
sh scripts/vm-vpp-os-info.sh
sudo DRY_RUN=0 sh scripts/vm-install-vpp-fdio.sh
```

### packagecloudへ接続できない

DNS / outbound networkを確認します。

```sh
sh scripts/vm-network-dns-check.sh packagecloud.io
```

### charonが起動しない

まずdefault strongSwan confで起動できるかを確認します。

```sh
sudo sh scripts/vm-charon-config-probe.sh
```

### directとhubのcharonが衝突する

direct/hubは別run dirを使いますが、起動前に片方を止めるのが安全です。

```sh
sudo sh scripts/vm-netns-ipsec-direct-stop.sh
sudo sh scripts/vm-netns-ipsec-hub-stop.sh
```

## 9. 次に触るなら

優先度が高い順:

1. `eventnetd`風の簡易reconcile loopを作る。
2. health/event fileを監視して、自動でdirectからhubへfallbackする。
3. `selected-path.txt` だけでなくJSON explain outputを出す。
4. hub/relay/VPP edge mappingをより一般化する。
5. IPsecとVPPを同一packet pipelineに接続する設計を詰める。

おすすめの次ファイル:

- `examples/netns_plan.c`
  - 生成runtimeを増やすならここ。
- `src/controller.c`
  - event/reconcile loopへ進む前にcontroller APIを読む。
- `src/path_selection.c`
  - fallbackやevaluated selectionを触るならここ。
- `scripts/vm-controller-integrated-runtime-smoke.sh`
  - デモの入口を増やすならここ。

関連資料:

- `docs/scenario-vs-production.md`
  - `eventnet_scenario` と本番 `eventnetd` の差分、共通化する部分、本番化までに必要な実装を整理している。
- `docs/future-implementation-map.md`
  - 次に実装するファイル、関数、出力、後回しにする領域を具体的に整理している。
