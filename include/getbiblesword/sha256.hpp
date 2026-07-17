// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace getbiblesword {

class Sha256 final {
public:
    Sha256();

    void update(std::span<const unsigned char> data);
    void update(std::string_view data);

    [[nodiscard]] std::array<unsigned char, 32> digest() const;
    [[nodiscard]] std::string hex_digest() const;

private:
    void transform(const unsigned char* block);
    [[nodiscard]] std::array<unsigned char, 32> finalize();

    std::array<std::uint32_t, 8> state_{};
    std::array<unsigned char, 64> buffer_{};
    std::uint64_t total_bytes_{0};
    std::size_t buffer_size_{0};
};

[[nodiscard]] std::string sha256_hex(std::span<const unsigned char> data);
[[nodiscard]] std::string sha256_hex(std::string_view data);

} // namespace getbiblesword
