"""Live merge of the static OTA config's `devices` map with
device_registration.py's persistent registry -- this is what actually
integrates dynamic registration into OTA authorization.

`DeviceRegistry` is a `collections.abc.Mapping`, so it is a drop-in
replacement wherever `config['devices']` is currently consulted: build
one in ota_bootstrap.py and put it in the config dict passed to
`create_ota_router()` in place of the plain static dict.

Activation is deliberate and post-authentication, not a side effect of
a Mapping read: `__getitem__` (exercised by `.items()`/`.values()` on
EVERY lookup, not just the one a caller cares about -- e.g. an admin
overview enumerating all devices) must never mutate lifecycle state as
a side effect, independent of whether the caller ever validated a
credential at all. Activation is `note_successful_auth(uid,
token_sha256)`, called explicitly by ota.py's device_auth() (a small,
reviewed hook there) ONLY after `hmac.compare_digest` has already
confirmed that exact digest matched a SNAPSHOT read of this Mapping.

A snapshot match can go stale between the read and the activation call
if a concurrent reissue commits in between; treating activation as a
pure side effect (not returning any verdict) would let device_auth()
return success regardless. `note_successful_auth()` RETURNS the actual
verdict -- True only if `token_sha256` is confirmed, in the same atomic
transaction as any activation, to still be current -- and device_auth()
rejects the request when it's False, exactly like a wrong token. See
device_registration.py's `activate_if_current()` for the single atomic
transaction this delegates to.

Static entries always win on collision (never silently overwritten by a
dynamic registration) -- device_registration.py's own stage() already
refuses to stage a uid that collides with a static entry, so this is a
second, independent guard rather than the only one.
"""
from collections.abc import Mapping


class DeviceRegistry(Mapping):
    def __init__(self, static_devices, registration_store):
        self._static = dict(static_devices)
        self._registrations = registration_store

    def _dynamic(self):
        entries = self._registrations.live_entries()
        # Static entries are authoritative; a dynamic registration can
        # never shadow one (belt-and-suspenders with stage()'s own check,
        # which also refuses this at registration time).
        for uid in self._static:
            entries.pop(uid, None)
        return entries

    def note_successful_auth(self, uid, token_sha256):
        """Called by ota.py's device_auth() immediately after a
        SNAPSHOT read of this Mapping matched `token_sha256` -- never
        for a failed match, and never merely from reading/enumerating
        this Mapping on its own.

        Returns True or False, and the caller MUST use it as the final
        authentication verdict, not just an activation side effect: a
        static-config uid has no registration row to race against, so
        its earlier snapshot match is already authoritative and this
        returns True unconditionally. A dynamic uid's snapshot match can
        go stale between the snapshot read and this call (a concurrent
        reissue/revoke), so this delegates to
        RegistrationStore.activate_if_current(), whose single atomic
        transaction re-checks validity and activates together -- see its
        docstring for why the two cannot be separated safely."""
        if uid in self._static:
            return True
        return self._registrations.activate_if_current(uid, token_sha256)

    def __getitem__(self, uid):
        if uid in self._static:
            return self._static[uid]
        dynamic = self._dynamic()
        if uid in dynamic:
            return dynamic[uid]
        raise KeyError(uid)

    def __iter__(self):
        yield from self._static
        yield from self._dynamic()

    def __len__(self):
        return len(self._static) + len(self._dynamic())

    def __contains__(self, uid):
        return uid in self._static or uid in self._dynamic()


def build_device_registry(config, root, registration_store=None):
    """Convenience for ota_bootstrap.py: builds the merged registry from
    a loaded OTA config dict and the state root."""
    from .device_registration import RegistrationStore
    if registration_store is None:
        registration_store = RegistrationStore(root)
    return DeviceRegistry(config.get('devices', {}), registration_store)
