// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/c_api.h"

#include "callback_streambuf.hpp"
#include "getbiblesword/ndjson_writer.hpp"
#include "getbiblesword/sword_extractor.hpp"
#include "getbiblesword/version.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include <ostream>
#include <string_view>

namespace {

constexpr std::string_view contract_identifier = GBS_CONTRACT_IDENTIFIER;
std::mutex sword_operation_mutex;
thread_local bool sword_operation_active = false;

class ActiveOperation final {
public:
    ActiveOperation() noexcept {
        sword_operation_active = true;
    }

    ~ActiveOperation() {
        sword_operation_active = false;
    }

    ActiveOperation(const ActiveOperation&) = delete;
    ActiveOperation& operator=(const ActiveOperation&) = delete;
};

bool error_storage_is_valid(const gbs_error* const error) noexcept {
    return error == nullptr || error->struct_size >= sizeof(gbs_error);
}

void set_error(
    gbs_error* const error,
    const gbs_status status,
    const std::string_view message) noexcept {
    if (error == nullptr || error->struct_size < sizeof(gbs_error)) {
        return;
    }

    error->status = status;
    const auto length = std::min(
        message.size(),
        static_cast<std::size_t>(GBS_ERROR_MESSAGE_CAPACITY - 1U));
    if (length != 0U) {
        std::memcpy(error->message, message.data(), length);
    }
    error->message[length] = '\0';
}

gbs_status callback_failure_status(
    const getbiblesword::CallbackStreambuf& buffer,
    gbs_error* const error) noexcept {
    switch (buffer.failure()) {
    case getbiblesword::CallbackFailure::cancelled:
        set_error(error, GBS_STATUS_CANCELLED, "The output callback cancelled the operation.");
        return GBS_STATUS_CANCELLED;
    case getbiblesword::CallbackFailure::write_failed:
        set_error(error, GBS_STATUS_WRITE_FAILED, "The output callback failed.");
        return GBS_STATUS_WRITE_FAILED;
    case getbiblesword::CallbackFailure::none:
        return GBS_STATUS_OK;
    }
    set_error(error, GBS_STATUS_INTERNAL_ERROR, "Unknown callback state.");
    return GBS_STATUS_INTERNAL_ERROR;
}

template<typename Operation>
gbs_status run_streaming_operation(
    const gbs_write_callback callback,
    void* const context,
    gbs_error* const error,
    Operation&& operation) noexcept {
    if (!error_storage_is_valid(error)) {
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    set_error(error, GBS_STATUS_OK, {});
    if (callback == nullptr) {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "The output callback is required.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (sword_operation_active) {
        set_error(
            error,
            GBS_STATUS_INVALID_ARGUMENT,
            "A streaming C ABI call cannot be re-entered from its callback.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }

    getbiblesword::CallbackStreambuf buffer(callback, context);
    try {
        const std::scoped_lock operation_lock(sword_operation_mutex);
        const ActiveOperation active_operation;
        std::ostream output(&buffer);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        getbiblesword::NdjsonWriter writer(output);
        const bool success = operation(writer);
        output.flush();

        const auto callback_status = callback_failure_status(buffer, error);
        if (callback_status != GBS_STATUS_OK) {
            return callback_status;
        }
        if (!output) {
            set_error(error, GBS_STATUS_WRITE_FAILED, "The NDJSON output stream failed.");
            return GBS_STATUS_WRITE_FAILED;
        }
        if (!success) {
            set_error(
                error,
                GBS_STATUS_EXTRACTION_FAILED,
                "The operation completed with a failed NDJSON footer.");
            return GBS_STATUS_EXTRACTION_FAILED;
        }
        return GBS_STATUS_OK;
    } catch (const std::exception& exception) {
        const auto callback_status = callback_failure_status(buffer, error);
        if (callback_status != GBS_STATUS_OK) {
            return callback_status;
        }
        set_error(error, GBS_STATUS_INTERNAL_ERROR, exception.what());
        return GBS_STATUS_INTERNAL_ERROR;
    } catch (...) {
        const auto callback_status = callback_failure_status(buffer, error);
        if (callback_status != GBS_STATUS_OK) {
            return callback_status;
        }
        set_error(error, GBS_STATUS_INTERNAL_ERROR, "Unknown C++ exception.");
        return GBS_STATUS_INTERNAL_ERROR;
    }
}

bool reserved_fields_are_zero(const gbs_extract_options_v1& options) noexcept {
    return std::all_of(
        std::begin(options.reserved),
        std::end(options.reserved),
        [](const std::uint64_t value) { return value == 0U; });
}

} // namespace

extern "C" {

uint32_t gbs_abi_version(void) {
    return GBS_ABI_VERSION;
}

const char* gbs_product_version(void) {
    return GETBIBLESWORD_VERSION;
}

const char* gbs_contract_identifier(void) {
    return contract_identifier.data();
}

const char* gbs_status_message(const gbs_status status) {
    switch (status) {
    case GBS_STATUS_OK:
        return "success";
    case GBS_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case GBS_STATUS_EXTRACTION_FAILED:
        return "extraction failed";
    case GBS_STATUS_WRITE_FAILED:
        return "write failed";
    case GBS_STATUS_CANCELLED:
        return "cancelled";
    case GBS_STATUS_INTERNAL_ERROR:
        return "internal error";
    default:
        return "unknown status";
    }
}

gbs_status gbs_list_modules_v1(
    const char* const sword_path,
    const gbs_write_callback callback,
    void* const context,
    gbs_error* const error) {
    if (!error_storage_is_valid(error)) {
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (sword_path == nullptr || sword_path[0] == '\0') {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "The SWORD module path is required.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }

    return run_streaming_operation(
        callback,
        context,
        error,
        [sword_path](getbiblesword::NdjsonWriter& writer) {
            return getbiblesword::list_modules(std::filesystem::path(sword_path), writer);
        });
}

gbs_status gbs_extract_module_v1(
    const gbs_extract_options_v1* const options,
    const gbs_write_callback callback,
    void* const context,
    gbs_error* const error) {
    if (!error_storage_is_valid(error)) {
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (options == nullptr) {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "The extraction options are required.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (options->struct_size < sizeof(gbs_extract_options_v1)) {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "The extraction options are too small.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (options->abi_version != GBS_ABI_VERSION) {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "Unsupported C ABI version.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (!reserved_fields_are_zero(*options)) {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "Reserved extraction options must be zero.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (options->sword_path == nullptr || options->sword_path[0] == '\0') {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "The SWORD module path is required.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }
    if (options->module_name == nullptr || options->module_name[0] == '\0') {
        set_error(error, GBS_STATUS_INVALID_ARGUMENT, "The module name is required.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }

    const std::size_t chunk_size = options->artifact_chunk_size == 0U
        ? GBS_DEFAULT_ARTIFACT_CHUNK_SIZE
        : options->artifact_chunk_size;
    if (chunk_size < GBS_MINIMUM_ARTIFACT_CHUNK_SIZE
        || chunk_size > GBS_MAXIMUM_ARTIFACT_CHUNK_SIZE) {
        set_error(
            error,
            GBS_STATUS_INVALID_ARGUMENT,
            "The artifact chunk size must be from 4096 through 16777216 bytes.");
        return GBS_STATUS_INVALID_ARGUMENT;
    }

    try {
        const auto sword_path = std::filesystem::path(options->sword_path);
        const auto module_name = std::string(options->module_name);
        return run_streaming_operation(
            callback,
            context,
            error,
            [&sword_path, &module_name, chunk_size](
                getbiblesword::NdjsonWriter& writer) {
                return getbiblesword::extract_module(
                    {sword_path, module_name, chunk_size},
                    writer);
            });
    } catch (const std::exception& exception) {
        set_error(error, GBS_STATUS_INTERNAL_ERROR, exception.what());
        return GBS_STATUS_INTERNAL_ERROR;
    } catch (...) {
        set_error(error, GBS_STATUS_INTERNAL_ERROR, "Unknown C++ exception.");
        return GBS_STATUS_INTERNAL_ERROR;
    }
}

} // extern "C"
