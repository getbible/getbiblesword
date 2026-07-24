// SPDX-License-Identifier: GPL-2.0-only

#include "getbiblesword/c_api.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct output_buffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
} output_buffer;

typedef struct parallel_extraction {
    gbs_extract_options_v1 options;
    output_buffer output;
    gbs_error error;
    gbs_status status;
} parallel_extraction;

static gbs_write_result write_stdout(
    const uint8_t* const data,
    const size_t size,
    void* const context) {
    (void)context;
    return fwrite(data, 1U, size, stdout) == size
        ? GBS_WRITE_CONTINUE
        : GBS_WRITE_ERROR;
}

static gbs_write_result collect_output(
    const uint8_t* const data,
    const size_t size,
    void* const context) {
    output_buffer* const output = (output_buffer*)context;
    if (size > SIZE_MAX - output->size) {
        return GBS_WRITE_ERROR;
    }
    const size_t required = output->size + size;
    if (required > output->capacity) {
        size_t capacity = output->capacity == 0U ? 65536U : output->capacity;
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
    return GBS_WRITE_CONTINUE;
}

static void* extract_in_thread(void* const context) {
    parallel_extraction* const extraction = (parallel_extraction*)context;
    extraction->status = gbs_extract_module_v1(
        &extraction->options,
        collect_output,
        &extraction->output,
        &extraction->error);
    return NULL;
}

static void usage(const char* const program) {
    fprintf(
        stderr,
        "Usage:\n"
        "  %s list SWORD_PATH\n"
        "  %s extract SWORD_PATH MODULE [ARTIFACT_CHUNK_SIZE]\n"
        "  %s parallel-extract SWORD_PATH MODULE ARTIFACT_CHUNK_SIZE THREADS\n",
        program,
        program,
        program);
}

static int status_to_exit_code(
    const gbs_status status,
    const gbs_error* const error) {
    if (status == GBS_STATUS_OK) {
        return EXIT_SUCCESS;
    }
    fprintf(
        stderr,
        "getbiblesword C ABI: %s: %s\n",
        gbs_status_message(status),
        error->message);
    return status == GBS_STATUS_INVALID_ARGUMENT ? 2 : EXIT_FAILURE;
}

static int parse_size(const char* const text, size_t* const result) {
    char* end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0'
        || value > (unsigned long long)SIZE_MAX) {
        return 0;
    }
    *result = (size_t)value;
    return 1;
}

static int run_parallel_extraction(
    const char* const sword_path,
    const char* const module_name,
    const char* const chunk_size_text,
    const char* const thread_count_text) {
    size_t chunk_size = 0U;
    size_t thread_count = 0U;
    if (!parse_size(chunk_size_text, &chunk_size)
        || !parse_size(thread_count_text, &thread_count)
        || thread_count < 2U || thread_count > 32U) {
        fputs("Invalid parallel extraction size or thread count.\n", stderr);
        return 2;
    }

    pthread_t* const threads = (pthread_t*)calloc(thread_count, sizeof(*threads));
    parallel_extraction* const extractions =
        (parallel_extraction*)calloc(thread_count, sizeof(*extractions));
    if (threads == NULL || extractions == NULL) {
        free(threads);
        free(extractions);
        fputs("Unable to allocate parallel extraction state.\n", stderr);
        return EXIT_FAILURE;
    }

    size_t created = 0U;
    int exit_code = EXIT_SUCCESS;
    for (size_t index = 0U; index < thread_count; ++index) {
        extractions[index].options =
            (gbs_extract_options_v1)GBS_EXTRACT_OPTIONS_V1_INITIALIZER;
        extractions[index].options.sword_path = sword_path;
        extractions[index].options.module_name = module_name;
        extractions[index].options.artifact_chunk_size = chunk_size;
        extractions[index].error = (gbs_error)GBS_ERROR_INITIALIZER;
        extractions[index].status = GBS_STATUS_INTERNAL_ERROR;
        if (pthread_create(
                &threads[index],
                NULL,
                extract_in_thread,
                &extractions[index]) != 0) {
            fputs("Unable to create extraction thread.\n", stderr);
            exit_code = EXIT_FAILURE;
            break;
        }
        ++created;
    }
    for (size_t index = 0U; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0) {
            fputs("Unable to join extraction thread.\n", stderr);
            exit_code = EXIT_FAILURE;
        }
    }

    if (created != thread_count) {
        exit_code = EXIT_FAILURE;
    }
    for (size_t index = 0U; index < created; ++index) {
        if (extractions[index].status != GBS_STATUS_OK) {
            fprintf(
                stderr,
                "Parallel extraction failed: %s: %s\n",
                gbs_status_message(extractions[index].status),
                extractions[index].error.message);
            exit_code = EXIT_FAILURE;
        }
        if (index != 0U
            && (extractions[index].output.size != extractions[0].output.size
                || (extractions[0].output.size != 0U
                    && memcmp(
                        extractions[index].output.data,
                        extractions[0].output.data,
                        extractions[0].output.size) != 0))) {
            fputs("Parallel extraction output differs between threads.\n", stderr);
            exit_code = EXIT_FAILURE;
        }
    }
    if (exit_code == EXIT_SUCCESS
        && extractions[0].output.size != 0U
        && fwrite(
               extractions[0].output.data,
               1U,
               extractions[0].output.size,
               stdout) != extractions[0].output.size) {
        fputs("Unable to write parallel extraction output.\n", stderr);
        exit_code = EXIT_FAILURE;
    }

    for (size_t index = 0U; index < thread_count; ++index) {
        free(extractions[index].output.data);
    }
    free(extractions);
    free(threads);
    return exit_code;
}

int main(const int argc, char** const argv) {
    gbs_error error = GBS_ERROR_INITIALIZER;

    if (argc == 3 && strcmp(argv[1], "list") == 0) {
        return status_to_exit_code(
            gbs_list_modules_v1(argv[2], write_stdout, NULL, &error),
            &error);
    }

    if ((argc == 4 || argc == 5) && strcmp(argv[1], "extract") == 0) {
        gbs_extract_options_v1 options = GBS_EXTRACT_OPTIONS_V1_INITIALIZER;
        options.sword_path = argv[2];
        options.module_name = argv[3];
        if (argc == 5) {
            if (!parse_size(argv[4], &options.artifact_chunk_size)) {
                fprintf(stderr, "Invalid artifact chunk size: %s\n", argv[4]);
                return 2;
            }
        }
        return status_to_exit_code(
            gbs_extract_module_v1(&options, write_stdout, NULL, &error),
            &error);
    }

    if (argc == 6 && strcmp(argv[1], "parallel-extract") == 0) {
        return run_parallel_extraction(argv[2], argv[3], argv[4], argv[5]);
    }

    usage(argv[0]);
    return 2;
}
