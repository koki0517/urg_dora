// Copyright 2022 eSOL Co.,Ltd.
// Copyright 2026 urg_dora contributors
// SPDX-License-Identifier: Apache-2.0

#include "urg_dora/time_sync.hpp"

#include <cmath>

namespace urg_dora {

SystemTime system_now() {
  return std::chrono::time_point_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now());
}

std::int64_t to_nanoseconds(SystemTime time) {
  return time.time_since_epoch().count();
}

void TimeSynchronizer::reset() {
  hardware_clock_seconds_ = 0.0;
  last_hardware_timestamp_ = 0;
  hardware_clock_adjustment_seconds_ = 0.0;
  adjustment_count_ = 0;
}

SystemTime TimeSynchronizer::synchronize(long hardware_timestamp_ms,
                                         SystemTime system_time,
                                         bool *clock_warp) {
  if (clock_warp != nullptr) {
    *clock_warp = false;
  }

  const auto t1 = static_cast<std::uint32_t>(hardware_timestamp_ms);
  const auto t0 = static_cast<std::uint32_t>(last_hardware_timestamp_);
  constexpr std::uint32_t kTimestampMask = 0x00ffffffU;
  const double delta_seconds =
      static_cast<double>(kTimestampMask & (t1 - t0)) / 1000.0;
  hardware_clock_seconds_ += delta_seconds;

  const double system_seconds =
      std::chrono::duration<double>(system_time.time_since_epoch()).count();
  const double current_adjustment = system_seconds - hardware_clock_seconds_;
  if (adjustment_count_ > 0) {
    hardware_clock_adjustment_seconds_ =
        kAdjustmentAlpha * current_adjustment +
        (1.0 - kAdjustmentAlpha) * hardware_clock_adjustment_seconds_;
  } else {
    hardware_clock_adjustment_seconds_ = current_adjustment;
  }
  ++adjustment_count_;
  last_hardware_timestamp_ = hardware_timestamp_ms;

  // urg_node2 returns the receive-side system clock for the first 100 scans.
  if (adjustment_count_ > 100) {
    const auto adjusted =
        SystemTime(std::chrono::nanoseconds(static_cast<std::int64_t>(
            (hardware_clock_seconds_ + hardware_clock_adjustment_seconds_) *
            1.0e9)));
    if (std::abs(
            std::chrono::duration<double>(adjusted - system_time).count()) >
        0.1) {
      reset();
      if (clock_warp != nullptr) {
        *clock_warp = true;
      }
      return system_time;
    }
    return adjusted;
  }
  return system_time;
}

} // namespace urg_dora
