#!/usr/bin/env python3
"""Explicit USB commissioning; never flashes, erases NVS, or requests a reboot.

Keep the input JSON and its credentials private. --dry-run validates locally and
prints only UID and record length. The running firmware file must be the exact
binary currently installed; the device independently checks its full SHA256.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import time
import zlib

FIELDS = ('uid', 'name', 'wifi_ssid', 'wifi_password', 'telemetry_url', 'telemetry_token',
          'ota_origin', 'ota_device_token', 'ca_file', 'signing_key_id', 'signing_public_key_file')
TOKEN = re.compile(r'[A-Za-z0-9_.-]{1,96}\Z')
ORIGIN = re.compile(r'https://([A-Za-z0-9.-]+)(?::([0-9]{1,5}))?\Z')


def origin_valid(value):
    match = ORIGIN.fullmatch(value)
    return bool(match and len(value) <= 300 and 0 < len(match[1]) <= 253
                and not match[1].startswith('.') and not match[1].endswith('.')
                and (match[2] is None or 0 < int(match[2]) <= 65535))


def encode_config(config, firmware, directory):
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    if not isinstance(config, dict) or set(config) != set(FIELDS) or any(not isinstance(v, str) for v in config.values()):
        raise ValueError('Invalid configuration fields')
    c = dict(config)
    c['ca'] = (directory / c['ca_file']).read_text()
    c['public'] = (directory / c['signing_public_key_file']).read_text()
    limits = dict(uid=48, name=64, wifi_ssid=32, wifi_password=63, telemetry_url=512,
                  telemetry_token=256, ota_origin=300, ota_device_token=256,
                  ca=4096, signing_key_id=96, public=1024)
    for field, limit in limits.items():
        value = c[field]
        if len(value.encode('utf-8')) > limit or any(ord(ch) == 127 or
                (ord(ch) < 32 and not (field in ('ca', 'public') and ch in '\r\n')) for ch in value):
            raise ValueError('Invalid configuration value')
    if (not TOKEN.fullmatch(c['uid']) or not TOKEN.fullmatch(c['signing_key_id']) or
            not c['name'] or ',' in c['name'] or not c['wifi_ssid'] or
            (c['wifi_password'] and len(c['wifi_password'].encode()) < 8) or
            not re.fullmatch(r'[A-Za-z0-9_-]{32,256}', c['ota_device_token']) or
            not origin_valid(c['ota_origin'])):
        raise ValueError('Invalid identity or network configuration')
    url = c['telemetry_url']
    origin = url.split('/', 3)
    base = '/'.join(origin[:3])
    if not origin_valid(base) or '#' in url or '?' in url or any(ord(ch) <= 32 or ord(ch) >= 127 for ch in url):
        raise ValueError('Invalid telemetry URL')
    key = serialization.load_pem_public_key(c['public'].encode())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError('Signing key must be public P-256')
    if not x509.load_pem_x509_certificates(c['ca'].encode()):
        raise ValueError('Missing CA certificate')
    if not 0 < len(firmware) <= 2097152:
        raise ValueError('Invalid running firmware size')
    record = bytearray(b'DC1' + struct.pack('<I', len(firmware)))
    values = [c[k] for k in ('uid', 'name', 'wifi_ssid', 'wifi_password', 'telemetry_url',
              'telemetry_token', 'ota_origin', 'ota_device_token', 'ca', 'signing_key_id', 'public')]
    values.append(hashlib.sha256(firmware).hexdigest())
    for value in values:
        encoded = value.encode('utf-8')
        record += struct.pack('<H', len(encoded)) + encoded
    record += struct.pack('<I', zlib.crc32(record))
    if len(record) > 8192:
        raise ValueError('Configuration too large')
    return bytes(record)


def commission(serial_port, record):
    # Never include outgoing commands or unexpected device text in exceptions:
    # a misconfigured serial echo could otherwise expose credential fragments.
    def exchange(command, expected, timeout=10):
        serial_port.reset_input_buffer()
        serial_port.write(command.encode('ascii') + b'\n')
        serial_port.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = serial_port.readline(160)
            if line.rstrip(b'\r\n') == expected.encode('ascii'):
                return
            if line.startswith(b'ERR,'):
                raise RuntimeError('Device rejected provisioning operation')
        raise RuntimeError('Provisioning response timed out; inspect status before retrying')
    exchange('PROVISION_BEGIN ' + str(len(record)), 'PROVISION_BEGIN_OK')
    for offset in range(0, len(record), 16):
        part = record[offset:offset+16]
        exchange(f'PROVISION_DATA {offset} {part.hex()}', f'PROVISION_DATA_OK,{offset+len(part)}')
    exchange('PROVISION_COMMIT', 'PROVISION_COMMIT_OK,activation=next_boot', timeout=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--running-firmware', type=Path, required=True)
    parser.add_argument('--port')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--assert-dtr', action='store_true', help='Assert USB CDC host-ready DTR without RTS; use only after verifying board reset behavior')
    args = parser.parse_args()
    try:
        config = json.loads(args.config.read_text())
        record = encode_config(config, args.running_firmware.read_bytes(), args.config.parent)
        if args.dry_run:
            print(f'Configuration valid for {config["uid"]}; {len(record)} bytes. No device contacted.')
            return
        if not args.port:
            parser.error('--port is required unless --dry-run is used')
        import serial
        connection = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=5)
        connection.dtr = args.assert_dtr
        connection.rts = False
        connection.port = args.port
        connection.open()
        with connection:
            commission(connection, record)
        print('Configuration stored and verified. It activates on the next boot. No reboot requested.')
    except (ValueError, OSError, RuntimeError):
        # Intentionally omit exception details: private JSON and serial data may
        # contain credentials. Do not retry a possibly committed mutation blindly.
        raise SystemExit('Provisioning failed. Check private inputs and device PROVISION_STATUS; no automatic erase or reboot.')


if __name__ == '__main__':
    main()
