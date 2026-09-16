"""Opt-in BardBox OTA control plane. Measurement APIs remain read-only.

Copied without project-specific logic into adopting projects. Runtime state is
SQLite (assignments/releases only, never the measurement archive).
"""
import base64
import hashlib
import hmac
import json
import re
import sqlite3
import time
import asyncio
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import urlsplit

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import HTMLResponse, Response
from starlette.concurrency import run_in_threadpool

FIELDS = ('format', 'release_id', 'component', 'version', 'target', 'layout',
          'config_schema', 'queue_schema', 'size', 'sha256', 'key_id')
TOKEN = re.compile(r'^[A-Za-z0-9_.-]{1,96}$')
DIGEST = re.compile(r'^[a-f0-9]{64}$')
STATES = {'assigned', 'waiting_for_transport', 'downloading', 'verifying',
          'pending_reboot', 'validating', 'confirmed', 'failed', 'rolled_back'}
TERMINAL = {'confirmed', 'failed', 'rolled_back'}
MAX_IMAGE = 2 * 1024 * 1024
MAX_BODY = 3 * 1024 * 1024


def canonical_manifest(manifest):
    if not isinstance(manifest, dict) or set(manifest) != set(FIELDS):
        raise ValueError('Invalid manifest fields')
    if manifest['format'] != 'bardbox-ota-v1':
        raise ValueError('Unsupported manifest format')
    if type(manifest['size']) is not int or not 0 < manifest['size'] <= MAX_IMAGE:
        raise ValueError('Invalid image size')
    if not isinstance(manifest['sha256'], str) or not DIGEST.fullmatch(manifest['sha256']):
        raise ValueError('Invalid image digest')
    for key in set(FIELDS) - {'size', 'sha256'}:
        if not isinstance(manifest[key], str) or not TOKEN.fullmatch(manifest[key]):
            raise ValueError('Invalid manifest value: ' + key)
    # Fixed order, ASCII, decimal size, one trailing LF. No JSON canonicalization.
    return ''.join(str(manifest[key]) + '\n' for key in FIELDS).encode('ascii')


def verify_release(envelope, image, keys):
    if not isinstance(envelope, dict) or set(envelope) != {'manifest', 'signature'}:
        raise ValueError('Expected manifest and signature')
    m = envelope['manifest']
    message = canonical_manifest(m)
    if len(image) != m['size'] or hashlib.sha256(image).hexdigest() != m['sha256']:
        raise ValueError('Image size or digest mismatch')
    if m['key_id'] not in keys:
        raise ValueError('Untrusted signing key')
    try:
        public = serialization.load_pem_public_key(keys[m['key_id']].encode('ascii'))
        if not isinstance(public, ec.EllipticCurvePublicKey) or not isinstance(public.curve, ec.SECP256R1):
            raise ValueError('Signing key must be P-256')
        signature = base64.b64decode(envelope['signature'], validate=True)
        public.verify(signature, message, ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, TypeError, ValueError) as exc:
        raise ValueError('Invalid release signature') from exc
    return m


def token_hash(token):
    return hashlib.sha256(token.encode('utf-8')).hexdigest()


def compatible(device, manifest):
    return all(device.get(k) == manifest[k] for k in ('component', 'target', 'layout', 'config_schema', 'queue_schema')) and manifest['size'] <= device['slot_bytes']


