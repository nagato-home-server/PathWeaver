# Linux VM Smoke Test

共有フォルダ上の `controller-c` を Linux VM から実行するための最小手順です。

## 1. Dependency Check

```sh
cd /path/to/controller-c
sh scripts/vm-check.sh
```

足りないものが出たら、VM 側でインストールしてください。

Debian/Ubuntu 系なら helper も使えます。

```sh
sudo sh scripts/vm-install-deps-debian.sh
sh scripts/vm-runtime-status.sh
```

## 2. Build

```sh
sh scripts/vm-build.sh
```

成果物は既定で `build-linux/` に作られます。

`cmake` がまだ無い場合は、`cc` だけで fallback build できます。

```sh
sh scripts/vm-build-cc.sh
```

この場合、成果物は `build-linux-cc/` に作られます。

## 3. Generate Apply Plan

```sh
sh scripts/vm-generate-plan.sh samples/linux-vm-netns.yaml
```

出力:

- `out/eventnet-swanctl.conf`
- `out/apply-eventnet.sh`

## 4. Namespace Underlay

```sh
sudo sh scripts/vm-netns-setup.sh
```

基本疎通:

```sh
sudo ip netns exec site-a ping -c 1 203.0.113.9
sudo ip netns exec site-a ping -c 1 203.0.113.13
sudo ip netns exec site-b ping -c 1 203.0.113.17
```

## 5. Namespace Route Smoke Test

IPsec/VPP の前に、Direct / Hub / Relay の L3 経路が namespace 内で成立するか確認します。

```sh
sudo sh scripts/vm-netns-smoke.sh
```

このテストは `10.10.1.1/24` と `10.10.2.1/24` の dummy LAN を作り、経路を direct → hub → relay に切り替えながら ping します。

片付け:

```sh
sudo sh scripts/vm-netns-clean.sh
```

## Current Limit

この段階では `swanctl.conf` と apply script の生成、および namespace underlay の作成までです。

実際に namespace 内で strongSwan / VPP を動かすには、次の追加が必要です。

- namespace ごとの strongSwan daemon 起動
- namespace ごとの `swanctl --load-conns`
- VPP と namespace の interface 接続
- VPP route table と tunnel interface の対応づけ

次の実装ステップは、`site-a` namespace 内だけで `swanctl.conf` を load し、`site-b` と direct IKE SA を確立するスクリプトです。

## 6. Direct IPsec Config Generation

strongSwan daemon 起動の前段として、site-a / site-b 双方の direct tunnel 設定を生成できます。

```sh
sh scripts/vm-netns-ipsec-direct-generate.sh
sudo sh scripts/vm-netns-ipsec-direct-status.sh
```

生成先:

- `out/netns-ipsec-direct/site-a/swanctl.conf`
- `out/netns-ipsec-direct/site-b/swanctl.conf`

XFRM の掃除:

```sh
sudo sh scripts/vm-netns-ipsec-direct-clean.sh
```

## 7. Direct IPsec Start Attempt

`/usr/lib/ipsec/charon` がある場合、site-a / site-b namespace 内で direct tunnel を起動できます。

```sh
sudo sh scripts/vm-netns-ipsec-direct-start.sh
sudo sh scripts/vm-netns-ipsec-direct-status.sh
```

ログ:

```sh
sh scripts/vm-netns-ipsec-direct-logs.sh
```

暗号化された direct tunnel に実際の LAN ping が流れるか確認します。

```sh
sudo sh scripts/vm-netns-ipsec-direct-smoke.sh
```

この smoke test は `site-a -> site-b` の ping に加えて、`swanctl --list-sas` の ESP packet counter が増えることを確認します。

停止:

```sh
sudo sh scripts/vm-netns-ipsec-direct-stop.sh
```

## 8. Hub IPsec Start Attempt

direct IPsec を停止してから、`site-a -> hub-1 -> site-b` の hub 経由 route-based IPsec を起動します。

```sh
sudo sh scripts/vm-netns-ipsec-direct-stop.sh
sudo sh scripts/vm-netns-ipsec-hub-start.sh
sudo sh scripts/vm-netns-ipsec-hub-status.sh
```

hub path は中継ノードで複数 tunnel を扱うため、policy-based IPsec ではなく XFRM interface を使います。

- `site-a <-> hub-1`: `tun-a-hub`, `if_id 101`
- `hub-1 <-> site-b`: `tun-hub-b`, `if_id 102`

実 traffic が hub 経由の ESP に乗るか確認します。

```sh
sudo sh scripts/vm-netns-ipsec-hub-smoke.sh
```

ログ:

```sh
sh scripts/vm-netns-ipsec-hub-logs.sh
```

停止:

```sh
sudo sh scripts/vm-netns-ipsec-hub-stop.sh
```

## 9. Controller/YAML Selected Netns Runtime

controller が YAML intent から選んだ path を、netns 実行 wrapper に変換します。

```sh
sh scripts/vm-build-cc.sh
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml
cat out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh
```

既定では `intent-a-b` の priority selection により `path-direct` が選ばれます。

fallback / hub path を明示的に検証する場合:

```sh
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml --path path-via-hub
cat out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh
```

direct と hub の両方を controller 生成 wrapper 経由で連続確認する smoke test:

```sh
sh scripts/vm-netns-controller-switch-smoke.sh samples/linux-vm-netns.yaml
```

active direct path の failure event を注入し、YAML の fallback policy で hub を選ぶ smoke test:

```sh
sh scripts/vm-netns-controller-fallback-smoke.sh samples/linux-vm-netns.yaml
```

direct failure 後に hub fallback へ移り、direct recovery で priority direct へ戻る smoke test:

