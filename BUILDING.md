# Build and CPU checks

[日本語](BUILDING.ja.md) · [Quick Start](QUICKSTART.md)

## Checks

On Linux, use an existing Python 3 and NumPy environment. No GPU, game or model is needed; the tests use synthetic pixels and do not automatically install dependencies. Running the same tests on Windows requires separately building the x64 atomic helper, which is not included in a source checkout.

```text
python3 -B scripts/check_source.py --regression
```

The suite checks RGB/BGRA, vertical padding, slot sequence/error, synthetic fixture serve/prime, conflict refusal and receipt-bound recovery boundaries after stopping. CPU mocks do **not** establish Windows, GPU, real-game, image-quality or safety success.

## Cross-build

Explicitly provide an existing llvm-mingw toolchain. Each `output-dir` must not already exist and must be outside the source tree. The scripts do not install or download tools.

```text
python3 -B scripts/build.py --compiler-i686 <i686-w64-mingw32-clang++> --compiler-x64 <x86_64-w64-mingw32-clang> --output-dir <new-build>
python3 -B runtime/bridge/windows/build_smoke.py --compiler <i686-w64-mingw32-clang++> --output-dir <new-smoke-build>
```

The historical build with LLVM-Mingw clang 23.1.4, C++17 `-O2 -shared -static` and fixed link flags matched hook SHA-256 `4096f1f380cb346f2418105b5b4b1617e7b71a5be49e1e38520bfe1da6b1486f` and helper `43ff50ab95eb10c2b7fcbcde37a33c4d00adfa327307ca8befda8824376c2cba`. Link using the original output name `d3d9.dll`, then rename it; linking with a different name changes the PE. Smoke SHA-256 was `3752972d5390f229dcce22fc728eb76575ffb49c876664781f94fca822ff2a37`. A successful cross-build is not evidence of Windows execution. Check compiler, flags, source hashes, exit codes and output hashes in the build receipts.

Place the resulting `d3d9-overlay.dll` in `runtime/bridge/windows/overlay/`, `slot_atomic_x64.dll` in `runtime/worker/windows/`, and `chain-smoke.exe` in `runtime/bridge/windows/` to form a runtime tree. The release ZIP includes these and the historical chain. Manually adding binaries to a source checkout makes it a different configuration from a source-only audit. Weights, a Windows GPU environment and FFmpeg are not built. A different chain or new synthetic fixture is a different candidate.

## Create a runtime ZIP

A normal source checkout contains no binaries; the release runtime ZIP contains both source and binaries. `scripts/package.py` requires a clean source commit, the two new build directories above, and the historical chain with its fixed hash. The output directory must be new. It does not recursively collect internal history, game, model or environments.

```text
python3 -B scripts/package.py --hook-build <new-build> --smoke-build <new-smoke-build> --chain <matching-d3d9-chain.dll> --output-dir <new-release-dir>
```

For chain source revisions and component licenses see [NOTICE](NOTICE.md). This Alpha's packaging entrypoint accepts only the historical chain hash, not another build as if it were the demonstrated one. For research with a new build, distinguish candidate identity and unverified scope.

In the distribution ZIP, `python -B scripts/verify_package.py` checks all payloads using the standard library. CPU tests: `python -B -m unittest discover -s tests -p "test_*.py"`. In a source checkout, `check_source.py --regression` additionally audits selected source, index, internal paths and possible secrets. Preserve [LICENSE_SCOPE](LICENSE_SCOPE.md) and distribute corresponding source and build entrypoints.
