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
- [x] CrossWire-distributable [conformance corpus](conformance-corpus.md) covering every driver family.
- [x] Independent [v1 validator and round-trip artifact reassembler](validator-v1.md).
- [ ] Maintainer review of the v1 schema and classification policy.

The final maintainer review gates a stable `1.0.0`. The two automated integration
prerequisites are complete; use the [review packet](schema-review-v1.md) to decide
whether Builder integration may begin and whether any v1 changes are required.
