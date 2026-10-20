"""Local diagnostic test receiver (development only, off by default).

Accepts one documented *normalized diagnostic test sample* per request. This is a
test format for inspecting what arrived; it is not a firmware wire protocol,
production Ingestion or Archive. Results live only in a small in-process buffer:
nothing is written to disk, to the readings archive or to the live
``/readings/latest`` state, and no response is a durable archive ACK.

Enable with ``BARDBOX_TEST_RECEIVER=1`` (see software/app/README.md).
"""
import json
import math
import re
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Callable, Dict, List, Mapping, Optional, Tuple

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse

MAX_BODY_BYTES = 16 * 1024
MAX_RESULTS = 20
MAX_DATA_KEYS = 64
MAX_IGNORED_SHOWN = 10
MAX_TEXT = 64
TEST_HEADER = "x-bardbox-test"
LOOPBACK_HOSTS = frozenset({"127.0.0.1", "::1"})
RECORD_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
SAFE_TEXT_RE = re.compile(r"^[\x21-\x7e][\x20-\x7e]{0,63}$")
SAFE_NAME_RE = re.compile(r"^[A-Za-z0-9_.:-]{1,64}$")
FORMAT_LABEL = "normalized diagnostic test sample (not a firmware wire protocol)"

SECURITY_HEADERS = {
    # Short max-age: this is a loopback development tool with a self-signed
    # certificate, not a Bard-facing site.
    "Strict-Transport-Security": "max-age=300",
    "Content-Security-Policy": (
        "default-src 'none'; style-src 'self'; img-src 'self'; "
        "frame-ancestors 'none'; base-uri 'none'; form-action 'none'"
    ),
    "X-Content-Type-Options": "nosniff",
    "Cache-Control": "no-store",
    "X-Frame-Options": "DENY",
}


@dataclass(frozen=True)
class Registry:
    """Locally configured identities and channels; nothing here is authenticated."""

    channels: Mapping[str, Tuple[str, ...]]
    uid_syntax_ok: Callable[[str], bool]  # required: no permissive default
    aliases: Mapping[str, str] = field(default_factory=dict)


class _Malformed(ValueError):
    pass


class _UnrepresentableInt:
    """Stands in for a JSON integer beyond the parser's digit limit."""


def _parse_int(text: str) -> Any:
    try:
        return int(text)
    except ValueError:
        return _UnrepresentableInt()


def _reject_constant(token: str) -> Any:
    raise _Malformed(f"non-JSON constant {token}")


def _no_duplicate_keys(pairs: List[Tuple[str, Any]]) -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _Malformed("duplicate key")
        result[key] = value
    return result


def _parse(raw: bytes) -> Dict[str, Any]:
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_no_duplicate_keys,
                           parse_constant=_reject_constant, parse_int=_parse_int)
    except (UnicodeDecodeError, ValueError, RecursionError):
        raise _Malformed("undecodable") from None
    if not isinstance(value, dict):
        raise _Malformed("not one JSON object")
    return value


def _safe_text(value: Any) -> Optional[str]:
    return value if isinstance(value, str) and SAFE_TEXT_RE.fullmatch(value) else None


def _utc(value: datetime) -> str:
    return value.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _measurement_time(value: Any) -> Tuple[Optional[str], Optional[str]]:
    """Return (timestamp, time_fault). Never falls back to receipt time.

    A valid UTC timestamp is kept exactly as sent, so fractional seconds and the
    original precision are preserved.
    """
    if value is None:
        return None, "missing"
    if not isinstance(value, str) or not value.endswith("Z") or len(value) > 40:
        return None, "invalid"
    try:
        datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError:
        return None, "invalid"
    return value, None


def _channel_fault(value: Any) -> Optional[str]:
    """Return a channel fault code, or None for a usable finite number."""
    if isinstance(value, _UnrepresentableInt):
        return "not_representable"
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return "invalid_type"
    if isinstance(value, int):
        try:
            float(value)
        except OverflowError:
            return "not_representable"
        return None
    return None if math.isfinite(value) else "non_finite"


def _metadata(extended: Dict[str, Any], name: str, errors: List[Dict[str, str]]) -> Optional[str]:
    path = f"extended.{name}"
    if name not in extended:
        errors.append({"code": "missing_metadata", "path": path})
        return None
    text = _safe_text(extended[name])
    if text is None or text.lower() == "unknown":
        errors.append({"code": "invalid_metadata", "path": path})
        return None
    return text


