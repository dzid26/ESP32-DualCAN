# yoziru/tesla-ble — ESP-IDF component wrapper.
#
# Structure:
#   tesla-ble/   git submodule of github.com/yoziru/tesla-ble (pinned to a tag)
#   nanopb/      git submodule of github.com/nanopb/nanopb (the protobuf runtime
#                tesla-ble depends on)
#
# We register the sources directly rather than invoking the library's own
# CMakeLists.txt for two reasons:
#   1) The library uses FetchContent_Populate to grab nanopb at build time;
#      we already have it as a submodule and want to avoid a network call
#      during every clean build.
#   2) It uses CMake project()/set(CMAKE_CXX_STANDARD 23) outside the
#      IDF-component branch; bringing it in as add_subdirectory pollutes
#      the parent build config.

set(TB    "${CMAKE_CURRENT_SOURCE_DIR}/tesla-ble")
set(PB    "${CMAKE_CURRENT_SOURCE_DIR}/nanopb")

idf_component_register(
    SRCS
        # C++ sources from yoziru/tesla-ble
        "${TB}/src/client.cpp"
        "${TB}/src/crypto_context.cpp"
        "${TB}/src/errors.cpp"
        "${TB}/src/logging_bridge.cpp"
        "${TB}/src/message_builders.cpp"
        "${TB}/src/message_processor.cpp"
        "${TB}/src/peer.cpp"
        "${TB}/src/tb_logging.cpp"
        "${TB}/src/tb_utils.cpp"
        "${TB}/src/vehicle.cpp"
        "${TB}/src/vin_utils.cpp"
        # nanopb-generated protobuf sources (committed in the repo)
        "${TB}/generated/src/car_server.pb.c"
        "${TB}/generated/src/common.pb.c"
        "${TB}/generated/src/errors.pb.c"
        "${TB}/generated/src/google/protobuf/timestamp.pb.c"
        "${TB}/generated/src/keys.pb.c"
        "${TB}/generated/src/managed_charging.pb.c"
        "${TB}/generated/src/signatures.pb.c"
        "${TB}/generated/src/universal_message.pb.c"
        "${TB}/generated/src/vcsec.pb.c"
        "${TB}/generated/src/vehicle.pb.c"
        # nanopb runtime
        "${PB}/pb_common.c"
        "${PB}/pb_decode.c"
        "${PB}/pb_encode.c"
    INCLUDE_DIRS
        "${TB}/include"
        "${TB}/generated/include"
        "${PB}"
    REQUIRES
        mbedtls
)

# Upstream uses C++17/20-ish features; bump the standard to keep their
# templates / std::optional usage happy. Applied to the component target.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
