// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/base64.hpp"

#include <cstddef>
#include <span>

namespace getbiblesword {
namespace {

constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::string base64_encode(const std::span<const unsigned char> input) {
    std::string output;
    output.reserve(((input.size() + 2U) / 3U) * 4U);

    std::size_t index = 0;
    while (index + 3U <= input.size()) {
        const auto value = (static_cast<unsigned int>(input[index]) << 16U)
            | (static_cast<unsigned int>(input[index + 1U]) << 8U)
            | static_cast<unsigned int>(input[index + 2U]);
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
        output.push_back(alphabet[(value >> 6U) & 0x3fU]);
        output.push_back(alphabet[value & 0x3fU]);
        index += 3U;
    }

    const auto remaining = input.size() - index;
    if (remaining == 1U) {
        const auto value = static_cast<unsigned int>(input[index]) << 16U;
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
        output.append("==");
    } else if (remaining == 2U) {
        const auto value = (static_cast<unsigned int>(input[index]) << 16U)
            | (static_cast<unsigned int>(input[index + 1U]) << 8U);
        output.push_back(alphabet[(value >> 18U) & 0x3fU]);
        output.push_back(alphabet[(value >> 12U) & 0x3fU]);
        output.push_back(alphabet[(value >> 6U) & 0x3fU]);
        output.push_back('=');
    }

    return output;
}

std::string base64_encode(const std::string_view input) {
    const auto* first = reinterpret_cast<const unsigned char*>(input.data());
    return base64_encode(std::span<const unsigned char>(first, input.size()));
}

} // namespace getbiblesword
