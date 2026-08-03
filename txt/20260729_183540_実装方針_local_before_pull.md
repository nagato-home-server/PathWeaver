# イベント駆動型オープンネットワーク制御基盤
## 技術仕様書（構想・基本設計統合版）

- 文書状態: Draft
- 文書種別: アーキテクチャ仕様・基本設計
- 対象実装段階: 初期プロトタイプから拡張実装まで
- 想定データプレーン: strongSwan、FD.io VPP、将来のFRRouting

---

## 1. 文書の目的

本書は、複数のネットワークOSSを統合し、宣言的なIntent、イベント駆動制御、統一状態モデル、Path Selection PolicyおよびPath Transition Policyによってネットワーク経路を制御する基盤について、目的、境界、用語、機能要件、状態モデル、制御手順、障害処理、初期実装範囲および評価方法を定義する。

本書は研究構想の説明だけを目的とせず、実装判断に利用できる技術仕様の基準文書とする。

本書における要求レベルは次のとおりである。

- **必須（MUST）**: 初期仕様または対象段階で満たさなければならない。
- **禁止（MUST NOT）**: 実装してはならない、または前提としてはならない。
- **推奨（SHOULD）**: 原則として満たす。満たさない場合は理由を記録する。
- **任意（MAY）**: 実装上の選択肢とする。

---

## 2. プロジェクトの目的

本プロジェクトは、strongSwan、FD.io VPP、FRRoutingなどの既存ネットワークOSSを統合し、ネットワークの構成、経路選択および経路遷移を宣言的かつイベント駆動で制御する、オープンなネットワーク制御基盤を構築することを目的とする。

本プロジェクトは、次を目的としない。

- 新しいVPN方式または独自暗号方式の開発
- ESP、IKEv2その他の標準IPsecプロトコルの独自拡張
- 商用VPNルータまたは商用SD-WAN製品の単純な低価格代替
- 任意のネットワークトポロジを無制限に自動生成する仕組み

標準IPsec、既存のルーティング機能および既存の転送機能をデータプレーンとして利用し、その上位に次の機能を提供する。

1. 宣言的Intentの受付と検証
2. 異種OSSの状態を統一モデルへ正規化するAdapter機構
3. Desired State、Applied State、Observed Stateの管理
4. イベントおよび定期観測に基づくReconciliation
5. 許可済み候補Path集合からの経路選択
6. Pathの準備、検証、切替、確認およびロールバック
7. 経路選択理由と状態遷移履歴の記録
8. 選択アルゴリズム、遷移方式およびAdapterを交換可能にする拡張機構

SD-WANに類似する拠点間経路制御は、本基盤を検証する最初のユースケースと位置付ける。

---

## 3. プロジェクトの位置付け

本基盤は、単純な「OSS版SD-WAN」ではない。

商用SD-WAN製品では、VPN、ルーティング、品質測定、経路選択、フェイルオーバーおよび管理機能が一体化されている。一方、その内部状態モデルや遷移ロジックは製品固有であり、第三者が新しい選択アルゴリズムや遷移方式を追加して比較することは難しい。

本基盤では、各OSSを独立した状態機械を持つネットワーク機能として扱い、それらを共通のリソースモデルと制御ランタイムで統合する。

```text
利用者・上位システム
        |
        v
    Intent API
        |
        v
 Intent / Policy Engine
        |
        v
 Path Selection Engine
        |
        v
 Path Transition Engine
        |
        v
 Reconciliation Runtime
        |
        +------------------+------------------+
        |                  |                  |
        v                  v                  v
 strongSwan Adapter     VPP Adapter       FRR Adapter
        |                  |                  |
        v                  v                  v
 IKE / IPsec SA        Forwarding          Routing
```

制御ランタイムは、単に各OSSのAPIを決められた順序で呼び出すワークフロー実行器ではない。目標状態と観測状態の差分を継続的に評価し、冪等な操作によってシステムを目標状態へ収束させる。

ただし、Path切替のように操作順序が通信へ直接影響する処理については、単純な最終状態への収束ではなく、明示的なTransition Resourceと状態機械を使用する。

---

## 4. 想定利用者

### 4.1 初期利用者

初期段階では、次の研究・実証・開発用途を対象とする。

- 大学および研究機関
- ネットワーク研究者
- OSS開発者
- 通信事業者の研究開発部門
- SIerの技術検証部門
- ネットワーク製品・アプライアンス開発者
- ネットワーク制御の教育環境

### 4.2 将来利用者

成果が成熟した場合は、次への展開を想定する。

- 小規模ネットワークでのPoC
- 学校、自治体および中小組織での限定実証
- OSSネットワークアプライアンスへの組み込み
- クラウド・エッジ間接続制御
- Zero Trust Gateway、SASE、Service Chainingへの応用
- 上位オーケストレータまたはKubernetesとの連携

商用VPNルータの全面置換は初期目標に含めない。

---

## 5. 適用範囲とシステム境界

### 5.1 制御対象

初期段階の制御対象は次のとおりとする。

- strongSwanが管理するIKE SAおよびCHILD SA
- VPPが管理するインターフェース、転送表、経路および必要なフロー分類状態
- Controllerが管理するNode、Tunnel、Segment、Path、Intent、HealthおよびTransition

FRRoutingのBGP、OSPF、BFDおよび経路状態は拡張段階の対象とする。

### 5.2 制御対象外

次は初期段階の制御対象外とする。

- 既存VPNルータ内部の非公開制御機能
- ベンダー固有SD-WAN製品内部のポリシー
- アプリケーション層の再送・順序制御
- IPsecをまたぐ独自共通シーケンス番号
- 同一パケットの複数Pathへの複製
- 完全な無損失・無瞬断・順序保証

