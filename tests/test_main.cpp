// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/annotation.hpp"
#include "getbiblesword/base64.hpp"
#include "getbiblesword/byte_value.hpp"
#include "getbiblesword/ndjson_writer.hpp"
#include "getbiblesword/sha256.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_sha256() {
    expect(
        getbiblesword::sha256_hex("")
            == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA-256 empty vector");
    expect(
        getbiblesword::sha256_hex("abc")
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc vector");

    getbiblesword::Sha256 streaming;
    streaming.update("a");
    streaming.update("b");
    streaming.update("c");
    expect(
        streaming.hex_digest()
            == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 streaming vector");
}

void test_base64() {
    expect(getbiblesword::base64_encode("") == "", "base64 empty");
    expect(getbiblesword::base64_encode("f") == "Zg==", "base64 one byte");
    expect(getbiblesword::base64_encode("fo") == "Zm8=", "base64 two bytes");
    expect(getbiblesword::base64_encode("foo") == "Zm9v", "base64 three bytes");
    expect(getbiblesword::base64_encode("foobar") == "Zm9vYmFy", "base64 six bytes");
}

void test_utf8_and_json() {
    const std::array<unsigned char, 4> valid{0xf0U, 0x9fU, 0x98U, 0x80U};
    const std::array<unsigned char, 2> invalid{0xc0U, 0xafU};
    expect(getbiblesword::is_valid_utf8(valid), "valid UTF-8 scalar");
    expect(!getbiblesword::is_valid_utf8(invalid), "reject overlong UTF-8");
    expect(getbiblesword::json_string("a\n\"b") == "\"a\\n\\\"b\"", "JSON escaping");

    const auto binary = getbiblesword::byte_value_json(invalid);
    expect(binary.find("\"base64\":\"wK8=\"") != std::string::npos, "binary byte envelope");
    expect(binary.find("\"utf8\"") == std::string::npos, "invalid UTF-8 has no projection");
}

void test_annotation_round_trip() {
    const std::string raw = "word <w lemma=\"strong:G3056\">logos</w> &amp; <future x='>'/>";
    const auto segments = getbiblesword::segment_annotations(raw);
    std::string reconstructed;
    for (const auto& segment : segments) {
        reconstructed += segment.raw;
    }
    expect(reconstructed == raw, "annotation segmentation is byte-lossless");
    expect(segments.size() == 8U, "annotation segmentation count");
}

void test_ndjson_determinism() {
    const auto produce = [] {
        std::ostringstream output;
        getbiblesword::NdjsonWriter writer(output);
        writer.emit("header", {{"contract", "\"test/v1\""}});
        writer.emit("value", {{"z", "2"}, {"a", "1"}});
        writer.finish(true, {{"entries", "1"}});
        return output.str();
    };

    const auto first = produce();
    const auto second = produce();
    expect(first == second, "NDJSON output is deterministic");
    expect(first.starts_with("{\"contract\":\"test/v1\",\"sequence\":0,\"type\":\"header\"}"),
           "record members are canonicalized");
    expect(first.find("\"stream_sha256\"") != std::string::npos, "footer has stream digest");
}

} // namespace

int main() {
    test_sha256();
    test_base64();
    test_utf8_and_json();
    test_annotation_round_trip();
    test_ndjson_determinism();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All getBibleSword core tests passed\n";
    return EXIT_SUCCESS;
}
