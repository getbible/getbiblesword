# getBibleSword

[![CI](https://github.com/getbible/getbiblesword/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/getbible/getbiblesword/actions/workflows/ci.yml)
[![CodeQL](https://github.com/getbible/getbiblesword/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/getbible/getbiblesword/actions/workflows/codeql.yml)
[![Release](https://github.com/getbible/getbiblesword/actions/workflows/release.yml/badge.svg?branch=main)](https://github.com/getbible/getbiblesword/actions/workflows/release.yml)
[![Version](https://img.shields.io/github/v/release/getbible/getbiblesword?display_name=tag&sort=semver&label=version)](https://github.com/getbible/getbiblesword/releases/latest)
[![License](https://img.shields.io/badge/license-GPL--2.0--only-blue.svg)](LICENSE)

`getBibleSword` is a GPL-2.0-only C++ command-line extractor built directly on the
official CrossWire SWORD engine. The repository and executable are both named
`getbiblesword`.

It exports every SWORD module family through deterministic NDJSON: Biblical
texts, commentaries, dictionaries, lexicons, general books, daily devotionals,
maps, images and other generic resources. SWORD's official interpretation is
kept alongside the exact source bytes; rendered text never replaces source data.

## Status

The current `0.2.x` line is an engineering preview. Its all-driver conformance
suite, independent validator and byte-for-byte artifact round trip are complete.
The final maintainer review of the public contract and classification policy is
still required before the project is declared stable `1.0.0`.

The software release and output contract have separate versions:

| Item | Current value | Meaning |
|---|---|---|
| Product release | `0.2.0` | Version of the executable and release archive |
| NDJSON contract | `getbiblesword.ndjson/v1` | Compatibility identifier consumers must check |
| Contract version | `1` | Numeric value in each stream header |
| JSON Schema | `schema/v1/contract.schema.json` | Record-shape schema for contract v1 |

There is no contract or schema v2 yet. A future incompatible format would use a
new identifier and schema directory rather than silently changing v1.

## Download

Linux release archives are published for `x86_64` and `arm64`, with a separate
SHA-256 file for each archive. This example downloads and verifies the current
version for the host architecture:

```sh
version="$(curl -fsSL https://raw.githubusercontent.com/getbible/getbiblesword/main/VERSION)"
case "$(uname -m)" in
    x86_64) architecture=x86_64 ;;
    aarch64|arm64) architecture=arm64 ;;
    *) printf 'Unsupported architecture: %s\n' "$(uname -m)" >&2; exit 1 ;;
esac
archive="getbiblesword-${version}-linux-${architecture}.tar.gz"
base="https://github.com/getbible/getbiblesword/releases/download/v${version}"
curl -fLO "${base}/${archive}"
curl -fLO "${base}/${archive}.sha256"
sha256sum --check "${archive}.sha256"
tar -xzf "$archive"
```

The archive uses a `/usr` install prefix. Extract it into a staging directory or
install it with the package/deployment method appropriate for the target system.

## Build

Dependencies are a C++20 compiler, CMake 3.25+, Ninja, pkg-config and the official
CrossWire SWORD library 1.9.0 or newer.

```sh
cmake --preset dev
cmake --build --preset dev --parallel
ctest --preset dev
```

To build the pinned official SWORD release locally first:

```sh
./scripts/build-sword.sh "$PWD/.local/sword"
PKG_CONFIG_PATH="$PWD/.local/sword/lib/pkgconfig" cmake --preset release
PKG_CONFIG_PATH="$PWD/.local/sword/lib/pkgconfig" cmake --build --preset release --parallel
ctest --preset release
```

## Quick start

An explicit SWORD installation root is required. This prevents ambient host
module discovery from changing otherwise identical output.

```sh
getbiblesword list --sword-path /usr/share/sword
getbiblesword extract \
    --sword-path /usr/share/sword \
    --module KJV \
    --output kjv.ndjson
getbiblesword-v1 validate kjv.ndjson
```

Reconstruct the captured configuration and module files only when that is the
intended operation:

```sh
getbiblesword-v1 reassemble kjv.ndjson reconstructed-kjv
```

`extract` refuses to overwrite an existing output file unless `--force` is given.
NDJSON is written to standard output when `--output` is omitted. Operational
errors go to standard error; extraction warnings and failures also remain in the
stream as contract diagnostics.

## Downstream integration

Treat `getbiblesword` as a subprocess boundary:

1. run `list` or `extract` with an explicit SWORD root;
2. independently validate the completed stream;
3. require the v1 contract, consecutive sequence numbers, a valid footer digest
   and `success: true`;
4. transform validated records in a separate application-specific adapter; and
5. use decoded `base64` bytes as authoritative, not the optional `utf8`, rendered
   or stripped convenience views.

See the [downstream integration guide](docs/downstream-integration.md) for the
record map and Bash, Python, Node/TypeScript and PHP examples. AI tools and agents
can start with [llms.txt](llms.txt), [AGENTS.md](AGENTS.md) and the
[AI integration guide](docs/ai-integration.md), which includes MCP-style tool
descriptors without introducing an MCP server.

## Documentation

| Document | Purpose |
|---|---|
| [Documentation index](docs/README.md) | Reading paths for users, integrators, maintainers and agents |
| [Downstream integration](docs/downstream-integration.md) | Safe consumption patterns and language examples |
| [Contract v1](docs/contract-v1.md) | Normative NDJSON semantics, ordering and hashing |
| [JSON Schema](schema/v1/contract.schema.json) | Machine-readable record shapes |
| [Independent validator](docs/validator-v1.md) | Full-stream validation and safe artifact reconstruction |
| [Conformance corpus](docs/conformance-corpus.md) | Coverage of all concrete SWORD 1.9.0 drivers |
| [Architecture](docs/architecture.md) | Component and trust boundaries |
| [Threat model](docs/threat-model.md) | Security controls and residual risks |
| [Milestone 1](docs/milestone-1.md) | Acceptance criteria and remaining stable-v1 gate |

## Scope

This repository does not generate the GetBible API shape, Builder templates,
Study Builder repositories or an MCP service. Those are downstream consumers that
must work from validated NDJSON rather than private extractor internals.

## License

Copyright (C) 2026 Llewellyn van der Merwe and contributors.

Licensed under the GNU General Public License version 2 only. See [LICENSE](LICENSE).
