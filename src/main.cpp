// Copyright 2026 urg_dora contributors
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <exception>
#include <iostream>
#include <string>

#include "dora-node-api.h"
#include "urg_dora/config.hpp"
#include "urg_dora/urg_dora_node.hpp"

int main(int argc, char **argv) {
  try {
#ifdef SIGPIPE
    // urg_node2 ignores SIGPIPE because an Ethernet sensor can disappear
    // after urg_open(), causing a socket write to otherwise terminate us.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const std::string config_path = argc > 1 ? argv[1] : "config/urg_dora.yaml";
    const auto config = urg_dora::load_config(config_path);
    auto dora_node = init_dora_node();
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    urg_dora::UrgDoraNode node(config, dora_node);
    return node.run();
  } catch (const std::exception &error) {
    std::cerr << "urg_dora fatal error: " << error.what() << std::endl;
    return 1;
  }
}
