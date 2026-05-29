cmake_minimum_required(VERSION 3.25)

project(FlowQ VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(FLOWQ_BUILD_TESTS "Build FlowQ tests" ON)
option(FLOWQ_BUILD_EXAMPLES "Build FlowQ examples" ON)
option(FLOWQ_BUILD_FUZZ "Build FlowQ fuzz targets (requires libFuzzer)" OFF)
option(FLOWQ_BUILD_INTEROP "Build FlowQ interop harness" OFF)

add_library(flowq INTERFACE)
add_library(FlowQ::flowq ALIAS flowq)

target_compile_features(flowq INTERFACE cxx_std_20)
target_compile_definitions(flowq INTERFACE $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>)
target_include_directories(
    flowq
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

find_package(asio CONFIG REQUIRED)
find_package(stdexec CONFIG REQUIRED)

include(cmake/FlowQCryptoBackendOptions.cmake)
include(cmake/FlowQTlsBackendOptions.cmake)

set(FLOWQ_CONFIG_FIND_OPENSSL "")
if(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    set(FLOWQ_CONFIG_FIND_OPENSSL "find_dependency(OpenSSL COMPONENTS SSL Crypto)")
elseif(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    set(FLOWQ_CONFIG_FIND_OPENSSL "find_dependency(OpenSSL COMPONENTS Crypto)")
endif()

target_link_libraries(
    flowq
    INTERFACE
        asio::asio
        STDEXEC::stdexec
)

if(FLOWQ_BUILD_EXAMPLES)
    add_executable(flowq_example_in_memory_loopback examples/in-memory/loopback.cpp)
    target_link_libraries(flowq_example_in_memory_loopback PRIVATE FlowQ::flowq)

    add_executable(flowq_example_udp_stream_echo examples/udp/stream_echo.cpp)
    target_link_libraries(flowq_example_udp_stream_echo PRIVATE FlowQ::flowq)

    add_executable(flowq_example_protection_policy examples/protection-policy/main.cpp)
    target_link_libraries(flowq_example_protection_policy PRIVATE FlowQ::flowq)

    add_executable(flowq_example_http3 examples/http3/main.cpp)
    target_link_libraries(flowq_example_http3 PRIVATE FlowQ::flowq)

    add_executable(flowq_example_webtransport examples/webtransport/main.cpp)
    target_link_libraries(flowq_example_webtransport PRIVATE FlowQ::flowq)

    add_executable(flowq_example_qpack examples/qpack/main.cpp)
    target_link_libraries(flowq_example_qpack PRIVATE FlowQ::flowq)

    add_executable(flowq_example_echo_server examples/echo/server.cpp)
    target_link_libraries(flowq_example_echo_server PRIVATE FlowQ::flowq)

    add_executable(flowq_example_echo_client examples/echo/client.cpp)
    target_link_libraries(flowq_example_echo_client PRIVATE FlowQ::flowq)

    add_executable(flowq_example_chat_room examples/chat-room/main.cpp)
    target_link_libraries(flowq_example_chat_room PRIVATE FlowQ::flowq)
endif()

if(FLOWQ_BUILD_TESTS)
    include(CTest)
    if(FLOWQ_BUILD_EXAMPLES)
        add_test(NAME flowq.example.in_memory_loopback COMMAND flowq_example_in_memory_loopback)
        add_test(NAME flowq.example.udp_stream_echo COMMAND flowq_example_udp_stream_echo)
        add_test(NAME flowq.example.protection_policy COMMAND flowq_example_protection_policy)
    endif()
    add_test(
        NAME flowq.release_readiness
        COMMAND powershell
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-release-readiness.ps1
            -SourceRoot ${CMAKE_CURRENT_SOURCE_DIR}
            -SkipBuild
    )
    add_subdirectory(tests)
endif()

install(
    TARGETS flowq
    EXPORT FlowQTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT FlowQTargets
    FILE FlowQTargets.cmake
    NAMESPACE FlowQ::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlowQ
)

configure_package_config_file(
    cmake/FlowQConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/FlowQConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlowQ
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/FlowQConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/FlowQConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/FlowQConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlowQ
)

if(FLOWQ_BUILD_FUZZ)
    add_executable(flowq_packet_header_fuzz tests/fuzz/packet_header_fuzz.cpp)
    target_link_libraries(flowq_packet_header_fuzz PRIVATE FlowQ::flowq)
    target_compile_options(flowq_packet_header_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(flowq_packet_header_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)

    add_executable(flowq_frame_codec_fuzz tests/fuzz/frame_codec_fuzz.cpp)
    target_link_libraries(flowq_frame_codec_fuzz PRIVATE FlowQ::flowq)
    target_compile_options(flowq_frame_codec_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(flowq_frame_codec_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)

    add_executable(flowq_fuzz_packet_header tests/fuzz/fuzz_packet_header.cpp)
    target_link_libraries(flowq_fuzz_packet_header PRIVATE FlowQ::flowq)
    target_compile_options(flowq_fuzz_packet_header PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(flowq_fuzz_packet_header PRIVATE -fsanitize=fuzzer,address,undefined)

    add_executable(flowq_fuzz_frame_decode tests/fuzz/fuzz_frame_decode.cpp)
    target_link_libraries(flowq_fuzz_frame_decode PRIVATE FlowQ::flowq)
    target_compile_options(flowq_fuzz_frame_decode PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(flowq_fuzz_frame_decode PRIVATE -fsanitize=fuzzer,address,undefined)

    add_executable(flowq_fuzz_qpack tests/fuzz/fuzz_qpack.cpp)
    target_link_libraries(flowq_fuzz_qpack PRIVATE FlowQ::flowq)
    target_compile_options(flowq_fuzz_qpack PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(flowq_fuzz_qpack PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(FLOWQ_BUILD_INTEROP)
    find_package(Catch2 3 CONFIG REQUIRED)

    add_executable(flowq_interop_tests tests/interop/flowq_endpoint_driver.cpp)
    target_link_libraries(flowq_interop_tests PRIVATE FlowQ::flowq Catch2::Catch2WithMain)
    target_compile_definitions(flowq_interop_tests PRIVATE
        INTEROP_SCENARIOS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/scenarios"
    )

    include(Catch)
    catch_discover_tests(flowq_interop_tests)
endif()
