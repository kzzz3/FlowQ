cmake_minimum_required(VERSION 3.25)

project(FlowQ VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(FLOWQ_BUILD_TESTS "Build FlowQ tests" ON)
option(FLOWQ_BUILD_EXAMPLES "Build FlowQ examples" ON)

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
    add_executable(flowq_example_in_memory_loopback examples/in_memory_loopback.cpp)
    target_link_libraries(flowq_example_in_memory_loopback PRIVATE FlowQ::flowq)

    add_executable(flowq_example_udp_stream_echo examples/udp_stream_echo.cpp)
    target_link_libraries(flowq_example_udp_stream_echo PRIVATE FlowQ::flowq)

    add_executable(flowq_example_protection_policy examples/protection_policy.cpp)
    target_link_libraries(flowq_example_protection_policy PRIVATE FlowQ::flowq)
endif()

if(FLOWQ_BUILD_TESTS)
    include(CTest)
    if(FLOWQ_BUILD_EXAMPLES)
        add_test(NAME flowq.example.in_memory_loopback COMMAND flowq_example_in_memory_loopback)
        add_test(NAME flowq.example.udp_stream_echo COMMAND flowq_example_udp_stream_echo)
        add_test(NAME flowq.example.protection_policy COMMAND flowq_example_protection_policy)
    endif()
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
