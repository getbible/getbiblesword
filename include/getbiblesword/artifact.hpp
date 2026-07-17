// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "getbiblesword/ndjson_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace getbiblesword {

struct Diagnostic final {
    std::string severity;
    std::string code;
    std::string message;
    JsonFields context;
};

struct ArtifactCandidate final {
    std::filesystem::path absolute_path;
    std::filesystem::path relative_path;
    std::string role;
};

struct ArtifactResult final {
    std::uint64_t artifacts{0};
    std::uint64_t bytes{0};
    std::vector<Diagnostic> diagnostics;
    bool success{true};
};

[[nodiscard]] std::vector<ArtifactCandidate> discover_artifacts(
    const std::filesystem::path& sword_root,
    const std::string& data_path,
    const std::vector<std::filesystem::path>& configuration_files,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] ArtifactResult emit_artifacts(
    const std::vector<ArtifactCandidate>& candidates,
    std::size_t chunk_size,
    NdjsonWriter& writer);

} // namespace getbiblesword
