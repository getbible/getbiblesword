#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

readonly SWORD_VERSION='1.9.0'
readonly SWORD_SHA256='42409cf3de2faf1108523e2c5ac0745d21f9ed2a5c78ed878ee9dcc303426b8a'
readonly SWORD_URL="https://crosswire.org/ftpmirror/pub/sword/source/v1.9/sword-${SWORD_VERSION}.tar.gz"

if (($# != 1)); then
    echo "Usage: $0 OUTPUT_FILE" >&2
    exit 2
fi

readonly output_file=$1
readonly output_directory="$(cd "$(dirname "$output_file")" && pwd)"
readonly temporary_file="$(mktemp "$output_directory/.sword-source.XXXXXX")"

cleanup() {
    rm -f -- "$temporary_file"
}
trap cleanup EXIT INT TERM

curl \
    --fail \
    --location \
    --proto '=https' \
    --retry 3 \
    --show-error \
    --silent \
    --tlsv1.2 \
    --output "$temporary_file" \
    "$SWORD_URL"

printf '%s  %s\n' "$SWORD_SHA256" "$temporary_file" | sha256sum --check --strict
mv -- "$temporary_file" "$output_file"
trap - EXIT INT TERM
