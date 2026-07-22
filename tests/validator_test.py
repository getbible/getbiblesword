#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

from __future__ import annotations

import base64
from collections import Counter
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = REPOSITORY_ROOT / "tools" / "getbiblesword_v1.py"
SPEC = importlib.util.spec_from_file_location("getbiblesword_v1", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
TOOL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TOOL
SPEC.loader.exec_module(TOOL)


def byte_value(data: bytes) -> dict[str, object]:
    value: dict[str, object] = {
        "base64": base64.b64encode(data).decode("ascii"),
        "encoding": "base64",
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
    }
    try:
        value["utf8"] = data.decode("utf-8")
    except UnicodeDecodeError:
        pass
    return value


def module_record(sequence: int = 1) -> dict[str, object]:
    return {
        "classification": "bible",
        "description": byte_value(b"Synthetic module"),
        "direction": {"code": 0, "name": "ltr"},
        "driver": byte_value(b"RawText"),
        "encoding": {"code": 2, "name": "utf8"},
        "language": byte_value(b"en"),
        "markup": {"code": 7, "name": "osis"},
        "name": byte_value(b"Test"),
        "sequence": sequence,
        "sword_type": byte_value(b"Biblical Texts"),
        "type": "module",
    }


def build_stream(command: str, records: list[dict[str, object]], *, success: bool = True) -> bytes:
    header: dict[str, object] = {
        "command": command,
        "contract": TOOL.CONTRACT,
        "contract_version": 1,
        "deterministic": True,
        "producer": "getBibleSword",
        "producer_version": "test",
        "sequence": 0,
        "sword_version": "1.9.0",
        "type": "header",
    }
    if command == "extract":
        header["artifact_chunk_size"] = 4096
    all_records = [header, *records]
    counts = Counter(str(record["type"]) for record in all_records)
    entries = sum(record["type"] == "entry" for record in all_records)
    artifacts = [record for record in all_records if record["type"] == "artifact_end"]
    artifact_bytes = sum(int(record["size"]) for record in artifacts)
    diagnostics = Counter(
        str(record["severity"])
        for record in all_records
        if record["type"] == "diagnostic"
    )
    lines = [
        json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n"
        for record in all_records
    ]
    footer = {
        "artifact_bytes": artifact_bytes,
        "artifacts": len(artifacts),
        "counts": dict(sorted(counts.items())),
        "diagnostics": {
            "error": diagnostics["error"],
            "info": diagnostics["info"],
            "warning": diagnostics["warning"],
        },
        "entries": entries,
        "sequence": len(all_records),
        "stream_sha256": hashlib.sha256(b"".join(lines)).hexdigest(),
        "success": success,
        "type": "footer",
    }
    lines.append(json.dumps(footer, sort_keys=True, separators=(",", ":")).encode("utf-8") + b"\n")
    return b"".join(lines)


class ValidatorTests(unittest.TestCase):
    def validate(self, stream: bytes):
        return TOOL.Validator(TOOL.DEFAULT_MAX_RECORD_BYTES).validate(io.BytesIO(stream))

    def test_accepts_minimal_list_stream(self) -> None:
        summary = self.validate(build_stream("list", []))
        self.assertEqual(summary.command, "list")
        self.assertTrue(summary.success)
        self.assertEqual(summary.records, 2)

    def test_rejects_duplicate_json_members(self) -> None:
        stream = build_stream("list", [])
        first, rest = stream.split(b"\n", 1)
        first = first[:-1] + b',"type":"header"}'
        with self.assertRaisesRegex(TOOL.ContractError, "duplicate JSON member"):
            self.validate(first + b"\n" + rest)

    def test_rejects_tampered_stream_digest(self) -> None:
        stream = build_stream("list", [])
        lines = stream.splitlines()
        footer = json.loads(lines[-1])
        footer["stream_sha256"] = "0" * 64
        lines[-1] = json.dumps(footer, sort_keys=True, separators=(",", ":")).encode()
        with self.assertRaisesRegex(TOOL.ContractError, "stream_sha256"):
            self.validate(b"\n".join(lines) + b"\n")

    def test_rejects_invalid_byte_value_digest(self) -> None:
        module = module_record()
        module["name"] = dict(module["name"])
        module["name"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(TOOL.ContractError, "module.name.sha256"):
            self.validate(build_stream("extract", [module]))

    def test_rejects_artifact_path_traversal(self) -> None:
        records = [
            module_record(),
            {
                "artifact_id": 0,
                "file_type": "directory",
                "mode": 0o755,
                "path": byte_value(b"../escape"),
                "role": "module_data",
                "sequence": 2,
                "type": "artifact_begin",
            },
            {
                "artifact_id": 0,
                "sequence": 3,
                "sha256": hashlib.sha256(b"").hexdigest(),
                "size": 0,
                "type": "artifact_end",
            },
        ]
        with self.assertRaisesRegex(TOOL.ContractError, "may not traverse"):
            self.validate(build_stream("extract", records))

    def test_validates_and_reassembles_regular_artifact(self) -> None:
        content = b"byte-exact artifact\n"
        records = [
            module_record(),
            {
                "artifact_id": 0,
                "file_type": "regular",
                "mode": 0o640,
                "path": byte_value(b"modules/test/data.bin"),
                "role": "module_data",
                "sequence": 2,
                "size_expected": len(content),
                "type": "artifact_begin",
            },
            {
                "artifact_id": 0,
                "data": byte_value(content),
                "index": 0,
                "sequence": 3,
                "type": "artifact_chunk",
            },
            {
                "artifact_id": 0,
                "sequence": 4,
                "sha256": hashlib.sha256(content).hexdigest(),
                "size": len(content),
                "stable": True,
                "type": "artifact_end",
            },
        ]
        stream = build_stream("extract", records)
        summary = self.validate(stream)
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "stream.ndjson"
            output = root / "output"
            source.write_bytes(stream)
            TOOL.reassemble(
                source,
                output,
                summary,
                allow_symlinks=False,
                preserve_special_mode_bits=False,
            )
            self.assertEqual((output / "modules/test/data.bin").read_bytes(), content)


if __name__ == "__main__":
    unittest.main()
