# urg_dora

`urg_dora` は、Hokuyo 2D LiDAR 向けのネイティブな [dora-rs](https://dora-rs.ai/) C++ ソースノードです。
[Hokuyo-aut/urg_node2](https://github.com/Hokuyo-aut/urg_node2) の single-echo Ethernet 経路を必要部分だけ移植し、センサー通信には
[UrgNetwork/urg_library](https://github.com/UrgNetwork/urg_library) を使います。ROS2 依存はなく、各スキャンごとに Apache Arrow の `StructArray`
1 行を dora の出力 `scan` に送ります。

英語版の README は [`README.md`](README.md) を参照してください。

## クイックスタート

1. submodule 付きで clone します。

   ```bash
   git clone --recurse-submodules <urg_dora-repository-url>
   cd urg_dora
   ```

2. 下記の非 ROS 依存関係を入れます。特に Arrow C++ と `yaml-cpp` が必要です。dora CLI は次で入れます。

   ```bash
   cargo install dora-cli
   ```

3. ノードをビルドします。

   ```bash
   cmake -S . -B build
   cmake --build build -j
   ```

4. example dataflow を validate/run します。

   ```bash
   dora validate dataflow/urg_dora.yaml
   dora run dataflow/urg_dora.yaml
   ```

   この example dataflow は `build/urg_dora_node` と `config/urg_dora.yaml`
   を前提にしています。

## v0.1 の対象範囲

対応:

- Ethernet 接続と single-echo 距離スキャン
- `angle_min`、`angle_max`、`cluster`、`skip`
- `calibrate_time`、`synchronize_time`、`time_offset`
- 連続エラー判定、再接続、タイムスタンプ同期状態のリセット
- upstream 互換のスキャンメタデータ、0 距離の NaN 化、24-bit ハードウェア時刻の wrap 処理、EMA 平滑化、clock-warp リセット
- dora の停止ポーリングと、対応プラットフォームでの SIGPIPE 抑制

v0.1 で明示的に非対応:

- intensity と multiecho。`/first`、`/last`、`/most_intense` を含む
- Serial/USB
- ROS2 メッセージ、publisher/subscriber、互換機能、diagnostics
- lifecycle / component mounting、dynamic parameter changes
- `status` 出力

## 出力

`scan` は長さ 1 の Arrow `Struct` 配列で、スキーマは次のとおりです。

| Field | Arrow type | 意味 |
| --- | --- | --- |
| `stamp_ns` | `Int64` | 補正済み Unix 時刻。単位はナノ秒 |
| `frame_id` | `Utf8` | 設定された frame。upstream と同じく先頭の `/` は除去 |
| `angle_min` | `Float32` | 実際の先頭 URG step の角度 [rad] |
| `angle_max` | `Float32` | 実際の末尾 URG step の角度 [rad] |
| `angle_increment` | `Float32` | URG の角度 step に `cluster` を掛けた値 |
| `time_increment` | `Float32` | upstream 互換のビーム時間 |
| `scan_time` | `Float32` | センサー固有の scan period [s] |
| `range_min` | `Float32` | センサーの最小距離 [m] |
| `range_max` | `Float32` | センサーの最大距離 [m] |
| `ranges` | `List<Float32>` | 可変長の距離配列 [m]。0 は NaN |

`List<Float32>` を使うのは意図的です。ビーム数はセンサーモデル、実際の step 範囲、cluster に依存するため、固定長スキーマにはできません。

## 依存関係

- Linux、C++20 コンパイラ、CMake 3.21 以上、Rust/Cargo、Git
- dora CLI（`cargo install dora-cli`）と、ソース互換の C++ node API
- Apache Arrow C++（現行の dora C++ 例では Arrow 19.0.1 以上が必要）
- yaml-cpp
- urg_library C API（Git submodule として同梱）

ビルドは dora の公式 CMake 例に合わせています。デフォルトでは CMake が `DORA_GIT_TAG` に記録された revision の公式 dora リポジトリを取得し、`dora-node-api-cxx` をビルドします。dora CLI 本体は別途 `cargo install dora-cli` で入れます。ローカルの dora checkout を使う場合は `-DDORA_ROOT_DIR=/path/to/dora` を指定してください。C++ API をその source tree からビルドしたいときに使います。

このリポジトリは submodule を含むので、clone は recursive で行うのが簡単です。

既存 checkout の場合は `git submodule update --init --recursive` を実行してください。CMake は必要な urg_library の C ソースを直接コンパイルするため、urg_library の別ビルドやインストールは不要です。必要なら `URG_LIBRARY_ROOT` で事前インストール済みの外部版を明示できます。

upstream の `urg_node2` と違い、このプロジェクトは rosdep、ament、ROS2 パッケージを使いません。具体的には `rclcpp`、`rclcpp_components`、`rclcpp_lifecycle`、`lifecycle_msgs`、`sensor_msgs`、`diagnostic_updater`、`laser_proc` には依存しません。残る非 ROS 依存は Arrow C++、yaml-cpp、CMake/C++ ツールチェーン、Rust/Cargo、Git、dora CLI です。

Arrow と yaml-cpp は OS のパッケージマネージャで入れてください。Arrow の公式インストール手順は
[dora C++ Arrow example](https://github.com/dora-rs/dora/tree/main/examples/c%2B%2B-arrow-dataflow)
から辿れます。

## ビルドとテスト

このリポジトリのルートで:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`DORA_ROOT_DIR` を省略すると、CMake が pinned された公式 dora revision を取得します。ハードウェア非依存のテストは config 読み込みと hardware-clock synchronizer を対象にしており、LiDAR 自体はテストしません。

## 設定

ノードは YAML 設定ファイル 1 つだけを受け取ります。省略時は [`config/urg_dora.yaml`](config/urg_dora.yaml) を参照してください。

```yaml
ip_address: "192.168.0.10"
ip_port: 10940
frame_id: "laser"

angle_min: -3.141592653589793
angle_max: 3.141592653589793
cluster: 1
skip: 0

calibrate_time: true
synchronize_time: true
time_offset: 0.0

error_limit: 4
error_reset_period: 5.0
```

角度は `[-pi, pi]` に、`cluster` は `[1, 99]` に、`skip` は `[0, 9]` に切り詰めます。これは `urg_node2` と同じです。その後 URG が device step に対して角度を丸めるので、出力メタデータにはその実値が入ります。再接続時はセンサーを stop/close してから reopen し、scan 設定を再適用し、calibration が有効なら再実行し、同期状態をリセットして measurement を再開します。upstream と同様に、再接続は error count が `error_limit` を厳密に超えたときに起きます。error count は `error_reset_period` 秒ごとにクリアされます。

## dora での実行

[`dataflow/urg_dora.yaml`](dataflow/urg_dora.yaml) は true な source node です。inputs はなく、`scan` output を dora 側で宣言しています。パスは dataflow ファイルのディレクトリ基準です。

```bash
dora validate dataflow/urg_dora.yaml
dora run dataflow/urg_dora.yaml
```

リポジトリ root から実行すれば、`dataflow/urg_dora.yaml` は `dataflow/` ディレクトリ基準でノード binary と config を解決します。例の layout では path の修正は不要です。

現在の dora source node は、入力が 0 個でも自動的に `AllInputsClosed` を受け取りません。そのため C++ ノードは自前でスキャン処理を回し、スキャン間や再接続待ちの間に公式の non-blocking `try_next_event()` を使って `Stop` を監視します。URG の blocking I/O は socket timeout まで停止応答を遅らせます。

## タイムスタンプ方針と検証

`stamp_ns` は `urg_node2` の `header.stamp` 計算に合わせます。

1. `urg_get_distance` の直前に system time を取る
2. `synchronize_time` が有効なら、LiDAR の 24-bit millisecond clock を unwrap し、upstream の EMA（`alpha = 0.01`）を適用する。最初の 100 scan は receive time を維持する。100 ms を超える差分が出たら EMA をリセットする
3. calibrated system latency、ユーザー指定の `time_offset`、および configured first step までの angular time を加算する

互換性のため、calibration には upstream の癖をそのまま残しています。つまり calibration で angular offset を `system_latency` に足し、通常の scan stamping でも angular offset をさらに足します。これは implementation site で明示し、勝手に再設計していません。

ハードウェア上での確認は、まず `synchronize_time: false` で動かして `stamp_ns` と host の受信時刻を比べます。その後 synchronization を有効にし、最初の 100 scan を捨て、`scan_time * (skip + 1)` に対する単調な差分を確認します。calibration は別に有効化し、ログに出る latency を記録します。長時間評価では 24-bit wrap 境界（約 4.66 時間）をまたぐか確認してください。`100 ms` を超える NTP/PTP の時刻ジャンプは、ログに出る EMA リセットを発生させるはずです。実機なしでの runtime validation はしません。

## 既知の制限

- calibration は upstream の timing 手法と latency 仮定をそのまま使うため、まだ実験的です
- shutdown の応答性は urg_library の blocking network call に依存します
- dataflow には dora の type URN を付けていません。上記の Arrow schema は C Data Interface 経由で伝わる前提で、ここに明記しています
- このプロジェクトは CMake で pinned した official dora C++ API revision を対象にしています。dora API が変わる場合は `DORA_ROOT_DIR` を意図的に切り替えてください

## ライセンスと帰属

このプロジェクトは Apache-2.0 ライセンスです。port した箇所には `urg_node2` の eSOL 著作権表示と Apache-2.0 notice を残しています。urg_library は Simplified BSD ライセンスで、Git submodule として固定し、その C API ソースを node に組み込んでいます。dora の CMake bridge integration は dora の Apache-2.0 公式 example を元にしています。帰属の概要は [`NOTICE`](NOTICE) を参照してください。各 upstream リポジトリの完全な license もあわせて確認してください。
