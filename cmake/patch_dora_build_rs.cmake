if(NOT DEFINED DORA_SOURCE_DIR)
  message(FATAL_ERROR "DORA_SOURCE_DIR is required")
endif()

set(_build_rs "${DORA_SOURCE_DIR}/apis/c++/node/build.rs")
file(READ "${_build_rs}" _contents)

if(_contents MATCHES "generated_header")
  return()
endif()

string(REPLACE [[
    // rename header files
    let src_dir = origin_dir();
    let target_dir = src_dir
        .parent()
        .expect("failed to get parent directory of source directory");

    let install_dir = if let Ok(target_path) = std::env::var("DORA_NODE_API_CXX_INSTALL") {
        PathBuf::from_str(&target_path).expect("failed to parse DORA_NODE_API_CXX_INSTALL path")
    } else {
        target_dir.join("install")
    };
    println!("cargo:rerun-if-env-changed=DORA_NODE_API_CXX_INSTALL");

    // recreate target dir
    match std::fs::remove_dir_all(&install_dir) {
        Ok(()) => {}
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(error) => panic!(
            "failed to remove dora-node-api-cxx install dir {}: {error}",
            install_dir.display()
        ),
    }
    std::fs::create_dir_all(&install_dir).unwrap();

    std::fs::copy(
        src_dir.join("lib.rs.h"),
        install_dir.join("dora-node-api.h"),
    )
    .unwrap();
    std::fs::copy(
        src_dir.join("lib.rs.cc"),
        install_dir.join("dora-node-api.cc"),
    )
    .unwrap();

    std::fs::copy(src_dir.join("../../rust/cxx.h"), install_dir.join("cxx.h")).unwrap();

    #[cfg(feature = "ros2-bridge")]
    ros2::generate_ros2_message_header(&install_dir);

    // Generate cmake config files alongside the cxxbridge artefacts so the
    // `xtask stage` post-build step can pick them up and assemble a
    // `find_package(dora-node-api-cxx)`-ready install prefix. See
    // `apis/C-CPP-LIBRARIES.md` and `apis/c/node/cmake/dora-api-config.cmake.in`.
    let cxxbridge_crate_dir = src_dir
        .parent()
        .expect("failed to get cxxbridge crate directory");

    let cmake_dir = cxxbridge_crate_dir.join("lib/cmake").join(PACKAGE);
    let include_dir = cxxbridge_crate_dir.join("include");
    let src_cmake_dir = cxxbridge_crate_dir.join("src");

    std::fs::create_dir_all(&cmake_dir).expect("failed to create cmake directory");
    std::fs::create_dir_all(&include_dir).expect("failed to create include directory");
    std::fs::create_dir_all(&src_cmake_dir).expect("failed to create src directory");

    let version = env!("CARGO_PKG_VERSION");
    let target = compute_target();

    generate_config_cmake(&cmake_dir, &target);
    generate_config_version_cmake(&cmake_dir, version);
    copy_cxx_header(&src_dir, &include_dir);
    copy_cxx_source(&src_dir, &src_cmake_dir);

    // to avoid unnecessary `mut` warning
    bridge_files.clear();
]] [[
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let generated_header = out_dir.join("cxxbridge/include/dora-node-api-cxx/src/lib.rs.h");
    let generated_source = out_dir.join("cxxbridge/sources/dora-node-api-cxx/src/lib.rs.cc");

    // rename header files
    let target_dir = std::env::var("CARGO_TARGET_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| {
            let root = Path::new(env!("CARGO_MANIFEST_DIR"))
                .ancestors()
                .nth(3)
                .expect("failed to get root directory from manifest path");
            root.join("target")
        });

    let install_dir = if let Ok(target_path) = std::env::var("DORA_NODE_API_CXX_INSTALL") {
        PathBuf::from_str(&target_path).expect("failed to parse DORA_NODE_API_CXX_INSTALL path")
    } else {
        target_dir.join("cxxbridge").join(PACKAGE).join("install")
    };
    println!("cargo:rerun-if-env-changed=DORA_NODE_API_CXX_INSTALL");

    // recreate target dir
    match std::fs::remove_dir_all(&install_dir) {
        Ok(()) => {}
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
        Err(error) => panic!(
            "failed to remove dora-node-api-cxx install dir {}: {error}",
            install_dir.display()
        ),
    }
    std::fs::create_dir_all(&install_dir).unwrap();

    std::fs::copy(&generated_header, install_dir.join("dora-node-api.h")).unwrap();
    std::fs::copy(&generated_source, install_dir.join("dora-node-api.cc")).unwrap();

    std::fs::copy(out_dir.join("cxxbridge/include/rust/cxx.h"), install_dir.join("cxx.h")).unwrap();

    #[cfg(feature = "ros2-bridge")]
    ros2::generate_ros2_message_header(&install_dir);

    // Generate cmake config files alongside the cxxbridge artefacts so the
    // `xtask stage` post-build step can pick them up and assemble a
    // `find_package(dora-node-api-cxx)`-ready install prefix. See
    // `apis/C-CPP-LIBRARIES.md` and `apis/c/node/cmake/dora-api-config.cmake.in`.
    let cxxbridge_crate_dir = target_dir.join("cxxbridge").join(PACKAGE);

    let cmake_dir = cxxbridge_crate_dir.join("lib/cmake").join(PACKAGE);
    let include_dir = cxxbridge_crate_dir.join("include");
    let src_cmake_dir = cxxbridge_crate_dir.join("src");

    std::fs::create_dir_all(&cmake_dir).expect("failed to create cmake directory");
    std::fs::create_dir_all(&include_dir).expect("failed to create include directory");
    std::fs::create_dir_all(&src_cmake_dir).expect("failed to create src directory");

    let version = env!("CARGO_PKG_VERSION");
    let target = compute_target();

    generate_config_cmake(&cmake_dir, &target);
    generate_config_version_cmake(&cmake_dir, version);
    copy_cxx_header(&generated_header, &include_dir);
    copy_cxx_source(&generated_source, &src_cmake_dir);

    // to avoid unnecessary `mut` warning
    bridge_files.clear();
]] _patched "${_contents}")

if(NOT _patched STREQUAL _contents)
  file(WRITE "${_build_rs}" "${_patched}")
endif()
