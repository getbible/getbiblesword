# libgetbiblesword C ABI v1

## Purpose

`libgetbiblesword.so.1` exposes the existing deterministic `list` and `extract`
operations to native consumers without starting the `getbiblesword` executable.
The ABI is intentionally C-only even though its implementation and the embedded
SWORD engine are C++.

The shared library and standalone CLI use the same extraction functions and emit
the same `getbiblesword.ndjson/v1` bytes. The C ABI does not define a second data
contract.

## Installed interface

The development interface consists of:

```text
include/getbiblesword/c_api.h
include/getbiblesword/export.h
lib/libgetbiblesword.so -> libgetbiblesword.so.1
lib/libgetbiblesword.so.1 -> libgetbiblesword.so.PRODUCT_VERSION
lib/pkgconfig/getbiblesword.pc
lib/cmake/getBibleSword/getBibleSwordConfig.cmake
```

Linux distributions may place the library and metadata below a multiarch
directory such as `lib/x86_64-linux-gnu`.

The ELF SONAME is `libgetbiblesword.so.1`. The product version and ABI version are
independent. Product `0.3.0` provides ABI version `1` and still emits NDJSON
contract v1.

## Building

Official builds use the pinned SWORD 1.9.0 static archive compiled as
position-independent code:

```sh
./scripts/build-sword.sh "$PWD/build/dependencies/sword"
PKG_CONFIG_PATH="$PWD/build/dependencies/sword/lib/pkgconfig" \
    cmake --preset release
PKG_CONFIG_PATH="$PWD/build/dependencies/sword/lib/pkgconfig" \
    cmake --build --preset release --parallel
ctest --preset release
```

`GETBIBLESWORD_SWORD_PROVIDER=BUNDLED` is the default. It requires an exact static
SWORD 1.9.0 archive and fails configuration rather than silently selecting an
ambient shared library.

Distribution maintainers can deliberately use a system SWORD 1.9.0 or newer:

```sh
cmake \
    -S . \
    -B build/system \
    -G Ninja \
    -DGETBIBLESWORD_SWORD_PROVIDER=SYSTEM
```

Official release artifacts never depend on `libsword.so`. The standalone
`getbiblesword` executable also never depends on `libgetbiblesword.so`; downstream
systems that extract only `/usr/bin/getbiblesword` remain supported.

## Complete C example

```c
#include <getbiblesword/c_api.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static gbs_write_result write_file(
    const uint8_t *data,
    size_t size,
    void *context
) {
    FILE *output = context;
    return fwrite(data, 1U, size, output) == size
        ? GBS_WRITE_CONTINUE
        : GBS_WRITE_ERROR;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SWORD_ROOT MODULE\n", argv[0]);
        return 2;
    }

    gbs_error error = GBS_ERROR_INITIALIZER;
    gbs_extract_options_v1 options = GBS_EXTRACT_OPTIONS_V1_INITIALIZER;
    options.sword_path = argv[1];
    options.module_name = argv[2];

    const gbs_status status = gbs_extract_module_v1(
        &options,
        write_file,
        stdout,
        &error
    );
    if (status != GBS_STATUS_OK) {
        fprintf(
            stderr,
            "getBibleSword: %s: %s\n",
            gbs_status_message(status),
            error.message
        );
        return status == GBS_STATUS_INVALID_ARGUMENT ? 2 : 1;
    }
    return 0;
}
```

Build through `pkg-config`:

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -Werror \
    consumer.c \
    $(pkg-config --cflags --libs getbiblesword) \
    -o consumer
```

Or through CMake:

```cmake
find_package(getBibleSword CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE getBibleSword::getBibleSword)
```

## Functions

### Metadata

- `gbs_abi_version()` returns `GBS_ABI_VERSION`.
- `gbs_product_version()` returns the immutable product version string.
- `gbs_contract_identifier()` returns `getbiblesword.ndjson/v1`.
- `gbs_status_message()` returns a static English description for a status code.

Returned metadata pointers have static lifetime. Consumers must not free or
modify them.

### Listing

`gbs_list_modules_v1()` takes an explicit SWORD module root and writes the same
NDJSON as:

```sh
getbiblesword list --sword-path ROOT
```

### Extraction

`gbs_extract_module_v1()` takes a `gbs_extract_options_v1` structure and writes the
same NDJSON as:

```sh
getbiblesword extract \
    --sword-path ROOT \
    --module NAME \
    --artifact-chunk-size BYTES
