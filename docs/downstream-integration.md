# Downstream integration guide

## Compatibility boundary

getBibleSword is a protocol producer, not an application data model. Consumers
may use the standalone `getbiblesword` process or `libgetbiblesword.so.1`. Both
frontends produce identical NDJSON for the same operation, product version,
module bytes and options. Run either frontend against an explicit SWORD
installation, validate the resulting NDJSON, and then transform validated records
in a separate adapter owned by the consuming project.

The product version, native ABI and protocol version are independent. Release
`0.3.0` provides native ABI 1 and emits contract `getbiblesword.ndjson/v1` with
numeric `contract_version: 1`. Consumers must inspect the stream header instead
of inferring compatibility from the binary, library or archive version.

## Safe pipeline

```sh
getbiblesword extract \
    --sword-path /usr/share/sword \
    --module KJV \
    --output kjv.ndjson
getbiblesword-v1 validate --json kjv.ndjson
```

Only begin application-specific processing after validation exits zero. Validation
checks exact LF framing, JSON objects, record order, sequence numbers, byte values,
artifact state, counts, diagnostics, `footer.success` and the SHA-256 of all
pre-footer stream bytes.

For large modules, keep the export on disk or stream it record by record. Do not
load the full document into memory and do not wrap the lines in a JSON array.

Native consumers follow the same validate-before-use rule. C callback chunks are
not record boundaries and a successful C function status does not replace footer,
digest, sequence or byte-envelope validation. See [C ABI v1](c-api-v1.md).

## Record map

| Record | Cardinality | Consumer purpose |
|---|---:|---|
| `header` | exactly 1, first | Contract, producer and SWORD versions; command |
| `module` | 1 for `extract`; many for `list` | Name, authoritative SWORD type/driver and convenience classification |
| `config_source` | 0..n | Exact defining configuration-file bytes |
| `config_entry` | 0..n | SWORD's ordered interpreted configuration map, including repeated keys |
| `entry` | 0..n | Key, scope, raw bytes, render/strip views, attributes and annotation segments |
| `artifact_begin` | 0..n groups | Artifact path, role, file type and safe mode metadata |
| `artifact_chunk` | 0..n per regular file | Ordered binary file content chunks |
| `artifact_end` | 1 per artifact group | Final size and digest for the concatenated chunks |
| `diagnostic` | 0..n | Stable information, warning or error raised during extraction |
| `footer` | exactly 1, last | Counts, stream digest and final success decision |

The normative order and field semantics are in [contract v1](contract-v1.md). The
[JSON Schema](../schema/v1/contract.schema.json) validates individual record
shapes; it cannot replace whole-stream validation.

## Byte values

Source-derived strings and binary values use a lossless byte envelope:

```json
{
  "base64": "S0pW",
  "encoding": "base64",
  "sha256": "f98326ec7971053443d80268b911680a0eec8d4ead3b1d67445b7d534f1b5b2f",
  "size": 3,
  "utf8": "KJV"
}
```

Decode `base64`, verify `size` and `sha256`, and treat those decoded bytes as the
value. `utf8` is present only when the bytes have an exact safe UTF-8 projection.
Never substitute `utf8`, `rendered_default` or `stripped` for authoritative raw
bytes.

## Acceptance rules

A production consumer must:

- require `header.contract == "getbiblesword.ndjson/v1"` and
  `header.contract_version == 1`;
- require zero-based consecutive `sequence` values;
- preserve array order, duplicate configuration entries and duplicate logical
  keys;
- retain unknown additive fields, classifications, attributes, annotations and
  diagnostics even when it cannot interpret them;
- verify every byte envelope and artifact group;
- verify the footer digest over the exact preceding lines, including each LF;
- reject a missing footer, invalid digest or `footer.success != true`; and
- apply separate authorization and licensing rules before redistributing captured
  module bytes.

The bundled independent validator implements these rules and is the recommended
gate even when the application also performs its own checks.

## Bash and jq

This lists module names, drivers and classifications without decoding entry data:

```sh
getbiblesword list --sword-path /usr/share/sword --output modules.ndjson
getbiblesword-v1 validate modules.ndjson
jq -r '
  select(.type == "module")
  | [.name.utf8, .driver.utf8, .classification]
  | @tsv
' modules.ndjson
```

The optional `utf8` projection is suitable here only as display text. A program
that uses the values as identifiers must decode and compare their base64 bytes.

## Python

This complete script validates a file first and then streams entry keys, raw byte
sizes and digests without printing untrusted content:

