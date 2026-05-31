# FlowQ Interop Test Tools
# Build with: cmake --preset windows-msvc-vcpkg -DFLOWQ_BUILD_INTEROP=ON

cmake_minimum_required(VERSION 3.25)

# Find QUIC implementations
find_package(msquic CONFIG QUIET)
find_package(ngtcp2 CONFIG QUIET)
find_package(lsquic CONFIG QUIET)

# Interop test runner
add_executable(flowq_interop_runner
    interop_runner.cpp
)

target_link_libraries(flowq_interop_runner PRIVATE
    FlowQ::flowq
)

flowq_enable_project_warnings(flowq_interop_runner)

# MsQuic interop test
if(msquic_FOUND)
    message(STATUS "MsQuic found, building MsQuic interop test")
    add_executable(flowq_msquic_interop
        msquic_interop.cpp
    )
    target_link_libraries(flowq_msquic_interop PRIVATE
        FlowQ::flowq
        msquic
    )
    flowq_enable_project_warnings(flowq_msquic_interop)
else()
    message(STATUS "MsQuic not found, skipping MsQuic interop test")
endif()

# ngtcp2 interop test
if(ngtcp2_FOUND)
    message(STATUS "ngtcp2 found, building ngtcp2 interop test")
    add_executable(flowq_ngtcp2_interop
        ngtcp2_interop.cpp
    )
    target_link_libraries(flowq_ngtcp2_interop PRIVATE
        FlowQ::flowq
        ngtcp2
    )
    flowq_enable_project_warnings(flowq_ngtcp2_interop)
else()
    message(STATUS "ngtcp2 not found, skipping ngtcp2 interop test")
endif()

# lsquic interop test
if(lsquic_FOUND)
    message(STATUS "lsquic found, building lsquic interop test")
    add_executable(flowq_lsquic_interop
        lsquic_interop.cpp
    )
    target_link_libraries(flowq_lsquic_interop PRIVATE
        FlowQ::flowq
        lsquic
    )
    flowq_enable_project_warnings(flowq_lsquic_interop)
else()
    message(STATUS "lsquic not found, skipping lsquic interop test")
endif()

# Install interop binaries
install(TARGETS flowq_interop_runner
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(msquic_FOUND)
    install(TARGETS flowq_msquic_interop
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(ngtcp2_FOUND)
    install(TARGETS flowq_ngtcp2_interop
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(lsquic_FOUND)
    install(TARGETS flowq_lsquic_interop
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
