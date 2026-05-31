# FlowQ Interop Test Tools
# Build with: cmake --preset windows-msvc-vcpkg-interop -DVCPKG_MANIFEST_FEATURES="interop"

cmake_minimum_required(VERSION 3.25)

# Find QUIC implementations
find_package(ngtcp2 CONFIG QUIET)

# Interop test runner
add_executable(flowq_interop_runner
    interop_runner.cpp
)

target_link_libraries(flowq_interop_runner PRIVATE
    FlowQ::flowq
)

flowq_enable_project_warnings(flowq_interop_runner)

# ngtcp2 interop test
if(ngtcp2_FOUND)
    message(STATUS "ngtcp2 found, building ngtcp2 interop test")
    add_executable(flowq_ngtcp2_interop
        ngtcp2_interop.cpp
    )
    target_link_libraries(flowq_ngtcp2_interop PRIVATE
        FlowQ::flowq
    )
    flowq_enable_project_warnings(flowq_ngtcp2_interop)
else()
    message(STATUS "ngtcp2 not found, skipping ngtcp2 interop test")
endif()

# Install interop binaries
install(TARGETS flowq_interop_runner
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(ngtcp2_FOUND)
    install(TARGETS flowq_ngtcp2_interop
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
