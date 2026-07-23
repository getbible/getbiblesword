#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

if (($# != 3)); then
    echo "Usage: $0 LIBGETBIBLESWORD GETBIBLESWORD SWORD_PROVIDER" >&2
    exit 2
fi

readonly library=$1
readonly binary=$2
readonly sword_provider=$3
readonly library_directory="$(dirname "$library")"
readonly soname_link="$library_directory/libgetbiblesword.so.1"
readonly linker_link="$library_directory/libgetbiblesword.so"

if [[ ! -f "$library" || ! -r "$library" ]]; then
    printf 'Required shared library is missing or unreadable: %s\n' "$library" >&2
    exit 1
fi
if [[ ! -f "$binary" || ! -x "$binary" ]]; then
    printf 'Required executable is missing or not executable: %s\n' "$binary" >&2
    exit 1
fi

for link in "$soname_link" "$linker_link"; do
    if [[ ! -L "$link" || ! -e "$link" ]]; then
        printf 'Required shared-library link is missing or broken: %s\n' "$link" >&2
        exit 1
    fi
done
if [[ "$(readlink -f "$soname_link")" != "$(readlink -f "$library")" ]]; then
    echo 'The libgetbiblesword.so.1 link does not resolve to the tested library.' >&2
    exit 1
fi
if [[ "$(readlink -f "$linker_link")" != "$(readlink -f "$library")" ]]; then
    echo 'The libgetbiblesword.so linker link does not resolve to the tested library.' >&2
    exit 1
fi

if ! readelf --dynamic "$library" | grep -F 'Library soname: [libgetbiblesword.so.1]' >/dev/null; then
    echo 'libgetbiblesword does not declare SONAME libgetbiblesword.so.1.' >&2
    exit 1
fi

if readelf --dynamic "$binary" \
    | grep -E 'Shared library: \[libgetbiblesword\.so' >/dev/null; then
    echo 'The standalone CLI unexpectedly depends on libgetbiblesword.so.' >&2
    exit 1
fi
case "$sword_provider" in
    BUNDLED)
        if readelf --dynamic "$library" \
            | grep -E 'Shared library: \[libsword\.so' >/dev/null; then
            echo 'Bundled libgetbiblesword unexpectedly depends on libsword.so.' >&2
            exit 1
        fi
        if readelf --dynamic "$binary" \
            | grep -E 'Shared library: \[libsword\.so' >/dev/null; then
            echo 'The bundled standalone CLI unexpectedly depends on libsword.so.' >&2
            exit 1
        fi
        ;;
    SYSTEM) ;;
    *)
        printf 'Unknown SWORD provider: %s\n' "$sword_provider" >&2
        exit 2
        ;;
esac

readonly expected_symbols="$(
    printf '%s\n' \
        gbs_abi_version \
        gbs_contract_identifier \
        gbs_extract_module_v1 \
        gbs_list_modules_v1 \
        gbs_product_version \
        gbs_status_message \
        | sort
)"
readonly actual_symbols="$(
    readelf --dyn-syms --wide "$library" \
        | awk '$4 == "FUNC" && $5 == "GLOBAL" && $7 != "UND" {
            split($8, symbol, "@");
            print symbol[1]
        }' \
        | sort -u
)"

if [[ "$actual_symbols" != "$expected_symbols" ]]; then
    echo 'The exported C ABI symbol set differs from the allowlist.' >&2
    diff \
        <(printf '%s\n' "$expected_symbols") \
        <(printf '%s\n' "$actual_symbols") \
        >&2 || true
    exit 1
fi

echo 'C ABI SONAME, dependencies and exported symbols are valid'