### 5.3 配置モデル

既存ルータの内蔵VPN機能へ依存せず、LinuxサーバまたはPCをOSS VPN Gatewayとして配置する。

```text
拠点LAN
   |
   v
OSS VPN Gateway
- Agent / Controller Client
- strongSwan
- VPP
- 将来: FRRouting
   |
   v
既存ルータ
   |
   v
Internet
```

既存ルータはInternet接続、NATおよび通常のルーティングを担当する。OSS VPN Gatewayは対象となる拠点間通信の暗号化、転送、経路切替および状態観測を担当する。

Controllerは中央配置を基本とするが、データプレーンはController停止時にも最後に適用済みの転送状態を維持しなければならない。

---

## 6. 設計原則

### 6.1 標準データプレーンの維持

本基盤は標準IPsecを利用し、独自ESP形式または独自暗号ヘッダを導入してはならない。

### 6.2 Control PlaneとData Planeの分離

- strongSwanは鍵交換、認証およびIPsec SA管理を担当する。
- VPPは転送、経路選択の適用および必要に応じたフロー分類を担当する。
- FRRoutingは動的ルーティング統合時に経路学習および広告を担当する。
- ControllerはIntent解釈、候補選択、状態収束および遷移制御を担当する。

### 6.3 冪等性

Adapterの設定操作は可能な限り冪等でなければならない。同じDesired Stateに対する再適用で、不要なSA再生成、経路の一時削除または通信断を発生させてはならない。

### 6.4 状態の分離

次の状態を混同してはならない。

- 設定が存在すること
- Tunnelが確立していること
- PathがEnd-to-Endで利用可能なこと
- Pathが現在の転送に使用されていること

### 6.5 安全な既定動作

不明状態、観測不能または部分適用時には、新Pathへの切替よりも現在の正常Path維持を優先する。ただし現在Pathが明確に利用不能な場合はFallback Policyに従う。

### 6.6 説明可能性

Controllerは、候補除外理由、比較結果、選択理由、遷移方式およびロールバック理由を記録しなければならない。

---

## 7. 用語定義

### 7.1 Node

制御対象となる論理的または物理的なネットワーク参加点。Site、Hub、Relay、Security Gatewayなどのroleを持つ。

### 7.2 Tunnel

二つのNode間に確立される暗号化通信関係。初期実装では原則としてIPsec CHILD SAに対応する。ただし、IKE SAとTunnelを一対一と仮定してはならない。

### 7.3 Segment

Pathを構成する隣接Node間の論理区間。Segmentは一つ以上のTunnelまたは転送要素へ解決される。

### 7.4 Waypoint

送信元と宛先の間で必ず通過する中継Node。

### 7.5 Path

SourceからDestinationへ到達するための順序付きNode列およびSegment列。Pathはトポロジ上の候補だけでなく、準備、検証、Active化および終了のライフサイクルを持つ制御対象である。

### 7.6 Candidate Path Set

管理者または上位システムによって利用を許可されたPath集合。Controllerは原則としてこの集合外のPathを自動選択してはならない。

### 7.7 Intent

利用者または上位システムが宣言する目標状態、制約、選択方式、遷移方式およびFallback方針。

### 7.8 Desired State

IntentおよびPolicy評価から導出された目標状態。

### 7.9 Applied State

Controllerが各Adapterへ適用済みと記録している状態。

### 7.10 Observed State

各OSS、ProbeまたはAgentから実際に観測された状態。

### 7.11 Transition

Current PathからTarget Pathへ移行する一回の制御トランザクション。状態、期限、対象、実行方式およびRollback情報を持つ。

### 7.12 Reconciliation

Desired State、Applied StateおよびObserved Stateの差分を評価し、収束のための操作を行う処理。

---

## 8. リソースモデル

すべての主要リソースは、最低限次のメタデータを持つべきである。

```yaml
metadata:
  id: string
  generation: integer
  resource_version: string
  created_at: timestamp
  updated_at: timestamp
```

`generation`は利用者が変更するSpecの世代、`resource_version`は保存状態の競合制御に使用する。

### 8.1 Node

```yaml
node:
  metadata:
    id: site-a
  spec:
    role: site
    endpoints:
      - address: 203.0.113.10
        transport: udp
        port: 500
    capabilities:
      - ipsec
      - vpp-forwarding
    administrative_state: enabled
  status:
    operational_state: reachable
    last_observed_at: "2026-01-01T00:00:00Z"
```

### 8.2 Tunnel

```yaml
tunnel:
  metadata:
    id: tunnel-site-a-relay-c
  spec:
    local_node: site-a
    remote_node: relay-c
    protocol: ipsec
    traffic_selectors:
      - local: 10.10.0.0/16
        remote: 10.20.0.0/16
  status:
    lifecycle_state: established
    health_state: healthy
    ike_sa_id: optional-string
    child_sa_ids:
      - optional-string
```

### 8.3 Segment

```yaml
segment:
  metadata:
    id: segment-site-a-relay-c
  spec:
    from_node: site-a
    to_node: relay-c
    tunnel_refs:
      - tunnel-site-a-relay-c
```

### 8.4 Path

```yaml
path:
  metadata:
    id: path-via-relay-c
  spec:
    source: site-a
    destination: site-b
    waypoints:
      - relay-c
    segment_refs:
      - segment-site-a-relay-c
      - segment-relay-c-site-b
    administrative_priority: 100
    administrative_state: enabled
  status:
    lifecycle_state: ready
    health_state: healthy
    active_since: null
```

### 8.5 Transition

```yaml
transition:
  metadata:
    id: transition-0001
  spec:
    current_path: path-via-hub
    target_path: path-direct
    strategy: graceful
    deadline_ms: 10000
    rollback_path: path-via-hub
  status:
    phase: validating
    started_at: "2026-01-01T00:00:00Z"
    last_error: null
```

