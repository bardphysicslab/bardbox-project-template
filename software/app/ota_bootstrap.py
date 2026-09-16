"""Load optional management configuration independently of measurement config."""
import json
import os
from pathlib import Path


def install_ota(app):
    path = os.environ.get('BARDBOX_OTA_CONFIG')
    if not path:
        return False
    config = json.loads(Path(path).read_text())
    if not config.get('enabled', False):
        return False
    root = Path(config['state_directory'])
    if not root.is_absolute():
        raise ValueError('OTA state_directory must be absolute')
    # Optional dependency: existing installations run without OTA packages.
    from .ota import create_ota_router
    app.include_router(create_ota_router(config, root))
    return True
