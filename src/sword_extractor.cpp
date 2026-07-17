// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/sword_extractor.hpp"

#include "getbiblesword/annotation.hpp"
#include "getbiblesword/artifact.hpp"
#include "getbiblesword/byte_value.hpp"
#include "getbiblesword/version.hpp"

#include <swbuf.h>
#include <swconfig.h>
#include <swkey.h>
#include <swmgr.h>
#include <swmodule.h>
#include <swversion.h>
#include <versekey.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace getbiblesword {
namespace {

struct DiagnosticCounts final {
    std::uint64_t info{0};
    std::uint64_t warning{0};
    std::uint64_t error{0};
};

std::string safe_c_string(const char* value) {
    return value == nullptr ? std::string{} : std::string(value);
}

std::string swbuf_bytes(const sword::SWBuf& value) {
    return std::string(value.c_str(), value.size());
}

std::string json_object(const JsonFields& fields) {
    std::string output = "{";
    bool first = true;
    for (const auto& [name, value] : fields) {
        if (!first) {
            output += ',';
        }
        first = false;
        output += json_string(name) + ':' + value;
    }
    output += '}';
    return output;
}

void emit_diagnostic(
    NdjsonWriter& writer,
    DiagnosticCounts& counts,
    const std::string_view severity,
    const std::string_view code,
    const std::string_view message,
    JsonFields context = {}) {
    if (severity == "error") {
        ++counts.error;
    } else if (severity == "warning") {
        ++counts.warning;
    } else {
        ++counts.info;
    }
    context.emplace("code", json_string(code));
    context.emplace("message", byte_value_json(message));
    context.emplace("severity", json_string(severity));
    writer.emit("diagnostic", std::move(context));
}

JsonFields footer_fields(
    const DiagnosticCounts& diagnostics,
    const std::uint64_t entries,
    const std::uint64_t artifacts,
    const std::uint64_t artifact_bytes) {
    return {
        {"artifact_bytes", std::to_string(artifact_bytes)},
        {"artifacts", std::to_string(artifacts)},
        {"diagnostics", json_object({
            {"error", std::to_string(diagnostics.error)},
            {"info", std::to_string(diagnostics.info)},
            {"warning", std::to_string(diagnostics.warning)}
        })},
        {"entries", std::to_string(entries)}
    };
}

void emit_header(
    NdjsonWriter& writer,
    const std::string_view command,
    const std::size_t chunk_size = 0U) {
    JsonFields fields{
        {"command", json_string(command)},
        {"contract", "\"getbiblesword.ndjson/v1\""},
        {"contract_version", "1"},
        {"deterministic", "true"},
        {"producer", "\"getBibleSword\""},
        {"producer_version", json_string(GETBIBLESWORD_VERSION)},
        {"sword_version", json_string(SWORD_VERSION_STR)}
    };
    if (chunk_size != 0U) {
        fields.emplace("artifact_chunk_size", std::to_string(chunk_size));
    }
    writer.emit("header", std::move(fields));
}

std::string direction_name(const char value) {
    switch (value) {
    case sword::DIRECTION_LTR: return "ltr";
    case sword::DIRECTION_RTL: return "rtl";
    case sword::DIRECTION_BIDI: return "bidi";
    default: return "unknown";
    }
}

std::string encoding_name(const char value) {
    switch (value) {
    case sword::ENC_LATIN1: return "latin1";
    case sword::ENC_UTF8: return "utf8";
    case sword::ENC_SCSU: return "scsu";
    case sword::ENC_UTF16: return "utf16";
    case sword::ENC_RTF: return "rtf";
    case sword::ENC_HTML: return "html";
    default: return "unknown";
    }
}

std::string markup_name(const char value) {
    switch (value) {
    case sword::FMT_PLAIN: return "plain";
    case sword::FMT_THML: return "thml";
    case sword::FMT_GBF: return "gbf";
    case sword::FMT_HTML: return "html";
    case sword::FMT_HTMLHREF: return "htmlhref";
    case sword::FMT_RTF: return "rtf";
    case sword::FMT_OSIS: return "osis";
    case sword::FMT_WEBIF: return "webif";
    case sword::FMT_TEI: return "tei";
    case sword::FMT_XHTML: return "xhtml";
    case sword::FMT_LATEX: return "latex";
    default: return "unknown";
    }
}

std::string enum_json(const char code, const std::string_view name) {
    return json_object({
        {"code", std::to_string(static_cast<unsigned int>(static_cast<unsigned char>(code)))},
        {"name", json_string(name)}
    });
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool config_contains(const sword::SWModule& module, const std::string_view needle) {
    const auto normalized = lowercase_ascii(std::string(needle));
    for (const auto& [name, value] : module.getConfig()) {
        const auto name_string = lowercase_ascii(swbuf_bytes(name));
        const auto value_string = lowercase_ascii(swbuf_bytes(value));
        if (name_string == "category" || name_string == "feature" || name_string == "type") {
            if (value_string.find(normalized) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

std::string classify_module(const sword::SWModule& module) {
    const auto type = safe_c_string(module.getType());
    if (type == sword::SWMgr::MODTYPE_BIBLES) {
        return "bible";
    }
    if (type == sword::SWMgr::MODTYPE_COMMENTARIES) {
        return "commentary";
    }
    if (type == sword::SWMgr::MODTYPE_LEXDICTS) {
        return "dictionary_or_lexicon";
    }
    if (type == sword::SWMgr::MODTYPE_DAILYDEVOS) {
        return "devotional";
    }
    if (type == sword::SWMgr::MODTYPE_GENBOOKS) {
        if (config_contains(module, "map") || config_contains(module, "image")
            || config_contains(module, "resource")) {
            return "resource";
        }
        return "general_book";
    }
    return "unknown";
}

JsonFields module_fields(const sword::SWModule& module) {
    const auto driver = safe_c_string(module.getConfigEntry("ModDrv"));
    return {
        {"classification", json_string(classify_module(module))},
        {"description", byte_value_json(safe_c_string(module.getDescription()))},
        {"direction", enum_json(module.getDirection(), direction_name(module.getDirection()))},
        {"driver", byte_value_json(driver)},
        {"encoding", enum_json(module.getEncoding(), encoding_name(module.getEncoding()))},
        {"language", byte_value_json(safe_c_string(module.getLanguage()))},
        {"markup", enum_json(module.getMarkup(), markup_name(module.getMarkup()))},
        {"name", byte_value_json(safe_c_string(module.getName()))},
        {"sword_type", byte_value_json(safe_c_string(module.getType()))}
    };
}

bool ascii_iequals(const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto l = static_cast<unsigned char>(left[index]);
        const auto r = static_cast<unsigned char>(right[index]);
        if (std::tolower(l) != std::tolower(r)) {
            return false;
        }
    }
    return true;
}

bool defines_module(const std::string_view bytes, const std::string_view module_name) {
    std::size_t offset = 0;
    while (offset <= bytes.size()) {
        const auto newline = bytes.find('\n', offset);
        const auto length = newline == std::string_view::npos ? bytes.size() - offset : newline - offset;
        auto line = bytes.substr(offset, length);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        if (offset == 0U && line.starts_with("\xef\xbb\xbf")) {
            line.remove_prefix(3U);
        }
        if (line.size() >= 2U && line.front() == '[' && line.back() == ']'
            && ascii_iequals(line.substr(1U, line.size() - 2U), module_name)) {
            return true;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        offset = newline + 1U;
    }
    return false;
}

std::optional<std::string> read_file(const std::filesystem::path& path, std::string& error_message) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error_message = "Unable to open the configuration file.";
        return std::nullopt;
    }
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        error_message = "Unable to read the complete configuration file.";
        return std::nullopt;
    }
    return bytes;
}

std::vector<std::filesystem::path> find_configuration_files(
    const std::filesystem::path& sword_root,
    const std::string_view module_name,
    NdjsonWriter& writer,
    DiagnosticCounts& diagnostics) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    const auto master_config = sword_root / "mods.conf";
    const auto master_status = std::filesystem::symlink_status(master_config, error);
    if (!error && std::filesystem::is_regular_file(master_status)) {
        std::string read_error;
        const auto bytes = read_file(master_config, read_error);
        if (!bytes) {
            emit_diagnostic(
                writer, diagnostics, "error", "config.read.failed", read_error,
                {{"path", byte_value_json(master_config.lexically_relative(sword_root).native())}});
        } else if (defines_module(*bytes, module_name)) {
            files.push_back(master_config);
        }
        return files;
    }
    if (!error && std::filesystem::is_symlink(master_status)) {
        emit_diagnostic(
            writer, diagnostics, "error", "config.symlink.refused",
            "The active SWORD mods.conf symlink was not followed.",
            {{"path", byte_value_json(master_config.lexically_relative(sword_root).native())}});
        return files;
    }
    error.clear();

    const auto config_root = sword_root / "mods.d";
    std::filesystem::recursive_directory_iterator iterator(
        config_root, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        emit_diagnostic(
            writer, diagnostics, "error", "config.directory.unreadable",
            "Unable to enumerate the SWORD mods.d directory.",
            {{"detail", byte_value_json(error.message())}});
        return files;
    }

    while (iterator != end) {
        const auto path = iterator->path();
        const auto status = iterator->symlink_status(error);
        if (error) {
            emit_diagnostic(
                writer, diagnostics, "error", "config.stat.failed",
                "Unable to inspect a configuration candidate.",
                {{"detail", byte_value_json(error.message())},
                 {"path", byte_value_json(path.lexically_relative(sword_root).native())}});
            error.clear();
            iterator.increment(error);
            continue;
        }

        if (std::filesystem::is_regular_file(status)) {
            std::string read_error;
            const auto bytes = read_file(path, read_error);
            if (!bytes) {
                emit_diagnostic(
                    writer, diagnostics, "error", "config.read.failed", read_error,
                    {{"path", byte_value_json(path.lexically_relative(sword_root).native())}});
            } else if (defines_module(*bytes, module_name)) {
                files.push_back(path);
            }
        } else if (std::filesystem::is_symlink(status)) {
            emit_diagnostic(
                writer, diagnostics, "error", "config.symlink.refused",
                "A configuration symlink was not followed.",
                {{"path", byte_value_json(path.lexically_relative(sword_root).native())}});
        }
        iterator.increment(error);
        if (error) {
            emit_diagnostic(
                writer, diagnostics, "error", "config.enumeration.failed",
                "Configuration enumeration did not complete.",
                {{"detail", byte_value_json(error.message())}});
            break;
        }
    }

    std::sort(files.begin(), files.end(), [&sword_root](const auto& left, const auto& right) {
        return left.lexically_relative(sword_root).native() < right.lexically_relative(sword_root).native();
    });
    return files;
}

void emit_configuration_sources(
    const std::filesystem::path& root,
    const std::vector<std::filesystem::path>& files,
    NdjsonWriter& writer,
    DiagnosticCounts& diagnostics) {
    std::uint64_t ordinal = 0;
    for (const auto& file : files) {
        std::string error_message;
        const auto bytes = read_file(file, error_message);
        if (!bytes) {
            emit_diagnostic(
                writer, diagnostics, "error", "config.read.failed", error_message,
                {{"path", byte_value_json(file.lexically_relative(root).native())}});
            continue;
        }
        writer.emit("config_source", {
            {"ordinal", std::to_string(ordinal)},
            {"path", byte_value_json(file.lexically_relative(root).native())},
            {"raw", byte_value_json(*bytes)}
        });
        ++ordinal;
    }
}

void emit_interpreted_configuration(const sword::SWModule& module, NdjsonWriter& writer) {
    std::uint64_t ordinal = 0;
    for (const auto& [name, value] : module.getConfig()) {
        writer.emit("config_entry", {
            {"name", byte_value_json(swbuf_bytes(name))},
            {"ordinal", std::to_string(ordinal)},
            {"value", byte_value_json(swbuf_bytes(value))}
        });
        ++ordinal;
    }
}

std::string official_attributes_json(const sword::AttributeTypeList& attributes) {
    std::string output = "[";
    bool first_type = true;
    for (const auto& [type_name, lists] : attributes) {
        if (!first_type) {
            output += ',';
        }
        first_type = false;
        output += "{\"lists\":[";
        bool first_list = true;
        for (const auto& [list_name, values] : lists) {
            if (!first_list) {
                output += ',';
            }
            first_list = false;
            output += "{\"name\":" + byte_value_json(swbuf_bytes(list_name)) + ",\"values\":[";
            bool first_value = true;
            for (const auto& [value_name, value] : values) {
                if (!first_value) {
                    output += ',';
                }
                first_value = false;
                output += "{\"name\":" + byte_value_json(swbuf_bytes(value_name))
                    + ",\"value\":" + byte_value_json(swbuf_bytes(value)) + '}';
            }
            output += "]}";
        }
        output += "],\"name\":" + byte_value_json(swbuf_bytes(type_name)) + '}';
    }
    output += ']';
    return output;
}

std::string scope_json(sword::SWModule& module) {
    auto* key = module.getKey();
    using sword::VerseKey;
    auto* verse_key = SWDYNAMIC_CAST(VerseKey, key);
    if (verse_key == nullptr) {
        return json_object({
            {"index", std::to_string(key->getIndex())},
            {"type", "\"sword_key\""}
        });
    }

    const auto testament = static_cast<int>(verse_key->getTestament());
    const auto book = static_cast<int>(verse_key->getBook());
    const auto chapter = verse_key->getChapter();
    const auto verse = verse_key->getVerse();
    std::string intro_scope = "verse";
    if (book == 0) {
        intro_scope = testament == 0 ? "module" : "testament";
    } else if (chapter == 0) {
        intro_scope = "book";
    } else if (verse == 0) {
        intro_scope = "chapter";
    }

    const auto book_abbreviation = book == 0
        ? std::string("null")
        : byte_value_json(safe_c_string(verse_key->getBookAbbrev()));
    const auto book_name = book == 0
        ? std::string("null")
        : byte_value_json(safe_c_string(verse_key->getBookName()));

    return json_object({
        {"book", std::to_string(book)},
        {"book_abbreviation", book_abbreviation},
        {"book_name", book_name},
        {"chapter", std::to_string(chapter)},
        {"index", std::to_string(verse_key->getIndex())},
        {"intro_scope", json_string(intro_scope)},
        {"osis_reference", byte_value_json(safe_c_string(verse_key->getOSISRef()))},
        {"suffix", std::to_string(static_cast<int>(verse_key->getSuffix()))},
        {"testament", std::to_string(testament)},
        {"type", "\"verse_key\""},
        {"verse", std::to_string(verse)},
        {"versification", byte_value_json(safe_c_string(verse_key->getVersificationSystem()))}
    });
}

std::optional<int> sword_length(
    const std::size_t size,
    NdjsonWriter& writer,
    DiagnosticCounts& diagnostics,
    const std::uint64_t ordinal) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        emit_diagnostic(
            writer, diagnostics, "error", "entry.length.unsupported",
            "The raw entry is larger than the SWORD rendering API can address.",
            {{"entry_ordinal", std::to_string(ordinal)}, {"size", std::to_string(size)}});
        return std::nullopt;
    }
    return static_cast<int>(size);
}

