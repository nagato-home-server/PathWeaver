# 未踏提出向け 実装到達点 詳細版

この文書は、PathWeaver の現時点の実装を、未踏提出・共同作業・デモ説明でそのまま使える粒度に整理したものです。

単に「どの機能があるか」を列挙するだけでなく、なぜその機能を実装したのか、どのような操作で何を確認できるのか、そして本番実装へ進む際に何が残っているのかを明確にします。

## 1. プロジェクト概要

PathWeaver は、strongSwan、VPP、将来的には FRRouting などの既存ネットワークOSSを、宣言的なIntentとイベント駆動の制御ロジックで束ねるためのネットワーク制御基盤です。

本プロジェクトの目的は、新しいVPNプロトコルや独自データプレーンを作ることではありません。IPsec、VPP forwarding、Linux network namespace など、既に存在する標準的な仕組みを利用し、その上位に「どの経路を選ぶか」「いつ切り替えるか」「なぜその経路を選んだか」を扱う制御プレーンを作ることを狙っています。

現在の実装では、YAMLで複数の候補経路を定義し、Cで実装したcontrollerがIntentとpolicyに基づいて経路を選択します。その選択結果から、strongSwan IPsec runtime と VPP forwarding runtime を操作するスクリプトを生成し、Linux VM上で実際に疎通確認できるところまで到達しています。

一文で言うと、現時点の PathWeaver は次のことができます。

```text
YAMLで宣言したIntent/Path/Policyを読み、
controllerが経路を選び、
strongSwanとVPPを組み合わせたruntimeを生成し、
障害・回復・品質条件の違いによる経路選択を実験し、
その判断理由をJSONLで説明できる。
```

## 2. 現在のプロトタイプで実証したいこと

未踏提出向けの現段階では、商用品レベルの常駐daemonやGUIではなく、「研究・実証用の制御基盤として価値があるか」を示すことを重視しています。

そのため、現時点の実証ポイントは次の4つです。

1. **宣言的なネットワーク制御**
   - 利用者は `swanctl` や `vppctl` の具体コマンドを書くのではなく、YAMLでIntent、Path、Tunnel、VPP edgeを定義します。
   - controllerはそのYAMLを読み、候補経路の集合と選択policyを理解します。

2. **複数経路の抽象化**
   - direct、hub、relayを特別扱いせず、すべて `Path` と `Segment` の組み合わせとして扱います。
   - これにより、将来的にSecurity GatewayやCloud POPなどの経由地を増やしても、同じモデルで扱える余地があります。

3. **イベント・品質条件による経路選択**
   - 通常時はdirectを選び、direct障害時はhubにfallbackし、direct回復時はpriorityに従ってdirectへ戻る、という流れをscenario harnessで再現できます。
   - RTTやpacket lossを注入し、evaluated selectionでrelayが選ばれるケースも再現できます。

4. **OSS runtimeとの接続**
   - strongSwanをLinux network namespace内で起動し、direct/hub IPsec tunnelを確立しています。
   - VPPをhost-interface経由でnamespaceへ接続し、controllerが生成したroute planを実VPPへ適用してLAN forwardingを確認しています。

## 3. 実装済みコンポーネント

### 3.1 C controller core

controller core は `src/` と `include/eventnet/` にあります。

主な役割は、YAMLから読み込んだモデルに対して、Intentを受け取り、Pathを選び、Transitionの骨格を実行し、結果を説明可能な形にまとめることです。

実装済みの主な要素:

- `Intent`
  - どの通信を、どのようなpolicyで制御するかを表します。
- `Path`
  - `site-a -> site-b` のdirectや、`site-a -> hub-1 -> site-b` のhub経由など、通信経路全体を表します。
- `Segment`
  - Pathを構成する1区間です。hub経由なら `site-a -> hub-1` と `hub-1 -> site-b` の2つのSegmentを持ちます。
- `Tunnel`
  - Segmentに対応するIPsec tunnelを表します。
- `VPP edge`
  - 各site nodeとVPP host-interface/next-hopの対応を表します。
- `Health`
  - Pathの状態、RTT、packet lossなどを表します。
- `Selection result`
  - 選ばれたPath、除外されたPath、選択理由を表します。

この段階では、controllerは本番daemonとして常駐するのではなく、CLIやscriptから呼び出して使います。これは実装を小さく保ち、選択ロジックやruntime生成を先に検証するためです。

### 3.2 YAML parser

