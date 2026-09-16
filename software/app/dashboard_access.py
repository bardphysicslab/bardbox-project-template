"""Optional campus access boundary. Enable only behind a trusted HTTPS proxy."""
import base64
import binascii
import os
import secrets
from urllib.parse import urlsplit

from starlette.responses import JSONResponse

VIEW_PATHS = frozenset({'/', '/time', '/app/info', '/readings/latest'})


def enabled(prefix="BARDBOX"):
    # Unknown nonempty values fail closed instead of silently disabling access.
    return os.environ.get(f'{prefix}_REQUIRE_DASHBOARD_AUTH', '') not in ('', '0')


def settings(prefix="BARDBOX"):
    origin = os.environ.get(f'{prefix}_DASHBOARD_ORIGIN', '')
    parsed = urlsplit(origin)
    _ = parsed.port  # Reject malformed/out-of-range ports.
    admin = (os.environ.get(f'{prefix}_ADMIN_USER', ''), os.environ.get(f'{prefix}_ADMIN_PASSWORD', ''))
    viewer = (os.environ.get(f'{prefix}_VIEWER_USER', ''), os.environ.get(f'{prefix}_VIEWER_PASSWORD', ''))
    if (os.environ.get(f'{prefix}_REQUIRE_DASHBOARD_AUTH') != '1'
            or parsed.scheme != 'https' or not parsed.hostname
            or parsed.username or parsed.password or parsed.query or parsed.fragment
            or origin != f'https://{parsed.netloc}'
            or not all(admin) or ':' in admin[0]
            or not all(value.isascii() and not any(ord(c) < 32 or ord(c) == 127 for c in value)
                       for value in admin + viewer)
            or bool(viewer[0]) != bool(viewer[1]) or ':' in viewer[0]
            or (viewer[0] and viewer[0] == admin[0])):
        raise ValueError('Invalid dashboard access configuration')
    return origin, admin, viewer


def authenticate(header, admin, viewer):
    try:
        if len(header) > 2048:
            return None
        scheme, encoded = header.split(' ', 1)
        if scheme.lower() != 'basic':
            return None
        username, password = base64.b64decode(encoded, validate=True).decode('utf-8').split(':', 1)
    except (ValueError, UnicodeError, binascii.Error):
        return None
    role = None
    for label, pair in (('viewer', viewer), ('admin', admin)):
        user_ok = secrets.compare_digest(username.encode(), pair[0].encode())
        pass_ok = secrets.compare_digest(password.encode(), pair[1].encode())
        if all(pair) and user_ok and pass_ok:
            role = label
    return role


class DashboardAccess:
    def __init__(self, app, config_prefix="BARDBOX", view_paths=VIEW_PATHS, disabled_paths=()):
        self.app = app
        self.config_prefix = config_prefix
        self.view_paths = frozenset(view_paths)
        self.disabled_paths = frozenset(disabled_paths)

    async def __call__(self, scope, receive, send):
        if scope['type'] == 'websocket' and enabled(self.config_prefix):
            await send({'type': 'websocket.close', 'code': 1008})
            return
        if scope['type'] != 'http' or not enabled(self.config_prefix):
            return await self.app(scope, receive, send)
        path, method = scope['path'], scope['method']
        # Minimal, side-effect-free watchdog response remains available locally.
        if path == '/health' and method in ('GET', 'HEAD'):
            return await self.app(scope, receive, send)
        headers = {}
        for key, value in scope.get('headers', []):
            if key in headers:
                return await JSONResponse({'detail': 'Duplicate headers rejected'}, 400)(scope, receive, send)
            headers[key] = value.decode('latin-1')
        try:
            origin, admin, viewer = settings(self.config_prefix)
        except ValueError:
            return await JSONResponse({'detail': 'Dashboard access is not configured'}, 503)(scope, receive, send)
        if scope.get('scheme') != 'https' or headers.get(b'host') != urlsplit(origin).netloc:
            return await JSONResponse({'detail': 'Use the configured HTTPS dashboard address'}, 403)(scope, receive, send)
        # This deployment uses outbound Twilio polling. A browser credential must
        # never stand in for provider signature validation on an inbound webhook.
        if path in self.disabled_paths:
            return await JSONResponse({'detail': 'Inbound webhook disabled in campus mode'}, 403)(scope, receive, send)
        role = authenticate(headers.get(b'authorization', ''), admin, viewer)
        if role is None:
            return await JSONResponse({'detail': 'Login required'}, 401,
                headers={'WWW-Authenticate': 'Basic realm="BardBox", charset="UTF-8"', 'Cache-Control': 'no-store'})(scope, receive, send)
        safe = method in ('GET', 'HEAD')
        if role == 'viewer' and not (safe and (path in self.view_paths or path.startswith('/static/'))):
            return await JSONResponse({'detail': 'Administrator access required'}, 403)(scope, receive, send)
        if not safe and (headers.get(b'origin') != origin or headers.get(b'x-bardbox-request') != '1'):
            return await JSONResponse({'detail': 'Same-origin request required'}, 403)(scope, receive, send)
        scope.setdefault('state', {})['dashboard_role'] = role
        async def protected_send(message):
            if message['type'] == 'http.response.start':
                message = dict(message)
                message['headers'] = [(k, v) for k, v in message.get('headers', [])
                                      if k.lower() not in (b'cache-control', b'x-frame-options')]
                message['headers'] += [(b'cache-control', b'no-store'), (b'x-frame-options', b'DENY')]
            await send(message)
        await self.app(scope, receive, protected_send)
