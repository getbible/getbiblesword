// SPDX-License-Identifier: GPL-2.0-only

#include "callback_streambuf.hpp"

#include <cstdint>

namespace getbiblesword {

CallbackStreambuf::CallbackStreambuf(
    const gbs_write_callback callback,
    void* const context) noexcept
    : callback_(callback),
      context_(context) {
    setp(buffer_.data(), buffer_.data() + buffer_.size());
}

CallbackFailure CallbackStreambuf::failure() const noexcept {
    return failure_;
}

CallbackStreambuf::int_type CallbackStreambuf::overflow(const int_type character) {
    if (!flush_pending()) {
        return traits_type::eof();
    }
    if (!traits_type::eq_int_type(character, traits_type::eof())) {
        *pptr() = traits_type::to_char_type(character);
        pbump(1);
    }
    return traits_type::not_eof(character);
}

int CallbackStreambuf::sync() {
    return flush_pending() ? 0 : -1;
}

bool CallbackStreambuf::flush_pending() noexcept {
    if (failure_ != CallbackFailure::none) {
        return false;
    }

    const auto pending = pptr() - pbase();
    if (pending == 0) {
        return true;
    }

    gbs_write_result result = GBS_WRITE_ERROR;
    try {
        result = callback_(
            reinterpret_cast<const std::uint8_t*>(pbase()),
            static_cast<std::size_t>(pending),
            context_);
    } catch (...) {
        result = GBS_WRITE_ERROR;
    }

    if (result == GBS_WRITE_CONTINUE) {
        setp(buffer_.data(), buffer_.data() + buffer_.size());
        return true;
    }

    failure_ = result == GBS_WRITE_CANCEL
        ? CallbackFailure::cancelled
        : CallbackFailure::write_failed;
    return false;
}

} // namespace getbiblesword
