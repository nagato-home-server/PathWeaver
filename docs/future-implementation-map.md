# Future Implementation Map

この文書は、今後どこに何を実装するかを迷わないための作業地図です。

現時点の実装は、YAMLからIntent / Path / Tunnel / VPP edgeを読み、controllerがPathを選び、strongSwan / VPP向けruntime scriptを生成し、Linux VM上でdirect / hub fallback / VPP forwarding / integrated runtime smokeまで確認できています。

次は、scenario harnessで手動実験しているイベント注入を、よりcontroller本体に近い `eventnetd --once` へ寄せます。

## 1. 最優先で実装する場所

### 1.1 `eventnetd --once`

追加予定:

- `examples/eventnetd.c`
- `scripts/vm-eventnetd-once-smoke.sh`

更新予定:

- `CMakeLists.txt`
- `scripts/vm-build-cc.sh`
- `scripts/demo-mitou.sh`

目的:

- YAML configを読む。
- event fileを読む。
- controllerにhealth / failure / active pathを反映する。
- Pathを再選択する。
- 必要なら既存runtime generatorにつなぐ。
- `out/eventnetd/status.jsonl` に判断結果を残す。

最初のCLI案:

```sh
build-linux-cc/eventnetd samples/linux-vm-netns.yaml \
  --once \
  --events out/eventnetd/events.txt \
  --status-json out/eventnetd/status.jsonl \
  --generate-runtime
```

最初のevent file案:

```text
active_path path-direct
path_failed path-direct
health path-via-hub healthy rtt=12 loss=0.0
health path-via-relay-c healthy rtt=30 loss=0.1
```

期待する最小出力:

```json
{"intent":"intent-a-b","selected_path":"path-via-hub","transition_state":"completed","reason":"active path path-direct failed; using fallback path-via-hub"}
```

## 2. event parserを置く場所

最初は `examples/eventnetd.c` 内に小さく実装してよいです。

ただし、行数が増えたら以下へ分離します。

- `include/eventnet/event_file.h`
- `src/event_file.c`
- `tests/test_event_file.c`

想定する関数:

```c
int en_event_file_load(const char *path, en_event_batch_t *batch, en_error_t *error);
void en_event_batch_init(en_event_batch_t *batch);
void en_event_batch_free(en_event_batch_t *batch);
```

扱うevent種別:

- `active_path <path_id>`
- `path_failed <path_id>`
- `path_recovered <path_id>`
- `health <path_id> <state> rtt=<ms> loss=<percent>`

この層は、将来strongSwan / VPP / health probe adapterから来るObserved Stateの代替入力です。

## 3. reconcile coreを置く場所

現在、scenario実験用のevent注入と選択処理は主に `examples/eventnet_scenario.c` にあります。

`eventnetd` でも同じ判断を使うため、次段階で以下へ切り出します。

- `include/eventnet/reconcile.h`
- `src/reconcile.c`
- `tests/test_reconcile.c`

想定する関数:

```c
int en_reconcile_once(
    en_controller_t *controller,
    const en_intent_t *intent,
    const en_event_batch_t *events,
    en_reconcile_result_t *result,
    en_error_t *error);
```

`en_reconcile_once()` が担うこと:

- event batchをcontroller stateへ反映する。
- active pathのfailureを解釈する。
- `src/path_selection.c` の選択器を呼ぶ。
- transition stateを更新する。
- explain outputに渡す結果を作る。

既存で読むべき場所:

- `src/controller.c`
- `src/path_selection.c`
- `src/transition.c`
- `include/eventnet/controller.h`
- `include/eventnet/path_selection.h`
- `include/eventnet/transition.h`

## 4. status / explain出力を置く場所

現在、JSON Lines出力は `examples/eventnet_scenario.c` 側にあります。

将来はscenario harnessと `eventnetd` の両方で使うため、以下に切り出します。

- `include/eventnet/explain_json.h`
- `src/explain_json.c`
- `tests/test_explain_json.c`

想定する関数:

```c
int en_explain_json_write_line(
    FILE *stream,
    const en_reconcile_result_t *result,
    en_error_t *error);
```

初期出力先:

- `out/eventnetd/status.jsonl`

将来の出力先:

- local file
- HTTP status API
- GUI backend
- demo用trace log

## 5. runtime generationを整理する場所

現在、Linux VM用runtime script生成は `examples/netns_plan.c` に集中しています。

