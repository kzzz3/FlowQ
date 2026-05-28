#include <flowq/quic/http3.hpp>
#include <flowq/quic/qpack.hpp>

#include <iostream>
#include <vector>

int main() {
    std::cout << "FlowQ HTTP/3 Example\n";
    std::cout << "====================\n\n";

    // Example 1: Encode HTTP/3 DATA frame
    std::cout << "1. Encoding HTTP/3 DATA frame:\n";
    flowq::buffer payload{std::vector<std::byte>{
        std::byte{0x48}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}  // "Hello"
    }};
    auto data_frame = flowq::quic::http3::encode_data_frame(payload);
    std::cout << "   Encoded DATA frame size: " << data_frame.size() << " bytes\n\n";

    // Example 2: Encode HTTP/3 SETTINGS frame
    std::cout << "2. Encoding HTTP/3 SETTINGS frame:\n";
    flowq::quic::http3::settings settings{};
    settings.max_field_section_size = 16384;
    settings.qpack_max_table_capacity = 0;
    settings.qpack_blocked_streams = 0;
    auto settings_frame = flowq::quic::http3::encode_settings_frame(settings);
    std::cout << "   Encoded SETTINGS frame size: " << settings_frame.size() << " bytes\n\n";

    // Example 3: QPACK header compression
    std::cout << "3. QPACK header compression:\n";
    flowq::quic::qpack::encoder qpack_encoder;
    std::vector<flowq::quic::qpack::header_field> headers = {
        {":method", "GET"},
        {":path", "/index.html"},
        {":authority", "example.com"},
        {":scheme", "https"}
    };
    auto encoded = qpack_encoder.encode(headers);
    if (encoded.ok()) {
        std::cout << "   Encoded " << headers.size() << " headers into " << encoded.data.size() << " bytes\n";
    }

    // Example 4: HTTP/3 GOAWAY frame
    std::cout << "\n4. Encoding HTTP/3 GOAWAY frame:\n";
    auto goaway_frame = flowq::quic::http3::encode_goaway_frame(42);
    std::cout << "   Encoded GOAWAY frame size: " << goaway_frame.size() << " bytes\n\n";

    // Example 5: HTTP/3 frame types
    std::cout << "5. HTTP/3 frame types:\n";
    std::cout << "   DATA: " << static_cast<std::uint64_t>(flowq::quic::http3::frame_type::data) << "\n";
    std::cout << "   HEADERS: " << static_cast<std::uint64_t>(flowq::quic::http3::frame_type::headers) << "\n";
    std::cout << "   SETTINGS: " << static_cast<std::uint64_t>(flowq::quic::http3::frame_type::settings) << "\n";
    std::cout << "   GOAWAY: " << static_cast<std::uint64_t>(flowq::quic::http3::frame_type::goaway) << "\n\n";

    std::cout << "HTTP/3 example completed successfully!\n";
    return 0;
}
