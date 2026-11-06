"""Prepare or run an AI-free D3D9 chain smoke in a fresh private directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
PACKAGE = HERE.parent.parent
FLOOR = 2147483648 + 512 * 1024 * 1024


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def pe32(path):
    with path.open('rb') as f:
        header = f.read(64)
        if len(header) != 64 or header[:2] != b'MZ':
            raise ValueError('not a PE file: ' + str(path))
        offset = int.from_bytes(header[60:64], 'little')
        if offset < 64 or offset > 16 * 1024 * 1024:
            raise ValueError('invalid PE offset')
        f.seek(offset)
        header = f.read(26)
        if len(header) < 26 or header[:4] != b'PE\0\0' or header[4:6] != b'\x4c\x01' or header[24:26] != b'\x0b\x01':
            raise ValueError('expected x86 PE32: ' + str(path))


def copy_exclusive(src, dst):
    with src.open('rb') as source, dst.open('xb') as target:
        shutil.copyfileobj(source, target)


def save(path, data):
    with path.open('x', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write('\n')


def prepare(run_dir, hook, chain):
    run_dir = run_dir.resolve()
    if PACKAGE == run_dir or PACKAGE in run_dir.parents:
        raise ValueError('run directory must be outside package')
    for path in (HERE / 'chain-smoke.exe', hook, chain):
        pe32(path)
    if shutil.disk_usage(run_dir.parent).free < FLOOR:
        raise RuntimeError('insufficient free space: 2 GiB floor + 512 MiB margin')
    run_dir.mkdir(mode=0o700)  # exclusive identity; no existing game or DLL is touched
    for src, name in ((HERE / 'chain-smoke.exe', 'chain-smoke.exe'), (hook, 'd3d9.dll'),
                      (chain, 'd3d9_chain.dll')):
        copy_exclusive(src, run_dir / name)
    once = {'mode': 'AI-free', 'hook_sha256': sha(run_dir / 'd3d9.dll'),
            'chain_sha256': sha(run_dir / 'd3d9_chain.dll'),
            'smoke_sha256': sha(run_dir / 'chain-smoke.exe'),
            'native_windows_executed': False, 'windows_game_validated': False}
    save(run_dir / 'once.json', once)
    return once


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('prepare', 'run'))
    p.add_argument('--run-dir', type=Path, required=True, help='new private directory outside package')
    p.add_argument('--hook', type=Path, default=HERE / 'd3d9.dll')
    p.add_argument('--chain', type=Path, default=PACKAGE / 'bridge/linux/d3d9_chain.dll')
    p.add_argument('--timeout', type=int, default=30)
    args = p.parse_args(argv)
    if not 1 <= args.timeout <= 120:
        p.error('timeout must be 1..120 seconds')
    if args.action == 'run' and sys.platform != 'win32':
        raise RuntimeError('run requires native Windows; prepare works on Linux')
    run_dir = args.run_dir.absolute()
    once = prepare(run_dir, args.hook, args.chain)
    if args.action == 'prepare':
        print(json.dumps(once, ensure_ascii=False))
        return 0
    started = time.monotonic()
    terminal = {'native_windows_executed': True, 'windows_game_validated': False}
    try:
        result = subprocess.run([str(run_dir / 'chain-smoke.exe')], cwd=run_dir,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=args.timeout, text=True, errors='replace')
        terminal.update({'exit_code': result.returncode, 'stdout': result.stdout,
                         'stderr': result.stderr, 'timed_out': False})
    except subprocess.TimeoutExpired as exc:
        terminal.update({'exit_code': None, 'timed_out': True, 'error': str(exc)})
    finally:
        terminal['elapsed_seconds'] = time.monotonic() - started
        save(run_dir / 'terminal.json', terminal)
    print(json.dumps(terminal, ensure_ascii=False))
    return terminal['exit_code'] if terminal['exit_code'] is not None else 2


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
