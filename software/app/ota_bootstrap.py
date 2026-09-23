"""Load optional management configuration independently of measurement config."""
import json
import os
import re
from pathlib import Path

_HEX64 = re.compile(r'^[0-9a-f]{64}$')


def _validate_role_config(config):
    """Fail fast on a misconfigured admins/operators split rather than
    silently letting it slip through -- device_registration.py's
    engineer/operator role gating in create_registration_router()
    depends on these two credential sets actually being disjoint. A
    username present in both would authenticate as an operator through
    an admins-only check too, silently defeating the role split."""
    admins = config.get('admins', {})
    operators = config.get('operators', {})
    overlap = set(admins) & set(operators)
    if overlap:
        raise ValueError(
            f'OTA config error: {sorted(overlap)} present in both admins and operators -- these must be disjoint')
    for role, table in (('admins', admins), ('operators', operators)):
        for user, digest in table.items():
            if not isinstance(digest, str) or not _HEX64.match(digest):
                raise ValueError(f'OTA config error: {role}["{user}"] is not a valid sha256 hex digest')


def install_ota(app):
    """Returns (enabled: bool, registration_store: RegistrationStore | None).
    A caller that needs the store for something else (e.g. a telemetry
    ingestion gate consulting live_uids()) reuses this same instance
    rather than re-parsing config and opening a second one."""
    path = os.environ.get('BARDBOX_OTA_CONFIG')
    if not path:
        return False, None
    config = json.loads(Path(path).read_text())
    if not config.get('enabled', False):
        return False, None
    _validate_role_config(config)
    root = Path(config['state_directory'])
    if not root.is_absolute():
        raise ValueError('OTA state_directory must be absolute')
    # Capture the genuinely-static devices dict BEFORE it's replaced below --
    # the registration router needs this exact set for collision checking,
    # not the already-merged view.
    static_devices = dict(config.get('devices', {}))
    # Merge in live, persistently-registered devices (see
    # device_registration.py/device_registry.py) so a newly-issued
    # registration is authenticatable immediately, with no restart.
    # ota.py gets one small, explicit note_successful_auth() hook call in
    # its device_auth() (see that file) -- everything else about it is
    # unmodified; it still only ever sees `config['devices']` as a mapping.
    from .device_registration import RegistrationStore, resolve_sensor_profiles
    from .device_registry import build_device_registry
    registration_store = RegistrationStore(root, sensor_profiles=resolve_sensor_profiles(config))
    config['devices'] = build_device_registry(config, root, registration_store=registration_store)
    # Optional dependency: existing installations run without OTA packages.
    from .ota import create_ota_router
    app.include_router(create_ota_router(config, root))
    # Device registration lifecycle (stage/issue/reissue/revoke) backing
    # the merged registry above.
    from .device_registration import create_registration_router
    app.include_router(create_registration_router(
        config, root, registration_store=registration_store, static_devices=static_devices))
    return True, registration_store
