"""End-to-end integration test for software/app/ota_bootstrap.py's
install_ota(): proves the device_registration.py/device_registry.py
wiring actually works through the real bootstrap path, not just in
isolation -- a registration staged and issued through the registration
router becomes usable on ota.py's real device-facing endpoint with NO
restart, because config['devices'] is the live DeviceRegistry by the
time create_ota_router() reads it. ota.py itself has exactly one small,
reviewed hook added to device_auth() (see software/app/ota.py) to call
DeviceRegistry.note_successful_auth() after a digest match -- everything
else about it is unmodified.

Ported from the CESH project's own test suite (see that project's
docs/bardbox-template-sensor-profiles-delta.md), which flagged that
copying registration modules and ota_bootstrap.py alone -- without also
porting THIS regression -- would leave the note_successful_auth hook's
activation/credential-race semantics unverified in this template.

Reproduces, and proves fixed, the exact P1 race CESH's own review found
through this same bootstrap path: stage -> issue -> a real device
assignment call (204) -> an immediate reissue attempt. Without the
hook/atomic activation, this sequence would let reissue succeed (200)
because activation would lag one request behind the real contact event,
after which the device's already-in-use original token would stop
working (401) -- a live device silently losing its own credential. With
it, the assignment call's own device_auth() success activates
synchronously, in the same request, so the immediate reissue is
correctly refused (409) and the original token keeps working.
"""
import base64
import hashlib
import json

import pytest
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from fastapi import FastAPI
from fastapi.testclient import TestClient

from software.app.ota_bootstrap import install_ota

ADMIN_TOKEN = 'admin-token-value-0123456789abcdef'
UID = 'bb-node-042'
ORIGIN = 'https://project.example.test'
RKC_PROFILE = {
    'component': 'firmware', 'target': 'rkc-freezer-monitor', 'layout': 'single-app',
    'config_schema': 'rkc1', 'queue_schema': 'rkc-bq1', 'slot_bytes': 1048576,
}


def sha256(s):
    return hashlib.sha256(s.encode()).hexdigest()


def ec_public_key_pem():
    key = ec.generate_private_key(ec.SECP256R1())
    return key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode('ascii')


@pytest.fixture
def installed(tmp_path, monkeypatch):
    root = tmp_path / 'state'
    config = {
        'enabled': True,
        'state_directory': str(root),
        'admins': {'engineer': sha256(ADMIN_TOKEN)},
        'public_origin': ORIGIN,
        'public_keys': {'test-key': ec_public_key_pem()},
        'devices': {},
        'sensor_profiles': {'rkc_freezer_v1': RKC_PROFILE},
    }
    config_path = tmp_path / 'ota.json'
    config_path.write_text(json.dumps(config))
    monkeypatch.setenv('BARDBOX_OTA_CONFIG', str(config_path))
    app = FastAPI()
    enabled, registration_store = install_ota(app)
    assert enabled
    return app, registration_store


def engineer_headers(write=False):
    creds = base64.b64encode(f'engineer:{ADMIN_TOKEN}'.encode()).decode()
    h = {'Authorization': f'Basic {creds}'}
    if write:
        h['X-Bardbox-OTA'] = '1'
        h['Origin'] = ORIGIN
    return h


