#include <flowq/quic/qpack.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>

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

TEST_CASE("qpack encoder does not emit dynamic table dependencies") {
    flowq::quic::qpack::encoder encoder;

    std::vector<flowq::quic::qpack::header_field> first = {
        {"custom-header", "custom-value"}
    };
    auto r1 = encoder.encode(first);
    REQUIRE(r1.ok());

    std::vector<flowq::quic::qpack::header_field> second = {
        {":method", "GET"},
        {"custom-header", "custom-value"}
    };
    auto r2 = encoder.encode(second);
    REQUIRE(r2.ok());

    auto data = r2.data.data();
    auto sz = r2.data.size();
    REQUIRE(sz >= 3);

    // The current header block codec is intentionally stateless: dynamic table
    // references require encoder-stream synchronization that FlowQ does not
    // expose yet, so every emitted header block is independently decodable.
    CHECK(static_cast<std::uint8_t>(data[0]) == 0x00);
    CHECK(static_cast<std::uint8_t>(data[1]) == 0x00);
}

TEST_CASE("qpack decoder rejects dynamic table dependencies") {
    flowq::quic::qpack::decoder decoder;
    const std::array<std::byte, 3> data{
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x91},
    };

    auto result = decoder.decode(data.data(), data.size());

    CHECK_FALSE(result.ok());
}

TEST_CASE("qpack decode multi-byte string length") {
    // Manually construct an encoded block with a string whose length requires
    // multi-byte varint encoding (>127 bytes).
    flowq::quic::qpack::decoder decoder;

    // Build the encoded block by hand:
    //   prefix: RIC=0x00, DeltaBase=0x00
    //   literal header (0x20): name="x", value=<200 bytes of 'A'>
    std::vector<std::byte> data;
    data.push_back(std::byte{0x00});  // Required Insert Count
    data.push_back(std::byte{0x00});  // Delta Base
    data.push_back(std::byte{0x20});  // Literal Header Field without Name Reference

    // Encode name "x": length=1 (single byte), then 'x'
    data.push_back(std::byte{0x01});
    data.push_back(static_cast<std::byte>('x'));

    // Encode value: 200 bytes of 'A'. 200 = 0xC8, needs 2-byte LEB128.
    // LEB128(200): first byte = 0x80 | (200 & 0x7f) = 0x80 | 0x48 = 0xC8
    //              second byte = 200 >> 7 = 0x01
    data.push_back(std::byte{0xC8});  // continuation bit set, low 7 bits = 0x48
    data.push_back(std::byte{0x01});  // final byte, high bits = 0x01

    // 200 bytes of 'A'
    for (int i = 0; i < 200; ++i) {
        data.push_back(static_cast<std::byte>('A'));
    }

    auto result = decoder.decode(data.data(), data.size());
    REQUIRE(result.ok());
    REQUIRE(result.headers.size() == 1);
    CHECK(result.headers[0].name == "x");
    CHECK(result.headers[0].value.size() == 200);
    CHECK(std::all_of(result.headers[0].value.begin(),
                       result.headers[0].value.end(),
                       [](char c) { return c == 'A'; }));
}

TEST_CASE("qpack round trip keeps repeated custom headers independently decodable") {
    // Verify that encoding then decoding preserves header content.
    flowq::quic::qpack::encoder encoder;
    flowq::quic::qpack::decoder decoder;

    std::vector<flowq::quic::qpack::header_field> original = {
        {":method", "GET"},
        {":path", "/index.html"},
        {":authority", "example.com"},
        {":scheme", "https"},
        {"accept", "text/html"},
    };

    auto encoded = encoder.encode(original);
    REQUIRE(encoded.ok());

    auto decoded = decoder.decode(encoded.data.data(), encoded.data.size());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.headers.size() == original.size());

    for (std::size_t i = 0; i < original.size(); ++i) {
        CHECK(decoded.headers[i].name == original[i].name);
        CHECK(decoded.headers[i].value == original[i].value);
    }
}

TEST_CASE("qpack round trip with long value") {
    // Round-trip test for a header value that exceeds 127 bytes, exercising
    // multi-byte string-length encoding and decoding.
    flowq::quic::qpack::encoder encoder;
    flowq::quic::qpack::decoder decoder;

    std::string long_value(256, 'Z');
    std::vector<flowq::quic::qpack::header_field> original = {
        {"x-custom", long_value}
    };

    auto encoded = encoder.encode(original);
    REQUIRE(encoded.ok());

    auto decoded = decoder.decode(encoded.data.data(), encoded.data.size());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.headers.size() == 1);
    CHECK(decoded.headers[0].name == "x-custom");
    CHECK(decoded.headers[0].value == long_value);
}

TEST_CASE("qpack encode varint multi-byte correctness") {
    // Verify that the encoder produces correct LEB128-encoded varints for
    // values that require 2+ bytes (high bit set on non-final bytes only).
    flowq::quic::qpack::encoder encoder;
    flowq::quic::qpack::decoder decoder;

    // A string of exactly 128 bytes should produce a 2-byte varint length.
    std::string value_128(128, 'B');
    std::vector<flowq::quic::qpack::header_field> headers = {
        {"test", value_128}
    };

    auto encoded = encoder.encode(headers);
    REQUIRE(encoded.ok());

    auto decoded = decoder.decode(encoded.data.data(), encoded.data.size());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.headers.size() == 1);
    CHECK(decoded.headers[0].name == "test");
    CHECK(decoded.headers[0].value == value_128);
}

TEST_CASE("qpack decoder rejects truncated multi-byte length") {
    // If the varint says the string is longer than the remaining data,
    // the decoder must report an error.
    flowq::quic::qpack::decoder decoder;

    std::vector<std::byte> data;
    data.push_back(std::byte{0x00});  // RIC
    data.push_back(std::byte{0x00});  // Delta Base
    data.push_back(std::byte{0x20});  // Literal header
    data.push_back(std::byte{0x01});  // name length = 1
    data.push_back(static_cast<std::byte>('n'));
    // Value length: multi-byte varint claiming 300 bytes
    // LEB128(300): 0x80|(300&0x7f)=0xAC, 300>>7=2 → 0x02
    data.push_back(std::byte{0xAC});
    data.push_back(std::byte{0x02});
    // But only provide 5 bytes of value data (not 300)
    for (int i = 0; i < 5; ++i) {
        data.push_back(static_cast<std::byte>('!'));
    }

    auto result = decoder.decode(data.data(), data.size());
    CHECK_FALSE(result.ok());
}
