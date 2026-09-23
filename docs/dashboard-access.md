# Optional campus dashboard access

Canonical policy: Bardbox `docs/network-access.md`. This optional module is not
installed by default: adopt it only after inventorying the consumer's routes.

```python
from software.app.dashboard_access import DashboardAccess
app.add_middleware(DashboardAccess, config_prefix="BARDBOX",
                   view_paths=("/", "/time", "/app/info", "/readings/latest"),
                   disabled_paths=("/unused-provider-webhook",))
```

Configure `BARDBOX_REQUIRE_DASHBOARD_AUTH=1`, `BARDBOX_DASHBOARD_ORIGIN` as an exact
HTTPS origin (no trailing slash), `BARDBOX_ADMIN_USER/PASSWORD`, and optionally
`BARDBOX_VIEWER_USER/PASSWORD`. Use different usernames. Missing enabled settings
fail closed. Unset/0 preserves legacy behavior; it is not a secure deployment.

Only GET/HEAD viewer routes and static assets are viewer-accessible. All other
routes require admin; all mutations additionally require exact Origin and
`X-Bardbox-Request: 1`. Supply that header in browser fetch calls. Disable editing
controls for `request.state.dashboard_role == "viewer"`; backend checks remain
mandatory. `/health` must contain no sensitive data or side effects.

Bind the application to loopback behind an IT-approved HTTPS proxy. Do not trust
forwarded headers from arbitrary peers. Proxy/firewall configuration, certificate
renewal, credential storage/rotation and real campus/VPN isolation must be verified
per deployment. Basic auth is a deliberately small initial option: no individual
account directory, login throttling, self-service password reset or reliable
browser logout is included. Use proxy rate limiting and unique strong credentials;
prefer institutional identity integration if more users need individual access.

RKC is the first consumer and disables its unused inbound SMS webhook while
retaining outbound polling. This module does not authenticate device ingestion,
OTA or other machine clients. Those need their own contracts. Tests exercise
role bypasses, malformed credentials, incomplete settings, HTTPS/host checks,
cross-site mutations and the watchdog exemption without real devices/messages.

Credentials must use printable ASCII characters; usernames cannot contain a colon.
WebSocket access is disabled in this mode until a consumer defines its own policy.
