"""Tests for software/app/device_registration.py's staged->issued->
activated lifecycle (RegistrationStore) and its HTTP router
(create_registration_router). Ported from the CESH project's own
device_registration.py test suite (see that project's
docs/bardbox-template-sensor-profiles-delta.md) -- this template's own
SENSOR_PROFILES default is empty, unlike CESH's, so tests here confirm
"no configured profiles" fails closed rather than that a specific
default profile survives unchanged.

Focus: profile validation, project-configurable sensor profiles with
the empty template default, persisted-profile-removal/change safety
(reopening the same store after config changes), and that two
independently-configured stores cannot select or list each other's
profiles.
"""
import base64
import hashlib
from pathlib import Path

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from software.app.device_registration import (
    STAGED,
    RegistrationError, RegistrationStore, SENSOR_PROFILES, create_registration_router,
    resolve_sensor_profiles, validate_sensor_profiles,
)

UID = 'bb-node-042'
ADMIN_TOKEN = 'admin-token-value-0123456789abcdef'
OPERATOR_TOKEN = 'field-operator-token-value-0123456789ab'
ORIGIN = 'https://project.example.test'

RKC_PROFILE = {
    'component': 'firmware', 'target': 'rkc-freezer-monitor', 'layout': 'single-app',
    'config_schema': 'rkc1', 'queue_schema': 'rkc-bq1', 'slot_bytes': 1048576,
}


def sha256(s):
    return hashlib.sha256(s.encode()).hexdigest()


# --- Validation ------------------------------------------------------

def test_resolve_returns_the_empty_template_default_when_key_absent():
    assert resolve_sensor_profiles({}) == SENSOR_PROFILES == {}


def test_resolve_rejects_an_explicit_null_rather_than_silently_defaulting():
    with pytest.raises(ValueError):
        resolve_sensor_profiles({'sensor_profiles': None})


def test_resolve_returns_the_override_unchanged_when_valid():
    override = {'rkc_freezer_v1': RKC_PROFILE}
    assert resolve_sensor_profiles({'sensor_profiles': override}) == override


def test_rejects_a_non_dict_or_empty_override():
    with pytest.raises(ValueError):
        validate_sensor_profiles(['not', 'a', 'dict'])
    with pytest.raises(ValueError):
        validate_sensor_profiles({})


def test_rejects_a_profile_missing_a_required_field():
    broken = dict(RKC_PROFILE)
    del broken['slot_bytes']
    with pytest.raises(ValueError):
        validate_sensor_profiles({'rkc_freezer_v1': broken})


def test_rejects_a_non_positive_slot_bytes():
    for bad in (0, -1, 'not-an-int', True):
        broken = dict(RKC_PROFILE, slot_bytes=bad)
        with pytest.raises(ValueError):
            validate_sensor_profiles({'rkc_freezer_v1': broken})


# --- RegistrationStore with the empty template default ---------------

def test_default_store_requires_explicit_profiles_before_anything_is_stageable(tmp_path):
    store = RegistrationStore(tmp_path)
    assert store.sensor_profiles == {}
    with pytest.raises(RegistrationError):
        store.stage(UID, 'Node 042', 'anything', 'engineer')


