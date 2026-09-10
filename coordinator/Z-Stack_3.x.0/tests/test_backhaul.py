#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Radio-only tests. Optionally use real OSAL/NVOCMP and patched MAC callback sources."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = None
FLAGS = ['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']


def compile_run(source, output, extra=()):
    subprocess.run(FLAGS + ['-I' + str(SOURCE), '-Itests/backhaul/bootstrap', *extra, source, '-o', str(output)],
                   cwd=ROOT, check=True)
    subprocess.run([str(output)], check=True)


def bootstrap_sdk(sdk, out):
    source = (sdk / 'ti/zstack/osal/osal_nv.c').read_text()
    functions = []
    for name in ('item_init_ex', 'item_init', 'item_len_ex', 'item_len', 'write_ex',
                 'write', 'read_ex', 'read', 'delete_ex', 'delete'):
        match = re.search(r'uint(?:8|16)_t osal_nv_' + name + r'\s*\([^)]*\)\s*\{', source)
        assert match, name
        pos, depth = match.end(), 1
        while depth:
            depth += (source[pos] == '{') - (source[pos] == '}')
            pos += 1
        functions.append(source[match.start():pos])
    # Generated SDK excerpts stay in a temporary directory, never in the repository.
    (out / 'sdk-osal-nv.inc').write_text('\n'.join(functions))
    platform = (ROOT / 'tests/backhaul/bootstrap/znp_bootstrap_test_platform.h').read_text()
    names = set(re.findall(r'ZCD_NV_[A-Z_]+', platform)) | {'ZCD_NV_EX_LEGACY'}
    definitions = (sdk / 'ti/zstack/stack/sys/zcomdef.h').read_text()
    constants = []
    for name in sorted(names):
        match = re.search(r'^#define\s+' + name + r'\s+(\S+)', definitions, re.M)
        assert match, name
        constants.append(f'#define {name} {match[1]}')
    (out / 'sdk-nv-ids.inc').write_text('\n'.join(constants) + '\n')
    allocated = {int(m, 16) for m in re.findall(
        r'^#define\s+ZCD_NV_\w+\s+(0x[0-9a-fA-F]+)', definitions, re.M)}
    for name in ('BOOT_NV', 'REG_NV'):
        val = int(re.search(r'^#define ' + name + r' (0x\w+)',
                           (SOURCE / 'znp_bootstrap.c').read_text(), re.M)[1], 16)
        assert val <= 0x3ff and val not in allocated, (name, 'NV ID collision/bounds')
    binary = out / 'bootstrap-sdk'
    # TI NVOCMP has its own warning policy; keep sanitizers for the integrated test.
    flags = [f for f in FLAGS if f not in ('-Wall', '-Wextra', '-Werror')]
    subprocess.run(flags + ['-D_GNU_SOURCE', '-DNV_LINUX', '-DNVOCMP_POSIX_MUTEX', '-pthread',
                           '-I' + str(SOURCE), '-Itests/backhaul/bootstrap', '-I' + str(out), '-I' + str(sdk),
                           'tests/backhaul/test_bootstrap_sdk.c', str(sdk / 'ti/common/nv/nvocmp.c'),
                           str(sdk / 'ti/common/nv/crc.c'), '-o', str(binary)], cwd=ROOT, check=True)
    for mode in ('master', 'master-reboot', 'satellite-stage', 'satellite-restore',
                 'satellite-reboot', 'satellite-migrate-mark', 'satellite-migrate-clear',
                 'satellite-migrate-stage', 'satellite-migrate-restore', 'satellite-migrate-reboot'):
        flash = out / ('master.flash' if mode.startswith('master') else 'satellite.flash')
        subprocess.run([str(binary), mode, str(flash)], check=True)
    print('PASS SDK bootstrap: real OSAL/NVOCMP, persistent restarts and compaction')


def broadcast_sdk(sdk, out):
    source = (sdk / 'ti/zstack/zmac/f8w/zmac_cb.c').read_text()
    if 'ZnpBh_queue' not in source:
        raise SystemExit('--sdk must contain the backhaul overlay for callback tests')
    start = source.index('        if ( (pData->dataInd.mac.dstAddr.addr.shortAddr == 0xFFFF)')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    (out / 'sdk-broadcast-branch.inc').write_text(source[start:end] + '\n')
    compile_run('tests/backhaul/test_radio.c', out / 'radio-sdk',
                ['-DZNP_SDK_BROADCAST_FIXTURE', '-I' + str(out)])
    print('PASS SDK broadcast callback: admission, duplicate suppression and ownership')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, help='Patched SimpleLink SDK 8.30.01.01 root')
    args = parser.parse_args()
    global SOURCE
    os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=1')
    subprocess.run([sys.executable, str(ROOT / 'tests/test_package.py')], check=True)
    with tempfile.TemporaryDirectory(prefix='znp-backhaul-tests-') as tmp:
        out = Path(tmp)
        subprocess.run(['git', 'init', '-q', str(out)], check=True)
        subprocess.run(['git', '-C', str(out), 'apply',
                        '--include=source/ti/zstack/backhaul/*', str(ROOT / 'firmware.patch')],
                       check=True)
        SOURCE = out / 'source/ti/zstack/backhaul'
        if args.sdk:
            installed = args.sdk.resolve() / 'source/ti/zstack/backhaul'
            for source in SOURCE.iterdir():
                if (installed / source.name).read_bytes() != source.read_bytes():
                    raise SystemExit(f'SDK source differs from firmware.patch: {source.name}')
            SOURCE = installed
        for source in SOURCE.glob('*.c'):
            obj = out / (source.stem + '-disabled.o')
            subprocess.run(FLAGS + ['-c', str(source), '-o', str(obj)], check=True)
            symbols = subprocess.check_output(['nm', '-g', '--defined-only', str(obj)], text=True)
            assert not symbols.strip(), (source.name, 'feature code present without opt-in')
        print('PASS default-off: all extension translation units have no exported symbols')
        compile_run('tests/backhaul/test_radio.c', out / 'radio')
        compile_run('tests/backhaul/test_bootstrap.c', out / 'bootstrap')
        if args.sdk:
            sdk = args.sdk.resolve() / 'source'
            bootstrap_sdk(sdk, out)
            broadcast_sdk(sdk, out)


if __name__ == '__main__':
    main()