---

## 9. Pathモデル

### 9.1 Pathの一般化

Hub、DirectおよびRelayを別機能として実装してはならない。すべて共通のPathとして表現する。

```text
Direct: Site A -> Site B
Hub:    Site A -> Hub -> Site B
Relay:  Site A -> Relay C -> Site B
```

Direct PathはWaypointが空のPathである。

### 9.2 Pathの妥当性条件

Pathは少なくとも次を満たさなければならない。

1. SourceとDestinationが存在する。
2. SourceとDestinationが同一でない。
3. Node列に重複Nodeがなく、ループを形成しない。
4. すべての隣接Node間に解決可能なSegmentが存在する。
5. すべてのNodeおよびSegmentが管理上有効である。
6. 必須Waypointおよび禁止Waypoint制約を満たす。
7. 最大Hop数を超えない。
8. セキュリティおよび地域制約を満たす。

### 9.3 自動経路生成の制限

初期実装では、Controllerは任意のNode列を探索して新規Pathを生成しない。管理者が登録した候補Pathを選択対象とする。

将来、自動生成を実装する場合も、許可グラフ、最大Hop数、許可role、ループ検出、トンネル上限および管理承認条件を必須とする。

---

## 10. Intent仕様

### 10.1 共通構造

```yaml
intent:
  metadata:
    id: intent-site-a-site-b
  spec:
    traffic:
      source: site-a
      destination: site-b
      selectors:
        - protocol: any
          source_prefix: 10.10.0.0/16
          destination_prefix: 10.20.0.0/16

    path_selection:
      mode: explicit
      candidate_paths:
        - path-direct
        - path-via-relay-c
        - path-via-hub
      explicit_path: path-direct

    transition:
      strategy: graceful
      deadline_ms: 10000
      drain_timeout_ms: 40

    fallback:
      enabled: true
      mode: priority
      paths:
        - path-via-relay-c
        - path-via-hub
```

### 10.2 検証要件

Intent APIは、保存前に次を検証しなければならない。

- 参照するNodeおよびPathが存在すること
- Candidate PathがSource/Destinationと一致すること
- `explicit`時に`explicit_path`が指定されていること
- `priority`時に候補順序が空でないこと
- `evaluated`時に比較規則と最終Tie-breakが存在すること
- Transition Strategy固有の必須パラメータが存在すること
- Fallback Pathが禁止制約に違反しないこと
- 同じTraffic Selectorに競合するIntentが存在しないこと、または競合解決規則が定義されていること

### 10.3 Intentの競合

複数Intentが同一Traffic Selectorへ異なるDesired Pathを要求する場合、Controllerは暗黙に一方を選択してはならない。

初期実装では、次のいずれかを採用する。

- 競合を検出して`CONFLICTED`とする。
- 明示的な`intent_priority`で解決する。

同一優先度の場合は、Intent IDの辞書順で解決せず、競合エラーとする。Path SelectionのTie-breakとIntent競合解決は別概念である。

---

## 11. Path Selection Policy

### 11.1 選択処理

Path選択は次の順で行う。

1. Candidate Path Setの取得
2. 管理状態による除外
3. 静的制約による除外
4. Healthおよび測定値の鮮度確認
5. 動的制約による除外
6. 選択モードに応じた順位付け
7. 安定化条件の評価
8. 一意なTarget Pathの決定
9. 選択根拠の記録

### 11.2 Explicit Mode

指定されたPathを選択する。指定Pathが利用不能の場合は、Fallback Policyを使用するかIntentを`UNSATISFIED`とする。

### 11.3 Priority Mode

候補リストの先頭から、すべての必須制約を満たす最初のPathを選択する。

### 11.4 Evaluated Mode

制約を満たす候補を、辞書式比較によって順位付けする。

初期の標準比較順は次とする。

1. Health Class: `HEALTHY`を`DEGRADED`より優先
2. Packet Loss: 小さい値を優先
3. RTT: 小さい値を優先
4. Hop Count: 小さい値を優先
5. Administrative Priority: 大きい値を優先
6. Path ID: UTF-8バイト列の昇順

Path IDによる最終Tie-breakは再現性を確保するための機械的規則であり、品質上の優位性を表さない。

### 11.5 測定値の鮮度

RTT、LossおよびJitterには`observed_at`と`valid_for_ms`を持たせる。期限切れの測定値を最新値として比較してはならない。

測定値が不足するPathは、Policyに応じて次のいずれかとする。

- 候補から除外
- `UNKNOWN`として最低順位へ配置
- 事前Probeを実行してから再評価

初期既定値は「事前Probeを実行し、期限内に取得できなければ除外」とする。

### 11.6 安定化

選択結果が現在Pathと異なる場合、次を評価する。

- minimum improvement
- confirmation samples
- hold-down duration
- minimum active duration
- emergency override

現在Pathが`FAILED`の場合、hold-downおよびminimum active durationを無視してよい。ただしTarget Pathの最低限の安全検証は省略してはならない。

### 11.7 重み付きスコア

重み付きスコア方式は拡張機能とする。実装する場合、各指標の正規化範囲、欠損値処理、スコア方向、重みの総和およびTie-breakを仕様化しなければならない。

---

## 12. Health Monitoring

### 12.1 監視対象

初期実装は次を監視する。

- Node Reachability
- IKE SAおよびCHILD SA状態
- VPP Interface状態
- End-to-End Reachability
- RTT
- Packet Loss
- Jitter
- 観測時刻
- 連続成功数および連続失敗数

### 12.2 Health状態

```text
UNKNOWN
HEALTHY
DEGRADED
UNHEALTHY
FAILED
```

