# Documentation

Use the shortest reading path that matches the task.

## Running getBibleSword

1. Start with the repository [README](../README.md) for downloads, builds and the
   CLI quick start.
2. Follow the [downstream integration guide](downstream-integration.md) before
   consuming an export in another project.
3. Use the [validator and reassembler guide](validator-v1.md) to verify a stream or
   recreate its captured source files.

## Implementing a consumer

1. Choose the standalone process boundary or the
   [libgetbiblesword C ABI](c-api-v1.md).
2. Read the normative [contract v1](contract-v1.md).
3. Use the [JSON Schema](../schema/v1/contract.schema.json) for individual record
   shapes.
4. Use `getbiblesword-v1 validate` for ordering, framing, byte-envelope and footer
   rules that JSON Schema cannot express.
5. Review [architecture](architecture.md) and the
   [lossless dual-view decision](adr/0001-lossless-dual-view.md) before designing a
   downstream data model.

For the planned Zend extension and PIE package, also read the
[native PHP extension roadmap](php-extension-roadmap.md). It distinguishes the
implemented streaming ABI from future direct-reference and module-installation
operations.

## Reviewing stability and security

- [Milestone 1](milestone-1.md) records completed and outstanding acceptance gates.
- [Schema review v1](schema-review-v1.md) contains the final maintainer decision
  packet for a stable public contract.
- [Conformance corpus](conformance-corpus.md) describes coverage of every concrete
  SWORD 1.9.0 storage driver.
- [Threat model](threat-model.md) defines untrusted inputs, controls and residual
  risks.
- The root [security policy](../SECURITY.md) explains private vulnerability
  reporting.

## AI and automation

- [AI integration](ai-integration.md) provides compact operating rules and
  MCP-style tool descriptors for subprocess wrappers.
- [`llms.txt`](../llms.txt) is a small machine-readable orientation index.
- [`AGENTS.md`](../AGENTS.md) is the full repository guide for coding agents and
  project sessions.

## Version authority

`VERSION` identifies the software release. The first `header` record identifies
the output contract. The ELF SONAME identifies the native ABI. These values are
independent: release `0.3.0` provides `libgetbiblesword.so.1` and emits
`getbiblesword.ndjson/v1` with `contract_version: 1`.

Breaking contract changes require a new identifier and schema directory. Additive
v1 fields remain possible, so consumers must ignore unknown fields while retaining
the records and source bytes they do not interpret.
