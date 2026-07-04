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

#include "urg_dora/urg_dora_node.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arrow/c/bridge.h>
#include <urg_utils.h>

#include "dora-node-api.h"

namespace urg_dora {
namespace {

constexpr std::size_t kCalibrationMeasurements = 10;
constexpr int kMaximumDataSize = 5000; // Same defensive cap as urg_node2.
constexpr double kPi = 3.14159265358979323846;

template <typename Builder, typename Value>
std::shared_ptr<arrow::Array> scalar_array(Value &&value) {
  Builder builder;
  auto status = builder.Append(std::forward<Value>(value));
  if (!status.ok()) {
    throw std::runtime_error("Arrow append failed: " + status.ToString());
  }
  auto result = builder.Finish();
  if (!result.ok()) {
    throw std::runtime_error("Arrow finish failed: " +
                             result.status().ToString());
  }
  return result.ValueOrDie();
}

std::chrono::nanoseconds seconds_to_nanoseconds(double seconds) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(seconds));
}

std::chrono::nanoseconds median(std::vector<std::chrono::nanoseconds> &values) {
  if (values.empty()) {
    throw std::runtime_error(
        "cannot calculate a median from zero measurements");
  }
  const auto middle =
      values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::nth_element(values.begin(), middle, values.end());
  return *middle;
}

} // namespace

UrgDoraNode::UrgDoraNode(Config config, DoraNode &dora_node)
    : config_(std::move(config)), dora_node_(dora_node),
      user_latency_(seconds_to_nanoseconds(config_.time_offset)) {}

UrgDoraNode::~UrgDoraNode() { disconnect(); }

