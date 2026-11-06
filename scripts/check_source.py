"""Audit this candidate and optionally run the offline CPU regression suite."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {'.py', '.c', '.cpp', '.h', '.def', '.md', '.txt', '.json'}
SECRETS = [re.compile(p, re.M) for p in (
    r'gh[pousr]_[A-Za-z0-9]{30,}', r'github_pat_[A-Za-z0-9_]{30,}',
    r'^-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----', r'AKIA[0-9A-Z]{16}',
    r'https?://[^/\s:@]+:[^@\s]+@',
    r'(?i)(?:api[_-]?key|access[_-]?token|client[_-]?secret)\s*[=:]\s*[\x22\x27][A-Za-z0-9_\-]{24,}[\x22\x27]')]
INTERNAL = ('/' + 'srv/archive/', '/' + 'srv/work/', '/' + 'home/user/',
            'O:' + '\\' + '__gamedev', '.' + 'sortie-env/', '.' + 'sortie-dogs-v010/')
MEDIA = {'assets/church-input-output.png':
         '0ad8b33f3a626dde442955a2566dc9eba6d66e330cbdb1e4549f10e4fb57d65c'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def git(*args):
    proc = subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, timeout=30)
    require(proc.returncode == 0, 'Git audit failed: ' + proc.stderr.decode(errors='replace'))
    return proc.stdout


def audit():
    files = {}
    for p in sorted(ROOT.rglob('*')):
        rel = p.relative_to(ROOT)
        if '.git' in rel.parts:
            continue
        require(not p.is_symlink(), 'symlink refused: ' + str(rel))
        if not p.is_file():
            continue
        name = rel.as_posix()
        data = p.read_bytes()
        if name in MEDIA:
            require(hashlib.sha256(data).hexdigest() == MEDIA[name], 'Main Visual differs')
            files[name] = data
            continue
        require(p.suffix in SUFFIXES or name in ('.gitignore', 'LICENSE'), 'unexpected file type: ' + name)
        require(not any(x in rel.parts for x in ('known-input-output', '.env', 'trials')), 'excluded tree: ' + name)
        require(len(data) <= 512 * 1024 and b'\0' not in data, 'binary or oversized file: ' + name)
        text = data.decode('utf-8')
        require(not any(s.search(text) for s in SECRETS), 'possible secret (value hidden): ' + name)
        require(not any(s in text for s in INTERNAL), 'internal path: ' + name)
        if p.suffix == '.py':
            compile(text, name, 'exec')
        if p.suffix == '.json':
            json.loads(text)
        files[name] = data
    require(0 < len(files) <= 150 and sum(map(len, files.values())) <= 8 * 1024**2,
            'unexpected file count or size')
    indexed = False
    if (ROOT / '.git').exists():
        indexed = True
        names = set()
        for row in filter(None, git('ls-files', '--stage', '-z').split(b'\0')):
            info, path = row.split(b'\t', 1)
            mode, _, stage = info.decode().split()
            name = path.decode()
            require(mode in ('100644', '100755') and stage == '0', 'unsupported index entry: ' + name)
            require(name in files and git('show', ':' + name) == files[name], 'index/worktree mismatch: ' + name)
            names.add(name)
        require(names == set(files), 'untracked or missing candidate files')
    locked = json.loads(files['source-lock.json'])
    require(len(locked) == 22, 'unexpected frozen source list')
    for name, digest in locked.items():
        require(name in files and hashlib.sha256(files[name]).hexdigest() == digest,
                'locked source differs: ' + name)
    manifest = json.loads(files['release-manifest.json'])
    require(manifest['binary_in_repository'] is False and manifest['chain_and_weights_included'] is False,
            'asset manifest contradicts audit')
    require(manifest['candidate_windows_gpu_game_or_recovery_tested'] is False,
             'unverified Windows claim')
    require(manifest['main_visual']['sha256'] == MEDIA['assets/church-input-output.png'],
            'visual manifest differs')
    return {'files': len(files), 'bytes': sum(map(len, files.values())),
            'locked_source_files': len(locked), 'index_checked': indexed,
            'windows_game_or_safety_verified': False}


def regression():
    # When available, use the bounded temporary root; do not alter system environments.
    if Path('/tmp' + '/opencode').is_dir():
        os.environ.setdefault('TMPDIR', '/tmp' + '/opencode')
    suite = unittest.defaultTestLoader.discover(str(ROOT / 'tests'), pattern='test_*.py')
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    require(result.wasSuccessful() and result.testsRun >= 12, 'CPU contract regression failed or incomplete')
    return {'cpu_tests': result.testsRun, 'windows_game_or_safety_verified': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--regression', action='store_true')
    args = p.parse_args()
    report = {'audit': audit()}
    if args.regression:
        report['regression'] = regression()
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
