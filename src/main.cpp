// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/byte_value.hpp"
#include "getbiblesword/ndjson_writer.hpp"
#include "getbiblesword/sword_extractor.hpp"
#include "getbiblesword/version.hpp"

#include <swversion.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {

constexpr std::size_t minimum_chunk_size = 4096U;
constexpr std::size_t maximum_chunk_size = 16U * 1024U * 1024U;

struct CommandLine final {
    std::string command;
    std::filesystem::path sword_path;
    std::string module_name;
    std::optional<std::filesystem::path> output_path;
    std::size_t chunk_size{1024U * 1024U};
    bool force{false};
};

void print_help(std::ostream& output) {
    output
        << "getBibleSword " << GETBIBLESWORD_VERSION << '\n'
        << "Lossless deterministic extraction using CrossWire SWORD.\n\n"
        << "Usage:\n"
        << "  getbiblesword list --sword-path PATH [--output FILE] [--force]\n"
        << "  getbiblesword extract --sword-path PATH --module NAME [options]\n"
        << "  getbiblesword contract\n"
        << "  getbiblesword version\n\n"
        << "Extract options:\n"
        << "  --output FILE                 Write atomically to FILE instead of stdout\n"
        << "  --force                       Replace an existing output file\n"
        << "  --artifact-chunk-size BYTES   4096 through 16777216 (default 1048576)\n\n"
        << "GPL-2.0-only; no warranty.\n";
}

std::size_t parse_chunk_size(const std::string_view input) {
    std::size_t value = 0;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    if (result.ec != std::errc{} || result.ptr != input.data() + input.size()
        || value < minimum_chunk_size || value > maximum_chunk_size) {
        throw std::invalid_argument(
            "--artifact-chunk-size must be an integer from 4096 through 16777216");
    }
    return value;
}

CommandLine parse_arguments(const int argc, char** argv) {
    if (argc < 2) {
        throw std::invalid_argument("a command is required");
    }

    CommandLine result;
    result.command = argv[1];
    if (result.command == "--help" || result.command == "-h") {
        result.command = "help";
        return result;
    }
    if (result.command == "--version") {
        result.command = "version";
        return result;
    }
    if (result.command == "help" || result.command == "version" || result.command == "contract") {
        if (argc != 2) {
            throw std::invalid_argument(result.command + " does not accept options");
        }
        return result;
    }
    if (result.command != "list" && result.command != "extract") {
        throw std::invalid_argument("unknown command: " + result.command);
    }

    for (int index = 2; index < argc; ++index) {
        const std::string_view option(argv[index]);
        const auto require_value = [&](const std::string_view name) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string(name) + " requires a value");
            }
            ++index;
            return argv[index];
        };

        if (option == "--sword-path") {
            result.sword_path = std::filesystem::path(require_value(option));
        } else if (option == "--module") {
            result.module_name = require_value(option);
        } else if (option == "--output") {
            result.output_path = std::filesystem::path(require_value(option));
        } else if (option == "--artifact-chunk-size") {
            result.chunk_size = parse_chunk_size(require_value(option));
        } else if (option == "--force") {
            result.force = true;
        } else if (option == "--help" || option == "-h") {
            result.command = "help";
            return result;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }

    if (result.sword_path.empty()) {
        throw std::invalid_argument("--sword-path is required");
    }
    if (result.command == "extract" && result.module_name.empty()) {
        throw std::invalid_argument("--module is required for extract");
    }
    if (result.command == "list" && !result.module_name.empty()) {
        throw std::invalid_argument("--module is only valid for extract");
    }
    if (result.command == "list" && result.chunk_size != 1024U * 1024U) {
        throw std::invalid_argument("--artifact-chunk-size is only valid for extract");
    }
    if (result.force && !result.output_path) {
        throw std::invalid_argument("--force requires --output");
    }
    return result;
}

std::string system_error_message(const int error) {
    return std::error_code(error, std::generic_category()).message();
}

