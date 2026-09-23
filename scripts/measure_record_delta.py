#!/usr/bin/env python3
"""Estimate compact bytes with generated CESH records; not flash capacity proof."""
import argparse
from datetime import datetime, timedelta, timezone
import json
import random
import subprocess
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--payload-executable', required=True,
                   help='CESH host fixture executable emitting its actual completed JSON record')
    p.add_argument('--codec-executable', required=True)
    p.add_argument('--records', type=int, default=10080)
    p.add_argument('--records-output', help='Also save generated JSON lines for queue integration tests')
    args = p.parse_args()
    first = subprocess.check_output([args.payload_executable], text=True).splitlines()[0]
    base = json.loads(first)
    randomizer = random.Random(20260916)
    lines = []
    for i in range(args.records):
        r = dict(base)
        start = datetime(2026, 9, 1, tzinfo=timezone.utc) + timedelta(seconds=i*120)
        end = start + timedelta(seconds=120)
        r.update(timestamp=end.isoformat().replace('+00:00','Z'),
                 window_start=start.isoformat().replace('+00:00','Z'),
                 window_end=end.isoformat().replace('+00:00','Z'),
                 record_id='bb-cesh-air-001-'+format(randomizer.getrandbits(64),'016x'),
                 read_count=i, buffer_count=i, sample_uptime_ms=i*120000,
                 window_start_uptime_ms=i*120000, window_end_uptime_ms=(i+1)*120000,
                 temp_c=round(randomizer.uniform(-20,45),2),
                 humidity_percent=round(randomizer.uniform(0,100),2),
                 pressure_hpa=round(randomizer.uniform(900,1100),2),
                 bme680_gas_resistance_ohms=round(randomizer.uniform(5000,500000),2),
                 rssi_dbm=-90)
        for channel in ('a','b'):
            r[f'pms_{channel}_valid']=True
            r[f'pms_{channel}_sample_count']=randomizer.randint(110,130)
            for key in r:
                if key.startswith(f'pms_{channel}_pm') or key.startswith(f'pms_{channel}_particles_'):
                    r[key]=round(randomizer.uniform(0,65535),2)
            r[f'pms_{channel}_frames_valid']=i*120
        r['thpg_reads_valid']=i*12
        r['thpg_sample_count']=12
        lines.append(json.dumps(r,separators=(',',':')))
    generated='\n'.join(lines)+'\n'
    if args.records_output:
        Path(args.records_output).write_text(generated)
    result = subprocess.check_output([args.codec_executable,'stream'], input=generated, text=True)
    count, stored = map(int,result.split())
    print(json.dumps({'records':count,'days_at_120s':count/720,
                      'plain_json_bytes':sum(len(line.encode())+2 for line in lines),
                      'compact_bytes_with_base_and_framing_estimate':stored,
                      'average_bytes_per_record':stored/count,
                      'filesystem_bytes':0x3e0000,
                      'remaining_before_filesystem_overhead':0x3e0000-stored,
                      'caveat':'Generated data only; every decoded record checked byte-for-byte. No real filesystem, fault recovery or retention guarantee.'},indent=2))


if __name__ == '__main__':
    main()
