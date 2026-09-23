"""Persistent device registration lifecycle: staged -> issued -> activated
(or revoked at any point before activation, or after). See
device_registry.py for how this becomes LIVE authorization data (no
restart needed) merged with the existing static config.

`admins`/`operators`/`devices` live wherever `BARDBOX_OTA_CONFIG`
points; this module's records are independent of that file --
device_registry.py is what actually merges them together for the
routers to consult, live.

Lifecycle:
- `stage(uid, name, sensor_profile, actor, static_uids)`: an engineer
  records a preallocated UID/profile (never invented here -- UID
  allocation stays wherever the project already manages it). Explicit
  collision handling: refuses a uid that collides with an existing
  STATIC config entry (`static_uids`, passed by the caller) or an
  existing non-revoked registration -- an existing identity/credential
  is never silently overwritten.
- `issue_credentials(uid, actor)`: mints a fresh random OTA device token,
  stores only its SHA-256 hash, and returns the plaintext exactly once.
  Moves `staged` -> `issued`.
- `reissue_credentials(uid, actor)`: recovers from a lost/undelivered
  response -- while still `issued` (i.e. the device has NOT yet made
  first contact), an engineer can safely mint a replacement token; the
  old hash is overwritten (invalidated) so a lost/leaked token can
  never be replayed. Refused once `activated`, since a live device is
  already relying on its current credential -- see `revoke()` for that
  case instead.
- Activation is explicit and post-authentication, not a side effect of
  reading the registry: `activate_if_current(uid, token_sha256)` folds
  "is this digest still current" and "activate if so" into ONE atomic
  transaction and returns that verdict. `ota.py`'s `device_auth()` (see
  the `note_successful_auth` hook there) calls it immediately after
  `hmac.compare_digest` matches a SNAPSHOT read of the registry, and
  rejects the request (401) when it returns False -- exactly as it
  would for a wrong token, never "skip activation and let it through
  anyway." SQLite's BEGIN IMMEDIATE serializes this against
  reissue_credentials()/revoke()'s own BEGIN IMMEDIATE transactions on
  the same file, so whichever commits first is authoritative for every
  later reader.
- `revoke(uid, actor, reason)`: refuses further authentication as this
  identity from any state (staged/issued/activated) -- terminal. A
  revoked uid can be staged again later as a fresh registration if the
  engineer chooses (revocation does not permanently block the UID).

Sensor profiles are project-configurable: `SENSOR_PROFILES` below is
deliberately empty in this template -- a neutral starting point keeps
OTA optional (as already unmodified) but requires a project to supply
its own explicit, valid `sensor_profiles` config before ANY device can
be staged; nothing is silently pre-approved. `RegistrationStore` takes
an optional `sensor_profiles` argument; `resolve_sensor_profiles()`
reads it from a project's own OTA config (via `sensor_profiles`,
validated) or falls back to this empty default -- there is no shared
global namespace a second project's configuration could accidentally
read from or stage into (see `resolve_sensor_profiles()`'s own
docstring, and the isolation tests in tests/test_device_registration.py).
"""
import base64
import hmac
import json
import secrets
import sqlite3
import time
from contextlib import contextmanager
from pathlib import Path

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import Response

from .ota import TOKEN, token_hash

# Deliberately empty: a neutral template has no compiled-in device
# build to describe. A project enables registration by supplying its
# own `sensor_profiles` config (see resolve_sensor_profiles()) -- until
# it does, stage() rejects every profile name, fail-closed by
# construction rather than by an extra check.
SENSOR_PROFILES = {}

REQUIRED_SENSOR_PROFILE_FIELDS = {'component', 'target', 'layout', 'config_schema', 'queue_schema', 'slot_bytes'}