class OTAStore:
    def __init__(self, root):
        root = Path(root)
        root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.path = root / 'ota.sqlite3'
        with self.connection() as db:
            db.executescript('''
            CREATE TABLE IF NOT EXISTS releases (
              id TEXT PRIMARY KEY, envelope TEXT NOT NULL, image BLOB NOT NULL,
              actor TEXT NOT NULL, created REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS assignments (
              uid TEXT NOT NULL, generation INTEGER NOT NULL, release_id TEXT NOT NULL,
              request_id TEXT NOT NULL, actor TEXT NOT NULL, created REAL NOT NULL,
              active INTEGER NOT NULL DEFAULT 1, seq INTEGER NOT NULL DEFAULT -1,
              status TEXT, PRIMARY KEY(uid,generation), UNIQUE(uid,request_id));
            CREATE TABLE IF NOT EXISTS events (
              id INTEGER PRIMARY KEY, uid TEXT NOT NULL, generation INTEGER NOT NULL,
              sequence INTEGER NOT NULL, payload TEXT NOT NULL, received REAL NOT NULL,
              UNIQUE(uid,generation,sequence));
            CREATE TABLE IF NOT EXISTS audit (
              id INTEGER PRIMARY KEY, actor TEXT NOT NULL, action TEXT NOT NULL,
              details TEXT NOT NULL, created REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS contacts (
              uid TEXT PRIMARY KEY, received REAL NOT NULL);
            ''')
        self.path.chmod(0o600)

    @contextmanager
    def connection(self):
        db = sqlite3.connect(self.path, timeout=5)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def add_release(self, envelope, image, actor):
        rid = envelope['manifest']['release_id']
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            old = db.execute('SELECT envelope FROM releases WHERE id=?', (rid,)).fetchone()
            if old:
                if json.loads(old['envelope'])['manifest'] != envelope['manifest']:
                    raise HTTPException(409, 'Release ID already has different contents')
                return {'release_id': rid, 'existing': True}
            db.execute('INSERT INTO releases VALUES(?,?,?,?,?)', (rid, json.dumps(envelope), image, actor, time.time()))
            db.execute('INSERT INTO audit(actor,action,details,created) VALUES(?,?,?,?)', (actor, 'release', rid, time.time()))
        return {'release_id': rid, 'existing': False}

    def assign(self, uid, rid, request_id, actor, device):
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            old = db.execute('SELECT * FROM assignments WHERE uid=? AND request_id=?', (uid, request_id)).fetchone()
            if old:
                if old['release_id'] != rid:
                    raise HTTPException(409, 'Request ID already used for a different release')
                return {'generation': old['generation'], 'existing': True}
            release = db.execute('SELECT envelope FROM releases WHERE id=?', (rid,)).fetchone()
            if not release:
                raise HTTPException(404, 'Unknown release')
            if not compatible(device, json.loads(release['envelope'])['manifest']):
                raise HTTPException(409, 'Release is incompatible with this device')
            generation = db.execute('SELECT COALESCE(MAX(generation),0)+1 FROM assignments WHERE uid=?', (uid,)).fetchone()[0]
            db.execute('INSERT INTO assignments(uid,generation,release_id,request_id,actor,created) VALUES(?,?,?,?,?,?)', (uid, generation, rid, request_id, actor, time.time()))
            db.execute('INSERT INTO audit(actor,action,details,created) VALUES(?,?,?,?)', (actor, 'assign', json.dumps({'uid': uid, 'generation': generation, 'release_id': rid}), time.time()))
        return {'generation': generation, 'existing': False}

    def cancel(self, uid, generation, actor):
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            row = db.execute('SELECT * FROM assignments WHERE uid=? ORDER BY generation DESC LIMIT 1', (uid,)).fetchone()
            if not row or row['generation'] != generation:
                raise HTTPException(409, 'Assignment changed; refresh before stopping delivery')
            if row['active']:
                db.execute('UPDATE assignments SET active=0 WHERE uid=? AND generation=?', (uid, generation))
                db.execute('INSERT INTO audit(actor,action,details,created) VALUES(?,?,?,?)',
                           (actor, 'stop_delivery', json.dumps({'uid':uid, 'generation':generation}), time.time()))
        return {'stopped': True}

    def contact(self, uid):
        with self.connection() as db:
            db.execute('INSERT INTO contacts VALUES(?,?) ON CONFLICT(uid) DO UPDATE SET received=excluded.received', (uid, time.time()))

    def latest(self, uid):
        with self.connection() as db:
            row = db.execute('SELECT * FROM assignments WHERE uid=? ORDER BY generation DESC LIMIT 1', (uid,)).fetchone()
            return dict(row) if row else None

    def report(self, uid, payload):
        required = {'generation', 'sequence', 'boot_id', 'state', 'running_version', 'running_sha256', 'bytes', 'failure'}
        if not isinstance(payload, dict) or set(payload) != required:
            raise HTTPException(422, 'Invalid status fields')
        for key in ('generation', 'sequence', 'bytes'):
            if type(payload[key]) is not int or not 0 <= payload[key] <= 2147483647:
                raise HTTPException(422, 'Invalid status counter')
        for key in ('boot_id', 'running_version', 'failure'):
            if not isinstance(payload[key], str) or not TOKEN.fullmatch(payload[key]):
                raise HTTPException(422, 'Invalid status value')
        if not isinstance(payload['state'], str) or payload['state'] not in STATES or not isinstance(payload['running_sha256'], str) or not DIGEST.fullmatch(payload['running_sha256']):
            raise HTTPException(422, 'Invalid state or running image digest')
        with self.connection() as db:
            db.execute('BEGIN IMMEDIATE')
            row = db.execute('SELECT * FROM assignments WHERE uid=? ORDER BY generation DESC LIMIT 1', (uid,)).fetchone()
            if not row or row['generation'] != payload['generation']:
                raise HTTPException(409, 'Status is not for the current assignment')
            release = db.execute('SELECT envelope FROM releases WHERE id=?', (row['release_id'],)).fetchone()
            manifest = json.loads(release['envelope'])['manifest']
            if payload['bytes'] > manifest['size']:
                raise HTTPException(422, 'Progress exceeds image size')
            if payload['state'] == 'confirmed' and (payload['running_version'] != manifest['version'] or payload['running_sha256'] != manifest['sha256']):
                raise HTTPException(409, 'Confirmed image does not match assigned release')
            encoded = json.dumps(payload, sort_keys=True)
            if payload['sequence'] <= row['seq']:
                previous = db.execute('SELECT payload FROM events WHERE uid=? AND generation=? AND sequence=?', (uid, row['generation'], payload['sequence'])).fetchone()
                if previous and previous['payload'] == encoded:
                    return {'accepted': True, 'duplicate': True}
                raise HTTPException(409, 'Outdated or conflicting status sequence')
            if row['status'] and json.loads(row['status'])['state'] in TERMINAL:
                old = json.loads(row['status'])
                if payload['state'] != old['state']:
                    raise HTTPException(409, 'Terminal outcome cannot change; create a new assignment')
            db.execute('UPDATE assignments SET seq=?,status=? WHERE uid=? AND generation=?', (payload['sequence'], encoded, uid, row['generation']))
            db.execute('INSERT INTO events(uid,generation,sequence,payload,received) VALUES(?,?,?,?,?)', (uid, row['generation'], payload['sequence'], encoded, time.time()))
        return {'accepted': True, 'duplicate': False}


