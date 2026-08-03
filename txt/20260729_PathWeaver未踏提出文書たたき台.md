# PathWeaver 未踏提出向け文章たたき台

作成日: 2026-07-29

この文書は、未踏への提出・報告・README・発表資料に転用できるように、
PathWeaver の目的、実装状況、技術的特徴、今後の実装予定を文章として
一度大きめに書き下したもの。

後で短くする前提なので、現時点では説明を厚めにしている。

---

## 1. プロジェクト概要

PathWeaver は、複数拠点間の IPsec VPN 経路を、イベントに応じて動的に選択・切り替えるための
イベント駆動型ネットワーク制御基盤である。

従来の VPN 構成では、StrongSwan や VPP などの強力な OSS を利用して暗号化通信や高速転送を
実現できる一方で、「どの経路を使うか」「障害時にどう迂回するか」「なぜその経路を選んだか」を
アプリケーション側の意図や外部イベントに応じて扱う部分は、個別のスクリプトや手作業に寄りがちである。

PathWeaver はこの隙間を埋めることを目指す。

YAML で記述された intent、path、tunnel、endpoint、health 条件を読み取り、
コントローラが現在利用すべき経路を選択する。
選択結果から StrongSwan の接続設定、VPP のルーティング設定、Linux network namespace 上での
検証用 runtime script を生成し、実際に通信可能な経路として適用できるようにする。

最初の実装段階では、GUI や大規模クラスタ管理ではなく、C 言語によるコントローラコア、
YAML との接合部、StrongSwan/VPP との接合部、Linux VM 上で再現できる検証環境を優先している。

---

## 2. 解きたい課題

### 2.1 VPN は作れるが、意図に基づく切り替えが難しい

StrongSwan を使えば IPsec tunnel を構成できる。
VPP を使えば高性能な packet forwarding を実現できる。

しかし、実運用では tunnel が存在するだけでは不十分である。

- direct 経路が使えるなら direct を使いたい。
- direct 経路が落ちたら hub 経由へ切り替えたい。
- 遅延や損失が大きい場合は relay 経路を選びたい。
- 切り替えた理由を後から確認したい。
- 設定変更のたびに手で swanctl や vppctl を打ちたくない。

このような要求は、VPN daemon や forwarding plane そのものよりも一段上の
「制御ロジック」の問題である。

PathWeaver は、IPsec や VPP を置き換えるのではなく、それらを制御対象として扱う。
ネットワークの状態や外部イベントを受け取り、利用者が YAML で書いた意図に従って
経路選択と適用計画を生成する。

### 2.2 設定と実行の間に説明可能性が足りない

ネットワーク制御では「動いたかどうか」と同じくらい「なぜそう動いたか」が重要になる。

障害時に hub 経由へ切り替わったとしても、
その理由がログや状態ファイルに残っていなければ、運用者は次に何を確認すべきか分からない。

PathWeaver では、選択された path だけでなく、

- active path が失敗したため除外された
- priority により direct が選ばれた
- health score 比較により relay が選ばれた
- fallback path として hub が選ばれた

といった判断理由を explain として出力する。

これは未踏提出向けにも重要である。
単に「IPsec がつながった」だけでなく、
「コントローラが状態を見て、意図に沿って判断し、実際の runtime に反映した」ことを示せるからである。

---

## 3. 現時点で実装できていること

### 3.1 C 言語による controller core

PathWeaver の初期実装は C 言語で進めている。

理由は、最終的に VPP や StrongSwan などの低レイヤ OSS と接続する際に、
関数呼び出し、構造体、adapter 境界が読みやすくなるためである。
また、将来的に VPP plugin や daemon 連携へ近づける場合にも、
C 実装の方が自然に拡張できる。

現時点の controller core は、主に以下を扱う。

- YAML から intent/path/tunnel/segment/health 情報を読む。
- intent に対して利用可能な path を列挙する。
- priority または評価値に基づいて path を選択する。
- active path の障害イベントを受けて fallback path を選ぶ。
- 選択理由を explain として出力する。
- StrongSwan/VPP へ渡す apply plan を生成する。
- Linux VM 検証用の runtime script を生成する。