`FAILED`は利用不能が確定した状態、`UNHEALTHY`は閾値違反または連続失敗により通常選択から除外される状態とする。

### 12.3 Path Healthの集約

Path Healthは次の入力から算出する。

- すべての必須SegmentのHealth
- 必要なTunnelの状態
- End-to-End Probe結果
- 測定値の鮮度

Tunnelがすべて`ESTABLISHED`でもEnd-to-End Probeに失敗する場合、Pathを`READY`または`HEALTHY`としてはならない。

### 12.4 Debounce

単発の失敗で直ちに`FAILED`へ遷移させない。初期設定例を次に示す。

```yaml
health_policy:
  observation_interval_ms: 1000
  unhealthy_after_failures: 3
  healthy_after_successes: 3
  failed_after_ms: 10000
```

DPDなど、データプレーンの明確な喪失を示すイベントは、連続失敗回数を待たず状態を悪化させてもよい。

---

## 13. Path Transition Policy

Path SelectionとPath Transitionを分離する。

- Path Selection: どのPathをTargetとするか
- Path Transition: Current PathからTarget Pathへどの手順で移行するか

初期仕様では次の三方式を定義する。

### 13.1 Immediate

ForwardingをTarget Pathへ即時変更する。

要件:

- Target Pathは最低限`READY`でなければならない。
- 切替後にPost-checkを実行しなければならない。
- 失敗時はRollbackを開始しなければならない。

想定影響:

- 短時間のPacket Loss
- Packet Reordering
- TCP再送
- UDP損失

### 13.2 Graceful

旧Pathへの新規投入を停止または抑止し、短いDrain期間後にTarget Pathへ切り替える。

```text
READY
  -> PAUSING
  -> DRAINING
  -> SWITCHING
  -> VERIFYING
  -> COMPLETED
```

Gracefulは完全な無損失または完全な順序保証を意味しない。

必要パラメータ:

- `drain_timeout_ms`
- `deadline_ms`
- Drain完了判定方法

Drain完了を厳密に観測できない実装では、`drain_timeout_ms`経過を完了条件としてよいが、その制約を記録する。

### 13.3 Flow Preserve

既存フローをCurrent Pathへ残し、新規フローをTarget Pathへ割り当てる。

原則として5-Tupleでフローを識別する。

- Source IP
- Destination IP
- Source Port
- Destination Port
- L4 Protocol

要件:

- Fragment、ICMP、ESPなど5-Tupleが成立しない通信の扱いを定義する。
- フロー状態の最大保持数と削除方針を定義する。
- Controller再起動時のフロー状態復旧方針を定義する。
- 長時間フローのTimeoutまたは強制終了方針を定義する。

Flow Preserveは初期第2段階の機能とする。

---

## 14. Make Before Break

Target Pathへ切り替える前に、必要なTunnelおよびForwarding状態を準備する。

```text
Current Path: ACTIVE
Target Path:  PREPARING
        |
        v
Target Path:  VALIDATING
        |
        v
Target Path:  READY
        |
        v
Transition
        |
        v
Target Path:  ACTIVE
Current Path: STANDBY or DRAINING
```

二つのPathを同時に準備することは、同一パケットを二重送信することを意味しない。本基盤は初期仕様でPacket Duplicationを行わない。

旧PathはPost-checkおよびRollback Windowが終了するまで削除しないことを推奨する。

---

## 15. 状態モデル

### 15.1 Desired State

IntentとPolicyから導出された目標状態。

### 15.2 Applied State

ControllerがAdapterへの正常な適用応答を受け、適用済みと記録した状態。

Applied Stateは実際の転送状態を保証しない。Adapter応答後にOSS内部で失敗する可能性があるため、Observed Stateによる確認が必要である。

### 15.3 Observed State

OSSイベント、状態Dump、ProbeおよびAgent報告から得られた状態。

### 15.4 State Confidence

Observed Stateには、情報源と鮮度を記録する。

```yaml
observation:
  source: vici-event
  observed_at: timestamp
  confidence: authoritative
```

イベントのみでは欠落を検出できないため、Controllerはイベント購読に加え、定期的なFull Resyncを実行しなければならない。

### 15.5 Error State

最低限次のエラーコードを定義する。

- `INVALID_INTENT`
- `INTENT_CONFLICT`
- `NO_ELIGIBLE_PATH`
- `TUNNEL_ESTABLISH_TIMEOUT`
- `PATH_VALIDATION_FAILED`
- `FORWARDING_UPDATE_FAILED`
- `STATE_CONFLICT`
- `OBSERVATION_STALE`
- `TRANSITION_DEADLINE_EXCEEDED`
- `ROLLBACK_FAILED`
- `CONTROLLER_RECOVERY_REQUIRED`

エラーは機械可読コード、説明、発生時刻、関連Resource IDおよび再試行可否を持つ。

---

## 16. Tunnel状態機械

```text
ABSENT
CONFIGURED
ESTABLISHING
ESTABLISHED
REKEYING
DEGRADED
FAILED
DELETING
UNKNOWN
```

代表遷移:

- `ABSENT -> CONFIGURED`: 設定適用成功
- `CONFIGURED -> ESTABLISHING`: 接続開始
- `ESTABLISHING -> ESTABLISHED`: CHILD SA確立確認
- `ESTABLISHED -> REKEYING`: Rekey開始
- `REKEYING -> ESTABLISHED`: 新SAへの移行確認
- 任意状態`-> FAILED`: 明確な失敗または期限超過
- 任意状態`-> UNKNOWN`: 観測経路喪失または情報期限切れ

IKE SAが存在しても、必要なTraffic Selectorを持つCHILD SAが存在しない場合、対象Tunnelを`ESTABLISHED`としてはならない。

