# Adapted from dora-rs/dora examples/cmake-dataflow/DoraTargets.cmake.
# dora is Apache-2.0 licensed. This project only needs the standalone C++ node API.

include(ExternalProject)

set(DORA_ROOT_DIR "" CACHE PATH "Path to an existing dora-rs source checkout")
set(DORA_GIT_TAG "25bac6b3e5ed7435f49bd494e0ff4ef81ee0a674" CACHE STRING
    "dora-rs revision fetched when DORA_ROOT_DIR is empty")

set(dora_cxx_include_dir "${CMAKE_CURRENT_BINARY_DIR}/dora-cxx/include")
set(node_bridge "${CMAKE_CURRENT_BINARY_DIR}/dora-cxx/dora-node-api.cc")

if(DORA_ROOT_DIR)
  get_filename_component(_dora_source "${DORA_ROOT_DIR}" ABSOLUTE)
  set(_dora_target "${_dora_source}/target")
  set(dora_node_api_library "${_dora_target}/debug/${CMAKE_STATIC_LIBRARY_PREFIX}dora_node_api_cxx${CMAKE_STATIC_LIBRARY_SUFFIX}")
  ExternalProject_Add(DoraNodeApi
    SOURCE_DIR "${_dora_source}"
    BUILD_IN_SOURCE TRUE
    PATCH_COMMAND "${CMAKE_COMMAND}" -DDORA_SOURCE_DIR=${_dora_source} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_dora_build_rs.cmake"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${CMAKE_COMMAND}" -E env CARGO_TARGET_DIR=${_dora_target} DORA_NODE_API_CXX_INSTALL=${_dora_target}/cxxbridge/dora-node-api-cxx/install cargo build --package dora-node-api-cxx --target-dir ${_dora_target}
    BUILD_BYPRODUCTS "${dora_node_api_library}"
    INSTALL_COMMAND ""
  )
else()
  set(_dora_source "${CMAKE_CURRENT_BINARY_DIR}/dora/src/DoraNodeApi")
  set(_dora_target "${CMAKE_CURRENT_BINARY_DIR}/dora-target")
  set(dora_node_api_library "${_dora_target}/debug/${CMAKE_STATIC_LIBRARY_PREFIX}dora_node_api_cxx${CMAKE_STATIC_LIBRARY_SUFFIX}")
  ExternalProject_Add(DoraNodeApi
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/dora"
    GIT_REPOSITORY https://github.com/dora-rs/dora.git
    GIT_TAG "${DORA_GIT_TAG}"
    GIT_SHALLOW FALSE
    BUILD_IN_SOURCE TRUE
    PATCH_COMMAND "${CMAKE_COMMAND}" -DDORA_SOURCE_DIR=${_dora_source} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_dora_build_rs.cmake"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${CMAKE_COMMAND}" -E env CARGO_TARGET_DIR=${_dora_target} DORA_NODE_API_CXX_INSTALL=${_dora_target}/cxxbridge/dora-node-api-cxx/install cargo build --package dora-node-api-cxx --target-dir ${_dora_target}
    BUILD_BYPRODUCTS "${dora_node_api_library}"
    INSTALL_COMMAND ""
  )
endif()

add_custom_command(
  OUTPUT "${node_bridge}" "${dora_cxx_include_dir}/dora-node-api.h"
  DEPENDS DoraNodeApi
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${dora_cxx_include_dir}"
  COMMAND "${CMAKE_COMMAND}" -E copy
          "${_dora_target}/cxxbridge/dora-node-api-cxx/install/dora-node-api.cc"
          "${node_bridge}"
  COMMAND "${CMAKE_COMMAND}" -E copy
          "${_dora_target}/cxxbridge/dora-node-api-cxx/install/dora-node-api.h"
          "${dora_cxx_include_dir}/dora-node-api.h"
  VERBATIM
)

add_custom_target(Dora_cxx DEPENDS
  "${node_bridge}" "${dora_cxx_include_dir}/dora-node-api.h")
set_source_files_properties("${node_bridge}" PROPERTIES GENERATED TRUE)
