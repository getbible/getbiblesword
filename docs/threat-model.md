# Threat model

## Assets

- Host filesystem confidentiality and integrity.
- Correct, complete module output.
- Availability under hostile or malformed input.
- Release provenance and dependency integrity.

## Untrusted inputs

Module configuration, data files, indexes, compressed blocks, markup, entry keys,
filenames, symlinks, media and SWORD-returned values are all untrusted. Module size
is not assumed to be small.

## Controls

- Require an explicit SWORD root for deterministic commands.
- Canonicalize the root and refuse artifact paths that escape it.
- Enumerate symlinks but never follow them; open regular files with no-follow
  semantics on Linux and verify file identity before and after reading.
- Stream artifacts with bounded fixed-size buffers.
- Encode input bytes rather than interpolating them into JSON or terminal output.
- Never execute module content or invoke a shell with module-derived arguments.
- Never fetch URLs referenced by a module.
- Do not accept cipher keys through command-line arguments or environment variables.
- Refuse accidental output overwrite unless `--force` is explicit.
- Detect input mutation during extraction and fail the stream.
- Run unit tests under ASan/UBSan and continuous CodeQL analysis.
- Build releases from the official SWORD 1.9.0 tarball with a pinned SHA-256.

## Residual risks

The SWORD engine parses untrusted module formats in-process. A memory-safety flaw in
SWORD or a linked decompression library can affect getBibleSword. A future hardened
runner may add seccomp, namespaces and resource limits, but those must wrap this CLI
rather than alter the contract.

Artifact capture can expose copyrighted or confidential module bytes. Operators are
responsible for authorization and distribution rights; the tool performs extraction,
not license policy enforcement.
