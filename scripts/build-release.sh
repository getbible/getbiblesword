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
        -DGETBIBLESWORD_ENABLE_CONFORMANCE_TESTS=ON
cmake --build "$build_directory" --parallel
ctest --test-dir "$build_directory" --output-on-failure
DESTDIR="$stage_directory" cmake --install "$build_directory" --strip

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
