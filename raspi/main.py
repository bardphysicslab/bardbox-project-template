import json
import os
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from raspi.drivers.example_driver import ExampleDriver
from raspi.data_api import create_data_api_router


BASE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = BASE_DIR.parent
STATIC_DIR = BASE_DIR / "static"
TEMPLATES_DIR = BASE_DIR / "templates"
READINGS_DATA_DIR = PROJECT_ROOT / "data" / "readings"
DEFAULT_CONFIG_PATH = BASE_DIR / "config" / "app_config.example.json"
APP_CONFIG_PATH = BASE_DIR / "config" / "app_config.json"

app = FastAPI(title="Bard Box Project Template")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")
templates = Jinja2Templates(directory=str(TEMPLATES_DIR))


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def local_now() -> datetime:
    return utc_now().astimezone()


def load_config() -> Dict[str, Any]:
    default_path = APP_CONFIG_PATH if APP_CONFIG_PATH.exists() else DEFAULT_CONFIG_PATH
    config_path = Path(os.environ.get("BARDBOX_APP_CONFIG", default_path))
    with config_path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


APP_CONFIG = load_config()
app.include_router(create_data_api_router(READINGS_DATA_DIR, APP_CONFIG))
NODE_STALE_AFTER_S = float(APP_CONFIG.get("node_stale_after_s", 15))
NODE_STATE: Dict[str, Dict[str, Any]] = {}
DRIVER_UIDS: Dict[int, str] = {}
UID_ALIASES: Dict[str, str] = {}
NODE_UID_RE = re.compile(r"^bb-[a-z]{3}-[a-z]{3}-[0-9]{3}$")
LEGACY_NODE_UID_RE = re.compile(r"^bb-[0-9]{4}$")


def is_valid_new_node_uid(uid: str) -> bool:
    return bool(NODE_UID_RE.fullmatch(uid or ""))


def is_valid_node_uid(uid: str, allow_legacy: bool = True) -> bool:
    if is_valid_new_node_uid(uid):
        return True
    return allow_legacy and bool(LEGACY_NODE_UID_RE.fullmatch(uid or ""))


def register_uid_alias(alias: str, canonical_uid: str) -> None:
    if alias:
        UID_ALIASES[str(alias)] = canonical_uid


def canonical_uid(uid: str) -> str:
    return UID_ALIASES.get(str(uid), str(uid))


def load_drivers(config: Dict[str, Any]) -> List[Any]:
    loaded = []
    for alias, canonical in config.get("uid_aliases", {}).items():
        if not is_valid_node_uid(canonical, allow_legacy=True):
            raise ValueError(f"Invalid canonical BardBox node UID for alias {alias}: {canonical}")
        register_uid_alias(alias, canonical)

    for entry in config.get("drivers", []):
        driver_name = entry.get("driver")
        uid = entry.get("uid", "bb-prj-air-001")
        driver_config = entry.get("config", {})
        if not is_valid_node_uid(uid, allow_legacy=True):
            raise ValueError(f"Invalid BardBox node UID: {uid}")

        if driver_name == "example":
            driver = ExampleDriver(uid=uid, config=driver_config)
        else:
            raise ValueError(f"Unsupported driver in template: {driver_name}")
        loaded.append(driver)
        DRIVER_UIDS[id(driver)] = uid
        register_uid_alias(uid, uid)
        for legacy_uid in entry.get("legacy_uids", []):
            register_uid_alias(legacy_uid, uid)
    return loaded


DRIVERS = load_drivers(APP_CONFIG)


def time_status() -> Dict[str, Any]:
    return {
        "valid": True,
        "source": "system",
        "sane": True,
        "ntp_synced": False,
    }


def utc_iso() -> str:
    return utc_now().strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_utc_timestamp(value: Optional[str]) -> Optional[datetime]:
    if not isinstance(value, str) or not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)
    except ValueError:
        return None


def null_data_for_driver(driver: Any) -> Dict[str, Any]:
    capabilities = driver.get_capabilities()
    return {channel: None for channel in capabilities.get("channels", {})}


def driver_uid(driver: Any) -> str:
    configured_uid = DRIVER_UIDS.get(id(driver))
    reported_uid = str(driver.get_info().get("uid", configured_uid or "unknown"))
    return canonical_uid(reported_uid)


def unavailable_reading(
    driver: Any,
    status: str,
    message: str,
    last_seen: Optional[str] = None,
    raw: Any = None,
) -> Dict[str, Any]:
    return {
        "uid": driver_uid(driver),
        "timestamp": utc_iso(),
        "status": status,
        "message": message,
        "data": null_data_for_driver(driver),
        "extended": {
            "last_seen": last_seen,
            "stale_after_s": NODE_STALE_AFTER_S,
        },
        "raw": raw,
    }


