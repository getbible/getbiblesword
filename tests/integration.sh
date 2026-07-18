#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

if (($# != 4)); then
    echo "Usage: $0 GETBIBLESWORD IMP2LD IMP2GBS IMP2VS" >&2
    exit 2
fi

readonly binary=$1
readonly imp2ld=$2
readonly imp2gbs=$3
readonly imp2vs=$4
readonly test_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly fixture_root="$test_root/fixtures"
readonly temporary_directory="$(mktemp -d)"
readonly sword_root="$temporary_directory/sword"

cleanup() {
    rm -rf -- "$temporary_directory"
}
trap cleanup EXIT INT TERM

mkdir -p \
    "$sword_root/mods.d" \
    "$sword_root/modules/comments/rawcom/testcommentary" \
    "$sword_root/modules/genbook/rawgenbook/testbook/media" \
    "$sword_root/modules/lexdict/rawld/testdict" \
    "$sword_root/modules/texts/rawtext/testbible"

"$imp2ld" "$fixture_root/dictionary.imp" \
    -o "$sword_root/modules/lexdict/rawld/testdict/testdict" >/dev/null
"$imp2gbs" "$fixture_root/general-book.imp" \
    -o "$sword_root/modules/genbook/rawgenbook/testbook/book" >/dev/null
"$imp2vs" "$fixture_root/verse.imp" \
    -o "$sword_root/modules/texts/rawtext/testbible" >/dev/null

cp "$fixture_root/modules.conf" "$sword_root/mods.d/synthetic.conf"
cp "$fixture_root/plate.txt" \
    "$sword_root/modules/genbook/rawgenbook/testbook/media/plate.txt"
cp "$sword_root/modules/texts/rawtext/testbible/"* \
    "$sword_root/modules/comments/rawcom/testcommentary/"

"$binary" list --sword-path "$sword_root" > "$temporary_directory/list-1.ndjson"
"$binary" list --sword-path "$sword_root" > "$temporary_directory/list-2.ndjson"
cmp "$temporary_directory/list-1.ndjson" "$temporary_directory/list-2.ndjson"

assert_classification() {
    local classification=$1
    local module=$2
    grep -F "\"classification\":\"$classification\"" "$temporary_directory/list-1.ndjson" \
        | grep -F "\"utf8\":\"$module\"" >/dev/null
}

assert_classification bible TestBible
assert_classification commentary TestCommentary
assert_classification devotional TestDevotional
assert_classification dictionary_or_lexicon TestDictionary
assert_classification general_book TestGeneralBook
assert_classification resource TestResource

extract_twice() {
    local module=$1
    local first="$temporary_directory/$module-1.ndjson"
    local second="$temporary_directory/$module-2.ndjson"
    "$binary" extract --sword-path "$sword_root" --module "$module" \
        --artifact-chunk-size 4096 > "$first"
    "$binary" extract --sword-path "$sword_root" --module "$module" \
        --artifact-chunk-size 4096 > "$second"
    cmp "$first" "$second"
    tail -n 1 "$first" | grep -F '"success":true' >/dev/null
}

extract_twice TestDictionary
extract_twice TestDevotional
extract_twice TestGeneralBook
extract_twice TestResource

grep -F 'x-unknown' "$temporary_directory/TestDictionary-1.ndjson" >/dev/null
grep -F '"type":"config_source"' "$temporary_directory/TestDictionary-1.ndjson" >/dev/null
if [[ $(grep -c '"key":{[^}]*"utf8":"KADESH"' \
    "$temporary_directory/TestDictionary-1.ndjson") -ne 2 ]]; then
    echo 'Duplicate dictionary keys were not preserved as separate entries.' >&2
    exit 1
fi
grep -F '"type":"artifact_begin"' "$temporary_directory/TestResource-1.ndjson" \
    | grep -F 'media/plate.txt' >/dev/null

# Verse-key traversal includes module, testament, book and chapter introductions as
# CrossWire's mod2imp does. The full streams are intentionally not retained here.
"$binary" extract --sword-path "$sword_root" --module TestBible \
    --artifact-chunk-size 4096 >/dev/null
"$binary" extract --sword-path "$sword_root" --module TestCommentary \
    --artifact-chunk-size 4096 >/dev/null

readonly atomic_output="$temporary_directory/atomic.ndjson"
"$binary" extract --sword-path "$sword_root" --module TestDictionary \
    --artifact-chunk-size 4096 --output "$atomic_output"
if "$binary" extract --sword-path "$sword_root" --module TestDictionary \
    --artifact-chunk-size 4096 --output "$atomic_output" 2>/dev/null; then
    echo 'Existing output was overwritten without --force.' >&2
    exit 1
fi
"$binary" extract --sword-path "$sword_root" --module TestDictionary \
    --artifact-chunk-size 4096 --output "$atomic_output" --force
cmp "$temporary_directory/TestDictionary-1.ndjson" "$atomic_output"

echo 'SWORD integration tests passed'
