"""既存llvm-mingwでx64 atomic helperを新規folderへbuildする。installはしない。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True, help='存在しないpackage外folder')
    args = p.parse_args()
    # llvm-mingw dispatches its target from argv[0]; preserve the symlink name.
    compiler = args.compiler.absolute()
    if not compiler.is_file():
        raise FileNotFoundError(compiler)
    out = args.output_dir.absolute()
    out.mkdir(mode=0o700)
    source = Path(__file__).resolve().with_name('slot_atomic.c')
    captured = out / source.name
    with captured.open('xb') as f:
        f.write(source.read_bytes())
    dll = out / 'slot_atomic_x64.dll'
    command = [str(compiler), '-std=c11', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-stack-protector', '-nostdlib',
               '-Wl,--entry,DllMain', '-Wl,--no-insert-timestamp', '-Wl,--no-undefined',
               str(captured), '-o', str(dll)]
    version = subprocess.run([str(compiler), '--version'], text=True, capture_output=True,
                             check=True, timeout=10).stdout
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    receipt = {'command': command, 'compiler_version': version, 'compiler_sha256': sha(compiler),
               'source_sha256': sha(captured), 'exit_code': result.returncode,
               'stdout': result.stdout, 'stderr': result.stderr,
               'dll_sha256': sha(dll) if dll.exists() else None,
               'windows_runtime_tested': False}
    with (out / 'build-receipt.json').open('x', encoding='utf-8') as f:
        json.dump(receipt, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print(json.dumps(receipt, ensure_ascii=False, indent=2))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
