"""Cross-build the Windows hook and slot helper; never install or download tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'runtime'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_one(command, compiler, output, build_root):
    version = subprocess.run([str(compiler), '--version'], capture_output=True,
                             text=True, check=True, timeout=10).stdout.splitlines()[0]
    started = time.monotonic()
    env = dict(os.environ, TMPDIR=str(build_root), TMP=str(build_root), TEMP=str(build_root))
    p = subprocess.run(command, capture_output=True, text=True, timeout=120, env=env)
    result = {'compiler': compiler.name, 'compiler_sha256': digest(compiler),
              'version': version, 'exit_code': p.returncode,
              'elapsed_seconds': round(time.monotonic() - started, 3),
              'output_sha256': digest(output) if p.returncode == 0 and output.is_file() else None,
              'stderr': p.stderr.replace(str(build_root), '<build>').replace(str(ROOT), '<source>'),
              'windows_runtime_tested': False}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler-i686', type=Path, required=True)
    p.add_argument('--compiler-x64', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True,
                   help='new empty path; build is never performed in the source tree')
    args = p.parse_args()
    # llvm-mingw selects its target from argv[0]; do not resolve symlinks.
    cxx, cc = args.compiler_i686.absolute(), args.compiler_x64.absolute()
    if not cxx.is_file() or not cc.is_file():
        raise FileNotFoundError('both existing compilers are required')
    out = args.output_dir.absolute()
    if out == ROOT or ROOT in out.parents:
        raise ValueError('output must be outside the source tree')
    out.mkdir(mode=0o700)  # refuses reuse, including a previously failed build
    source = out / 'source'
    shutil.copytree(RUNTIME / 'bridge/source', source, symlinks=False)
    shutil.copyfile(RUNTIME / 'bridge/windows/overlay/boundary.h', source / 'boundary.h')
    # PE export metadata uses the link output name; the historical build used
    # d3d9.dll even though the stored overlay is named d3d9-overlay.dll.
    hook = out / 'd3d9.dll'
    hook_args = ['-std=c++17', '-O2', '-shared', '-static', '-Wall', '-Wextra',
                 str(source / 'shadowed.cpp'), str(source / 'd3d9.def'),
                 '-Wl,--kill-at,--disable-stdcall-fixup,--no-insert-timestamp',
                 '-o', str(hook), '-lkernel32', '-luser32']
    helper_source = RUNTIME / 'worker/windows/slot_atomic.c'
    helper = out / 'slot_atomic_x64.dll'
    helper_args = ['-std=c11', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-ffreestanding', '-fno-stack-protector', '-nostdlib',
                   '-Wl,--entry,DllMain', '-Wl,--no-insert-timestamp', '-Wl,--no-undefined',
                   str(helper_source), '-o', str(helper)]
    results = {'hook': compile_one([str(cxx), *hook_args], cxx, hook, out),
               'helper': compile_one([str(cc), *helper_args], cc, helper, out),
               'hook_flags': ['-std=c++17', '-O2', '-shared', '-static', '-Wall', '-Wextra',
                              '-Wl,--kill-at,--disable-stdcall-fixup,--no-insert-timestamp', '-lkernel32', '-luser32'],
               'helper_flags': ['-std=c11', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
                                '-ffreestanding', '-fno-stack-protector', '-nostdlib',
                                '-Wl,--entry,DllMain', '-Wl,--no-insert-timestamp', '-Wl,--no-undefined'],
               'source_sha256': {p.relative_to(source).as_posix(): digest(p)
                                 for p in sorted(source.iterdir()) if p.is_file()},
               'helper_source_sha256': digest(helper_source),
               'note': 'Cross-build only; no Windows game, GPU, smoke or safety validation.'}
    if results['hook']['exit_code'] == 0 and hook.is_file():
        hook.rename(out / 'd3d9-overlay.dll')
    with (out / 'build-receipt.json').open('x', encoding='utf-8') as f:
        json.dump(results, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print(json.dumps(results, ensure_ascii=False, indent=2))
    return 0 if all(results[k]['exit_code'] == 0 and results[k]['output_sha256']
                    for k in ('hook', 'helper')) else 2


if __name__ == '__main__':
    raise SystemExit(main())
