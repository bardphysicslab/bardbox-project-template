"""Local diagnostic test receiver: SYNTHETIC local checks only.

These exercise the normalized diagnostic test format, not firmware wire or
hardware/API conformance.
"""
import asyncio
import json
import os
import subprocess
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI
from fastapi.templating import Jinja2Templates

from software.app.main import is_valid_node_uid
from software.app.local_receiver import (MAX_BODY_BYTES, MAX_RESULTS, Registry,
                                         create_local_receiver_router, validate_test_sample)

ROOT = Path(__file__).resolve().parents[1]
UID = "bb-prj-air-001"
NOW = datetime(2026, 10, 20, 16, 0, 1, tzinfo=timezone.utc)
REGISTRY = Registry(channels={UID: ("temp_c", "rh_pct", "press_pa")}, uid_syntax_ok=is_valid_node_uid,
                    aliases={"bb-0001": UID})
META = {"firmware_version": "1.0.0", "protocol_version": "test"}


def sample(**overrides):
    body = {"uid": UID, "timestamp": "2026-10-20T16:00:00Z",
            "data": {"temp_c": 21.6, "rh_pct": 43.0, "press_pa": 100980}, "extended": dict(META)}
    body.update(overrides)
    return json.dumps(body).encode()


def validate(raw):
    return validate_test_sample(raw, REGISTRY, NOW)


class ValidatorTests(unittest.TestCase):
    def test_valid_sample_is_not_an_ack_and_protocol_is_unchecked(self):
        status, result = validate(sample(extended={**META, "record_id": "t-1"}))
        self.assertEqual(status, 200)
        self.assertEqual(result["result"], "valid")
        self.assertEqual((result["test_only"], result["durable_ack"], result["archived"]), (True, False, False))
        self.assertEqual(result["identity"], {"claimed_uid": UID, "uid_syntax_valid": True,
                                              "uid_configured": True, "authenticated": False})
        record = result["record"]
        self.assertEqual(record["data"], {"temp_c": 21.6, "rh_pct": 43.0, "press_pa": 100980})
        self.assertEqual(record["extended"]["protocol_check"], "unchecked")
        self.assertEqual(record["extended"]["record_id"], "t-1")
        self.assertEqual(result["received_at"], "2026-10-20T16:00:01Z")

    def test_partial_channels_keep_healthy_values(self):
        status, result = validate(sample(data={"temp_c": 21.6, "rh_pct": "43", "co2": 600, "<b>": 1}))
        self.assertEqual(status, 200)
        self.assertEqual(result["result"], "valid_with_channel_faults")
        self.assertEqual(result["record"]["data"], {"temp_c": 21.6, "rh_pct": None, "press_pa": None})
        self.assertEqual(result["record"]["extended"]["channel_faults"],
                         {"rh_pct": {"code": "invalid_type"}, "press_pa": {"code": "missing"}})
        self.assertEqual(result["ignored_channels"], ["(not shown)", "co2"])
        self.assertEqual(result["record"]["status"], "error")

    def test_boolean_is_not_a_number(self):
        _, result = validate(sample(data={"temp_c": True, "rh_pct": 1, "press_pa": 2}))
        self.assertEqual(result["record"]["extended"]["channel_faults"], {"temp_c": {"code": "invalid_type"}})

    def test_oversized_numbers_are_channel_faults_not_server_errors(self):
        cases = {"1" + "0" * 400: "not_representable", "9" * 5000: "not_representable", "1e400": "non_finite"}
        for literal, code in cases.items():
            with self.subTest(literal=literal[:10]):
                raw = sample().replace(b"21.6", literal.encode())
                self.assertLessEqual(len(raw), MAX_BODY_BYTES)
                status, result = validate(raw)
                self.assertEqual(status, 200)
                self.assertEqual(result["record"]["data"]["temp_c"], None)
                self.assertEqual(result["record"]["extended"]["channel_faults"]["temp_c"], {"code": code})

    def test_measurement_time_is_preserved_or_unknown_never_receipt_time(self):
        _, result = validate(sample(timestamp="2026-10-20T16:00:00.123456Z"))
        self.assertEqual(result["record"]["timestamp"], "2026-10-20T16:00:00.123456Z")
        for value, fault in ((None, "missing"), ("yesterday", "invalid"), ("2026-10-20T16:00:00+02:00", "invalid"), (5, "invalid")):
            with self.subTest(value=value):
                _, result = validate(sample(timestamp=value))
                self.assertIsNone(result["record"]["timestamp"])
                self.assertEqual(result["record"]["extended"]["time_fault"], fault)
                self.assertEqual(result["record"]["extended"]["time_source"], "unknown")

    def test_alias_reports_original_uid(self):
        _, result = validate(sample(uid="bb-0001"))
        self.assertEqual(result["record"]["uid"], UID)
        self.assertEqual(result["record"]["extended"]["reported_uid"], "bb-0001")

    def test_rejections_are_structured(self):
        cases = [
            (b"[1]", 400, [{"code": "malformed_message", "path": "record"}]),
            (b'{"uid": "a", "uid": "b"}', 400, [{"code": "malformed_message", "path": "record"}]),
            (b'{"uid": NaN}', 400, [{"code": "malformed_message", "path": "record"}]),
            (b"\xff\xfe", 400, [{"code": "malformed_message", "path": "record"}]),
            (b"[" * 5000, 400, [{"code": "malformed_message", "path": "record"}]),
            (sample(uid="bb-xyz-air-999"), 403, [{"code": "identity_mismatch", "path": "uid"}]),
            (sample(uid="not a uid"), 422, [{"code": "invalid_metadata", "path": "uid"}]),
            (sample(extended={"protocol_version": "test"}), 422,
             [{"code": "missing_metadata", "path": "extended.firmware_version"}]),
            (sample(extended={**META, "firmware_version": "unknown"}), 422,
             [{"code": "invalid_metadata", "path": "extended.firmware_version"}]),
            (sample(extended={**META, "record_id": "-bad"}), 422,
             [{"code": "invalid_metadata", "path": "extended.record_id"}]),
            (sample(data=[1]), 400, [{"code": "malformed_message", "path": "data"}]),
        ]
        for raw, status, errors in cases:
            with self.subTest(raw=raw[:30]):
                got_status, result = validate(raw)
                self.assertEqual((got_status, result["errors"], result["record"]), (status, errors, None))
                self.assertFalse(result["durable_ack"])

    def test_missing_uid_and_metadata_are_reported_together_in_order(self):
        status, result = validate(json.dumps({"data": {}}).encode())
        self.assertEqual(status, 422)
        self.assertEqual([e["path"] for e in result["errors"]],
                         ["extended.firmware_version", "extended.protocol_version", "uid"])

    def test_unknown_metadata_is_never_echoed(self):
        raw = sample(extended={**META, "token": "s3cret-value"}, password="s3cret-value")
        for status, result in (validate(raw), validate(raw.replace(b'"uid": "bb-prj-air-001"', b'"uid": "s3cret-value"'))):
            self.assertNotIn("s3cret-value", json.dumps(result))