def validate_sensor_profiles(profiles):
    """Fail fast on a malformed sensor_profiles override -- every
    profile must have exactly the fields device_registry.py's
    live_entries() will later merge into a `devices[uid]` entry;
    catching a missing/extra/mistyped field here is far cheaper than
    debugging a device that silently got the wrong compatibility
    metadata."""
    if not isinstance(profiles, dict) or not profiles:
        raise ValueError('OTA config error: sensor_profiles must be a non-empty object')
    for name, profile in profiles.items():
        if not isinstance(name, str) or not name:
            raise ValueError('OTA config error: sensor_profiles keys must be non-empty strings')
        if not isinstance(profile, dict) or set(profile) != REQUIRED_SENSOR_PROFILE_FIELDS:
            raise ValueError(
                f'OTA config error: sensor_profiles["{name}"] must have exactly the fields '
                f'{sorted(REQUIRED_SENSOR_PROFILE_FIELDS)}')
        if not isinstance(profile['slot_bytes'], int) or isinstance(profile['slot_bytes'], bool) \
                or profile['slot_bytes'] <= 0:
            raise ValueError(f'OTA config error: sensor_profiles["{name}"].slot_bytes must be a positive integer')
        for field in ('component', 'target', 'layout', 'config_schema', 'queue_schema'):
            if not isinstance(profile[field], str) or not profile[field]:
                raise ValueError(f'OTA config error: sensor_profiles["{name}"].{field} must be a non-empty string')


def resolve_sensor_profiles(config):
    """The project's actual sensor-profile allow-list: `config['sensor_profiles']`
    if present (validated), else this template's empty `SENSOR_PROFILES`
    default -- meaning registration is enabled (OTA stays optional
    overall) but nothing is stageable until a project supplies its own
    explicit, valid set. A project that supplies one gets ONLY that
    set -- never merged with the default, so a second project's config
    can never accidentally make this project's profile names
    selectable, or vice versa."""
    if 'sensor_profiles' not in config:
        return SENSOR_PROFILES
    override = config['sensor_profiles']
    # An ABSENT key ("no opinion, use the default") and an explicit
    # `"sensor_profiles": null` ("almost certainly a config-authoring
    # mistake") are different: the latter is rejected outright rather
    # than silently falling back to the default.
    if override is None:
        raise ValueError(
            'OTA config error: sensor_profiles must not be null -- omit the key entirely to use the '
            'default, or provide a valid profiles object')
    validate_sensor_profiles(override)
    return override

STAGED, ISSUED, ACTIVATED, REVOKED = 'staged', 'issued', 'activated', 'revoked'
LIVE_STATES = {ISSUED, ACTIVATED}  # states device_registry.py should treat as authenticatable


class RegistrationError(Exception):
    """Raised for any invalid/duplicate/out-of-order registration action,
    with a plain-language message safe to show an operator (never
    includes generated credential values)."""


