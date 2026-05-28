#include <flowq/quic/qpack.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("qpack static table has expected size") {
    auto& table = flowq::quic::qpack::static_table();
    CHECK(table.size() == 70);  // RFC 9204 static table size with common headers
}

TEST_CASE("qpack static table contains common headers") {
    auto& table = flowq::quic::qpack::static_table();
    
    // Check first entry
    CHECK(table[0].name == ":authority");
    CHECK(table[0].value == "");
    
    // Check :method GET (index 17)
    CHECK(table[17].name == ":method");
    CHECK(table[17].value == "GET");
    
    // Check :status 200 (find by name)
    bool found_status_200 = false;
    for (const auto& entry : table) {
        if (entry.name == ":status" && entry.value == "200") {
            found_status_200 = true;
            break;
        }
    }
    CHECK(found_status_200);
}

TEST_CASE("qpack encoder encodes static indexed header") {
    flowq::quic::qpack::encoder encoder;
    
    std::vector<flowq::quic::qpack::header_field> headers = {
        {":method", "GET"},
        {":path", "/"}
    };
    
    auto result = encoder.encode(headers);
    REQUIRE(result.ok());
    CHECK_FALSE(result.data.empty());
}

TEST_CASE("qpack encoder encodes literal header") {
    flowq::quic::qpack::encoder encoder;
    
    std::vector<flowq::quic::qpack::header_field> headers = {
        {"custom-header", "custom-value"}
    };
    
    auto result = encoder.encode(headers);
    REQUIRE(result.ok());
    CHECK_FALSE(result.data.empty());
}

TEST_CASE("qpack decoder decodes static indexed header") {
    flowq::quic::qpack::decoder decoder;
    
    // Manually encode :method GET as static indexed (index 17)
    std::vector<std::byte> data = {
        std::byte{0x00},  // Required Insert Count
        std::byte{0x00},  // Delta Base
        std::byte{0x91}   // Static indexed, index 17 (0x80 | 17)
    };
    
    auto result = decoder.decode(data.data(), data.size());
    REQUIRE(result.ok());
    REQUIRE(result.headers.size() == 1);
    CHECK(result.headers[0].name == ":method");
    CHECK(result.headers[0].value == "GET");
}

TEST_CASE("qpack round trip") {
    flowq::quic::qpack::encoder encoder;
    flowq::quic::qpack::decoder decoder;
    
    std::vector<flowq::quic::qpack::header_field> original = {
        {":method", "GET"},
        {":path", "/index.html"},
        {":authority", "example.com"},
        {":scheme", "https"}
    };
    
    auto encoded = encoder.encode(original);
    REQUIRE(encoded.ok());
    
    auto decoded = decoder.decode(encoded.data.data(), encoded.data.size());
    REQUIRE(decoded.ok());
    CHECK(decoded.headers.size() == original.size());
}

TEST_CASE("qpack dynamic table add and find") {
    flowq::quic::qpack::dynamic_table table(4096);
    
    table.add("custom-header", "custom-value");
    CHECK(table.size() == 1);
    
    auto index = table.find("custom-header", "custom-value");
    REQUIRE(index.has_value());
    CHECK(*index == 0);
}

TEST_CASE("qpack dynamic table evicts when full") {
    flowq::quic::qpack::dynamic_table table(100);
    
    table.add("header1", "value1");
    table.add("header2", "value2");
    CHECK(table.size() == 2);
    
    // Add a large entry that should evict older ones
    table.add("large-header", std::string(100, 'x'));
    CHECK(table.size() <= 2);
}
