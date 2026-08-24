from datetime import datetime, timedelta, timezone
import unittest

from software.app import main


class FakeDriver:
    def __init__(self, reading=None, exc=None, info_uid="bb-prj-air-001"):
        self.reading = reading
        self.exc = exc
        self.info_uid = info_uid

    def get_info(self):
        return {
            "uid": self.info_uid,
            "source_type": "fake",
            "transport": "serial",
            "protocol": "bardbox",
            "firmware": None,
        }

    def get_capabilities(self):
        return {
            "channels": {
                "temp_c": {"label": "Temperature", "unit": "°C"},
                "rh_pct": {"label": "Relative Humidity", "unit": "%"},
            },
            "raw_available": False,
        }

    def get_reading(self):
        if self.exc:
            raise self.exc
        return self.reading


def iso(dt):
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def ok_reading(timestamp=None):
    return {
        "uid": "bb-prj-air-001",
        "timestamp": timestamp or iso(datetime.now(timezone.utc)),
        "status": "ok",
        "message": "Fresh valid reading",
        "data": {
            "temp_c": 21.5,
            "rh_pct": 43.0,
        },
        "extended": {},
        "raw": None,
    }


class NodeFreshnessTests(unittest.TestCase):
    def setUp(self):
        main.NODE_STATE.clear()
        main.NODE_STALE_AFTER_S = 15.0
        self._uid_aliases = dict(main.UID_ALIASES)

    def tearDown(self):
        main.UID_ALIASES.clear()
        main.UID_ALIASES.update(self._uid_aliases)
    def test_fresh_reading_returns_ok(self):
        reading = main.normalize_driver_reading(FakeDriver(ok_reading()))
        self.assertEqual(reading["status"], "ok")
        self.assertEqual(reading["data"]["temp_c"], 21.5)
        self.assertEqual(reading["extended"]["last_seen"], reading["timestamp"])

    def test_no_device_response_returns_node_unavailable_with_null_values(self):
        reading = main.normalize_driver_reading(FakeDriver(exc=TimeoutError("no response")))
        self.assertEqual(reading["status"], "node_unavailable")
        self.assertEqual(reading["message"], "no response")
        self.assertEqual(reading["data"], {"temp_c": None, "rh_pct": None})

    def test_parse_failure_returns_error_with_null_values(self):
        reading = main.normalize_driver_reading(FakeDriver(exc=ValueError("bad DAT line")))
        self.assertEqual(reading["status"], "error")
        self.assertEqual(reading["message"], "bad DAT line")
        self.assertEqual(reading["data"], {"temp_c": None, "rh_pct": None})

    def test_stale_timeout_returns_stale_with_null_values(self):
        old_timestamp = iso(datetime.now(timezone.utc) - timedelta(seconds=60))
        reading = main.normalize_driver_reading(FakeDriver(ok_reading(old_timestamp)))
        self.assertEqual(reading["status"], "stale")
        self.assertEqual(reading["data"], {"temp_c": None, "rh_pct": None})
        self.assertEqual(reading["extended"]["last_seen"], old_timestamp)

    def test_driver_stale_status_returns_stale_with_null_values(self):
        stale = ok_reading()
        stale["status"] = "stale"
        stale["message"] = "Buffered reading is stale"
        reading = main.normalize_driver_reading(FakeDriver(stale))
        self.assertEqual(reading["status"], "stale")
        self.assertEqual(reading["data"], {"temp_c": None, "rh_pct": None})

    def test_cached_reading_is_not_presented_as_live_after_failure(self):
        driver = FakeDriver(ok_reading())
        live = main.normalize_driver_reading(driver)
        self.assertEqual(live["status"], "ok")

        driver.reading = None
        driver.exc = TimeoutError("node disconnected")
        unavailable = main.normalize_driver_reading(driver)
        self.assertEqual(unavailable["status"], "node_unavailable")
        self.assertEqual(unavailable["data"], {"temp_c": None, "rh_pct": None})
        self.assertEqual(unavailable["extended"]["last_seen"], live["timestamp"])

    def test_legacy_reported_uid_is_normalized_to_canonical_uid(self):
        main.register_uid_alias("bb-0001", "bb-gol-air-001")
        legacy_reading = ok_reading()
        legacy_reading["uid"] = "bb-0001"

        reading = main.normalize_driver_reading(FakeDriver(legacy_reading, info_uid="bb-0001"))
        self.assertEqual(reading["uid"], "bb-gol-air-001")
        self.assertEqual(reading["status"], "ok")

    def test_arbitrary_legacy_reported_uid_is_normalized_to_canonical_uid(self):
        main.register_uid_alias("rkc-01", "bb-rkc-frz-001")
        legacy_reading = ok_reading()
        legacy_reading["uid"] = "rkc-01"

        reading = main.normalize_driver_reading(FakeDriver(legacy_reading, info_uid="rkc-01"))
        self.assertEqual(reading["uid"], "bb-rkc-frz-001")


if __name__ == "__main__":
    unittest.main()
