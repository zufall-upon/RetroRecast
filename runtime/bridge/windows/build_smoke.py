"""Cross-build the AI-free x86 smoke with an explicitly supplied llvm-mingw."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    args = p.parse_args()
    compiler = args.compiler.absolute()  # preserve llvm-mingw's target-selecting argv[0]
    out = args.output_dir.resolve()
    package = Path(__file__).resolve().parents[2]
    if out == package or package in out.parents:
        raise ValueError('build directory must be outside package')
    out.mkdir(mode=0o700)
    source = out / 'chain-smoke.cpp'
    shutil.copyfile(Path(__file__).resolve().with_name('chain-smoke.cpp'), source)
    exe = out / 'chain-smoke.exe'
    command = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
               '-Wl,--no-insert-timestamp', str(source), '-o', str(exe), '-luser32', '-lgdi32']
    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    receipt = {'command': command, 'compiler_sha256': sha(compiler), 'source_sha256': sha(source),
               'exit_code': result.returncode, 'elapsed_seconds': time.monotonic() - started,
               'stdout': result.stdout, 'stderr': result.stderr, 'exe_sha256': sha(exe) if exe.exists() else None,
               'native_windows_executed': False}
    with (out / 'build-receipt.json').open('x', encoding='utf-8') as f:
        json.dump(receipt, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print(json.dumps(receipt, ensure_ascii=False, indent=2))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