class RouterTests(unittest.TestCase):
    def setUp(self):
        self.app = FastAPI()
        templates = Jinja2Templates(directory=str(ROOT / "software" / "app" / "templates"))
        self.router = create_local_receiver_router(REGISTRY, templates, clock=lambda: NOW)
        self.app.include_router(self.router)

    def call(self, method, path, body=b"", headers=None, client=("127.0.0.1", 50000)):
        messages, sent = [], False

        async def receive():
            nonlocal sent
            if not sent:
                sent = True
                return {"type": "http.request", "body": body, "more_body": False}
            return {"type": "http.disconnect"}

        async def send(message):
            messages.append(message)

        scope = {"type": "http", "asgi": {"version": "3.0"}, "http_version": "1.1", "method": method,
                 "scheme": "https", "path": path, "raw_path": path.encode(), "query_string": b"",
                 "headers": [(k.lower().encode(), v.encode()) for k, v in (headers or {}).items()],
                 "client": client, "server": ("127.0.0.1", 8443), "root_path": ""}
        asyncio.run(self.app(scope, receive, send))
        start = next(m for m in messages if m["type"] == "http.response.start")
        payload = b"".join(m.get("body", b"") for m in messages if m["type"] == "http.response.body")
        return start["status"], {k.decode(): v.decode() for k, v in start["headers"]}, payload

    def post(self, body, headers=None, **kwargs):
        return self.call("POST", "/dev/test-receiver/samples", body,
                         {"x-bardbox-test": "1", "content-type": "application/json", **(headers or {})}, **kwargs)

    def test_valid_post_returns_result_with_security_headers(self):
        status, headers, payload = self.post(sample())
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(payload)["result"], "valid")
        self.assertEqual(headers["cache-control"], "no-store")
        self.assertEqual(headers["x-content-type-options"], "nosniff")
        self.assertIn("strict-transport-security", headers)
        self.assertIn("default-src 'none'", headers["content-security-policy"])

    def test_missing_test_header_is_non_2xx_and_not_recorded(self):
        for value in (None, "true", "0"):
            with self.subTest(value=value):
                status, _, payload = self.call("POST", "/dev/test-receiver/samples", sample(),
                                               {} if value is None else {"x-bardbox-test": value})
                self.assertEqual(status, 400)
                self.assertEqual(json.loads(payload)["errors"], [{"code": "test_header_required", "path": "request"}])
        self.assertEqual(self.router.snapshot.items(), [])

    def test_non_loopback_clients_are_refused(self):
        for client in (("10.0.0.5", 1), ("192.168.1.2", 1), None):
            for method, path in (("POST", "/dev/test-receiver/samples"), ("GET", "/dev/test-receiver/samples"),
                                 ("GET", "/dev/test-receiver")):
                with self.subTest(client=client, path=path, method=method):
                    status, _, payload = self.call(method, path, sample(), {"x-bardbox-test": "1"}, client=client)
                    self.assertEqual(status, 403)
                    self.assertEqual(json.loads(payload)["errors"][0]["code"], "not_local")
        self.assertEqual(self.router.snapshot.items(), [])

    def test_oversize_body_is_refused_before_parsing(self):
        big = b" " * (MAX_BODY_BYTES + 1)
        self.assertEqual(self.post(big)[0], 413)
        self.assertEqual(self.post(sample(), headers={"content-length": str(MAX_BODY_BYTES + 1)})[0], 413)
        self.assertEqual(self.post(sample(), headers={"content-length": "abc"})[0], 413)
        self.assertEqual(self.router.snapshot.items(), [])

    def test_results_are_bounded_newest_first(self):
        for index in range(MAX_RESULTS + 5):
            self.post(sample(extended={**META, "record_id": f"r-{index}"}))
        status, _, payload = self.call("GET", "/dev/test-receiver/samples")
        results = json.loads(payload)["results"]
        self.assertEqual(status, 200)
        self.assertEqual(len(results), MAX_RESULTS)
        self.assertEqual(results[0]["record"]["extended"]["record_id"], f"r-{MAX_RESULTS + 4}")

    def test_view_escapes_received_text(self):
        self.post(sample(extended={**META, "firmware_version": "<script>alert(1)</script>"}))
        self.post(b"not json")
        status, headers, page = self.call("GET", "/dev/test-receiver")
        html = page.decode()
        self.assertEqual(status, 200)
        self.assertNotIn("<script>alert(1)</script>", html)
        self.assertIn("&lt;script&gt;alert(1)&lt;/script&gt;", html)
        self.assertIn("malformed_message", html)
        self.assertIn("unchecked", html)
        self.assertIn("authenticated: False", html)
        self.assertEqual(headers["cache-control"], "no-store")


