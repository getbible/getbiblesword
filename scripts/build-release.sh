#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

if (($# != 2)); then
    echo "Usage: $0 ARCHITECTURE VERSION" >&2
    exit 2
fi

readonly architecture=$1
readonly version=$2
readonly repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly dependency_prefix="$repository_root/build/dependencies/sword"
readonly build_directory="$repository_root/build/release-${architecture}"
readonly stage_directory="$repository_root/build/stage-${architecture}"
readonly dist_directory="$repository_root/dist"
readonly archive_name="getbiblesword-${version}-linux-${architecture}.tar.gz"
readonly source_date_epoch="${SOURCE_DATE_EPOCH:-$(git -C "$repository_root" show -s --format=%ct HEAD)}"
readonly project_version="$(tr -d '\r\n' < "$repository_root/VERSION")"

if [[ "$version" != "$project_version" ]]; then
    printf 'Release version %s does not match VERSION (%s).\n' "$version" "$project_version" >&2
    exit 2
fi

case "$architecture:$(uname -m)" in
    x86_64:x86_64|arm64:aarch64) ;;
    *)
        printf 'Architecture label %s does not match build host %s.\n' \
            "$architecture" "$(uname -m)" >&2
        exit 2
        ;;
esac

rm -rf -- "$build_directory" "$stage_directory"
mkdir -p -- "$dist_directory"

"$repository_root/scripts/build-sword.sh" "$dependency_prefix"

PKG_CONFIG_PATH="$dependency_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
    cmake \
        -S "$repository_root" \
        -B "$build_directory" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=ON \
        -DGETBIBLESWORD_SWORD_PROVIDER=BUNDLED \
        -DGETBIBLESWORD_ENABLE_CONFORMANCE_TESTS=ON
cmake --build "$build_directory" --parallel
ctest --test-dir "$build_directory" --output-on-failure
DESTDIR="$stage_directory" cmake --install "$build_directory" --strip

readonly installed_documentation="$stage_directory/usr/share/doc/getBibleSword"
readonly shared_library="$(
    find "$stage_directory/usr" \
        -type f \
        -name "libgetbiblesword.so.$version" \
        -print \
        -quit
)"
readonly pkg_config_file="$(
    find "$stage_directory/usr" \
        -type f \
        -path '*/pkgconfig/getbiblesword.pc' \
        -print \
        -quit
)"
readonly cmake_config_file="$(
    find "$stage_directory/usr" \
        -type f \
        -path '*/cmake/getBibleSword/getBibleSwordConfig.cmake' \
        -print \
        -quit
)"
readonly required_installed_files=(
    "$stage_directory/usr/bin/getbiblesword"
    "$stage_directory/usr/bin/getbiblesword-v1"
    "$stage_directory/usr/include/getbiblesword/c_api.h"
    "$stage_directory/usr/include/getbiblesword/export.h"
    "$shared_library"
    "$pkg_config_file"
    "$cmake_config_file"
    "$installed_documentation/AGENTS.md"
    "$installed_documentation/README.md"
    "$installed_documentation/llms.txt"
    "$installed_documentation/docs/c-api-v1.md"
    "$installed_documentation/docs/contract-v1.md"
    "$installed_documentation/docs/downstream-integration.md"
    "$installed_documentation/schema/v1/contract.schema.json"
)
for installed_file in "${required_installed_files[@]}"; do
    if [[ ! -f "$installed_file" ]]; then
        printf 'Required release file is missing: %s\n' "$installed_file" >&2
        exit 1
    fi
done

"$repository_root/scripts/check-abi.sh" \
    "$shared_library" \
    "$stage_directory/usr/bin/getbiblesword" \
    BUNDLED
"$repository_root/tests/installed_consumer.sh" "$stage_directory"

tar \
    --create \
    --gzip \
    --sort=name \
    --mtime="@$source_date_epoch" \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    --file "$dist_directory/$archive_name" \
    --directory "$stage_directory" \
    .

(
    cd "$dist_directory"
    sha256sum "$archive_name" > "$archive_name.sha256"
)
printf 'Created %s\n' "$dist_directory/$archive_name"
