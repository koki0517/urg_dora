# urg_dora

`urg_dora` is a native [dora-rs](https://dora-rs.ai/) C++ source node for
Hokuyo 2D LiDARs. It ports the relevant single-echo Ethernet path from
[Hokuyo-aut/urg_node2](https://github.com/Hokuyo-aut/urg_node2) and uses
[UrgNetwork/urg_library](https://github.com/UrgNetwork/urg_library) for all
sensor communication. It has no ROS2 dependency and emits one Apache Arrow
`StructArray` row per scan on the dora output `scan`.

日本語版の README は [`README_ja.md`](README_ja.md) を参照してください。

## Quickstart

1. Clone with the submodule:

   ```bash
   git clone --recurse-submodules <urg_dora-repository-url>
   cd urg_dora
   ```

2. Install the non-ROS dependencies listed below, especially a working Arrow C++
   and `yaml-cpp` installation. Install the dora CLI with:

   ```bash
   cargo install dora-cli
   ```

3. Build the node:

   ```bash
   cmake -S . -B build
   cmake --build build -j
   ```

4. Validate and run the example dataflow:

   ```bash
   dora validate dataflow/urg_dora.yaml
   dora run dataflow/urg_dora.yaml
   ```

   The example dataflow expects the built binary at `build/urg_dora_node` and
   the default config at `config/urg_dora.yaml`.

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
- dora CLI (`cargo install dora-cli`) and source-compatible C++ node API
- Apache Arrow C++ (the current dora example requires Arrow 19.0.1 or newer)
- yaml-cpp
- urg_library C API (included as a pinned Git submodule)

The build follows dora's official CMake example. By default CMake fetches the
official dora repository at the revision recorded in `DORA_GIT_TAG` and builds
`dora-node-api-cxx`. The dora CLI itself is installed separately with
`cargo install dora-cli`. To use a local dora checkout instead, pass
`-DDORA_ROOT_DIR=/path/to/dora` if you explicitly want the C++ API build to use
that source tree.

Clone this repository recursively so the pinned urg_library revision is checked
out automatically:

For an existing non-recursive checkout, run
`git submodule update --init --recursive`. CMake compiles the required
urg_library C sources directly; no separate urg_library build or installation
is needed. `URG_LIBRARY_ROOT` remains available as an explicit override for a
prebuilt external installation.

Unlike upstream `urg_node2`, this project does not use rosdep, ament, or ROS2
packages. In particular, it does not depend on `rclcpp`, `rclcpp_components`,
`rclcpp_lifecycle`, `lifecycle_msgs`, `sensor_msgs`, `diagnostic_updater`, or
`laser_proc`. The non-ROS dependencies that still must be installed are Arrow
C++, yaml-cpp, CMake/C++ tooling, Rust/Cargo, Git, and the dora CLI.

Install Arrow and yaml-cpp through the platform package manager. Arrow's
official installation instructions are linked from the
[dora C++ Arrow example](https://github.com/dora-rs/dora/tree/main/examples/c%2B%2B-arrow-dataflow).

## Build and test

From this repository:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Omit `DORA_ROOT_DIR` to let CMake fetch the pinned official dora revision.
The hardware-independent tests cover config loading and the hardware-clock
synchronizer; they do not pretend to test a LiDAR.

## Configuration

The node accepts one optional argument: the YAML configuration path. See
[`config/urg_dora.yaml`](config/urg_dora.yaml):

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

Angles are clipped to `[-pi, pi]`, `cluster` to `[1, 99]`, and `skip` to
`[0, 9]`, matching `urg_node2`. URG then clips/rounds angles to device steps;
the output metadata reports those actual steps. A reconnect stops and closes
the sensor, reopens Ethernet, reapplies scanning parameters, reruns calibration
when enabled, resets synchronization state, and restarts measurement. As in
upstream, reconnect happens when the error count is strictly greater than
`error_limit`; the count is cleared every `error_reset_period` seconds.

## Run with dora

The example [`dataflow/urg_dora.yaml`](dataflow/urg_dora.yaml) is a true source
node: it declares no inputs and declares the `scan` output as required by dora.
Paths are relative to the dataflow file's directory.

```bash
dora validate dataflow/urg_dora.yaml
dora run dataflow/urg_dora.yaml
```

From the repository root, `dataflow/urg_dora.yaml` resolves the node binary and
config relative to the `dataflow/` directory, so no path edits are needed for
the example layout.

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
- This project currently targets the official dora C++ API revision pinned in
  CMake. Override `DORA_ROOT_DIR` deliberately when dora APIs change.

## License and attribution

This project is Apache-2.0 licensed. Ported sections retain the eSOL copyright
and Apache-2.0 notice from `urg_node2`. urg_library is Simplified BSD licensed
and is pinned as a Git submodule whose C API sources are compiled into the
node. The dora CMake bridge integration is adapted from dora's Apache-2.0
official example. See [`NOTICE`](NOTICE) for the attribution summary and each
upstream repository for its full license.
