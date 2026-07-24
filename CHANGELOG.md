# Changelog

All notable changes are documented here. The project follows Semantic Versioning.

## [Unreleased]

## [0.3.0] - 2026-07-23

- Add the versioned `libgetbiblesword.so.1` C ABI with callback-streamed module
  listing and extraction, contained C++ exceptions, explicit status codes and
  forward-compatible option structures.
- Keep the existing CLI independently linked and byte-compatible with the C ABI;
  neither official output depends on a system `libsword.so`, and the CLI does not
  depend on `libgetbiblesword.so`.
- Make the pinned PIC static SWORD 1.9.0 engine the default build provider while
  retaining an explicit system-library mode for distribution maintainers.
- Install the C header, SONAME links, `pkg-config` metadata and relocatable CMake
  package metadata for downstream native and PHP extension builds.
- Add genuine C, C++ exception-containment, repetition, cancellation, failure,
  installed-consumer, exported-symbol, dependency, concurrency and full
  conformance parity tests.
- Serialize in-process SWORD operations for deterministic NTS/ZTS behavior and
  reject callback re-entry without deadlocking.
- Package the shared library and development interface in both architecture
  archives and gate release creation on installed CMake and `pkg-config`
  consumers.
- Document the Zend/PIE package boundary and acceptance gates for the planned
  `getbible/sword` extension without claiming unimplemented direct-query or
  automatic module-installation behavior.
- Add concise CI, CodeQL, release, version and license badges.
- Add download instructions, a documentation index, downstream language examples,
  an AI orientation index and MCP-style subprocess tool descriptors.
- Preserve the documented `docs/` and `schema/` paths in installed release
  archives instead of flattening both directories, and fail release packaging if
  required binaries or integration documents are missing.
- Clarify that product release `0.2.0` emits NDJSON contract v1 rather than a v2
  schema, and correct the supported branch name in the security policy.

## [0.2.0] - 2026-07-22

- Add a public-domain conformance corpus for all 14 concrete SWORD 1.9.0 drivers,
  plus coverage of the `RawGBF` compatibility alias and every classification.
- Add an independent Python v1 stream validator with strict framing, ordering,
  byte-envelope, artifact and footer verification.
- Add an atomic artifact reassembler with normalized path checks and opt-in
  symlink creation.
- Gate CI and release builds on deterministic extraction, validation and
  byte-for-byte artifact reconstruction across the complete corpus.
- Add the stable-v1 schema and classification maintainer-review packet.

## [0.1.1] - 2026-07-18

- Use the authoritative `SWKey` index for deterministic generic-book scopes and
  traversal diagnostics.
- Preserve consecutive dictionary records that share a public key by bounding
  `SWLD` traversal with the driver's declared entry count.

## [0.1.0] - 2026-07-17

- Establish the repository, architecture, threat model and NDJSON v1 contract.
- Add the first lossless CrossWire SWORD extraction milestone.
- Add deterministic x86-64 and ARM64 Linux release automation.