YAML parser は `src/yaml_config.c` にあります。

現在読めるtop-level key:

- `tunnels`
- `vpp_edges`
- `paths`
- `intents`

例:

```yaml
vpp_edges:
  - node_id: site-a
    vpp_interface: host-vpp-site-a
    namespace_address: 172.16.1.2/30
    next_hop: 172.16.1.2
```

この情報により、Cコードに `172.16.x.x` のnext-hopを固定せず、YAML側でVPP接続点を定義できます。

YAML parserは本格的なYAMLライブラリではなく、現在の実証に必要なindentation-based parserです。将来的にYAML表現が複雑になる場合は、libyaml等への置き換え候補があります。

### 3.3 Path selection

Path selection は `src/path_selection.c` にあります。

実装済みの選択方式:

- `explicit`
  - 指定されたPathをそのまま選びます。
  - 実験や手動切替に向いています。
- `priority`
  - YAMLに書かれた候補順に、利用可能な最初のPathを選びます。
  - primary/secondaryの単純fallbackに向いています。
- `evaluated`
  - packet loss、latency、hop count、priority、path id などの比較順序に従って候補を比較します。
  - 品質に応じた経路制御の入口です。

現在のevaluated selectionは基本形です。ヒステリシス、ホールドダウン、連続成功回数などの安定化制御はまだ入れていません。

### 3.4 Transition model

Transition model は `src/transition.c` にあります。

現在のcontroller内部では、次のような段階を持っています。

```text
prepare
-> validate
-> commit
-> confirm
-> completed
```

失敗時にはrollbackへ進む骨格もあります。

ただし、このTransition modelは現時点では「controller内部の状態遷移モデル」です。VMで実際にstrongSwan/VPPを動かす部分は、主に生成されたshell scriptが担っています。将来的なproduction `eventnetd` では、このTransition modelと実adapter操作をより密に接続します。

### 3.5 Runtime generator

Runtime generator は `examples/netns_plan.c` です。

このCLIは、YAMLとcontrollerの選択結果から、Linux VM上で実行するruntime scriptを生成します。

生成される主なファイル:

- `out/netns-runtime/selected-path.txt`
  - controllerが選んだPath、Segment、Tunnel、VPP edge情報を出します。
- `out/netns-runtime/apply-selected.sh`
  - selected pathのIPsec runtimeだけを起動・検証します。
- `out/netns-runtime/apply-integrated.sh`
  - IPsec runtimeとVPP forwarding runtimeを一つの流れで連続制御します。
- `out/netns-runtime/vpp-route-plan.sh`
  - 一般的なVPP route planを出します。
- `out/netns-runtime/vpp-netns-route-plan.sh`
  - Linux VMのVPP host-interface構成向けroute planを出します。

このうち、提出デモで最も重要なのは `apply-integrated.sh` です。これは、controllerが選んだPathに応じて、strongSwan IPsecの起動・検証と、VPP route apply・forwarding smokeを一つの生成planで実行します。

### 3.6 Scenario harness

Scenario harness は `examples/eventnet_scenario.c` です。

本番daemon化前に、controller判断を高速に実験するためのCLIです。実ネットワークを毎回動かさず、healthやfailure eventをCLI引数で注入し、どのPathが選ばれるかを確認できます。

代表例:

```sh
build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct \
  --expect path-via-hub
```

この例では、現在のactive pathが `path-direct` で、そのdirectがfailedになった、という条件を注入します。その結果、YAMLのfallback policyに従って `path-via-hub` が選ばれることを確認します。

multi-step scenarioもあります。

```sh
build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --step direct-ok \
  --step direct-failed \
  --step direct-recovered \
  --step relay-best \
  --explain-json out/scenario/multistep-explain.jsonl
```

この1コマンドで、次の流れを順に確認します。

```text
direct正常
-> direct障害
-> hub fallback
-> direct回復
-> 品質条件でrelayが最良
```

`--explain-json` を指定すると、各stepの判断理由をJSON Lines形式で保存します。

### 3.7 Explain JSONL

Explain JSONL は、controllerの判断理由を機械可読に保存するための出力です。

例:

```json
{"schema":"eventnet.scenario.explain.v1","scenario_step":"direct-failed","selected_path":"path-via-hub","reason":"active path path-direct failed; using fallback path-via-hub","result":"pass"}
```

実際の出力には、次の情報も含まれます。

