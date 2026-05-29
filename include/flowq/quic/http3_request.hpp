#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/http3.hpp>
#include <flowq/quic/qpack.hpp>

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace flowq::quic::http3 {

/// HTTP/3 request.
struct request {
    std::string method;
    std::string path;
    std::string authority;
    std::string scheme;
    std::unordered_map<std::string, std::string> headers;
    flowq::buffer body;
};

/// HTTP/3 response.
struct response {
    std::uint64_t status_code{};
    std::unordered_map<std::string, std::string> headers;
    flowq::buffer body;
};

/// HTTP/3 request builder.
class request_builder {
public:
    request_builder& method(const std::string& method) {
        request_.method = method;
        return *this;
    }

    request_builder& path(const std::string& path) {
        request_.path = path;
        return *this;
    }

    request_builder& authority(const std::string& authority) {
        request_.authority = authority;
        return *this;
    }

    request_builder& scheme(const std::string& scheme) {
        request_.scheme = scheme;
        return *this;
    }

    request_builder& header(const std::string& name, const std::string& value) {
        request_.headers[name] = value;
        return *this;
    }

    request_builder& body(const flowq::buffer& body) {
        request_.body = body;
        return *this;
    }

    [[nodiscard]] request build() const {
        return request_;
    }

private:
    request request_;
};

/// HTTP/3 response builder.
class response_builder {
public:
    response_builder& status(std::uint64_t status_code) {
        response_.status_code = status_code;
        return *this;
    }

    response_builder& header(const std::string& name, const std::string& value) {
        response_.headers[name] = value;
        return *this;
    }

    response_builder& body(const flowq::buffer& body) {
        response_.body = body;
        return *this;
    }

    [[nodiscard]] response build() const {
        return response_;
    }

private:
    response response_;
};

/// HTTP/3 request encoder.
/// Encodes HTTP/3 requests into frames.
class request_encoder {
public:
    /// Encode a request into HEADERS + DATA frames.
    [[nodiscard]] qpack::encode_result encode(const request& req) {
        std::vector<qpack::header_field> headers;
        
        // Pseudo-headers
        headers.push_back({":method", req.method});
        headers.push_back({":path", req.path});
        headers.push_back({":authority", req.authority});
        headers.push_back({":scheme", req.scheme});
        
        // Regular headers
        for (const auto& [name, value] : req.headers) {
            headers.push_back({name, value});
        }
        
        // Encode headers with QPACK
        qpack::encoder qpack_encoder;
        auto encoded_headers = qpack_encoder.encode(headers);
        if (!encoded_headers.ok()) {
            return {{}, encoded_headers.error};
        }
        
        // Build HEADERS frame
        auto headers_frame = encode_headers_frame(encoded_headers.data);
        
        // Build DATA frame if body is present
        if (!req.body.empty()) {
            auto data_frame = encode_data_frame(req.body);
            if (!data_frame.ok()) {
                return {{}, data_frame.error};
            }
            
            // Combine frames
            std::vector<std::byte> combined;
            combined.insert(combined.end(), headers_frame.data(), headers_frame.data() + headers_frame.size());
            combined.insert(combined.end(), data_frame.payload.data(), data_frame.payload.data() + data_frame.payload.size());
            
            return {flowq::buffer{combined}, {}};
        }
        
        return {headers_frame, {}};
    }

private:
    [[nodiscard]] static flowq::buffer encode_headers_frame(const flowq::buffer& headers) {
        std::vector<std::byte> output;
        
        // Frame type (0x01 = HEADERS)
        append_varint(output, 0x01);
        
        // Frame length
        append_varint(output, headers.size());
        
        // Headers payload
        output.insert(output.end(), headers.data(), headers.data() + headers.size());
        
        return flowq::buffer{output};
    }

    static void append_varint(std::vector<std::byte>& output, std::uint64_t value) {
        std::array<std::byte, 8> encoded{};
        const auto result = encode_varint(value, encoded);
        if (result.ok()) {
            output.insert(output.end(), encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(result.bytes_written));
        }
    }
};

/// HTTP/3 response encoder.
/// Encodes HTTP/3 responses into frames.
class response_encoder {
public:
    /// Encode a response into HEADERS + DATA frames.
    [[nodiscard]] qpack::encode_result encode(const response& resp) {
        std::vector<qpack::header_field> headers;
        
        // Pseudo-header
        headers.push_back({":status", std::to_string(resp.status_code)});
        
        // Regular headers
        for (const auto& [name, value] : resp.headers) {
            headers.push_back({name, value});
        }
        
        // Encode headers with QPACK
        qpack::encoder qpack_encoder;
        auto encoded_headers = qpack_encoder.encode(headers);
        if (!encoded_headers.ok()) {
            return {{}, encoded_headers.error};
        }
        
        // Build HEADERS frame
        auto headers_frame = encode_headers_frame(encoded_headers.data);
        
        // Build DATA frame if body is present
        if (!resp.body.empty()) {
            auto data_frame = encode_data_frame(resp.body);
            if (!data_frame.ok()) {
                return {{}, data_frame.error};
            }
            
            // Combine frames
            std::vector<std::byte> combined;
            combined.insert(combined.end(), headers_frame.data(), headers_frame.data() + headers_frame.size());
            combined.insert(combined.end(), data_frame.payload.data(), data_frame.payload.data() + data_frame.payload.size());
            
            return {flowq::buffer{combined}, {}};
        }
        
        return {headers_frame, {}};
    }

private:
    [[nodiscard]] static flowq::buffer encode_headers_frame(const flowq::buffer& headers) {
        std::vector<std::byte> output;
        
        // Frame type (0x01 = HEADERS)
        append_varint(output, 0x01);
        
        // Frame length
        append_varint(output, headers.size());
        
        // Headers payload
        output.insert(output.end(), headers.data(), headers.data() + headers.size());
        
        return flowq::buffer{output};
    }

    static void append_varint(std::vector<std::byte>& output, std::uint64_t value) {
        std::array<std::byte, 8> encoded{};
        const auto result = encode_varint(value, encoded);
        if (result.ok()) {
            output.insert(output.end(), encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(result.bytes_written));
        }
    }
};

/// HTTP/3 request decoder.
/// Decodes HTTP/3 frames into requests.
class request_decoder {
public:
    /// Decode frames into a request.
    [[nodiscard]] std::optional<request> decode(const std::vector<http3_frame_variant>& frames) {
        request req;
        
        for (const auto& frame : frames) {
            if (std::holds_alternative<headers_frame>(frame)) {
                const auto& headers = std::get<headers_frame>(frame);
                if (headers.headers.fields.empty()) {
                    return std::nullopt;
                }
                // Decode QPACK headers
                qpack::decoder qpack_decoder;
                auto decoded = qpack_decoder.decode(
                    headers.headers.fields[0].name.data(),
                    headers.headers.fields[0].name.size()
                );
                if (decoded.ok()) {
                    for (const auto& field : decoded.headers) {
                        if (field.name == ":method") {
                            req.method = field.value;
                        } else if (field.name == ":path") {
                            req.path = field.value;
                        } else if (field.name == ":authority") {
                            req.authority = field.value;
                        } else if (field.name == ":scheme") {
                            req.scheme = field.value;
                        } else {
                            req.headers[field.name] = field.value;
                        }
                    }
                }
            } else if (std::holds_alternative<data_frame>(frame)) {
                const auto& data = std::get<data_frame>(frame);
                req.body = data.data;
            }
        }
        
        if (req.method.empty() || req.path.empty()) {
            return std::nullopt;
        }
        
        return req;
    }
};

/// HTTP/3 response decoder.
/// Decodes HTTP/3 frames into responses.
class response_decoder {
public:
    /// Decode frames into a response.
    [[nodiscard]] std::optional<response> decode(const std::vector<http3_frame_variant>& frames) {
        response resp;
        
        for (const auto& frame : frames) {
            if (std::holds_alternative<headers_frame>(frame)) {
                const auto& headers = std::get<headers_frame>(frame);
                if (headers.headers.fields.empty()) {
                    return std::nullopt;
                }
                // Decode QPACK headers
                qpack::decoder qpack_decoder;
                auto decoded = qpack_decoder.decode(
                    headers.headers.fields[0].name.data(),
                    headers.headers.fields[0].name.size()
                );
                if (decoded.ok()) {
                    for (const auto& field : decoded.headers) {
                        if (field.name == ":status") {
                            resp.status_code = std::stoull(field.value);
                        } else {
                            resp.headers[field.name] = field.value;
                        }
                    }
                }
            } else if (std::holds_alternative<data_frame>(frame)) {
                const auto& data = std::get<data_frame>(frame);
                resp.body = data.data;
            }
        }
        
        if (resp.status_code == 0) {
            return std::nullopt;
        }
        
        return resp;
    }
};

} // namespace flowq::quic::http3
