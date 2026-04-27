find_package(Catch2 3 CONFIG REQUIRED)

add_executable(
    flowq_unit_tests
    integration/quic_loopback_tests.cpp
    unit/buffer_tests.cpp
    unit/context_tests.cpp
    unit/endpoint_tests.cpp
    unit/error_tests.cpp
    unit/execution_tests.cpp
    unit/protocol_core_tests.cpp
    unit/quic_ack_loss_tests.cpp
    unit/quic_connection_tests.cpp
    unit/quic_frame_tests.cpp
    unit/quic_packet_header_tests.cpp
    unit/quic_packet_pipeline_tests.cpp
    unit/quic_recovery_core_tests.cpp
    unit/quic_stream_tests.cpp
    unit/quic_varint_tests.cpp
    unit/timer_tests.cpp
    unit/udp_tests.cpp
)

target_link_libraries(
    flowq_unit_tests
    PRIVATE
        FlowQ::flowq
        Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(flowq_unit_tests)
