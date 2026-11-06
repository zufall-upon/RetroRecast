"""After shutdown, report/remove only DLLs bound by this run's recovery receipt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

NAMES = ('d3d9.dll', 'd3d9_chain.dll')


def reject_link_components(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('linked or reparse path refused: ' + str(part))


def inspect(path, expected):
    info = path.lstat()
    if (not stat.S_ISREG(info.st_mode) or path.is_symlink() or info.st_nlink != 1 or
            getattr(info, 'st_file_attributes', 0) & 0x400):  # Windows reparse point
        raise ValueError('not a singly linked regular file: ' + path.name)
    observed = {'size': info.st_size, 'device': info.st_dev, 'inode': info.st_ino,
                'mode': info.st_mode}
    if any(observed[k] != expected[k] for k in observed):
        raise ValueError('file identity or permissions changed: ' + path.name)
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags)
    try:
        opened = os.fstat(fd)
        if (opened.st_dev, opened.st_ino, opened.st_mode, opened.st_size, opened.st_nlink) != (
                info.st_dev, info.st_ino, info.st_mode, info.st_size, 1):
            raise ValueError('file changed during inspection: ' + path.name)
        h = hashlib.sha256()
        while True:
            part = os.read(fd, 1024 * 1024)
            if not part:
                break
            h.update(part)
        if h.hexdigest() != expected['sha256']:
            raise ValueError('file contents changed: ' + path.name)
    finally:
        os.close(fd)
    return observed


def recover(system, run_dir, mode, apply=False, confirm_stopped=False):
    if mode not in ('disable', 'restore'):
        raise ValueError('unknown recovery mode')
    if apply and not confirm_stopped:
        raise ValueError('confirm that game and controller have stopped first')
    reject_link_components(system)
    reject_link_components(run_dir)
    system, run_dir = system.resolve(), run_dir.resolve()
    receipt_path = run_dir / 'recovery.json'
    if receipt_path.is_symlink() or not receipt_path.is_file():
        raise ValueError('run recovery receipt is missing or is a link')
    receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
    if (receipt.get('schema') != 1 or Path(receipt.get('system', '')).resolve() != system or
            set(receipt.get('files', {})) != set(NAMES)):
        raise ValueError('recovery receipt does not identify this System directory')
    selected = NAMES[:1] if mode == 'disable' else NAMES
    plan = []
    for name in selected:
        path = system / name
        if not path.exists() and not path.is_symlink():
            continue
        plan.append((path, inspect(path, receipt['files'][name])))
    # Check every selected file before removing any; concurrent changes remain unsafe.
    removed = []
    if apply:
        for path, observed in plan:
            if inspect(path, receipt['files'][path.name]) != observed:
                raise ValueError('file changed before removal: ' + path.name)
            path.unlink()
            removed.append(path.name)
    return {'dry_run': not apply, 'mode': mode, 'planned': [p.name for p, _ in plan],
            'removed': removed, 'markers_ipc_and_logs_removed': False,
            'stopped_processes_verified': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['disable', 'restore'])
    p.add_argument('--system', type=Path, required=True)
    p.add_argument('--run-dir', type=Path, required=True)
    p.add_argument('--apply', action='store_true', help='without this option only report')
    p.add_argument('--confirm-stopped', action='store_true', help='operator confirms game/controller are stopped')
    a = p.parse_args()
    print(json.dumps(recover(a.system, a.run_dir, a.mode, a.apply, a.confirm_stopped), ensure_ascii=False))


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
