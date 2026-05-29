#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/tls_provider_backend.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("openssl_tls_handshake_adapter constructs with default config") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
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
    CHECK(result.ok());
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
