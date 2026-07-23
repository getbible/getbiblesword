// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "getbiblesword/c_api.h"

#include <array>
#include <cstddef>
#include <streambuf>

namespace getbiblesword {

enum class CallbackFailure {
    none,
    cancelled,
    write_failed
};

class CallbackStreambuf final : public std::streambuf {
public:
    CallbackStreambuf(gbs_write_callback callback, void* context) noexcept;

    [[nodiscard]] CallbackFailure failure() const noexcept;

protected:
    int_type overflow(int_type character) override;
    int sync() override;

private:
    [[nodiscard]] bool flush_pending() noexcept;

    static constexpr std::size_t buffer_size = 64U * 1024U;
    std::array<char, buffer_size> buffer_{};
    gbs_write_callback callback_;
    void* context_;
    CallbackFailure failure_{CallbackFailure::none};
};

} // namespace getbiblesword
