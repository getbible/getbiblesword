#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Independent validator and artifact reassembler for getBibleSword NDJSON v1."""

from __future__ import annotations

import argparse
import base64
import binascii
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
from typing import BinaryIO, Iterator, NoReturn


CONTRACT = "getbiblesword.ndjson/v1"
RECORD_TYPES = {
    "header",
    "module",
    "config_source",
    "config_entry",
    "entry",
    "artifact_begin",
    "artifact_chunk",
    "artifact_end",
    "diagnostic",
    "footer",
}
CLASSIFICATIONS = {
    "bible",
    "commentary",
    "dictionary_or_lexicon",
    "general_book",
    "devotional",
    "resource",
    "unknown",
}
SEVERITIES = {"info", "warning", "error"}
FILE_TYPES = {"regular", "directory", "symlink"}
HEX_DIGEST_LENGTH = 64
DEFAULT_MAX_RECORD_BYTES = 64 * 1024 * 1024


class ContractError(Exception):
    """A deterministic contract-validation failure."""


def fail(message: str, *, line: int | None = None) -> NoReturn:
    prefix = f"line {line}: " if line is not None else ""
    raise ContractError(prefix + message)


def _reject_constant(value: str) -> NoReturn:
    raise ValueError(f"non-finite JSON number {value!r} is not permitted")


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON member {key!r}")
        result[key] = value
    return result