def test_registration_becomes_authenticatable_on_the_real_device_endpoint_with_no_restart(installed):
    app, _store = installed
    client = TestClient(app)
    client.post('/ota/v1/admin/registrations/stage', headers=engineer_headers(True),
                json={'uid': UID, 'name': 'Freezer 01', 'sensor_profile': 'rkc_freezer_v1'})
    issue = client.post('/ota/v1/admin/registrations/issue', headers=engineer_headers(True), json={'uid': UID})
    assert issue.status_code == 200
    token = issue.json()['ota_device_token']

    resp = client.get('/ota/v1/device/assignment', headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 204  # no assignment yet, but authenticated -- the point of this test


def test_activation_race_reissue_immediately_after_first_real_contact_is_refused(installed):
    app, _store = installed
    client = TestClient(app)
    client.post('/ota/v1/admin/registrations/stage', headers=engineer_headers(True),
                json={'uid': UID, 'name': 'Freezer 01', 'sensor_profile': 'rkc_freezer_v1'})
    issue = client.post('/ota/v1/admin/registrations/issue', headers=engineer_headers(True), json={'uid': UID})
    token = issue.json()['ota_device_token']

    # Real device contact -- this is what activates the registration,
    # synchronously, via the note_successful_auth hook in device_auth().
    contact = client.get('/ota/v1/device/assignment', headers={'Authorization': f'Bearer {token}'})
    assert contact.status_code == 204

    # An immediate reissue attempt must now be refused: the device is
    # ACTIVATED, and reissue_credentials() only accepts ISSUED.
    reissue = client.post('/ota/v1/admin/registrations/reissue', headers=engineer_headers(True), json={'uid': UID})
    assert reissue.status_code == 409

    # The original token must still work -- it was never invalidated.
    still_works = client.get('/ota/v1/device/assignment', headers={'Authorization': f'Bearer {token}'})
    assert still_works.status_code == 204


def test_a_revoked_registrations_old_token_no_longer_authenticates(installed):
    app, _store = installed
    client = TestClient(app)
    client.post('/ota/v1/admin/registrations/stage', headers=engineer_headers(True),
                json={'uid': UID, 'name': 'Freezer 01', 'sensor_profile': 'rkc_freezer_v1'})
    issue = client.post('/ota/v1/admin/registrations/issue', headers=engineer_headers(True), json={'uid': UID})
    token = issue.json()['ota_device_token']
    client.post('/ota/v1/admin/registrations/revoke', headers=engineer_headers(True),
                json={'uid': UID, 'reason': 'test'})
    resp = client.get('/ota/v1/device/assignment', headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 401


# --- Role config validation -------------------------------------------
# Ported from CESH's own _validate_role_config tests. Flagged by review
# as a missing dependency of this port: device_registration.py's
# engineer/operator role gating in create_registration_router() depends
# on admins/operators actually being disjoint, and install_ota() must
# fail fast on a misconfiguration rather than let it reach a running
# deployment.

def base_role_config(tmp_path):
    return {
        'enabled': True, 'state_directory': str(tmp_path / 'state'), 'public_origin': ORIGIN,
        'public_keys': {'test-key': ec_public_key_pem()}, 'devices': {},
    }


def write_and_install(tmp_path, monkeypatch, config):
    config_path = tmp_path / 'ota.json'
    config_path.write_text(json.dumps(config))
    monkeypatch.setenv('BARDBOX_OTA_CONFIG', str(config_path))
    return install_ota(FastAPI())


def test_rejects_a_username_present_in_both_admins_and_operators(tmp_path, monkeypatch):
    config = base_role_config(tmp_path)
    config['admins'] = {'shared-user': sha256('admin-token-0123456789abcdef')}
    config['operators'] = {'shared-user': sha256('operator-token-0123456789abcdef')}
    with pytest.raises(ValueError):
        write_and_install(tmp_path, monkeypatch, config)


def test_rejects_a_non_hex_digest_admin_hash(tmp_path, monkeypatch):
    config = base_role_config(tmp_path)
    config['admins'] = {'engineer': 'not-a-real-sha256-digest'}
    with pytest.raises(ValueError):
        write_and_install(tmp_path, monkeypatch, config)


def test_rejects_a_non_hex_digest_operator_hash(tmp_path, monkeypatch):
    config = base_role_config(tmp_path)
    config['admins'] = {'engineer': sha256('admin-token-0123456789abcdef')}
    config['operators'] = {'fielduser': 'short'}
    with pytest.raises(ValueError):
        write_and_install(tmp_path, monkeypatch, config)


def test_accepts_disjoint_admins_and_operators_with_valid_hashes(tmp_path, monkeypatch):
    config = base_role_config(tmp_path)
    config['admins'] = {'engineer': sha256('admin-token-0123456789abcdef')}
    config['operators'] = {'fielduser': sha256('operator-token-0123456789abcdef')}
    enabled, _store = write_and_install(tmp_path, monkeypatch, config)
    assert enabled


def test_accepts_a_config_with_no_operators_key_at_all(tmp_path, monkeypatch):
    config = base_role_config(tmp_path)
    config['admins'] = {'engineer': sha256('admin-token-0123456789abcdef')}
    enabled, _store = write_and_install(tmp_path, monkeypatch, config)
    assert enabled
