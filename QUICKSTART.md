# Install, stop, disable and restore (unverified research procedure)

[日本語](QUICKSTART.ja.md) · [Build and CPU tests](BUILDING.md)

> **Serious warning:** Windows itself unexpectedly rebooted during earlier tests; the cause is unknown. Data loss or file corruption is possible. Dark-scene structure collapse and performance degradation remain; GPUs other than those tested are unverified. Do not run on a daily-use PC or a system with important data. Warnings, supervision and recovery do not guarantee safety.

This is a **high-risk Experimental Alpha for developers**, not a stable release or finished mod. The repackaged configuration has not been retested on Windows/GPU/KF1, including ACLs or recovery after abnormal termination. The steps below are not a recommendation for ordinary use.

Download `RetroRecast-v0.1.0-alpha-windows.zip` from the [release](https://github.com/zufall-upon/RetroRecast/releases/tag/v0.1.0-alpha). GitHub's automatic Source code ZIP is source-only and does not contain the executable DLLs. Compare `Get-FileHash <zip> -Algorithm SHA256` with the release `SHA256SUMS`, then in the extracted ZIP root run `python -B scripts/verify_package.py`. It checks payload hashes, sizes and unexpected files, not safety or successful execution.

## Requirements and what is not included

- A legally obtained copy of KF1, a separate complete game copy, adequate backups and a dedicated experimental Windows environment. Do not modify the original game, OS DLLs or a daily-use environment. The intended experiment is offline and single-player.
- Windows x64 Python, NumPy, compatible GPU PyTorch and GPU/driver. Historical testing used Python 3.12.16, NumPy 2.5.3, torch 2.14.0+rocm10.1.0 and RX 6950 XT; this is not a compatibility promise for other environments. If the GPU is unavailable it fails; there is no automatic CPU fallback.
- Unmodified CARLA2Flux FP32 weights are not included. Consult the [REGEN author's model instructions](https://github.com/stefanos50/REGEN/tree/de240056522d066235b48b541e7d49f28c80f1ed) and [author's file](https://drive.google.com/file/d/1deJ7g4Lo52ZdeXIiEz10lZ2Gp2RC4mgR/view), and check the terms yourself. The expected file is **729795098 bytes**, SHA-256 `0fbb08561b71ad759c4503a77233497f06c7b168ba8eaf023a35b214d71af254`; the runtime checks the hash. Links and hashes do not grant rights to weights, teacher or data.
- The runtime ZIP contains the hook, x64 atomic helper, smoke test and the 32-bit DXVK chain whose bytes match the historical one. It does not automatically substitute another DXVK or Windows system DLL. See [NOTICE](NOTICE.md) for component terms and [BUILDING](BUILDING.md); do not copy or replace system DLLs.
- Supply your own [FFmpeg](https://ffmpeg.org/download.html#build-windows) for supervised recording and check the selected build's GPL/LGPL and other terms. The project does not redistribute FFmpeg or automatically download/install dependencies.

### Separate Python environment and GPU selection

Do not replace a working environment or system Python. Create a new venv using [official 64-bit Python 3.12 for Windows](https://www.python.org/downloads/windows/); check the [AMD TheRock instructions](https://github.com/ROCm/TheRock/blob/main/RELEASES.md) and [GPU list](https://github.com/ROCm/TheRock/blob/main/SUPPORTED_GPUS.md). These are **historically recorded version examples, not a setup rerun on Windows for this candidate**. The gfx1030 route uses a development TheRock build and is not a general support guarantee:

```powershell
py -3.12 -m venv C:\RetroRecast-env
C:\RetroRecast-env\Scripts\python.exe -m pip install --index-url https://stable.repo.amd.com/rocm/whl-next/ "torch[device-gfx1030]==2.14.0+rocm10.1.0"
C:\RetroRecast-env\Scripts\python.exe -m pip install "numpy==2.5.3"
```

Below, substitute the venv's `C:\RetroRecast-env\Scripts\python.exe` for `python`. The historical Windows experiment explicitly selected RX 6950 XT/gfx1030; indices differ on systems with integrated or other GPUs. Verify the device enumeration and driver using the official runtime, replace the placeholder with the actual discrete-GPU index, and run comparison and controller in **the same PowerShell**. Do not hard-code `1` for every PC. If selection cannot be confirmed, stop; do not switch to CPU, DirectML, ZLUDA or spoofing.

```powershell
$env:HIP_VISIBLE_DEVICES = "<verified dGPU index>"
```

This setting applies to the current shell and child processes. Close the dedicated shell afterward; do not change system-wide settings or update drivers as part of this procedure. PyTorch uses the `cuda` API name even for ROCm.

## Check for conflicts first

Specify the `System` directory in the separate copy and a not-yet-existing run directory outside the game. Replace `<...>` with your paths. The dry run does not copy, launch or check the GPU:

```text
python -B scripts/dry_run.py --system <KF1-copy/System> --run-dir <new-run-directory>
```

Existing hook, markers or IPC cause refusal, not overwrite; symlink or reparse-point paths are rejected. A dry run does not check rights, DLLs, model, GPU or safety. Inspect the copy's permissions, links and unexpected files yourself.

## Experimental execution entrypoint

1. Use the included hook/helper/smoke/chain, officially obtained fixed-hash weights and your FFmpeg. For source builds see [BUILDING](BUILDING.md). Do not proceed if the conditions for the model or dependencies are unmet. Confirm the GPU selection; other GPUs are not promised to work.
2. The following GPU comparison is a synthetic-input self-consistency check, **not** the historical game-image quality comparison. Use new output directories:

```text
python -B runtime/worker/stage.py compare --model <weights> --output-dir <new-comparison> --reference-mode synthetic --comparison-mode measure
python -B runtime/bridge/windows/smoke.py run --run-dir <new-smoke> --hook runtime/bridge/windows/overlay/d3d9-overlay.dll --chain runtime/bridge/windows/d3d9_chain.dll
```

3. Only if native smoke succeeded with the same hook/chain and the operator inspected the synthetic comparison, the following entrypoint exclusively creates files in the owned game copy and launches. This candidate has not received a new Windows execution test:

```text
python -B runtime/controller.py run --private-system <KF1-copy/System> --run-dir <new-run> --model <weights> --chain runtime/bridge/windows/d3d9_chain.dll --smoke-dir <new-smoke> --comparison-dir <new-comparison> --ffmpeg <ffmpeg.exe> --confirm-private-copy --confirm-visual-review --reference-mode synthetic
```

After the operator enters and `waiting-for-user.txt` appears, use a separate terminal: `python -B runtime/controller.py signal --run-dir <new-run> start`. To stop early, replace `start` with `stop`. There is no automatic input or focus. Supervision is not guaranteed to run during an OS reboot or blocked GPU call.

As in the historical session, configure the game copy for D3D9, windowed, 960×540 and manually start WestLondon. Do not bypass legitimate Steam authentication or terms; do not use online/anti-cheat servers. The slot is fixed at 960×540, without automatic resolution changes or higher-resolution support. The experimental window is finite; `--duration` is capped at 200 seconds. This is not a long-duration stability procedure.

## Disable and restore

Inspect the dedicated copy even after normal shutdown. The stop signal is described above. After a crash, personally confirm that the game and controller have actually exited and use that run's `recovery.json`; the commands do not find or stop processes:

```text
python -B scripts/recover.py disable --system <KF1-copy/System> --run-dir <new-run>
python -B scripts/recover.py restore --system <KF1-copy/System> --run-dir <new-run>
```

Both are dry runs by default. Add `--apply --confirm-stopped` only for an intentional operation. Disable removes only the recorded matching hook; restore removes only the recorded matching hook/chain. Changed hash, inode, permissions or link count cause refusal; original files are not written back. Concurrent changes, Windows ACLs and complete recovery are not guaranteed; partial failure is non-atomic, so inspect the result again. There is no automatic recovery if termination precedes the receipt. IPC, markers, journal and recordings are not automatically removed: quarantine the dedicated copy and inspect manually. This is not a way to restore an overwritten original game.
