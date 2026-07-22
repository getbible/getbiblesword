# Independent v1 validator and artifact reassembler

## Independence boundary

`getbiblesword-v1` is a Python standard-library reference consumer. It neither
links to CrossWire SWORD nor imports getBibleSword's C++ serializer. That separation
is deliberate: a producer cannot establish its own correctness by parsing output
with the same implementation assumptions that created it.

The validator reads the original NDJSON bytes and checks the rules that a JSON
Schema cannot fully express, including record order, consecutive sequence numbers,
canonical byte envelopes, artifact state transitions, record and diagnostic
counts, and the footer hash over the exact preceding lines.

## Validation

```sh
getbiblesword-v1 validate module.ndjson
getbiblesword-v1 validate - < module.ndjson
```

A valid stream exits zero. Invalid JSON, duplicate object members, non-LF framing,
unknown or misplaced records, invalid base64, size or SHA mismatches, incomplete
artifacts, failed producer streams and footer mismatches exit nonzero with the
record line and reason. `list` streams are accepted but have no artifacts to
reassemble.

## Artifact reconstruction

```sh
getbiblesword-v1 reassemble module.ndjson reconstructed-module
```

The command validates the complete stream before writing anything. It refuses an
existing destination and reconstructs into a sibling staging directory before an
atomic rename. Paths must be normalized, relative byte paths without `.` or `..`;
collisions and parent/child type conflicts are rejected. Regular files are checked
against their final size and digest. Directory and regular-file permission bits are
restored without owner, group, timestamps, set-ID or sticky bits.

Symlinks are rejected by default because replaying an untrusted target is an
intentional trust decision. An operator can preserve them explicitly:

```sh
getbiblesword-v1 reassemble --allow-symlinks module.ndjson reconstructed-module
```

The target bytes are preserved exactly, but the reassembler never follows the
link while validating or writing any other artifact.

## Why both tools are one reference consumer

Artifact reconstruction depends on every validation invariant. Combining the two
in a small independent implementation prevents a caller from accidentally writing
data from an unverified or producer-failed stream. It also gives downstream Builder
adapters a directly executable statement of the v1 consumer obligations.
