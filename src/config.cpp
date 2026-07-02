// Copyright 2026 urg_dora contributors
// SPDX-License-Identifier: Apache-2.0

#include "urg_dora/config.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>

namespace urg_dora {
namespace {

template <typename T>
void read_optional(const YAML::Node &root, const char *key, T &value) {
  if (root[key]) {
    value = root[key].as<T>();
  }
}

} // namespace

Config load_config(const std::string &path) {
  const YAML::Node root = YAML::LoadFile(path);
  if (!root.IsMap()) {
    throw std::runtime_error("configuration root must be a YAML mapping");
  }

  Config config;
  read_optional(root, "ip_address", config.ip_address);
  read_optional(root, "ip_port", config.ip_port);
  read_optional(root, "frame_id", config.frame_id);
  read_optional(root, "angle_min", config.angle_min);
  read_optional(root, "angle_max", config.angle_max);
  read_optional(root, "cluster", config.cluster);
  read_optional(root, "skip", config.skip);
  read_optional(root, "calibrate_time", config.calibrate_time);
  read_optional(root, "synchronize_time", config.synchronize_time);
  read_optional(root, "time_offset", config.time_offset);
  read_optional(root, "error_limit", config.error_limit);
  read_optional(root, "error_reset_period", config.error_reset_period);

  if (config.ip_address.empty()) {
    throw std::runtime_error(
        "ip_address is required; v0.1 supports Ethernet only");
  }
  if (config.ip_port < 1 || config.ip_port > 65535) {
    throw std::runtime_error("ip_port must be in the range 1..65535");
  }
  if (!std::isfinite(config.angle_min) || !std::isfinite(config.angle_max) ||
      !std::isfinite(config.time_offset)) {
    throw std::runtime_error("angles and time_offset must be finite");
  }
  if (config.error_limit < 0) {
    throw std::runtime_error("error_limit must be non-negative");
  }
  if (!std::isfinite(config.error_reset_period) ||
      config.error_reset_period <= 0.0) {
    throw std::runtime_error("error_reset_period must be greater than zero");
  }

  // These are the same bounds applied by urg_node2 before configuring URG.
  constexpr double kPi = 3.14159265358979323846;
  config.angle_min = std::clamp(config.angle_min, -kPi, kPi);
  config.angle_max = std::clamp(config.angle_max, -kPi, kPi);
  config.skip = std::clamp(config.skip, 0, 9);
  config.cluster = std::clamp(config.cluster, 1, 99);

  const auto first_non_slash = config.frame_id.find_first_not_of('/');
  config.frame_id = first_non_slash == std::string::npos
                        ? std::string{}
                        : config.frame_id.substr(first_non_slash);
  return config;
}

} // namespace urg_dora
