#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the actual packaging script embedded in firmware.patch."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
P7 = 'znp_LP_CC1352P7_4_tirtos7_ticlang'


def record(address, kind, data):
    raw = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ':' + (raw + bytes([-sum(raw) & 255])).hex().upper() + '\n'


def main():
    with tempfile.TemporaryDirectory(prefix='zstack-package-') as directory:
        root = Path(directory)
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        subprocess.run(['git', '-C', str(root), 'apply', '--whitespace=nowarn', '--include=package.py',
                        '--include=source/preinclude.h', str(ROOT / 'firmware.patch')], check=True)
        build = root / 'workspace' / P7 / 'default'
        build.mkdir(parents=True)
        image = build / (P7 + '.hex')
        symbols = build / (P7 + '.map')
        config = root / 'source/preinclude.h'
        original = config.read_text()
        assert '#define ZNP_BACKHAUL_ENABLE 0' in original
        for enabled, setting, revision, archive in (
            (False, 1, 20250321, 'CC1352P7_coordinator_20250321'),
            (True, 0, 20260917, 'CC1352P7_znp_backhaul_20260917'),
            (True, 0, 20270101, 'CC1352P7_znp_backhaul_20270101'),
        ):
            # Deliberately change the option after "building": names follow the image.
            config.write_text(original.replace('ZNP_BACKHAUL_ENABLE 0',
                                               f'ZNP_BACKHAUL_ENABLE {setting}'))
            payload = b'\x02\x01\x02\x07\x01' + revision.to_bytes(4, 'little')
            image.write_text(record(0, 4, b'\x00\x01') + record(0x100, 0, payload) + record(0, 1, b''))
            symbols.write_text('00010100  MTVersionString\n' +
                               ('00001234  ZnpBh_command\n' if enabled else ''))
            subprocess.run([sys.executable, str(root / 'package.py')], cwd=root, check=True)
            with zipfile.ZipFile(root / 'dist' / (archive + '.zip')) as output:
                assert output.namelist() == [archive + '.hex']
                assert output.read(archive + '.hex') == image.read_bytes()
        # Missing linker/version evidence must not mislabel an optional image.
        symbols.write_text('00001234  ZnpBh_command\n')
        failed = subprocess.run([sys.executable, str(root / 'package.py')], cwd=root,
                                capture_output=True, text=True)
        assert failed.returncode and 'Cannot locate backhaul firmware version' in failed.stderr
    print('PASS packaging: standard names, compiled backhaul variant/revision, stale option and missing evidence')


if __name__ == '__main__':
    main()
