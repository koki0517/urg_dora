// Copyright 2026 urg_dora contributors
// SPDX-License-Identifier: Apache-2.0

#include "urg_dora/config.hpp"

#include <cassert>
#include <string>

int main() {
  const auto config = urg_dora::load_config(std::string(URG_DORA_SOURCE_DIR) +
                                            "/config/urg_dora.yaml");
  assert(config.ip_address == "192.168.0.10");
  assert(config.ip_port == 10940);
  assert(config.frame_id == "laser");
  assert(config.cluster == 1);
  assert(config.skip == 0);
  assert(config.calibrate_time);
  assert(config.synchronize_time);
  return 0;
}
