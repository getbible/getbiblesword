# getBibleSword

`getBibleSword` is a GPL-2.0-only C++ command-line extractor built directly on the
official CrossWire SWORD engine. The repository and executable are both named
`getbiblesword`.

The project exports every SWORD module family through a deterministic, versioned
NDJSON contract: Biblical texts, commentaries, dictionaries, lexicons, general
books, daily devotionals, maps, images and other generic resources. The contract
keeps SWORD's official interpretation alongside exact source bytes. It never uses
rendered text as a replacement for source data.

## Milestone 1

The first milestone establishes the extraction boundary before any Builder 3 or
Study Builder integration:

- deterministic `getbiblesword.ndjson/v1` framing and stream digest;
- complete interpreted configuration maps, including repeated keys;
- raw configuration files and module data/media as chunked SHA-256 artifacts;
- generic official SWORD iteration, including verse introductions;
- raw entry bytes, default render, stripped text and official entry attributes;
- lossless raw annotation tokenization and structured diagnostics;
- x86-64 and ARM64 Linux release automation using pinned SWORD 1.9.0 source;
- unit, contract, sanitizer and static-analysis workflows.

See [the protocol](docs/contract-v1.md), [architecture](docs/architecture.md),
[threat model](docs/threat-model.md) and [milestone acceptance criteria](docs/milestone-1.md).

## Build

Dependencies are a C++20 compiler, CMake 3.25+, Ninja, pkg-config and the official
CrossWire SWORD library 1.9.0 or newer.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

To build the pinned official SWORD release locally first:

```sh
./scripts/build-sword.sh "$PWD/.local/sword"
PKG_CONFIG_PATH="$PWD/.local/sword/lib/pkgconfig" cmake --preset release
PKG_CONFIG_PATH="$PWD/.local/sword/lib/pkgconfig" cmake --build --preset release
ctest --preset release
```

## Use

An explicit SWORD installation root is required. This prevents host-specific
module discovery from changing otherwise identical output.

```sh
getbiblesword list --sword-path /usr/share/sword
getbiblesword extract --sword-path /usr/share/sword --module KJV --output kjv.ndjson
getbiblesword contract
getbiblesword version
```

`extract` refuses to overwrite an existing output file unless `--force` is given.
NDJSON is written to standard output when `--output` is omitted. Operational
errors go to standard error; extraction warnings and failures are also preserved
as contract diagnostics.

## Non-goals for this milestone

This repository does not generate the GetBible API shape, Builder templates or
Study Builder repositories. Those become downstream consumers only after the v1
contract and conformance corpus are stable.

## License

Copyright (C) 2026 Llewellyn van der Merwe and contributors.

Licensed under the GNU General Public License version 2 only. See [LICENSE](LICENSE).
