// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/artifact.hpp"

#include "getbiblesword/byte_value.hpp"
#include "getbiblesword/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace getbiblesword {
namespace {

constexpr mode_t permission_mask = 07777;

Diagnostic diagnostic(
    std::string severity,
    std::string code,
    std::string message,
    JsonFields context = {}) {
    return {std::move(severity), std::move(code), std::move(message), std::move(context)};
}

bool is_within_root(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty()) {
        return candidate == root;
    }
    const auto first = relative.begin();
    return first != relative.end() && *first != ".." && !relative.is_absolute();
}

std::filesystem::path normalize_candidate(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    const auto normalized = candidate.is_absolute()
        ? candidate.lexically_normal()
        : (root / candidate).lexically_normal();
    if (!is_within_root(root, normalized)) {
        throw std::runtime_error("artifact path escapes the SWORD root");
    }
    return normalized;
}

void add_candidate(
    std::vector<ArtifactCandidate>& output,
    const std::filesystem::path& root,
    const std::filesystem::path& absolute,
    const std::string& role) {
    output.push_back({absolute, absolute.lexically_relative(root), role});
}

std::string path_bytes(const std::filesystem::path& path) {
    return path.native();
}

std::string errno_message(const int error) {
    return std::error_code(error, std::generic_category()).message();
}

bool same_file_state(const struct stat& before, const struct stat& after) {
    return before.st_dev == after.st_dev
        && before.st_ino == after.st_ino
        && before.st_mode == after.st_mode
        && before.st_size == after.st_size
        && before.st_mtim.tv_sec == after.st_mtim.tv_sec
        && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
        && before.st_ctim.tv_sec == after.st_ctim.tv_sec
        && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
}

class FileDescriptor final {
public:
    explicit FileDescriptor(const int value) : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            static_cast<void>(::close(value_));
        }
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    [[nodiscard]] int get() const noexcept { return value_; }

private:
    int value_;
};

void emit_diagnostic(NdjsonWriter& writer, const Diagnostic& value) {
    JsonFields fields = value.context;
    fields.emplace("code", json_string(value.code));
    fields.emplace("message", byte_value_json(value.message));
    fields.emplace("severity", json_string(value.severity));
    writer.emit("diagnostic", std::move(fields));
}

} // namespace

std::vector<ArtifactCandidate> discover_artifacts(
    const std::filesystem::path& sword_root,
    const std::string& data_path,
    const std::vector<std::filesystem::path>& configuration_files,
    std::vector<Diagnostic>& diagnostics) {
    std::error_code error;
    const auto root = std::filesystem::canonical(sword_root, error);
    if (error) {
        diagnostics.push_back(diagnostic(
            "error", "artifact.root.invalid", "Unable to canonicalize the SWORD root.",
            {{"detail", byte_value_json(error.message())}}));
        return {};
    }

    std::vector<ArtifactCandidate> output;
    for (const auto& config : configuration_files) {
        try {
            const auto normalized = normalize_candidate(root, config);
            add_candidate(output, root, normalized, "configuration");
        } catch (const std::exception& exception) {
            diagnostics.push_back(diagnostic(
                "error", "artifact.config.escape", exception.what(),
                {{"path", byte_value_json(path_bytes(config))}}));
        }
    }

    if (data_path.empty()) {
        diagnostics.push_back(diagnostic(
            "error", "artifact.datapath.missing", "The module has no interpreted DataPath."));
    } else {
        try {
            const auto data = normalize_candidate(root, std::filesystem::path(data_path));
            const auto status = std::filesystem::symlink_status(data, error);
            if (!error && std::filesystem::exists(status)) {
                add_candidate(output, root, data, "module_data");
                if (std::filesystem::is_directory(status)) {
                    std::filesystem::recursive_directory_iterator iterator(
                        data, std::filesystem::directory_options::none, error);
                    const std::filesystem::recursive_directory_iterator end;
                    while (!error && iterator != end) {
                        add_candidate(output, root, iterator->path(), "module_data");
                        iterator.increment(error);
                    }
                    if (error) {
                        diagnostics.push_back(diagnostic(
                            "error", "artifact.enumeration.failed",
                            "Unable to enumerate the complete module DataPath.",
                            {{"detail", byte_value_json(error.message())},
                             {"path", byte_value_json(path_bytes(data.lexically_relative(root)))}}));
                    }
                }
            } else {
                error.clear();
                const auto parent = data.parent_path();
                const auto prefix = data.filename().native();
                std::filesystem::directory_iterator iterator(
                    parent, std::filesystem::directory_options::none, error);
                const std::filesystem::directory_iterator end;
                bool matched = false;
                while (!error && iterator != end) {
                    const auto filename = iterator->path().filename().native();
                    if (filename.starts_with(prefix)) {
                        add_candidate(output, root, iterator->path(), "module_data_prefix");
                        matched = true;
                    }
                    iterator.increment(error);
                }
                if (error || !matched) {
                    diagnostics.push_back(diagnostic(
                        "error", "artifact.datapath.unresolved",
                        "DataPath does not exist and no prefix-style module files were found.",
                        {{"path", byte_value_json(data_path)}}));
                }
            }
        } catch (const std::exception& exception) {
            diagnostics.push_back(diagnostic(
                "error", "artifact.datapath.escape", exception.what(),
                {{"path", byte_value_json(data_path)}}));
        }
    }

    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return left.relative_path.native() < right.relative_path.native();
    });
    output.erase(std::unique(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return left.absolute_path == right.absolute_path;
    }), output.end());
    return output;
}

