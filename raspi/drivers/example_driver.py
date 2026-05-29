import math
import time
from datetime import datetime, timezone
from typing import Any, Dict, Optional


class ExampleDriver:
    def __init__(self, uid: str = "bb-prj-air-001", config: Optional[Dict[str, Any]] = None):
        self.uid = uid
        self.config = config or {}
        self._started_at = time.time()
        self._last_valid_reading: Optional[Dict[str, Any]] = None

    def get_info(self) -> dict:
        return {
            "uid": self.config.get("reported_uid", self.uid),
            "source_type": "example_sensor",
            "transport": "serial",
            "protocol": "example",
            "firmware": None,
        }

    def get_capabilities(self) -> dict:
        return {
            "channels": {
                "temp_c": {"label": "Temperature", "unit": "°C"},
                "rh_pct": {"label": "Relative Humidity", "unit": "%"},
                "press_pa": {"label": "Pressure", "unit": "Pa"},
            },
            "raw_available": False,
        }

    def get_reading(self) -> dict:
        simulated_status = self.config.get("simulate_status", "ok")
        if simulated_status == "node_unavailable":
            raise TimeoutError("Node unavailable")
        if simulated_status == "error":
            return {
                "uid": self.config.get("reported_uid", self.uid),
                "timestamp": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                "status": "error",
                "message": "Malformed example payload",
                "data": {
                    "temp_c": None,
                    "rh_pct": None,
                    "press_pa": None,
                },
                "extended": {},
                "raw": "ERR MALFORMED",
            }

        elapsed = time.time() - self._started_at
        temp_c = 21.6 + math.sin(elapsed / 18.0) * 0.8
        rh_pct = 43.0 + math.sin(elapsed / 24.0) * 2.5
        press_pa = 100980 + math.sin(elapsed / 30.0) * 45
        timestamp = self.config.get("fixed_timestamp") or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

        reading = {
            "uid": self.config.get("reported_uid", self.uid),
            "timestamp": timestamp,
            "status": "ok",
            "message": "Fresh valid reading",
            "data": {
                "temp_c": round(temp_c, 2),
                "rh_pct": round(rh_pct, 2),
                "press_pa": int(round(press_pa)),
            },
            "extended": {
                "note": "Example driver reading",
            },
            "raw": None,
        }
        self._last_valid_reading = reading
        return reading
