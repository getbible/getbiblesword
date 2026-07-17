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
{"base64":"S0pW","encoding":"base64","sha256":"a6b10c...","size":3,"utf8":"KJV"}
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
- `annotation_segments`: a lossless lexical segmentation of raw markup. Segments
  not projected into official attributes remain explicitly present as
  `uninterpreted`; getBibleSword never invents a SWORD interpretation.

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
