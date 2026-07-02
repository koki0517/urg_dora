// Copyright 2026 urg_dora contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace urg_dora {

struct Config {
  std::string ip_address;
  int ip_port = 10940;
  std::string frame_id = "laser";

  double angle_min = -3.14159265358979323846;
  double angle_max = 3.14159265358979323846;
  int cluster = 1;
  int skip = 0;

  bool calibrate_time = false;
  bool synchronize_time = false;
  double time_offset = 0.0;

  int error_limit = 4;
  double error_reset_period = 5.0;
};

Config load_config(const std::string &path);

} // namespace urg_dora