def validate_test_sample(raw: bytes, registry: Registry, received_at: datetime) -> Tuple[int, Dict[str, Any]]:
    """Validate one diagnostic test sample; returns (HTTP status, allowlisted result)."""
    result: Dict[str, Any] = {
        "test_only": True,
        "durable_ack": False,
        "archived": False,
        "format": FORMAT_LABEL,
        "received_at": _utc(received_at),
        "identity": {"claimed_uid": None, "uid_syntax_valid": False,
                     "uid_configured": False, "authenticated": False},
        "result": "rejected",
        "record": None,
        "errors": [],
    }
    errors: List[Dict[str, str]] = []
    try:
        body = _parse(raw)
    except _Malformed:
        result["errors"] = [{"code": "malformed_message", "path": "record"}]
        return 400, result

    extended = body.get("extended", {})
    data = body.get("data")
    if not isinstance(extended, dict):
        errors.append({"code": "malformed_message", "path": "extended"})
        extended = {}
    if not isinstance(data, dict) or len(data) > MAX_DATA_KEYS:
        errors.append({"code": "malformed_message", "path": "data"})
        data = {}

    uid = body.get("uid")
    identity = result["identity"]
    canonical = None
    if "uid" not in body:
        errors.append({"code": "missing_metadata", "path": "uid"})
    elif not isinstance(uid, str) or not SAFE_NAME_RE.fullmatch(uid) or not registry.uid_syntax_ok(uid):
        errors.append({"code": "invalid_metadata", "path": "uid"})
    else:
        identity["claimed_uid"] = uid
        identity["uid_syntax_valid"] = True
        canonical = registry.aliases.get(uid, uid)
        if canonical in registry.channels:
            identity["uid_configured"] = True
        else:
            errors.append({"code": "identity_mismatch", "path": "uid"})

    firmware = _metadata(extended, "firmware_version", errors)
    protocol = _metadata(extended, "protocol_version", errors)
    record_id = extended.get("record_id")
    if record_id is not None and not (isinstance(record_id, str) and RECORD_ID_RE.fullmatch(record_id)):
        errors.append({"code": "invalid_metadata", "path": "extended.record_id"})

    if errors:
        result["errors"] = sorted(errors, key=lambda e: (e["path"], e["code"]))
        codes = {e["code"] for e in errors}
        status = 400 if "malformed_message" in codes else 403 if codes == {"identity_mismatch"} else 422
        return status, result

    channel_values: Dict[str, Any] = {}
    channel_faults: Dict[str, Dict[str, str]] = {}
    registered = registry.channels[canonical]
    for channel in registered:
        if channel not in data:
            channel_values[channel] = None
            channel_faults[channel] = {"code": "missing"}
            continue
        fault = _channel_fault(data[channel])
        channel_values[channel] = None if fault else data[channel]
        if fault:
            channel_faults[channel] = {"code": fault}
    ignored = sorted(name for name in data if name not in registered)
    timestamp, time_fault = _measurement_time(body.get("timestamp"))

    extended_out: Dict[str, Any] = {
        "firmware_version": firmware,
        "protocol_version": protocol,
        "protocol_check": "unchecked",
        "time_source": "device" if timestamp else "unknown",
        "time_fault": time_fault,
        "channel_faults": channel_faults,
    }
    if record_id is not None:
        extended_out["record_id"] = record_id
    if canonical != uid:
        extended_out["reported_uid"] = uid
    result["record"] = {
        "uid": canonical,
        "timestamp": timestamp,
        "status": "error" if channel_faults else "ok",
        "data": channel_values,
        "extended": extended_out,
    }
    result["ignored_channels"] = [n if SAFE_NAME_RE.fullmatch(n) else "(not shown)" for n in ignored[:MAX_IGNORED_SHOWN]]
    result["ignored_channel_count"] = len(ignored)
    result["result"] = "valid_with_channel_faults" if channel_faults else "valid"
    return 200, result


class ResultBuffer:
    """Bounded, process-local list of recent results; cleared on restart."""

    def __init__(self, maxlen: int = MAX_RESULTS):
        self._results: deque = deque(maxlen=maxlen)

    def add(self, status: int, result: Dict[str, Any]) -> None:
        self._results.appendleft({"http_status": status, **result})

    def items(self) -> List[Dict[str, Any]]:
        return list(self._results)


def _is_local(request: Request) -> bool:
    return bool(request.client) and request.client.host in LOOPBACK_HOSTS


def _error(status: int, code: str, path: str) -> JSONResponse:
    body = {"test_only": True, "durable_ack": False, "archived": False,
            "errors": [{"code": code, "path": path}]}
    return JSONResponse(body, status, headers=SECURITY_HEADERS)


async def _read_bounded(request: Request) -> Optional[bytes]:
    declared = request.headers.get("content-length")
    if declared is not None and (not declared.isdigit() or int(declared) > MAX_BODY_BYTES):
        return None
    chunks, size = [], 0
    async for chunk in request.stream():
        size += len(chunk)
        if size > MAX_BODY_BYTES:
            return None
        chunks.append(chunk)
    return b"".join(chunks)


def create_local_receiver_router(registry: Registry, templates: Any,
                                clock: Callable[[], datetime] = lambda: datetime.now(timezone.utc)) -> APIRouter:
    router = APIRouter(prefix="/dev/test-receiver", tags=["dev-test-receiver"])
    snapshot = ResultBuffer()

    @router.post("/samples")
    async def receive_sample(request: Request):
        if not _is_local(request):
            return _error(403, "not_local", "request")
        # Existing archival senders treat any 2xx as an ACK; requiring this header
        # means a node accidentally pointed here gets a non-2xx and keeps its data.
        if request.headers.get(TEST_HEADER) != "1":
            return _error(400, "test_header_required", "request")
        raw = await _read_bounded(request)
        if raw is None:
            return _error(413, "payload_too_large", "record")
        status, result = validate_test_sample(raw, registry, clock())
        snapshot.add(status, result)
        return JSONResponse(result, status, headers=SECURITY_HEADERS)

    @router.get("/samples")
    def list_samples(request: Request):
        if not _is_local(request):
            return _error(403, "not_local", "request")
        return JSONResponse({"test_only": True, "results": snapshot.items()}, headers=SECURITY_HEADERS)

    @router.get("")
    def view(request: Request):
        if not _is_local(request):
            return _error(403, "not_local", "request")
        response = templates.TemplateResponse(request, "local_receiver.html", {
            "results": snapshot.items(), "format_label": FORMAT_LABEL, "max_results": MAX_RESULTS})
        response.headers.update(SECURITY_HEADERS)
        return response

    router.snapshot = snapshot  # exposed for tests
    return router