int UrgDoraNode::run() {
  int reconnect_count = 0;

  while (!dora_stop_requested()) {
    if (!connected_) {
      if (!connect()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
      if (!configure_scan()) {
        disconnect();
        ++reconnect_count;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
      reset_time_state();
      if (config_.calibrate_time) {
        calibrate_system_latency(kCalibrationMeasurements);
      }
      if (!start_measurement()) {
        std::cerr << "Could not start Hokuyo measurement: " << urg_error(&urg_)
                  << '\n';
        disconnect();
        ++reconnect_count;
        continue;
      }
    }

    int error_count = 0;
    auto error_window_start = std::chrono::system_clock::now();
    while (connected_ && !dora_stop_requested()) {
      Scan scan;
      const bool read_ok = read_scan(scan);
      if (read_ok) {
        if (!send_scan(scan)) {
          return 1;
        }
      } else {
        ++error_count;
        std::cerr << "Could not get single-echo scan (" << error_count << "/"
                  << config_.error_limit << "): " << urg_error(&urg_) << '\n';
      }

      // Upstream reconnects only after the count is strictly greater than the
      // limit.
      if (error_count > config_.error_limit) {
        std::cerr << "Error count exceeded limit; reconnecting Hokuyo (attempt "
                  << (reconnect_count + 1) << ")\n";
        stop_measurement();
        disconnect();
        reset_time_state();
        ++reconnect_count;
        break;
      }

      const auto now = std::chrono::system_clock::now();
      if (std::chrono::duration<double>(now - error_window_start).count() >=
          config_.error_reset_period) {
        error_window_start = now;
        error_count = 0;
      }
    }
  }

  stop_measurement();
  disconnect();
  return 0;
}

bool UrgDoraNode::dora_stop_requested() {
  if (stop_requested_) {
    return true;
  }
  for (;;) {
    auto event = try_next_event(dora_node_.events);
    const auto type = event_type(event);
    if (type == DoraEventType::Empty || type == DoraEventType::Timeout) {
      return false;
    }
    if (type == DoraEventType::Stop || type == DoraEventType::AllInputsClosed) {
      stop_requested_ = true;
      return true;
    }
    if (type == DoraEventType::Error) {
      std::cerr << "Dora delivered an error event; stopping source node\n";
      stop_requested_ = true;
      return true;
    }
    // A source has no declared inputs. Ignore any other control event while
    // draining the queue, then poll again until Empty.
  }
}

bool UrgDoraNode::connect() {
  std::cerr << "Connecting to Hokuyo at " << config_.ip_address << ':'
            << config_.ip_port << std::endl;
  const int result = urg_open(&urg_, URG_ETHERNET, config_.ip_address.c_str(),
                              config_.ip_port);
  if (result < 0) {
    std::cerr << "Could not open network Hokuyo: " << urg_error(&urg_)
              << std::endl;
    return false;
  }
  connected_ = true;
  scan_period_seconds_ = 1.0e-6 * static_cast<double>(urg_scan_usec(&urg_));
  std::cerr << "Connected: product=" << urg_sensor_product_type(&urg_)
            << " serial=" << urg_sensor_serial_id(&urg_)
            << " firmware=" << urg_sensor_firmware_version(&urg_)
            << std::endl;
  return true;
}

void UrgDoraNode::disconnect() {
  stop_measurement();
  if (connected_) {
    urg_close(&urg_);
    connected_ = false;
  }
}

bool UrgDoraNode::configure_scan() {
  int data_size = urg_max_data_size(&urg_);
  if (data_size <= 0) {
    std::cerr << "Invalid maximum data size from sensor: " << data_size << '\n';
    return false;
  }
  data_size = std::min(data_size, kMaximumDataSize);
  distances_.assign(static_cast<std::size_t>(data_size), 0L);

  first_step_ = urg_rad2step(&urg_, config_.angle_min);
  last_step_ = urg_rad2step(&urg_, config_.angle_max);
  if (last_step_ < first_step_) {
    std::swap(first_step_, last_step_);
  }
  if (last_step_ == first_step_) {
    int minimum_step = 0;
    int maximum_step = 0;
    urg_step_min_max(&urg_, &minimum_step, &maximum_step);
    if (first_step_ == minimum_step) {
      ++last_step_;
    } else {
      --first_step_;
    }
  }

  if (urg_set_scanning_parameter(&urg_, first_step_, last_step_,
                                 config_.cluster) < 0) {
    std::cerr << "Could not set scanning parameters: " << urg_error(&urg_)
              << '\n';
    return false;
  }

  angle_min_ = static_cast<float>(urg_step2rad(&urg_, first_step_));
  angle_max_ = static_cast<float>(urg_step2rad(&urg_, last_step_));
  angle_increment_ =
      static_cast<float>(config_.cluster * urg_step2rad(&urg_, 1));

  int minimum_step = 0;
  int maximum_step = 0;
  urg_step_min_max(&urg_, &minimum_step, &maximum_step);
  const double angular_fraction_per_step =
      (urg_step2rad(&urg_, maximum_step) - urg_step2rad(&urg_, minimum_step)) /
      (2.0 * kPi) / static_cast<double>(maximum_step - minimum_step);
  time_increment_ = static_cast<float>(
      config_.cluster * angular_fraction_per_step * scan_period_seconds_);

  long minimum_distance_mm = 0;
  long maximum_distance_mm = 0;
  urg_distance_min_max(&urg_, &minimum_distance_mm, &maximum_distance_mm);
  range_min_ = static_cast<float>(minimum_distance_mm / 1000.0);
  range_max_ = static_cast<float>(maximum_distance_mm / 1000.0);

  return true;
}

bool UrgDoraNode::start_measurement() {
  const int result =
      urg_start_measurement(&urg_, URG_DISTANCE, 0, config_.skip, 0);
  measurement_started_ = result >= 0;
  return measurement_started_;
}

void UrgDoraNode::stop_measurement() {
  if (measurement_started_) {
    urg_stop_measurement(&urg_);
    measurement_started_ = false;
  }
}

bool UrgDoraNode::read_scan(Scan &scan) {
  long hardware_timestamp_ms = 0;
  const SystemTime receive_time = system_now();
  const int beam_count =
      urg_get_distance(&urg_, distances_.data(), &hardware_timestamp_ms);
  if (beam_count <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(beam_count) > distances_.size()) {
    std::cerr << "Sensor returned more beams than the allocated URG buffer\n";
    return false;
  }

  SystemTime stamp = receive_time;
  if (config_.synchronize_time) {
    bool clock_warp = false;
    stamp = time_synchronizer_.synchronize(hardware_timestamp_ms, receive_time,
                                           &clock_warp);
    if (clock_warp) {
        std::cerr << "Detected clock warp; reset timestamp EMA" << std::endl;
    }
  }

  // This mirrors urg_node2 exactly. calibrate_system_latency() already adds
  // angular_time_offset(), and this expression adds it again. The apparent
  // double offset is retained for header.stamp compatibility with upstream.
  stamp += system_latency_ + user_latency_ + angular_time_offset();

  scan.stamp_ns = to_nanoseconds(stamp);
  scan.frame_id = config_.frame_id;
  scan.angle_min = angle_min_;
  scan.angle_max = angle_max_;
  scan.angle_increment = angle_increment_;
  scan.time_increment = time_increment_;
  scan.scan_time = static_cast<float>(scan_period_seconds_);
  scan.range_min = range_min_;
  scan.range_max = range_max_;
  scan.ranges.resize(static_cast<std::size_t>(beam_count));
  for (int index = 0; index < beam_count; ++index) {
    scan.ranges[static_cast<std::size_t>(index)] =
        distances_[static_cast<std::size_t>(index)] == 0
            ? std::numeric_limits<float>::quiet_NaN()
            : static_cast<float>(distances_[static_cast<std::size_t>(index)]) /
                  1000.0F;
  }
  return true;
}

bool UrgDoraNode::send_scan(const Scan &scan) {
  const auto array = make_scan_array(scan);
  ArrowArray c_array{};
  ArrowSchema c_schema{};
  const auto export_status = arrow::ExportArray(*array, &c_array, &c_schema);
  if (!export_status.ok()) {
    std::cerr << "Failed to export Arrow scan: " << export_status << '\n';
    return false;
  }

  auto result = send_arrow_output(dora_node_.send_output, "scan",
                                  reinterpret_cast<std::uint8_t *>(&c_array),
                                  reinterpret_cast<std::uint8_t *>(&c_schema));
  if (!result.error.empty()) {
    // Dora takes ownership only on success.
    if (c_array.release != nullptr) {
      c_array.release(&c_array);
    }
    if (c_schema.release != nullptr) {
      c_schema.release(&c_schema);
    }
    std::cerr << "Failed to send scan output: " << std::string(result.error)
              << '\n';
    return false;
  }
  return true;
}

void UrgDoraNode::calibrate_system_latency(std::size_t measurements) {
  if (!connected_) {
    std::cerr << "Unable to calibrate time offset: sensor is not connected\n";
    return;
  }

  std::cerr << "Starting experimental time calibration (" << measurements
            << " measurements)" << std::endl;
  system_latency_ = std::chrono::nanoseconds::zero();
  try {
    const auto starting_offset = get_native_clock_offset(1);
    auto previous_offset = std::chrono::nanoseconds::zero();
    std::vector<std::chrono::nanoseconds> offsets;
    offsets.reserve(measurements);

    for (std::size_t index = 0; index < measurements; ++index) {
      const auto scan_offset = get_scan_timestamp_offset(1);
      const auto post_offset = get_native_clock_offset(1);
      const auto adjusted_scan_offset = scan_offset - starting_offset;
      const auto adjusted_post_offset = post_offset - starting_offset;
      const auto average_offset = (adjusted_post_offset + previous_offset) / 2;
      offsets.push_back(adjusted_scan_offset - average_offset);
      previous_offset = adjusted_post_offset;
    }

    system_latency_ = median(offsets) + angular_time_offset();
    std::cerr << "Time calibration finished: latency="
              << std::chrono::duration<double>(system_latency_).count()
              << " s" << std::endl;
  } catch (const std::exception &error) {
    stop_measurement();
    system_latency_ = std::chrono::nanoseconds::zero();
    std::cerr << "Could not calibrate time offset: " << error.what() << '\n';
  }
}

std::chrono::nanoseconds
UrgDoraNode::get_native_clock_offset(std::size_t measurements) {
  if (measurement_started_) {
    throw std::runtime_error("cannot get native clock offset while measuring");
  }
  if (urg_start_time_stamp_mode(&urg_) < 0) {
    throw std::runtime_error("cannot start URG timestamp mode");
  }

  try {
    std::vector<std::chrono::nanoseconds> offsets;
    offsets.reserve(measurements);
    for (std::size_t index = 0; index < measurements; ++index) {
      const auto request_time = system_now();
      const long lidar_timestamp_ms = urg_time_stamp(&urg_);
      if (lidar_timestamp_ms < 0) {
        throw std::runtime_error(
            std::string("cannot read native URG timestamp: ") +
            urg_error(&urg_));
      }
      const auto response_time = system_now();
      const auto average_time = SystemTime(std::chrono::nanoseconds(
          (to_nanoseconds(request_time) + to_nanoseconds(response_time)) / 2));
      const auto lidar_time =
          SystemTime(std::chrono::milliseconds(lidar_timestamp_ms));
      offsets.push_back(lidar_time - average_time);
    }
    if (urg_stop_time_stamp_mode(&urg_) < 0) {
      throw std::runtime_error("cannot stop URG timestamp mode");
    }
    return median(offsets);
  } catch (...) {
    urg_stop_time_stamp_mode(&urg_);
    throw;
  }
}

std::chrono::nanoseconds
UrgDoraNode::get_scan_timestamp_offset(std::size_t measurements) {
  if (measurement_started_) {
    throw std::runtime_error(
        "cannot get scan timestamp offset while measuring");
  }
  if (urg_start_measurement(&urg_, URG_DISTANCE, 0, config_.skip, 0) < 0) {
    throw std::runtime_error(std::string("cannot start calibration scan: ") +
                             urg_error(&urg_));
  }
  measurement_started_ = true;

  try {
    std::vector<std::chrono::nanoseconds> offsets;
    offsets.reserve(measurements);
    for (std::size_t index = 0; index < measurements; ++index) {
      long lidar_timestamp_ms = 0;
      // Upstream captures system time immediately before the blocking read.
      const auto system_timestamp = system_now();
      const int result =
          urg_get_distance(&urg_, distances_.data(), &lidar_timestamp_ms);
      if (result <= 0) {
        throw std::runtime_error(std::string("cannot get calibration scan: ") +
                                 urg_error(&urg_));
      }
      const auto lidar_timestamp =
          SystemTime(std::chrono::milliseconds(lidar_timestamp_ms));
      offsets.push_back(lidar_timestamp - system_timestamp);
    }
    stop_measurement();
    return median(offsets);
  } catch (...) {
    stop_measurement();
    throw;
  }
}

std::chrono::nanoseconds UrgDoraNode::angular_time_offset() const {
  // URG timestamps are referenced to the rear of the sensor. Move that
  // reference forward to the configured first step, as urg_node2 does.
  const double circle_fraction =
      (urg_step2rad(&urg_, first_step_) + kPi) / (2.0 * kPi);
  return seconds_to_nanoseconds(circle_fraction * scan_period_seconds_);
}

void UrgDoraNode::reset_time_state() {
  system_latency_ = std::chrono::nanoseconds::zero();
  time_synchronizer_.reset();
}

std::shared_ptr<arrow::Array>
UrgDoraNode::make_scan_array(const Scan &scan) const {
  std::vector<std::shared_ptr<arrow::Field>> fields{
      arrow::field("stamp_ns", arrow::int64(), false),
      arrow::field("frame_id", arrow::utf8(), false),
      arrow::field("angle_min", arrow::float32(), false),
      arrow::field("angle_max", arrow::float32(), false),
      arrow::field("angle_increment", arrow::float32(), false),
      arrow::field("time_increment", arrow::float32(), false),
      arrow::field("scan_time", arrow::float32(), false),
      arrow::field("range_min", arrow::float32(), false),
      arrow::field("range_max", arrow::float32(), false),
      arrow::field("ranges", arrow::list(arrow::float32()), false),
  };

  arrow::ListBuilder ranges_builder(arrow::default_memory_pool(),
                                    std::make_shared<arrow::FloatBuilder>());
  auto status = ranges_builder.Append();
  if (!status.ok()) {
    throw std::runtime_error("Arrow list append failed: " + status.ToString());
  }
  auto *values =
      static_cast<arrow::FloatBuilder *>(ranges_builder.value_builder());
  status = values->AppendValues(scan.ranges);
  if (!status.ok()) {
    throw std::runtime_error("Arrow range append failed: " + status.ToString());
  }
  auto ranges_result = ranges_builder.Finish();
  if (!ranges_result.ok()) {
    throw std::runtime_error("Arrow range finish failed: " +
                             ranges_result.status().ToString());
  }

  std::vector<std::shared_ptr<arrow::Array>> columns{
      scalar_array<arrow::Int64Builder>(scan.stamp_ns),
      scalar_array<arrow::StringBuilder>(scan.frame_id),
      scalar_array<arrow::FloatBuilder>(scan.angle_min),
      scalar_array<arrow::FloatBuilder>(scan.angle_max),
      scalar_array<arrow::FloatBuilder>(scan.angle_increment),
      scalar_array<arrow::FloatBuilder>(scan.time_increment),
      scalar_array<arrow::FloatBuilder>(scan.scan_time),
      scalar_array<arrow::FloatBuilder>(scan.range_min),
      scalar_array<arrow::FloatBuilder>(scan.range_max),
      ranges_result.ValueOrDie(),
  };
  auto result = arrow::StructArray::Make(std::move(columns), std::move(fields));
  if (!result.ok()) {
    throw std::runtime_error("Arrow struct creation failed: " +
                             result.status().ToString());
  }
  return result.ValueOrDie();
}

} // namespace urg_dora