def test_configured_store_accepts_only_its_own_profile_name(tmp_path):
    store = RegistrationStore(tmp_path, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    store.stage(UID, 'Freezer 01', 'rkc_freezer_v1', 'engineer')
    assert store.status(UID) == STAGED


def test_configured_store_live_entries_uses_its_own_compatibility_fields(tmp_path):
    store = RegistrationStore(tmp_path, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    store.stage(UID, 'Freezer 01', 'rkc_freezer_v1', 'engineer')
    store.issue_credentials(UID, 'operator')
    entry = store.live_entries()[UID]
    for key, value in RKC_PROFILE.items():
        assert entry[key] == value


# --- Persisted-profile removal/change safety --------------------------

def test_removing_a_profile_after_activation_does_not_crash_live_entries_on_reopen(tmp_path):
    store = RegistrationStore(tmp_path, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    store.stage(UID, 'Freezer 01', 'rkc_freezer_v1', 'engineer')
    store.issue_credentials(UID, 'operator')

    # Reopen the SAME persisted store with the profile now REMOVED from
    # config -- must not raise, and the device's compatibility fields
    # must still reflect what was true when it was staged.
    reopened = RegistrationStore(tmp_path, sensor_profiles={})
    entry = reopened.live_entries()[UID]
    for key, value in RKC_PROFILE.items():
        assert entry[key] == value


def test_changing_a_profiles_metadata_under_the_same_name_does_not_retarget_existing_devices(tmp_path):
    store = RegistrationStore(tmp_path, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    store.stage(UID, 'Freezer 01', 'rkc_freezer_v1', 'engineer')
    store.issue_credentials(UID, 'operator')

    changed = dict(RKC_PROFILE, slot_bytes=RKC_PROFILE['slot_bytes'] * 2, target='rkc-freezer-monitor-v2')
    reopened = RegistrationStore(tmp_path, sensor_profiles={'rkc_freezer_v1': changed})
    entry = reopened.live_entries()[UID]
    # The EXISTING device keeps its original (staged-time) metadata --
    # the config edit never silently retargets it.
    assert entry['slot_bytes'] == RKC_PROFILE['slot_bytes']
    assert entry['target'] == RKC_PROFILE['target']


def test_reopening_with_an_unresolvable_persisted_profile_and_no_prior_snapshot_fails_fast(tmp_path):
    store = RegistrationStore(tmp_path, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    store.stage(UID, 'Freezer 01', 'rkc_freezer_v1', 'engineer')
    with store.connection() as db:
        db.execute('UPDATE registrations SET sensor_profile_snapshot=NULL WHERE uid=?', (UID,))

    with pytest.raises(RegistrationError):
        RegistrationStore(tmp_path, sensor_profiles={})  # profile no longer configured either


# --- Cross-project isolation ------------------------------------------

def test_two_independently_configured_stores_cannot_select_each_others_profiles(tmp_path):
    root_a, root_b = tmp_path / 'a', tmp_path / 'b'
    store_a = RegistrationStore(root_a)  # empty template default
    store_b = RegistrationStore(root_b, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})

    with pytest.raises(RegistrationError):
        store_a.stage(UID, 'Cross-project attempt', 'rkc_freezer_v1', 'engineer')
    with pytest.raises(RegistrationError):
        store_b.stage(UID, 'Cross-project attempt', 'bench_shared_pms6003', 'engineer')

    assert set(store_a.sensor_profiles) == set()
    assert set(store_b.sensor_profiles) == {'rkc_freezer_v1'}


def test_each_projects_router_only_advertises_its_own_profiles(tmp_path):
    root_a, root_b = tmp_path / 'a', tmp_path / 'b'
    store_a = RegistrationStore(root_a)
    store_b = RegistrationStore(root_b, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    config_a = {'enabled': True, 'admins': {'engineer': sha256(ADMIN_TOKEN)}, 'public_origin': ORIGIN}
    config_b = dict(config_a, sensor_profiles={'rkc_freezer_v1': RKC_PROFILE})
    app_a, app_b = FastAPI(), FastAPI()
    app_a.include_router(create_registration_router(config_a, root_a, registration_store=store_a))
    app_b.include_router(create_registration_router(config_b, root_b, registration_store=store_b))
    headers = {'Authorization': 'Basic ' + base64.b64encode(f'engineer:{ADMIN_TOKEN}'.encode()).decode()}
    listing_a = TestClient(app_a).get('/ota/v1/admin/registrations', headers=headers).json()
    listing_b = TestClient(app_b).get('/ota/v1/admin/registrations', headers=headers).json()
    assert listing_a['sensor_profiles'] == []
    assert listing_b['sensor_profiles'] == ['rkc_freezer_v1']
