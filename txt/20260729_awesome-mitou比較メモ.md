# awesome-mitou と PathWeaver の比較メモ

作成日: 2026-07-29

比較対象:

- local `awesome-mitou` checkout
- this repository

## 1. 比較の前提

`awesome-mitou` は、未踏事業の応募資料・成果報告書・成果物リンクをまとめるリポジトリである。

PathWeaver は、strongSwan、VPP、将来的にはFRRoutingなどを、Intentとイベント駆動制御で束ねるネットワーク制御基盤である。

機能分野はまったく異なるが、以下の観点では `awesome-mitou` が参考になる。

- OSSとして初見で分かりやすいか
- CI / test / lint が整っているか
- コード構造が第三者に読めるか
- 入出力契約が明確か
- 未踏提出物として信頼されやすいか

## 2. awesome-mitou の良いところ

### 2.1 READMEの入口が分かりやすい

READMEを開くと、すぐに「未踏資料リンク集」であることが分かる。

良い点:

- 目的が1文で分かる。
- 目次がある。
- 年度・事業区分・プロジェクト単位で整理されている。
- 関連リンクがまとまっている。
- コントリビュート方法がある。

PathWeaverでもREADMEはあるが、初見の人にとっては、まだ「何が一番すごいのか」「どの順で見ればよいのか」がやや分かりにくい。

### 2.2 CIがある

`awesome-mitou` はGitHub Actionsで以下を実行している。

- lint
- format check
- typecheck
- unit test
- coverage
- dead link check
- scheduled discovery
- candidate PR automation

この構成により、第三者から見て「継続的に保守されているOSS」に見える。

PathWeaverはVM上の実機smoke testは強いが、GitHub Actionsで通る軽量CIがまだ弱い。

### 2.3 coreとinfraが分かれている

`awesome-mitou` は、責務分離がきれいである。

- `src/core`
  - scoring
  - classification
  - rendering
  - pipeline
  - parser
- `src/infra`
  - GitHub client
  - logger
  - clock
  - store
- `src/cli`
  - CLI引数
  - main
  - discover orchestration

この分け方により、外部I/Oなしでcore logicをテストできる。

PathWeaverにも `src/path_selection.c`、`src/controller.c`、`src/yaml_config.c`、`src/render_commands.c` などの分離はある。

ただし、`examples/netns_plan.c` や `examples/eventnet_scenario.c` では、判断ロジック、runtime生成、shell実行がまだ混ざっている。

### 2.4 テストが細かく分かれている

`awesome-mitou` は以下のようにテストが分割されている。

- unit test
- integration test
- fixture
- snapshot

PathWeaverは `tests/test_controller.c` に多くのテストがまとまっている。

これは初期実装としては良いが、今後は以下のように分割した方が第三者に伝わりやすい。

- `tests/test_path_selection.c`
- `tests/test_yaml_config.c`
- `tests/test_render_commands.c`
- `tests/test_apply_plan.c`
- `tests/test_scenario.c`
- `tests/test_security_validation.c`

### 2.5 入出力契約が明確

`awesome-mitou` は `zod` schemaでJSON出力の形を定義している。

PathWeaverにもExplain JSONLやruntime planがあるが、まだschemaとして明文化されていない。

未踏提出では、以下を明文化すると強い。

- `selected-path.txt`
- `explain.jsonl`
- `status.jsonl`
- generated runtime script

## 3. PathWeaver の良いところ

### 3.1 実機で動くところまで行っている

PathWeaverは、単なる資料やCLIではなく、Linux VMで実際に以下を確認している。

- network namespace underlay
- direct IPsec
- hub IPsec
- VPP forwarding
- controller-generated VPP route
- IPsec + VPP integrated runtime
- direct failure fallback
- evaluated path selection
- scenario harness

この点は `awesome-mitou` と比べても技術的深さがある。

### 3.2 設計と実装が対応している

PathWeaverでは、設計文書に出てくる概念がCコードに落ちている。

- Intent
- Path
- Segment
- Tunnel
- VPP edge
- Health
- Transition
- Explain
- Adapter

これは未踏提出で重要である。

「構想だけでなく、第一段階のcontrollerとして動いている」と説明できる。

### 3.3 C実装であることに意味がある

将来的にstrongSwan / VPP / FRRoutingと接続するなら、Cでcontroller coreを持つことは説得力がある。

shell scriptだけだと「既存OSSを呼んでいるだけ」に見えやすい。

C側に以下があることで、制御基盤として説明しやすい。

- path selection
- transition
- observed / desired / applied state
- adapter boundary
- explain

## 4. PathWeaver が負けているところ

### 4.1 GitHub上での信頼感

PathWeaverはローカル/VMではかなり動く。

しかしGitHubだけ見る第三者には、以下がまだ不足している。

- CI badge
- GitHub Actions
- non-root test
- test coverageの見える化
- demo output
- 図
- release / milestone

### 4.2 最短デモが見えにくい

scriptが多いこと自体は良い。

しかし、初見の人には「どれを実行すれば一番よいのか」が分かりにくい。

必要な整理:

- 30秒デモ
- 非rootデモ
- Linux VM full demo
- 未踏提出デモ

### 4.3 security posture

現状はprototypeとして動くことを優先しているため、以下が不安である。

- `system()` 実行
- YAML由来値のcommand injection
- `swanctl.conf` injection
- `PSK=change-me`
- `chmod 644` のsecret config
- secret入りconfigのログ出力
- IDのサイレント切り詰め
- `atoi()` / `atof()` の緩いparse

ネットワーク制御基盤なので、未踏提出前に最低限のsecurity quick fixを入れると信頼性が上がる。

### 4.4 テスト構成

PathWeaverにはテストがあるが、`tests/test_controller.c` にまとまっている。

今後の共同作業では、責務ごとに分けた方がよい。

### 4.5 eventnetdがまだない

PathWeaverの主役は、最終的には `eventnetd` である。

現状はscenario harnessとruntime generatorが強い。

しかし、本番controllerに近い入口として `eventnetd --once` が欲しい。

## 5. 比較まとめ

PathWeaverが勝っている点:

- 技術的な深さ
- Linux VMでの実機実証
- strongSwan / VPPとの実接続
- Cでcontroller coreを持っていること
- 設計概念と実装概念が対応していること

awesome-mitouが勝っている点:

- READMEの入口
- CI
- test分割
- I/O分離
- 入出力契約
- OSSとしての整い方
- 第三者が安心して読める構成

結論:

PathWeaverは「中身」は強い。

一方で「外から見た信頼性」と「一発で伝わる見せ方」がまだ弱い。

未踏提出までには、大きな機能を増やすだけでなく、見せ方・CI・security・テスト整理を入れるべきである。
