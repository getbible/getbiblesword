// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/source_encoding.hpp"

#include "getbiblesword/byte_value.hpp"

#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>
#include <unicode/ustring.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace getbiblesword {

std::optional<std::string> source_utf8(
    const std::string_view raw, const std::string_view encoding) {
    std::string name(encoding);
    std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z'
            ? character + ('a' - 'A') : character);
    });

    const char* converter_name = nullptr;
    if (name == "utf-8") {
        const auto* bytes = reinterpret_cast<const unsigned char*>(raw.data());
        if (!is_valid_utf8(std::span<const unsigned char>(bytes, raw.size()))) {
            return std::nullopt;
        }
        return std::string(raw);
    }
    if (name.empty() || name == "latin-1") {
        converter_name = "ISO-8859-1";
    } else if (name == "scsu") {
        converter_name = "SCSU";
    } else if (name == "utf-16") {
        // A BOM selects either byte order. Without one SWORD's Linux module
        // convention is little-endian; do not depend on the host byte order.
        const bool bom = raw.size() >= 2U
            && (raw.substr(0, 2) == std::string_view("\xff\xfe", 2)
                || raw.substr(0, 2) == std::string_view("\xfe\xff", 2));
        converter_name = bom ? "UTF-16" : "UTF-16LE";
    } else {
        return std::nullopt;
    }
    if (raw.empty()) {
        return std::string{};
    }
    if (raw.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }

    UErrorCode status = U_ZERO_ERROR;
    const std::unique_ptr<UConverter, decltype(&ucnv_close)> converter(
        ucnv_open(converter_name, &status), &ucnv_close);
    if (U_FAILURE(status)) {
        return std::nullopt;
    }
    // ICU's default callback substitutes U+FFFD. A lossless extractor must
    // report undecodable source instead of manufacturing replacement text.
    ucnv_setToUCallBack(converter.get(), UCNV_TO_U_CALLBACK_STOP,
        nullptr, nullptr, nullptr, &status);
    const auto input_size = static_cast<std::int32_t>(raw.size());
    auto length = ucnv_toUChars(converter.get(), nullptr, 0, raw.data(), input_size, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        return std::nullopt;
    }
    status = U_ZERO_ERROR;
    std::vector<UChar> unicode(static_cast<std::size_t>(length));
    length = ucnv_toUChars(converter.get(), unicode.data(), length,
        raw.data(), input_size, &status);
    if (U_FAILURE(status)) {
        return std::nullopt;
    }
    std::int32_t utf8_length = 0;
    u_strToUTF8(nullptr, 0, &utf8_length, unicode.data(), length, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
        return std::nullopt;
    }
    status = U_ZERO_ERROR;
    std::string result(static_cast<std::size_t>(utf8_length), '\0');
    u_strToUTF8(result.data(), utf8_length, nullptr, unicode.data(), length, &status);
    if (U_FAILURE(status)) {
        return std::nullopt;
    }
    return result;
}

} // namespace getbiblesword
