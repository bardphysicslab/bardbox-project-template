from pathlib import Path
import os
import shutil
import subprocess
import pytest


@pytest.mark.parametrize('source', ['record_delta.cpp', 'record_chunk.cpp'])
def test_record_delta_lossless_and_corrupt_inputs(tmp_path, source):
    root = Path(__file__).resolve().parents[1]
    binary = tmp_path / 'record-delta'
    subprocess.run([shutil.which('c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root/'software/firmware/include'),
                    str(root/'tests'/source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=60,
                   env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
