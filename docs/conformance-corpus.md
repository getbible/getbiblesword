# CrossWire driver conformance corpus

## Purpose

The conformance corpus is a small, public-domain SWORD installation generated from
checked-in source fixtures with CrossWire's own module-making utilities. It answers
a different question from the existing classification tests:

> Can getBibleSword discover, traverse, export, validate and reconstruct the files
> produced for every concrete driver that SWORD 1.9.0 can instantiate?

“Conformance” means known inputs and verifiable outputs. It is not a performance
benchmark. The corpus is distributable because its original text, configuration
and media fixtures are dedicated to the public domain; it does not copy a third
party Bible or dictionary module.

## Coverage

SWORD module type is the logical interface; `ModDrv` selects the physical storage
implementation. Testing one type does not exercise every driver that implements
it. The manifest therefore covers all 14 concrete drivers accepted by
`SWMgr::createModule()` in the pinned SWORD release:

| Logical family | Drivers |
|---|---|
| Biblical text | `RawText`, `RawText4`, `zText`, `zText4` |
| Commentary | `RawCom`, `RawCom4`, `zCom`, `zCom4`, `HREFCom` |
| Dictionary or lexicon | `RawLD`, `RawLD4`, `zLD` |
| General book or resource | `RawGenBook`, `RawFiles` |

`RawGBF` is recorded as a compatibility alias rather than a fifteenth storage
driver: SWORD creates it through the `RawText` implementation. The same corpus also
proves all getBibleSword classifications: `bible`, `commentary`,
`dictionary_or_lexicon`, `general_book`, `devotional` and `resource`.

The machine-readable source of truth is
[`tests/corpus/manifest.json`](../tests/corpus/manifest.json).

## Construction and assertions

The integration test builds modules with pinned SWORD 1.9.0 utilities. Where two
drivers have format-compatible storage but a tool cannot name the target driver,
the test generates the bytes with the matching official utility and exposes them
under the target `ModDrv`. `RawFiles` is written through SWORD's C++ API.

For every manifest entry, the test:

1. discovers the module and verifies its driver and classification;
2. extracts it twice and requires byte-identical NDJSON;
3. validates both streams with the independent v1 validator;
4. reconstructs every artifact into an empty directory;
5. compares each reconstructed regular file byte-for-byte with its source; and
6. exercises digest, byte-envelope and output-atomicity failure cases.

## Running it

The complete suite is intentionally opt-in because the uncompressed driver
fixtures produce large artifact streams:

```sh
PKG_CONFIG_PATH=/path/to/sword/lib/pkgconfig cmake \
  -S . -B build/conformance -G Ninja \
  -DGETBIBLESWORD_ENABLE_CONFORMANCE_TESTS=ON
cmake --build build/conformance --parallel
ctest --test-dir build/conformance --output-on-failure
```

CI has a dedicated conformance job. Release builds also enable the suite, making
all-driver compatibility and artifact reversibility release gates rather than
occasional developer checks.
