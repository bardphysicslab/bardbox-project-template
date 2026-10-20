"""Library compilation must see the same project config as the sketch."""
import configparser
import shlex
from pathlib import Path


def test_project_include_flag_reaches_every_supported_environment():
    fw = Path(__file__).resolve().parents[1] / "software" / "firmware"
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(fw / "platformio.ini")
    shared = ini["env:feather_s3"]["build_flags"]
    assert "-Iinclude" in shlex.split(shared)
    for name in ("env:feather_s3_m1_test", "env:feather_s3_wifi42_test"):
        flags = ini[name]["build_flags"]
        assert "${env:feather_s3.build_flags}" in flags
        resolved = flags.replace("${env:feather_s3.build_flags}", shared)
        assert "-Iinclude" in shlex.split(resolved)
        assert "-DFIRMWARE_VERSION=" in resolved