class AppWiringTests(unittest.TestCase):
    """Runs in a subprocess so the module-level app is built with the flag set."""

    SCRIPT = r'''
import asyncio, json, os, sys
from software.app import main
paths = sorted(r.path for r in main.app.routes if "test-receiver" in getattr(r, "path", ""))
before = (json.dumps(main.NODE_STATE, sort_keys=True),
          sorted(str(p) for p in main.READINGS_DATA_DIR.rglob("*")) if main.READINGS_DATA_DIR.exists() else None)
async def run(body):
    out = []
    async def receive():
        return {"type": "http.request", "body": body, "more_body": False}
    async def send(m):
        out.append(m)
    await main.app({"type": "http", "asgi": {"version": "3.0"}, "http_version": "1.1", "method": "POST",
                    "scheme": "https", "path": "/dev/test-receiver/samples", "raw_path": b"/dev/test-receiver/samples",
                    "query_string": b"", "headers": [(b"x-bardbox-test", b"1")], "client": ("127.0.0.1", 1),
                    "server": ("127.0.0.1", 8443), "root_path": ""}, receive, send)
    return next(m for m in out if m["type"] == "http.response.start")["status"]
status = asyncio.run(run(json.dumps({"uid": "bb-prj-air-001", "data": {"temp_c": 1},
                                     "extended": {"firmware_version": "1", "protocol_version": "t"}}).encode())) if paths else None
after = (json.dumps(main.NODE_STATE, sort_keys=True),
         sorted(str(p) for p in main.READINGS_DATA_DIR.rglob("*")) if main.READINGS_DATA_DIR.exists() else None)
print(json.dumps({"paths": paths, "status": status, "unchanged": before == after}))
'''

    def run_app(self, flag):
        env = {k: v for k, v in os.environ.items() if k != "BARDBOX_TEST_RECEIVER"}
        if flag is not None:
            env["BARDBOX_TEST_RECEIVER"] = flag
        return subprocess.run([sys.executable, "-c", self.SCRIPT], cwd=ROOT, env=env, capture_output=True, text=True)

    def test_routes_absent_by_default(self):
        for flag in (None, "0"):
            with self.subTest(flag=flag):
                run = self.run_app(flag)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(json.loads(run.stdout)["paths"], [])

    def test_enabled_receiver_leaves_live_state_and_archive_untouched(self):
        run = self.run_app("1")
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        self.assertEqual(result["paths"], ["/dev/test-receiver", "/dev/test-receiver/samples", "/dev/test-receiver/samples"])
        self.assertEqual(result["status"], 200)
        self.assertTrue(result["unchanged"])

    def test_unknown_flag_value_fails_closed(self):
        run = self.run_app("yes")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("BARDBOX_TEST_RECEIVER must be unset, 0 or 1", run.stderr)


if __name__ == "__main__":
    unittest.main()
