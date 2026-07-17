// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/annotation.hpp"

#include "getbiblesword/byte_value.hpp"

#include <cstddef>

namespace getbiblesword {
namespace {

std::size_t markup_end(const std::string_view raw, const std::size_t begin) {
    char quote = '\0';
    for (std::size_t index = begin + 1U; index < raw.size(); ++index) {
        const auto value = raw[index];
        if (quote != '\0') {
            if (value == quote) {
                quote = '\0';
            }
        } else if (value == '\'' || value == '"') {
            quote = value;
        } else if (value == '>') {
            return index + 1U;
        }
    }
    return begin + 1U;
}

std::size_t entity_end(const std::string_view raw, const std::size_t begin) {
    for (std::size_t index = begin + 1U; index < raw.size(); ++index) {
        const auto value = raw[index];
        if (value == ';') {
            return index + 1U;
        }
        if (value == '<' || value == '&' || value == ' ' || value == '\t'
            || value == '\r' || value == '\n') {
            break;
        }
    }
    return begin + 1U;
}

std::string_view kind_name(const AnnotationSegmentKind kind) {
    switch (kind) {
    case AnnotationSegmentKind::text: return "text";
    case AnnotationSegmentKind::markup: return "markup";
    case AnnotationSegmentKind::entity: return "entity";
    }
    return "text";
}

} // namespace

std::vector<AnnotationSegment> segment_annotations(const std::string_view raw) {
    std::vector<AnnotationSegment> segments;
    std::size_t index = 0;
    while (index < raw.size()) {
        if (raw[index] == '<') {
            const auto end = markup_end(raw, index);
            segments.push_back({AnnotationSegmentKind::markup, std::string(raw.substr(index, end - index))});
            index = end;
            continue;
        }
        if (raw[index] == '&') {
            const auto end = entity_end(raw, index);
            if (end > index + 1U) {
                segments.push_back({AnnotationSegmentKind::entity, std::string(raw.substr(index, end - index))});
                index = end;
                continue;
            }
        }

        const auto begin = index;
        ++index;
        while (index < raw.size() && raw[index] != '<' && raw[index] != '&') {
            ++index;
        }
        segments.push_back({AnnotationSegmentKind::text, std::string(raw.substr(begin, index - begin))});
    }
    return segments;
}

std::string annotation_segments_json(const std::string_view raw) {
    const auto segments = segment_annotations(raw);
    std::string output = "[";
    bool first = true;
    for (const auto& segment : segments) {
        if (!first) {
            output += ',';
        }
        first = false;
        const auto interpretation = segment.kind == AnnotationSegmentKind::text
            ? "not_applicable" : "uninterpreted";
        output += "{\"interpretation\":" + json_string(interpretation)
            + ",\"kind\":" + json_string(kind_name(segment.kind))
            + ",\"raw\":" + byte_value_json(segment.raw) + '}';
    }
    output += ']';
    return output;
}

} // namespace getbiblesword
