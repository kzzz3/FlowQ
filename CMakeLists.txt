cmake_minimum_required(VERSION 3.25)

project(FlowQ VERSION 0.1.0 LANGUAGES CXX)

option(FLOWQ_BUILD_TESTS "Build FlowQ tests" ON)

add_library(flowq INTERFACE)
add_library(FlowQ::flowq ALIAS flowq)

target_compile_features(flowq INTERFACE cxx_std_20)
target_compile_definitions(flowq INTERFACE $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>)
target_include_directories(
    flowq
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

find_package(asio CONFIG REQUIRED)
find_package(stdexec CONFIG REQUIRED)

target_link_libraries(
    flowq
    INTERFACE
        asio::asio
        STDEXEC::stdexec
)

if(FLOWQ_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