def normalize_driver_reading(driver: Any) -> Dict[str, Any]:
    uid = driver_uid(driver)
    state = NODE_STATE.setdefault(uid, {"last_successful_communication": None, "last_valid_reading": None})

    try:
        reading = driver.get_reading()
    except TimeoutError as exc:
        last_valid = state.get("last_valid_reading")
        last_seen = last_valid.get("timestamp") if last_valid else None
        return unavailable_reading(driver, "node_unavailable", str(exc) or "Node unavailable", last_seen)
    except Exception as exc:
        last_valid = state.get("last_valid_reading")
        last_seen = last_valid.get("timestamp") if last_valid else None
        return unavailable_reading(driver, "error", str(exc) or "Invalid device response", last_seen)

    if reading is None:
        last_valid = state.get("last_valid_reading")
        last_seen = last_valid.get("timestamp") if last_valid else None
        return unavailable_reading(driver, "node_unavailable", "Node unavailable", last_seen)

    state["last_successful_communication"] = utc_iso()
    reading = dict(reading)
    reading["uid"] = canonical_uid(str(reading.get("uid") or uid))
    reading.setdefault("timestamp", utc_iso())
    reading.setdefault("message", "")
    reading.setdefault("data", {})
    reading.setdefault("extended", {})
    reading.setdefault("raw", None)

    status = reading.get("status", "error")
    if status != "ok":
        last_valid = state.get("last_valid_reading")
        last_seen = last_valid.get("timestamp") if last_valid else reading.get("timestamp")
        message = reading.get("message") or reading.get("error") or "Invalid device response"
        normalized_status = status if status in {"stale", "error", "node_unavailable"} else "error"
        return unavailable_reading(driver, normalized_status, message, last_seen, reading.get("raw"))

    timestamp = parse_utc_timestamp(reading.get("timestamp"))
    if timestamp is None:
        return unavailable_reading(driver, "error", "Invalid or missing reading timestamp", None, reading.get("raw"))

    age_s = max(0.0, (utc_now() - timestamp).total_seconds())
    if age_s > NODE_STALE_AFTER_S:
        return unavailable_reading(driver, "stale", "Reading is stale", reading.get("timestamp"), reading.get("raw"))

    expected_channels = null_data_for_driver(driver)
    reading["data"] = {channel: reading.get("data", {}).get(channel) for channel in expected_channels}
    reading["extended"] = dict(reading.get("extended") or {})
    reading["extended"]["last_seen"] = reading.get("timestamp")
    reading["extended"]["stale_after_s"] = NODE_STALE_AFTER_S
    reading["message"] = reading.get("message") or "Fresh valid reading"
    state["last_valid_reading"] = reading
    return reading


def latest_readings() -> List[Dict[str, Any]]:
    return [normalize_driver_reading(driver) for driver in DRIVERS]


@app.get("/")
def landing(request: Request):
    return templates.TemplateResponse(
        "landing.html",
        {
            "request": request,
            "title": APP_CONFIG.get("title", "Example Deployment Monitor"),
            "app_id": APP_CONFIG.get("app_id", "bb-example-monitor"),
        },
    )


@app.get("/monitor")
def dashboard(request: Request):
    return templates.TemplateResponse(
        "index.html",
        {
            "request": request,
            "title": APP_CONFIG.get("title", "Example Deployment Monitor"),
            "app_id": APP_CONFIG.get("app_id", "bb-example-monitor"),
            "poll_interval_ms": APP_CONFIG.get("poll_interval_ms", 1000),
        },
    )


@app.get("/health")
async def health():
    # Lightweight endpoint for automated watchdog and health monitoring.
    return {"status": "ok"}


@app.get("/time")
def get_time():
    now_utc = utc_now()
    now_local = local_now()
    status = time_status()
    return JSONResponse(
        {
            "utc": now_utc.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "local": now_local.strftime("%a %b %d, %H:%M:%S"),
            "local_tz": now_local.tzname(),
            "time_status": status,
        }
    )


@app.get("/app/info")
def get_app_info():
    return JSONResponse(
        {
            "app_id": APP_CONFIG.get("app_id", "bb-example-monitor"),
            "title": APP_CONFIG.get("title", "Example Deployment Monitor"),
            "mode": APP_CONFIG.get("mode", "sensor_monitor"),
            "driver_count": len(DRIVERS),
            "node_stale_after_s": NODE_STALE_AFTER_S,
        }
    )


@app.get("/app/health")
def get_app_health():
    return JSONResponse(
        {
            "ok": True,
            "status": "ok",
            "time_status": time_status(),
            "driver_count": len(DRIVERS),
            "node_stale_after_s": NODE_STALE_AFTER_S,
        }
    )


@app.get("/drivers")
def get_drivers():
    payload = []
    for driver in DRIVERS:
        info = dict(driver.get_info())
        reported_uid = str(info.get("uid", "unknown"))
        info["uid"] = canonical_uid(reported_uid)
        if reported_uid != info["uid"]:
            info["reported_uid"] = reported_uid
        payload.append(
            {
                "info": info,
                "capabilities": driver.get_capabilities(),
            }
        )
    return JSONResponse({"drivers": payload})


@app.get("/readings/latest")
def get_latest_readings():
    return JSONResponse({"readings": latest_readings()})