class RegistrationStore:
    """Independent SQLite file -- never shares a table with ota.py, and
    never imports anything mutable from it."""

    def __init__(self, root, sensor_profiles=None):
        # Defaults to this template's empty SENSOR_PROFILES -- every
        # existing construction site that doesn't pass this argument is
        # completely unaffected. A caller that does pass one gets ONLY
        # that set; see resolve_sensor_profiles()'s docstring for why
        # this is never merged with the default.
        self.sensor_profiles = sensor_profiles if sensor_profiles is not None else SENSOR_PROFILES
        root = Path(root)
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.path = root / 'device-registrations.sqlite3'
        with self.connection() as db:
            db.executescript('''
            CREATE TABLE IF NOT EXISTS registrations(
              uid TEXT PRIMARY KEY, name TEXT NOT NULL, sensor_profile TEXT NOT NULL,
              status TEXT NOT NULL, staged_by TEXT NOT NULL, staged_at REAL NOT NULL,
              updated_by TEXT, updated_at REAL);
            CREATE TABLE IF NOT EXISTS credentials(
              uid TEXT PRIMARY KEY, ota_token_sha256 TEXT NOT NULL, generated_at REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS audit(
              id INTEGER PRIMARY KEY, actor TEXT NOT NULL, action TEXT NOT NULL,
              details TEXT NOT NULL, created REAL NOT NULL);
            ''')
            self._ensure_sensor_profile_snapshot_column(db)
            self._backfill_sensor_profile_snapshots(db)
        self.path.chmod(0o600)

    def _ensure_sensor_profile_snapshot_column(self, db):
        # live_entries() indexing self.sensor_profiles by name at READ
        # time would mean removing/renaming a profile after devices
        # were already staged/issued/activated under it raises a bare
        # KeyError on next registry access, and changing a profile's
        # metadata under the same name would silently change EXISTING
        # devices' compatibility identity out from under them,
        # retroactively. Avoided by persisting the profile's exact
        # metadata at the moment each registration is staged, and
        # having live_entries() (below) read that frozen snapshot
        # forever after -- a later config edit, rename, or removal can
        # never retarget an existing registration.
        cols = {row['name'] for row in db.execute('PRAGMA table_info(registrations)')}
        if 'sensor_profile_snapshot' not in cols:
            db.execute('ALTER TABLE registrations ADD COLUMN sensor_profile_snapshot TEXT')

    def _backfill_sensor_profile_snapshots(self, db):
        # One-time migration for rows that predate the snapshot column
        # (or, defensively, any row that somehow lacks one): freeze
        # today's config as their snapshot now, since no earlier
        # snapshot ever existed to recover. If a persisted row's profile
        # name isn't even resolvable in the CURRENT config, there is
        # nothing safe to freeze -- fail loudly with an actionable
        # message rather than silently drop compatibility data or defer
        # the failure to whatever request happens to call live_entries()
        # next.
        rows = db.execute(
            'SELECT uid, sensor_profile FROM registrations WHERE sensor_profile_snapshot IS NULL').fetchall()
        for row in rows:
            profile = self.sensor_profiles.get(row['sensor_profile'])
            if profile is None:
                raise RegistrationError(
                    f'Existing registration {row["uid"]!r} references sensor_profile '
                    f'{row["sensor_profile"]!r}, which is not in the current sensor_profiles '
                    'configuration -- refusing to start with stale, unresolvable registration data. '
                    'Restore that profile in config, or revoke/resolve this registration using the '
                    'prior configuration before switching.')
            db.execute('UPDATE registrations SET sensor_profile_snapshot=? WHERE uid=?',
                       (json.dumps(profile, sort_keys=True), row['uid']))

    @contextmanager
    def connection(self):
        db = sqlite3.connect(self.path, timeout=5)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def _audit(self, db, actor, action, details):
        db.execute('INSERT INTO audit(actor,action,details,created) VALUES(?,?,?,?)',
                   (actor, action, json.dumps(details), time.time()))

    def stage(self, uid, name, sensor_profile, actor, static_uids=()):
        if not TOKEN.fullmatch(uid):
            raise RegistrationError('Invalid device identifier')
        if not name or ',' in name:
            raise RegistrationError('Invalid device name')
        if sensor_profile not in self.sensor_profiles:
            raise RegistrationError('Unknown or unapproved sensor profile')
        if uid in static_uids:
            raise RegistrationError('This UID is already a static config entry; refusing to shadow an existing identity')
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            existing = db.execute('SELECT status FROM registrations WHERE uid=?', (uid,)).fetchone()
            if existing and existing['status'] != REVOKED:
                raise RegistrationError(f'A registration for this device already exists ({existing["status"]})')
            if existing:
                # Re-staging a previously revoked uid starts a clean lifecycle,
                # but never silently resurrects the old credential.
                db.execute('DELETE FROM registrations WHERE uid=?', (uid,))
                db.execute('DELETE FROM credentials WHERE uid=?', (uid,))
            # Freeze this profile's exact metadata NOW -- see
            # _ensure_sensor_profile_snapshot_column()'s comment. A later
            # config change can never retarget this registration, because
            # live_entries() reads this snapshot, never live config.
            snapshot = json.dumps(self.sensor_profiles[sensor_profile], sort_keys=True)
            db.execute(
                'INSERT INTO registrations(uid,name,sensor_profile,sensor_profile_snapshot,status,staged_by,staged_at) '
                'VALUES(?,?,?,?,?,?,?)',
                (uid, name, sensor_profile, snapshot, STAGED, actor, time.time()))
            self._audit(db, actor, 'stage', {'uid': uid, 'sensor_profile': sensor_profile})
        return {'uid': uid, 'status': STAGED}

    def staged(self):
        with self.connection() as db:
            rows = db.execute("SELECT * FROM registrations WHERE status=? ORDER BY staged_at", (STAGED,)).fetchall()
        return [dict(row) for row in rows]

    def status(self, uid):
        with self.connection() as db:
            row = db.execute('SELECT status FROM registrations WHERE uid=?', (uid,)).fetchone()
        return row['status'] if row else None

    def revoke(self, uid, actor, reason):
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            row = db.execute('SELECT status FROM registrations WHERE uid=?', (uid,)).fetchone()
            if not row:
                raise RegistrationError('Unknown registration')
            if row['status'] == REVOKED:
                raise RegistrationError('Registration is already revoked')
            db.execute("UPDATE registrations SET status=?, updated_by=?, updated_at=? WHERE uid=?",
                       (REVOKED, actor, time.time(), uid))
            db.execute('DELETE FROM credentials WHERE uid=?', (uid,))  # revoked credentials are never authenticatable again
            self._audit(db, actor, 'revoke', {'uid': uid, 'reason': reason})
        return {'uid': uid, 'status': REVOKED}

    def issue_credentials(self, uid, actor):
        """Mints a fresh, random OTA token and durably stores only its
        SHA-256 hash. The plaintext is returned ONCE, in this call's
        result, and is never persisted or retrievable again."""
        return self._mint(uid, actor, 'issue', expected_status=STAGED, new_status=ISSUED)

    def reissue_credentials(self, uid, actor):
        """Recovery path for a lost/undelivered response: while still
        `issued` (device has not yet made first contact), replace the
        token with a fresh one. Refused once `activated` -- a live
        device already depends on its current credential; use revoke()
        for that case instead."""
        return self._mint(uid, actor, 'reissue', expected_status=ISSUED, new_status=ISSUED)

    def _mint(self, uid, actor, action, expected_status, new_status):
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            row = db.execute('SELECT * FROM registrations WHERE uid=?', (uid,)).fetchone()
            if not row:
                raise RegistrationError('Unknown registration')
            if row['status'] != expected_status:
                raise RegistrationError(f'Registration is {row["status"]}, expected {expected_status}')
            token = secrets.token_urlsafe(32)
            db.execute('DELETE FROM credentials WHERE uid=?', (uid,))
            db.execute('INSERT INTO credentials(uid,ota_token_sha256,generated_at) VALUES(?,?,?)',
                       (uid, token_hash(token), time.time()))
            if new_status != row['status']:
                db.execute('UPDATE registrations SET status=?, updated_by=?, updated_at=? WHERE uid=?',
                           (new_status, actor, time.time(), uid))
            self._audit(db, actor, action, {'uid': uid})  # never audit the plaintext token
        return {'uid': uid, 'name': row['name'], 'sensor_profile': row['sensor_profile'], 'ota_device_token': token}

    def activate_if_current(self, uid, token_sha256):
        """The single atomic transaction that decides BOTH whether
        `token_sha256` is still uid's genuinely current, valid credential
        AND (if so) records activation -- called by ota.py's
        device_auth() (see the hook added there) ONLY after
        hmac.compare_digest has already confirmed this exact digest
        matched a SNAPSHOT read of the registry.

        Returns True if `token_sha256` is confirmed current (the caller
        MUST treat this as "authentication succeeds"), or False if it is
        NOT current any more (revoked, or superseded by a reissue that
        committed between the caller's snapshot read and this call --
        the caller MUST treat False as "authentication fails", the same
        as a wrong password, not merely "skip activation").

        Folding the validity check and the activation into one BEGIN
        IMMEDIATE transaction, with the caller's final accept/reject
        decision depending on this transaction's result, closes a race
        where a request carrying an already-superseded token could
        otherwise still succeed: SQLite serializes this against
        reissue_credentials()/revoke()'s own BEGIN IMMEDIATE
        transactions on the same file, so whichever commits first is
        authoritative for every subsequent reader.

        Handles every reachable state explicitly: ISSUED + matching hash
        -> activate, return True. ACTIVATED + still-matching hash (the
        ordinary case of a device polling again after its first contact)
        -> already active, still valid, return True, no redundant write.
        No matching row at all (hash superseded by reissue, or revoke
        already deleted the credential row entirely) -> return False.
        STAGED/REVOKED never have a credentials row to join against, so
        they fall into the "no matching row" case naturally."""
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            row = db.execute(
                'SELECT r.status FROM registrations r JOIN credentials c ON c.uid = r.uid '
                'WHERE r.uid=? AND c.ota_token_sha256=?', (uid, token_sha256)).fetchone()
            if not row:
                return False
            if row['status'] == ISSUED:
                db.execute("UPDATE registrations SET status=?, updated_at=? WHERE uid=?",
                           (ACTIVATED, time.time(), uid))
                self._audit(db, 'system', 'activate', {'uid': uid})
                return True
            if row['status'] == ACTIVATED:
                return True
            return False

    def live_entries(self):
        """Returns {uid: device_dict} for every registration in a
        servable state (issued or activated) -- device_dict has exactly
        the fields ota.py expects from a `devices[uid]` entry
        (token_sha256 plus the sensor profile's compatibility fields),
        ready to merge into that mapping.

        Reads `sensor_profile_snapshot` -- the profile's metadata as it
        was AT STAGE TIME -- never `self.sensor_profiles` (today's live
        config), so a later config change, rename, or removal can never
        retarget an existing registration."""
        with self.connection() as db:
            rows = db.execute(
                "SELECT r.uid, r.name, r.sensor_profile_snapshot, c.ota_token_sha256 FROM registrations r "
                "JOIN credentials c ON c.uid = r.uid WHERE r.status IN (?, ?)",
                (ISSUED, ACTIVATED)).fetchall()
        entries = {}
        for row in rows:
            entries[row['uid']] = {
                'token_sha256': row['ota_token_sha256'],
                'name': row['name'],
                **json.loads(row['sensor_profile_snapshot']),
            }
        return entries

    def live_uids(self):
        """Just the uids in a servable state (issued or activated), for
        callers that only need membership -- e.g. a telemetry ingestion
        gate that needs no per-device schema fields, only "is this uid
        currently a legitimate registered device"."""
        with self.connection() as db:
            rows = db.execute("SELECT uid FROM registrations WHERE status IN (?, ?)", (ISSUED, ACTIVATED)).fetchall()
        return {row['uid'] for row in rows}

    def issued_awaiting_activation(self):
        """Registrations that are `issued` but have NOT yet made first
        contact -- surfaced so a lost response or a page reload right
        after issue_credentials() doesn't make the just-issued device
        effectively disappear, with its token unrecoverable and no way
        to see it needed a reissue or a revoke."""
        with self.connection() as db:
            rows = db.execute("SELECT * FROM registrations WHERE status=? ORDER BY staged_at", (ISSUED,)).fetchall()
        return [dict(row) for row in rows]