```sh
sh scripts/vm-netns-controller-recovery-smoke.sh samples/linux-vm-netns.yaml
```

手動で見る場合:

```sh
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct
cat out/netns-runtime/selected-path.txt
sh out/netns-runtime/apply-selected.sh
```

この段階では runtime script は `path-direct` と `path-via-hub` に対応しています。`path-via-relay-c` は次の拡張対象です。

## 10. VPP Route Plan Preparation

VPP を実 interface に接続する前段として、controller-selected path から VPP route command plan を生成します。

VPP の有無を確認:

```sh
sh scripts/vm-vpp-preflight.sh
```

標準 apt で `vpp` / `vpp-plugin-core` が見つからない場合、OS と apt source を確認します。

```sh
sh scripts/vm-vpp-os-info.sh
```

FD.io repository の取得で `Could not resolve host: packagecloud.io` が出る場合は、VM の DNS / outbound network を確認します。

```sh
sh scripts/vm-network-dns-check.sh packagecloud.io
```

FD.io packagecloud repository を使う場合、まず dry-run で内容を確認します。

```sh
sudo sh scripts/vm-install-vpp-fdio.sh
```

実際に repository を追加して VPP を install する場合:

```sh
sudo DRY_RUN=0 sh scripts/vm-install-vpp-fdio.sh
sh scripts/vm-vpp-preflight.sh
```

VPP が未導入でも、dry-run の route plan は確認できます。

```sh
sh scripts/vm-build-cc.sh
sh scripts/vm-vpp-route-plan-smoke.sh samples/linux-vm-netns.yaml
```

個別に見る場合:

```sh
sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml
cat out/netns-runtime/selected-path.txt
DRY_RUN=1 sh out/netns-runtime/vpp-route-plan.sh

sh scripts/vm-generate-netns-runtime.sh samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct
cat out/netns-runtime/selected-path.txt
DRY_RUN=1 sh out/netns-runtime/vpp-route-plan.sh
```

実 VPP に適用する場合は、VPP 起動と interface/next-hop 到達性を確認したうえで `DRY_RUN=0` を指定します。

```sh
sudo DRY_RUN=0 sh out/netns-runtime/vpp-route-plan.sh
```

現在の VPP 準備段階では route command generation までです。次の段階で VPP interface と namespace / XFRM interface の接続を詰めます。

## 11. VPP Netns Host-Interface Smoke

VPP 導入後、Linux namespace と VPP を veth + AF_PACKET host-interface で接続します。

```sh
sudo sh scripts/vm-vpp-netns-setup.sh
sudo sh scripts/vm-vpp-netns-status.sh
sudo sh scripts/vm-vpp-netns-smoke.sh
```

構成:

- `site-a:vpp-client 172.16.1.2/30` <-> `VPP host-vpp-site-a 172.16.1.1/30`
- `site-b:vpp-client 172.16.2.2/30` <-> `VPP host-vpp-site-b 172.16.2.1/30`

この VPP edge 情報は `samples/linux-vm-netns.yaml` の `vpp_edges:` にも定義します。
`eventnet_netns_plan` が生成する `vpp-netns-route-plan.sh` は、この YAML mapping の `node_id` と `next_hop` を使って VPP route を作ります。

片付け:

```sh
sudo sh scripts/vm-vpp-netns-clean.sh
```

この smoke test は IPsec とは独立して、VPP が netns 間の L3 forwarding plane として使えるかを確認します。

controller-generated VPP route plan を VPP netns 接続へ実適用する smoke test:

```sh
sudo sh scripts/vm-vpp-controller-netns-smoke.sh samples/linux-vm-netns.yaml
```

この smoke test は `eventnet_netns_plan` が生成する `out/netns-runtime/vpp-netns-route-plan.sh` を `DRY_RUN=0` で適用し、`10.10.1.0/24 <-> 10.10.2.0/24` の LAN traffic が VPP 経由で流れることを確認します。

## 12. Integrated IPsec + VPP Runtime Smoke

controller が選んだ path から、IPsec runtime と VPP netns forwarding を一つの生成 plan で連続制御します。

```sh
sudo sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
```

fallback/hub も同じ入口で確認できます。

```sh
sudo MODE=fallback sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
sudo MODE=both sh scripts/vm-controller-integrated-runtime-smoke.sh samples/linux-vm-netns.yaml
```

生成される統合 runtime:

- `out/netns-runtime/apply-integrated.sh`

この段階の統合は、同一 controller-generated plan の中で次を連続して行うものです。

- selected path の IPsec tunnel 起動
- IPsec smoke による ESP counter 確認
- VPP host-interface setup
- YAML `vpp_edges` から生成した VPP route 適用
- VPP forwarding smoke

まだ「同一packetをIPsec復号後にVPPで転送する本番gateway pipeline」ではありません。そこは次段階で、Linux/VPP interface設計とXFRM/VPP接続を詰めます。

## 13. Scenario Harness Smoke

本番 `eventnetd` の前段として、path selection / fallback / evaluated policyをCLI引数で実験できます。

```sh
sh scripts/vm-build-cc.sh
sh scripts/vm-eventnet-scenario-smoke.sh samples/linux-vm-netns.yaml
```

個別に実行する場合:

```sh
build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --active-path path-direct \
  --fail-path path-direct \
  --expect path-via-hub

build-linux-cc/eventnet_scenario samples/linux-vm-netns.yaml \
  --mode evaluated \
  --health path-direct=healthy,rtt=80,loss=0.5 \
  --health path-via-relay-c=healthy,rtt=30,loss=0.1 \
  --health path-via-hub=healthy,rtt=50,loss=0.2 \
  --compare packet_loss,latency,hop_count,path_id \
  --expect path-via-relay-c
```
