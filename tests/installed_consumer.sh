#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

if (($# != 1)); then
    echo "Usage: $0 STAGED_INSTALL_ROOT" >&2
    exit 2
fi

readonly stage_root=$1
readonly test_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly temporary_directory="$(mktemp -d)"
readonly pkg_config="${PKG_CONFIG:-pkg-config}"

cleanup() {
    rm -rf -- "$temporary_directory"
}
trap cleanup EXIT INT TERM

readonly library="$(
    find "$stage_root" -type f -name 'libgetbiblesword.so.*.*.*' -print -quit
)"
readonly pkg_config_file="$(
    find "$stage_root" -type f -path '*/pkgconfig/getbiblesword.pc' -print -quit
)"
readonly cmake_config="$(
    find "$stage_root" -type f -path '*/cmake/getBibleSword/getBibleSwordConfig.cmake' \
        -print -quit
)"

for required_file in "$library" "$pkg_config_file" "$cmake_config"; do
    if [[ -z "$required_file" || ! -f "$required_file" ]]; then
        echo 'The staged development package is incomplete.' >&2
        exit 1
    fi
done

readonly library_directory="$(dirname "$library")"
readonly pkg_config_directory="$(dirname "$pkg_config_file")"

cmake \
    -S "$test_root/consumer" \
    -B "$temporary_directory/cmake-build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$stage_root/usr"
cmake --build "$temporary_directory/cmake-build" --parallel
LD_LIBRARY_PATH="$library_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temporary_directory/cmake-build/getbiblesword_consumer"

IFS=' ' read -r -a pkg_config_flags <<< "$(
    PKG_CONFIG_LIBDIR="$pkg_config_directory" \
    PKG_CONFIG_SYSROOT_DIR="$stage_root" \
        "$pkg_config" --cflags --libs getbiblesword
)"
"${CC:-cc}" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    "$test_root/consumer/main.c" \
    "${pkg_config_flags[@]}" \
    -Wl,-rpath,"$library_directory" \
    -o "$temporary_directory/pkg-config-consumer"
LD_LIBRARY_PATH="$library_directory${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$temporary_directory/pkg-config-consumer"

echo 'Installed CMake and pkg-config consumers passed'
