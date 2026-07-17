// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/byte_value.hpp"

#include "getbiblesword/base64.hpp"
#include "getbiblesword/sha256.hpp"

#include <cstddef>
#include <iomanip>
#include <sstream>

namespace getbiblesword {

bool is_valid_utf8(const std::span<const unsigned char> bytes) noexcept {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto lead = bytes[index];
        if (lead <= 0x7fU) {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if ((lead & 0xe0U) == 0xc0U) {
            continuation_count = 1;
            code_point = lead & 0x1fU;
            minimum = 0x80U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            continuation_count = 2;
            code_point = lead & 0x0fU;
            minimum = 0x800U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            continuation_count = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }

        if (index + continuation_count >= bytes.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto next = bytes[index + offset];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (next & 0x3fU);
        }

        if (code_point < minimum || code_point > 0x10ffffU
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
        index += continuation_count + 1U;
    }
    return true;
}

std::string json_escape(const std::string_view utf8) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const char character : utf8) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (byte < 0x20U) {
                output << "\\u" << std::setw(4) << static_cast<unsigned int>(byte);
            } else {
                output << static_cast<char>(byte);
            }
            break;
        }
    }
    return output.str();
}

std::string json_string(const std::string_view utf8) {
    return '"' + json_escape(utf8) + '"';
}

std::string byte_value_json(const std::span<const unsigned char> bytes) {
    std::string output = "{\"base64\":" + json_string(base64_encode(bytes))
        + ",\"encoding\":\"base64\",\"sha256\":" + json_string(sha256_hex(bytes))
        + ",\"size\":" + std::to_string(bytes.size());
    if (is_valid_utf8(bytes)) {
        const auto* first = reinterpret_cast<const char*>(bytes.data());
        output += ",\"utf8\":" + json_string(std::string_view(first, bytes.size()));
    }
    output += '}';
    return output;
}

std::string byte_value_json(const std::string_view bytes) {
    const auto* first = reinterpret_cast<const unsigned char*>(bytes.data());
    return byte_value_json(std::span<const unsigned char>(first, bytes.size()));
}

} // namespace getbiblesword