---

## 17. Path状態機械

```text
UNAVAILABLE
PREPARING
VALIDATING
READY
ACTIVE
STANDBY
DRAINING
DEGRADED
FAILED
UNKNOWN
```

- `UNAVAILABLE`: 必要な構成が存在しない。
- `PREPARING`: Tunnel、ForwardingまたはRouteを準備中。
- `VALIDATING`: End-to-End利用可能性を検証中。
- `READY`: 検証済みで切替可能。
- `ACTIVE`: 対象Trafficが現在使用中。
- `STANDBY`: 利用可能だが対象Trafficには未使用。
- `DRAINING`: 旧パケットまたは既存フローの終了待ち。
- `DEGRADED`: 通信可能だが品質または一部要件に違反。
- `FAILED`: 利用不能が確定。
- `UNKNOWN`: 状態を信頼できない。

同一Traffic Scopeに対して`ACTIVE`となるPathは、Flow Preserve期間を除き一つでなければならない。

---

## 18. Transition状態機械

```text
PENDING
SELECTING
PREPARING
VALIDATING
READY
COMMITTING
DRAINING
SWITCHING
VERIFYING
COMPLETED
ROLLING_BACK
ROLLED_BACK
FAILED
CANCELLED
```

### 18.1 正常系

```text
PENDING
 -> SELECTING
 -> PREPARING
 -> VALIDATING
 -> READY
 -> COMMITTING
 -> SWITCHING or DRAINING
 -> VERIFYING
 -> COMPLETED
```

### 18.2 失敗系

Current Pathが正常な場合:

```text
PREPARING / VALIDATING / READY
 -> FAILED
```

この段階ではForwardingを変更していないため、必ずしもRollback操作を必要としない。

Forwarding変更後:

```text
COMMITTING / SWITCHING / VERIFYING
 -> ROLLING_BACK
 -> ROLLED_BACK
```

Rollbackに失敗した場合:

```text
ROLLING_BACK
 -> FAILED
```

`FAILED`時は運用者介入を要求し、残存状態を記録する。

### 18.3 Deadline

各Transitionは絶対期限または相対`deadline_ms`を持つ。期限超過時は現在フェーズに応じて中止またはRollbackを開始する。

---

## 19. 両端協調プロトコル

拠点間Path切替では、両端の状態不一致によるブラックホールまたは非対称経路を抑えるため、次の論理フェーズを使用する。

```text
Prepare -> Ready -> Commit -> Confirm
```

### 19.1 Prepare

各Endpoint Agentは、Target Pathに必要なTunnel、Forwarding、RouteおよびProbe経路を準備する。

### 19.2 Ready

各Agentは次を満たした場合のみReadyを報告する。

- 必要なTunnelが確立済み
- 必要なForwarding設定が適用済み
- End-to-Endまたは定義済みValidationが成功
- Observationが期限内

### 19.3 Commit

Controllerは必要なすべてのParticipantがReadyであることを確認してCommitを発行する。

Commitは`transition_id`と`generation`を持ち、古いCommitの再実行を防止する。

### 19.4 Confirm

各Agentは切替後のObserved Stateを報告する。単なるAPI成功応答をConfirmとしてはならない。

### 19.5 整合性モデル

厳密な分散トランザクションや原子的な同時切替は保証しない。Timeout、世代番号、冪等操作、Post-checkおよびRollbackによる実用的な整合性を目標とする。

---

## 20. パケット順序と通信影響

Path切替では、旧Path上のPacketより新Path上の後続Packetが先に到着する可能性がある。

本基盤は、異なるIPsec SA間で共通の順序番号を導入しない。また受信側の独自並べ替え機構を必須としない。

通信特性に応じて次を選択する。

- 切替速度優先: Immediate
- 短い停止を許容し順序乱れ低減: Graceful
- 既存フロー継続優先: Flow Preserve

評価では最低限次を測定する。

- Packet Loss
- Packet Reordering
- TCP Retransmission
- UDP Loss
- Application-visible interruption

---

## 21. Rollback

### 21.1 Rollback条件

- Target Tunnel確立失敗
- Path Validation失敗
- Forwarding更新失敗
- Commit Confirm欠落
- 切替後のReachability喪失
- 切替後の品質がRollback閾値を超過
- Participant状態不一致
- Transition Deadline超過

### 21.2 Rollback Path

明示された`rollback_path`を優先する。指定がない場合、切替開始時のCurrent PathをRollback Pathとして固定する。

Transition中に自動選択結果が変化しても、実行中TransitionのRollback Pathを暗黙変更してはならない。

### 21.3 Rollback Window

切替後、一定期間は旧Pathを`STANDBY`として維持する。

```yaml
rollback_policy:
  window_ms: 30000
  keep_old_tunnels: true
```

Rollback Window終了後に旧Pathを削除する場合も、別のReconciliation操作として実行する。

---

## 22. Reconciliation Runtime

### 22.1 責務

- Eventの受信と永続化
- 状態の正規化
- Resource間依存関係の解決
- Desired/Applied/Observed差分計算
- 冪等操作の発行
- 操作結果の追跡
- Full Resync
- 状態競合の検出
- Retry、BackoffおよびDeadline管理
- Controller再起動後の復旧

### 22.2 EventとFull Resync

イベント駆動のみを状態の正確性の根拠としてはならない。イベント欠落、順序逆転およびAdapter再接続に備え、定期的に各OSSの完全状態を取得して再同期する。

### 22.3 Retry

Retryは指数Backoffと上限を持つ。同じ操作を無制限に繰り返してはならない。

操作は次に分類する。

- Retry Safe: 冪等で自動再試行可能
- Verify Before Retry: 再試行前にObserved State確認が必要
- Manual Recovery: 自動再試行禁止

