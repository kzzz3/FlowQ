#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/tls_provider_backend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

TEST_CASE("openssl_tls_handshake_adapter constructs with default config") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
    CHECK_FALSE(adapter.last_error().ok());
    CHECK(adapter.last_error().message().find("client TLS requires CA certificate") != std::string::npos);
#else
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
#endif
    CHECK_FALSE(adapter.key_availability().initial);
    CHECK_FALSE(adapter.key_availability().handshake);
    CHECK_FALSE(adapter.key_availability().application);
}

TEST_CASE("openssl_tls_handshake_adapter receive_crypto follows build-time backend mode") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    // Try to receive crypto - should fail since no TLS context is set up
    flowq::quic::crypto_bytes bytes{};
    bytes.level = flowq::quic::tls_encryption_level::initial;
    bytes.offset = 0;
    
    auto result = adapter.receive_crypto(bytes);

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK_FALSE(result.ok());
    CHECK(result.message().find("client TLS requires CA certificate") != std::string::npos);
    CHECK_FALSE(adapter.provider_status().key_schedule_ready());
#else
    CHECK_FALSE(result.ok());
#endif
}

TEST_CASE("openssl_tls_handshake_adapter drain_crypto returns empty") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    auto crypto = adapter.drain_crypto();
    CHECK(crypto.empty());
}

TEST_CASE("openssl_tls_handshake_adapter peer_transport_params returns empty") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    auto params = adapter.peer_transport_params();
    CHECK(params.empty());
}

TEST_CASE("tls_provider_backend_status reports correct state") {
    auto status = flowq::quic::openssl_quic_tls_backend_status();
    
    // The backend should be available if OpenSSL is enabled
    #if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK(status.available);
    CHECK(status.metadata.family == flowq::quic::tls_provider_family::openssl_quic_tls);
    CHECK(status.metadata.quic_tls_api_available);
    CHECK(status.error.ok());
    #else
    CHECK_FALSE(status.available);
    CHECK(status.metadata.family == flowq::quic::tls_provider_family::openssl_quic_tls);
    CHECK_FALSE(status.metadata.quic_tls_api_available);
    CHECK_FALSE(status.error.ok());
    #endif
}

TEST_CASE("disabled_tls_provider_backend_status returns disabled state") {
    auto status = flowq::quic::disabled_tls_provider_backend_status();
    
    CHECK_FALSE(status.available);
    CHECK(status.metadata.family == flowq::quic::tls_provider_family::unavailable);
    CHECK_FALSE(status.metadata.quic_tls_api_available);
    CHECK_FALSE(status.error.ok());
}

TEST_CASE("openssl_tls_config default values") {
    flowq::quic::openssl_tls_config config{};
    
    CHECK(config.is_client);
    CHECK(config.cert_file == nullptr);
    CHECK(config.key_file == nullptr);
    CHECK(config.ca_file == nullptr);
    CHECK(config.server_name == nullptr);
    REQUIRE(config.alpn != nullptr);
    CHECK(std::string_view{config.alpn} == std::string_view{"hq-interop"});
    CHECK(config.local_transport_parameters.max_udp_payload_size == 1200);
    CHECK(config.local_transport_parameters.initial_max_data == 1048576);
    CHECK(config.local_transport_parameters.initial_max_stream_data_bidi_local == 262144);
    CHECK(config.local_transport_parameters.initial_max_stream_data_bidi_remote == 262144);
    CHECK(config.local_transport_parameters.initial_max_stream_data_uni == 262144);
    CHECK(config.local_transport_parameters.initial_max_streams_bidi == 128);
    CHECK(config.local_transport_parameters.initial_max_streams_uni == 128);
    CHECK(config.local_transport_parameters.active_connection_id_limit == 2);
}

TEST_CASE("openssl_tls_config can be set to server mode") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = false;
    config.cert_file = "/path/to/cert.pem";
    config.key_file = "/path/to/key.pem";
    
    CHECK_FALSE(config.is_client);
    CHECK(config.cert_file != nullptr);
    CHECK(config.key_file != nullptr);
}

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
TEST_CASE("openssl_tls_handshake_adapter rejects server config without certificate and key") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = false;

    flowq::quic::openssl_tls_handshake_adapter adapter{config};

    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
    CHECK_FALSE(adapter.last_error().ok());
    CHECK(adapter.last_error().message().find("server TLS requires certificate chain and private key") != std::string::npos);
}

TEST_CASE("openssl_tls_handshake_adapter rejects unreadable server certificate") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = false;
    config.cert_file = "missing-flowq-server-cert.pem";
    config.key_file = "missing-flowq-server-key.pem";

    flowq::quic::openssl_tls_handshake_adapter adapter{config};

    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
    CHECK_FALSE(adapter.last_error().ok());
    CHECK(adapter.last_error().message().find("failed to load server certificate chain") != std::string::npos);
}

TEST_CASE("openssl_tls_handshake_adapter rejects client config without CA certificate") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    config.server_name = "localhost";

    flowq::quic::openssl_tls_handshake_adapter adapter{config};

    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
    CHECK_FALSE(adapter.last_error().ok());
    CHECK(adapter.last_error().message().find("client TLS requires CA certificate") != std::string::npos);
}

TEST_CASE("openssl_tls_handshake_adapter rejects client config without verification host") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    config.ca_file = "missing-flowq-ca.pem";

    flowq::quic::openssl_tls_handshake_adapter adapter{config};

    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
    CHECK_FALSE(adapter.last_error().ok());
    CHECK(adapter.last_error().message().find("client TLS requires server name") != std::string::npos);
}

TEST_CASE("openssl_tls_handshake_adapter rejects unreadable client CA certificate") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    config.ca_file = "missing-flowq-ca.pem";
    config.server_name = "localhost";

    flowq::quic::openssl_tls_handshake_adapter adapter{config};

    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
    CHECK_FALSE(adapter.last_error().ok());
    CHECK(adapter.last_error().message().find("failed to load client CA certificate") != std::string::npos);
}
#endif

TEST_CASE("openssl_tls_config defaults to FlowQ-supported TLS 1.3 cipher suite") {
    flowq::quic::openssl_tls_config config{};

    REQUIRE(config.tls13_ciphersuite != nullptr);
    CHECK(std::string_view{config.tls13_ciphersuite} == std::string_view{"TLS_AES_128_GCM_SHA256"});
}

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
TEST_CASE("openssl_tls_handshake_adapter maps OpenSSL QUIC protection levels") {
    CHECK(flowq::quic::openssl_tls_handshake_adapter::tls_level_for_openssl_protection_level(
        OSSL_RECORD_PROTECTION_LEVEL_NONE) == flowq::quic::tls_encryption_level::initial);
    CHECK(flowq::quic::openssl_tls_handshake_adapter::tls_level_for_openssl_protection_level(
        OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE) == flowq::quic::tls_encryption_level::handshake);
    CHECK(flowq::quic::openssl_tls_handshake_adapter::tls_level_for_openssl_protection_level(
        OSSL_RECORD_PROTECTION_LEVEL_APPLICATION) == flowq::quic::tls_encryption_level::application);
    CHECK_FALSE(flowq::quic::openssl_tls_handshake_adapter::tls_level_for_openssl_protection_level(
        OSSL_RECORD_PROTECTION_LEVEL_EARLY).has_value());
}
#endif
