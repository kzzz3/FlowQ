#include <flowq/quic/webtransport.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("webtransport_session connects successfully") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;
    config.max_streams_uni = 10;

    flowq::quic::webtransport::webtransport_session session{config};

    CHECK(session.state() == flowq::quic::webtransport::session_state::connecting);

    auto result = session.connect();
    REQUIRE(result.ok());
    CHECK(result.is_connected());
    CHECK(session.state() == flowq::quic::webtransport::session_state::connected);
}

TEST_CASE("webtransport_session rejects double connect") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto result = session.connect();
    CHECK_FALSE(result.ok());
}

TEST_CASE("webtransport_session closes successfully") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto result = session.close();
    REQUIRE(result.ok());
    CHECK(session.state() == flowq::quic::webtransport::session_state::closed);
}

TEST_CASE("webtransport_session rejects close when not connected") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};

    auto result = session.close();
    CHECK_FALSE(result.ok());
}

TEST_CASE("webtransport_session opens bidirectional stream") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream = session.open_bidi_stream();
    REQUIRE(stream.has_value());
    CHECK(stream->type == flowq::quic::webtransport::stream_type::bidirectional);
    CHECK(stream->stream_id == 0);
}

TEST_CASE("webtransport_session opens unidirectional stream") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_uni = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream = session.open_uni_stream();
    REQUIRE(stream.has_value());
    CHECK(stream->type == flowq::quic::webtransport::stream_type::unidirectional);
    CHECK(stream->stream_id == 2);  // Unidirectional streams start at 2
}

TEST_CASE("webtransport_session rejects stream when not connected") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};

    auto stream = session.open_bidi_stream();
    CHECK_FALSE(stream.has_value());
}

TEST_CASE("webtransport_session sends stream data") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream = session.open_bidi_stream();
    REQUIRE(stream.has_value());

    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
    auto result = session.send_stream_data(stream->stream_id, data, true);
    CHECK(result.ok());
}

TEST_CASE("webtransport_session sends datagram") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}};
    auto result = session.send_datagram(data);
    CHECK(result.ok());
}

TEST_CASE("webtransport_session rejects operations when closed") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();
    session.close();

    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}}};
    CHECK_FALSE(session.send_stream_data(0, data).ok());
    CHECK_FALSE(session.send_datagram(data).ok());
    CHECK_FALSE(session.open_bidi_stream().has_value());
    CHECK_FALSE(session.open_uni_stream().has_value());
}

TEST_CASE("webtransport_session_builder creates valid session") {
    auto session = flowq::quic::webtransport::session_builder{}
        .authority("example.com")
        .path("/webtransport")
        .max_streams_bidi(5)
        .max_streams_uni(5)
        .build();

    CHECK(session.config().authority == "example.com");
    CHECK(session.config().path == "/webtransport");
    CHECK(session.config().max_streams_bidi == 5);
    CHECK(session.config().max_streams_uni == 5);
}

TEST_CASE("webtransport_session config has correct defaults") {
    flowq::quic::webtransport::session_config config{};

    CHECK(config.authority.empty());
    CHECK(config.path.empty());
    CHECK(config.max_streams_bidi == 0);
    CHECK(config.max_streams_uni == 0);
}

TEST_CASE("webtransport_session tracks active streams") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;
    config.max_streams_uni = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    CHECK(session.stream_count() == 0);

    auto stream1 = session.open_bidi_stream();
    CHECK(session.stream_count() == 1);

    auto stream2 = session.open_uni_stream();
    CHECK(session.stream_count() == 2);
}

TEST_CASE("webtransport_session checks stream existence") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream = session.open_bidi_stream();
    REQUIRE(stream.has_value());

    CHECK(session.has_stream(stream->stream_id));
    CHECK_FALSE(session.has_stream(999));
}

TEST_CASE("webtransport_session gets stream by ID") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream = session.open_bidi_stream();
    REQUIRE(stream.has_value());

    auto* found = session.get_stream(stream->stream_id);
    REQUIRE(found != nullptr);
    CHECK(found->stream_id == stream->stream_id);
    CHECK(found->type == flowq::quic::webtransport::stream_type::bidirectional);

    auto* not_found = session.get_stream(999);
    CHECK(not_found == nullptr);
}

TEST_CASE("webtransport_session rejects send on closed stream") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream = session.open_bidi_stream();
    REQUIRE(stream.has_value());

    // Close stream with FIN
    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}}};
    auto result = session.send_stream_data(stream->stream_id, data, true);
    REQUIRE(result.ok());

    // Try to send again - should fail
    auto result2 = session.send_stream_data(stream->stream_id, data, false);
    CHECK_FALSE(result2.ok());
}

TEST_CASE("webtransport_session rejects send on non-existent stream") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}}};
    auto result = session.send_stream_data(999, data, false);
    CHECK_FALSE(result.ok());
}

TEST_CASE("webtransport_session receives stream data") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    // Simulate receiving data
    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}};
    session.simulate_receive(0, data, false);

    auto event = session.receive_stream_data();
    REQUIRE(event.has_value());
    CHECK(event->stream_id == 0);
    CHECK_FALSE(event->fin);
}

TEST_CASE("webtransport_session receives stream data with FIN") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    // Simulate receiving data with FIN
    flowq::buffer data{std::vector<std::byte>{std::byte{0x01}}};
    session.simulate_receive(0, data, true);

    auto event = session.receive_stream_data();
    REQUIRE(event.has_value());
    CHECK(event->stream_id == 0);
    CHECK(event->fin);
}

TEST_CASE("webtransport_session returns nullopt when no stream data") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto event = session.receive_stream_data();
    CHECK_FALSE(event.has_value());
}

TEST_CASE("webtransport_session closes all streams on close") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto stream1 = session.open_bidi_stream();
    auto stream2 = session.open_bidi_stream();
    CHECK(session.stream_count() == 2);

    session.close();
    CHECK(session.stream_count() == 0);
}

TEST_CASE("webtransport_session multiple streams with different IDs") {
    flowq::quic::webtransport::session_config config{};
    config.authority = "example.com";
    config.path = "/webtransport";
    config.max_streams_bidi = 10;
    config.max_streams_uni = 10;

    flowq::quic::webtransport::webtransport_session session{config};
    session.connect();

    auto bidi1 = session.open_bidi_stream();
    auto bidi2 = session.open_bidi_stream();
    auto uni1 = session.open_uni_stream();
    auto uni2 = session.open_uni_stream();

    REQUIRE(bidi1.has_value());
    REQUIRE(bidi2.has_value());
    REQUIRE(uni1.has_value());
    REQUIRE(uni2.has_value());

    CHECK(bidi1->stream_id != bidi2->stream_id);
    CHECK(uni1->stream_id != uni2->stream_id);
    CHECK(bidi1->stream_id != uni1->stream_id);
}
