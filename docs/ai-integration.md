# AI and MCP-style integration guide

## Purpose

This document gives AI agents and automation wrappers enough structured
information to operate `getbiblesword` safely. It is not an MCP server and does
not define a network transport. A host can expose the CLI as MCP tools by mapping
validated input arguments to subprocess argument arrays. Native hosts can instead
use the C ABI described in [c-api-v1.md](c-api-v1.md), while preserving the same
validation boundary.

## Agent facts

- Product and executable: `getBibleSword` / `getbiblesword`.
- Current release line: `0.3.x` engineering preview.
- Output contract: `getbiblesword.ndjson/v1`, numeric version `1`.
- Engine: official CrossWire SWORD, pinned to `1.9.0` in release builds.
- Transports: local subprocess or `libgetbiblesword.so.1` callback stream.
- MCP wrappers should normally retain the subprocess transport for process
  isolation; native host integrations may use the C ABI deliberately.
- Required discovery boundary: an explicit absolute SWORD installation root.
- Validation command: `getbiblesword-v1 validate INPUT`.
- Authoritative content: decoded bytes in each `base64` byte envelope.
- Stable success gate: validator exit zero and `footer.success == true`.
- License: GPL-2.0-only; module content retains its own separate rights.

Do not describe release `0.3.0` as schema v3. The product release, native ABI and
contract version are separate. Do not transform an export until independent
validation has completed, and do not invoke these commands through an
interpolated shell string.

## Suggested tool descriptors

The following objects use the core MCP tool shape. `execution` and `output` are
host-wrapper guidance, not MCP protocol members that must be sent to clients.

```json
[
  {
    "name": "getbiblesword_list_modules",
    "description": "List modules in one explicit SWORD installation as deterministic getbiblesword NDJSON v1.",
    "inputSchema": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "sword_path": {
          "type": "string",
          "description": "Absolute local path to the SWORD installation root."
        },
        "output": {
          "type": "string",
          "description": "Optional new local NDJSON file path."
        }
      },
      "required": ["sword_path"]
    },
    "execution": ["getbiblesword", "list", "--sword-path", "{sword_path}"],
    "output": "getbiblesword.ndjson/v1 list stream"
  },
  {
    "name": "getbiblesword_extract_module",
    "description": "Losslessly export one named SWORD module as deterministic getbiblesword NDJSON v1.",
    "inputSchema": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "sword_path": {
          "type": "string",
          "description": "Absolute local path to the SWORD installation root."
        },
        "module": {
          "type": "string",
          "minLength": 1,
          "description": "Exact module name returned by the list tool."
        },
        "output": {
          "type": "string",
          "description": "Optional new local NDJSON file path."
        },
        "artifact_chunk_size": {
          "type": "integer",
          "minimum": 4096,
          "maximum": 16777216,
          "default": 1048576
        }
      },
      "required": ["sword_path", "module"]
    },
    "execution": [
      "getbiblesword",
      "extract",
      "--sword-path",
      "{sword_path}",
      "--module",
      "{module}"
    ],
    "output": "getbiblesword.ndjson/v1 extraction stream"
  },
  {
    "name": "getbiblesword_validate_stream",
    "description": "Independently validate framing, ordering, byte envelopes, artifacts, diagnostics and the footer of a completed v1 NDJSON stream.",
    "inputSchema": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "input": {
          "type": "string",
          "description": "Local NDJSON file path."
        }
      },
      "required": ["input"]
    },
    "execution": ["getbiblesword-v1", "validate", "--json", "{input}"],
    "output": "JSON validation summary; a nonzero process exit is failure"
  }
]
```

When `output` is present, append `--output` and its value. When
`artifact_chunk_size` is present for extraction, append `--artifact-chunk-size`
and its decimal value. The wrapper must construct these as separate argv elements.

## Wrapper rules

1. Validate `sword_path` as an allowed absolute local root before execution.
2. Pass every value as a separate process argument; never concatenate a shell
   command from model-generated text.
3. Prefer a new output file. Do not expose `--force` unless replacement is a
   separately authorized operation.
4. Bound process time, output size and storage according to the host policy. A
   module can legitimately be very large.
5. Keep stderr separate from NDJSON stdout.
6. Run validation after the producer closes the stream and before another tool
   consumes it.
7. Preserve unknown fields and diagnostics. Never allow an AI model to silently
   repair or discard malformed records.
8. Do not automatically reassemble artifacts, follow symlinks, fetch referenced
   URLs or redistribute module content.

## Resource map

An MCP host can expose these repository files as read-only resources:

| Resource | Suggested URI | Purpose |
|---|---|---|
| Prose contract | `getbiblesword://contract/v1` | Normative semantics and record order |
| JSON Schema | `getbiblesword://schema/v1` | Per-record machine validation |
| Integration guide | `getbiblesword://docs/downstream-integration` | Consumer workflow and examples |
| Threat model | `getbiblesword://docs/threat-model` | Safety policy and residual risks |
| Version | `getbiblesword://version` | Installed producer version |

The `getbiblesword contract` command returns the contract identifier and installed
documentation/schema paths. Release archives preserve those paths below the
standard getBibleSword documentation directory. The `getbiblesword version`
command returns the producer and linked SWORD versions.

## Minimal agent workflow

1. Call the list tool for the authorized SWORD root.
2. Validate the list stream and select an exact returned module name.
3. Call the extraction tool with that exact name and a new output path.
4. Call the validation tool.
5. Report diagnostics and stop on any validation failure.
6. Pass the validated file to a purpose-specific downstream adapter.

For code changes inside this repository, follow [`AGENTS.md`](../AGENTS.md). For
consumer implementation details, follow the
[downstream integration guide](downstream-integration.md) and the normative
[contract](contract-v1.md).