```

An artifact chunk size of zero selects the documented default of 1 MiB. Any
nonzero value must be from 4096 through 16777216 bytes.

`struct_size` must be at least `sizeof(gbs_extract_options_v1)`,
`abi_version` must equal `GBS_ABI_VERSION`, and every reserved field must be zero.
These rules allow future callers to pass an extended structure without letting
older libraries reinterpret unknown options.

## Callback contract

The callback:

- is called synchronously on the calling thread;
- receives a borrowed byte span valid only for that invocation;
- may be called with different chunk sizes;
- must consume the complete span before returning;
- returns `GBS_WRITE_CONTINUE`, `GBS_WRITE_CANCEL`, or `GBS_WRITE_ERROR`; and
- must not retain, modify, or free the supplied data; and
- must not call `gbs_list_modules_v1()` or `gbs_extract_module_v1()` recursively.

Cancellation returns `GBS_STATUS_CANCELLED`. A callback error, unknown callback
result, or exception thrown by a C++ callback returns
`GBS_STATUS_WRITE_FAILED`. No exception can cross an exported ABI function.

The callback chunks are transport chunks, not NDJSON record boundaries. Consumers
must reassemble or incrementally frame them by LF.

## Status and error contract

| Status | Meaning |
|---|---|
| `GBS_STATUS_OK` | A successful footer was emitted and the callback accepted all bytes |
| `GBS_STATUS_INVALID_ARGUMENT` | A pointer, structure, ABI version, reserved field, path, name, or size is invalid |
| `GBS_STATUS_EXTRACTION_FAILED` | A complete NDJSON stream with `success:false` was emitted |
| `GBS_STATUS_WRITE_FAILED` | The callback or output stream failed |
| `GBS_STATUS_CANCELLED` | The callback deliberately cancelled |
| `GBS_STATUS_INTERNAL_ERROR` | An unexpected contained C++ failure occurred |

`gbs_error` is optional. When supplied, callers must initialize `struct_size` to
`sizeof(gbs_error)`, normally through `GBS_ERROR_INITIALIZER`. An undersized error
structure is never written and causes `GBS_STATUS_INVALID_ARGUMENT`.

An extraction failure is not a truncated output by definition. When the extractor
can frame output, it preserves diagnostics and writes `success:false`. Callback
failure or cancellation can truncate output because the consumer has refused more
bytes.

## Validation remains required

An in-process call does not make module bytes trustworthy. A consumer must still
apply the complete rules in [contract v1](contract-v1.md), including sequence,
byte-envelope, artifact and footer-digest verification, before committing
downstream state.

A PHP extension should write callback data into a PHP stream or temporary file,
validate the completed stream, and only then expose higher-level records. It must
not concatenate an entire Bible into one PHP string.

## Concurrency

Independent calls from multiple threads are supported, tested repeatedly and
tested with concurrent extraction from the conformance corpus. SWORD access is
serialized inside ABI 1 because the embedded engine does not provide a stable
cross-driver thread-safety guarantee. The serialization applies within one
process; separate PHP-FPM workers remain independent.

A callback runs while its operation owns that serialization boundary and must not
re-enter a streaming C ABI function. Re-entry is rejected with
`GBS_STATUS_INVALID_ARGUMENT` before attempting to acquire the lock. Metadata
functions remain safe to call from a callback.

A caller must not modify, install, replace or remove module files while any call
is reading the same root.

Module installation and remote repository access are not part of ABI v1. They
will require an explicit policy, locking, atomic installation, repository
selection, disclaimer handling, TLS failure behavior, and separate tests before
being added. Reserved fields and new versioned functions allow that work without
changing these ABI v1 symbols.

## ABI governance

- Existing `gbs_*_v1` signatures and status meanings are immutable within SONAME
  1.
- New functions may be added to SONAME 1 when they do not change existing
  behavior.
- Incompatible layout or calling-convention changes require SONAME 2 and ABI
  version 2.
- Only the documented `gbs_*` allowlist is exported; C++, SWORD, and private
  helper symbols remain hidden.
- CI verifies the SONAME, exact exported-symbol allowlist, absence of
  `libsword.so` from bundled official outputs, standalone CLI independence, both
  bundled and system-provider builds, GCC and Clang builds, sanitizers, genuine C
  consumers, installed CMake/pkg-config consumption, cancellation, write
  failure, exception containment, repetition, concurrency and CLI parity.
