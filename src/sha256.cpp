// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/sha256.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

namespace getbiblesword {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

constexpr std::uint32_t rotate_right(const std::uint32_t value, const unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::update(const std::span<const unsigned char> data) {
    if (data.empty()) {
        return;
    }

    total_bytes_ += static_cast<std::uint64_t>(data.size());
    std::size_t offset = 0;

    if (buffer_size_ != 0U) {
        const auto count = std::min(64U - buffer_size_, data.size());
        std::copy_n(data.data(), count, buffer_.data() + buffer_size_);
        buffer_size_ += count;
        offset += count;
        if (buffer_size_ == 64U) {
            transform(buffer_.data());
            buffer_size_ = 0;
        }
    }

    while (offset + 64U <= data.size()) {
        transform(data.data() + offset);
        offset += 64U;
    }

    if (offset < data.size()) {
        buffer_size_ = data.size() - offset;
        std::copy_n(data.data() + offset, buffer_size_, buffer_.data());
    }
}

void Sha256::update(const std::string_view data) {
    const auto* first = reinterpret_cast<const unsigned char*>(data.data());
    update(std::span<const unsigned char>(first, data.size()));
}

void Sha256::transform(const unsigned char* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16U; ++i) {
        const auto offset = i * 4U;
        words[i] = (static_cast<std::uint32_t>(block[offset]) << 24U)
            | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
            | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
            | static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t i = 16U; i < words.size(); ++i) {
        const auto s0 = rotate_right(words[i - 15U], 7U)
            ^ rotate_right(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
        const auto s1 = rotate_right(words[i - 2U], 17U)
            ^ rotate_right(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const auto choice = (e & f) ^ ((~e) & g);
        const auto temporary1 = h + sum1 + choice + round_constants[i] + words[i];
        const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::array<unsigned char, 32> Sha256::finalize() {
    const auto message_bits = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80U;

    if (buffer_size_ > 56U) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.end(), 0U);
        transform(buffer_.data());
        buffer_size_ = 0;
    }

    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.begin() + 56, 0U);
    for (std::size_t i = 0; i < 8U; ++i) {
        const auto shift = static_cast<unsigned int>((7U - i) * 8U);
        buffer_[56U + i] = static_cast<unsigned char>((message_bits >> shift) & 0xffU);
    }
    transform(buffer_.data());

    std::array<unsigned char, 32> result{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        result[i * 4U] = static_cast<unsigned char>((state_[i] >> 24U) & 0xffU);
        result[i * 4U + 1U] = static_cast<unsigned char>((state_[i] >> 16U) & 0xffU);
        result[i * 4U + 2U] = static_cast<unsigned char>((state_[i] >> 8U) & 0xffU);
        result[i * 4U + 3U] = static_cast<unsigned char>(state_[i] & 0xffU);
    }
    return result;
}

std::array<unsigned char, 32> Sha256::digest() const {
    auto copy = *this;
    return copy.finalize();
}

std::string Sha256::hex_digest() const {
    const auto bytes = digest();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

std::string sha256_hex(const std::span<const unsigned char> data) {
    Sha256 hash;
    hash.update(data);
    return hash.hex_digest();
}

std::string sha256_hex(const std::string_view data) {
    Sha256 hash;
    hash.update(data);
    return hash.hex_digest();
}

} // namespace getbiblesword
