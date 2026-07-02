// Copyright 2026 urg_dora contributors
// SPDX-License-Identifier: Apache-2.0

#include "urg_dora/time_sync.hpp"

#include <cassert>
#include <chrono>
#include <cmath>

int main() {
  using namespace std::chrono_literals;
  using urg_dora::SystemTime;
  using urg_dora::TimeSynchronizer;

  TimeSynchronizer synchronizer;
  const auto base = SystemTime(1700000000s);

  // Upstream deliberately uses receive time during the 100-scan warmup.
  for (int index = 0; index < 100; ++index) {
    const auto system = base + index * 25ms;
    const auto result = synchronizer.synchronize(1000 + index * 25, system);
    assert(result == system);
  }

  bool warped = false;
  const auto system = base + 2500ms;
  const auto adjusted = synchronizer.synchronize(3500, system, &warped);
  assert(!warped);
  assert(std::abs(std::chrono::duration<double>(adjusted - system).count()) <
         0.1);

  synchronizer.reset();
  constexpr long kMask = 0x00ffffffL;
  constexpr long kNearWrap = 0x00fffff0L;
  synchronizer.synchronize(kNearWrap, base);
  SystemTime after_wrap{};
  for (int index = 1; index <= 101; ++index) {
    const long timestamp = (kNearWrap + index * 32L) & kMask;
    after_wrap =
        synchronizer.synchronize(timestamp, base + index * 32ms, &warped);
    assert(!warped);
  }
  assert(
      std::abs(std::chrono::duration<double>(after_wrap - (base + 101 * 32ms))
                   .count()) < 0.1);

  // A host clock step greater than upstream's 100 ms guard resets the EMA.
  const long next_timestamp = (kNearWrap + 102L * 32L) & kMask;
  const auto stepped_system = base + 101 * 32ms + 1s;
  const auto reset_result =
      synchronizer.synchronize(next_timestamp, stepped_system, &warped);
  assert(warped);
  assert(reset_result == stepped_system);
  return 0;
}
