// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/c_api.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

struct ReentrantContext {
    const char* sword_path;
    bool rejected;
};

extern "C" gbs_write_result throwing_callback(
    const std::uint8_t* data,
    const std::size_t size,
    void* context) {
    static_cast<void>(data);
    static_cast<void>(size);
    static_cast<void>(context);
    throw std::runtime_error("callback exception");
}

extern "C" gbs_write_result discard_callback(
    const std::uint8_t* data,
    const std::size_t size,
    void* context) {
    static_cast<void>(data);
    static_cast<void>(size);
    static_cast<void>(context);
    return GBS_WRITE_CONTINUE;
}

extern "C" gbs_write_result reentrant_callback(
    const std::uint8_t* data,
    const std::size_t size,
    void* context) {
    static_cast<void>(data);
    static_cast<void>(size);
    auto& reentrant = *static_cast<ReentrantContext*>(context);
    gbs_error nested_error = GBS_ERROR_INITIALIZER;
    const auto nested_status = gbs_list_modules_v1(
        reentrant.sword_path,
        discard_callback,
        nullptr,
        &nested_error);
    reentrant.rejected = nested_status == GBS_STATUS_INVALID_ARGUMENT
        && nested_error.status == GBS_STATUS_INVALID_ARGUMENT;
    return reentrant.rejected ? GBS_WRITE_CONTINUE : GBS_WRITE_ERROR;
}

int main() {
    const auto root = std::filesystem::temp_directory_path()
        / ("getbiblesword-c-api-exception-test-" + std::to_string(::getpid()));
    std::error_code error_code;
    std::filesystem::remove_all(root, error_code);
    if (error_code) {
        std::cerr << "Unable to prepare temporary root: " << error_code.message() << '\n';
        return 1;
    }
    if (!std::filesystem::create_directories(root / "mods.d", error_code) || error_code) {
        std::cerr << "Unable to create temporary root: " << error_code.message() << '\n';
        return 1;
    }

    gbs_error error = GBS_ERROR_INITIALIZER;
    gbs_status status = GBS_STATUS_INTERNAL_ERROR;
    try {
        status = gbs_list_modules_v1(
            root.c_str(),
            throwing_callback,
            nullptr,
            &error);
    } catch (...) {
        std::cerr << "A callback exception crossed the C ABI boundary.\n";
        std::filesystem::remove_all(root, error_code);
        return 1;
    }

    if (status != GBS_STATUS_WRITE_FAILED || error.status != GBS_STATUS_WRITE_FAILED) {
        std::cerr << "Callback exception did not become GBS_STATUS_WRITE_FAILED.\n";
        return 1;
    }

    ReentrantContext reentrant{root.c_str(), false};
    error = GBS_ERROR_INITIALIZER;
    status = gbs_list_modules_v1(
        root.c_str(),
        reentrant_callback,
        &reentrant,
        &error);
    if (status != GBS_STATUS_OK || error.status != GBS_STATUS_OK
        || !reentrant.rejected) {
        std::cerr << "A re-entrant streaming call was not rejected safely.\n";
        return 1;
    }

    std::filesystem::remove_all(root, error_code);
    if (error_code) {
        std::cerr << "Unable to remove temporary root: " << error_code.message() << '\n';
        return 1;
    }

    std::cout << "C ABI exception containment passed\n";
    return 0;
}
