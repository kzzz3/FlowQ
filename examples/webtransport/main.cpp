#include <flowq/quic/webtransport.hpp>

#include <iostream>
#include <vector>

int main() {
    std::cout << "FlowQ WebTransport Example\n";
    std::cout << "==========================\n\n";

    // Example 1: Create WebTransport session
    std::cout << "1. Creating WebTransport session:\n";
    auto session = flowq::quic::webtransport::session_builder{}
        .authority("example.com")
        .path("/webtransport")
        .max_streams_bidi(10)
        .max_streams_uni(10)
        .build();

    std::cout << "   Authority: " << session.config().authority << "\n";
    std::cout << "   Path: " << session.config().path << "\n";
    std::cout << "   Max bidi streams: " << session.config().max_streams_bidi << "\n";
    std::cout << "   Max uni streams: " << session.config().max_streams_uni << "\n\n";

    // Example 2: Connect session
    std::cout << "2. Connecting session:\n";
    auto connect_result = session.connect();
    if (connect_result.ok()) {
        std::cout << "   Session connected successfully!\n";
        std::cout << "   State: " << static_cast<int>(session.state()) << "\n\n";
    }

    // Example 3: Open bidirectional stream
    std::cout << "3. Opening bidirectional stream:\n";
    auto bidi_stream = session.open_bidi_stream();
    if (bidi_stream.has_value()) {
        std::cout << "   Stream ID: " << bidi_stream->stream_id << "\n";
        std::cout << "   Type: bidirectional\n\n";
    }

    // Example 4: Open unidirectional stream
    std::cout << "4. Opening unidirectional stream:\n";
    auto uni_stream = session.open_uni_stream();
    if (uni_stream.has_value()) {
        std::cout << "   Stream ID: " << uni_stream->stream_id << "\n";
        std::cout << "   Type: unidirectional\n\n";
    }

    // Example 5: Send stream data
    std::cout << "5. Sending stream data:\n";
    if (bidi_stream.has_value()) {
        flowq::buffer data{std::vector<std::byte>{
            std::byte{0x48}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}  // "Hello"
        }};
        auto send_result = session.send_stream_data(bidi_stream->stream_id, data, true);
        if (send_result.ok()) {
            std::cout << "   Sent " << data.size() << " bytes on stream " << bidi_stream->stream_id << "\n\n";
        }
    }

    // Example 6: Send datagram
    std::cout << "6. Sending datagram:\n";
    flowq::buffer datagram{std::vector<std::byte>{
        std::byte{0x57}, std::byte{0x6f}, std::byte{0x72}, std::byte{0x6c}, std::byte{0x64}  // "World"
    }};
    auto datagram_result = session.send_datagram(datagram);
    if (datagram_result.ok()) {
        std::cout << "   Sent " << datagram.size() << " bytes as datagram\n\n";
    }

    // Example 7: Close session
    std::cout << "7. Closing session:\n";
    auto close_result = session.close();
    if (close_result.ok()) {
        std::cout << "   Session closed successfully!\n";
        std::cout << "   Final state: " << static_cast<int>(session.state()) << "\n\n";
    }

    std::cout << "WebTransport example completed successfully!\n";
    return 0;
}
