// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace getbiblesword {

// Decode source markup without interpreting or removing any markup. Missing
// SWORD Encoding means Latin-1; unsupported or malformed input has no projection.
[[nodiscard]] std::optional<std::string> source_utf8(
    std::string_view raw, std::string_view encoding);

} // namespace getbiblesword
