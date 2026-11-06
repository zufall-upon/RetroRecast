"""CPU-only regressions: procedural pixels; no game, model or Windows launch."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest import mock

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'runtime'
sys.path.insert(0, str(RUNTIME / 'worker'))
sys.path.insert(0, str(RUNTIME))
sys.path.insert(0, str(ROOT / 'scripts'))
import core
import slot
import stage
import controller
import capture_window
import recover
import verify_package


def wait_path(path, timeout=5):
    end = time.monotonic() + timeout
    while not path.exists():
        if time.monotonic() >= end:
            raise TimeoutError(str(path))
        time.sleep(.005)


class ProceduralFrameContract(unittest.TestCase):
    def test_synthetic_input_pad_and_reject_bad_bmp_and_nonfinite_output(self):
        data = core.synthetic_bmp()
        rgb = core.bmp_to_rgb(data)
        self.assertEqual(rgb.shape, (540, 960, 3))
        self.assertEqual(tuple(rgb[0, 0]), (0, 0, 0))
        self.assertEqual(tuple(rgb[1, 1]), (4, 6, 18))
        a = core.prepare(rgb)
        self.assertEqual(a.shape, (1, 3, 544, 960))
        np.testing.assert_array_equal(a[0, :, 0, :], a[0, :, 2, :])
        np.testing.assert_array_equal(a[0, :, 543, :], a[0, :, 541, :])
        with self.assertRaisesRegex(ValueError, 'BMP'):
            core.bmp_to_rgb(data[:-1])
        raw = np.zeros((1, 3, 544, 960), dtype=np.float32)
        raw[0, 0, 0, 0] = np.nan
        with self.assertRaisesRegex(ValueError, 'non-finite'):
            core.quantize(raw)

    def test_slot_sequence_failure_is_reported_to_peer(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'slot'
            slot.create(path)
            with self.assertRaises(FileExistsError):
                slot.create(path)
            producer, worker = slot.Slot(path), slot.Slot(path)
            try:
                frame = core.synthetic_bmp()[54:]
                producer.request(2, frame, diagnostic=4)
                with self.assertRaisesRegex(ValueError, 'sequence'):
                    worker.receive(1)
                self.assertEqual(producer.state(), slot.ERROR)
            finally:
                worker.close()
                producer.close()

    def test_synthetic_service_and_prime_capture_mode_for_the_call(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            ipc, out, shared_path = d / 'ipc', d / 'out', d / 'slot'
            ipc.mkdir()
            slot.create(shared_path)
            args = stage.parser().parse_args(['serve', '--slot', str(shared_path), '--ipc-dir', str(ipc),
                  '--output-dir', str(out), '--model', str(d / 'not-loaded.pth'), '--timeout', '5',
                  '--reference-mode', 'synthetic', '--comparison-mode', 'measure'])
            raw = np.zeros((1, 3, 544, 960), dtype=np.float32)
            pixels = core.quantize(raw)
            errors, results = [], []

            def run_service():
                try:
                    results.append(stage.serve(args))
                except BaseException as e:
                    errors.append(e)

            with mock.patch.object(core, 'load_model', return_value=object()), \
                 mock.patch.object(core, 'infer', return_value=(raw, pixels, {'total_worker_ms': 0})) as inference:
                thread = threading.Thread(target=run_service)
                thread.start()
                try:
                    wait_path(ipc / 'slot-ready.txt')
                    # A call uses the mode it captured when it began, not a later change.
                    args.reference_mode = 'retained'
                    args.comparison_mode = 'exact'
                    prime_args = stage.parser().parse_args(['prime', '--slot', str(shared_path),
                        '--reference-mode', 'synthetic', '--comparison-mode', 'measure',
                        '--oracle-output', str(out / 'oracle-output.bmp'), '--timeout', '5'])
                    self.assertEqual(stage.prime(prime_args), 0)
                    wait_path(ipc / 'worker-ready.txt')
                    report = json.loads((out / 'oracle-comparison.json').read_text())
                    self.assertEqual(report['reference_sha256'], core.synthetic_identity())
                    self.assertEqual(report['comparison_mode'], 'measure')
                    self.assertIsNone(report['matches'])
                    self.assertFalse(report['visual_quality_assessed'])
                    # The startup oracle must not replace the subsequent live
                    # game-frame loop. Send a *different* frame at the game's
                    # reset sequence 1, then a second frame at sequence 2.
                    peer = slot.Slot(shared_path)
                    try:
                        live_frame = bytes([17, 29, 43, 255]) * (core.WIDTH * core.HEIGHT)
                        for sequence in (1, 2):
                            peer.request(sequence, live_frame, diagnostic=0)
                            slot.wait_for(peer, slot.RESPONSE, 5)
                            self.assertEqual(peer.response(sequence), pixels)
                            peer.publish(slot.FREE)
                    finally:
                        peer.close()
                finally:
                    (ipc / 'stop.txt').write_text('stop')
                    thread.join(6)
            self.assertFalse(thread.is_alive())
            self.assertEqual(errors, [])
            self.assertEqual(results, [0])
            self.assertEqual(inference.call_count, 3)
            for call in inference.call_args_list[1:]:
                self.assertEqual(tuple(call.args[1][0, 0]), (43, 29, 17))
            events = [json.loads(line) for line in (out / 'worker-events.jsonl').read_text().splitlines()]
            self.assertEqual([r['index'] for r in events if r['event'] == 'frame'], [1, 2])
            self.assertEqual(events[-1]['frames'], 2)
            shared = slot.Slot(shared_path)
            try:
                self.assertEqual(shared.state(), slot.FREE)
            finally:
                shared.close()

    def test_synthetic_service_rejects_wrong_request_and_publishes_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            ipc, out, shared_path = d / 'ipc', d / 'out', d / 'slot'
            ipc.mkdir()
            slot.create(shared_path)
            args = stage.parser().parse_args(['serve', '--slot', str(shared_path), '--ipc-dir', str(ipc),
                  '--output-dir', str(out), '--model', str(d / 'not-loaded.pth'), '--timeout', '5',
                  '--reference-mode', 'synthetic', '--comparison-mode', 'measure'])
            errors = []
            def worker():
                try:
                    stage.serve(args)
                except BaseException as e:
                    errors.append(e)
            with mock.patch.object(core, 'load_model', return_value=object()), \
                 mock.patch.object(core, 'infer') as infer:
                thread = threading.Thread(target=worker)
                thread.start()
                wait_path(ipc / 'slot-ready.txt')
                peer = slot.Slot(shared_path)
                try:
                    peer.request(1, bytes(slot.PIXELS), diagnostic=4)
                    thread.join(6)
                    self.assertFalse(thread.is_alive())
                    self.assertEqual(peer.state(), slot.ERROR)
                    self.assertTrue((ipc / 'worker-error.txt').is_file())
                finally:
                    peer.close()
            self.assertEqual(len(errors), 1)
            self.assertIsInstance(errors[0], ValueError)
            infer.assert_not_called()

    def test_missing_retained_reference_and_synthetic_exact_fail_before_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            base = [sys.executable, '-B', str(RUNTIME / 'worker/stage.py'), 'compare',
                    '--model', str(d / 'absent.pth'), '--output-dir', str(d / 'out'),
                    '--reference', str(d / 'missing')]
            for extra, message in (([], 'FileNotFoundError'),
                                   (['--reference-mode', 'synthetic'], 'synthetic reference requires measure')):
                p = subprocess.run(base + extra, capture_output=True, text=True, timeout=20)
                self.assertEqual(p.returncode, 2, p.stderr)
                self.assertIn(message, p.stderr)
                self.assertFalse((d / 'out').exists())

    def test_synthetic_comparison_checks_raw_bytes_and_never_claims_reference_match(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            args = stage.parser().parse_args(['compare', '--model', str(d / 'not-loaded.pth'),
                    '--output-dir', str(d / 'out'), '--reference-mode', 'synthetic',
                    '--comparison-mode', 'measure'])
            raw = np.zeros((1, 3, 544, 960), dtype=np.float32)
            pixels = core.quantize(raw)
            with mock.patch.object(core, 'load_model', return_value=object()), \
                 mock.patch.object(core, 'infer', return_value=(raw, pixels, {'total_worker_ms': 0})):
                self.assertEqual(stage.run_compare(args), 0)
            report = controller.check_comparison(d / 'out', 'synthetic')
            self.assertIsNone(report['matches'])
            bmp = d / 'out/output.bmp'
            altered = bytearray(bmp.read_bytes())
            altered[-1] ^= 1
            bmp.write_bytes(altered)
            with self.assertRaisesRegex(ValueError, 'disagree'):
                controller.check_comparison(d / 'out', 'synthetic')


class DistributedPayloadIntegrity(unittest.TestCase):
    def fixture(self, directory):
        payload = directory / 'sample.txt'
        payload.write_bytes(b'public fixture')
        manifest = {'schema': 1, 'source_commit': 'test-source', 'files': {'sample.txt': {
            'bytes': payload.stat().st_size, 'sha256': verify_package.digest(payload.read_bytes())}}}
        (directory / 'package-manifest.json').write_text(json.dumps(manifest))
        return payload

    def test_integrity_rejects_missing_additional_or_changed_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            payload = self.fixture(d)
            self.assertTrue(verify_package.verify(d)['integrity_verified'])
            extra = d / 'unexpected.pth'
            extra.write_bytes(b'not a model')
            with self.assertRaisesRegex(ValueError, 'additional'):
                verify_package.verify(d)
            extra.unlink()
            payload.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'differs'):
                verify_package.verify(d)
            payload.unlink()
            with self.assertRaisesRegex(ValueError, 'missing'):
                verify_package.verify(d)

    def test_integrity_rejects_link_instead_of_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            payload = self.fixture(d)
            payload.unlink()
            payload.symlink_to(d / 'absent')
            with self.assertRaisesRegex(ValueError, 'linked'):
                verify_package.verify(d)


class OriginalInstallBoundary(unittest.TestCase):
    def test_recovery_receipt_failure_aborts_before_launch_and_removes_owned_dlls(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system, package, out = d / 'game/System', d / 'package', d / 'run'
            system.mkdir(parents=True)
            (system.parent / 'Maps').mkdir()
            (system / 'KillingFloor.exe').write_bytes(b'unchanged')
            (system.parent / 'Maps/KF-WestLondon.rom').write_bytes(b'mock map')
            hook = package / 'bridge/windows/overlay/d3d9-overlay.dll'
            hook.parent.mkdir(parents=True)
            hook.write_bytes(b'hook')
            chain, model, ffmpeg = d / 'chain', d / 'model', d / 'ffmpeg'
            for p in (chain, model, ffmpeg):
                p.write_bytes(b'mock')
            args = types.SimpleNamespace(private_system=system, run_dir=out, model=model, chain=chain,
                smoke_dir=d / 'smoke', comparison_dir=d / 'comparison', ffmpeg=ffmpeg,
                confirm_private_copy=True, confirm_visual_review=True, reference_mode='synthetic',
                entry_timeout=10, duration=10)
            real_sha = core.sha
            def sha(path):
                return core.CHECKPOINT if Path(path) == model else real_sha(path)
            with mock.patch.object(controller, 'ROOT', package), \
                 mock.patch.object(controller.sys, 'platform', 'win32'), \
                 mock.patch.object(controller, 'check_smoke'), \
                 mock.patch.object(controller, 'check_comparison', return_value={'measurement_completed': True}), \
                 mock.patch.object(core, 'sha', side_effect=sha), \
                 mock.patch.object(controller, 'save_recovery_receipt', side_effect=OSError('injected receipt failure')), \
                 mock.patch.object(controller, 'spawn_owned') as spawn, mock.patch('builtins.print'):
                self.assertEqual(controller.run(args), 2)
            spawn.assert_not_called()
            terminal = json.loads((out / 'terminal.json').read_text())
            self.assertIn('injected receipt failure', terminal['error'])
            self.assertFalse((system / 'd3d9.dll').exists())
            self.assertFalse((system / 'd3d9_chain.dll').exists())
            self.assertEqual((system / 'KillingFloor.exe').read_bytes(), b'unchanged')
            self.assertFalse((system / 'playable-mode.txt').exists())

    def test_controller_orders_and_cleans_owned_files_with_recovery_receipt(self):
        class Process:
            def __init__(self, code=None):
                self.returncode = code
            def poll(self):
                return self.returncode
            def wait(self, timeout=None):
                self.returncode = 0
                return 0
            def terminate(self):
                self.returncode = -1
            def kill(self):
                self.returncode = -9

        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system, package, out = d / 'game/System', d / 'package', d / 'run'
            system.mkdir(parents=True)
            (system.parent / 'Maps').mkdir()
            (system / 'KillingFloor.exe').write_bytes(b'mock executable')
            (system.parent / 'Maps/KF-WestLondon.rom').write_bytes(b'mock map')
            hook = package / 'bridge/windows/overlay/d3d9-overlay.dll'
            hook.parent.mkdir(parents=True)
            hook.write_bytes(b'hook')
            chain, model, ffmpeg = d / 'chain.dll', d / 'model', d / 'ffmpeg.exe'
            chain.write_bytes(b'chain')
            model.write_bytes(b'not a model')
            ffmpeg.write_bytes(b'not an encoder')
            args = types.SimpleNamespace(private_system=system, run_dir=out, model=model,
                chain=chain, smoke_dir=d / 'smoke', comparison_dir=d / 'comparison', ffmpeg=ffmpeg,
                confirm_private_copy=True, confirm_visual_review=True, reference_mode='synthetic',
                entry_timeout=10, duration=10)
            order, real_sha, real_write = [], core.sha, controller.write_new

            def sha(path):
                return core.CHECKPOINT if Path(path) == model else real_sha(path)

            def spawn(name, command, directory, stdout, stderr, cwd=None, env=None):
                order.append(name)
                if name == 'worker':
                    (system / 'live-ipc/slot-ready.txt').write_bytes(b'ready')
                    (system / 'live-ipc/worker-ready.txt').write_bytes(b'ready')
                if name in ('worker', 'prime'):
                    self.assertEqual(command[command.index('--reference-mode') + 1], 'synthetic')
                return Process(0 if name == 'prime' else None), 23, ('kernel', name)

            def record(command, **kwargs):
                order.append('capture')
                events = [{'event': 'boundary_window_started', 'mode': 'on', 'recipe': 'manual-v1'},
                    {'event': 'boundary_composite', 'pair': 1, 'frame': 41, 'B_read_hr': 0, 'draw_hr': 0,
                     **{k: True for k in ('request_sent', 'output_valid', 'uploaded', 'state_captured', 'restored', 'applied')}},
                    {'event': 'boundary_original_clear', 'pair': 1, 'frame': 41, 'original_hr': 0},
                    {'event': 'boundary_present', 'kind': 'C-1', 'frame': 41, 'original_present_hr': 0},
                    {'event': 'boundary_window_finished', 'mode': 'on', 'pairs': 1,
                     'failed': False, 'restore_failed': False}]
                (system / 'draw-events.jsonl').write_text(''.join(json.dumps(x) + '\n' for x in events))
                (system / 'boundary-done.txt').write_bytes(b'')
                (out / 'recording.mp4').write_bytes(b'mock')
                return Process(0)

            def write(path, data):
                real_write(path, data)
                if path == out / 'waiting-for-user.txt':
                    real_write(out / 'start.request', b'manual')

            with mock.patch.object(controller, 'ROOT', package), \
                 mock.patch.object(controller.sys, 'platform', 'win32'), \
                 mock.patch.object(controller, 'check_smoke'), \
                 mock.patch.object(controller, 'check_comparison', return_value={'measurement_completed': True}), \
                 mock.patch.object(core, 'sha', side_effect=sha), \
                 mock.patch.object(controller, 'spawn_owned', side_effect=spawn), \
                 mock.patch.object(capture_window, 'pin_process', return_value=('kernel', 'handle', 99)), \
                 mock.patch.object(capture_window, 'find_window', return_value=101), \
                 mock.patch.object(capture_window, 'close_job'), \
                 mock.patch.object(controller.subprocess, 'Popen', side_effect=record), \
                 mock.patch.object(controller, 'write_new', side_effect=write), \
                 mock.patch('builtins.print'):
                self.assertEqual(controller.run(args), 0)
            self.assertEqual(order, ['worker', 'prime', 'game', 'capture'])
            self.assertFalse((system / 'd3d9.dll').exists())
            self.assertFalse((system / 'd3d9_chain.dll').exists())
            self.assertEqual((system / 'KillingFloor.exe').read_bytes(), b'mock executable')
            receipt = json.loads((out / 'recovery.json').read_text())
            self.assertEqual(set(receipt['files']), {'d3d9.dll', 'd3d9_chain.dll'})
            terminal = json.loads((out / 'terminal.json').read_text())
            self.assertIsNone(terminal['error'])
            self.assertFalse(terminal['windows_game_validated'])

    def test_conflicts_include_dangling_symlink_and_preserve_original_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system = d / 'game/System'
            maps = d / 'game/Maps'
            system.mkdir(parents=True)
            maps.mkdir()
            (system / 'KillingFloor.exe').write_bytes(b'not a real game')
            (maps / 'KF-WestLondon.rom').write_bytes(b'not a real map')
            original = system / 'untouched.ini'
            original.write_bytes(b'keep')
            original.chmod(0o640)
            collision = system / 'd3d9.dll'
            collision.symlink_to(d / 'missing-target')
            before = original.stat()
            with self.assertRaises(FileExistsError):
                controller.validate_game(system, d / 'unused-run')
            self.assertTrue(collision.is_symlink())
            self.assertEqual(original.read_bytes(), b'keep')
            self.assertEqual(original.stat().st_mode, before.st_mode)
            self.assertEqual(original.stat().st_ino, before.st_ino)
            self.assertFalse((d / 'unused-run').exists())
            self.assertFalse((system / 'd3d9_chain.dll').exists())

    def test_linked_system_and_dangling_run_path_are_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system = d / 'game/System'
            system.mkdir(parents=True)
            (system.parent / 'Maps').mkdir()
            (system / 'KillingFloor.exe').write_bytes(b'original')
            (system.parent / 'Maps/KF-WestLondon.rom').write_bytes(b'original map')
            link, run = d / 'system-link', d / 'run-link'
            link.symlink_to(system, target_is_directory=True)
            run.symlink_to(d / 'not-created')
            with self.assertRaisesRegex(ValueError, 'linked or reparse'):
                controller.validate_game(link, d / 'new-run')
            with self.assertRaisesRegex(ValueError, 'linked or reparse'):
                controller.validate_game(system, run)
            self.assertTrue(link.is_symlink())
            self.assertTrue(run.is_symlink())
            self.assertFalse((d / 'not-created').exists())
            self.assertFalse((d / 'new-run').exists())
            self.assertEqual((system / 'KillingFloor.exe').read_bytes(), b'original')
            self.assertFalse((system / 'd3d9.dll').exists())

    def make_receipt(self, d):
        system, run = d / 'System', d / 'run'
        system.mkdir()
        run.mkdir()
        paths = [system / 'd3d9.dll', system / 'd3d9_chain.dll']
        for i, path in enumerate(paths):
            path.write_bytes(('owned' + str(i)).encode())
            path.chmod(0o640)
        controller.save_recovery_receipt(system, run, {p: core.sha(p) for p in paths})
        return system, run, paths

    def test_receipt_bound_disable_and_restore_are_dry_by_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system, run, paths = self.make_receipt(d)
            original = system / 'existing.ini'
            original.write_bytes(b'keep')
            original.chmod(0o600)
            before = original.stat()
            plan = recover.recover(system, run, 'restore')
            self.assertEqual(plan['planned'], [p.name for p in paths])
            self.assertEqual(plan['removed'], [])
            self.assertTrue(all(p.exists() for p in paths))
            with self.assertRaisesRegex(ValueError, 'stopped'):
                recover.recover(system, run, 'disable', apply=True)
            disabled = recover.recover(system, run, 'disable', apply=True, confirm_stopped=True)
            self.assertEqual(disabled['removed'], ['d3d9.dll'])
            self.assertFalse(paths[0].exists())
            self.assertTrue(paths[1].exists())
            restored = recover.recover(system, run, 'restore', apply=True, confirm_stopped=True)
            self.assertEqual(restored['removed'], ['d3d9_chain.dll'])
            self.assertEqual(original.read_bytes(), b'keep')
            self.assertEqual((original.stat().st_ino, original.stat().st_mode), (before.st_ino, before.st_mode))

    def test_recovery_rejects_changed_file_mode_symlink_and_hardlink_before_removal(self):
        for change in ('contents', 'mode', 'symlink', 'hardlink'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as tmp:
                d = Path(tmp)
                system, run, paths = self.make_receipt(d)
                first, second = paths
                if change == 'contents':
                    second.write_bytes(b'changed')
                elif change == 'mode':
                    second.chmod(0o600)
                elif change == 'symlink':
                    second.unlink()
                    second.symlink_to(d / 'not-present')
                else:
                    import os
                    os.link(second, d / 'other-link')
                with self.assertRaises(ValueError):
                    recover.recover(system, run, 'restore', apply=True, confirm_stopped=True)
                self.assertEqual(first.read_bytes(), b'owned0')
                self.assertTrue(second.exists() or second.is_symlink())
                if change == 'symlink':
                    self.assertTrue(second.is_symlink())

    def test_recovery_reports_failure_if_second_unlink_fails_without_claiming_atomicity(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system, run, paths = self.make_receipt(d)
            unlink = Path.unlink
            def injected(path, *args, **kwargs):
                if path.name == 'd3d9_chain.dll':
                    raise PermissionError('injected removal failure')
                return unlink(path, *args, **kwargs)
            with mock.patch.object(Path, 'unlink', injected):
                with self.assertRaisesRegex(PermissionError, 'injected'):
                    recover.recover(system, run, 'restore', apply=True, confirm_stopped=True)
            self.assertFalse(paths[0].exists())
            self.assertEqual(paths[1].read_bytes(), b'owned1')

    def test_dry_run_cli_checks_collisions_without_creating_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system = d / 'game/System'
            system.mkdir(parents=True)
            (system.parent / 'Maps').mkdir()
            (system / 'KillingFloor.exe').write_bytes(b'fake')
            (system.parent / 'Maps/KF-WestLondon.rom').write_bytes(b'fake')
            command = [sys.executable, '-B', str(ROOT / 'scripts/dry_run.py'),
                       '--system', str(system), '--run-dir', str(d / 'run')]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)['dry_run'])
            self.assertFalse((d / 'run').exists())
            (system / 'manual-boundary.txt').write_bytes(b'keep')
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2)
            self.assertIn('FileExistsError', result.stderr)
            self.assertEqual((system / 'manual-boundary.txt').read_bytes(), b'keep')
            self.assertFalse((d / 'run').exists())

    def test_recover_cli_rejects_unconfirmed_apply_and_reports_dry_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            system, run, paths = self.make_receipt(d)
            command = [sys.executable, '-B', str(ROOT / 'scripts/recover.py'), 'restore',
                       '--system', str(system), '--run-dir', str(run)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['planned'], [p.name for p in paths])
            result = subprocess.run(command + ['--apply'], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2)
            self.assertIn('stopped', result.stderr)
            self.assertTrue(all(p.exists() for p in paths))
            result = subprocess.run(command + ['--apply', '--confirm-stopped'], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)['removed'], [p.name for p in paths])
            self.assertFalse(any(p.exists() for p in paths))


if __name__ == '__main__':
    unittest.main()
