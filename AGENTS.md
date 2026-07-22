# getBibleSword repository guide

This file is the starting point for coding agents and MCP-backed project sessions.
Read it before changing the extractor or integrating it into another GetBible
project.

## Identity and license

- Product name: `getBibleSword`.
- Repository and executable name: `getbiblesword`.
- License: GPL-2.0-only. New source files must carry the matching SPDX identifier.
- Language: C++20.
- Engine: the official CrossWire SWORD engine, pinned to SWORD 1.9.0 for release
  builds.

## Current phase

Version `0.2.x` is an engineering preview of the extraction boundary. The full
driver conformance corpus and independent validator/reassembler are complete. Do
not treat it as the final Builder 3 or Study Builder data model until the remaining
maintainer review is recorded.

The remaining stable-1.0 gates are recorded in `docs/milestone-1.md`:

1. ~~a redistributable conformance corpus covering every SWORD driver family~~;
2. ~~an independent NDJSON validator and artifact reassembler~~;
3. maintainer review of the v1 schema and classification policy, using
   `docs/schema-review-v1.md`.

## Sources of truth

Read these in order when orienting a new project session:

1. `README.md` for the product, build and CLI overview;
2. `docs/README.md` for audience-specific documentation paths;
3. `docs/downstream-integration.md` for consumer rules and examples;
4. `docs/contract-v1.md` for the normative NDJSON semantics and record order;
5. `schema/v1/contract.schema.json` for machine validation;
6. `docs/architecture.md` for component boundaries and design decisions;
7. `docs/threat-model.md` for trust boundaries and resource hazards;
8. `docs/milestone-1.md` for phase gates and integration readiness.

Use `docs/schema-review-v1.md` for the final stable-v1 product decision and
`docs/conformance-corpus.md` for the physical-driver coverage matrix.
Use `llms.txt` and `docs/ai-integration.md` to orient AI consumers or design a
subprocess-to-MCP wrapper; neither file defines or implements an MCP server.

The prose contract is authoritative when a schema cannot express an ordering,
byte-preservation or hashing rule.

## Build and verification

Use the checked-in presets for normal development:

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
./scripts/check.sh
```

Build the pinned official SWORD engine when the host does not provide an acceptable
development package:

```sh
./scripts/build-sword.sh "$PWD/.local/sword"
PKG_CONFIG_PATH="$PWD/.local/sword/lib/pkgconfig" cmake --preset release
PKG_CONFIG_PATH="$PWD/.local/sword/lib/pkgconfig" cmake --build --preset release --parallel
ctest --preset release
```

Exercise the exact release packaging path with:

```sh
./scripts/build-release.sh "$(uname -m | sed 's/aarch64/arm64/')" "$(tr -d '\r\n' < VERSION)"
```

Do not weaken warnings, sanitizers, checksum verification or pinned dependency
hashes merely to make a build pass. Fix the underlying code or document a narrowly
scoped toolchain exception.

## CLI integration contract

Always pass an explicit SWORD installation root. Ambient host discovery would make
otherwise identical runs produce different results.

```sh
getbiblesword list --sword-path /absolute/sword/root
getbiblesword extract --sword-path /absolute/sword/root --module KJV --output module.ndjson
getbiblesword contract
getbiblesword version
```

Consumer requirements:

- process the NDJSON stream incrementally;
- require `getbiblesword.ndjson/v1` in the header;
- require monotonically increasing `sequence` values;
- verify the footer SHA-256 over every preceding serialized line including LF;
- treat decoded `base64` bytes as authoritative and `utf8` as a convenience view;
- preserve array order and repeated configuration keys;
- retain unknown module classifications, annotations and diagnostics;
- never substitute rendered or stripped text for `raw` entry bytes;
- reject or quarantine streams whose footer is missing, whose hash fails or whose
  `success` value is false.

Operational errors are written to standard error. Contract diagnostics remain in
the NDJSON stream so downstream systems cannot silently lose extraction failures.
An output path is never overwritten unless `--force` is supplied.

## Change rules

Any protocol change must remain deterministic across repeated runs over identical
bytes. Do not introduce timestamps, random identifiers, locale-dependent sorting,
host paths, ownership metadata or unordered serialization.

When adding support for a SWORD behavior:

1. retain the original bytes or engine value;
2. retain SWORD's official interpretation separately;
3. preserve unrecognized data explicitly;
4. emit a stable diagnostic when complete preservation is impossible;
5. add unit, integration and determinism coverage;
6. update the prose contract and schema together if the public contract changes.

Contract-breaking changes require a new contract identifier and schema directory.
They must not silently change `getbiblesword.ndjson/v1` semantics.

## Downstream Builder integration

Treat `getbiblesword` as a subprocess boundary. Builder 3 or Study Builder should
consume a validated NDJSON stream and transform it in a separate adapter layer.
They must not link directly to private extractor internals or reinterpret raw
SWORD module files independently.

Before enabling production integration, prove the following against the
conformance corpus:

- every required module family exports successfully;
- validation and footer hashing are independent of this executable;
- artifact chunks can be reassembled byte-for-byte;
- repeated runs produce byte-identical NDJSON;
- malformed, unknown and partially unreadable modules remain visible through
  records and diagnostics rather than disappearing.

## Versioning and releases

`VERSION` contains the semantic product version. A push to `main` always builds
retained x86-64 and ARM64 candidate artifacts. After both builds pass, the first
successful run for a version without a matching tag creates `vVERSION` and a
GitHub Release. Subsequent commits at the same version remain candidates and do
not rewrite the published release.

Before intentionally publishing the next version:

1. update `VERSION`;
2. update `CHANGELOG.md`;
3. run the normal checks and release packaging path;
4. merge to `main`.

The release contains both Linux architecture archives, their SHA-256 files, the
corresponding pinned SWORD source archive and its checksum. Release notes enumerate
all commit headlines since the previous version and link each one to its complete
GitHub commit message.
