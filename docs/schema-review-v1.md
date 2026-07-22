# Maintainer review packet: NDJSON v1

## Decision requested

Milestone 1's final gate is an explicit maintainer decision that the v1 schema,
prose contract and classification policy are suitable as the stable boundary for
Builder consumers. The conformance corpus and independent validator provide the
engineering evidence; they do not substitute for product ownership of the public
contract.

The reviewer should approve the items below or record the required change before
the contract is declared stable `1.0.0`.

## Contract decisions

- The identifier remains `getbiblesword.ndjson/v1`; breaking changes require v2.
- Decoded `base64` is authoritative. `utf8`, rendered text and stripped text are
  convenience projections and never replace source bytes.
- Header, module, configuration, entry, artifact, diagnostic and footer ordering
  is normative. Unknown additive fields within v1 must be ignored.
- The footer covers the exact bytes of every preceding LF-terminated record.
- A producer error is represented both by a diagnostic and `success:false`.
- Complete configuration and module artifacts are retained even when a logical
  projection is available.
- Artifact paths are root-relative byte paths; symlinks are captured but not
  followed, and reconstruction requires explicit permission to create them.

## Classification policy

Classification is a convenience projection. `sword_type` and `driver` remain the
authoritative SWORD values and are never overwritten.

| Precedence | SWORD signal | Classification |
|---:|---|---|
| 1 | Bible module type | `bible` |
| 2 | Commentary module type | `commentary` |
| 3 | Lexicon/dictionary module type | `dictionary_or_lexicon` |
| 4 | Daily-devotional module type | `devotional` |
| 5 | General-book type with `Category`, `Feature` or `Type` containing `map`, `image` or `resource`, ASCII case-insensitive | `resource` |
| 6 | Other general-book type | `general_book` |
| 7 | Anything else | `unknown` plus a stable warning during extraction |

The policy intentionally does not infer “devotional” from dates or “resource” from
filenames. Unknown values remain visible so future SWORD types do not disappear.

## Evidence to review

- [`contract-v1.md`](contract-v1.md) — normative semantics and ordering
- [`contract.schema.json`](../schema/v1/contract.schema.json) — record-shape schema
- [`validator-v1.md`](validator-v1.md) — executable consumer checks and safe replay
- [`conformance-corpus.md`](conformance-corpus.md) — all-driver matrix and acceptance
- [`threat-model.md`](threat-model.md) — trust boundary and residual risk

Run the complete evidence suite with:

```sh
PKG_CONFIG_PATH=/path/to/sword/lib/pkgconfig cmake \
  -S . -B build/conformance -G Ninja \
  -DGETBIBLESWORD_ENABLE_CONFORMANCE_TESTS=ON
cmake --build build/conformance --parallel
ctest --test-dir build/conformance --output-on-failure
```

## Sign-off record

- [ ] Schema and prose describe the intended stable v1 consumer boundary.
- [ ] Classification precedence and the `unknown` fallback are accepted.
- [ ] Artifact and byte-preservation policy is accepted.
- [ ] Builder integration may begin against validated v1 streams.

Record the approval in a reviewed pull request or issue and then mark the final
item in [`milestone-1.md`](milestone-1.md) complete. Do not check these boxes merely
because the automated suite passes.
