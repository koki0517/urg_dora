cmake_minimum_required(VERSION 3.21)
project(urg_dora VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(URG_DORA_BUILD_TESTS "Build hardware-independent tests" ON)
option(URG_DORA_USE_DORA_SOURCE
  "Build dora C++ API from a local dora source checkout"
  OFF)
set(URG_LIBRARY_ROOT "" CACHE PATH
    "Optional external urg_library root; empty uses the bundled submodule")

find_package(Arrow CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
  set(URG_DORA_YAML_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
  set(URG_DORA_YAML_TARGET yaml-cpp)
else()
  message(FATAL_ERROR "yaml-cpp package did not define a supported CMake target")
endif()

if(URG_LIBRARY_ROOT)
  find_path(URG_INCLUDE_DIR urg_sensor.h
    HINTS "${URG_LIBRARY_ROOT}"
    PATH_SUFFIXES current/include/c include/c include/urg_c include
    NO_DEFAULT_PATH)
  find_library(URG_C_LIBRARY NAMES urg_c
    HINTS "${URG_LIBRARY_ROOT}"
    PATH_SUFFIXES current/src src lib lib64
    NO_DEFAULT_PATH)
  if(NOT URG_INCLUDE_DIR OR NOT URG_C_LIBRARY)
    message(FATAL_ERROR
      "External urg_library not found below URG_LIBRARY_ROOT=${URG_LIBRARY_ROOT}")
  endif()
  set(URG_C_TARGET "${URG_C_LIBRARY}")
else()
  set(URG_LIBRARY_CURRENT
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/urg_library/current")
  set(URG_INCLUDE_DIR "${URG_LIBRARY_CURRENT}/include/c")
  if(NOT EXISTS "${URG_INCLUDE_DIR}/urg_sensor.h")
    message(FATAL_ERROR
      "urg_library submodule is missing. Run: git submodule update --init --recursive")
  endif()

  add_library(urg_c STATIC
    "${URG_LIBRARY_CURRENT}/src/urg_sensor.c"
    "${URG_LIBRARY_CURRENT}/src/urg_utils.c"
    "${URG_LIBRARY_CURRENT}/src/urg_debug.c"
    "${URG_LIBRARY_CURRENT}/src/urg_connection.c"
    "${URG_LIBRARY_CURRENT}/src/urg_ring_buffer.c"
    "${URG_LIBRARY_CURRENT}/src/urg_serial.c"
    "${URG_LIBRARY_CURRENT}/src/urg_serial_utils.c"
    "${URG_LIBRARY_CURRENT}/src/urg_tcpclient.c"
  )
  target_include_directories(urg_c PUBLIC "${URG_INCLUDE_DIR}")
  if(UNIX)
    target_link_libraries(urg_c PUBLIC m)
  endif()
  set(URG_C_TARGET urg_c)
endif()

add_executable(urg_dora_node
  src/main.cpp
  src/config.cpp
  src/time_sync.cpp
  src/urg_dora_node.cpp
)
target_include_directories(urg_dora_node PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(urg_dora_node PRIVATE -Wall -Wextra -Wpedantic)

if(URG_DORA_USE_DORA_SOURCE)
  include(cmake/DoraTargets.cmake)
  target_sources(urg_dora_node PRIVATE "${node_bridge}")
  add_dependencies(urg_dora_node Dora_cxx)
  target_include_directories(urg_dora_node PRIVATE "${dora_cxx_include_dir}")
  target_link_libraries(urg_dora_node PRIVATE "${dora_node_api_library}")
else()
  find_package(dora-node-api-cxx CONFIG REQUIRED)
  target_sources(urg_dora_node PRIVATE ${dora-node-api-cxx_CXX_BRIDGE_FILES})
  target_link_libraries(urg_dora_node PRIVATE
    dora-node-api-cxx::dora-node-api-cxx
    dora-node-api-cxx::extra
  )
endif()

target_include_directories(urg_dora_node PRIVATE "${URG_INCLUDE_DIR}")
target_link_libraries(urg_dora_node PRIVATE
  Arrow::arrow_shared
  "${URG_DORA_YAML_TARGET}"
  "${URG_C_TARGET}"
)

install(TARGETS urg_dora_node RUNTIME DESTINATION bin)
install(FILES config/urg_dora.yaml DESTINATION share/urg_dora/config)
install(FILES dataflow/urg_dora.yaml DESTINATION share/urg_dora/dataflow)

if(URG_DORA_BUILD_TESTS)
  enable_testing()
  add_executable(time_sync_test tests/time_sync_test.cpp src/time_sync.cpp)
  target_include_directories(time_sync_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_options(time_sync_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME time_sync_test COMMAND time_sync_test)

  add_executable(config_test tests/config_test.cpp src/config.cpp)
  target_include_directories(config_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(config_test PRIVATE "${URG_DORA_YAML_TARGET}")
  target_compile_definitions(config_test PRIVATE
    URG_DORA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(config_test PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME config_test COMMAND config_test)
endif()