class FileDescriptorBuffer final : public std::streambuf {
public:
    FileDescriptorBuffer() {
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    void attach(const int file_descriptor) noexcept {
        file_descriptor_ = file_descriptor;
    }

    void detach() noexcept {
        file_descriptor_ = -1;
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

protected:
    int_type overflow(const int_type character) override {
        if (!flush_pending()) {
            return traits_type::eof();
        }
        if (!traits_type::eq_int_type(character, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(character);
            pbump(1);
        }
        return traits_type::not_eof(character);
    }

    int sync() override {
        return flush_pending() ? 0 : -1;
    }

private:
    bool flush_pending() {
        const auto pending = pptr() - pbase();
        std::ptrdiff_t written = 0;
        while (written < pending) {
            const auto count = ::write(
                file_descriptor_, pbase() + written,
                static_cast<std::size_t>(pending - written));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            written += count;
        }
        setp(buffer_.data(), buffer_.data() + buffer_.size());
        return true;
    }

    std::array<char, 64U * 1024U> buffer_{};
    int file_descriptor_{-1};
};

class AtomicOutput final {
public:
    AtomicOutput(const std::filesystem::path& target, const bool force)
        : target_(target), force_(force) {
        std::error_code error;
        if (!force && std::filesystem::exists(target_, error)) {
            throw std::runtime_error("output file already exists; use --force to replace it");
        }
        if (error) {
            throw std::runtime_error("unable to inspect output path: " + error.message());
        }

        parent_ = target_.parent_path();
        if (parent_.empty()) {
            parent_ = ".";
        }
        auto template_path = (parent_ / ".getbiblesword.tmp.XXXXXX").native();
        file_descriptor_ = ::mkstemp(template_path.data());
        if (file_descriptor_ < 0) {
            throw std::runtime_error(
                "unable to create a secure temporary output file: "
                + system_error_message(errno));
        }
        temporary_ = std::filesystem::path(template_path);
        if (::fcntl(file_descriptor_, F_SETFD, FD_CLOEXEC) != 0) {
            const auto error_number = errno;
            static_cast<void>(::close(file_descriptor_));
            file_descriptor_ = -1;
            static_cast<void>(::unlink(temporary_.c_str()));
            throw std::runtime_error(
                "unable to secure the temporary output descriptor: "
                + system_error_message(error_number));
        }
        buffer_.attach(file_descriptor_);
        stream_.rdbuf(&buffer_);
    }

    ~AtomicOutput() {
        buffer_.detach();
        if (file_descriptor_ >= 0) {
            static_cast<void>(::close(file_descriptor_));
        }
        if (!committed_) {
            static_cast<void>(::unlink(temporary_.c_str()));
        }
    }

    AtomicOutput(const AtomicOutput&) = delete;
    AtomicOutput& operator=(const AtomicOutput&) = delete;

    [[nodiscard]] std::ostream& stream() noexcept { return stream_; }

    void commit() {
        stream_.flush();
        if (!stream_) {
            throw std::runtime_error("failed to flush output file");
        }
        if (::fsync(file_descriptor_) != 0) {
            throw std::runtime_error(
                "failed to synchronize output file: " + system_error_message(errno));
        }
        buffer_.detach();
        if (::close(file_descriptor_) != 0) {
            file_descriptor_ = -1;
            throw std::runtime_error(
                "failed to close output file: " + system_error_message(errno));
        }
        file_descriptor_ = -1;

        if (force_) {
            if (::rename(temporary_.c_str(), target_.c_str()) != 0) {
                throw std::runtime_error(
                    "failed to replace output file atomically: " + system_error_message(errno));
            }
        } else {
            if (::link(temporary_.c_str(), target_.c_str()) != 0) {
                throw std::runtime_error(
                    "failed to commit output without overwriting: "
                    + system_error_message(errno));
            }
            if (::unlink(temporary_.c_str()) != 0) {
                throw std::runtime_error(
                    "output committed but temporary link cleanup failed: "
                    + system_error_message(errno));
            }
        }

        const int directory = ::open(parent_.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
        if (directory < 0) {
            throw std::runtime_error(
                "output committed but its directory could not be opened for synchronization: "
                + system_error_message(errno));
        }
        const int sync_status = ::fsync(directory);
        const int sync_error = errno;
        const int close_status = ::close(directory);
        const int close_error = errno;
        if (sync_status != 0 || close_status != 0) {
            throw std::runtime_error(
                "output committed but its directory could not be synchronized: "
                + system_error_message(sync_status != 0 ? sync_error : close_error));
        }
        committed_ = true;
    }

private:
    std::filesystem::path target_;
    std::filesystem::path temporary_;
    std::filesystem::path parent_;
    FileDescriptorBuffer buffer_;
    std::ostream stream_{nullptr};
    int file_descriptor_{-1};
    bool force_{false};
    bool committed_{false};
};

bool run_extraction(const CommandLine& command, std::ostream& output) {
    getbiblesword::NdjsonWriter writer(output);
    if (command.command == "list") {
        return getbiblesword::list_modules(command.sword_path, writer);
    }
    return getbiblesword::extract_module({
        command.sword_path,
        command.module_name,
        command.chunk_size
    }, writer);
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const auto command = parse_arguments(argc, argv);
        if (command.command == "help") {
            print_help(std::cout);
            return 0;
        }
        if (command.command == "version") {
            std::cout << "getBibleSword " << GETBIBLESWORD_VERSION
                      << " (CrossWire SWORD " << SWORD_VERSION_STR << ")\n"
                      << "License: GPL-2.0-only\n";
            return 0;
        }
        if (command.command == "contract") {
            std::cout
                << "{\"contract\":\"getbiblesword.ndjson/v1\","
                << "\"documentation\":\"docs/contract-v1.md\","
                << "\"schema\":\"schema/v1/contract.schema.json\"}\n";
            return 0;
        }

        if (command.output_path) {
            AtomicOutput output(*command.output_path, command.force);
            const bool success = run_extraction(command, output.stream());
            output.commit();
            return success ? 0 : 1;
        }
        return run_extraction(command, std::cout) ? 0 : 1;
    } catch (const std::invalid_argument& exception) {
        std::cerr << "getbiblesword: " << exception.what() << "\n\n";
        print_help(std::cerr);
        return 2;
    } catch (const std::exception& exception) {
        std::cerr << "getbiblesword: " << exception.what() << '\n';
        return 1;
    }
}
