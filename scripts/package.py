"""Package audited source and fixed binaries. No download, model or game execution."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TAG = 'v0.1.0-alpha'
BINARY_HASHES = {
    'runtime/bridge/windows/overlay/d3d9-overlay.dll': '4096f1f380cb346f2418105b5b4b1617e7b71a5be49e1e38520bfe1da6b1486f',
    'runtime/worker/windows/slot_atomic_x64.dll': '43ff50ab95eb10c2b7fcbcde37a33c4d00adfa327307ca8befda8824376c2cba',
    'runtime/bridge/windows/chain-smoke.exe': '3752972d5390f229dcce22fc728eb76575ffb49c876664781f94fca822ff2a37',
    'runtime/bridge/windows/d3d9_chain.dll': '70571734e0758c9791f46d5b0d26a0cc0dd110e07093b5e82f6069da3fd4a2bc',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8')


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], timeout=30)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hook-build', type=Path, required=True)
    p.add_argument('--smoke-build', type=Path, required=True)
    p.add_argument('--chain', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--staged-candidate', action='store_true',
                   help='local review only: bind the audited index snapshot, not a release commit')
    args = p.parse_args()
    require(not args.output_dir.is_symlink(), 'linked output directory refused')
    out = args.output_dir.resolve()
    require(out != ROOT and ROOT not in out.parents, 'output must be outside source tree')
    require(not out.exists() and not out.is_symlink(), 'output already exists')
    base_commit = git('rev-parse', 'HEAD').decode().strip()
    require(args.staged_candidate or git('status', '--porcelain') == b'',
            'commit a clean, audited source tree first')
    commit = None if args.staged_candidate else base_commit
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/check_source.py')], check=True,
                   cwd=ROOT, timeout=60)
    files = {}
    for row in filter(None, git('ls-files', '-z').split(b'\0')):
        name = row.decode('utf-8')
        data = git('show', ':' + name if args.staged_candidate else commit + ':' + name)
        require((ROOT / name).read_bytes() == data, 'source changed: ' + name)
        files[name] = data
    source_snapshot = sha(encoded({name: sha(data) for name, data in sorted(files.items())}))

    hook_receipt = json.loads((args.hook_build / 'build-receipt.json').read_text())
    smoke_receipt = json.loads((args.smoke_build / 'build-receipt.json').read_text())
    require(all(hook_receipt[k]['exit_code'] == 0 for k in ('hook', 'helper')),
            'hook/helper build did not succeed')
    require(smoke_receipt['exit_code'] == 0, 'smoke build did not succeed')
    for name, expected in hook_receipt['source_sha256'].items():
        source_name = 'runtime/bridge/' + ('windows/overlay/' if name == 'boundary.h' else 'source/') + name
        require(sha(files[source_name]) == expected, 'build source differs: ' + source_name)
    require(sha(files['runtime/worker/windows/slot_atomic.c']) == hook_receipt['helper_source_sha256'],
            'helper source differs')
    require(sha(files['runtime/bridge/windows/chain-smoke.cpp']) == smoke_receipt['source_sha256'],
            'smoke source differs')
    inputs = (args.hook_build / 'd3d9-overlay.dll', args.hook_build / 'slot_atomic_x64.dll',
              args.smoke_build / 'chain-smoke.exe', args.chain)
    for (name, expected), path in zip(BINARY_HASHES.items(), inputs):
        require(not path.is_symlink() and path.is_file(), 'binary must be a regular file')
        data = path.read_bytes()
        require(data[:2] == b'MZ' and sha(data) == expected, 'binary differs: ' + name)
        files[name] = data
    require(hook_receipt['hook']['output_sha256'] == BINARY_HASHES[next(iter(BINARY_HASHES))],
            'hook receipt mismatch')
    require(hook_receipt['helper']['output_sha256'] == BINARY_HASHES['runtime/worker/windows/slot_atomic_x64.dll'],
            'helper receipt mismatch')
    require(smoke_receipt['exe_sha256'] == BINARY_HASHES['runtime/bridge/windows/chain-smoke.exe'],
            'smoke receipt mismatch')
    # Preserve measured receipt fields without publishing local paths/logs.
    public_smoke = {k: smoke_receipt[k] for k in ('compiler_sha256', 'source_sha256', 'exit_code',
                    'elapsed_seconds', 'exe_sha256', 'native_windows_executed')}
    public_smoke['command'] = ['<compiler>', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                              '-static', '-Wl,--no-insert-timestamp', '<source>/chain-smoke.cpp',
                              '-o', '<build>/chain-smoke.exe', '-luser32', '-lgdi32']
    public_hook = {k: hook_receipt[k] for k in ('hook', 'helper', 'hook_flags', 'helper_flags',
                    'source_sha256', 'helper_source_sha256', 'note')}
    # Diagnostic paths are not needed to reproduce these successful outputs.
    for k in ('hook', 'helper'):
        public_hook[k] = {key: value for key, value in public_hook[k].items() if key != 'stderr'}
    files['build-receipts.json'] = encoded({'hook_helper': public_hook, 'smoke': public_smoke})
    manifest = {'schema': 1, 'release_tag': TAG, 'source_commit': commit,
                'source_base_commit': base_commit, 'source_snapshot_sha256': source_snapshot,
                'local_review_only': args.staged_candidate,
                'windows_runtime_retested': False, 'model_game_environment_included': False,
                'files': {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in sorted(files.items())}}
    files['package-manifest.json'] = encoded(manifest)
    # Reject accidental private paths, weights, or runtime environment assets.
    for name, data in files.items():
        require(Path(name).suffix not in ('.pth', '.pt', '.rom', '.utx', '.mp4', '.npy', '.bmp'),
                'excluded asset: ' + name)
        if name not in BINARY_HASHES and not name.endswith('.png'):
            require(not any(token in data for token in (b'/srv/' + b'archive/', b'/srv/' + b'work/',
                        b'/home/' + b'user/', b'O:' + b'\\__gamedev')), 'internal path in package: ' + name)

    out.mkdir(mode=0o700)
    archive = out / ('RetroRecast-' + TAG + ('-windows-review.zip' if args.staged_candidate else '-windows.zip'))
    prefix = 'RetroRecast-' + TAG + '/'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(prefix + name, date_time=(2026, 11, 6, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)
    # Verify the exact archive, not only the input tree. Extraction is limited to
    # the names authored above; unknown archive members are refused.
    with tempfile.TemporaryDirectory(prefix='retrorecast-package-') as tmp:
        extracted = Path(tmp) / 'runtime'
        extracted.mkdir()
        with zipfile.ZipFile(archive) as z:
            require(z.namelist() == [prefix + name for name in sorted(files)], 'archive entries differ')
            for name, data in files.items():
                require(z.read(prefix + name) == data, 'archive bytes differ: ' + name)
                dest = extracted / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
        subprocess.run([sys.executable, '-B', 'scripts/verify_package.py'], cwd=extracted,
                       check=True, timeout=60)
        subprocess.run([sys.executable, '-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py'],
                       cwd=extracted, check=True, timeout=60)
    (out / 'package-manifest.json').write_bytes(files['package-manifest.json'])
    (out / 'SHA256SUMS').write_text(sha(archive.read_bytes()) + '  ' + archive.name + '\n', encoding='ascii')
    print(json.dumps({'archive': archive.name, 'bytes': archive.stat().st_size,
                      'sha256': sha(archive.read_bytes()), 'source_commit': commit,
                      'verified_payload_files': len(manifest['files']),
                      'windows_runtime_retested': False}, ensure_ascii=False))


if __name__ == '__main__':
    main()
