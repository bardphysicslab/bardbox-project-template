from pathlib import Path
import os
import shutil
import subprocess

from software.app.ota import canonical_manifest
import pytest


@pytest.mark.parametrize('source', ['ota_state.cpp', 'ota_https_config.cpp', 'ota_stream.cpp'])
def test_update_state_commit_and_reboot_failures(tmp_path, source):
    root = Path(__file__).resolve().parents[1]
    binary = tmp_path / 'ota-state'
    subprocess.run([shutil.which('c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root/'software/firmware/include'),
                    str(root/'tests'/source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30,
                   env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


def test_embedded_manifest_matches_signing_contract(tmp_path):
    root = Path(__file__).resolve().parents[1]
    binary = tmp_path / 'ota-manifest'
    subprocess.run([shutil.which('c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root/'software/firmware/include'),
                    str(root/'tests/ota_manifest.cpp'), '-o', str(binary)], check=True)
    result = subprocess.check_output([str(binary)], timeout=30,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
    manifest = dict(format='bardbox-ota-v1', release_id='bench-001', component='firmware',
                    version='0.7.0', target='esp32-s3', layout='dual-2m', config_schema='1',
                    queue_schema='bq1', size=12345, sha256='0'*64, key_id='lab-1')
    assert result == canonical_manifest(manifest)
