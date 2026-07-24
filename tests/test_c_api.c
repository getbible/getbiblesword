// SPDX-License-Identifier: GPL-2.0-only
#define _XOPEN_SOURCE 700

#include "getbiblesword/c_api.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct output_buffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
    size_t calls;
} output_buffer;

typedef struct thread_test {
    const char* sword_path;
    int successful;
} thread_test;

static int failures = 0;

static void check(const int condition, const char* const message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void reset_output(output_buffer* const output) {
    free(output->data);
    output->data = NULL;
    output->size = 0U;
    output->capacity = 0U;
    output->calls = 0U;
}

static gbs_write_result collect_output(
    const uint8_t* const data,
    const size_t size,
    void* const context) {
    output_buffer* const output = (output_buffer*)context;
    ++output->calls;

    if (size > SIZE_MAX - output->size - 1U) {
        return GBS_WRITE_ERROR;
    }
    const size_t required = output->size + size + 1U;
    if (required > output->capacity) {
        size_t capacity = output->capacity == 0U ? 4096U : output->capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        uint8_t* const resized = (uint8_t*)realloc(output->data, capacity);
        if (resized == NULL) {
            return GBS_WRITE_ERROR;
        }
        output->data = resized;
        output->capacity = capacity;
    }
    if (size != 0U) {
        memcpy(output->data + output->size, data, size);
    }
    output->size += size;
    output->data[output->size] = '\0';
    return GBS_WRITE_CONTINUE;
}

static gbs_write_result cancel_output(
    const uint8_t* const data,
    const size_t size,
    void* const context) {
    (void)data;
    (void)size;
    (void)context;
    return GBS_WRITE_CANCEL;
}

static gbs_write_result fail_output(
    const uint8_t* const data,
    const size_t size,
    void* const context) {
    (void)data;
    (void)size;
    (void)context;
    return GBS_WRITE_ERROR;
}

static int output_contains(
    const output_buffer* const output,
    const char* const needle) {
    return output->data != NULL
        && strstr((const char*)output->data, needle) != NULL;
}

static void* run_thread_test(void* const context) {
    thread_test* const test = (thread_test*)context;
    output_buffer output = {0};
    gbs_error error = GBS_ERROR_INITIALIZER;

    test->successful = 1;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const gbs_status status = gbs_list_modules_v1(
            test->sword_path,
            collect_output,
            &output,
            &error);
        if (status != GBS_STATUS_OK
            || !output_contains(&output, "\"success\":true")
            || error.status != GBS_STATUS_OK) {
            test->successful = 0;
            break;
        }
        reset_output(&output);
    }
    reset_output(&output);
    return NULL;
}

