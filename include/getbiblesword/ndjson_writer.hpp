// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "getbiblesword/sha256.hpp"

#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <string_view>

namespace getbiblesword {

using JsonFields = std::map<std::string, std::string, std::less<>>;

class NdjsonWriter final {
public:
    explicit NdjsonWriter(std::ostream& output);

    void emit(std::string_view type, JsonFields fields = {});
    void finish(bool success, JsonFields fields = {});

    [[nodiscard]] std::uint64_t next_sequence() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

private:
    [[nodiscard]] static std::string object_json(const JsonFields& fields);

    std::ostream& output_;
    Sha256 stream_hash_;
    std::uint64_t sequence_{0};
    std::map<std::string, std::uint64_t, std::less<>> counts_;
    bool finished_{false};
};

} // namespace getbiblesword
