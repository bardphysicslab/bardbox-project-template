"""Guard the real hazard: auxiliary images must not occupy either app slot."""
import configparser
import csv
import json
from pathlib import Path

FW = Path(__file__).resolve().parents[1] / "software" / "firmware"


def test_supported_board_keeps_auxiliary_images_out_of_app_slots():
    ini = configparser.ConfigParser()
    ini.read(FW / "platformio.ini")
    base = ini["env:feather_s3"]
    profile = json.loads((FW / "boards" / (base["board"] + ".json")).read_text())
    assert profile["build"]["mcu"] == "esp32s3"
    assert profile["upload"]["flash_size"] == "8MB"
    partitions = list(csv.reader(line for line in (FW / base["board_build.partitions"]).read_text().splitlines()
                                 if line.strip() and not line.lstrip().startswith("#")))
    slots = [(int(row[3].strip(), 0), int(row[4].strip(), 0)) for row in partitions if row[1].strip() == "app"]
    assert len(slots) == 2
    assert profile["upload"]["maximum_size"] <= min(size for _, size in slots)
    for offset, image in profile["upload"]["arduino"]["flash_extra_images"]:
        assert "tinyuf2" not in image.lower()
        assert all(not (start <= int(offset, 0) < start + size) for start, size in slots)
    for env in ("env:feather_s3_m1_test", "env:feather_s3_wifi42_test"):
        assert ini[env]["extends"] == "env:feather_s3"
        assert "board" not in ini[env]  # tests must use the same corrected board
