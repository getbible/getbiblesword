// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace getbiblesword {

enum class AnnotationSegmentKind {
    text,
    markup,
    entity
};

struct AnnotationSegment final {
    AnnotationSegmentKind kind;
    std::string raw;
};

[[nodiscard]] std::vector<AnnotationSegment> segment_annotations(std::string_view raw);
[[nodiscard]] std::string annotation_segments_json(std::string_view raw);

} // namespace getbiblesword
