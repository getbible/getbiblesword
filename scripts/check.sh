#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

readonly repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

git -C "$repository_root" diff --check

while IFS= read -r script; do
    bash -n "$script"
done < <(find "$repository_root/scripts" "$repository_root/tests" \
    -type f -name '*.sh' -print | sort)

if command -v jq >/dev/null 2>&1; then
    while IFS= read -r document; do
        jq empty "$document"
    done < <(find "$repository_root" \
        -path "$repository_root/build" -prune -o \
        -type f \( -name '*.json' -o -name '*.json.in' \) -print | sort)
fi

while IFS= read -r source; do
    if ! head -n 3 "$source" | grep -q 'SPDX-License-Identifier: GPL-2.0-only'; then
        echo "Missing GPL-2.0-only SPDX header: $source" >&2
        exit 1
    fi
done < <(find "$repository_root" \
    -path "$repository_root/build" -prune -o \
    -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.hpp.in' -o -name '*.cmake' -o -name '*.py' -o -name '*.sh' \) -print | sort)

echo 'Repository checks passed'
