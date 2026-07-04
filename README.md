# urg_dora

`urg_dora` is a native [dora-rs](https://dora-rs.ai/) C++ source node for
Hokuyo 2D LiDARs. It ports the relevant single-echo Ethernet path from
[Hokuyo-aut/urg_node2](https://github.com/Hokuyo-aut/urg_node2) and uses
[UrgNetwork/urg_library](https://github.com/UrgNetwork/urg_library) for all
sensor communication. It has no ROS2 dependency and emits one Apache Arrow
`StructArray` row per scan on the dora output `scan`.

日本語版の README は [`README_ja.md`](README_ja.md) を参照してください。

## Quickstart

1. Clone this repository with the submodule:

   ```bash
   git clone --recurse-submodules <urg_dora-repository-url>
   cd urg_dora
   ```

2. Download and extract the prebuilt dora C++ package for your platform, then
   point `CMAKE_PREFIX_PATH` at that extracted directory. For example:

   ```bash
   cmake -S . -B build \
     -DCMAKE_PREFIX_PATH=/home/koki/dora_ws/dora-cpp-libraries-linux-x86_64
   ```

   The dora CLI is still installed separately, for example with:

   ```bash
   cargo install dora-cli
   ```

3. Build the node in the default `build/` directory:

   ```bash
   cmake --build build -j
   ```

4. Run the example dataflow:

   ```bash
   dora run dataflow/urg_dora.yaml
   ```

   The example dataflow expects the built binary at `build/urg_dora_node` and
   the default config at `config/urg_dora.yaml`. It also enables
   `_unstable_debug.enable_debug_inspection` so `dora topic echo/hz/info` works
   when you use `dora up` + `dora start`.

## v0.1 scope

Supported:

- Ethernet Hokuyo connection and single-echo distance scans
- `angle_min`, `angle_max`, `cluster`, and `skip`
- `calibrate_time`, `synchronize_time`, and `time_offset`
- consecutive-error windows, reconnection, and timestamp-state reset
- upstream scan metadata, zero-distance-to-NaN conversion, 24-bit hardware
  timestamp wrap handling, EMA smoothing, and clock-warp reset
- graceful dora stop polling and SIGPIPE suppression on platforms that define it

Explicitly unsupported in v0.1:

- intensity and multiecho, including `/first`, `/last`, and `/most_intense`
- Serial/USB
- ROS2 messages, publishers/subscribers, compatibility, or diagnostics
- lifecycle/component mounting and dynamic parameter changes
- a `status` output

## Output

`scan` is an Arrow `Struct` array of length one with this schema:

| Field | Arrow type | Meaning |
| --- | --- | --- |
| `stamp_ns` | `Int64` | Corrected Unix timestamp in nanoseconds |
| `frame_id` | `Utf8` | Configured frame, with leading `/` removed like upstream |
| `angle_min` | `Float32` | Actual first URG step angle, radians |
| `angle_max` | `Float32` | Actual last URG step angle, radians |
| `angle_increment` | `Float32` | URG angular step times `cluster` |
| `time_increment` | `Float32` | Upstream-equivalent beam timing |
| `scan_time` | `Float32` | Native sensor scan period, seconds |
| `range_min` | `Float32` | Sensor minimum range, metres |
| `range_max` | `Float32` | Sensor maximum range, metres |
| `ranges` | `List<Float32>` | Variable-length ranges in metres; zero readings are NaN |

`List<Float32>` is intentional: beam count depends on the sensor model, actual
step range, and clustering and is therefore not part of the static schema.

## Dependencies

- Linux with a C++20 compiler, CMake 3.21+, Rust/Cargo, and Git
- dora CLI (`cargo install dora-cli` or the official installation method)
- prebuilt dora C++ libraries extracted from a `dora-cpp-libraries-<target>`
  archive published by dora-rs
- Apache Arrow C++ installed through the official Apache APT repository on
  Ubuntu 22.04:

  ```bash
  sudo apt update
  sudo apt install -y -V ca-certificates lsb-release wget
  wget https://packages.apache.org/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb
  sudo apt install -y -V ./apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb
  sudo apt update
  sudo apt install -y -V libarrow-dev
  ```

- yaml-cpp
- urg_library C API (included as a pinned Git submodule)

Clone this repository recursively so the pinned urg_library revision is checked
out automatically:

For an existing non-recursive checkout, run
`git submodule update --init --recursive`. CMake compiles the required
urg_library C sources directly; no separate urg_library build or installation
is needed. `URG_LIBRARY_ROOT` remains available as an explicit override for a
prebuilt external installation.

Package mode is the default: `find_package(dora-node-api-cxx CONFIG REQUIRED)`
locates the prebuilt dora C++ archive, checks that the archive target matches
the current host, and exposes `${dora-node-api-cxx_CXX_BRIDGE_FILES}` plus
the `dora-node-api-cxx::dora-node-api-cxx` and `dora-node-api-cxx::extra`
targets. No dora source checkout is needed in this mode.

If you need the older source-based workflow for development, enable the
fallback mode explicitly with `-DURG_DORA_USE_DORA_SOURCE=ON -DDORA_ROOT_DIR=/path/to/dora`.

Unlike upstream `urg_node2`, this project does not use rosdep, ament, or ROS2
packages. In particular, it does not depend on `rclcpp`, `rclcpp_components`,
`rclcpp_lifecycle`, `lifecycle_msgs`, `sensor_msgs`, `diagnostic_updater`, or
`laser_proc`. The non-ROS dependencies that still must be installed are Arrow
C++, yaml-cpp, CMake/C++ tooling, Rust/Cargo, Git, and the dora CLI.

Install Arrow and yaml-cpp through the platform package manager. Arrow's
official Ubuntu APT instructions are listed on the
[Apache Arrow install page](https://arrow.apache.org/install/).

## Build and test

From this repository:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/home/koki/dora_ws/dora-cpp-libraries-linux-x86_64
cmake --build build -j
ctest --test-dir build --output-on-failure
```

To exercise the fallback source mode, configure a separate build tree with:

```bash
cmake -S . -B build-source \
  -DURG_DORA_USE_DORA_SOURCE=ON \
  -DDORA_ROOT_DIR=/path/to/dora
cmake --build build-source -j
```

The hardware-independent tests cover config loading and the hardware-clock
synchronizer; they do not pretend to test a LiDAR.

## Configuration

The node accepts one optional argument: the YAML configuration path. See
[`config/urg_dora.yaml`](config/urg_dora.yaml):

```yaml
ip_address: "192.168.0.10"
ip_port: 10940
frame_id: "laser"

angle_min: -1.4
angle_max: 1.4
cluster: 1
skip: 0

calibrate_time: true
synchronize_time: true
time_offset: 0.0

error_limit: 4
error_reset_period: 5.0
```

`cluster` is clamped to `[1, 99]` and `skip` to `[0, 9]`, matching
`urg_node2`. A reconnect stops and closes the sensor, reopens Ethernet,
reapplies scanning parameters, reruns calibration when enabled, resets
synchronization state, and restarts measurement. As in upstream, reconnect
happens when the error count is strictly greater than `error_limit`; the count
is cleared every `error_reset_period` seconds.

## Run with dora

The example [`dataflow/urg_dora.yaml`](dataflow/urg_dora.yaml) is a true source
node: it declares no inputs and declares the `scan` output as required by dora.
It also enables `_unstable_debug.enable_debug_inspection`, which is required
for `dora topic echo`, `dora topic hz`, and `dora topic info`. Paths are
relative to the dataflow file's directory.

```bash
dora run dataflow/urg_dora.yaml
```

From the repository root, `dataflow/urg_dora.yaml` resolves the node binary and
config relative to the `dataflow/` directory, so no path edits are needed for
the example layout. Use the single default build tree:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/home/koki/dora_ws/dora-cpp-libraries-linux-x86_64
cmake --build build -j
```

If a dataflow named `urg` is already running, stop it first:

```bash
dora down
```

Current dora source nodes do not receive `AllInputsClosed` merely because they
have zero inputs. The C++ node therefore drives scans itself and uses the
official non-blocking `try_next_event()` API between reads/reconnect attempts
to observe `Stop`. A blocking URG operation can delay stop handling until its
socket timeout expires.

## Timestamp policy and verification

`stamp_ns` follows `urg_node2`'s `header.stamp` calculation:

1. Capture system time immediately before `urg_get_distance`.
2. If `synchronize_time` is enabled, unwrap the LiDAR's 24-bit millisecond
   clock and apply the upstream EMA (`alpha = 0.01`). The first 100 scans retain
   receive time. A difference over 100 ms resets the EMA.
3. Add calibrated system latency, user `time_offset`, and the angular time from
   the rear timestamp reference to the configured first step.

For compatibility, calibration preserves an upstream quirk: calibration adds
the angular offset to `system_latency`, and normal scan stamping adds the
angular offset again. This is marked at the implementation site rather than
silently redesigned.

To validate on hardware, run with `synchronize_time: false` first and compare
`stamp_ns` with host receive time. Then enable synchronization, discard the
100-scan warmup, and check monotonic deltas against
`scan_time * (skip + 1)`. Enable calibration separately and record the logged
latency. Test across the 24-bit wrap boundary (about 4.66 hours) for long-run
validation. NTP/PTP clock steps greater than 100 ms should produce the logged
EMA reset. Runtime validation requires a real Hokuyo; none is fabricated here.

## Known limitations

- Calibration remains experimental because it preserves upstream's timing
  method and assumptions about request/read latency.
- Shutdown responsiveness is bounded by urg_library's blocking network call.
- The dataflow does not attach a dora type URN; the exact Arrow schema above is
  carried by the Arrow C Data Interface and documented here.
- Package mode expects a matching `dora-cpp-libraries-<target>` archive for
  the host you are building on; the package config aborts early if the target
  triple does not match.
- The source-based fallback remains available for development, but it is no
  longer the default path.

## License and attribution

This project is Apache-2.0 licensed. Ported sections retain the eSOL copyright
and Apache-2.0 notice from `urg_node2`. urg_library is Simplified BSD licensed
and is pinned as a Git submodule whose C API sources are compiled into the
node. The package-mode C++ integration follows dora's official
`dora-node-api-cxx` CMake contract. The source-based fallback is retained only
for development and compatibility. See [`NOTICE`](NOTICE) for the attribution
summary and each upstream repository for its full license.
