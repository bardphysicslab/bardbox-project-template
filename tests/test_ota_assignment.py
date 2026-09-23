import copy
import json
import os
from pathlib import Path
import random
import shutil
import subprocess

import pytest
from software.app.ota import canonical_manifest


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    root = Path(__file__).resolve().parents[1]
    binary = tmp_path_factory.mktemp('ota-parser')/'parser'
    subprocess.run([shutil.which('c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root/'software/firmware/include'),
                    str(root/'tests/ota_assignment.cpp'), '-o', str(binary)], check=True)
    def run(text, valid=False):
        result = subprocess.run([str(binary)], input=text.encode(), capture_output=True,
                                timeout=10, env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0'})
        assert result.returncode == (0 if valid else 1), result.stderr.decode()
        return result.stdout
    return run


def assignment():
    return dict(generation=1, artifact_path='/ota/v1/device/artifact/release-1',
                envelope=dict(signature='YWJjZA==', manifest=dict(format='bardbox-ota-v1',
                    release_id='release-1', component='app', version='0.7.0', target='cesh-bme280',
                    layout='dual-2m', config_schema='1', queue_schema='bq1', size=1234,
                    sha256='a'*64, key_id='lab')))


def test_valid_server_shape_and_field_order(parser):
    a = assignment()
    expected = ('1\nYWJjZA==\n'+a['artifact_path']+'\n').encode()+canonical_manifest(a['envelope']['manifest'])
    assert parser(json.dumps(a), True) == expected
    assert parser(json.dumps(a, sort_keys=True, indent=2), True) == expected
    a['generation'] = 4294967295
    assert parser(json.dumps(a), True).startswith(b'4294967295\n')


def test_reject_duplicate_unknown_missing_fields(parser):
    a = assignment()
    for path in [(), ('envelope',), ('envelope', 'manifest')]:
        for action in ('missing', 'extra'):
            changed = copy.deepcopy(a)
            target = changed
            for key in path: target = target[key]
            if action == 'missing': target.pop(next(iter(target)))
            else: target['unexpected'] = 'value'
            parser(json.dumps(changed))
    raw = json.dumps(a)
    for name, value in [('generation','1'), ('signature','"YWJjZA=="'), ('size','1234')]:
        parser(raw.replace('"'+name+'":', '"'+name+'": '+value+', "'+name+'":', 1))


def test_reject_coercions_paths_and_unbounded_data(parser):
    for value in [0, -1, True, None, '123', 1.2, 4294967296, 10**40]:
        a=assignment(); a['generation']=value; parser(json.dumps(a))
        a=assignment(); a['envelope']['manifest']['size']=value; parser(json.dumps(a))
    for value in ['https://other.example/image', '//other.example/image',
                  '/ota/v1/device/artifact/other', '/ota/v1/device/artifact/release-1?token=x']:
        a=assignment(); a['artifact_path']=value; parser(json.dumps(a))
    for value in ['', 'a'*108, 'a===', '=aaa', 'YW JjZA==']:
        a=assignment(); a['envelope']['signature']=value; parser(json.dumps(a))
    raw=json.dumps(assignment())
    for value in ['01', '1e0', '1.0', '+1']:
        parser(raw.replace('"generation": 1', '"generation": '+value))
    parser(raw+' {}'); parser(raw+' '*4096)
    parser(raw.replace('release-1', r'release\u002d1'))
    for rid in ['.', '..']:
        a=assignment(); a['envelope']['manifest']['release_id']=rid
        a['artifact_path']='/ota/v1/device/artifact/'+rid
        parser(json.dumps(a))
        with pytest.raises(ValueError): canonical_manifest(a['envelope']['manifest'])


def test_truncations_and_random_input(parser):
    raw=json.dumps(assignment())
    for length in range(0,len(raw),7): parser(raw[:length])
    rng=random.Random(916)
    for _ in range(30):
        parser(''.join(chr(rng.randrange(32,127)) for _ in range(rng.randrange(0,1024))))
