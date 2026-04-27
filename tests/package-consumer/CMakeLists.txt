cmake_minimum_required(VERSION 3.25)

project(flowq_package_consumer LANGUAGES CXX)

find_package(FlowQ CONFIG REQUIRED)

add_executable(flowq_package_consumer main.cpp)
target_link_libraries(flowq_package_consumer PRIVATE FlowQ::flowq)