int main(void) {
    check(gbs_abi_version() == GBS_ABI_VERSION, "ABI version mismatch");
    check(gbs_product_version() != NULL, "product version is null");
    check(gbs_product_version()[0] != '\0', "product version is empty");
    check(
        strcmp(gbs_contract_identifier(), GBS_CONTRACT_IDENTIFIER) == 0,
        "contract identifier mismatch");
    check(
        strcmp(gbs_status_message(GBS_STATUS_CANCELLED), "cancelled") == 0,
        "known status message mismatch");
    check(
        strcmp(gbs_status_message(12345), "unknown status") == 0,
        "unknown status message mismatch");

    char root_template[] = "/tmp/getbiblesword-c-api-XXXXXX";
    char* const sword_path = mkdtemp(root_template);
    check(sword_path != NULL, "unable to create temporary SWORD root");
    if (sword_path == NULL) {
        return EXIT_FAILURE;
    }

    char mods_path[sizeof(root_template) + 16U];
    const int path_length = snprintf(
        mods_path,
        sizeof(mods_path),
        "%s/mods.d",
        sword_path);
    check(
        path_length > 0 && (size_t)path_length < sizeof(mods_path),
        "temporary mods.d path overflow");
    check(mkdir(mods_path, 0700) == 0, "unable to create temporary mods.d");

    output_buffer output = {0};
    gbs_error error = GBS_ERROR_INITIALIZER;

    check(
        gbs_list_modules_v1(NULL, collect_output, &output, &error)
            == GBS_STATUS_INVALID_ARGUMENT,
        "null SWORD path was accepted");
    check(
        error.status == GBS_STATUS_INVALID_ARGUMENT
            && strstr(error.message, "path") != NULL,
        "null path error was not populated");

    check(
        gbs_list_modules_v1(sword_path, NULL, NULL, &error)
            == GBS_STATUS_INVALID_ARGUMENT,
        "null callback was accepted");

    gbs_error undersized_error = {1U, GBS_STATUS_OK, {0}};
    check(
        gbs_list_modules_v1(
            sword_path,
            collect_output,
            &output,
            &undersized_error) == GBS_STATUS_INVALID_ARGUMENT,
        "undersized error storage was accepted");

    check(
        gbs_list_modules_v1(sword_path, collect_output, &output, &error)
            == GBS_STATUS_OK,
        "empty-root module listing failed");
    check(error.status == GBS_STATUS_OK, "success did not clear error status");
    check(error.message[0] == '\0', "success did not clear error message");
    check(output.calls > 0U, "successful listing did not invoke callback");
    check(
        output_contains(&output, "\"contract\":\"getbiblesword.ndjson/v1\""),
        "listing omitted contract header");
    check(
        output_contains(&output, "\"success\":true"),
        "listing omitted successful footer");
    reset_output(&output);

    check(
        gbs_list_modules_v1(sword_path, cancel_output, NULL, &error)
            == GBS_STATUS_CANCELLED,
        "callback cancellation was not preserved");
    check(
        error.status == GBS_STATUS_CANCELLED,
        "cancellation error status mismatch");
    check(
        gbs_list_modules_v1(sword_path, fail_output, NULL, &error)
            == GBS_STATUS_WRITE_FAILED,
        "callback write failure was not preserved");
    check(
        error.status == GBS_STATUS_WRITE_FAILED,
        "write error status mismatch");

    gbs_extract_options_v1 options = GBS_EXTRACT_OPTIONS_V1_INITIALIZER;
    check(
        gbs_extract_module_v1(NULL, collect_output, &output, &error)
            == GBS_STATUS_INVALID_ARGUMENT,
        "null extraction options were accepted");

    options.sword_path = sword_path;
    options.module_name = "MissingModule";
    options.abi_version = GBS_ABI_VERSION + 1U;
    check(
        gbs_extract_module_v1(&options, collect_output, &output, &error)
            == GBS_STATUS_INVALID_ARGUMENT,
        "unsupported ABI version was accepted");
    options.abi_version = GBS_ABI_VERSION;

    options.reserved[2] = 1U;
    check(
        gbs_extract_module_v1(&options, collect_output, &output, &error)
            == GBS_STATUS_INVALID_ARGUMENT,
        "nonzero reserved option was accepted");
    options.reserved[2] = 0U;

    options.artifact_chunk_size = GBS_MINIMUM_ARTIFACT_CHUNK_SIZE - 1U;
    check(
        gbs_extract_module_v1(&options, collect_output, &output, &error)
            == GBS_STATUS_INVALID_ARGUMENT,
        "undersized artifact chunk was accepted");
    options.artifact_chunk_size = 0U;

    check(
        gbs_extract_module_v1(&options, collect_output, &output, &error)
            == GBS_STATUS_EXTRACTION_FAILED,
        "missing module did not return extraction failure");
    check(
        error.status == GBS_STATUS_EXTRACTION_FAILED,
        "missing-module error status mismatch");
    check(
        output_contains(&output, "\"code\":\"module.not_found\""),
        "missing-module diagnostic was not streamed");
    check(
        output_contains(&output, "\"success\":false"),
        "missing-module footer was not streamed");
    reset_output(&output);

    enum { thread_count = 6 };
    pthread_t threads[thread_count];
    thread_test thread_tests[thread_count];
    int created_threads = 0;
    for (int index = 0; index < thread_count; ++index) {
        thread_tests[index].sword_path = sword_path;
        thread_tests[index].successful = 0;
        const int result = pthread_create(
            &threads[index],
            NULL,
            run_thread_test,
            &thread_tests[index]);
        check(result == 0, "unable to create concurrency test thread");
        if (result != 0) {
            break;
        }
        ++created_threads;
    }
    for (int index = 0; index < created_threads; ++index) {
        check(
            pthread_join(threads[index], NULL) == 0,
            "unable to join concurrency test thread");
        check(
            thread_tests[index].successful,
            "independent concurrent C ABI calls failed");
    }

    check(rmdir(mods_path) == 0, "unable to remove temporary mods.d");
    check(rmdir(sword_path) == 0, "unable to remove temporary SWORD root");
    reset_output(&output);

    if (failures != 0) {
        fprintf(stderr, "%d C ABI assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("C ABI tests passed");
    return EXIT_SUCCESS;
}
