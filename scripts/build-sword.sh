#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

readonly SWORD_VERSION='1.9.0'

if (($# != 1)); then
    echo "Usage: $0 INSTALL_PREFIX" >&2
    exit 2
fi

readonly install_prefix=$1
readonly work_directory="$(mktemp -d)"

cleanup() {
    rm -rf -- "$work_directory"
}
trap cleanup EXIT INT TERM

"$(dirname "${BASH_SOURCE[0]}")/fetch-sword.sh" "$work_directory/sword.tar.gz"
tar \
    --extract \
    --gzip \
    --no-same-owner \
    --file "$work_directory/sword.tar.gz" \
    --directory "$work_directory"

pushd "$work_directory/sword-${SWORD_VERSION}" >/dev/null
./configure \
    --prefix="$install_prefix" \
    --disable-shared \
    --enable-static \
    --without-clucene
make --jobs="${JOBS:-$(nproc)}"
make install
popd >/dev/null

printf 'Installed official CrossWire SWORD %s in %s\n' "$SWORD_VERSION" "$install_prefix"
