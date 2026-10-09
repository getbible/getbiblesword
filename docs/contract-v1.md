# getbiblesword NDJSON contract v1

## Purpose

The v1 stream is a deterministic sequence of independent JSON objects separated by
LF (`0x0a`). It combines SWORD's official logical view with a reversible byte-level
source envelope. JSON member order is canonical and arrays preserve source or
engine order.

The header identifies the stream as `getbiblesword.ndjson/v1`. Every record has a
zero-based `sequence` number and `type`. The final `footer` contains the SHA-256 of
all preceding serialized lines.

## Byte value

Every untrusted or source-derived string uses the same lossless object:

```json
{"base64":"S0pW","encoding":"base64","sha256":"f98326ec7971053443d80268b911680a0eec8d4ead3b1d67445b7d534f1b5b2f","size":3,"utf8":"KJV"}
```

`base64`, `encoding`, `sha256` and `size` are required. `utf8` is present only when
the bytes are well-formed UTF-8 without forbidden JSON scalar values. Consumers
must treat decoded `base64` as authoritative.

## Record order

1. `header`
2. one `module` record
3. zero or more `config_source` records in path order
4. zero or more `config_entry` records in SWORD map order
5. zero or more `entry` records in official SWORD traversal order
6. zero or more artifact groups in path order: `artifact_begin`, `artifact_chunk*`,
   `artifact_end`
7. zero or more `diagnostic` records at the deterministic point of detection
8. exactly one `footer`

`list` uses `header`, sorted `module` records, diagnostics and `footer`.

## Module record

The module record preserves SWORD's exact name, type, driver, description, language,
direction, encoding, markup and the getBibleSword classification. Classification is
one of `bible`, `commentary`, `dictionary_or_lexicon`, `general_book`, `devotional`,
`resource` or `unknown`. It never overrides `sword_type` or `ModDrv`.

## Configuration records

`config_source` contains the complete bytes of each configuration file that defines
the selected module, including comments, blank lines, duplicate keys, continuation
lines and original line endings. `config_entry` is SWORD's interpreted ordered
multimap projection. The two representations are intentionally both present.

## Entry record

Each logical entry contains:

- `ordinal`: traversal order;
- `key`: exact official key text bytes;
- `scope`: engine type and available canonical scope data, including the index
  reported by the active `SWKey` (rather than a driver's optional module-level
  index cache);
- `raw`: exact bytes from `SWModule::getRawEntryBuf()`;
- `rendered_default`: result of `SWModule::renderText()` with the module's default
  manager filters and no user option overrides;
- `stripped`: result of `SWModule::stripText()`;
- `official_attributes`: the complete three-level ordered map produced after
  `setProcessEntryAttributes(true)` and rendering;
- `normalized_raw` (optional): source markup decoded strictly to UTF-8 using the
  declared `Encoding`, or `null` when decoding is unavailable. It does not apply
  markup filters, Unicode normalization, replacement characters or inferred
  encodings. Missing `Encoding` follows SWORD's Latin-1 default;
- `normalized_stripped` (optional): SWORD's `stripText()` projection of
  `normalized_raw`, or `null` when normalization or safe stripping is unavailable;
- `annotation_segments`: a lossless lexical segmentation of raw markup. Segments
  not projected into official attributes remain explicitly present as
  `uninterpreted`; getBibleSword never invents a SWORD interpretation.

Current-entry rendering populates `official_attributes`; the extractor copies
`raw` before rendering and snapshots attributes before stripping. Supplied-buffer
rendering disables SWORD attribute processing and must not be used to collect
those attributes. All legacy fields retain their existing meanings and encoding.
Consumers parsing source markup should prefer the optional UTF-8 fields while
retaining `raw` and artifacts for exact preservation.

The normalized projections support UTF-8, Latin-1, SCSU and UTF-16 through strict
ICU decoding. UTF-16 honors a byte-order mark; without one its defined byte order
is little-endian, independent of the host. Invalid byte sequences or unsupported
encodings produce `entry.encoding.unavailable` and `normalized_raw: null`.
Embedded NUL is retained in `normalized_raw`, but SWORD's string-based stripping
cannot preserve it, so `normalized_stripped` is null with
`entry.encoding.embedded_nul`. These warnings do not discard the entry.
The strip filter's output is validated independently: SWORD 1.9 can corrupt
Unicode punctuation while uppercasing an OSIS divine name (for example `Lord’s`).
Such output yields `normalized_stripped: null` and
`entry.normalized_stripped.invalid_utf8`; consumers can still derive display text
from the intact `normalized_raw`. The legacy `stripped` field retains the exact
engine result for inspection.

The logical raw boundary remains SWORD's `getRawEntryBuf()`, not direct physical
file reads. Some SWORD 1.9.0 drivers prepare text through NUL-terminated routines
and may truncate UTF-16 before this boundary. Normalization cannot restore bytes
already withheld by the engine; invalid surviving UTF-16 produces the diagnostic
above. Full artifact capture remains the exact physical-file recovery path.

Verse-key modules enable introductions before traversal, matching CrossWire's own
`mod2imp` exporter. Generic traversal is used for all official module drivers so
dictionaries, lexicons, commentaries, generic books and devotionals are not special
cases that can fall out of the export.

## Artifact records

Artifacts cover defining configuration files and every filesystem object under the
module directory exposed by SWORD as `AbsoluteDataPath`, falling back to `DataPath`
when the engine does not provide it. This retains sibling media as well as indexes
and content. Missing prefix-style data paths are resolved to all matching sibling
files. Regular files are streamed in fixed 1 MiB chunks by default.
Directories and symlinks are represented as metadata; symlinks are never followed.

`artifact_end.sha256` and `artifact_end.size` cover the concatenated decoded chunk
bytes. Paths and symlink targets are byte values. File modes are recorded without
owner, group or timestamps, because those are packaging metadata and make content
exports host-dependent.

## Diagnostics

Diagnostics have stable `code`, `severity`, `message` and structured context.
Severities are `info`, `warning` and `error`. An error makes `footer.success` false.
Unknown module types, unsupported filesystem objects, unreadable artifacts, SWORD
navigation errors and detected input mutation must produce diagnostics.

## Footer and verification

The footer contains record counts, logical entry count, artifact byte count,
diagnostic counts, `success` and `stream_sha256`. To verify, hash the exact bytes of
every line before the footer, including LF. The footer itself is excluded.

The independent `getbiblesword-v1 validate` reference consumer checks these rules,
including ordering and hashes that JSON Schema cannot express. A consumer must not
treat successful JSON Schema validation alone as proof of a valid stream.
