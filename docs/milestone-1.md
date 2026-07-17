# Milestone 1: extraction contract foundation

## Goal

Produce a reviewable, executable v1 boundary before Builder 3 or Study Builder gains
any dependency on the engine.

## Acceptance criteria

- [x] GPL-2.0-only repository named `getbiblesword`; product name `getBibleSword`.
- [x] C++20 CLI linked to official CrossWire SWORD 1.9.0 or newer.
- [x] Versioned deterministic NDJSON framing with SHA-256 footer.
- [x] Generic module discovery and traversal for every SWORD module family.
- [x] Raw bytes, render, strip, official attributes and raw annotation segments.
- [x] Complete raw and interpreted configuration preservation.
- [x] Chunked module data and media envelope without following symlinks.
- [x] Structured diagnostics and nonzero failure exits.
- [x] Unit, deterministic contract, sanitizer and static-analysis CI.
- [x] Reproducible release scripts for Linux x86-64 and ARM64.
- [ ] CrossWire-distributable conformance corpus covering every driver family.
- [ ] Independent v1 validator and round-trip artifact reassembler.
- [ ] Maintainer review of the v1 schema and classification policy.

The last three items gate a stable `1.0.0`; they do not block the `0.1.0` engineering
preview. No Builder integration starts until the conformance corpus and validator
are complete.
