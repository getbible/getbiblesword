// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/ndjson_writer.hpp"

#include "getbiblesword/byte_value.hpp"

#include <stdexcept>

namespace getbiblesword {

NdjsonWriter::NdjsonWriter(std::ostream& output) : output_(output) {}

void NdjsonWriter::emit(const std::string_view type, JsonFields fields) {
    if (finished_) {
        throw std::logic_error("cannot emit a record after the footer");
    }
    if (fields.contains("sequence") || fields.contains("type")) {
        throw std::invalid_argument("record fields may not replace sequence or type");
    }

    fields.emplace("sequence", std::to_string(sequence_));
    fields.emplace("type", json_string(type));
    const auto line = object_json(fields) + '\n';
    output_.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (!output_) {
        throw std::runtime_error("failed to write NDJSON output");
    }
    stream_hash_.update(line);
    ++counts_[std::string(type)];
    ++sequence_;
}

void NdjsonWriter::finish(const bool success, JsonFields fields) {
    if (finished_) {
        throw std::logic_error("NDJSON footer already emitted");
    }

    std::string counts = "{";
    bool first = true;
    for (const auto& [name, count] : counts_) {
        if (!first) {
            counts += ',';
        }
        first = false;
        counts += json_string(name) + ':' + std::to_string(count);
    }
    counts += '}';

    fields.emplace("counts", std::move(counts));
    fields.emplace("sequence", std::to_string(sequence_));
    fields.emplace("stream_sha256", json_string(stream_hash_.hex_digest()));
    fields.emplace("success", success ? "true" : "false");
    fields.emplace("type", "\"footer\"");

    const auto line = object_json(fields) + '\n';
    output_.write(line.data(), static_cast<std::streamsize>(line.size()));
    output_.flush();
    if (!output_) {
        throw std::runtime_error("failed to write NDJSON footer");
    }
    ++sequence_;
    finished_ = true;
}

std::uint64_t NdjsonWriter::next_sequence() const noexcept {
    return sequence_;
}

bool NdjsonWriter::finished() const noexcept {
    return finished_;
}

std::string NdjsonWriter::object_json(const JsonFields& fields) {
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

} // namespace getbiblesword
