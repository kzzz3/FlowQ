#include <flowq/quic/diagnostics.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("diagnostics disabled by default") {
    flowq::quic::diagnostics diag{};

    CHECK_FALSE(diag.enabled());
}

TEST_CASE("diagnostics enabled when sink is set") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    CHECK(diag.enabled());
}

TEST_CASE("diagnostics emits packet_sent event") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.packet_sent(42, 1200);

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::packet_sent);
    CHECK(collector.events()[0].category == "transport");
    CHECK(collector.events()[0].message.find("pn=42") != std::string::npos);
    CHECK(collector.events()[0].message.find("bytes=1200") != std::string::npos);
}

TEST_CASE("diagnostics emits packet_received event") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.packet_received(10, 800);

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::packet_received);
    CHECK(collector.events()[0].message.find("pn=10") != std::string::npos);
}

TEST_CASE("diagnostics emits packet_lost event") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.packet_lost(5);

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::packet_lost);
    CHECK(collector.events()[0].category == "recovery");
}

TEST_CASE("diagnostics emits key_updated event") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.key_updated("handshake");

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::key_updated);
    CHECK(collector.events()[0].category == "security");
    CHECK(collector.events()[0].message.find("handshake") != std::string::npos);
}

TEST_CASE("diagnostics emits congestion_state_changed event") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.congestion_state_changed("slow_start", "congestion_avoidance");

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::congestion_state_changed);
    CHECK(collector.events()[0].category == "congestion");
    CHECK(collector.events()[0].message.find("slow_start") != std::string::npos);
    CHECK(collector.events()[0].message.find("congestion_avoidance") != std::string::npos);
}

TEST_CASE("diagnostics emits transport_parameter_decoded event") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.transport_parameter_decoded("max_udp_payload_size");

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::transport_parameter_decoded);
    CHECK(collector.events()[0].message.find("max_udp_payload_size") != std::string::npos);
}

TEST_CASE("diagnostics does not emit when disabled") {
    flowq::quic::diagnostics diag{};

    diag.packet_sent(1, 100);
    diag.packet_received(2, 200);
    diag.packet_lost(3);
    diag.key_updated("initial");
    diag.congestion_state_changed("slow_start", "congestion_avoidance");
    diag.transport_parameter_decoded("max_idle_timeout");

    CHECK_FALSE(diag.enabled());
}

TEST_CASE("diagnostics collects multiple events in order") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    diag.packet_sent(1, 100);
    diag.packet_received(2, 200);
    diag.packet_lost(3);

    REQUIRE(collector.count() == 3);
    CHECK(collector.events()[0].type == flowq::quic::diagnostic_event_type::packet_sent);
    CHECK(collector.events()[1].type == flowq::quic::diagnostic_event_type::packet_received);
    CHECK(collector.events()[2].type == flowq::quic::diagnostic_event_type::packet_lost);
}

TEST_CASE("diagnostic collector clears events") {
    flowq::quic::diagnostic_collector collector{};

    collector.emit(flowq::quic::diagnostic_event{
        flowq::quic::diagnostic_event_type::packet_sent,
        std::chrono::steady_clock::now(),
        "test",
        "test event"
    });
    REQUIRE(collector.count() == 1);

    collector.clear();
    CHECK(collector.count() == 0);
}

TEST_CASE("null diagnostic sink discards all events") {
    flowq::quic::null_diagnostic_sink null_sink{};
    flowq::quic::diagnostics diag{&null_sink};

    diag.packet_sent(1, 100);
    diag.packet_lost(2);

    CHECK(diag.enabled());
    // No way to verify events were discarded without collector, but no crash is the test
}

TEST_CASE("diagnostics can switch sink at runtime") {
    flowq::quic::diagnostic_collector collector_a{};
    flowq::quic::diagnostic_collector collector_b{};
    flowq::quic::diagnostics diag{&collector_a};

    diag.packet_sent(1, 100);
    CHECK(collector_a.count() == 1);

    diag.set_sink(&collector_b);
    diag.packet_sent(2, 200);
    CHECK(collector_a.count() == 1);
    CHECK(collector_b.count() == 1);
}

TEST_CASE("diagnostics events have timestamps") {
    flowq::quic::diagnostic_collector collector{};
    flowq::quic::diagnostics diag{&collector};

    const auto before = std::chrono::steady_clock::now();
    diag.packet_sent(1, 100);
    const auto after = std::chrono::steady_clock::now();

    REQUIRE(collector.count() == 1);
    CHECK(collector.events()[0].timestamp >= before);
    CHECK(collector.events()[0].timestamp <= after);
}