def create_registration_router(config, root, registration_store=None, static_devices=None):
    """Engineer/operator-gated endpoints for the lifecycle above. Staging
    and revoking are engineer-only trust decisions; issuing/reissuing a
    credential is something the operator legitimately triggers
    themselves at the bench, the moment they need the plaintext for USB
    provisioning.

    `static_devices` must be the config's *original* static devices dict,
    captured before ota_bootstrap.py replaces config['devices'] with the
    merged DeviceRegistry -- collision checking needs to know what's
    genuinely static, not the already-merged view.
    """
    def no_cache(response: Response):
        response.headers['Cache-Control'] = 'no-store'
    router = APIRouter(dependencies=[Depends(no_cache)])
    if not config.get('enabled', False):
        return router
    admins = config.get('admins', {})
    operators = config.get('operators', {})
    origin = config.get('public_origin', '').rstrip('/')
    static_uids = set((static_devices or {}).keys())
    # A pre-built store (the production path, from ota_bootstrap.py) already
    # has its sensor_profiles resolved; a caller that doesn't supply one
    # (isolated tests of this router alone) gets it resolved here so
    # config-driven behavior is consistent either way.
    store = registration_store if registration_store is not None else \
        RegistrationStore(root, sensor_profiles=resolve_sensor_profiles(config))

    def principal(request, write=False):
        auth = request.headers.get('authorization', '')
        try:
            scheme, encoded = auth.split(' ', 1)
            user, token = base64.b64decode(encoded, validate=True).decode().split(':', 1)
            digest = token_hash(token)
            is_engineer = scheme.lower() == 'basic' and user in admins and hmac.compare_digest(digest, admins[user])
            is_operator = scheme.lower() == 'basic' and user in operators and hmac.compare_digest(digest, operators[user])
        except (ValueError, UnicodeError):
            is_engineer = is_operator = False
        if not (is_engineer or is_operator):
            raise HTTPException(401, 'Operator authentication required',
                                 headers={'WWW-Authenticate': 'Basic realm="Bardbox firmware", charset="UTF-8"'})
        if write and (request.headers.get('x-bardbox-ota') != '1' or request.headers.get('origin') != origin):
            raise HTTPException(403, 'Same-origin operator request required')
        return user, ('engineer' if is_engineer else 'operator')

    def require_engineer(request, write=False):
        user, role = principal(request, write)
        if role != 'engineer':
            raise HTTPException(403, 'Engineering authorization required')
        return user

    async def body(request, limit=8192):
        async def read():
            data = bytearray()
            async for chunk in request.stream():
                if len(data) + len(chunk) > limit:
                    raise HTTPException(413, 'Request too large')
                data.extend(chunk)
            return data
        try:
            return json.loads(await read())
        except (ValueError, UnicodeError):
            raise HTTPException(422, 'Invalid JSON')

    @router.get('/ota/v1/admin/registrations')
    def list_registrations(request: Request):
        principal(request)
        return {
            'staged': store.staged(),
            'issued': store.issued_awaiting_activation(),
            'sensor_profiles': list(store.sensor_profiles),
        }

    @router.post('/ota/v1/admin/registrations/stage')
    async def stage(request: Request):
        actor = require_engineer(request, True)
        p = await body(request)
        if not isinstance(p, dict) or set(p) != {'uid', 'name', 'sensor_profile'}:
            raise HTTPException(422, 'Invalid staging request')
        try:
            return store.stage(p['uid'], p['name'], p['sensor_profile'], actor, static_uids=static_uids)
        except RegistrationError as exc:
            raise HTTPException(409, str(exc))

    @router.post('/ota/v1/admin/registrations/issue')
    async def issue(request: Request):
        actor, _role = principal(request, True)
        p = await body(request)
        if not isinstance(p, dict) or set(p) != {'uid'}:
            raise HTTPException(422, 'Invalid request')
        try:
            return store.issue_credentials(p['uid'], actor)
        except RegistrationError as exc:
            raise HTTPException(409, str(exc))

    @router.post('/ota/v1/admin/registrations/reissue')
    async def reissue(request: Request):
        actor, _role = principal(request, True)
        p = await body(request)
        if not isinstance(p, dict) or set(p) != {'uid'}:
            raise HTTPException(422, 'Invalid request')
        try:
            return store.reissue_credentials(p['uid'], actor)
        except RegistrationError as exc:
            raise HTTPException(409, str(exc))

    @router.post('/ota/v1/admin/registrations/revoke')
    async def revoke(request: Request):
        actor = require_engineer(request, True)
        p = await body(request)
        if not isinstance(p, dict) or set(p) != {'uid', 'reason'}:
            raise HTTPException(422, 'Invalid request')
        try:
            return store.revoke(p['uid'], actor, p['reason'])
        except RegistrationError as exc:
            raise HTTPException(409, str(exc))

    return router
