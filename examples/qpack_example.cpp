#include <flowq/quic/qpack.hpp>

#include <iostream>
#include <vector>

int main() {
    std::cout << "FlowQ QPACK Example\n";
    std::cout << "====================\n\n";

    // Example 1: QPACK static table
    std::cout << "1. QPACK Static Table:\n";
    auto& table = flowq::quic::qpack::static_table();
    std::cout << "   Total entries: " << table.size() << "\n";
    std::cout << "   First 5 entries:\n";
    for (std::size_t i = 0; i < 5 && i < table.size(); ++i) {
        std::cout << "     [" << i << "] " << table[i].name << ": " << table[i].value << "\n";
    }
    std::cout << "\n";

    // Example 2: Encode HTTP headers
    std::cout << "2. Encoding HTTP headers with QPACK:\n";
    flowq::quic::qpack::encoder encoder;
    std::vector<flowq::quic::qpack::header_field> headers = {
        {":method", "GET"},
        {":path", "/index.html"},
        {":authority", "example.com"},
        {":scheme", "https"},
        {"accept", "text/html"},
        {"accept-encoding", "gzip, deflate"}
    };

    auto encoded = encoder.encode(headers);
    if (encoded.ok()) {
        std::cout << "   Encoded " << headers.size() << " headers\n";
        std::cout << "   Encoded size: " << encoded.data.size() << " bytes\n\n";
    }

    // Example 3: Decode headers
    std::cout << "3. Decoding headers:\n";
    flowq::quic::qpack::decoder decoder;
    auto decoded = decoder.decode(encoded.data.data(), encoded.data.size());
    if (decoded.ok()) {
        std::cout << "   Decoded " << decoded.headers.size() << " headers:\n";
        for (const auto& header : decoded.headers) {
            std::cout << "     " << header.name << ": " << header.value << "\n";
        }
    }
    std::cout << "\n";

    // Example 4: Dynamic table
    std::cout << "4. Dynamic Table:\n";
    flowq::quic::qpack::dynamic_table dyn_table(4096);
    dyn_table.add("custom-header", "custom-value");
    dyn_table.add("x-request-id", "abc123");

    std::cout << "   Table size: " << dyn_table.size() << " entries\n";
    std::cout << "   Capacity used: " << dyn_table.capacity() << " bytes\n";

    auto index = dyn_table.find("custom-header", "custom-value");
    if (index.has_value()) {
        std::cout << "   Found 'custom-header' at index " << *index << "\n";
    }
    std::cout << "\n";

    // Example 5: Mixed static and literal headers
    std::cout << "5. Mixed header encoding:\n";
    std::vector<flowq::quic::qpack::header_field> mixed_headers = {
        {":method", "POST"},           // Static table
        {":path", "/api/data"},        // Static name, literal value
        {"content-type", "application/json"},  // Static table
        {"x-custom-header", "custom-value"}    // Literal
    };

    auto mixed_encoded = encoder.encode(mixed_headers);
    if (mixed_encoded.ok()) {
        std::cout << "   Encoded " << mixed_headers.size() << " mixed headers\n";
        std::cout << "   Encoded size: " << mixed_encoded.data.size() << " bytes\n\n";
    }

    std::cout << "QPACK example completed successfully!\n";
    return 0;
}
