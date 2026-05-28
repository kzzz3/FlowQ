#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/http3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace flowq::quic::webtransport {

/// WebTransport session state.
enum class session_state {
    connecting,
    connected,
    closed
};

/// WebTransport stream type.
enum class stream_type {
    bidirectional,
    unidirectional
};

/// WebTransport session error.
[[nodiscard]] inline flowq::error webtransport_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

/// WebTransport session configuration.
struct session_config {
    std::string authority;
    std::string path;
    std::uint64_t max_streams_bidi{};
    std::uint64_t max_streams_uni{};
};

/// WebTransport stream.
struct webtransport_stream {
    std::uint64_t stream_id{};
    stream_type type{};
    flowq::buffer data;
    bool fin{};
};

/// WebTransport datagram.
struct webtransport_datagram {
    flowq::buffer data;
};

/// WebTransport session result.
struct session_result {
    session_state state{session_state::connecting};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }

    [[nodiscard]] bool is_connected() const noexcept {
        return state == session_state::connected && error.ok();
    }
};

/// WebTransport session manager.
/// Manages WebTransport sessions over HTTP/3.
class webtransport_session {
public:
    explicit webtransport_session(session_config config)
        : config_{std::move(config)} {}

    /// Initiate a WebTransport session.
    [[nodiscard]] session_result connect() {
        if (state_ != session_state::connecting) {
            return {state_, webtransport_error("Session already connected or closed")};
        }

        // In a real implementation, this would:
        // 1. Send HTTP/3 CONNECT request with :protocol=webtransport
        // 2. Wait for 200 response
        // 3. Establish WebTransport session

        // For now, simulate successful connection
        state_ = session_state::connected;
        return {state_, {}};
    }

    /// Close the WebTransport session.
    [[nodiscard]] session_result close(std::uint64_t error_code = 0) {
        if (state_ != session_state::connected) {
            return {state_, webtransport_error("Session not connected")};
        }

        // In a real implementation, this would:
        // 1. Send GOAWAY frame
        // 2. Close all streams
        // 3. Clean up resources

        state_ = session_state::closed;
        return {state_, {}};
    }

    /// Open a bidirectional stream.
    [[nodiscard]] std::optional<webtransport_stream> open_bidi_stream() {
        if (state_ != session_state::connected) {
            return std::nullopt;
        }

        if (next_bidi_stream_id_ >= config_.max_streams_bidi * 4) {
            return std::nullopt;
        }

        webtransport_stream stream{};
        stream.stream_id = next_bidi_stream_id_;
        stream.type = stream_type::bidirectional;
        next_bidi_stream_id_ += 4;

        return stream;
    }

    /// Open a unidirectional stream.
    [[nodiscard]] std::optional<webtransport_stream> open_uni_stream() {
        if (state_ != session_state::connected) {
            return std::nullopt;
        }

        if (next_uni_stream_id_ >= config_.max_streams_uni * 4) {
            return std::nullopt;
        }

        webtransport_stream stream{};
        stream.stream_id = next_uni_stream_id_;
        stream.type = stream_type::unidirectional;
        next_uni_stream_id_ += 4;

        return stream;
    }

    /// Send data on a stream.
    [[nodiscard]] flowq::error send_stream_data(std::uint64_t stream_id, const flowq::buffer& data, bool fin = false) {
        if (state_ != session_state::connected) {
            return webtransport_error("Session not connected");
        }

        // In a real implementation, this would:
        // 1. Encode data as STREAM frame
        // 2. Send over HTTP/3 stream

        return {};
    }

    /// Send a datagram.
    [[nodiscard]] flowq::error send_datagram(const flowq::buffer& data) {
        if (state_ != session_state::connected) {
            return webtransport_error("Session not connected");
        }

        // In a real implementation, this would:
        // 1. Encode datagram
        // 2. Send over HTTP/3 DATAGRAM frame

        return {};
    }

    /// Receive a datagram.
    [[nodiscard]] std::optional<webtransport_datagram> receive_datagram() {
        if (state_ != session_state::connected) {
            return std::nullopt;
        }

        // In a real implementation, this would:
        // 1. Receive DATAGRAM frame from HTTP/3
        // 2. Decode datagram data

        return std::nullopt;
    }

    /// Get current session state.
    [[nodiscard]] session_state state() const noexcept {
        return state_;
    }

    /// Get session configuration.
    [[nodiscard]] const session_config& config() const noexcept {
        return config_;
    }

private:
    session_config config_;
    session_state state_{session_state::connecting};
    std::uint64_t next_bidi_stream_id_{};
    std::uint64_t next_uni_stream_id_{};
};

/// WebTransport session builder.
class session_builder {
public:
    session_builder& authority(const std::string& authority) {
        config_.authority = authority;
        return *this;
    }

    session_builder& path(const std::string& path) {
        config_.path = path;
        return *this;
    }

    session_builder& max_streams_bidi(std::uint64_t max) {
        config_.max_streams_bidi = max;
        return *this;
    }

    session_builder& max_streams_uni(std::uint64_t max) {
        config_.max_streams_uni = max;
        return *this;
    }

    [[nodiscard]] webtransport_session build() {
        return webtransport_session{config_};
    }

private:
    session_config config_;
};

} // namespace flowq::quic::webtransport
