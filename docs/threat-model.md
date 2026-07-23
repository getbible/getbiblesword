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
- Validate the complete stream before reconstruction, refuse existing destinations,
  reject path traversal and write through a sibling staging directory.
- Refuse symlink reconstruction unless the operator explicitly allows it; never
  follow reconstructed links while writing another artifact.
- Run unit tests under ASan/UBSan and continuous CodeQL analysis.
- Build releases from the official SWORD 1.9.0 tarball with a pinned SHA-256.
- Compile the bundled static SWORD engine as PIC, hide all private C++ and SWORD
  symbols, and export only the versioned `gbs_*` C allowlist.
- Catch every exception at the C boundary and turn callback cancellation or
  failure into explicit status codes.
- Keep the standalone CLI independently linked so adding the shared library
  cannot create a new runtime dependency for existing downstream deployments.

## Residual risks

The SWORD engine parses untrusted module formats in-process. A memory-safety flaw in
SWORD or a linked decompression library can affect getBibleSword. A future hardened
runner may add seccomp, namespaces and resource limits, but those must wrap this CLI
rather than alter the contract.

A process using `libgetbiblesword.so.1` deliberately gives up the CLI process
isolation boundary. A memory-safety failure in SWORD, a linked dependency or the
extractor can terminate or compromise the host process, including a PHP-FPM
worker. Native consumers must apply operating-system confinement, resource limits,
module provenance controls and worker recycling appropriate to their deployment.
The C ABI contains C++ exceptions but cannot contain native memory corruption.

Artifact capture can expose copyrighted or confidential module bytes. Operators are
responsible for authorization and distribution rights; the tool performs extraction,
not license policy enforcement.