def parse_json_object(raw: bytes, line_number: int) -> dict[str, object]:
    try:
        text = raw.decode("utf-8", errors="strict")
        value = json.loads(
            text,
            object_pairs_hook=_unique_object,
            parse_constant=_reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exception:
        fail(f"invalid JSON: {exception}", line=line_number)
    if not isinstance(value, dict):
        fail("each NDJSON line must contain one JSON object", line=line_number)
    return value


def require_members(
    value: dict[str, object], names: tuple[str, ...], context: str, line: int
) -> None:
    missing = [name for name in names if name not in value]
    if missing:
        fail(f"{context} is missing required member(s): {', '.join(missing)}", line=line)


def require_integer(
    value: object,
    context: str,
    line: int,
    *,
    minimum: int = 0,
    maximum: int | None = None,
) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{context} must be an integer", line=line)
    if value < minimum or (maximum is not None and value > maximum):
        upper = f" through {maximum}" if maximum is not None else " or greater"
        fail(f"{context} must be {minimum}{upper}", line=line)
    return value


def require_string(value: object, context: str, line: int) -> str:
    if not isinstance(value, str):
        fail(f"{context} must be a string", line=line)
    return value


def require_boolean(value: object, context: str, line: int) -> bool:
    if not isinstance(value, bool):
        fail(f"{context} must be a boolean", line=line)
    return value


def require_sha256(value: object, context: str, line: int) -> str:
    digest = require_string(value, context, line)
    if len(digest) != HEX_DIGEST_LENGTH or any(
        character not in "0123456789abcdef" for character in digest
    ):
        fail(f"{context} must be a lowercase SHA-256 digest", line=line)
    return digest


def decode_byte_value(value: object, context: str, line: int) -> bytes:
    if not isinstance(value, dict):
        fail(f"{context} must be a byte-value object", line=line)
    require_members(value, ("base64", "encoding", "sha256", "size"), context, line)
    allowed = {"base64", "encoding", "sha256", "size", "utf8"}
    unexpected = sorted(set(value) - allowed)
    if unexpected:
        fail(f"{context} has unknown member(s): {', '.join(unexpected)}", line=line)
    if value["encoding"] != "base64":
        fail(f"{context}.encoding must be 'base64'", line=line)
    encoded = require_string(value["base64"], f"{context}.base64", line)
    try:
        decoded = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as exception:
        fail(f"{context}.base64 is invalid: {exception}", line=line)
    if base64.b64encode(decoded).decode("ascii") != encoded:
        fail(f"{context}.base64 is not canonical RFC 4648 base64", line=line)
    declared_size = require_integer(value["size"], f"{context}.size", line)
    if declared_size != len(decoded):
        fail(
            f"{context}.size is {declared_size}, decoded size is {len(decoded)}",
            line=line,
        )
    declared_hash = require_sha256(value["sha256"], f"{context}.sha256", line)
    actual_hash = hashlib.sha256(decoded).hexdigest()
    if declared_hash != actual_hash:
        fail(f"{context}.sha256 does not match its decoded bytes", line=line)
    if "utf8" in value:
        projection = require_string(value["utf8"], f"{context}.utf8", line)
        try:
            decoded_text = decoded.decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            fail(f"{context}.utf8 is present for invalid UTF-8 bytes", line=line)
        if projection != decoded_text:
            fail(f"{context}.utf8 is not an exact projection of the bytes", line=line)
    return decoded


def validate_embedded_byte_values(value: object, context: str, line: int) -> None:
    if isinstance(value, dict):
        if "base64" in value or value.get("encoding") == "base64":
            decode_byte_value(value, context, line)
            return
        for name, child in value.items():
            validate_embedded_byte_values(child, f"{context}.{name}", line)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            validate_embedded_byte_values(child, f"{context}[{index}]", line)


def validate_enum(value: object, context: str, line: int) -> None:
    if not isinstance(value, dict):
        fail(f"{context} must be an object", line=line)
    require_members(value, ("code", "name"), context, line)
    require_integer(value["code"], f"{context}.code", line, maximum=255)
    require_string(value["name"], f"{context}.name", line)


def validate_relative_path(
    path: bytes,
    context: str,
    line: int,
    *,
    allow_trailing_separator: bool = False,
) -> tuple[bytes, ...]:
    if not path or b"\x00" in path or path.startswith(b"/"):
        fail(
            f"{context} must be a non-empty root-relative byte path; got {path!r}",
            line=line,
        )
    normalized = path[:-1] if allow_trailing_separator and path.endswith(b"/") else path
    components = tuple(normalized.split(b"/"))
    if any(component in (b"", b".", b"..") for component in components):
        fail(
            f"{context} must be normalized and may not traverse directories; got {path!r}",
            line=line,
        )
    return components


@dataclass(frozen=True)
class Artifact:
    artifact_id: int
    path: bytes
    components: tuple[bytes, ...]
    file_type: str
    mode: int
    role: str
    target: bytes | None
    size: int
    sha256: str


@dataclass
class ActiveArtifact:
    artifact_id: int
    path: bytes
    components: tuple[bytes, ...]
    file_type: str
    mode: int
    role: str
    target: bytes | None
    size_expected: int | None
    hash: object = field(default_factory=hashlib.sha256)
    size: int = 0
    chunks: int = 0
    short_chunk_seen: bool = False


@dataclass(frozen=True)
class ValidationSummary:
    command: str
    producer_version: str
    sword_version: str
    records: int
    modules: int
    entries: int
    artifacts: tuple[Artifact, ...]
    artifact_bytes: int
    diagnostics: dict[str, int]
    success: bool
    stream_sha256: str


class Validator:
    def __init__(self, maximum_record_bytes: int) -> None:
        if maximum_record_bytes < 1024:
            raise ValueError("maximum_record_bytes must be at least 1024")
        self.maximum_record_bytes = maximum_record_bytes
        self.sequence = 0
        self.counts: Counter[str] = Counter()
        self.stream_hash = hashlib.sha256()
        self.header: dict[str, object] | None = None
        self.footer: dict[str, object] | None = None
        self.command = ""
        self.phase = 0
        self.module_count = 0
        self.entry_count = 0
        self.config_source_ordinal = 0
        self.config_entry_ordinal = 0
        self.diagnostics: Counter[str] = Counter()
        self.artifacts: list[Artifact] = []
        self.active_artifact: ActiveArtifact | None = None
        self.artifact_paths: set[tuple[bytes, ...]] = set()
        self.chunk_size: int | None = None

    def validate(self, input_stream: BinaryIO) -> ValidationSummary:
        line_number = 0
        while True:
            raw_line = input_stream.readline(self.maximum_record_bytes + 1)
            if not raw_line:
                break
            line_number += 1
            if len(raw_line) > self.maximum_record_bytes:
                fail(
                    f"record exceeds the {self.maximum_record_bytes}-byte safety limit",
                    line=line_number,
                )
            if not raw_line.endswith(b"\n"):
                fail("every NDJSON record must end with LF", line=line_number)
            if raw_line == b"\n":
                fail("blank NDJSON records are not permitted", line=line_number)
            record = parse_json_object(raw_line[:-1], line_number)
            record_type = require_string(record.get("type"), "record.type", line_number)
            if record_type not in RECORD_TYPES:
                fail(f"unknown record type {record_type!r}", line=line_number)
            record_sequence = require_integer(
                record.get("sequence"), "record.sequence", line_number
            )
            if record_sequence != self.sequence:
                fail(
                    f"record.sequence is {record_sequence}, expected {self.sequence}",
                    line=line_number,
                )
            if record_type == "footer":
                self._validate_footer(record, line_number)
            else:
                if self.footer is not None:
                    fail("records may not follow the footer", line=line_number)
                self._validate_record(record_type, record, line_number)
                validate_embedded_byte_values(record, record_type, line_number)
                self.counts[record_type] += 1
                self.stream_hash.update(raw_line)
            self.sequence += 1

        if line_number == 0:
            fail("the NDJSON stream is empty")
        if self.footer is None:
            fail("the NDJSON stream has no footer")
        if self.active_artifact is not None:
            fail(f"artifact {self.active_artifact.artifact_id} has no artifact_end")
        assert self.header is not None
        assert self.footer is not None
        success = require_boolean(self.footer["success"], "footer.success", line_number)
        if self.command == "extract" and success and self.module_count != 1:
            fail("a successful extract stream must contain exactly one module")
        if self.command == "list" and (self.entry_count or self.artifacts):
            fail("a list stream may not contain entries or artifacts")
        return ValidationSummary(
            command=self.command,
            producer_version=require_string(
                self.header["producer_version"], "header.producer_version", 1
            ),
            sword_version=require_string(self.header["sword_version"], "header.sword_version", 1),
            records=self.sequence,
            modules=self.module_count,
            entries=self.entry_count,
            artifacts=tuple(self.artifacts),
            artifact_bytes=sum(artifact.size for artifact in self.artifacts),
            diagnostics=dict(self.diagnostics),
            success=success,
            stream_sha256=require_string(
                self.footer["stream_sha256"], "footer.stream_sha256", line_number
            ),
        )

    def _validate_record(
        self, record_type: str, record: dict[str, object], line: int
    ) -> None:
        if self.header is None and record_type != "header":
            fail("the first record must be a header", line=line)
        if record_type == "header":
            self._validate_header(record, line)
        elif record_type == "module":
            self._validate_module(record, line)
        elif record_type == "config_source":
            self._validate_config_source(record, line)
        elif record_type == "config_entry":
            self._validate_config_entry(record, line)
        elif record_type == "entry":
            self._validate_entry(record, line)
        elif record_type == "artifact_begin":
            self._validate_artifact_begin(record, line)
        elif record_type == "artifact_chunk":
            self._validate_artifact_chunk(record, line)
        elif record_type == "artifact_end":
            self._validate_artifact_end(record, line)
        elif record_type == "diagnostic":
            self._validate_diagnostic(record, line)

    def _advance(self, target: int, context: str, line: int) -> None:
        if target < self.phase:
            fail(f"{context} appears outside the required record order", line=line)
        self.phase = target

    def _validate_header(self, record: dict[str, object], line: int) -> None:
        if self.header is not None or self.sequence != 0:
            fail("exactly one header is required at sequence 0", line=line)
        require_members(
            record,
            (
                "command",
                "contract",
                "contract_version",
                "deterministic",
                "producer",
                "producer_version",
                "sword_version",
            ),
            "header",
            line,
        )
        if record["contract"] != CONTRACT or record["contract_version"] != 1:
            fail(f"unsupported contract; expected {CONTRACT}", line=line)
        if record["producer"] != "getBibleSword":
            fail("header.producer must be 'getBibleSword'", line=line)
        if record["deterministic"] is not True:
            fail("header.deterministic must be true", line=line)
        self.command = require_string(record["command"], "header.command", line)
        if self.command not in {"list", "extract"}:
            fail("header.command must be 'list' or 'extract'", line=line)
        require_string(record["producer_version"], "header.producer_version", line)
        require_string(record["sword_version"], "header.sword_version", line)
        if self.command == "extract":
            require_members(record, ("artifact_chunk_size",), "header", line)
            self.chunk_size = require_integer(
                record["artifact_chunk_size"],
                "header.artifact_chunk_size",
                line,
                minimum=4096,
                maximum=16 * 1024 * 1024,
            )
        self.header = record

    def _validate_module(self, record: dict[str, object], line: int) -> None:
        self._advance(1, "module", line)
        if self.command == "extract" and self.module_count != 0:
            fail("an extract stream may contain at most one module", line=line)
        require_members(
            record,
            (
                "classification",
                "description",
                "direction",
                "driver",
                "encoding",
                "language",
                "markup",
                "name",
                "sword_type",
            ),
            "module",
            line,
        )
        classification = require_string(record["classification"], "module.classification", line)
        if classification not in CLASSIFICATIONS:
            fail(f"unknown module.classification {classification!r}", line=line)
        for name in ("description", "driver", "language", "name", "sword_type"):
            decode_byte_value(record[name], f"module.{name}", line)
        for name in ("direction", "encoding", "markup"):
            validate_enum(record[name], f"module.{name}", line)
        self.module_count += 1

    def _extract_only(self, record_type: str, line: int) -> None:
        if self.command != "extract":
            fail(f"{record_type} is not permitted in a list stream", line=line)
        if self.module_count != 1:
            fail(f"{record_type} requires a preceding module record", line=line)

    def _validate_config_source(self, record: dict[str, object], line: int) -> None:
        self._extract_only("config_source", line)
        self._advance(2, "config_source", line)
        require_members(record, ("ordinal", "path", "raw"), "config_source", line)
        ordinal = require_integer(record["ordinal"], "config_source.ordinal", line)
        if ordinal != self.config_source_ordinal:
            fail(
                f"config_source.ordinal is {ordinal}, expected {self.config_source_ordinal}",
                line=line,
            )
        path = decode_byte_value(record["path"], "config_source.path", line)
        validate_relative_path(path, "config_source.path", line)
        decode_byte_value(record["raw"], "config_source.raw", line)
        self.config_source_ordinal += 1

    def _validate_config_entry(self, record: dict[str, object], line: int) -> None:
        self._extract_only("config_entry", line)
        self._advance(3, "config_entry", line)
        require_members(record, ("name", "ordinal", "value"), "config_entry", line)
        ordinal = require_integer(record["ordinal"], "config_entry.ordinal", line)
        if ordinal != self.config_entry_ordinal:
            fail(
                f"config_entry.ordinal is {ordinal}, expected {self.config_entry_ordinal}",
                line=line,
            )
        decode_byte_value(record["name"], "config_entry.name", line)
        decode_byte_value(record["value"], "config_entry.value", line)
        self.config_entry_ordinal += 1

    def _validate_entry(self, record: dict[str, object], line: int) -> None:
        self._extract_only("entry", line)
        self._advance(4, "entry", line)
        require_members(
            record,
            (
                "annotation_segments",
                "key",
                "official_attributes",
                "ordinal",
                "projections_available",
                "raw",
                "rendered_default",
                "scope",
                "stripped",
            ),
            "entry",
            line,
        )
        ordinal = require_integer(record["ordinal"], "entry.ordinal", line)
        if ordinal != self.entry_count:
            fail(f"entry.ordinal is {ordinal}, expected {self.entry_count}", line=line)
        decode_byte_value(record["key"], "entry.key", line)
        raw = decode_byte_value(record["raw"], "entry.raw", line)
        projections = require_boolean(
            record["projections_available"], "entry.projections_available", line
        )
        for name in ("rendered_default", "stripped"):
            projection = record[name]
            if projections:
                decode_byte_value(projection, f"entry.{name}", line)
            elif projection is not None:
                fail(f"entry.{name} must be null when projections are unavailable", line=line)
        segments = record["annotation_segments"]
        if not isinstance(segments, list):
            fail("entry.annotation_segments must be an array", line=line)
        reconstructed = bytearray()
        for index, segment in enumerate(segments):
            context = f"entry.annotation_segments[{index}]"
            if not isinstance(segment, dict):
                fail(f"{context} must be an object", line=line)
            require_members(segment, ("interpretation", "kind", "raw"), context, line)
            kind = require_string(segment["kind"], f"{context}.kind", line)
            interpretation = require_string(
                segment["interpretation"], f"{context}.interpretation", line
            )
            if kind not in {"text", "markup", "entity"}:
                fail(f"{context}.kind is unknown", line=line)
            expected_interpretation = "not_applicable" if kind == "text" else "uninterpreted"
            if interpretation != expected_interpretation:
                fail(f"{context}.interpretation is inconsistent with its kind", line=line)
            reconstructed.extend(decode_byte_value(segment["raw"], f"{context}.raw", line))
        if bytes(reconstructed) != raw:
            fail("entry.annotation_segments do not reconstruct entry.raw", line=line)
        self._validate_official_attributes(record["official_attributes"], line)
        self._validate_scope(record["scope"], line)
        self.entry_count += 1

    def _validate_official_attributes(self, value: object, line: int) -> None:
        if not isinstance(value, list):
            fail("entry.official_attributes must be an array", line=line)
        for type_index, attribute_type in enumerate(value):
            context = f"entry.official_attributes[{type_index}]"
            if not isinstance(attribute_type, dict):
                fail(f"{context} must be an object", line=line)
            require_members(attribute_type, ("lists", "name"), context, line)
            decode_byte_value(attribute_type["name"], f"{context}.name", line)
            lists = attribute_type["lists"]
            if not isinstance(lists, list):
                fail(f"{context}.lists must be an array", line=line)
            for list_index, attribute_list in enumerate(lists):
                list_context = f"{context}.lists[{list_index}]"
                if not isinstance(attribute_list, dict):
                    fail(f"{list_context} must be an object", line=line)
                require_members(attribute_list, ("name", "values"), list_context, line)
                decode_byte_value(attribute_list["name"], f"{list_context}.name", line)
                values = attribute_list["values"]
                if not isinstance(values, list):
                    fail(f"{list_context}.values must be an array", line=line)
                for value_index, attribute in enumerate(values):
                    value_context = f"{list_context}.values[{value_index}]"
                    if not isinstance(attribute, dict):
                        fail(f"{value_context} must be an object", line=line)
                    require_members(attribute, ("name", "value"), value_context, line)
                    decode_byte_value(attribute["name"], f"{value_context}.name", line)
                    decode_byte_value(attribute["value"], f"{value_context}.value", line)

    def _validate_scope(self, value: object, line: int) -> None:
        if not isinstance(value, dict):
            fail("entry.scope must be an object", line=line)
        require_members(value, ("index", "type"), "entry.scope", line)
        require_integer(value["index"], "entry.scope.index", line, minimum=0)
        scope_type = require_string(value["type"], "entry.scope.type", line)
        if scope_type == "sword_key":
            return
        if scope_type != "verse_key":
            fail(f"unknown entry.scope.type {scope_type!r}", line=line)
        require_members(
            value,
            (
                "book",
                "book_abbreviation",
                "book_name",
                "chapter",
                "intro_scope",
                "osis_reference",
                "suffix",
                "testament",
                "verse",
                "versification",
            ),
            "entry.scope",
            line,
        )
        for name in ("book", "chapter", "testament", "verse"):
            require_integer(value[name], f"entry.scope.{name}", line)
        require_integer(value["suffix"], "entry.scope.suffix", line, maximum=255)
        intro_scope = require_string(value["intro_scope"], "entry.scope.intro_scope", line)
        if intro_scope not in {"module", "testament", "book", "chapter", "verse"}:
            fail("entry.scope.intro_scope is unknown", line=line)
        for name in ("book_abbreviation", "book_name"):
            if value[name] is not None:
                decode_byte_value(value[name], f"entry.scope.{name}", line)
        for name in ("osis_reference", "versification"):
            decode_byte_value(value[name], f"entry.scope.{name}", line)

    def _validate_artifact_begin(self, record: dict[str, object], line: int) -> None:
        self._extract_only("artifact_begin", line)
        self._advance(5, "artifact_begin", line)
        if self.active_artifact is not None:
            fail("artifact_begin may not occur before the active artifact ends", line=line)
        require_members(
            record,
            ("artifact_id", "file_type", "mode", "path", "role"),
            "artifact_begin",
            line,
        )
        artifact_id = require_integer(record["artifact_id"], "artifact_begin.artifact_id", line)
        if artifact_id != len(self.artifacts):
            fail(f"artifact_id is {artifact_id}, expected {len(self.artifacts)}", line=line)
        file_type = require_string(record["file_type"], "artifact_begin.file_type", line)
        if file_type not in FILE_TYPES:
            fail(f"unknown artifact file_type {file_type!r}", line=line)
        mode = require_integer(record["mode"], "artifact_begin.mode", line, maximum=0o7777)
        role = require_string(record["role"], "artifact_begin.role", line)
        path = decode_byte_value(record["path"], "artifact_begin.path", line)
        components = validate_relative_path(
            path,
            "artifact_begin.path",
            line,
            allow_trailing_separator=file_type == "directory",
        )
        if components in self.artifact_paths:
            fail("artifact paths must be unique", line=line)
        self.artifact_paths.add(components)
        target = None
        size_expected = None
        if file_type == "regular":
            require_members(record, ("size_expected",), "artifact_begin", line)
            size_expected = require_integer(
                record["size_expected"], "artifact_begin.size_expected", line
            )
        elif file_type == "symlink":
            require_members(record, ("target",), "artifact_begin", line)
            target = decode_byte_value(record["target"], "artifact_begin.target", line)
            if b"\x00" in target:
                fail("artifact_begin.target may not contain NUL", line=line)
        self.active_artifact = ActiveArtifact(
            artifact_id=artifact_id,
            path=path,
            components=components,
            file_type=file_type,
            mode=mode,
            role=role,
            target=target,
            size_expected=size_expected,
        )

    def _validate_artifact_chunk(self, record: dict[str, object], line: int) -> None:
        self._advance(5, "artifact_chunk", line)
        active = self.active_artifact
        if active is None or active.file_type != "regular":
            fail("artifact_chunk requires an active regular artifact", line=line)
        require_members(record, ("artifact_id", "data", "index"), "artifact_chunk", line)
        artifact_id = require_integer(record["artifact_id"], "artifact_chunk.artifact_id", line)
        index = require_integer(record["index"], "artifact_chunk.index", line)
        if artifact_id != active.artifact_id:
            fail("artifact_chunk.artifact_id does not match artifact_begin", line=line)
        if index != active.chunks:
            fail(f"artifact_chunk.index is {index}, expected {active.chunks}", line=line)
        data = decode_byte_value(record["data"], "artifact_chunk.data", line)
        assert self.chunk_size is not None
        if not data:
            fail("regular artifacts may not contain empty chunks", line=line)
        if len(data) > self.chunk_size:
            fail("artifact chunk exceeds header.artifact_chunk_size", line=line)
        if active.short_chunk_seen:
            fail("a short artifact chunk must be the final chunk", line=line)
        if len(data) < self.chunk_size:
            active.short_chunk_seen = True
        active.hash.update(data)
        active.size += len(data)
        active.chunks += 1

    def _validate_artifact_end(self, record: dict[str, object], line: int) -> None:
        self._advance(5, "artifact_end", line)
        active = self.active_artifact
        if active is None:
            fail("artifact_end requires an active artifact", line=line)
        require_members(record, ("artifact_id", "sha256", "size"), "artifact_end", line)
        artifact_id = require_integer(record["artifact_id"], "artifact_end.artifact_id", line)
        if artifact_id != active.artifact_id:
            fail("artifact_end.artifact_id does not match artifact_begin", line=line)
        declared_size = require_integer(record["size"], "artifact_end.size", line)
        declared_hash = require_sha256(record["sha256"], "artifact_end.sha256", line)
        if active.file_type == "symlink":
            assert active.target is not None
            actual_size = len(active.target)
            actual_hash = hashlib.sha256(active.target).hexdigest()
        elif active.file_type == "directory":
            actual_size = 0
            actual_hash = hashlib.sha256(b"").hexdigest()
        else:
            actual_size = active.size
            actual_hash = active.hash.hexdigest()
            require_members(record, ("stable",), "artifact_end", line)
            stable = require_boolean(record["stable"], "artifact_end.stable", line)
            if stable and active.size_expected != actual_size:
                fail("stable artifact size does not match size_expected", line=line)
        if declared_size != actual_size or declared_hash != actual_hash:
            fail("artifact_end digest or size does not match the artifact bytes", line=line)
        self.artifacts.append(
            Artifact(
                artifact_id=active.artifact_id,
                path=active.path,
                components=active.components,
                file_type=active.file_type,
                mode=active.mode,
                role=active.role,
                target=active.target,
                size=actual_size,
                sha256=actual_hash,
            )
        )
        self.active_artifact = None

    def _validate_diagnostic(self, record: dict[str, object], line: int) -> None:
        if self.active_artifact is not None:
            fail("diagnostics may not split an artifact record group", line=line)
        require_members(record, ("code", "message", "severity"), "diagnostic", line)
        require_string(record["code"], "diagnostic.code", line)
        decode_byte_value(record["message"], "diagnostic.message", line)
        severity = require_string(record["severity"], "diagnostic.severity", line)
        if severity not in SEVERITIES:
            fail(f"unknown diagnostic severity {severity!r}", line=line)
        self.diagnostics[severity] += 1

    def _validate_footer(self, record: dict[str, object], line: int) -> None:
        if self.header is None:
            fail("footer may not precede the header", line=line)
        if self.footer is not None:
            fail("exactly one footer is permitted", line=line)
        if self.active_artifact is not None:
            fail("footer may not split an artifact record group", line=line)
        require_members(
            record,
            (
                "artifact_bytes",
                "artifacts",
                "counts",
                "diagnostics",
                "entries",
                "stream_sha256",
                "success",
            ),
            "footer",
            line,
        )
        declared_hash = require_sha256(record["stream_sha256"], "footer.stream_sha256", line)
        if declared_hash != self.stream_hash.hexdigest():
            fail("footer.stream_sha256 does not match preceding NDJSON bytes", line=line)
        counts = record["counts"]
        if not isinstance(counts, dict):
            fail("footer.counts must be an object", line=line)
        normalized_counts: dict[str, int] = {}
        for name, count in counts.items():
            if name not in RECORD_TYPES - {"footer"}:
                fail(f"footer.counts contains unknown record type {name!r}", line=line)
            normalized_counts[name] = require_integer(count, f"footer.counts.{name}", line)
        if normalized_counts != dict(self.counts):
            fail("footer.counts does not match observed record counts", line=line)
        declared_entries = require_integer(record["entries"], "footer.entries", line)
        declared_artifacts = require_integer(record["artifacts"], "footer.artifacts", line)
        declared_bytes = require_integer(record["artifact_bytes"], "footer.artifact_bytes", line)
        if declared_entries != self.entry_count:
            fail("footer.entries does not match observed entries", line=line)
        if declared_artifacts != len(self.artifacts):
            fail("footer.artifacts does not match observed artifacts", line=line)
        if declared_bytes != sum(artifact.size for artifact in self.artifacts):
            fail("footer.artifact_bytes does not match observed artifact bytes", line=line)
        diagnostics = record["diagnostics"]
        if not isinstance(diagnostics, dict):
            fail("footer.diagnostics must be an object", line=line)
        expected_diagnostics = {severity: self.diagnostics[severity] for severity in sorted(SEVERITIES)}
        observed_diagnostics: dict[str, int] = {}
        for severity in sorted(SEVERITIES):
            if severity not in diagnostics:
                fail(f"footer.diagnostics.{severity} is required", line=line)
            observed_diagnostics[severity] = require_integer(
                diagnostics[severity], f"footer.diagnostics.{severity}", line
            )
        if set(diagnostics) != SEVERITIES:
            fail("footer.diagnostics contains unknown severities", line=line)
        if observed_diagnostics != expected_diagnostics:
            fail("footer.diagnostics does not match observed diagnostics", line=line)
        success = require_boolean(record["success"], "footer.success", line)
        if success != (self.diagnostics["error"] == 0):
            fail("footer.success is inconsistent with error diagnostics", line=line)
        self.footer = record


@contextmanager
def local_input(source: str) -> Iterator[Path]:
    if source != "-":
        yield Path(source)
        return
    temporary = tempfile.NamedTemporaryFile(prefix="getbiblesword-v1-", suffix=".ndjson", delete=False)
    path = Path(temporary.name)
    try:
        with temporary:
            shutil.copyfileobj(sys.stdin.buffer, temporary, length=1024 * 1024)
            temporary.flush()
            os.fsync(temporary.fileno())
        yield path
    finally:
        path.unlink(missing_ok=True)


def validate_path(path: Path, maximum_record_bytes: int) -> ValidationSummary:
    with path.open("rb") as input_stream:
        return Validator(maximum_record_bytes).validate(input_stream)


def artifact_path(root: bytes, artifact: Artifact) -> bytes:
    return os.path.join(root, *artifact.components)


def ensure_parent(path: bytes) -> None:
    parent = os.path.dirname(path)
    os.makedirs(parent, mode=0o700, exist_ok=True)


def safe_symlink_target(artifact: Artifact) -> None:
    assert artifact.target is not None
    target = artifact.target
    if not target or target.startswith(b"/"):
        raise ContractError(
            f"artifact {artifact.artifact_id} has an absolute or empty symlink target"
        )
    combined = list(artifact.components[:-1])
    for component in target.split(b"/"):
        if component in (b"", b"."):
            continue
        if component == b"..":
            if not combined:
                raise ContractError(
                    f"artifact {artifact.artifact_id} symlink target escapes the output root"
                )
            combined.pop()
        else:
            combined.append(component)


def _write_all(file_descriptor: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = os.write(file_descriptor, data[offset:])
        if written <= 0:
            raise OSError("short write while reassembling an artifact")
        offset += written


def reassemble(
    source: Path,
    destination: Path,
    summary: ValidationSummary,
    *,
    allow_symlinks: bool,
    preserve_special_mode_bits: bool,
) -> None:
    if summary.command != "extract":
        raise ContractError("only extract streams contain a reassemblable artifact envelope")
    if not summary.success:
        raise ContractError("refusing to reassemble a stream whose footer.success is false")
    if destination.exists() or destination.is_symlink():
        raise ContractError(f"output path already exists: {destination}")
    symlinks = [artifact for artifact in summary.artifacts if artifact.file_type == "symlink"]
    if symlinks and not allow_symlinks:
        raise ContractError(
            "stream contains symbolic links; review it and pass --allow-symlinks to recreate safe relative links"
        )
    for artifact in symlinks:
        safe_symlink_target(artifact)

    destination = destination.absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(
        tempfile.mkdtemp(prefix=f".{destination.name}.reassemble-", dir=destination.parent)
    )
    stage_bytes = os.fsencode(stage)
    artifact_by_id = {artifact.artifact_id: artifact for artifact in summary.artifacts}
    directory_modes: list[tuple[bytes, int]] = []
    active_id: int | None = None
    file_descriptor: int | None = None
    try:
        with source.open("rb") as input_stream:
            for line_number, raw_line in enumerate(input_stream, start=1):
                record = parse_json_object(raw_line[:-1], line_number)
                record_type = record["type"]
                if record_type == "artifact_begin":
                    active_id = int(record["artifact_id"])
                    artifact = artifact_by_id[active_id]
                    output_path = artifact_path(stage_bytes, artifact)
                    ensure_parent(output_path)
                    if artifact.file_type == "directory":
                        os.makedirs(output_path, mode=0o700, exist_ok=False)
                        directory_modes.append((output_path, artifact.mode))
                    elif artifact.file_type == "regular":
                        flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY
                        if hasattr(os, "O_NOFOLLOW"):
                            flags |= os.O_NOFOLLOW
                        file_descriptor = os.open(output_path, flags, 0o600)
                    else:
                        assert artifact.target is not None
                        os.symlink(artifact.target, output_path)
                elif record_type == "artifact_chunk":
                    if file_descriptor is None or active_id != int(record["artifact_id"]):
                        raise ContractError("validated artifact state changed during reassembly")
                    _write_all(
                        file_descriptor,
                        decode_byte_value(record["data"], "artifact_chunk.data", line_number),
                    )
                elif record_type == "artifact_end":
                    artifact_id = int(record["artifact_id"])
                    artifact = artifact_by_id[artifact_id]
                    if file_descriptor is not None:
                        os.fsync(file_descriptor)
                        mask = 0o7777 if preserve_special_mode_bits else 0o777
                        os.fchmod(file_descriptor, artifact.mode & mask)
                        os.close(file_descriptor)
                        file_descriptor = None
                    active_id = None
        mask = 0o7777 if preserve_special_mode_bits else 0o777
        for directory, mode in sorted(directory_modes, key=lambda item: item[0].count(b"/"), reverse=True):
            os.chmod(directory, mode & mask, follow_symlinks=False)
        os.replace(stage, destination)
        parent_descriptor = os.open(destination.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(parent_descriptor)
        finally:
            os.close(parent_descriptor)
    except Exception:
        if file_descriptor is not None:
            os.close(file_descriptor)
        shutil.rmtree(stage, ignore_errors=True)
        raise


def summary_text(summary: ValidationSummary) -> str:
    return (
        f"valid {CONTRACT}: command={summary.command}, records={summary.records}, "
        f"modules={summary.modules}, entries={summary.entries}, "
        f"artifacts={len(summary.artifacts)}, artifact_bytes={summary.artifact_bytes}, "
        f"success={str(summary.success).lower()}"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="getbiblesword-v1",
        description="Independently validate and reassemble getBibleSword NDJSON v1 streams.",
    )
    parser.add_argument(
        "--max-record-bytes",
        type=int,
        default=DEFAULT_MAX_RECORD_BYTES,
        help=f"maximum bytes accepted in one NDJSON record (default: {DEFAULT_MAX_RECORD_BYTES})",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate_parser = subparsers.add_parser("validate", help="validate a stream without writing files")
    validate_parser.add_argument("input", help="NDJSON file, or - for standard input")
    validate_parser.add_argument("--json", action="store_true", help="emit a JSON validation summary")
    reassemble_parser = subparsers.add_parser(
        "reassemble", help="validate, then atomically recreate the artifact tree"
    )
    reassemble_parser.add_argument("input", help="NDJSON file, or - for standard input")
    reassemble_parser.add_argument("output", type=Path, help="new output directory")
    reassemble_parser.add_argument(
        "--allow-symlinks",
        action="store_true",
        help="recreate safe relative symbolic links after explicit review",
    )
    reassemble_parser.add_argument(
        "--preserve-special-mode-bits",
        action="store_true",
        help="also restore setuid, setgid, and sticky mode bits",
    )
    return parser


def main(arguments: list[str] | None = None) -> int:
    parser = build_parser()
    options = parser.parse_args(arguments)
    try:
        with local_input(options.input) as source:
            summary = validate_path(source, options.max_record_bytes)
            if options.command == "validate":
                if options.json:
                    print(
                        json.dumps(
                            {
                                "artifact_bytes": summary.artifact_bytes,
                                "artifacts": len(summary.artifacts),
                                "command": summary.command,
                                "contract": CONTRACT,
                                "diagnostics": summary.diagnostics,
                                "entries": summary.entries,
                                "modules": summary.modules,
                                "producer_version": summary.producer_version,
                                "records": summary.records,
                                "stream_sha256": summary.stream_sha256,
                                "success": summary.success,
                                "sword_version": summary.sword_version,
                            },
                            sort_keys=True,
                            separators=(",", ":"),
                        )
                    )
                else:
                    print(summary_text(summary))
            else:
                reassemble(
                    source,
                    options.output,
                    summary,
                    allow_symlinks=options.allow_symlinks,
                    preserve_special_mode_bits=options.preserve_special_mode_bits,
                )
                print(f"reassembled {len(summary.artifacts)} artifacts into {options.output}")
    except (ContractError, OSError) as exception:
        print(f"getbiblesword-v1: {exception}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
