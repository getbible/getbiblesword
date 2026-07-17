// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace getbiblesword {

[[nodiscard]] std::string base64_encode(std::span<const unsigned char> input);
[[nodiscard]] std::string base64_encode(std::string_view input);

} // namespace getbiblesword