### 22.4 Controller再起動

Controller再起動後は、保存されたApplied Stateを正しいと仮定せず、全AdapterからObserved Stateを再取得する。

実行中Transitionが存在する場合、次を判定する。

- Target未Commit: Transitionを中止しCurrent Path維持
- Commit済み・未Confirm: 両端状態を再観測し継続またはRollback
- 状態判定不能: 新規切替を禁止し`RECOVERY_REQUIRED`

---

## 23. Eventモデル

すべてのEventは最低限次を持つ。

```yaml
event:
  id: uuid
  type: CHILD_SA_ESTABLISHED
  source: strongswan-adapter-site-a
  subject_ref: tunnel-site-a-site-b
  observed_at: timestamp
  received_at: timestamp
  sequence: optional-integer
  transition_id: optional-string
  payload: {}
```

### 23.1 主要Event

Intent:

- `INTENT_CREATED`
- `INTENT_UPDATED`
- `INTENT_DELETED`
- `INTENT_CONFLICT_DETECTED`

strongSwan:

- `IKE_SA_ESTABLISHED`
- `IKE_SA_DELETED`
- `CHILD_SA_ESTABLISHED`
- `CHILD_SA_REKEYED`
- `CHILD_SA_DELETED`
- `CHILD_SA_FAILED`
- `PEER_UNREACHABLE`

VPP:

- `INTERFACE_STATE_CHANGED`
- `ROUTE_APPLIED`
- `ROUTE_REMOVED`
- `FORWARDING_APPLY_FAILED`
- `COUNTER_THRESHOLD_EXCEEDED`

Health:

- `PATH_HEALTH_CHANGED`
- `VALIDATION_SUCCEEDED`
- `VALIDATION_FAILED`
- `OBSERVATION_EXPIRED`

Transition:

- `TRANSITION_STARTED`
- `TRANSITION_PHASE_CHANGED`
- `TRANSITION_COMPLETED`
- `ROLLBACK_STARTED`
- `ROLLBACK_COMPLETED`
- `TRANSITION_FAILED`

Event順序を完全には信頼せず、Resource VersionまたはObserved State再取得によって状態を確定する。

---

## 24. コンポーネント責務

### 24.1 Intent API

- Intent CRUD
- Schema検証
- 参照整合性検証
- 認証・認可
- 楽観的排他制御
- 更新履歴

### 24.2 Policy Engine

- IntentからDesired Stateを生成
- 静的制約評価
- Fallback方針決定
- Selection/Transition Pluginの選択

### 24.3 Path Selection Engine

- Candidate集合の確定
- 制約による除外
- 順位付け
- Tie-break
- 安定化
- Explain Record生成

### 24.4 Transition Engine

- Transition Resource生成
- Participant管理
- Prepare/Ready/Commit/Confirm
- Strategy固有処理
- Deadline管理
- Post-check
- Rollback

### 24.5 State Store

- Resource永続化
- Event永続化
- Resource Version管理
- Transition復旧情報保持

初期実装では単一Controllerと単一の整合性あるStoreを前提としてよい。複数ControllerのActive/Activeは初期対象外とする。

### 24.6 strongSwan Adapter

- 設定の適用・削除
- IKE SAおよびCHILD SAの状態取得
- VICI Event購読
- Traffic Selector照合
- Rekeyおよび削除イベントの正規化
- Full State Dump

### 24.7 VPP Adapter

- Interface状態取得
- Route/FIB操作
- Policy-based Forwardingの適用
- 必要なCounter取得
- Flow Preserve実装時の分類・状態管理
- Full State Dump

### 24.8 FRRouting Adapter

拡張段階で次を担当する。

- Route監視
- BGP/OSPF Neighbor状態
- BFD状態
- Route Advertisement制御
- Next-hop変更

### 24.9 Health Probe

- Path固有の送信元/宛先を用いたReachability確認
- RTT、Loss、Jitter計測
- Probe結果の署名または送信元確認
- 測定値鮮度管理

---

## 25. Plugin拡張モデル

次を交換可能なPlugin境界とする。

- `PathSelector`
- `ConstraintEvaluator`
- `TransitionStrategy`
- `HealthEvaluator`
- `PathValidator`
- `DataPlaneAdapter`

Pluginはバージョン付きインターフェースを実装し、入力と出力が再現可能でなければならない。

PathSelectorは直接Data Planeを変更してはならない。TransitionStrategyはCandidate Path集合を変更してはならない。責務を越えた副作用を禁止する。

Plugin実行失敗時は、Controller全体を停止させず、対象IntentまたはTransitionを失敗状態にする。

---

## 26. セキュリティ要件

### 26.1 Control Channel

Controller-AgentおよびController-Adapter間通信は相互認証し、暗号化しなければならない。

### 26.2 認可

最低限次の権限を分離する。

- Intent閲覧
- Intent変更
- 手動Path切替
- Node/Tunnel/Path定義変更
- Plugin登録
- Audit Log閲覧

### 26.3 Secret管理

PSK、秘密鍵および証明書秘密鍵をIntentまたは通常のAudit Logへ平文保存してはならない。

### 26.4 Replay防止

AgentへのCommit命令は`transition_id`、世代番号および期限を含み、古い命令の再適用を拒否しなければならない。

### 26.5 Audit

次を改ざん検知可能な形で記録することを推奨する。

- 操作者
- Intent変更前後
- Path選択理由
- Adapter操作
- Transition各Phase
- Rollback理由
- 手動介入

---

## 27. 可用性と障害時動作

### 27.1 Controller停止

Controller停止時、既存Data Planeは最後の正常状態を維持する。Controller不在を理由に自動的にTunnelまたはRouteを削除してはならない。

