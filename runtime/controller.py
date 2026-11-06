"""Private Windows manual-play controller; native operation is not yet validated."""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import time
import numpy as np

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'worker'))
import core
import slot
import capture_window

FLOOR = 2147483648 + 512 * 1024 * 1024
LOG_LIMIT = 128 * 1024 * 1024
FS_GROWTH_LIMIT = 1536 * 1024 * 1024
COLLISIONS = ('d3d9.dll', 'd3d9_chain.dll', 'live-ipc', 'draw-events.jsonl',
              'playable-ipc-path.txt', 'playable-mode.txt', 'manual-boundary.txt',
              'boundary-start.txt', 'boundary-stop.txt', 'boundary-done.txt',
              'controller-finished.txt', 'recording-mode.txt', 'combat-mode.txt',
              'scenario-mode.txt', 'test-harness.txt', 'long-harness.txt', 'external-controller.txt',
              'arm.txt', 'capture-request.txt', 'hybrid-scene.txt', 'hybrid-materials.txt',
              'bsp-scene.bin', 'visible-019.bin', 'visible-020.bin', 'limited-caster.txt')


def write_new(path, data):
    with path.open('xb') as f:
        f.write(data)


def save_new(path, value):
    write_new(path, (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def checked_copy(src, dest):
    with src.open('rb') as source, dest.open('xb') as target:
        shutil.copyfileobj(source, target)
    return core.sha(dest)


def save_recovery_receipt(system, out, installed):
    """Bind only the two exclusive-created DLLs; do not claim any other files."""
    entries = {}
    for name in ('d3d9.dll', 'd3d9_chain.dll'):
        path = system / name
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or path.is_symlink() or info.st_nlink != 1:
            raise ValueError('new DLL is not a singly linked regular file: ' + name)
        digest = core.sha(path)
        if installed.get(path) != digest:
            raise ValueError('new DLL changed before recovery receipt: ' + name)
        entries[name] = {'sha256': digest, 'size': info.st_size, 'device': info.st_dev,
                         'inode': info.st_ino, 'mode': info.st_mode}
    save_new(out / 'recovery.json', {'schema': 1, 'system': str(system), 'files': entries})


def check_comparison(directory, reference_mode='retained'):
    result = json.loads((directory / 'comparison.json').read_text(encoding='utf-8'))
    expected_reference = core.REFERENCE if reference_mode == 'retained' else core.synthetic_identity()
    if (result.get('checkpoint_sha256') != core.CHECKPOINT or
            result.get('reference_sha256') != expected_reference or
            result.get('comparison_mode') != 'measure' or not result.get('measurement_completed') or
            result.get('visual_quality_assessed') is not False):
        raise ValueError('a completed measurement with the retained checkpoint is required')
    bmp = (directory / 'output.bmp').read_bytes()
    core.bmp_to_rgb(bmp)
    raw = np.load(directory / 'raw.npy', allow_pickle=False)
    pixels = core.quantize(raw)
    if core.HEADER + pixels != bmp:
        raise ValueError('comparison raw and BMP disagree')
    if reference_mode == 'retained':
        calculated = core.compare(raw, pixels, ROOT / 'tests/known-input-output')
        if any(result.get(key) != value for key, value in calculated.items()):
            raise ValueError('comparison measurements disagree with saved output')
    elif (reference_mode != 'synthetic' or result.get('reference_kind') != 'synthetic-self-consistency'
          or result.get('matches', 'missing') is not None):
        raise ValueError('invalid synthetic measurement')
    return result


def check_smoke(directory, hook, chain):
    once = json.loads((directory / 'once.json').read_text(encoding='utf-8'))
    end = json.loads((directory / 'terminal.json').read_text(encoding='utf-8'))
    if (once.get('hook_sha256') != core.sha(hook) or once.get('chain_sha256') != core.sha(chain) or
            end.get('native_windows_executed') is not True or end.get('exit_code') != 0 or
            end.get('timed_out') is not False):
        raise ValueError('native smoke must succeed with these exact hook and chain bytes')


def read_boundary_result(system):
    journal = system / 'draw-events.jsonl'
    if journal.stat().st_size > LOG_LIMIT:
        raise ValueError('boundary journal exceeds log limit')
    starts, finishes = [], []
    composites, clears, presents = {}, {}, {}
    with journal.open(encoding='utf-8') as f:
        for index, line in enumerate(f):
            row = json.loads(line)
            event = row.get('event')
            if event == 'boundary_window_started':
                starts.append(row)
            elif event == 'boundary_window_finished':
                finishes.append(row)
            elif event == 'boundary_composite':
                composites.setdefault(row.get('pair'), []).append((index, row))
            elif event == 'boundary_original_clear':
                clears.setdefault(row.get('pair'), []).append((index, row))
            elif event == 'boundary_present':
                presents.setdefault(row.get('kind'), []).append((index, row))
    if len(starts) != 1 or len(finishes) != 1:
        raise ValueError('expected one manual boundary start and finish in the saved journal')
    # Same valid-pair contract as the established Linux public result assessor:
    # complete same-frame composite -> original Clear -> successful Present.
    valid = []
    for pair, rows in composites.items():
        zs, ps = clears.get(pair, []), presents.get('C-' + str(pair), [])
        if len(rows) != 1 or len(zs) != 1 or len(ps) != 1:
            continue
        ci, c = rows[0]
        zi, z = zs[0]
        pi, p = ps[0]
        if (ci < zi < pi and c.get('frame') == z.get('frame') == p.get('frame') and
                c.get('B_read_hr') == 0 and c.get('draw_hr') == 0 and z.get('original_hr') == 0 and
                p.get('original_present_hr') == 0 and
                all(c.get(key) is True for key in
                    ('request_sent', 'output_valid', 'uploaded', 'state_captured', 'restored', 'applied'))):
            valid.append(pair)
    failed_presents = [p for kind, rows in presents.items() if str(kind).startswith('C-')
                       for _, p in rows if p.get('original_present_hr') != 0]
    return {'start': starts[0], 'finish': finishes[0], 'valid_pairs': valid,
            'failed_presents': failed_presents}


def check_boundary_result(result):
    start, finish = result['start'], result['finish']
    if (start.get('mode') != 'on' or start.get('recipe') != 'manual-v1' or
            finish.get('mode') != 'on' or finish.get('failed') is not False or
            finish.get('restore_failed') is not False or type(finish.get('pairs')) is not int or
            finish['pairs'] <= 0 or result['valid_pairs'] != list(range(1, finish['pairs'] + 1))):
        raise ValueError('manual AI boundary failed, restore failed, or frame pairs incomplete/unsuccessful')


def reject_link_components(path):
    """Do not let an isolated-copy or run path silently resolve into another tree."""
    path = Path(path).absolute()
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('linked or reparse path refused: ' + str(part))


def validate_game(system, run_dir):
    reject_link_components(system)
    reject_link_components(run_dir)
    if run_dir.exists() or run_dir.is_symlink():
        raise FileExistsError('run directory already exists')
    system, run_dir = system.resolve(), run_dir.resolve()
    if system.name.lower() != 'system' or not (system / 'KillingFloor.exe').is_file():
        raise ValueError('specify the System folder of an explicit private KF1 copy')
    if not (system.parent / 'Maps/KF-WestLondon.rom').is_file():
        raise ValueError('private copy must contain the WestLondon map')
    game_root = system.parent
    if game_root == run_dir or game_root in run_dir.parents or run_dir in game_root.parents:
        raise ValueError('run directory must be outside the game copy')
    if ROOT == run_dir or ROOT in run_dir.parents:
        raise ValueError('run directory must be outside the package')
    if any((system / name).exists() or (system / name).is_symlink() for name in COLLISIONS):
        raise FileExistsError('private copy contains a hook, marker, output or conflicting scene; nothing overwritten')
    for p in (system, run_dir.parent):
        if shutil.disk_usage(p).free < FLOOR:
            raise RuntimeError('free-space floor + 512 MiB margin not met')
    # trial-029 reads this path with the Windows ANSI CRT; avoid lossy path conversion.
    str(run_dir / 'slot.bin').encode('ascii')
    return system, run_dir


def finish_process(proc, seconds=10):
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=seconds)


def spawn_owned(name, command, directory, stdout, stderr, cwd=None, env=None):
    """Gate child creation until its wrapper has been assigned to a kill-on-close Job."""
    kernel, job = capture_window.create_kill_job()
    proc = None
    try:
        go, pidfile = directory / (name + '-go'), directory / (name + '-pid.txt')
        # This stdlib-only gate must be the actual Python process, not a venv
        # redirector that can create an unassigned Python before the gate runs.
        # The supplied child command still selects the original worker venv.
        wrapper = [sys._base_executable, '-B', str(ROOT / 'job_start.py'), '--go', str(go),
                   '--pid-file', str(pidfile), '--'] + command
        proc = subprocess.Popen(wrapper, cwd=cwd, env=env, stdout=stdout, stderr=stderr)
        capture_window.assign_job(kernel, job, proc)
        write_new(go, b'job assigned\n')
        deadline = time.monotonic() + 15
        while not pidfile.exists():
            if proc.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError(name + ' child did not start')
            time.sleep(.05)
        pid = int(pidfile.read_text(encoding='ascii').strip())
        if pid <= 0:
            raise RuntimeError('invalid owned child PID')
        return proc, pid, (kernel, job)
    except BaseException:
        try:
            capture_window.close_job(kernel, job)
        finally:
            finish_process(proc)
        raise


def run(args):
    args = argparse.Namespace(**vars(args))
    if sys.platform != 'win32' or struct.calcsize('P') != 8:
        raise RuntimeError('native Windows and 64-bit Python required')
    if not args.confirm_private_copy or not args.confirm_visual_review:
        raise ValueError('private-copy and visual-review acknowledgements are required')
    system, out = validate_game(args.private_system, args.run_dir)
    hook = ROOT / 'bridge/windows/overlay/d3d9-overlay.dll'
    chain = args.chain.resolve()
    check_smoke(args.smoke_dir, hook, chain)
    reference_mode = getattr(args, 'reference_mode', 'retained')
    comparison = check_comparison(args.comparison_dir, reference_mode)
    if not args.ffmpeg.is_file():
        raise ValueError('an existing private ffmpeg.exe is required; no automatic install')
    if core.sha(args.model) != core.CHECKPOINT:
        raise ValueError('checkpoint hash differs')
    if not 1 <= args.entry_timeout <= 300 or not 1 <= args.duration <= 200:
        raise ValueError('invalid bounded time')
    out.mkdir(mode=0o700)
    baseline_free = {str(p): shutil.disk_usage(p).free for p in (system, out)}
    ipc = system / 'live-ipc'
    ipc_owned = False
    installed = {}
    worker = game = primer = recorder = None
    jobs = []
    game_process = None
    started = time.monotonic()
    status = {'native_windows_execution_attempted': True, 'windows_game_validated': False,
              'visual_quality_assessed_by_controller': False,
              'elapsed_seconds': None, 'error': None, 'game_exit': None, 'worker_exit': None,
              'boundary_done': False, 'boundary_result': None,
              'recording_performed': False, 'capture_exit': None}
    save_new(out / 'once.json', {'private_system': str(system), 'chain_sha256': core.sha(chain),
             'hook_sha256': core.sha(hook), 'model_sha256': core.CHECKPOINT,
             'comparison': comparison, 'reference_mode': reference_mode,
             'entry_timeout': args.entry_timeout, 'duration': args.duration,
             'vram_global_used_limit_mib': 22528, 'vram_free_min_mib': 512,
             'filesystem_growth_limit_bytes': FS_GROWTH_LIMIT, 'free_space_floor_bytes': FLOOR,
             'input_policy': 'no synthetic input or focus acquisition', 'previous_native_validation': False})
    logs = []
    try:
        ipc.mkdir()
        ipc_owned = True
        installed[system / 'd3d9.dll'] = checked_copy(hook, system / 'd3d9.dll')
        installed[system / 'd3d9_chain.dll'] = checked_copy(chain, system / 'd3d9_chain.dll')
        save_recovery_receipt(system, out, installed)
        for name, data in (('playable-ipc-path.txt', (str(out / 'slot.bin') + '\n').encode('ascii')),
                           ('playable-mode.txt', b'on\n'), ('manual-boundary.txt', b'manual-v1\n')):
            write_new(system / name, data)
            installed[system / name] = core.sha(system / name)
        slot.create(out / 'slot.bin')
        for name in ('tmp', 'cache', 'dxvk'):
            (out / name).mkdir()
        env = os.environ.copy()
        env.update({'TEMP': str(out / 'tmp'), 'TMP': str(out / 'tmp'),
                    'TORCH_HOME': str(out / 'cache'), 'XDG_CACHE_HOME': str(out / 'cache'),
                    'DXVK_LOG_PATH': str(out / 'dxvk'), 'DXVK_STATE_CACHE_PATH': str(out / 'cache'),
                    'PYTHONDONTWRITEBYTECODE': '1'})
        for name in ('worker.stdout', 'worker.stderr', 'game.stdout', 'game.stderr', 'prime.stdout', 'prime.stderr',
                     'capture.stdout', 'capture.stderr'):
            logs.append((out / name).open('xb'))
        stage = ROOT / 'worker/stage.py'
        worker, _, worker_job = spawn_owned('worker', [sys.executable, '-B', str(stage), 'serve', '--model', str(args.model.resolve()),
                  '--slot', str(out / 'slot.bin'), '--ipc-dir', str(ipc), '--output-dir', str(out / 'worker'),
                  '--comparison-mode', 'measure', '--reference-mode', reference_mode,
                  '--timeout', '300', '--max-vram-used-mib', '22528',
                  '--min-vram-free-mib', '512'], out, logs[0], logs[1], env=env)
        jobs.append(worker_job)

        def monitor(deadline):
            if (out / 'stop.request').exists():
                raise InterruptedError('stop requested')
            if time.monotonic() >= deadline:
                raise TimeoutError('bounded controller deadline')
            if worker.poll() is not None:
                raise RuntimeError('worker exited: ' + str(worker.returncode))
            if (ipc / 'worker-error.txt').exists():
                raise RuntimeError('worker reported an IPC error')
            for p in (system, out):
                free = shutil.disk_usage(p).free
                if free < FLOOR or baseline_free[str(p)] - free > FS_GROWTH_LIMIT:
                    raise RuntimeError('free-space floor + margin reached')
            watched = [out / n for n in ('worker.stdout', 'worker.stderr', 'game.stdout', 'game.stderr',
                                         'prime.stdout', 'prime.stderr', 'capture.stdout', 'capture.stderr')]
            watched += [system / 'draw-events.jsonl', out / 'worker/gpu-monitor.jsonl']
            if sum(p.stat().st_size for p in watched if p.exists()) > LOG_LIMIT:
                raise RuntimeError('log output limit reached')
            gpu_log = out / 'worker/gpu-monitor.jsonl'
            if gpu_log.exists() and time.time() - gpu_log.stat().st_mtime > 15:
                raise RuntimeError('VRAM monitor heartbeat stalled')
            time.sleep(.05)

        deadline = time.monotonic() + 300
        while not (ipc / 'slot-ready.txt').exists():
            monitor(deadline)
        primer, _, primer_job = spawn_owned('prime', [sys.executable, '-B', str(stage), 'prime', '--slot', str(out / 'slot.bin'),
                  '--comparison-mode', 'measure', '--reference-mode', reference_mode,
                  '--oracle-output', str(out / 'worker/oracle-output.bmp'),
                  '--timeout', '180'], out, logs[4], logs[5])
        jobs.append(primer_job)
        while primer.poll() is None:
            monitor(deadline)
        if primer.returncode:
            raise RuntimeError('same-slot prime failed: ' + str(primer.returncode))
        while not (ipc / 'worker-ready.txt').exists():
            monitor(deadline)
        game, game_pid, game_job = spawn_owned('game', [str(system / 'KillingFloor.exe'),
            'KF-WestLondon.rom?game=KFMod.KFGameType?bIsLanMatch=True', '-windowed', '-nosplash'],
            out, logs[2], logs[3], cwd=system, env=env)
        jobs.append(game_job)
        process_kernel, game_process, game_started = capture_window.pin_process(game_pid, job=game_job[1])
        write_new(out / 'waiting-for-user.txt', b'worker primed; owned game launched\n')
        print('Worker warm; manually enter WestLondon. Then create start.request with the signal command.', flush=True)
        deadline = time.monotonic() + args.entry_timeout
        while not (out / 'start.request').exists():
            if game.poll() is not None:
                raise RuntimeError('owned game exited before manual start')
            monitor(deadline)
        write_new(system / 'boundary-start.txt', b'manual WestLondon start\n')
        installed[system / 'boundary-start.txt'] = core.sha(system / 'boundary-start.txt')
        hwnd = capture_window.find_window(game_pid)
        recorder = subprocess.Popen([sys.executable, '-B', str(ROOT / 'capture_window.py'), 'supervise',
                    '--pid', str(game_pid), '--started', str(game_started), '--hwnd', str(hwnd), '--ffmpeg', str(args.ffmpeg.resolve()),
                    '--output', str(out / 'recording.mp4'), '--duration', str(args.duration),
                    '--stop', str(out / 'capture-stop')], stdout=logs[6], stderr=logs[7])
        deadline = time.monotonic() + args.duration
        while not (system / 'boundary-done.txt').exists() and game.poll() is None:
            if recorder.poll() is not None:
                if recorder.returncode:
                    raise RuntimeError('owned window recorder failed: ' + str(recorder.returncode))
                break
            if (out / 'stop.request').exists() or time.monotonic() >= deadline:
                break
            monitor(deadline + 1)
        status['boundary_done'] = (system / 'boundary-done.txt').exists()
    except BaseException as exc:
        status['error'] = type(exc).__name__ + ': ' + str(exc)
    finally:
        # Ask the active hook to stop while the worker is still available.
        # On errors, also unblock any synchronous slot wait immediately.
        try:
            if ipc_owned and status['error'] is not None and not (ipc / 'worker-error.txt').exists():
                write_new(ipc / 'worker-error.txt', b'controller error shutdown\n')
            if game is not None and game.poll() is None and not (system / 'boundary-stop.txt').exists():
                write_new(system / 'boundary-stop.txt', b'controller shutdown\n')
                installed[system / 'boundary-stop.txt'] = core.sha(system / 'boundary-stop.txt')
            deadline = time.monotonic() + 5
            while (game is not None and game.poll() is None and
                   not (system / 'boundary-done.txt').exists() and time.monotonic() < deadline):
                time.sleep(.05)
            status['boundary_done'] = (system / 'boundary-done.txt').exists()
            if recorder is not None and recorder.poll() is None:
                write_new(out / 'capture-stop', b'controller shutdown\n')
                try:
                    recorder.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    finish_process(recorder)
            status['capture_exit'] = recorder.returncode if recorder else None
            status['recording_performed'] = bool(recorder and recorder.returncode == 0 and
                                                 (out / 'recording.mp4').is_file() and
                                                 (out / 'recording.mp4').stat().st_size > 0)
            if ipc_owned and not (ipc / 'stop.txt').exists():
                write_new(ipc / 'stop.txt', b'controller shutdown\n')
            if worker is not None and worker.poll() is None:
                try:
                    worker.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
        except Exception as exc:
            status['cleanup_error'] = repr(exc)
        finally:
            # Every owned process gets a termination attempt even when creating
            # a marker or waiting for another process fails.
            for kernel, job in reversed(jobs):
                try:
                    capture_window.close_job(kernel, job)
                except Exception as exc:
                    status.setdefault('cleanup_errors', []).append(repr(exc))
            for proc in (recorder, game, primer, worker):
                try:
                    finish_process(proc)
                except Exception as exc:
                    status.setdefault('cleanup_errors', []).append(repr(exc))
            if game_process:
                try:
                    capture_window.close_job(process_kernel, game_process)
                except Exception as exc:
                    status.setdefault('cleanup_errors', []).append(repr(exc))
            status['game_exit'] = game.returncode if game else None
            status['worker_exit'] = worker.returncode if worker else None
        for f in logs:
            f.close()
        # A done marker also accompanies hook failure. Read the flushed journal
        # after shutdown; preserve its actual outcome even when capture succeeded.
        if status['boundary_done']:
            try:
                status['boundary_result'] = read_boundary_result(system)
                check_boundary_result(status['boundary_result'])
            except Exception as exc:
                if status['error'] is None:
                    status['error'] = type(exc).__name__ + ': ' + str(exc)
        # Remove only bytes we created and whose contents still match, after owned game exit.
        if ((game is None or game.poll() is not None) and 'cleanup_error' not in status
                and 'cleanup_errors' not in status):
            for path, digest in installed.items():
                try:
                    if path.is_file() and not path.is_symlink() and core.sha(path) == digest:
                        path.unlink()
                except OSError as exc:
                    status.setdefault('cleanup_errors', []).append(str(exc))
        status['elapsed_seconds'] = time.monotonic() - started
        save_new(out / 'terminal.json', status)
    print(json.dumps(status, ensure_ascii=False), flush=True)
    return 0 if (status['error'] is None and 'cleanup_error' not in status and 'cleanup_errors' not in status
                 and status['boundary_done'] and status['recording_performed']) else 2


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    start = sub.add_parser('run')
    for name in ('private-system', 'run-dir', 'model', 'chain', 'smoke-dir', 'comparison-dir', 'ffmpeg'):
        start.add_argument('--' + name, type=Path, required=True)
    start.add_argument('--confirm-private-copy', action='store_true')
    start.add_argument('--confirm-visual-review', action='store_true')
    start.add_argument('--reference-mode', choices=['retained', 'synthetic'], default='retained')
    start.add_argument('--entry-timeout', type=int, default=180)
    start.add_argument('--duration', type=int, default=190)
    signal = sub.add_parser('signal')
    signal.add_argument('--run-dir', type=Path, required=True)
    signal.add_argument('request', choices=('start', 'stop'))
    args = p.parse_args(argv)
    if args.action == 'signal':
        if not (args.run_dir / 'once.json').is_file() or (args.run_dir / 'terminal.json').exists():
            raise ValueError('not an active controller identity')
        if args.request == 'start' and not (args.run_dir / 'waiting-for-user.txt').is_file():
            raise ValueError('game is not yet ready for the manual start signal')
        write_new(args.run_dir / (args.request + '.request'), b'manual request\n')
        return 0
    return run(args)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
