import csv
from pathlib import Path


def test_dual_slot_layout_preserves_storage_and_alignment():
    path = Path(__file__).resolve().parents[1]/'software/firmware/partitions_8mb_dual.csv'
    rows = {}
    previous_end = 0x9000  # Partition table occupies the preceding sector.
    for row in csv.reader(line for line in path.read_text().splitlines() if not line.startswith('#')):
        name, kind, subtype, offset, size, *_ = (field.strip() for field in row)
        offset, size = int(offset, 0), int(size, 0)
        assert offset >= previous_end and size > 0
        assert offset % 4096 == 0 and size % 4096 == 0
        if kind == 'app':
            assert offset % 65536 == 0
        rows[name] = (kind, subtype, offset, size)
        previous_end = offset + size
    assert previous_end == 8 * 1024 * 1024
    assert rows['app0'] == ('app', 'ota_0', 0x10000, 0x200000)
    assert rows['app1'] == ('app', 'ota_1', 0x210000, 0x200000)
    assert rows['nvs'] == ('data', 'nvs', 0x9000, 0x5000)
    assert rows['otadata'] == ('data', 'ota', 0xe000, 0x2000)
    assert rows['spiffs'] == ('data', 'spiffs', 0x410000, 0x3e0000)
    assert rows['coredump'] == ('data', 'coredump', 0x7f0000, 0x10000)