`eventnetd --once --generate-runtime` では、まず既存binary / scriptを呼ぶ形で十分です。

その後、共通library化するなら以下へ移します。

- `include/eventnet/runtime_plan.h`
- `src/runtime_plan.c`
- `tests/test_runtime_plan.c`

想定する関数:

```c
int en_runtime_plan_generate_netns(
    const en_runtime_plan_input_t *input,
    en_runtime_plan_output_t *output,
    en_error_t *error);
```

生成物:

- `out/netns-runtime/apply-selected.sh`
- `out/netns-runtime/apply-integrated.sh`
- `out/netns-runtime/selected-path.txt`
- `out/netns-runtime/vpp-route-plan.sh`
- `out/netns-runtime/vpp-netns-route-plan.sh`

## 6. adapter実装を置く場所

`eventnetd --once` の後に進める領域です。

### 6.1 strongSwan VICI adapter

追加予定:

- `include/eventnet/strongswan_vici_adapter.h`
- `src/strongswan_vici_adapter.c`

役割:

- IKE SA / CHILD SA一覧を取得する。
- CHILD SA up/downをObserved Stateへ変換する。
- DPD / rekey / delete eventをfailure / recovery eventへ変換する。

未踏提出では、VICI購読の本格実装までは必須ではありません。
ただし、設計上の接合部としてheaderとmock実装を置けると説明しやすいです。

### 6.2 VPP adapter

追加予定:

- `include/eventnet/vpp_adapter.h`
- `src/vppctl_adapter.c`
- 将来: `src/vpp_api_adapter.c`

役割:

- route applyの成功 / 失敗をObserved Stateへ反映する。
- interface状態を取得する。
- FIB / route存在確認をする。
- counterをhealth評価へ渡す。

未踏提出では、まず `vppctl` command adapterで十分です。
VPP binary APIは後段です。

### 6.3 Health probe adapter

追加予定:

- `include/eventnet/health_probe.h`
- `src/health_probe_ping.c`

役割:

- pingでRTT / lossを測る。
- consecutive failure / recoveryを作る。
- `en_path_health_t` へ変換する。

将来:

- BFD
- FRRouting連携
- VPP counter連携

## 7. daemon loopを置く場所

`eventnetd --once` が動いてから実装します。

追加 / 拡張予定:

- `examples/eventnetd.c`
- `src/event_loop.c`
- `include/eventnet/event_loop.h`

CLI案:

```sh
build-linux-cc/eventnetd samples/linux-vm-netns.yaml \
  --loop \
  --interval-ms 1000 \
  --events out/eventnetd/events.txt \
  --status-json out/eventnetd/status.jsonl
```

必要な処理:

- interval実行
- config reload
- signal handling
- last applied pathの保持
- rollback / retry / backoff
- hold-down / hysteresis

## 8. 未踏提出までに実装する順番

推奨順:

1. `examples/eventnetd.c` に `--once` を作る。
2. `out/eventnetd/events.txt` を読む簡易event parserを作る。
3. `out/eventnetd/status.jsonl` を出す。
4. `--generate-runtime` で既存netns runtime生成へつなぐ。
5. `scripts/vm-eventnetd-once-smoke.sh` を追加する。
6. `scripts/demo-mitou.sh` に `eventnetd --once` demoを足す。
7. `src/reconcile.c` へ共通化する。
8. `src/explain_json.c` へ共通化する。
9. health条件のscenarioを増やす。
10. 提出資料用に、設計との差分と到達点を図にする。

## 9. 後回しでよい場所

未踏提出の第一段階では、以下は実装しなくてよいです。

- GUI
- FRRouting本統合
- VPP binary API
- strongSwan VICI event購読
- Flow Preserve本実装
- systemd service化
- 永続DB
- HA controller

ただし、上記の接合部名を文書とheaderに残しておくと、設計の拡張先を説明しやすくなります。

## 10. 判断基準

次の実装に進むかどうかは、以下で判断します。

- `eventnetd --once` がYAMLとevent fileから同じ選択結果を出せる。
- `status.jsonl` だけを見て、なぜそのPathになったか説明できる。
- `--generate-runtime` で既存のdirect / fallback統合runtime smokeへ接続できる。
- scenario harnessと `eventnetd` が同じreconcile coreを使い始めている。

この状態になれば、未踏提出向けには「設計だけでなく、イベント駆動controllerとして動き始めている」と言えます。