```python
#!/usr/bin/env python3
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit(f"usage: {Path(sys.argv[0]).name} MODULE.ndjson")

source = Path(sys.argv[1])
subprocess.run(["getbiblesword-v1", "validate", str(source)], check=True)

def decode_bytes(value: dict[str, object]) -> bytes:
    raw = base64.b64decode(str(value["base64"]), validate=True)
    if value.get("encoding") != "base64" or len(raw) != value["size"]:
        raise ValueError("invalid byte envelope")
    if hashlib.sha256(raw).hexdigest() != value["sha256"]:
        raise ValueError("invalid byte digest")
    return raw

with source.open("rb") as stream:
    for line_number, line in enumerate(stream, 1):
        record = json.loads(line)
        if record.get("type") != "entry":
            continue
        key = decode_bytes(record["key"])
        raw = decode_bytes(record["raw"])
        safe_key = base64.b64encode(key).decode("ascii")
        print(line_number, safe_key, len(raw), hashlib.sha256(raw).hexdigest())
```

## Node.js and TypeScript

This ES module works directly with Node.js 20+ and is also valid TypeScript after
adding the project-specific type annotations:

```javascript
#!/usr/bin/env node
import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { createInterface } from "node:readline";
import { spawnSync } from "node:child_process";

const input = process.argv[2];
if (!input) throw new Error(`usage: ${process.argv[1]} MODULE.ndjson`);

const checked = spawnSync("getbiblesword-v1", ["validate", input], {
  stdio: "inherit",
});
if (checked.error) throw checked.error;
if (checked.status !== 0) process.exit(checked.status ?? 1);

function decodeBytes(value) {
  if (value.encoding !== "base64") throw new Error("invalid byte encoding");
  const bytes = Buffer.from(value.base64, "base64");
  const canonical = bytes.toString("base64");
  const digest = createHash("sha256").update(bytes).digest("hex");
  if (canonical !== value.base64 || bytes.length !== value.size || digest !== value.sha256) {
    throw new Error("invalid byte envelope");
  }
  return bytes;
}

const lines = createInterface({ input: createReadStream(input), crlfDelay: Infinity });
for await (const line of lines) {
  const record = JSON.parse(line);
  if (record.type !== "entry") continue;
  const key = decodeBytes(record.key);
  const raw = decodeBytes(record.raw);
  console.log(JSON.stringify({
    key_base64: key.toString("base64"),
    size: raw.length,
    sha256: record.raw.sha256,
  }));
}
```

## PHP

This PHP 8.1+ CLI script follows the same validate-then-stream pattern:

```php
#!/usr/bin/env php
<?php
declare(strict_types=1);

if ($argc !== 2) {
    fwrite(STDERR, "usage: {$argv[0]} MODULE.ndjson\n");
    exit(2);
}

$process = proc_open(
    ['getbiblesword-v1', 'validate', $argv[1]],
    [0 => STDIN, 1 => STDOUT, 2 => STDERR],
    $pipes
);
if (!is_resource($process)) {
    throw new RuntimeException('unable to start getbiblesword-v1');
}
$status = proc_close($process);
if ($status !== 0) {
    exit($status);
}

$decode = static function (array $value): string {
    if (($value['encoding'] ?? null) !== 'base64') {
        throw new RuntimeException('invalid byte encoding');
    }
    $bytes = base64_decode($value['base64'], true);
    if ($bytes === false
        || base64_encode($bytes) !== $value['base64']
        || strlen($bytes) !== $value['size']
        || !hash_equals($value['sha256'], hash('sha256', $bytes))) {
        throw new RuntimeException('invalid byte envelope');
    }
    return $bytes;
};

$stream = new SplFileObject($argv[1], 'rb');
foreach ($stream as $lineNumber => $line) {
    if ($line === false || $line === '') {
        continue;
    }
    $record = json_decode($line, true, 512, JSON_THROW_ON_ERROR);
    if (($record['type'] ?? null) !== 'entry') {
        continue;
    }
    $key = $decode($record['key']);
    $raw = $decode($record['raw']);
    printf(
        "%d\t%s\t%d\t%s\n",
        $lineNumber + 1,
        base64_encode($key),
        strlen($raw),
        hash('sha256', $raw)
    );
}
```

These examples delegate whole-stream validation to the independent reference
consumer and re-check byte values at the application boundary. A downstream
adapter may add stronger project-specific validation, but it must not weaken the
contract rules.
