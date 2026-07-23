// SPDX-License-Identifier: GPL-2.0-only
#ifndef GETBIBLESWORD_C_API_H
#define GETBIBLESWORD_C_API_H

#include "getbiblesword/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBS_ABI_VERSION 1U
#define GBS_CONTRACT_IDENTIFIER "getbiblesword.ndjson/v1"
#define GBS_DEFAULT_ARTIFACT_CHUNK_SIZE (1024U * 1024U)
#define GBS_MINIMUM_ARTIFACT_CHUNK_SIZE 4096U
#define GBS_MAXIMUM_ARTIFACT_CHUNK_SIZE (16U * 1024U * 1024U)
#define GBS_ERROR_MESSAGE_CAPACITY 512U

typedef int32_t gbs_status;

enum {
    GBS_STATUS_OK = 0,
    GBS_STATUS_INVALID_ARGUMENT = 1,
    GBS_STATUS_EXTRACTION_FAILED = 2,
    GBS_STATUS_WRITE_FAILED = 3,
    GBS_STATUS_CANCELLED = 4,
    GBS_STATUS_INTERNAL_ERROR = 255
};

typedef int32_t gbs_write_result;

enum {
    GBS_WRITE_CONTINUE = 0,
    GBS_WRITE_CANCEL = 1,
    GBS_WRITE_ERROR = 2
};

typedef gbs_write_result (*gbs_write_callback)(
    const uint8_t* data,
    size_t size,
    void* context);

typedef struct gbs_error {
    uint32_t struct_size;
    gbs_status status;
    char message[GBS_ERROR_MESSAGE_CAPACITY];
} gbs_error;

#define GBS_ERROR_INITIALIZER \
    { (uint32_t)sizeof(gbs_error), GBS_STATUS_OK, { 0 } }

typedef struct gbs_extract_options_v1 {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* sword_path;
    const char* module_name;
    size_t artifact_chunk_size;
    uint64_t reserved[4];
} gbs_extract_options_v1;

#define GBS_EXTRACT_OPTIONS_V1_INITIALIZER \
    { \
        (uint32_t)sizeof(gbs_extract_options_v1), \
        GBS_ABI_VERSION, \
        NULL, \
        NULL, \
        GBS_DEFAULT_ARTIFACT_CHUNK_SIZE, \
        { 0U, 0U, 0U, 0U } \
    }

GBS_API uint32_t gbs_abi_version(void);
GBS_API const char* gbs_product_version(void);
GBS_API const char* gbs_contract_identifier(void);
GBS_API const char* gbs_status_message(gbs_status status);

GBS_API gbs_status gbs_list_modules_v1(
    const char* sword_path,
    gbs_write_callback callback,
    void* context,
    gbs_error* error);

GBS_API gbs_status gbs_extract_module_v1(
    const gbs_extract_options_v1* options,
    gbs_write_callback callback,
    void* context,
    gbs_error* error);

#ifdef __cplusplus
}
#endif

#endif