bool emit_entries(
    sword::SWModule& module,
    NdjsonWriter& writer,
    DiagnosticCounts& diagnostics,
    std::uint64_t& entry_count) {
    module.setSkipConsecutiveLinks(false);
    module.setProcessEntryAttributes(true);

    using sword::VerseKey;
    if (auto* verse_key = SWDYNAMIC_CAST(VerseKey, module.getKey()); verse_key != nullptr) {
        verse_key->setIntros(true);
    }

    module.setPosition(sword::TOP);
    if (module.popError() != 0) {
        emit_diagnostic(
            writer, diagnostics, "warning", "module.empty",
            "SWORD could not position the module at its first entry.");
        return true;
    }

    std::string previous_key;
    long previous_index = std::numeric_limits<long>::min();
    while (true) {
        const auto key = safe_c_string(module.getKeyText());
        // SWModule::getIndex() reads the module's entryIndex cache. Some official
        // drivers, including SWGenBook, do not initialize that cache. The key is
        // the authoritative traversal object and supplies the stable index for
        // every key type.
        const auto index = module.getKey()->getIndex();
        if (entry_count != 0U && key == previous_key && index == previous_index) {
            emit_diagnostic(
                writer, diagnostics, "error", "module.navigation.stalled",
                "SWORD traversal did not advance to a new key.",
                {{"entry_ordinal", std::to_string(entry_count)}, {"key", byte_value_json(key)}});
            return false;
        }
        previous_key = key;
        previous_index = index;

        const auto& raw_buffer = module.getRawEntryBuf();
        const auto raw = swbuf_bytes(raw_buffer);
        const auto read_error = module.popError();
        if (read_error != 0) {
            emit_diagnostic(
                writer, diagnostics, "error", "entry.read.failed",
                "SWORD reported an error while reading the logical entry.",
                {{"entry_ordinal", std::to_string(entry_count)},
                 {"key", byte_value_json(key)},
                 {"sword_error", std::to_string(static_cast<unsigned int>(
                     static_cast<unsigned char>(read_error)))}});
            return false;
        }
        const auto length = sword_length(raw.size(), writer, diagnostics, entry_count);

        std::string rendered;
        std::string stripped;
        std::string attributes = "[]";
        bool projections_available = false;
        if (length) {
            const auto rendered_buffer = module.renderText(raw.data(), *length, true);
            rendered = swbuf_bytes(rendered_buffer);
            attributes = official_attributes_json(module.getEntryAttributes());
            stripped = safe_c_string(module.stripText(raw.data(), *length));
            projections_available = true;
        }

        writer.emit("entry", {
            {"annotation_segments", annotation_segments_json(raw)},
            {"key", byte_value_json(key)},
            {"official_attributes", std::move(attributes)},
            {"ordinal", std::to_string(entry_count)},
            {"projections_available", projections_available ? "true" : "false"},
            {"raw", byte_value_json(raw)},
            {"rendered_default", projections_available ? byte_value_json(rendered) : "null"},
            {"scope", scope_json(module)},
            {"stripped", projections_available ? byte_value_json(stripped) : "null"}
        });
        ++entry_count;

        module.increment();
        const auto navigation_error = module.popError();
        if (navigation_error != 0) {
            if (navigation_error != KEYERR_OUTOFBOUNDS) {
                emit_diagnostic(
                    writer, diagnostics, "error", "module.navigation.failed",
                    "SWORD reported an unexpected error while advancing the module key.",
                    {{"entry_ordinal", std::to_string(entry_count)},
                     {"sword_error", std::to_string(static_cast<unsigned int>(
                         static_cast<unsigned char>(navigation_error)))}});
                return false;
            }
            break;
        }
    }
    return true;
}

