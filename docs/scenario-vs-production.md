# Scenario Harness と Production eventnetd の差分

この文書は、次に追加する `eventnet_scenario` の位置付けを明確にするためのものです。

`eventnet_scenario` は本番daemonではありません。Path selection、fallback、recovery、evaluated policyを安全かつ高速に実験するためのテストハーネスです。

## 1. なぜscenario harnessを先に作るか

未踏提出向けには、単に実ネットワークを一回動かすだけではなく、次を示せることが重要です。

- 複数のpath候補を宣言的に定義できる。
- health/event条件を変えると選択pathが変わる。
- fallbackやrecoveryの判断理由を説明できる。
- 新しい経路選択アルゴリズムを差し替えて試せる。

本番daemonを急いで作ると、OSプロセス管理、権限、VICI/VPP API、ログ管理などに実装時間を取られます。

そこで先に `eventnet_scenario` を作り、controller判断部分を小さく再現可能に検証します。

## 2. eventnet_scenario の役割

`eventnet_scenario` は、YAML configに対してテスト条件をCLIから注入し、controllerがどのpathを選ぶかを確認します。

想定入力:

- YAML config
- `--mode explicit|priority|evaluated`
- `--active-path PATH`
- `--fail-path PATH`
- `--health PATH=state[,rtt=N,loss=N]`
- `--expect PATH`
- `--generate-runtime`

想定出力:

- selected path
- selection reason
- excluded pathと理由
- injected health
- expectation pass/fail
- runtime生成先

用途:

- path selection policyの単体検証。
- fallback/recovery条件の再現。
- evaluated selectionの比較条件テスト。
- 未踏デモ用の説明ログ生成。

## 3. Production eventnetd の役割

本番 `eventnetd` は、長時間動作するcontroller processです。

想定入力:

- YAML desired state
- strongSwan observed state
- VPP observed state
- Health probe result
- 外部API/CLIからのIntent update
- timer/event source

想定出力:

- applied state
- observed state
- transition state
- audit log
- explain/status API
- adapter操作

用途:

- 実ネットワーク状態を継続監視する。
- Desired / Observed / Applied の差分を検出する。
- 必要に応じて再適用、fallback、rollbackする。
- controller再起動後に状態を復元する。

## 4. 差分一覧

| 項目 | eventnet_scenario | production eventnetd |
|---|---|---|
| 実行形態 | 1回実行CLI | 常駐daemon |
| 状態入力 | CLI引数で注入 | adapter/health/eventから観測 |
| strongSwan | mockまたは生成runtime接続 | VICI/API/状態購読 |
| VPP | mockまたは生成runtime接続 | vppctl/API/状態取得 |
| Health | `--health` で注入 | ping/counter/BFD等で測定 |
| Event | `--fail-path` などで注入 | event queue / watcher |
| State store | process内一時状態 | 永続/復元可能な状態 |
| Reconcile | 1回の判断 | 継続的な収束処理 |
| Rollback | 選択・plan検証中心 | 実adapter操作の失敗から復旧 |
| Explain | stdout中心 | file/API/structured log |

## 5. 共通化するべきもの

`eventnet_scenario` で作ったもののうち、productionでも使うべきもの:

- YAML parser
- data model
- path selection
- transition state enum
- health state model
- explanation model
- runtime generationの一部
- tests/scenario definitions

特に `en_select_path` と `en_reconcile_result_t` は、scenarioとproductionの両方で中心になります。

## 6. 本番化までに追加が必要なもの

### 6.1 Daemon lifecycle

- foreground/background mode
- signal handling
- config reload
- pid/runtime directory
- root権限チェック
- systemd unit候補

### 6.2 Event source

- file watcher
- timer
- CLI/API intent update
- strongSwan VICI event
- VPP route/interface/counter observation
- health probe result

### 6.3 State store

- desired state
- observed state
- applied state
- transition state
- health state
- error state
- restart recovery metadata

### 6.4 Adapter observation

strongSwan:

- IKE SA一覧取得
- CHILD SA一覧取得
- SA event購読
- DPD/rekey/delete検知

VPP:

- interface状態取得
- route/FIB確認
- counter取得
- route apply結果確認

Health:

- end-to-end ping
- RTT/loss/jitter測定
- consecutive success/failure管理

### 6.5 Reconciliation loop

- desiredとobservedの差分検出
- path candidate再評価
- transition開始条件
- hold-down/hysteresis
- fallback/recovery policy
- retry/backoff
- rollback

### 6.6 Apply orchestration

- prepare
- validate
- commit
- confirm
- rollback
- cleanup
- partial failure handling

### 6.7 Explain/status API

- selected path
- rejected candidates
- health snapshot
- active/applied path
- transition state
- last commands
- last error

初期はfile出力でよいです。

## 7. 破棄してよいscenario専用部分

productionへ持ち込まなくてよいもの:

- `--health` の手動注入parser
- `--expect` のpass/fail判定
- scenario専用step名
- stdout中心のテスト出力
- mock前提の一部ショートカット

ただし、これらはテスト用途として残す価値があります。

## 8. 実装順

具体的なファイル配置、関数名、出力先は `docs/future-implementation-map.md` にまとめています。

推奨順:

1. `eventnet_scenario --once` を作る。
2. `--health` / `--fail-path` / `--expect` を実装する。
3. evaluated selectionの実験を増やす。
4. `--generate-runtime` で既存 `eventnet_netns_plan` と接続する。
5. scenario smoke scriptを追加する。
6. event fileを読む簡易loopへ進む。
7. production `eventnetd` のstate store / event loopへ進む。

この順なら、実験可能性を早く示しつつ、本番実装への道筋も崩れません。
