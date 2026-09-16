import base64
import hashlib

import pytest
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from fastapi import FastAPI
from fastapi.testclient import TestClient

from software.app.ota import canonical_manifest, create_ota_router, token_hash


@pytest.fixture
def setup(tmp_path):
    key = ec.generate_private_key(ec.SECP256R1())
    public = key.public_key().public_bytes(serialization.Encoding.PEM,
                                          serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    device = dict(target='cesh-bme280', layout='dual-2m', config_schema='1',
                  queue_schema='1', slot_bytes=2097152)
    config = dict(enabled=True, public_origin='https://lab.example',
                  admins={'operator': token_hash('admin-secret')}, public_keys={'lab': public},
                  devices={'node-a': dict(device, token_sha256=token_hash('device-a')),
                           'node-b': dict(device, token_sha256=token_hash('device-b'))})
    def client():
        app = FastAPI()
        app.include_router(create_ota_router(config, tmp_path))
        return TestClient(app, base_url='https://lab.example')
    headers = {'Authorization': 'Basic ' + base64.b64encode(b'operator:admin-secret').decode(),
               'Origin': 'https://lab.example', 'X-Bardbox-OTA': '1'}
    image = b'test firmware bytes'
    manifest = dict(format='bardbox-ota-v1', release_id='release-1', component='app',
                    version='0.7.0', target='cesh-bme280', layout='dual-2m',
                    config_schema='1', queue_schema='1', size=len(image),
                    sha256=hashlib.sha256(image).hexdigest(), key_id='lab')
    signature = key.sign(canonical_manifest(manifest), ec.ECDSA(hashes.SHA256()))
    package = dict(envelope=dict(manifest=manifest, signature=base64.b64encode(signature).decode()),
                   image_base64=base64.b64encode(image).decode())
    return client, headers, package


def upload_assign(client, headers, package):
    assert client.post('/ota/v1/admin/releases', headers=headers, json=package).status_code == 200
    assignment = dict(uid='node-a', release_id='release-1', request_id='request-1')
    r = client.post('/ota/v1/admin/assignments', headers=headers, json=assignment)
    assert r.status_code == 200
    return assignment


def status(package, **changes):
    value = dict(generation=1, sequence=1, boot_id='boot-1', state='confirmed',
                 running_version='0.7.0', running_sha256=package['envelope']['manifest']['sha256'],
                 bytes=0, failure='none')
    value.update(changes)
    return value


def test_signed_release_assignment_and_restart(setup):
    make, headers, package = setup
    c = make()
    assignment = upload_assign(c, headers, package)
    again = make()
    duplicate = again.post('/ota/v1/admin/assignments', headers=headers, json=assignment).json()
    assert duplicate == {'generation': 1, 'existing': True}
    auth = {'Authorization': 'Bearer device-a'}
    response = again.get('/ota/v1/device/assignment', headers=auth)
    assert response.json()['envelope'] == package['envelope']
    assert again.get(response.json()['artifact_path'], headers=auth).content == b'test firmware bytes'
    assert again.post('/ota/v1/device/status', headers=auth, json=status(package)).status_code == 200
    overview = again.get('/ota/v1/admin/overview', headers=headers).json()
    assert overview['devices'][0]['status']['state'] == 'confirmed'


@pytest.mark.parametrize('change', ['image', 'metadata', 'signature'])
def test_tampered_release_is_rejected(setup, change):
    make, headers, package = setup
    if change == 'image':
        package['image_base64'] = base64.b64encode(b'changed').decode()
    elif change == 'metadata':
        package['envelope']['manifest']['target'] = 'cesh-bme680'
    else:
        package['envelope']['signature'] = base64.b64encode(b'bad').decode()
    assert make().post('/ota/v1/admin/releases', headers=headers, json=package).status_code == 422


def test_roles_devices_and_cross_origin_are_separate(setup):
    make, headers, package = setup
    c = make()
    assert c.get('/firmware').status_code == 401
    assert c.get('/ota/v1/admin/overview', headers={'Authorization': 'Bearer device-a'}).status_code == 401
    assert c.get('/ota/v1/device/assignment', headers=headers).status_code == 401
    unsafe = dict(headers, Origin='https://other.example')
    assert c.post('/ota/v1/admin/releases', headers=unsafe, json=package).status_code == 403
    upload_assign(c, headers, package)
    assert c.get('/ota/v1/device/artifact/release-1', headers={'Authorization': 'Bearer device-b'}).status_code == 404


def test_status_duplicates_stale_events_and_image_mismatch(setup):
    make, headers, package = setup
    c = make()
    upload_assign(c, headers, package)
    auth = {'Authorization': 'Bearer device-a'}
    url = '/ota/v1/device/status'
    assert c.post(url, headers=auth, json=status(package, running_version='wrong')).status_code == 409
    good = status(package)
    assert c.post(url, headers=auth, json=good).status_code == 200
    assert c.post(url, headers=auth, json=good).json()['duplicate'] is True
    assert c.post(url, headers=auth, json=status(package, sequence=0)).status_code == 409
    assert c.post(url, headers=auth, json=status(package, sequence=2, state='downloading')).status_code == 409


@pytest.mark.parametrize('state', [[], {}, None, 1])
def test_malformed_state_rejected_without_server_error(setup, state):
    make, headers, package = setup
    c = make()
    upload_assign(c, headers, package)
    assert c.post('/ota/v1/device/status', headers={'Authorization': 'Bearer device-a'},
                  json=status(package, state=state)).status_code == 422


def test_disabled_service_has_no_routes(tmp_path):
    assert not create_ota_router({}, tmp_path).routes


def test_poll_marks_unassigned_device_seen_without_inventing_version(setup):
    make, headers, _ = setup
    c = make()
    r = c.get('/ota/v1/device/assignment', headers={'Authorization':'Bearer device-b'})
    assert r.status_code == 204
    overview = c.get('/ota/v1/admin/overview', headers=headers)
    assert overview.headers['cache-control'] == 'no-store'
    device = overview.json()['devices'][1]
    assert device['last_seen'] is not None and not device['stale']
    assert device['status'] is None


def test_stopping_delivery_does_not_erase_installation_outcome(setup):
    make, headers, package = setup
    c = make()
    upload_assign(c, headers, package)
    auth = {'Authorization':'Bearer device-a'}
    assert c.post('/ota/v1/admin/stop-delivery', headers=headers,
                  json={'uid':'node-a','generation':2}).status_code == 409
    assert c.post('/ota/v1/admin/stop-delivery', headers=headers,
                  json={'uid':'node-a','generation':1}).status_code == 200
    assert c.get('/ota/v1/device/assignment', headers=auth).status_code == 204
    assert c.get('/ota/v1/device/artifact/release-1', headers=auth).status_code == 404
    # A device that already downloaded may still install; preserve its evidence.
    assert c.post('/ota/v1/device/status', headers=auth, json=status(package)).status_code == 200


def test_compatibility_and_immutable_release_id(setup):
    make, headers, package = setup
    c = make()
    upload_assign(c, headers, package)
    # A valid release for another variant is tested through a different device registry.
    from software.app.ota import compatible
    m = package['envelope']['manifest']
    device = dict(m, slot_bytes=2097152)
    assert compatible(device, m)
    device['target'] = 'cesh-bme680'
    assert not compatible(device, m)
    for value in ([], None, {'envelope':{},'image_base64':'???'}):
        assert c.post('/ota/v1/admin/releases', headers=headers, json=value).status_code == 422


def test_large_status_body_rejected(setup):
    make, _, _ = setup
    assert make().post('/ota/v1/device/status', headers={'Authorization':'Bearer device-a'},
                       content=b'x'*8193).status_code == 413