- YAML file
- Intent ID
- selection mode
- active path
- failed path
- selected path
- transition state
- reason
- expected path
- pass/fail result
- excluded paths
- injected health

この出力は、将来の `eventnetd` のstatus/explain APIの原型です。また、未踏提出時には「なぜそのPathを選んだか」を説明する材料になります。

## 4. 実ネットワークで確認済みのこと

### 4.1 direct IPsec

Linux network namespace上で、`site-a` と `site-b` の間にdirect IPsec tunnelを確立しました。

確認内容:

- `charon` を各namespace内で起動。
- `swanctl.conf` を生成・load。
- `tun-a-b` CHILD SAを確立。
- `site-a` から `site-b` へping。
- `swanctl --list-sas` のESP packet counterが増加することを確認。

単なるping疎通ではなく、ESP counter増加を見ているため、trafficがIPsecに乗っていることを確認しています。

### 4.2 hub IPsec

Linux network namespace上で、`site-a -> hub-1 -> site-b` のhub経由IPsec pathを確立しました。

確認内容:

- `site-a` / `hub-1` / `site-b` で `charon` を起動。
- `tun-a-hub` と `tun-hub-b` を確立。
- XFRM interfaceを使ってroute-based IPsecを構成。
- `site-a` から `site-b` へhub経由でping。
- `site-a/tun-a-hub` と `hub-1/tun-hub-b` のESP counterが増えることを確認。

hub pathでは複数のIPsec tunnelを扱うため、directよりも「PathがSegmentの集合である」という設計が見えやすくなっています。

### 4.3 VPP forwarding

VPPをLinux namespaceと接続し、VPPがL3 forwarding planeとして動作することを確認しました。

構成:

```text
site-a:vpp-client 172.16.1.2/30
  <-> VPP host-vpp-site-a 172.16.1.1/30

site-b:vpp-client 172.16.2.2/30
  <-> VPP host-vpp-site-b 172.16.2.1/30
```

確認内容:

- VPP host-interfaceを作成。
- namespace側vethとVPP host-interfaceを接続。
- controller生成のVPP routeを `DRY_RUN=0` で実適用。
- `site-a` / `site-b` のLAN routeをVPP edgeへ向ける。
- `10.10.1.1 <-> 10.10.2.1` のpingがVPP経由で成功。

### 4.4 Integrated runtime

`apply-integrated.sh` により、IPsec runtimeとVPP forwarding runtimeを同じcontroller-generated planで連続制御できます。

direct modeで確認した流れ:

```text
YAML/controller selects path-direct
-> generated apply-integrated.sh
-> start direct IPsec runtime
-> check ESP counters
-> setup VPP host-interface
-> apply VPP routes
-> verify VPP forwarding
```

fallback modeで確認した流れ:

```text
active path-direct failure event
-> controller selects path-via-hub
-> generated apply-integrated.sh
-> start hub IPsec runtime
-> check hub-path ESP counters
-> setup VPP host-interface
-> apply VPP routes
-> verify VPP forwarding
```

確認済みログ:

```text
Controller integrated runtime smoke passed: MODE=direct
Controller integrated runtime smoke passed: MODE=fallback
```

注意点として、現段階では「同じcontroller-generated planでIPsecとVPPを連続制御する」統合です。同一packetがIPsec復号後にVPP forwarding pipelineを連続通過する本番gateway pipelineは、次段階の設計・実装課題です。

## 5. 代表デモ

### 5.1 安全な非rootデモ

通常ユーザーで実行できる範囲のデモです。

```sh
sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
```

このデモで行うこと:

1. C controllerをbuildする。
2. C testを実行する。
3. scenario harnessでpriority/fallback/recovery/evaluated selectionを確認する。
4. Explain JSONLを表示する。
5. fallback条件でruntime planを生成する。
6. `selected-path.txt` と `apply-integrated.sh` の生成を確認する。

このデモはroot権限を必要としないため、審査や共同作業の場で安全に見せやすいです。

### 5.2 実IPsec/VPPを含むVMデモ

実際にstrongSwanとVPPを動かすデモです。

```sh
sudo RUN_RUNTIME=1 sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml
```

このデモでは、非rootデモの内容に加えて、実IPsec/VPP runtime smokeを実行します。

必要な前提:

- Linux VM
- network namespace作成済み
- strongSwan / swanctl / charon
- VPP / vppctl
- root権限

個別に確認する場合:

```sh
sudo sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
sudo MODE=fallback sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
```

## 6. 出力として見せるべきもの

