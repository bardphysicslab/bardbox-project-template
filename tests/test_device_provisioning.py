import datetime
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

import pytest
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('provision', ROOT/'scripts/provision_device.py')
provision = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provision)


def test_python_record_decodes_on_device_and_rejects_corruption(tmp_path):
    key = ec.generate_private_key(ec.SECP256R1())
    (tmp_path/'public.pem').write_bytes(key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Test CA')])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(1).not_valid_before(now)
            .not_valid_after(now+datetime.timedelta(days=1))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
    (tmp_path/'ca.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    config = json.loads((ROOT/'docs/device-provisioning.example.json').read_text())
    config.update(ca_file='ca.pem', signing_public_key_file='public.pem', wifi_password='')
    record = provision.encode_config(config, b'installed firmware', tmp_path)
    source = tmp_path/'decode.cpp'
    source.write_text('''#include "BardBoxDeviceConfig.h"
#include <iostream>
#include <iterator>
int main(){std::string bytes((std::istreambuf_iterator<char>(std::cin)),{});
bardbox::DeviceConfig c;
if(!bardbox::DeviceConfigRecord::decode(bytes,c))return 1;
return c.uid=="bb-ces-air-001" && c.wifiPassword.empty() && c.initialBytes==18 ? 0 : 2;}
''')
    binary = tmp_path/'decode'
    subprocess.run([shutil.which('c++'), '-std=c++11', '-I', str(ROOT/'software/firmware/include'),
                    str(source), '-o', str(binary)], check=True)
    assert subprocess.run([str(binary)], input=record).returncode == 0
    assert subprocess.run([str(binary)], input=record[:-1]+bytes([record[-1]^1])).returncode == 1
    for changes in [dict(uid='bad\nuid'), dict(telemetry_url='http://lab.example/readings'),
                    dict(wifi_password='short'), dict(telemetry_token='bad\r\nheader')]:
        with pytest.raises(ValueError):
            provision.encode_config(dict(config, **changes), b'installed firmware', tmp_path)


class FakeSerial:
    def __init__(self, reject=False):
        self.commands = []
        self.reply = b''
        self.reject = reject
    def reset_input_buffer(self):
        self.reply = b''
    def write(self, data):
        assert len(data) <= 64
        command = data.decode().strip()
        self.commands.append(command)
        if self.reject:
            self.reply = b'ERR,private-secret-echo\n'
        elif command.startswith('PROVISION_BEGIN '):
            self.reply = b'PROVISION_BEGIN_OK\n'
        elif command.startswith('PROVISION_DATA '):
            _, offset, hex_data = command.split()
            self.reply = f'PROVISION_DATA_OK,{int(offset)+len(bytes.fromhex(hex_data))}\n'.encode()
        else:
            assert command == 'PROVISION_COMMIT'
            self.reply = b'PROVISION_COMMIT_OK,activation=next_boot\n'
    def flush(self):
        pass
    def readline(self, limit):
        result, self.reply = self.reply, b''
        return result[:limit]


def test_usb_transfer_is_bounded_and_does_not_echo_errors():
    fake = FakeSerial()
    record = bytes(range(256))*30
    provision.commission(fake, record)
    replay = b''.join(bytes.fromhex(cmd.split()[2]) for cmd in fake.commands if cmd.startswith('PROVISION_DATA'))
    assert replay == record
    assert fake.commands[-1] == 'PROVISION_COMMIT'
    with pytest.raises(RuntimeError) as error:
        provision.commission(FakeSerial(reject=True), record)
    assert 'private-secret' not in str(error.value)
