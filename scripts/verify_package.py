"""Verify a distributed runtime tree, without importing torch or starting processes."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(root):
    root = Path(root)
    manifest = json.loads((root / 'package-manifest.json').read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or not isinstance(manifest.get('files'), dict):
        raise ValueError('unsupported package manifest')
    expected = set(manifest['files']) | {'package-manifest.json'}
    actual = set()
    for path in root.rglob('*'):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise ValueError('linked payload refused: ' + relative)
        if path.is_file():
            actual.add(relative)
    if actual != expected:
        raise ValueError('missing or additional payload files')
    for name, record in manifest['files'].items():
        p = PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name:
            raise ValueError('unsafe manifest path')
        data = (root / name).read_bytes()
        if len(data) != record['bytes'] or digest(data) != record['sha256']:
            raise ValueError('payload differs: ' + name)
    return {'payload_files': len(manifest['files']), 'source_commit': manifest['source_commit'],
            'integrity_verified': True, 'windows_or_safety_verified': False}


if __name__ == '__main__':
    try:
        print(json.dumps(verify(ROOT), ensure_ascii=False))
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