これにより、単なる設定ファイル生成ではなく、
「イベントを受けて判断し、切り替え計画を作る controller」としての最低限の骨格ができている。

### 3.2 YAML による経路定義

PathWeaver では、IPsec 経路や fallback 経路を YAML で定義する。

YAML には、site-a、site-b、hub-1、relay-c のような node と、
それらを結ぶ tunnel、さらに intent と path が含まれる。

例として、site-a から site-b へ通信したい intent に対して、

- path-direct
- path-via-hub
- path-via-relay-c

のような複数候補を定義できる。

controller はこの YAML を読み、
通常時は direct path を選択し、
direct path が失敗した場合は hub path を選択し、
評価条件によっては relay path を選択する。

この YAML 接合部は、今後の拡張の中心になる。
GUI を後回しにしている現在でも、YAML を編集することで経路実験ができるため、
未踏提出までのデモや検証に向いている。

### 3.3 Linux network namespace による再現可能な検証環境

PathWeaver では、Linux VM 一台の中に複数の network namespace を作り、
site-a、site-b、hub-1、relay-c を模擬している。

これにより、物理的な複数拠点や複数 VM を用意しなくても、
direct 経路、hub 経路、relay 経路の切り替えを一台の Linux VM 上で再現できる。

実装済みの smoke test では、以下が確認できている。

- direct L3 path の疎通
- hub 経由 L3 path の疎通
- relay 経由 L3 path の疎通
- direct IPsec tunnel の確立
- ESP packet counter の増加
- VPP host interface を経由した LAN traffic forwarding
- controller-generated VPP route による通信
- IPsec path と VPP forwarding を一つの generated plan から制御する integrated runtime
- direct failure 時の fallback mode

特に integrated runtime smoke では、
「controller が path を選び、その結果に基づいて IPsec と VPP の両方を制御し、実際に ping が通る」
ところまで確認できている。

### 3.4 scenario harness

eventnetd の本番 daemon 化の前段階として、
複数条件を実験するための scenario harness を追加している。

この harness では、シェルスクリプトから複数の条件を切り替えて実行できる。

確認済みの scenario は以下である。

- priority により direct path を選ぶ。
- direct failure により hub path へ fallback する。
- loss/latency 評価により relay path を選ぶ。
- direct failure から runtime script を生成する。
- direct -> fallback -> recovery -> relay-best の multi-step scenario を実行する。

これは本番実装ではないが、eventnetd が将来扱うべき判断パターンを先に固定するための
テスト用の足場として有効である。

---

## 4. 技術的な特徴

### 4.1 StrongSwan と VPP を制御対象として扱う

PathWeaver は StrongSwan や VPP を再実装しない。
既存 OSS の強い部分はそのまま使う。

StrongSwan は IKE/IPsec tunnel の確立を担当する。
VPP は高速な packet forwarding や routing を担当する。

PathWeaver は、その上位で次を担当する。

- どの tunnel を起動するか。
- どの path を選ぶか。
- VPP にどの route を入れるか。
- 障害時にどの fallback を使うか。
- その理由をどのように出力するか。

つまり PathWeaver は、データプレーンではなく制御プレーン寄りのソフトウェアである。

### 4.2 YAML intent から runtime plan への変換

利用者は低レイヤのコマンドを直接並べるのではなく、
YAML でネットワークの候補経路と意図を書く。

controller はそれを読み、選択結果に応じて以下を生成する。

- `swanctl.conf`
- `apply-eventnet.sh`
- `apply-selected.sh`
- `apply-integrated.sh`
- `selected-path.txt`
- `vpp-route-plan.sh`
- `vpp-netns-route-plan.sh`

これにより、入力は宣言的な intent、出力は実行可能な runtime plan になる。

未踏提出では、この「意図から実行計画へ」の変換を見せることが重要になる。

### 4.3 説明可能な path selection

PathWeaver の path selection は、単に selected_path だけを返すのではなく、
reason と excluded path を出す。

例えば direct path が失敗した場合、

- selected_path: path-via-hub
- reason: active path path-direct failed; using fallback path-via-hub
- excluded: path-direct: active path failed event

のように出力される。

これにより、デモ時にも「なぜ hub に切り替わったのか」を説明できる。