def create_ota_router(config, root, page_path=None):
    def no_cache(response: Response):
        response.headers['Cache-Control'] = 'no-store'
    router = APIRouter(dependencies=[Depends(no_cache)])
    if not config.get('enabled', False):
        return router
    admins = config.get('admins', {})
    devices = config.get('devices', {})
    keys = config.get('public_keys', {})
    origin = config.get('public_origin', '').rstrip('/')
    parsed_origin = urlsplit(origin)
    if (parsed_origin.scheme != 'https' or not parsed_origin.hostname or parsed_origin.path
            or parsed_origin.username or parsed_origin.password or parsed_origin.query
            or parsed_origin.fragment or not admins or not keys):
        raise ValueError('OTA requires HTTPS public_origin, admin hashes and signing keys')
    for pem in keys.values():
        key = serialization.load_pem_public_key(pem.encode('ascii'))
        if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
            raise ValueError('OTA public keys must be ECDSA P-256')
    for digest in list(admins.values()) + [d.get('token_sha256', '') for d in devices.values()]:
        if not isinstance(digest, str) or not DIGEST.fullmatch(digest):
            raise ValueError('OTA credentials must be SHA-256 hashes of random tokens')
    all_hashes = list(admins.values()) + [d['token_sha256'] for d in devices.values()]
    if len(set(all_hashes)) != len(all_hashes):
        raise ValueError('OTA credentials must be unique per principal and role')
    for uid, device in devices.items():
        if not TOKEN.fullmatch(uid) or type(device.get('slot_bytes')) is not int or not 0 < device['slot_bytes'] <= MAX_IMAGE:
            raise ValueError('Invalid OTA device registration')
        if any(not isinstance(device.get(k), str) or not TOKEN.fullmatch(device[k]) for k in ('component','target','layout','config_schema','queue_schema')):
            raise ValueError('Invalid device compatibility registration')
    store = OTAStore(root)

    def admin(request, write=False):
        # Browser-managed Basic credentials; custom same-origin write header blocks
        # cross-site form submissions. Do not enable CORS for this router.
        auth = request.headers.get('authorization', '')
        try:
            scheme, encoded = auth.split(' ', 1)
            user, token = base64.b64decode(encoded, validate=True).decode().split(':', 1)
            ok = scheme.lower() == 'basic' and user in admins and hmac.compare_digest(token_hash(token), admins[user])
        except (ValueError, UnicodeError):
            ok = False
        if not ok:
            raise HTTPException(401, 'Operator authentication required', headers={'WWW-Authenticate': 'Basic realm="Bardbox firmware", charset="UTF-8"'})
        if write and (request.headers.get('x-bardbox-ota') != '1' or request.headers.get('origin') != origin):
            raise HTTPException(403, 'Same-origin operator request required')
        return user

    def device_auth(request):
        auth = request.headers.get('authorization', '')
        scheme, _, token = auth.partition(' ')
        digest = token_hash(token)
        if scheme.lower() == 'bearer' and token:
            for uid, entry in devices.items():
                if hmac.compare_digest(digest, entry['token_sha256']):
                    return uid
        raise HTTPException(401, 'Device authentication required')

    async def body(request, limit=8192):
        async def read():
            data = bytearray()
            async for chunk in request.stream():
                if len(data) + len(chunk) > limit:
                    raise HTTPException(413, 'Request too large')
                data.extend(chunk)
            return data
        try:
            data = await asyncio.wait_for(read(), timeout=30)
        except asyncio.TimeoutError:
            raise HTTPException(408, 'Request body timeout')
        try:
            return json.loads(data)
        except (ValueError, UnicodeError):
            raise HTTPException(422, 'Invalid JSON')

    def name(value):
        if not isinstance(value, str) or not TOKEN.fullmatch(value):
            raise HTTPException(422, 'Invalid identifier')
        return value

    @router.get('/firmware', response_class=HTMLResponse)
    def page(request: Request):
        admin(request)
        path = page_path or Path(__file__).with_name('templates') / 'firmware.html'
        return HTMLResponse(Path(path).read_text(), headers={'Cache-Control':'no-store', 'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'"})

    @router.get('/ota/v1/admin/overview')
    def overview(request: Request):
        admin(request)
        with store.connection() as db:
            releases = [json.loads(r[0])['manifest'] for r in db.execute('SELECT envelope FROM releases ORDER BY created DESC')]
            rows = []
            for uid, registration in devices.items():
                latest = store.latest(uid)
                status = json.loads(latest['status']) if latest and latest['status'] else None
                seen = db.execute('SELECT received FROM contacts WHERE uid=?', (uid,)).fetchone()
                seen = seen[0] if seen else None
                rows.append({'uid':uid, **{k:registration[k] for k in ('component','target','layout','config_schema','queue_schema','slot_bytes')},
                             'assignment':latest, 'status':status, 'last_seen':seen,
                             'stale':seen is None or time.time()-seen > config.get('stale_after_s', 900)})
            audit = [dict(r) for r in db.execute('SELECT * FROM audit ORDER BY id DESC LIMIT 100')]
        return {'devices':rows, 'releases':releases, 'audit':audit}

    @router.post('/ota/v1/admin/releases')
    async def upload(request: Request):
        actor = admin(request, True)
        payload = await body(request, MAX_BODY)
        try:
            if not isinstance(payload, dict) or set(payload) != {'envelope','image_base64'}:
                raise ValueError('Expected envelope and image_base64')
            image = base64.b64decode(payload['image_base64'], validate=True)
            await run_in_threadpool(verify_release, payload['envelope'], image, keys)
        except (ValueError, TypeError, KeyError) as exc:
            raise HTTPException(422, str(exc))
        return await run_in_threadpool(store.add_release, payload['envelope'], image, actor)

    @router.post('/ota/v1/admin/assignments')
    async def assign(request: Request):
        actor = admin(request, True)
        p = await body(request)
        if not isinstance(p, dict) or set(p) != {'uid','release_id','request_id'}:
            raise HTTPException(422, 'Invalid assignment fields')
        uid, rid, request_id = (name(p[k]) for k in ('uid','release_id','request_id'))
        if uid not in devices:
            raise HTTPException(404, 'Unknown device')
        return await run_in_threadpool(store.assign, uid, rid, request_id, actor, devices[uid])

    @router.post('/ota/v1/admin/stop-delivery')
    async def cancel(request: Request):
        actor = admin(request, True)
        p = await body(request)
        if not isinstance(p, dict) or set(p) != {'uid', 'generation'} or type(p['generation']) is not int:
            raise HTTPException(422, 'Invalid stop request')
        uid = name(p['uid'])
        if uid not in devices:
            raise HTTPException(404, 'Unknown device')
        return await run_in_threadpool(store.cancel, uid, p['generation'], actor)

    @router.get('/ota/v1/device/assignment')
    def assignment(request: Request):
        uid = device_auth(request)
        store.contact(uid)
        row = store.latest(uid)
        if not row or not row['active']:
            return Response(status_code=204, headers={'Cache-Control':'no-store'})
        with store.connection() as db:
            release = db.execute('SELECT envelope FROM releases WHERE id=?', (row['release_id'],)).fetchone()
        envelope = json.loads(release[0])
        if not compatible(devices[uid], envelope['manifest']):
            raise HTTPException(409, 'Registration changed; review assignment')
        return {'generation':row['generation'], 'envelope':envelope,
                'artifact_path':'/ota/v1/device/artifact/' + row['release_id']}

    @router.get('/ota/v1/device/artifact/{release_id}')
    def artifact(release_id: str, request: Request):
        uid = device_auth(request)
        row = store.latest(uid)
        if not row or not row['active'] or row['release_id'] != release_id:
            raise HTTPException(404, 'No matching active assignment')
        with store.connection() as db:
            release = db.execute('SELECT envelope,image FROM releases WHERE id=?', (release_id,)).fetchone()
        envelope = json.loads(release['envelope'])
        if not compatible(devices[uid], envelope['manifest']):
            raise HTTPException(409, 'Registration changed; review assignment')
        return Response(bytes(release['image']), media_type='application/octet-stream', headers={'Cache-Control':'no-store'})

    @router.post('/ota/v1/device/status')
    async def status(request: Request):
        uid = device_auth(request)
        result = await run_in_threadpool(store.report, uid, await body(request))
        await run_in_threadpool(store.contact, uid)
        return result

    return router
