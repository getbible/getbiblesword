// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace getbiblesword {

[[nodiscard]] bool is_valid_utf8(std::span<const unsigned char> bytes) noexcept;
[[nodiscard]] std::string json_escape(std::string_view utf8);
[[nodiscard]] std::string json_string(std::string_view utf8);
[[nodiscard]] std::string byte_value_json(std::span<const unsigned char> bytes);
[[nodiscard]] std::string byte_value_json(std::string_view bytes);

} // namespace getbiblesword
