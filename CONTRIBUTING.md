# Contributing

Changes must preserve GPL-2.0-only licensing and use an SPDX header in source,
script, CMake and workflow files. Protocol changes require an ADR, schema update,
golden/conformance fixture and compatibility note.

Before submitting a change, run:

```sh
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
./scripts/check.sh
```

Never commit third-party SWORD modules, encryption keys, generated build trees or
release binaries. Small synthetic fixtures must state their source and license.
