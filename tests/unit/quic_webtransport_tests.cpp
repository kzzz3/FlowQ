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
    CHECK(stream->stream_id == 0);
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