### 4.4 実機に近い検証を一台の VM に閉じ込める

network namespace を利用することで、一台の Linux VM 内で複数拠点構成を作れる。

これは開発効率だけでなく、提出・審査・再現性の面でも大きい。
審査者や共同開発者が環境を再現しやすくなる。

今の段階では、複数 VM や実ネットワークに持ち込む前に、
VM 一台で controller の判断、IPsec、VPP forwarding を確認できる状態を優先している。

---

## 5. awesome-mitou から真似する部分

awesome-mitou は PathWeaver と対象分野は違うが、OSS としての見せ方がよい。
特に未踏提出や GitHub 公開に向けて、次の部分を真似する価値がある。

### 5.1 README の入口

awesome-mitou は、初見の人がすぐに何のリポジトリか分かる。

PathWeaver でも README 冒頭に、次を短く置くべきである。

- 何をするソフトウェアか
- なぜ必要か
- 30秒で見られるデモ
- 現時点で動くもの
- 未実装のもの

PathWeaver の README 冒頭案:

> PathWeaver is an event-driven IPsec path controller.
> It reads YAML intents, selects a usable VPN path, and generates StrongSwan/VPP runtime plans.
> In a single Linux VM, it can demonstrate direct IPsec, hub fallback, VPP forwarding, and integrated controller-driven runtime switching.

日本語なら:

> PathWeaver は、YAML で書かれた通信 intent に基づき、StrongSwan と VPP を制御して IPsec 経路を選択・切り替えるイベント駆動型ネットワーク制御基盤です。
> 一台の Linux VM 上で、direct IPsec、hub fallback、VPP forwarding、controller-generated runtime の統合動作を再現できます。

### 5.2 CI

awesome-mitou は GitHub Actions で typecheck、lint、test を実行している。

PathWeaver も最低限、GitHub 上で以下を通すべきである。

- C のビルド
- unit/smoke test
- scenario smoke
- shell script の構文チェック

VPP や StrongSwan が必要な integration smoke は GitHub Actions で無理に通さず、
VM manual smoke として分ける方がよい。

CI で通すべきもの:

- `sh scripts/vm-build-cc.sh`
- `sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml`
- 可能なら `sh -n scripts/*.sh`

CI ではまだ通さないもの:

- `sudo` が必要な netns setup
- StrongSwan daemon を起動する smoke
- VPP daemon を起動する smoke

### 5.3 core と I/O の分離

awesome-mitou は core logic と infra/adapter を分けている。

PathWeaver も方向性としては同じにすべきである。

理想的には以下のように分かれる。

- core: path selection、transition、intent evaluation
- config: YAML parser、validator
- adapters: swanctl、vppctl、Linux netns
- renderers: conf/script/status 出力
- examples: CLI entry、scenario harness
- scripts: VM setup/smoke

今の PathWeaver はすでにこの方向へ近いが、
今後は `system()` や shell command generation を adapter 境界に閉じ込める必要がある。

### 5.4 テストの細分化

awesome-mitou はテストが細かく分かれている。

PathWeaver も最終的には以下の単位に分けたい。

- YAML parser tests
- path selection tests
- transition tests
- render command tests
- security validation tests
- scenario tests
- VM integration smoke

未踏提出までに全てを完璧に分ける必要はないが、
少なくとも core の unit test と VM smoke test の役割を README に明示するべきである。

---

## 6. 現時点の弱点

### 6.1 security posture

現時点の PathWeaver は実験用として動作するが、本番に近づけるには security quick fix が必要である。

特に気になる箇所は以下。

- YAML 由来の値を含む shell command を生成している。
- `system()` 経路が残っている。
- `swanctl.conf` に PSK が含まれる場合がある。
- 一部 script が secret 入り config をログに出す可能性がある。
- generated config の permission が広すぎる箇所がある。
- ID や数値 parse の validation がまだ弱い。

未踏提出までに最低限やるべきこと:

1. secret 入りファイルを `chmod 600` にする。
2. エラー時に secret 入り conf を表示しない。
3. `PSK=change-me` を警告または拒否する。
4. YAML 由来 ID に allowlist validator を入れる。
5. `atoi()` / `atof()` を `strtol()` / `strtod()` に置き換える。
6. shell command 実行を `system()` から argv 形式へ寄せる。

