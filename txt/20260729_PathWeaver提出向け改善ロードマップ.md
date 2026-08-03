# PathWeaver 未踏提出向け改善ロードマップ

作成日: 2026-07-29

目的:

`awesome-mitou` の良い作法を取り入れ、PathWeaverを「動く研究実装」から「第三者に見せられる未踏提出プロトタイプ」へ近づける。

## 1. 現状の強み

PathWeaverは、すでに以下を実装・確認している。

- YAMLでIntent / Path / Tunnel / VPP edgeを宣言できる。
- C controllerがpriority / fallback / evaluated policyでPathを選択できる。
- scenario harnessでdirect障害、hub fallback、direct回復、品質条件評価を再現できる。
- Explain JSONLで選択理由を保存できる。
- generated runtimeでstrongSwan IPsecとVPP forwardingを連続制御できる。
- Linux VMでdirect IPsec、hub IPsec、VPP forwarding、integrated runtime smokeを確認している。

これは未踏提出向けに十分強い土台である。

## 2. 現状の弱み

外から見たときに弱い点:

- CIがない。
- READMEの冒頭だけでは成果が伝わり切らない。
- demoの最短ルートが分かりにくい。
- 実機VMで動いた結果がREADMEから見えにくい。
- テストが1ファイルに寄っている。
- JSONL schemaが明文化されていない。
- security quick fixがまだ残っている。
- `eventnetd --once` が未実装。

## 3. 改善方針

大きな機能をむやみに増やすより、以下を優先する。

1. 見せ方
2. CI
3. security
4. eventnetd
5. test整理
6. schema化

## 4. Milestone A: 見せ方

目的:

初見の審査員・共同作業者が、30秒で価値を理解できるようにする。

やること:

- README冒頭に「これは何か」を1文で書く。
- 「現在動くこと」を箇条書きで出す。
- 「一発デモ」を最上部近くに置く。
- Mermaid図を追加する。
- `docs/mitou-submission-status.md` への導線を強くする。

README冒頭案:

```md
PathWeaver is an event-driven network controller prototype that selects IPsec/VPP paths from declarative YAML intents and generates/runs strongSwan + VPP runtime plans.
```

日本語案:

```md
PathWeaverは、YAMLで宣言したIntentからIPsec/VPP経路を選択し、strongSwanとVPPのruntime planを生成・適用するイベント駆動ネットワーク制御基盤のプロトタイプです。
```

## 5. Milestone B: CI

目的:

GitHub上で、少なくともC実装と非root scenario harnessが壊れていないことを示す。

追加ファイル:

- `.github/workflows/ci.yml`

実行内容:

```sh
sudo apt-get update
sudo apt-get install -y build-essential
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

CIに入れないもの:

- `sudo sh scripts/vm-netns-setup.sh`
- strongSwan daemon起動
- VPP daemon起動
- real IPsec/VPP smoke

理由:

CIでdaemon/network namespace/VPPを安定運用するのは時間がかかる。
未踏提出前は、まず軽量CIで「C controllerは壊れていない」ことを見せる。

## 6. Milestone C: Security quick fix

目的:

ネットワーク制御基盤として、最低限の安全姿勢を示す。

優先修正:

1. secret入り `swanctl.conf` を `chmod 600` にする。
2. エラー時にsecret入りconfを表示しない。
3. `PSK=change-me` を警告または拒否する。
4. YAML由来IDのallowlist validatorを追加する。
5. 数値parseを `atoi()` / `atof()` から `strtol()` / `strtod()` に変える。

説明上の効果:

- 研究実装でも安全性を意識していると言える。
- strongSwan/VPPを扱うプロジェクトとして信頼されやすくなる。

## 7. Milestone D: eventnetd --once

目的:

scenario harnessから、本番controllerに近い入口へ進める。

追加ファイル:

- `examples/eventnetd.c`
- `scripts/vm-eventnetd-once-smoke.sh`

CLI案:

```sh
build-linux-cc/eventnetd samples/linux-vm-netns.yaml \
  --once \
  --events out/eventnetd/events.txt \
  --status-json out/eventnetd/status.jsonl \
  --generate-runtime
```

event file案:

```text
active_path path-direct
path_failed path-direct
health path-via-hub healthy rtt=12 loss=0.0
```

期待する成果:

- event fileを読む。
- controllerがfallbackを判断する。
- `status.jsonl` に理由を出す。
- runtime planを生成する。

## 8. Milestone E: JSONL schema

目的:

Explain outputを提出資料やGUI/API設計へつなげる。

追加ファイル:

- `docs/explain-jsonl-schema.md`

書く内容:

- field一覧
- sample
- meaning
- scenario harnessとの対応
- 将来の `eventnetd status API` との関係

schema案:

```json
{
  "intent": "intent-a-b",
  "selected_path": "path-via-hub",
  "transition_state": "completed",
  "reason": "active path path-direct failed; using fallback path-via-hub",
  "excluded": [
    {
      "path": "path-direct",
      "reason": "active path failed event"
    }
  ],
  "health": [
    {
      "path": "path-via-hub",
      "state": "healthy",
      "rtt_ms": 12,
      "packet_loss_percent": 0
    }
  ]
}
```

## 9. Milestone F: テスト分割

目的:

共同作業者が読みやすく、変更しやすいテスト構造にする。

分割案:

- `tests/test_path_selection.c`
- `tests/test_yaml_config.c`
- `tests/test_render_commands.c`
- `tests/test_apply_plan.c`
- `tests/test_scenario.c`
- `tests/test_security_validation.c`

まず分ける対象:

- renderer
- YAML
- path selection

## 10. 最終提出時に見せるもの

提出時に見せる順番:

1. README冒頭の図。
2. YAML Intent。
3. scenario harnessでdirect障害を注入。
4. controllerがhub fallbackを選ぶ。
5. Explain JSONLで理由を見る。
6. generated runtimeを見せる。
7. Linux VMでIPsec/VPP integrated smokeが通ったことを示す。

## 11. 直近の実装順

最短で成果が出る順:

1. CI追加。
2. README冒頭改善。
3. security quick fix。
4. Explain JSONL schema文書。
5. `eventnetd --once`。
6. test分割。

この順なら、途中で時間が尽きても提出物としての見え方が良くなる。
