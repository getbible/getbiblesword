// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "getbiblesword/ndjson_writer.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace getbiblesword {

struct ExtractionOptions final {
    std::filesystem::path sword_path;
    std::string module_name;
    std::size_t artifact_chunk_size{1024U * 1024U};
};

[[nodiscard]] bool list_modules(
    const std::filesystem::path& sword_path,
    NdjsonWriter& writer);

[[nodiscard]] bool extract_module(
    const ExtractionOptions& options,
    NdjsonWriter& writer);

} // namespace getbiblesword