### 6.2 eventnetd がまだ本番 daemon ではない

現時点では scenario harness により複数条件の実験はできる。
しかし、本番の eventnetd として常駐し、イベント入力を読み、状態を持ち、plan を生成し続けるところはまだ未実装である。

未踏提出までに目指す最小形は、完全な daemon ではなく `eventnetd --once` でよい。

`eventnetd --once` は以下を行う。

1. YAML config を読む。
2. event file を読む。
3. path selection を実行する。
4. explain/status JSONL を出す。
5. 必要なら runtime plan を生成する。

この形なら、実験用 scenario harness と本番実装の中間として扱える。

### 6.3 GitHub 上での見せ方

ローカルではかなり動いているが、GitHub 上だけを見る人にはまだ伝わりにくい。

提出や公開を考えると、README 冒頭、CI badge、デモコマンド、出力例、図が必要である。

現時点では実装の価値に対して、見せ方が追いついていない。

---

## 7. 未踏提出までの実装ロードマップ

### Phase A: 見せ方を整える

目的:

初見の人が、PathWeaver が何をして、どこまで動いているのかをすぐ理解できるようにする。

やること:

- README 冒頭を書き直す。
- Mermaid 図を追加する。
- 30秒デモコマンドを書く。
- `samples/linux-vm-netns.yaml` の意味を説明する。
- integrated runtime smoke の出力例を書く。

完了条件:

- GitHub の README だけで、目的・構成・デモ・現在地が分かる。

### Phase B: CI を追加する

目的:

リポジトリとして最低限の信頼性を持たせる。

やること:

- `.github/workflows/ci.yml` を追加する。
- C build を通す。
- scenario smoke を通す。
- shell script の構文チェックを通す。

完了条件:

- GitHub Actions 上で、sudo なしに動く範囲のテストが成功する。

### Phase C: security quick fix

目的:

提出時に「動くが危ない」と見られないようにする。

やること:

- secret file permission を直す。
- secret をログに出さない。
- default PSK の扱いを直す。
- YAML 由来 ID の validation を追加する。
- numeric parser を堅くする。

完了条件:

- 明らかな secret leak と command injection 足場が減る。
- security-audit-notes に対応状況を書ける。

### Phase D: eventnetd --once

目的:

scenario harness から一歩進めて、実行単位としての controller daemon 入口を作る。

やること:

- `examples/eventnetd.c` を追加する。
- `--config`、`--event-file`、`--status-jsonl`、`--generate-runtime` を受け取る。
- 一回だけ evaluate して終了する `--once` を実装する。
- smoke script を追加する。

完了条件:

- `eventnetd --once` で event 入力から path selection と runtime generation ができる。

### Phase E: explain/status schema

目的:

出力を人間にも他プログラムにも扱いやすくする。

やること:

- `docs/explain-jsonl-schema.md` を追加する。
- selected_path、reason、excluded、transition_state、generated_files を schema として説明する。
- サンプル出力を載せる。

完了条件:

- GUI や将来の監視ツールが読むべき出力形式が説明されている。

### Phase F: テスト分割

目的:

今後の実装変更に耐えられるようにする。

やること:

- YAML parser test を分ける。
- path selection test を分ける。
- renderer test を分ける。
- security validation test を追加する。

完了条件:

- 何が壊れたか分かりやすいテスト構成になる。

---

## 8. 最終デモの構成案

未踏提出では、全部の内部実装を説明するよりも、
次の流れで見せると伝わりやすい。

### Demo 1: YAML intent から direct path を選ぶ

見せるもの:

- `samples/linux-vm-netns.yaml`
- `selected_path: path-direct`
- generated plan

説明:

通常時は priority に従い、site-a から site-b への direct path が選ばれる。

### Demo 2: direct failure で hub fallback

見せるもの:

- fail-path event
- `selected_path: path-via-hub`
- reason
- generated VPP route

説明:

direct path が失敗したというイベントを与えると、
controller は path-direct を除外し、fallback path-via-hub を選ぶ。

### Demo 3: integrated runtime

見せるもの:

- IPsec tunnel
- VPP forwarding
- ping success
- ESP counter

