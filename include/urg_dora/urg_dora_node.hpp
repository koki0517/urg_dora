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
#include <memory>
#include <string>
#include <vector>

#include <arrow/api.h>
#include <urg_sensor.h>

#include "urg_dora/config.hpp"
#include "urg_dora/time_sync.hpp"

struct DoraNode;

namespace urg_dora {

struct Scan {
  std::int64_t stamp_ns = 0;
  std::string frame_id;
  float angle_min = 0.0F;
  float angle_max = 0.0F;
  float angle_increment = 0.0F;
  float time_increment = 0.0F;
  float scan_time = 0.0F;
  float range_min = 0.0F;
  float range_max = 0.0F;
  std::vector<float> ranges;
};

class UrgDoraNode {
public:
  UrgDoraNode(Config config, DoraNode &dora_node);
  ~UrgDoraNode();

  UrgDoraNode(const UrgDoraNode &) = delete;
  UrgDoraNode &operator=(const UrgDoraNode &) = delete;

  int run();

private:
  bool dora_stop_requested();
  bool connect();
  void disconnect();
  bool configure_scan();
  bool start_measurement();
  void stop_measurement();
  bool read_scan(Scan &scan);
  bool send_scan(const Scan &scan);

  void calibrate_system_latency(std::size_t measurements);
  std::chrono::nanoseconds get_native_clock_offset(std::size_t measurements);
  std::chrono::nanoseconds get_scan_timestamp_offset(std::size_t measurements);
  std::chrono::nanoseconds angular_time_offset() const;
  void reset_time_state();

  std::shared_ptr<arrow::Array> make_scan_array(const Scan &scan) const;
  void log_startup_details() const;

  Config config_;
  DoraNode &dora_node_;
  urg_t urg_{};
  bool connected_ = false;
  bool measurement_started_ = false;
  bool stop_requested_ = false;

  int first_step_ = 0;
  int last_step_ = 0;
  double scan_period_seconds_ = 0.0;
  float angle_min_ = 0.0F;
  float angle_max_ = 0.0F;
  float angle_increment_ = 0.0F;
  float time_increment_ = 0.0F;
  float range_min_ = 0.0F;
  float range_max_ = 0.0F;
  std::vector<long> distances_;

  std::chrono::nanoseconds system_latency_{0};
  std::chrono::nanoseconds user_latency_{0};
  TimeSynchronizer time_synchronizer_;
  std::uint64_t scan_count_ = 0;
  std::chrono::nanoseconds read_get_distance_accumulator_{0};
  std::chrono::nanoseconds read_synchronize_accumulator_{0};
  std::chrono::nanoseconds read_finalize_accumulator_{0};
};

} // namespace urg_dora
