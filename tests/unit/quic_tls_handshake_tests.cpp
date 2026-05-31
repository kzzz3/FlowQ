#include <flowq/quic/tls_handshake.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

flowq::buffer text(std::string value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return flowq::buffer{output};
}

std::string as_string(const flowq::buffer& buffer) {
    std::string output;
    output.reserve(buffer.size());
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        output.push_back(static_cast<char>(buffer.data()[index]));
    }
    return output;
}

class recording_tls_adapter final : public flowq::quic::tls_handshake_adapter {
public:
    flowq::quic::handshake_state state() const noexcept override {
        return state_value;
    }

    flowq::quic::tls_key_availability key_availability() const noexcept override {
        return keys;
    }

    flowq::quic::crypto_provider_status provider_status() const noexcept override {
        return status;
    }

    flowq::error receive_crypto(flowq::quic::crypto_bytes bytes) override {
        received.push_back(std::move(bytes));
        return {};
    }

    std::vector<flowq::quic::crypto_bytes> drain_crypto() override {
        auto output = std::move(outbound);
        outbound.clear();
        return output;
    }

    flowq::error advance() override {
        return {};
    }

    flowq::quic::handshake_state state_value{flowq::quic::handshake_state::idle};
    flowq::quic::tls_key_availability keys{};
    flowq::quic::crypto_provider_status status{flowq::quic::crypto_provider_status::unavailable()};
    std::vector<flowq::quic::crypto_bytes> received;
    std::vector<flowq::quic::crypto_bytes> outbound;
};

} // namespace

TEST_CASE("TLS handshake adapter exposes state and key availability") {
    recording_tls_adapter adapter{};

    CHECK(adapter.state() == flowq::quic::handshake_state::idle);
    CHECK_FALSE(adapter.key_availability().application);

    adapter.state_value = flowq::quic::handshake_state::handshaking;
    CHECK(adapter.state() == flowq::quic::handshake_state::handshaking);

    adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    adapter.keys.application = true;
    CHECK(adapter.state() == flowq::quic::handshake_state::handshake_confirmed);
    CHECK(adapter.key_availability().application);

    adapter.state_value = flowq::quic::handshake_state::failed;
    CHECK(adapter.state() == flowq::quic::handshake_state::failed);
}

TEST_CASE("TLS handshake adapter moves CRYPTO bytes by encryption level") {
    recording_tls_adapter adapter{};

    REQUIRE(adapter.receive_crypto(flowq::quic::crypto_bytes{flowq::quic::tls_encryption_level::initial, 7, text("hello")}).ok());
    adapter.outbound.push_back(flowq::quic::crypto_bytes{flowq::quic::tls_encryption_level::handshake, 3, text("world")});

    REQUIRE(adapter.received.size() == 1);
    CHECK(adapter.received[0].level == flowq::quic::tls_encryption_level::initial);
    CHECK(adapter.received[0].offset == 7);
    CHECK(as_string(adapter.received[0].data) == "hello");

    auto outbound = adapter.drain_crypto();
    REQUIRE(outbound.size() == 1);
    CHECK(outbound[0].level == flowq::quic::tls_encryption_level::handshake);
    CHECK(outbound[0].offset == 3);
    CHECK(as_string(outbound[0].data) == "world");
    CHECK(adapter.drain_crypto().empty());
}

TEST_CASE("TLS application data readiness requires TLS-owned key schedule evidence") {
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    adapter.keys.application = true;

    CHECK_FALSE(flowq::quic::application_data_ready(adapter));
}

TEST_CASE("TLS application data readiness accepts confirmed TLS-owned key schedule evidence") {
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    adapter.keys.application = true;
    adapter.status = flowq::quic::crypto_provider_status::available(
        flowq::quic::cipher_suite::aes_128_gcm_sha256,
        flowq::quic::crypto_capabilities{false, false, false, false, true});

    CHECK(flowq::quic::application_data_ready(adapter));
}