説明:

controller-generated runtime により、IPsec path と VPP forwarding が一つの計画として適用され、
実際に LAN traffic が通る。

### Demo 4: scenario harness

見せるもの:

- direct
- fallback
- recovery
- relay-best

説明:

eventnetd の本番実装前でも、複数のイベント条件を実験できる。
これは今後の policy evaluation や GUI の土台になる。

---

## 9. 発表で使える短い説明

### 9.1 一文説明

PathWeaver は、YAML で書いた通信 intent に基づいて、StrongSwan と VPP を制御し、
IPsec VPN の経路をイベントに応じて選択・切り替える C 実装のネットワーク制御基盤です。

### 9.2 もう少し長い説明

PathWeaver は、複数の VPN 経路候補を YAML で定義し、障害や評価値に応じて
利用すべき経路を選択するコントローラです。
選択結果から StrongSwan の IPsec 設定と VPP の routing 設定を生成し、
Linux VM 上の network namespace 環境で direct、hub fallback、relay 経路を再現できます。
現在は GUI よりも controller core と runtime integration を優先しており、
一台の VM 上で IPsec と VPP を含む統合 smoke test が成功しています。

### 9.3 技術的に面白い点

- IPsec/VPP を再実装せず、制御対象として組み合わせている。
- YAML intent から runtime plan を生成している。
- direct/hub/relay の複数 path を controller が選択できる。
- 選択理由を explain として出力する。
- Linux VM 一台で再現可能な実験環境を持つ。
- C 実装なので、将来的な VPP/StrongSwan 近接連携に持ち込みやすい。

---

## 10. README 冒頭に使える文章案

```markdown
# PathWeaver

PathWeaver is an event-driven IPsec path controller written in C.

It reads YAML intents, selects a usable VPN path, and generates runtime plans for
StrongSwan and VPP.  A single Linux VM can reproduce direct IPsec, hub fallback,
relay routing, VPP forwarding, and integrated controller-driven runtime switching.

## What works today

- YAML-based intent/path/tunnel parsing
- priority-based and health-evaluated path selection
- direct failure fallback to hub path
- StrongSwan swanctl config generation
- VPP route plan generation
- Linux network namespace smoke tests
- direct IPsec smoke with ESP counter validation
- controller-generated VPP runtime smoke
- integrated IPsec + VPP runtime smoke
- event scenario harness for direct/fallback/recovery/relay experiments

## Quick demo

```sh
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

For full VM integration:

```sh
sudo sh scripts/vm-netns-setup.sh
sudo sh scripts/vm-controller-integrated-smoke.sh direct
sudo sh scripts/vm-controller-integrated-smoke.sh fallback
```
```

---

## 11. 今すぐ実装すべき順番

現時点では、機能を増やすよりも「提出時に強く見える形」に整えるのがよい。

順番は以下。

1. README 冒頭を整える。
2. GitHub Actions CI を追加する。
3. security quick fix を入れる。
4. `docs/explain-jsonl-schema.md` を追加する。
5. `eventnetd --once` を追加する。
6. test を分割する。

この順番にする理由は、まず外から見える信頼性を上げ、
その後に本番実装へ近づけるためである。

特に README と CI は、実装量の割に見え方への効果が大きい。
security quick fix は、StrongSwan/VPP のような低レイヤ OSS と接続するプロジェクトでは
早めに入れておくべきである。

---

## 12. 現時点の結論

PathWeaver は、まだ本番運用できる完成品ではない。
しかし、未踏提出向けの初期実装としてはかなり良い位置まで来ている。

理由は、単なる設計資料や mock ではなく、

- C controller core
- YAML 接合
- StrongSwan config generation
- VPP route generation
- Linux VM network namespace
- direct IPsec smoke
- VPP forwarding smoke
- integrated runtime smoke
- fallback scenario

がすでに接続されているからである。

次に必要なのは、派手な新機能よりも、
「初見で価値が伝わる README」
「GitHub で通る CI」
「最低限の security posture」
「eventnetd --once による controller entrypoint」
である。

この4つが揃うと、PathWeaver は
「実験的だが、設計思想・実装・検証環境が一貫しているプロジェクト」
として見せられる。