ArtifactResult emit_artifacts(
    const std::vector<ArtifactCandidate>& candidates,
    const std::size_t chunk_size,
    NdjsonWriter& writer) {
    ArtifactResult result;
    if (chunk_size == 0U || chunk_size > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())) {
        result.success = false;
        result.diagnostics.push_back(diagnostic(
            "error", "artifact.chunk_size.invalid", "Artifact chunk size is outside the supported range."));
        return result;
    }

    std::vector<unsigned char> buffer(chunk_size);
    std::uint64_t artifact_id = 0;

    for (const auto& candidate : candidates) {
        struct stat metadata{};
        if (::lstat(candidate.absolute_path.c_str(), &metadata) != 0) {
            result.success = false;
            result.diagnostics.push_back(diagnostic(
                "error", "artifact.stat.failed", "Unable to inspect an artifact.",
                {{"detail", byte_value_json(errno_message(errno))},
                 {"path", byte_value_json(path_bytes(candidate.relative_path))}}));
            continue;
        }

        const auto mode = static_cast<unsigned int>(metadata.st_mode & permission_mask);
        const auto common = JsonFields{
            {"artifact_id", std::to_string(artifact_id)},
            {"mode", std::to_string(mode)},
            {"path", byte_value_json(path_bytes(candidate.relative_path))},
            {"role", json_string(candidate.role)}
        };

        if (S_ISDIR(metadata.st_mode)) {
            auto fields = common;
            fields.emplace("file_type", "\"directory\"");
            writer.emit("artifact_begin", std::move(fields));
            writer.emit("artifact_end", {
                {"artifact_id", std::to_string(artifact_id)},
                {"sha256", json_string(sha256_hex(std::string_view{}))},
                {"size", "0"}
            });
        } else if (S_ISLNK(metadata.st_mode)) {
            std::vector<char> target(static_cast<std::size_t>(metadata.st_size) + 1U);
            const auto length = ::readlink(candidate.absolute_path.c_str(), target.data(), target.size());
            if (length < 0) {
                result.success = false;
                result.diagnostics.push_back(diagnostic(
                    "error", "artifact.symlink.read_failed", "Unable to read a symbolic-link target.",
                    {{"detail", byte_value_json(errno_message(errno))},
                     {"path", byte_value_json(path_bytes(candidate.relative_path))}}));
                continue;
            }
            const std::string_view target_bytes(target.data(), static_cast<std::size_t>(length));
            auto fields = common;
            fields.emplace("file_type", "\"symlink\"");
            fields.emplace("target", byte_value_json(target_bytes));
            writer.emit("artifact_begin", std::move(fields));
            writer.emit("artifact_end", {
                {"artifact_id", std::to_string(artifact_id)},
                {"sha256", json_string(sha256_hex(target_bytes))},
                {"size", std::to_string(target_bytes.size())}
            });
            result.bytes += static_cast<std::uint64_t>(target_bytes.size());
        } else if (S_ISREG(metadata.st_mode)) {
            FileDescriptor file(::open(candidate.absolute_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
            if (file.get() < 0) {
                result.success = false;
                result.diagnostics.push_back(diagnostic(
                    "error", "artifact.open.failed", "Unable to open an artifact without following links.",
                    {{"detail", byte_value_json(errno_message(errno))},
                     {"path", byte_value_json(path_bytes(candidate.relative_path))}}));
                continue;
            }

            struct stat opened{};
            if (::fstat(file.get(), &opened) != 0 || !same_file_state(metadata, opened)) {
                result.success = false;
                result.diagnostics.push_back(diagnostic(
                    "error", "artifact.changed_before_read", "Artifact identity changed before reading.",
                    {{"path", byte_value_json(path_bytes(candidate.relative_path))}}));
                continue;
            }

            auto fields = common;
            fields.emplace("file_type", "\"regular\"");
            fields.emplace("size_expected", std::to_string(metadata.st_size));
            writer.emit("artifact_begin", std::move(fields));

            Sha256 hash;
            std::uint64_t total = 0;
            std::uint64_t chunk_index = 0;
            bool read_failed = false;
            while (true) {
                const auto count = ::read(file.get(), buffer.data(), buffer.size());
                if (count == 0) {
                    break;
                }
                if (count < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    read_failed = true;
                    result.success = false;
                    result.diagnostics.push_back(diagnostic(
                        "error", "artifact.read.failed", "Unable to read an artifact completely.",
                        {{"detail", byte_value_json(errno_message(errno))},
                         {"path", byte_value_json(path_bytes(candidate.relative_path))}}));
                    break;
                }

                const auto size = static_cast<std::size_t>(count);
                const auto chunk = std::span<const unsigned char>(buffer.data(), size);
                hash.update(chunk);
                writer.emit("artifact_chunk", {
                    {"artifact_id", std::to_string(artifact_id)},
                    {"data", byte_value_json(chunk)},
                    {"index", std::to_string(chunk_index)}
                });
                total += static_cast<std::uint64_t>(size);
                ++chunk_index;
            }

            struct stat after{};
            const bool stable = ::fstat(file.get(), &after) == 0 && same_file_state(opened, after);
            if (!stable) {
                result.success = false;
                result.diagnostics.push_back(diagnostic(
                    "error", "artifact.changed_during_read", "Artifact changed while it was being read.",
                    {{"path", byte_value_json(path_bytes(candidate.relative_path))}}));
            }
            writer.emit("artifact_end", {
                {"artifact_id", std::to_string(artifact_id)},
                {"sha256", json_string(hash.hex_digest())},
                {"size", std::to_string(total)},
                {"stable", stable && !read_failed ? "true" : "false"}
            });
            result.bytes += total;
        } else {
            result.success = false;
            result.diagnostics.push_back(diagnostic(
                "error", "artifact.type.unsupported",
                "The module tree contains an unsupported filesystem object.",
                {{"mode", std::to_string(static_cast<unsigned int>(metadata.st_mode))},
                 {"path", byte_value_json(path_bytes(candidate.relative_path))}}));
            continue;
        }

        ++artifact_id;
        ++result.artifacts;
    }

    for (const auto& value : result.diagnostics) {
        emit_diagnostic(writer, value);
    }
    return result;
}

} // namespace getbiblesword