### 6.1 `out/scenario/multistep-explain.jsonl`

scenario stepごとの判断理由です。

見るべき点:

- `direct-ok` で `path-direct` が選ばれている。
- `direct-failed` で `path-via-hub` が選ばれている。
- `direct-recovered` で `path-direct` に戻っている。
- `relay-best` で injected health により `path-via-relay-c` が選ばれている。

### 6.2 `out/netns-runtime/selected-path.txt`

controllerが選んだPathの説明です。

見るべき点:

- `selected_path`
- `runtime_kind`
- source/destination
- route destination prefix
- VPP edge mapping
- Segment一覧
- Segmentに対応するTunnel情報

### 6.3 `out/netns-runtime/apply-integrated.sh`

controllerが生成した統合runtime scriptです。

見るべき点:

- directならdirect IPsec scriptを呼ぶ。
- hubならhub IPsec scriptを呼ぶ。
- その後VPP host-interface setupとVPP route applyを行う。
- 最後にVPP forwarding smokeを行う。

## 7. 現時点で「できている」と言えること

提出・説明では、次のように言えます。

```text
PathWeaverは、YAMLで定義された複数の拠点間経路候補に対して、
controllerがIntentとpolicyを評価し、
direct/hub/relayのいずれかを選択できる。

また、選択結果からstrongSwanとVPPのruntime planを生成し、
Linux VM上でdirect IPsec、hub IPsec、VPP forwarding、
およびIPsec+VPP統合runtime smokeを確認している。

さらに、scenario harnessにより、
障害・回復・品質条件をCLIで注入して、
controllerの判断と説明JSONLを再現できる。
```

## 8. 本番実装との差分

現時点の実装は、提出向けプロトタイプです。本番controllerとの差分は以下です。

### 8.1 常駐daemonではない

現在はCLIとscriptで実行します。本番では `eventnetd` が常駐し、config reload、signal handling、event loop、status outputを持つ必要があります。

### 8.2 状態観測は限定的

現在はscenario harnessでhealthやfailureを注入しています。本番では、strongSwan、VPP、health probeから実際の状態を継続的に観測する必要があります。

### 8.3 strongSwan VICI event購読は未実装

現時点では `swanctl` とscriptでSA確立・状態確認を行っています。本番ではVICI eventを購読し、IKE SA / CHILD SA の作成・削除・rekey・DPDなどをcontrollerのObserved Stateへ反映する必要があります。

### 8.4 VPP binary API連携は未実装

現時点では主に `vppctl` commandを生成・実行しています。本番ではVPP API、FIB状態取得、interface/counter observationなどをadapter化する必要があります。

### 8.5 FRRoutingは未実装

初期実装ではFRRoutingは後段に置いています。BGP/OSPF/BFDなどと連携する場合は、第3段階の機能になります。

### 8.6 Flow Preserveは未完成

Flow Preserveの概念は設計にありますが、実際のflow分類、既存flow維持、新規flow割当制御はまだ実装していません。

### 8.7 同一packetのIPsec→VPP本番pipelineは未完成

現在の統合runtimeは、同じcontroller-generated planでIPsecとVPPを連続制御するものです。実運用gatewayとして、同一packetがIPsec tunnelとVPP forwarding pipelineを連続して通る構成は、次の設計課題です。

### 8.8 GUIは未実装

GUIは後段です。現時点ではCLI、script、JSONL、text outputで実証しています。

詳細は `docs/scenario-vs-production.md` を参照してください。

## 9. 次に実装する候補

未踏提出までに追加すると効果が大きい順:

1. **簡易 `eventnetd --once` / `--loop`**
   - file eventを読み、scenario harnessではなくcontroller loopとして判断する。
   - `out/eventnetd/status.jsonl` に状態を出す。

2. **提出資料用の図**
   - YAML -> Controller -> strongSwan/VPP -> Explain JSONL の流れを1枚にする。
   - direct/fallback/evaluated の切替図を作る。

3. **IPsec/VPP同一packet pipelineの設計メモ**
   - 現runtimeとの違いを明確にする。
   - XFRM interface、VPP interface、Linux routeの関係を整理する。

4. **scenario case追加**
   - required waypoint
   - forbidden waypoint
   - max RTT violation
   - packet loss violation

後回しでよいもの:

- GUI
- FRRouting本統合
- VPP binary API
- strongSwan VICI event購読
- systemd化
- 本番HA/永続DB