std::filesystem::path canonical_root(
    const std::filesystem::path& input,
    NdjsonWriter& writer,
    DiagnosticCounts& diagnostics) {
    std::error_code error;
    const auto root = std::filesystem::canonical(input, error);
    if (error || !std::filesystem::is_directory(root, error)) {
        emit_diagnostic(
            writer, diagnostics, "error", "sword.root.invalid",
            "The explicit SWORD path is not a readable directory.",
            {{"detail", byte_value_json(error.message())}});
        return {};
    }
    return root;
}

} // namespace

bool list_modules(const std::filesystem::path& sword_path, NdjsonWriter& writer) {
    DiagnosticCounts diagnostics;
    emit_header(writer, "list");
    const auto root = canonical_root(sword_path, writer, diagnostics);
    if (root.empty()) {
        writer.finish(false, footer_fields(diagnostics, 0, 0, 0));
        return false;
    }

    try {
        sword::SWMgr manager(root.c_str(), true, nullptr, false, false);
        for (const auto& [name, module] : manager.getModules()) {
            static_cast<void>(name);
            writer.emit("module", module_fields(*module));
        }
    } catch (const std::exception& exception) {
        emit_diagnostic(
            writer, diagnostics, "error", "sword.discovery.exception", exception.what());
    }

    const bool success = diagnostics.error == 0U;
    writer.finish(success, footer_fields(diagnostics, 0, 0, 0));
    return success;
}

