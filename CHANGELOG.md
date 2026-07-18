# Changelog

All notable changes are documented here. The project follows Semantic Versioning.

## [Unreleased]

## [0.1.1] - 2026-07-18

- Use the authoritative `SWKey` index for deterministic generic-book scopes and
  traversal diagnostics.
- Preserve consecutive dictionary records that share a public key by bounding
  `SWLD` traversal with the driver's declared entry count.

## [0.1.0] - 2026-07-17

- Establish the repository, architecture, threat model and NDJSON v1 contract.
- Add the first lossless CrossWire SWORD extraction milestone.
- Add deterministic x86-64 and ARM64 Linux release automation.