### 27.2 Agent停止

Agent状態が不明なNodeを含む新規Transitionを開始しない。実行中TransitionではDeadline後にRollbackまたはRecovery Requiredへ遷移する。

### 27.3 Store障害

永続化できない状態で新しいCommitを開始してはならない。読み取り専用の状態表示は許可してよい。

### 27.4 Split Brain

初期実装で複数Controllerを許可しない。将来許可する場合はLeader ElectionおよびFence Tokenを必須とする。

---

## 28. 既存基盤との比較

### 28.1 ONOS

ONOSは、ネットワーク装置、リンク、Host、FlowおよびIntentを抽象化し、SDN制御を行う基盤である。本基盤とは、Intent、トポロジ、PathおよびProvider/Adapterという考え方で共通する。

主要な差は次である。

| 項目 | ONOS | 本基盤 |
|---|---|---|
| 主対象 | Device、Link、Flow、Network Application | VPN SA、Tunnel、Segment、Path、Transition、異種OSS |
| 主な制御 | ネットワーク装置へのFlow/設定適用 | strongSwan、VPP、FRRにまたがる複合状態遷移 |
| Path | トポロジ上の経路 | 準備・検証・切替・Rollbackを持つ実行Resource |
| 遷移方式 | Application実装に依存 | Immediate、Graceful、Flow Preserveを標準概念化 |
| 状態同期 | Device/Provider中心 | Desired/Applied/Observedと複数OSS状態機械の同期 |

本基盤の機能をONOS ApplicationまたはProviderとして実装することは理論上可能である。しかし本研究では、VPN SAライフサイクル、複数OSSの部分適用およびPath Transitionを第一級Resourceとして扱うため、ONOS内部モデルへ依存しない専用ランタイムを設計する。

将来、ONOSを上位IntentまたはTopology提供元として接続してよい。

### 28.2 Kubernetes

Kubernetesは、ResourceのSpecでDesired Stateを表し、ControllerがCurrent StateをDesired Stateへ近づける制御ループを採用する。本基盤はこの宣言的ResourceとReconciliationの考え方を採用する。

ただし、Kubernetesの一般的なReconciliationでは、最終状態への収束経路をController実装へ委ねる。一方、ネットワークPath切替では中間操作の順序がPacket Loss、非対称経路およびセッション維持へ直接影響する。

そのため、本基盤はTransitionを独立Resourceとし、Prepare、Validate、Commit、VerifyおよびRollbackを明示的に管理する。

| 項目 | Kubernetes | 本基盤 |
|---|---|---|
| Resource例 | Pod、Service、Deployment | Node、Tunnel、Segment、Path、Transition |
| 基本モデル | Spec/Statusと制御ループ | Desired/Applied/Observedと制御ループ |
| 中間遷移 | ControllerまたはStrategy依存 | Transition Resourceで明示 |
| 対象 | Containerized Workload | Network OSSとData Plane |
| 両端協調 | 一般には必須でない | 拠点間切替で主要要件 |
| Flow/Packet影響 | 間接的 | 直接的な評価対象 |

Kubernetes CRDとOperatorで本基盤の一部を実装することは可能である。ただし、Kubernetesを必須実行基盤とはせず、将来の上位API Adapterとして連携可能にする。

### 28.3 位置付け

本基盤は、ONOSに近いネットワーク抽象化と、Kubernetesに近い宣言的Reconciliationを取り入れ、異種ネットワークOSSにまたがるPath Transitionを専門領域とする。

---

## 29. 新規性と研究課題

本プロジェクトの新規性は、Intent、SDN、IPsec、Reconciliationまたはフェイルオーバーを個別に発明することではない。次の統合と実行モデルに置く。

1. VPN、ForwardingおよびRouting OSSを共通Resourceへ正規化すること
2. Desired、Applied、ObservedおよびTransitionを分離すること
3. Path SelectionとPath Transitionを独立したPolicyとして扱うこと
4. Pathを準備・検証・切替・Rollbackを持つ実行Resourceとすること
5. 選択・遷移・評価・Adapterを交換可能にすること
6. 新しい制御アルゴリズムを同一基盤上で比較できること

主要な研究課題は次である。

- Event欠落・順序逆転を含む状態同期
- 複数OSSにまたがる部分適用
- Controller再起動中のTransition復旧
- 両端切替の実用的な整合性
- Path Healthと測定値鮮度の扱い
- Flow Preserve時の状態規模と復旧
- 選択理由および失敗理由の説明可能性

---

## 30. 非目標

初期段階では次を目標としない。

- 商用VPNルータの完全代替
- 大規模商用SLAの保証
- 完全無停止、完全無損失または完全順序保証
- 独自暗号方式または独自VPNプロトコル
- AIによる自律ネットワーク判断
- 高機能GUI
- Zero TrustまたはSASEの全機能
- 多数ベンダー機器の統一管理
- 任意トポロジの無制限自動生成
- ControllerのActive/Activeクラスタ

---

## 31. 初期実装範囲

### 31.1 トポロジ

```text
             Hub
            /   \
       Site A   Site B

       Site A -------- Site B
               Direct

       Site A -- Relay C -- Site B
```

### 31.2 Candidate Path

- `path-via-hub`: Site A -> Hub -> Site B
- `path-direct`: Site A -> Site B
- `path-via-relay-c`: Site A -> Relay C -> Site B

### 31.3 使用コンポーネント

必須:

- Controller
- State Store
- strongSwan Adapter
- VPP Adapter
- Health Probe
- Site Agent

後段:

- FRRouting Adapter

### 31.4 第1段階