bool extract_module(const ExtractionOptions& options, NdjsonWriter& writer) {
    DiagnosticCounts diagnostics;
    std::uint64_t entries = 0;
    std::uint64_t artifacts = 0;
    std::uint64_t artifact_bytes = 0;
    emit_header(writer, "extract", options.artifact_chunk_size);

    const auto root = canonical_root(options.sword_path, writer, diagnostics);
    if (root.empty()) {
        writer.finish(false, footer_fields(diagnostics, entries, artifacts, artifact_bytes));
        return false;
    }

    try {
        sword::SWMgr manager(root.c_str(), true, nullptr, false, false);
        auto* module = manager.getModule(options.module_name.c_str());
        if (module == nullptr) {
            emit_diagnostic(
                writer, diagnostics, "error", "module.not_found",
                "The requested module was not found in the explicit SWORD root.",
                {{"module", byte_value_json(options.module_name)}});
            writer.finish(false, footer_fields(diagnostics, entries, artifacts, artifact_bytes));
            return false;
        }

        writer.emit("module", module_fields(*module));
        if (classify_module(*module) == "unknown") {
            emit_diagnostic(
                writer, diagnostics, "warning", "module.type.unknown",
                "The SWORD module type is unknown; generic traversal and full artifacts are retained.",
                {{"sword_type", byte_value_json(safe_c_string(module->getType()))}});
        }

        const auto config_files = find_configuration_files(
            root, options.module_name, writer, diagnostics);
        if (config_files.empty()) {
            emit_diagnostic(
                writer, diagnostics, "warning", "config.source.not_found",
                "No defining raw configuration file was matched; interpreted configuration is retained.");
        }
        emit_configuration_sources(root, config_files, writer, diagnostics);
        emit_interpreted_configuration(*module, writer);

        const bool entries_success = emit_entries(*module, writer, diagnostics, entries);

        std::vector<Diagnostic> discovery_diagnostics;
        auto artifact_data_path = safe_c_string(module->getConfigEntry("AbsoluteDataPath"));
        if (artifact_data_path.empty()) {
            artifact_data_path = safe_c_string(module->getConfigEntry("DataPath"));
        }
        const auto candidates = discover_artifacts(
            root, artifact_data_path, config_files, discovery_diagnostics);
        for (const auto& value : discovery_diagnostics) {
            emit_diagnostic(
                writer, diagnostics, value.severity, value.code, value.message, value.context);
        }
        const auto artifact_result = emit_artifacts(
            candidates, options.artifact_chunk_size, writer);
        artifacts = artifact_result.artifacts;
        artifact_bytes = artifact_result.bytes;
        for (const auto& value : artifact_result.diagnostics) {
            if (value.severity == "error") {
                ++diagnostics.error;
            } else if (value.severity == "warning") {
                ++diagnostics.warning;
            } else {
                ++diagnostics.info;
            }
        }

        if (!entries_success || !artifact_result.success) {
            // The detailed error records were already emitted at the point of detection.
        }
    } catch (const std::exception& exception) {
        emit_diagnostic(
            writer, diagnostics, "error", "extraction.exception", exception.what());
    }

    const bool success = diagnostics.error == 0U;
    writer.finish(success, footer_fields(diagnostics, entries, artifacts, artifact_bytes));
    return success;
}

} // namespace getbiblesword
