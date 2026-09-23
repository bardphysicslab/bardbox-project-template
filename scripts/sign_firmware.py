#!/usr/bin/env python3
"""Sign a BardBox release offline. Never upload the private signing key."""
import argparse
import base64
import getpass
import hashlib
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from software.app.ota import canonical_manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--metadata', type=Path, required=True, help='JSON containing manifest fields except size and sha256')
    p.add_argument('--key', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    image = a.image.read_bytes()
    manifest = json.loads(a.metadata.read_text())
    manifest.update(size=len(image), sha256=hashlib.sha256(image).hexdigest())
    message = canonical_manifest(manifest)
    key_bytes = a.key.read_bytes()
    try:
        key = serialization.load_pem_private_key(key_bytes, password=None)
    except TypeError:
        key = serialization.load_pem_private_key(key_bytes, password=getpass.getpass('Signing key password: ').encode())
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        p.error('Use an ECDSA P-256 private key')
    envelope = {'manifest':manifest, 'signature':base64.b64encode(key.sign(message, ec.ECDSA(hashes.SHA256()))).decode()}
    with a.output.open('x') as f:
        json.dump(envelope, f, indent=2)
        f.write('\n')
    print('Signed release:', manifest['release_id'])

if __name__ == '__main__':
    main()