- Resource CRUD
- Explicit Selection
- Priority Selection
- Tunnel準備
- Path Validation
- Immediate Transition
- Graceful Transition
- Rollback
- Full Resync
- Audit/Explain

### 31.5 第2段階

- Evaluated Selection
- ヒステリシス・Hold-down
- 自動Fallback
- Flow Preserve
- Controller再起動復旧の強化
- Plugin API

### 31.6 第3段階

- FRRouting連携
- BFD連携
- 複数Waypoint
- Service Chaining
- Kubernetes/ONOS Adapter
- 自動Path生成の制限付き実験

---

## 32. 初期実証シナリオ

1. Hub PathからDirect Pathへの明示切替
2. Direct PathからHub Pathへの明示復帰
3. Hub PathからRelay Pathへの切替
4. Target Tunnel確立失敗時のCurrent Path維持
5. Validation失敗時のTransition中止
6. Immediate切替時の通信影響測定
7. Graceful切替時の通信影響測定
8. 切替後Post-check失敗時のRollback
9. Direct障害時のPriority Fallback
10. Relay障害時の別Path選択
11. Controller再起動後の状態再同期
12. Event欠落を想定したFull Resync
13. 古いCommit命令の拒否
14. 同一Traffic ScopeへのIntent競合検出

Flow Preserveは第2段階の実証シナリオとする。

---

## 33. 評価項目

### 33.1 制御性能

- Intent受付から選択開始までの時間
- Path選択処理時間
- Prepare時間
- Validation時間
- CommitからObserved Active確認までの時間
- Full Resync時間

### 33.2 通信影響

- Application-visible interruption
- Packet Loss
- Packet Reordering
- TCP Retransmission
- UDP Loss
- Throughput変化

### 33.3 障害復旧

- 障害検出時間
- Fallback開始時間
- Fallback完了時間
- Blackhole継続時間
- Rollback成功率
- Controller再起動後の復旧時間

### 33.4 状態同期

- Event反映遅延
- Event欠落からFull Resyncでの修復時間
- Applied/Observed不一致検出時間
- Stale Observation検出率
- 部分適用からの復旧成功率

### 33.5 リソース

- Controller CPU/Memory
- Agent CPU/Memory
- VPP処理量
- IPsec Throughput
- 同時Tunnel数
- 同時Transition数
- Flow Preserve時の状態数

---

## 34. 成功条件

初期プロトタイプの成功条件を次とする。

1. 三つの事前定義Pathを共通モデルで管理できる。
2. ExplicitおよびPriorityでTarget Pathを再現可能に選択できる。
3. Target PathをCurrent Path維持中に準備・検証できる。
4. ImmediateおよびGracefulで切替できる。
5. 切替後失敗時に旧PathへRollbackできる。
6. Controller再起動後にObserved Stateを再取得できる。
7. 選択理由、Transition Phaseおよび失敗理由を追跡できる。
8. strongSwanとVPPの状態不一致を検出できる。
9. 長時間のBlackholeを発生させないことを、定義した試験条件で確認できる。

完全無損失または完全無瞬断は成功条件に含めない。

---

## 35. Explainおよび監査仕様

Path選択ごとに次を記録する。

```yaml
selection_explanation:
  intent_id: intent-site-a-site-b
  current_path: path-via-hub
  candidates:
    - path-direct
    - path-via-relay-c
    - path-via-hub
  excluded:
    - path: path-direct
      reason_code: LOSS_CONSTRAINT_EXCEEDED
  ranked:
    - path: path-via-relay-c
      metrics:
        loss_percent: 0.1
        rtt_ms: 30
    - path: path-via-hub
      metrics:
        loss_percent: 0.1
        rtt_ms: 45
  selected_path: path-via-relay-c
  tie_break_applied: false
  transition_strategy: graceful
```

Explain情報は制御判断の入力を記録するが、秘密鍵、PSKまたは機微な認証情報を含めてはならない。

---

## 36. 将来拡張

### 36.1 VPN/Data Plane

- WireGuard Adapter
- OpenVPN Adapter
- Linux Kernel Routing Adapter
- eBPF/XDP Adapter
- OVS Adapter
- P4Runtime Adapter

### 36.2 Routing

- FRRouting
- BIRD
- GoBGP

### 36.3 Monitoring

- Prometheus Exporter
- IPFIX
- sFlow
- eBPF Telemetry

### 36.4 上位連携

- Kubernetes CRD/Operator Adapter
- ONOS Intent/Topology Adapter
- GitOpsによるIntent管理

### 36.5 応用

- Security Gateway経由制御
- Service Chaining
- 地理・法域制約
- Cost/Energy-aware Selection
- Multi-cloud Interconnect

---

## 37. 最終的なプロジェクト定義

本プロジェクトは、strongSwan、FD.io VPPおよび将来のFRRoutingなど、異なる状態モデルを持つネットワークOSSをデータプレーンとして利用し、それらを宣言的Intent、統一Resourceモデル、イベント駆動Reconciliation、Path Selection PolicyおよびPath Transition Policyによって統合する、オープンなネットワーク制御ランタイムである。

ネットワーク経路はHub、Direct、Relayという個別機能ではなく、Node、Waypoint、SegmentおよびTunnelから構成されるPathとして表現する。

Controllerは、管理者が許可したCandidate Path Setから制約、Health、品質、優先順位および安定化条件に基づいて一つのTarget Pathを決定する。経路変更時にはImmediate、GracefulまたはFlow PreserveのTransition Strategyを適用し、Target Pathの準備、検証、両端協調、切替後確認およびRollbackを管理する。

本基盤の価値は、特定VPN製品の代替に限定されない。ネットワーク制御アルゴリズム、状態同期方式およびPath遷移方式を交換・比較・検証できる、拡張可能な研究・実証基盤を提供することにある。
