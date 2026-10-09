#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Exercise actual SWORD filter attributes and strict source projections."""

import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def decode(value):
    return base64.b64decode(value["base64"], validate=True)


def attributes(entry):
    return {
        decode(group["name"]).decode(): {
            decode(item["name"]).decode(): {
                decode(value["name"]).decode(): decode(value["value"])
                for value in item["values"]
            }
            for item in group["lists"]
        }
        for group in entry["official_attributes"]
    }


binary, writer, validator, *c_api = sys.argv[1:]
c_api = [value for value in c_api if value]
text = (
    '<title canonical="true" subType="x-preverse">A heading</title>'
    '<w lemma="strong:G0001" morph="robinson:N-NSM">café</w>'
    '<note type="explanation"><p>Footnote body.</p></note>'
)
scsu = text.replace("é", "\xe9").encode("latin-1")
fixtures = [
    ("UTF8", "UTF-8", text.encode(), text),
    ("Latin1", "Latin-1", text.encode("latin-1"), text),
    ("DefaultLatin1", "", text.encode("latin-1"), text),
    ("SCSU", "SCSU", scsu, text),
    ("BadUTF8", "UTF-8", b"bad\xffsource", None),
    ("BadSCSU", "SCSU", b"bad\x0e\x03", None),
    ("Unknown", "Not-An-Encoding", b"do not guess", None),
]

with tempfile.TemporaryDirectory(prefix="getbiblesword-semantics-") as temporary:
    directory = Path(temporary)
    root = directory / "sword"
    (root / "mods.d").mkdir(parents=True)
    for name, encoding, raw, normalized in fixtures:
        payload = directory / "payload"
        payload.write_bytes(raw)
        clean = directory / "clean"
        clean.write_bytes(b"A second clean entry.")
        prefix = root / "modules" / name / "data"
        subprocess.run([writer, str(prefix), str(payload), str(clean)], check=True)
        (root / "mods.d" / f"{name}.conf").write_text(
            f"[{name}]\nDataPath=./modules/{name}/data\nModDrv=RawLD\n"
            "SourceType=OSIS\nDescription=Public-domain semantic fixture\n"
            "GlobalOptionFilter=OSISHeadings\nGlobalOptionFilter=OSISFootnotes\n"
            "GlobalOptionFilter=OSISStrongs\nGlobalOptionFilter=OSISMorph\n"
            + (f"Encoding={encoding}\n" if encoding else ""),
            encoding="utf-8",
        )
        command = [binary, "extract", "--sword-path", str(root), "--module", name]
        stream = subprocess.check_output(command)
        assert stream == subprocess.check_output(command), name
        if c_api:
            assert stream == subprocess.check_output(
                [c_api[0], "extract", str(root), name, "1048576"]
            ), f"CLI/C ABI parity: {name}"
        output = directory / f"{name}.ndjson"
        output.write_bytes(stream)
        subprocess.run([sys.executable, validator, "validate", str(output)], check=True,
                       stdout=subprocess.DEVNULL)
        records = [json.loads(line) for line in stream.splitlines()]
        entries = [record for record in records if record["type"] == "entry"]
        assert len(entries) == 2, name
        first, second = entries
        assert decode(first["raw"]) == raw, f"source bytes changed: {name}"
        assert b"".join(decode(part["raw"]) for part in first["annotation_segments"]) == raw
        assert not attributes(second), f"stale attributes leaked: {name}"
        if normalized is None:
            assert first["normalized_raw"] is None, name
            assert first["normalized_stripped"] is None, name
            assert any(record.get("code") == "entry.encoding.unavailable" for record in records)
        else:
            assert decode(first["normalized_raw"]).decode() == normalized, name
            assert "café" in decode(first["normalized_stripped"]).decode(), name
            assert "Footnote body" not in decode(first["normalized_stripped"]).decode(), name
            values = attributes(first)
            assert values["Heading"]["Preverse"]["0"] == (
                b'<title canonical="true">A heading</title>'
            ), values
            assert values["Footnote"]["1"]["body"] == b"<p>Footnote body.</p>", values
            assert values["Word"]["001"]["Lemma"] == b"G0001", values
            assert values["Word"]["001"]["Morph"] == b"N-NSM", values

print("SWORD headings, footnotes, lexical attributes, encoding, and parity regressions passed")
