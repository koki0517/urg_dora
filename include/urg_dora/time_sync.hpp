// Copyright 2022 eSOL Co.,Ltd.
// Copyright 2026 urg_dora contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <chrono>
#include <cstdint>

namespace urg_dora {

using SystemTime = std::chrono::time_point<std::chrono::system_clock,
                                           std::chrono::nanoseconds>;

SystemTime system_now();
std::int64_t to_nanoseconds(SystemTime time);

// Ports urg_node2's 24-bit hardware-clock unwrapping and EMA correction.
class TimeSynchronizer {
public:
  void reset();

  // hardware_timestamp_ms is the timestamp returned by urg_get_distance().
  // clock_warp is set when upstream's >100-sample, 100 ms warp guard resets.
  SystemTime synchronize(long hardware_timestamp_ms, SystemTime system_time,
                         bool *clock_warp = nullptr);

private:
  double hardware_clock_seconds_ = 0.0;
  long last_hardware_timestamp_ = 0;
  double hardware_clock_adjustment_seconds_ = 0.0;
  int adjustment_count_ = 0;
  static constexpr double kAdjustmentAlpha = 0.01;
};

} // namespace urg_dora
