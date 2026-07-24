#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only

set -euo pipefail
IFS=$'\n\t'

if (($# != 9)); then
    echo "Usage: $0 GETBIBLESWORD IMP2LD IMP2GBS IMP2VS OSIS2MOD CORPUS_WRITER PYTHON VALIDATOR C_API_DRIVER" >&2
    exit 2
fi

readonly binary=$1
readonly imp2ld=$2
readonly imp2gbs=$3
readonly imp2vs=$4
readonly osis2mod=$5
readonly corpus_writer=$6
readonly python=$7
readonly validator=$8
readonly c_api_driver=$9
readonly test_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly fixture_root="$test_root/fixtures"
readonly manifest="$test_root/corpus/manifest.json"
readonly temporary_directory="$(mktemp -d)"
readonly sword_root="$temporary_directory/sword"

cleanup() {
    rm -rf -- "$temporary_directory"
}
trap cleanup EXIT INT TERM

mkdir -p \
    "$sword_root/mods.d" \
    "$sword_root/modules/texts/rawtext/corpusrawtext" \
    "$sword_root/modules/texts/rawtext4/corpusrawtext4" \
    "$sword_root/modules/texts/ztext/corpusztext" \
    "$sword_root/modules/texts/ztext4/corpusztext4" \
    "$sword_root/modules/comments/rawcom/corpusrawcom" \
    "$sword_root/modules/comments/rawcom4/corpusrawcom4" \
    "$sword_root/modules/comments/zcom/corpuszcom" \
    "$sword_root/modules/comments/zcom4/corpuszcom4" \
    "$sword_root/modules/comments/rawfiles/corpusrawfiles" \
    "$sword_root/modules/comments/hrefcom/corpushrefcom" \
    "$sword_root/modules/lexdict/rawld/corpusrawld" \
    "$sword_root/modules/lexdict/rawld4/corpusrawld4" \
    "$sword_root/modules/lexdict/zld/corpuszld" \
    "$sword_root/modules/genbook/rawgenbook/corpusrawgenbook/media"

"$imp2vs" "$fixture_root/verse.imp" \
    -o "$sword_root/modules/texts/rawtext/corpusrawtext" >/dev/null
"$imp2vs" "$fixture_root/verse.imp" -4 \
    -o "$sword_root/modules/texts/rawtext4/corpusrawtext4" >/dev/null
"$imp2vs" "$fixture_root/verse.imp" -z z -b 4 \
    -o "$sword_root/modules/texts/ztext/corpusztext" >/dev/null
"$osis2mod" \
    "$sword_root/modules/texts/ztext4/corpusztext4" \
    "$fixture_root/verse.osis.xml" \
    -z z -b 4 -s 4 -N >/dev/null

cp "$sword_root/modules/texts/rawtext/corpusrawtext/"* \
    "$sword_root/modules/comments/rawcom/corpusrawcom/"
cp "$sword_root/modules/texts/rawtext4/corpusrawtext4/"* \
    "$sword_root/modules/comments/rawcom4/corpusrawcom4/"
cp "$sword_root/modules/texts/ztext/corpusztext/"* \
    "$sword_root/modules/comments/zcom/corpuszcom/"
cp "$sword_root/modules/texts/ztext4/corpusztext4/"* \
    "$sword_root/modules/comments/zcom4/corpuszcom4/"
cp "$sword_root/modules/texts/rawtext/corpusrawtext/"* \
    "$sword_root/modules/comments/hrefcom/corpushrefcom/"

"$corpus_writer" "$sword_root/modules/comments/rawfiles/corpusrawfiles"
"$imp2ld" "$fixture_root/dictionary.imp" \
    -o "$sword_root/modules/lexdict/rawld/corpusrawld/corpusrawld" >/dev/null
"$imp2ld" "$fixture_root/dictionary.imp" -4 \
    -o "$sword_root/modules/lexdict/rawld4/corpusrawld4/corpusrawld4" >/dev/null
"$imp2ld" "$fixture_root/dictionary.imp" -z z \
    -o "$sword_root/modules/lexdict/zld/corpuszld/corpuszld" >/dev/null
"$imp2gbs" "$fixture_root/general-book.imp" \
    -o "$sword_root/modules/genbook/rawgenbook/corpusrawgenbook/book" >/dev/null

cp "$fixture_root/modules.conf" "$sword_root/mods.d/conformance.conf"
cp "$fixture_root/plate.txt" \
    "$sword_root/modules/genbook/rawgenbook/corpusrawgenbook/media/plate.txt"

"$binary" list --sword-path "$sword_root" > "$temporary_directory/list-1.ndjson"
"$binary" list --sword-path "$sword_root" > "$temporary_directory/list-2.ndjson"
"$c_api_driver" list "$sword_root" > "$temporary_directory/list-c-api.ndjson"
cmp "$temporary_directory/list-1.ndjson" "$temporary_directory/list-2.ndjson"
cmp "$temporary_directory/list-1.ndjson" "$temporary_directory/list-c-api.ndjson"
"$python" "$validator" validate "$temporary_directory/list-1.ndjson" >/dev/null

manifest_rows() {
    "$python" - "$manifest" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
for item in manifest["drivers"] + manifest["classification_variants"]:
    print("\t".join((item["module"], item["driver"], item["classification"])))
PY
}

assert_module() {
    local module=$1
    local driver=$2
    local classification=$3
    grep -F "\"classification\":\"$classification\"" "$temporary_directory/list-1.ndjson" \
        | grep -F "\"utf8\":\"$module\"" \
        | grep -F "\"utf8\":\"$driver\"" >/dev/null
}

while IFS=$'\t' read -r module driver classification; do
    assert_module "$module" "$driver" "$classification"
done < <(manifest_rows)

extract_validate_reassemble() {
    local module=$1
    local first="$temporary_directory/$module-1.ndjson"
    local second="$temporary_directory/$module-2.ndjson"
    local c_api="$temporary_directory/$module-c-api.ndjson"
    local reassembled="$temporary_directory/$module-reassembled"

    "$binary" extract --sword-path "$sword_root" --module "$module" \
        --artifact-chunk-size 4096 > "$first"
    "$binary" extract --sword-path "$sword_root" --module "$module" \
        --artifact-chunk-size 4096 > "$second"
    "$c_api_driver" extract "$sword_root" "$module" 4096 > "$c_api"
    cmp "$first" "$second"
    cmp "$first" "$c_api"
    "$python" "$validator" validate "$first" >/dev/null
    "$python" "$validator" reassemble "$first" "$reassembled" >/dev/null

    local file_count=0
    while IFS= read -r -d '' reassembled_file; do
        local relative=${reassembled_file#"$reassembled/"}
        cmp "$reassembled_file" "$sword_root/$relative"
        ((file_count += 1))
    done < <(find "$reassembled" -type f -print0 | sort -z)
    if ((file_count == 0)); then
        echo "No regular artifacts were reassembled for $module." >&2
        exit 1
    fi
}

while IFS=$'\t' read -r module _driver _classification; do
    extract_validate_reassemble "$module"
done < <(manifest_rows)

"$c_api_driver" parallel-extract "$sword_root" CorpusRawLD 4096 4 \
    > "$temporary_directory/parallel.ndjson"
cmp \
    "$temporary_directory/CorpusRawLD-1.ndjson" \
    "$temporary_directory/parallel.ndjson"

grep -F 'CustomExperimentalFeature' "$temporary_directory/CorpusRawLD-1.ndjson" >/dev/null
grep -F '"type":"config_source"' "$temporary_directory/CorpusRawLD-1.ndjson" >/dev/null
if [[ $(grep -c '"key":{[^}]*"utf8":"KADESH"' \
    "$temporary_directory/CorpusRawLD-1.ndjson") -ne 2 ]]; then
    echo 'Duplicate dictionary keys were not preserved as separate entries.' >&2
    exit 1
fi
grep -F '"type":"artifact_begin"' "$temporary_directory/CorpusResource-1.ndjson" \
    | grep -F 'media/plate.txt' >/dev/null

readonly tampered="$temporary_directory/tampered.ndjson"
sed '0,/CorpusRawLD/s//CorpusRawLX/' \
    "$temporary_directory/CorpusRawLD-1.ndjson" > "$tampered"
if "$python" "$validator" validate "$tampered" >/dev/null 2>&1; then
    echo 'The independent validator accepted a tampered stream.' >&2
    exit 1
fi

readonly atomic_output="$temporary_directory/atomic.ndjson"
"$binary" extract --sword-path "$sword_root" --module CorpusRawLD \
    --artifact-chunk-size 4096 --output "$atomic_output"
if "$binary" extract --sword-path "$sword_root" --module CorpusRawLD \
    --artifact-chunk-size 4096 --output "$atomic_output" 2>/dev/null; then
    echo 'Existing output was overwritten without --force.' >&2
    exit 1
fi
"$binary" extract --sword-path "$sword_root" --module CorpusRawLD \
    --artifact-chunk-size 4096 --output "$atomic_output" --force
cmp "$temporary_directory/CorpusRawLD-1.ndjson" "$atomic_output"

echo 'SWORD driver conformance and artifact round-trip tests passed'
