# awesome-mitou から真似する部分と PathWeaver への反映

作成日: 2026-07-29

## 1. 真似する部分

### 1.1 READMEの入口

真似すること:

- README冒頭で目的を短く言い切る。
- すぐ下に「何が動くか」を置く。
- その次に「一発デモ」を置く。
- 詳細docsへのリンクを整理する。

PathWeaverで書くべきこと:

- これは何か:
  - Intentで複数IPsec/VPP経路を制御するC実装controller。
- 何が動いたか:
  - direct IPsec
  - hub fallback
  - VPP forwarding
  - IPsec + VPP integrated runtime
  - scenario harness
- どう試すか:
  - `sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml`
  - `sudo RUN_RUNTIME=1 sh scripts/demo-mitou.sh samples/linux-vm-netns.yaml`

### 1.2 CI

真似すること:

- GitHub Actionsで最低限のbuild/testを回す。
- VM依存の重いtestはCIに入れず、非rootで通る範囲に限定する。

PathWeaverで追加するもの:

- `.github/workflows/ci.yml`

CIで実行する候補:

```sh
sudo apt-get update
sudo apt-get install -y build-essential
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

CIで実行しないもの:

- strongSwan実daemon
- VPP実daemon
- network namespace root smoke

理由:

- CIではroot/network daemon環境差が大きい。
- まずはC build、unit test、scenario harnessだけを安定させる。

### 1.3 coreとI/Oの分離

真似すること:

- core logicは純粋関数に近づける。
- shell実行やdaemon操作はadapter層へ閉じ込める。
- CLIはcoreを呼ぶだけにする。

PathWeaverで分ける候補:

- `src/reconcile.c`
  - eventを受けてPathを選ぶ中核。
- `src/event_file.c`
  - event fileを読む。
- `src/explain_json.c`
  - JSONL出力。
- `src/runtime_plan.c`
  - runtime plan生成。
- `examples/eventnetd.c`
  - CLI入口。

`examples/eventnet_scenario.c` と `examples/netns_plan.c` から、徐々にcore logicを `src` へ移す。

### 1.4 テスト分割

真似すること:

- 1つの大きなtest fileに寄せすぎない。
- 領域ごとに小さくする。

PathWeaverで分割する候補:

- `tests/test_path_selection.c`
- `tests/test_yaml_config.c`
- `tests/test_render_commands.c`
- `tests/test_apply_plan.c`
- `tests/test_scenario.c`
- `tests/test_security_validation.c`
- `tests/test_event_file.c`

最初に分けるなら:

1. `tests/test_render_commands.c`
2. `tests/test_yaml_config.c`
3. `tests/test_path_selection.c`

### 1.5 入出力契約

真似すること:

- JSON出力の形を明文化する。
- CLI exit codeを明文化する。
- stdout / stderrの役割を分ける。

PathWeaverで作るもの:

- `docs/explain-jsonl-schema.md`
- `docs/cli-contract.md`

書くべき内容:

- `eventnet_scenario --explain-json`
- 将来の `eventnetd --status-json`
- `selected-path.txt`
- generated runtime script
- exit code:
  - `0`: success
  - `1`: runtime/config error
  - `2`: usage error

### 1.6 自動化

真似すること:

- 手でやる作業をscript化する。
- 定期的に確認するものをCI/automationへ置く。

PathWeaverで作る候補:

- `scripts/security-preflight.sh`
  - strongSwan/VPP version確認。
  - CVE backport確認コマンドを出す。
- `scripts/demo-output-capture.sh`
  - demo outputを `out/demo/*.txt` に保存。
- `scripts/check-generated-clean.sh`
  - generated fileが想定外にrepoへ混ざっていないか確認。

## 2. PathWeaverで優先して反映する順番

### Phase 1: 見せ方を整える

やること:

1. README冒頭を改善する。
2. Mermaid図を追加する。
3. `demo-mitou.sh` の出力例をREADMEに載せる。
4. `docs/mitou-submission-status.md` から重要部分をREADMEへ引き上げる。

狙い:

- 初見の人が30秒で価値を理解できるようにする。

### Phase 2: CIを入れる

やること:

1. `.github/workflows/ci.yml` を追加する。
2. `scripts/vm-build-cc.sh` をCIで通す。
3. `scripts/vm-eventnet-scenario-smoke.sh` をCIで通す。
4. READMEにCI badgeを追加する。

狙い:

- GitHub上で「壊れていない」ことを見せる。

### Phase 3: security quick fix

やること:

1. `swanctl.conf` を `chmod 600` にする。
2. secret入りconfigをログに出さない。
3. `PSK=change-me` を警告または拒否する。
4. YAML由来値のallowlist validatorを追加する。
5. `system()` 経路を段階的に減らす。

狙い:

- ネットワーク制御基盤としての信頼性を上げる。

### Phase 4: eventnetd --once

やること:

1. `examples/eventnetd.c` を追加する。
2. event fileを読む。
3. `status.jsonl` を出す。
4. `--generate-runtime` で既存runtime生成へ接続する。
5. `scripts/vm-eventnetd-once-smoke.sh` を追加する。

狙い:

- scenario harnessから本番controller入口へ一段進める。

### Phase 5: test分割

やること:

1. `tests/test_controller.c` からrenderer系を分離する。
2. YAML parser系を分離する。
3. path selection系を分離する。
4. security validation testを追加する。

狙い:

- 共同作業者が安心して触れる構造にする。

## 3. 真似しない部分

`awesome-mitou` から真似しなくてよいもの:

- TypeScript化
- npm package構成
- GitHub repository discovery
- PR自動生成
- dead link issue自動化

理由:

- PathWeaverはC実装であることに意味がある。
- ネットワーク制御基盤なので、主戦場はCLI/daemon/adapter/runtime smokeである。

ただし、考え方として以下は真似する。

- 外部I/Oを境界に追い出す。
- schemaを持つ。
- testを細かくする。
- CIで壊れないことを示す。
- READMEで初見に優しくする。

## 4. 最優先TODO

今すぐやるなら、この順番。

1. `.github/workflows/ci.yml` を追加する。
2. README冒頭を「30秒で分かる成果」にする。
3. `scripts/vm-netns-ipsec-*-start.sh` の `chmod 644` とsecret表示を直す。
4. `docs/explain-jsonl-schema.md` を追加する。
5. `examples/eventnetd.c` の `--once` を作る。

この5つで、未踏提出向けの見え方がかなり良くなる。
