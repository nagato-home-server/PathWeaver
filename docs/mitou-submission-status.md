# 未踏提出向け 実装到達点

この文書は、現時点のPathWeaver実装が「何を実証できるか」「何が未実装か」を提出・説明向けに整理したものです。

## 1. 一文要約

PathWeaverは、YAMLで宣言したIntent/Path/Policyから経路を選択し、strongSwanとVPPを組み合わせたruntimeを生成・検証できる、イベント駆動型ネットワーク制御基盤のCプロトタイプです。

## 2. 現時点で実証済みのこと

### Controller / YAML

- `tunnels` / `paths` / `intents` / `vpp_edges` をYAMLで定義できる。
- `path-direct` / `path-via-hub` / `path-via-relay-c` を共通のPath modelで扱える。
- priority / evaluated / fallback / recovery相当の判断を実験できる。

### strongSwan

- Linux network namespace内でdirect IPsecを確立できる。
- Linux network namespace内でhub経由IPsecを確立できる。
- ping疎通だけでなく、ESP packet counter増加を確認している。

### VPP

- VPPをLinux namespaceとhost-interfaceで接続できる。
- YAML `vpp_edges` からcontrollerがVPP route planを生成できる。
- 生成したVPP routeを実VPPへ適用し、LAN traffic forwardingを確認している。

### Integrated runtime

- `eventnet_netns_plan` が `apply-integrated.sh` を生成する。
- generated runtimeから、IPsec起動、IPsec smoke、VPP setup、VPP route apply、VPP forwarding smokeを連続制御できる。
- `MODE=direct` と `MODE=fallback` の統合runtime smokeがVMで成功済み。

### Scenario harness

- `eventnet_scenario` でpath selection条件をCLIから注入できる。
- multi-step scenarioで以下を一括検証できる。
  - direct正常時は `path-direct`
  - direct障害時は `path-via-hub`
  - direct回復時は `path-direct`
  - 品質条件評価では `path-via-relay-c`
- `--explain-json` で選択理由をJSON Linesとして保存できる。

## 3. 代表デモ

安全な非rootデモ:

```sh
sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
```

実IPsec/VPPも含むVMデモ:

```sh
sudo RUN_RUNTIME=1 sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
```

個別確認:

```sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
sudo sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
sudo MODE=fallback sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
```

## 4. 出力例として見せるもの

- `out/scenario/multistep-explain.jsonl`
  - scenario stepごとの選択理由。
- `out/netns-runtime/selected-path.txt`
  - controllerが選んだpathとsegment/tunnel/VPP edge情報。
- `out/netns-runtime/apply-integrated.sh`
  - controller-generated runtime script。
- VM smokeの成功ログ
  - `Controller integrated runtime smoke passed: MODE=direct`
  - `Controller integrated runtime smoke passed: MODE=fallback`

## 5. 本番実装との差分

現時点の実装は、提出向けプロトタイプです。本番controllerとの差分は以下です。

- 常駐daemonではなく、CLI/script中心。
- strongSwan VICI event購読は未実装。
- VPP binary API連携は未実装。
- FRRouting連携は未実装。
- Flow Preserveは未完成。
- 同一packetがIPsec復号後にVPP forwarding pipelineを連続通過する本番gateway構成は未完成。
- GUIは未実装。

詳細は `docs/scenario-vs-production.md` を参照してください。

## 6. 次に実装する候補

未踏提出までに追加すると効果が大きい順:

1. `eventnetd --once` / `--loop` の簡易file event reader。
2. `out/eventnetd/status.jsonl` への状態出力。
3. demo用の図・スクリーンショット・提出資料用README整理。
4. IPsec/VPP同一packet pipelineの設計メモ。

後回しでよいもの:

- GUI
- FRRouting本統合
- VPP binary API
- strongSwan VICI event購読
- systemd化
