#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

if (($# != 2)); then
    echo "Usage: $0 GETBIBLESWORD C_API_DRIVER" >&2
    exit 2
fi

readonly binary=$1
readonly c_api_driver=$2
readonly temporary_directory="$(mktemp -d)"
readonly sword_root="$temporary_directory/sword"

cleanup() {
    rm -rf -- "$temporary_directory"
}
trap cleanup EXIT INT TERM

mkdir -p "$sword_root/mods.d"
"$binary" list --sword-path "$sword_root" > "$temporary_directory/cli.ndjson"
"$c_api_driver" list "$sword_root" > "$temporary_directory/c-api.ndjson"
cmp "$temporary_directory/cli.ndjson" "$temporary_directory/c-api.ndjson"

echo 'CLI and C ABI empty-root output are byte-identical'
