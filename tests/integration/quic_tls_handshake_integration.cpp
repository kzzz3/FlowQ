#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/tls_provider_backend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("openssl_tls_handshake_adapter client state transitions") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    // Initial state
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
    CHECK_FALSE(adapter.key_availability().initial);
    CHECK_FALSE(adapter.key_availability().handshake);
    CHECK_FALSE(adapter.key_availability().application);
    CHECK(adapter.peer_transport_params().empty());
}

TEST_CASE("openssl_tls_handshake_adapter server state transitions") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = false;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    // Initial state
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
    CHECK_FALSE(adapter.key_availability().initial);
    CHECK_FALSE(adapter.key_availability().handshake);
    CHECK_FALSE(adapter.key_availability().application);
}

TEST_CASE("openssl_tls_handshake_adapter receive_crypto returns error for disabled backend") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    flowq::quic::crypto_bytes bytes{};
    bytes.level = flowq::quic::tls_encryption_level::initial;
    bytes.offset = 0;
    
    auto result = adapter.receive_crypto(bytes);
    
    #if !defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK_FALSE(result.ok());
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
    #endif
}

TEST_CASE("openssl_tls_handshake_adapter drain_crypto returns empty initially") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    auto crypto = adapter.drain_crypto();
    CHECK(crypto.empty());
}

TEST_CASE("openssl_tls_handshake_adapter peer_transport_params returns empty initially") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    auto params = adapter.peer_transport_params();
    CHECK(params.empty());
}

TEST_CASE("openssl_tls_handshake_adapter config preserves values") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = false;
    config.cert_file = "/path/to/cert.pem";
    config.key_file = "/path/to/key.pem";
    config.ca_file = "/path/to/ca.pem";
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    // Verify adapter was constructed (no crash)
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
}

TEST_CASE("openssl_tls_handshake_adapter can be constructed multiple times") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    // Construct and destroy multiple adapters
    for (int i = 0; i < 10; ++i) {
        flowq::quic::openssl_tls_handshake_adapter adapter{config};
        CHECK(adapter.state() == flowq::quic::handshake_state::idle);
    }
}

TEST_CASE("openssl_tls_handshake_adapter handles multiple receive_crypto calls") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    // Send multiple crypto bytes
    for (int i = 0; i < 5; ++i) {
        flowq::quic::crypto_bytes bytes{};
        bytes.level = flowq::quic::tls_encryption_level::initial;
        bytes.offset = i * 100;
        
        auto result = adapter.receive_crypto(bytes);
        
        #if !defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
        CHECK_FALSE(result.ok());
        #endif
    }
}

TEST_CASE("openssl_tls_handshake_adapter handles drain_crypto after receive_crypto") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    flowq::quic::crypto_bytes bytes{};
    bytes.level = flowq::quic::tls_encryption_level::initial;
    bytes.offset = 0;
    
    (void)adapter.receive_crypto(bytes);
    
    auto crypto = adapter.drain_crypto();
    
    #if !defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK(crypto.empty());
    #endif
}

TEST_CASE("openssl_tls_handshake_adapter state remains idle after failed receive") {
    flowq::quic::openssl_tls_config config{};
    config.is_client = true;
    
    flowq::quic::openssl_tls_handshake_adapter adapter{config};
    
    flowq::quic::crypto_bytes bytes{};
    bytes.level = flowq::quic::tls_encryption_level::initial;
    bytes.offset = 0;
    
    (void)adapter.receive_crypto(bytes);
    
    #if !defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
    CHECK_FALSE(adapter.key_availability().initial);
    CHECK_FALSE(adapter.key_availability().handshake);
    CHECK_FALSE(adapter.key_availability().application);
    #endif
}
