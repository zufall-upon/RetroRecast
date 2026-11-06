"""Private staging entry points. Windows game integration remains unvalidated."""
import argparse
import json
import sys
import threading
import time
import traceback
from pathlib import Path

import numpy as np
import core
import slot

DEFAULT_REFERENCE = Path(__file__).resolve().parent.parent / 'tests' / 'known-input-output'


def save_json(path, data):
    with path.open('x', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2, allow_nan=False)
        f.write('\n')


def save_bytes(path, data):
    with path.open('xb') as f:
        f.write(data)


def save_raw(path, raw):
    with path.open('xb') as f:
        np.save(f, raw, allow_pickle=False)


def output_dir(path):
    path.mkdir(mode=0o700)  # exclusive: a spent output identity is never reused


def oracle_input(mode, comparison_mode, reference):
    if mode == 'retained':
        core.verify_references(reference)
        return (reference / 'input-24.bmp').read_bytes(), dict(core.REFERENCE)
    if mode != 'synthetic' or comparison_mode != 'measure':
        raise ValueError('synthetic reference requires measure mode')
    return core.synthetic_bmp(), core.synthetic_identity()


def oracle_measurement(mode, reference, raw, pixels):
    if mode == 'retained':
        return core.compare(raw, pixels, reference)
    if core.quantize(raw) != pixels:
        raise ValueError('synthetic result raw and BMP disagree')
    # A self-consistency measurement cannot establish quality or old-oracle match.
    return {'matches': None, 'reference_kind': 'synthetic-self-consistency'}


def run_compare(args):
    args = argparse.Namespace(**vars(args))
    mode, reference, comparison_mode = getattr(args, 'reference_mode', 'retained'), args.reference, args.comparison_mode
    input_data, reference_identity = oracle_input(mode, comparison_mode, reference)
    rgb = core.bmp_to_rgb(input_data)
    output_dir(args.output_dir)
    model = core.load_model(args.model, args.device)
    raw, pixels, timings = core.infer(model, rgb, args.device)
    result = oracle_measurement(mode, reference, raw, pixels)
    result.update({'checkpoint_sha256': core.CHECKPOINT, 'reference_sha256': reference_identity,
                   'timings': timings, 'device': args.device,
                   'comparison_mode': comparison_mode, 'measurement_completed': True,
                   'visual_quality_assessed': False})
    save_raw(args.output_dir / 'raw.npy', raw)
    save_bytes(args.output_dir / 'output.bmp', core.HEADER + pixels)
    save_json(args.output_dir / 'comparison.json', result)
    print(json.dumps(result, ensure_ascii=False, allow_nan=False), flush=True)
    return 0 if comparison_mode == 'measure' or result['matches'] else 1


def prime(args):
    args = argparse.Namespace(**vars(args))
    mode, reference, comparison_mode = getattr(args, 'reference_mode', 'retained'), args.reference, args.comparison_mode
    input_data, _ = oracle_input(mode, comparison_mode, reference)
    core.bmp_to_rgb(input_data)
    shared = slot.Slot(args.slot)
    try:
        shared.request(1, input_data[54:], diagnostic=4)
        slot.wait_for(shared, slot.RESPONSE, args.timeout)
        output = shared.response(1)
        expected_path = args.oracle_output if comparison_mode == 'measure' else reference / 'output-24.bmp'
        expected = expected_path.read_bytes()
        core.bmp_to_rgb(expected)  # the service's saved image must be complete
        if core.HEADER + output != expected:
            shared.publish(slot.ERROR)
            raise ValueError('shared-slot oracle output differs')
        shared.publish(slot.FREE)
        print('shared-slot oracle acknowledged', flush=True)
    except BaseException:
        shared.publish(slot.ERROR)
        raise
    finally:
        shared.close()
    return 0


def serve(args):
    args = argparse.Namespace(**vars(args))
    mode, reference, comparison_mode = getattr(args, 'reference_mode', 'retained'), args.reference, args.comparison_mode
    input_data, reference_identity = oracle_input(mode, comparison_mode, reference)
    output_dir(args.output_dir)
    shared = slot.Slot(args.slot)
    ipc = args.ipc_dir
    monitor_stop = threading.Event()
    monitor_thread = None
    try:
        if args.max_vram_used_mib or args.min_vram_free_mib:
            ready = threading.Event()
            failures = []

            def monitor_gpu():
                try:
                    import torch
                    with (args.output_dir / 'gpu-monitor.jsonl').open('x', encoding='utf-8') as f:
                        while not monitor_stop.is_set():
                            free, total = map(int, torch.cuda.mem_get_info())
                            if not 0 <= free <= total or total <= 0:
                                raise RuntimeError('invalid VRAM telemetry')
                            row = {'free_vram_bytes': free, 'total_vram_bytes': total,
                                   'global_used_vram_bytes': total - free, 'time_monotonic': time.monotonic()}
                            f.write(json.dumps(row, allow_nan=False) + '\n')
                            f.flush()
                            if ((args.max_vram_used_mib and total - free > args.max_vram_used_mib * 1024**2) or
                                    (args.min_vram_free_mib and free < args.min_vram_free_mib * 1024**2)):
                                raise RuntimeError('VRAM safety limit')
                            ready.set()
                            monitor_stop.wait(1)
                except BaseException:
                    if not monitor_stop.is_set():
                        failures.append(traceback.format_exc())
                        try:
                            shared.publish(slot.ERROR)
                            save_bytes(ipc / 'worker-error.txt', failures[-1].encode('utf-8'))
                        except OSError:
                            pass
                    ready.set()

            monitor_thread = threading.Thread(target=monitor_gpu, daemon=True)
            monitor_thread.start()
            if not ready.wait(30) or failures:
                raise RuntimeError('GPU monitor unavailable or limit reached')
        model = core.load_model(args.model, args.device)
        save_bytes(ipc / 'slot-ready.txt', b'ready for saved oracle\n')
        slot.wait_for(shared, slot.REQUEST, args.timeout)
        request = shared.receive(1)
        if request is None or request[2] != 4 or core.HEADER + request[1] != input_data:
            raise ValueError('shared-slot oracle request missing or differs')
        raw, pixels, timings = core.infer(model, core.bgra_to_rgb(request[1]), args.device)
        result = oracle_measurement(mode, reference, raw, pixels)
        result['timings'] = timings
        result.update({'comparison_mode': comparison_mode, 'reference_sha256': reference_identity,
                       'measurement_completed': True,
                       'visual_quality_assessed': False})
        save_raw(args.output_dir / 'oracle-raw.npy', raw)
        save_bytes(args.output_dir / 'oracle-output.bmp', core.HEADER + pixels)
        save_json(args.output_dir / 'oracle-comparison.json', result)
        if comparison_mode == 'exact' and not result['matches']:
            raise ValueError('shared-slot oracle output differs')
        shared.respond(1, pixels)
        slot.wait_for(shared, slot.FREE, args.timeout)
        save_bytes(ipc / 'worker-ready.txt', b'ready\n')
        sequence = 1
        start = time.monotonic()
        with (args.output_dir / 'worker-events.jsonl').open('x', encoding='utf-8') as events:
            while slot.wait_for(shared, slot.REQUEST, args.timeout, lambda: (ipc / 'stop.txt').exists()):
                request = shared.receive(sequence)
                if request is None:
                    continue
                diagnostic = request[2]
                if diagnostic > 3:
                    raise ValueError('diagnostic save limit')
                raw, pixels, timings = core.infer(model, core.bgra_to_rgb(request[1]), args.device)
                if diagnostic:
                    save_bytes(args.output_dir / f'input-{sequence}.bmp', core.HEADER + request[1])
                    save_bytes(args.output_dir / f'output-{sequence}.bmp', core.HEADER + pixels)
                    save_raw(args.output_dir / f'raw-{sequence}.npy', raw)
                shared.respond(sequence, pixels)
                events.write(json.dumps({'event': 'frame', 'index': sequence, 'diagnostic': diagnostic,
                                         'timings': timings}, allow_nan=False) + '\n')
                events.flush()
                sequence += 1
            events.write(json.dumps({'event': 'finished', 'frames': sequence - 1,
                                     'elapsed_seconds': time.monotonic() - start}) + '\n')
        return 0
    except BaseException:
        shared.publish(slot.ERROR)
        try:
            save_bytes(ipc / 'worker-error.txt', traceback.format_exc().encode('utf-8'))
        except OSError:
            pass
        raise
    finally:
        monitor_stop.set()
        if monitor_thread:
            monitor_thread.join(timeout=5)
        # A stuck GPU telemetry call may outlive the join. Do not unmap memory
        # beneath that thread; the terminating process reclaims it instead.
        if monitor_thread is None or not monitor_thread.is_alive():
            shared.close()


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    init = sub.add_parser('init-slot', help='create a new private single-slot file')
    init.add_argument('--slot', type=Path, required=True)
    prime_p = sub.add_parser('prime', help='send and acknowledge the saved oracle through the slot')
    prime_p.add_argument('--slot', type=Path, required=True)
    prime_p.add_argument('--reference', type=Path, default=DEFAULT_REFERENCE)
    prime_p.add_argument('--reference-mode', choices=['retained', 'synthetic'], default='retained')
    prime_p.add_argument('--timeout', type=float, default=180)
    prime_p.add_argument('--comparison-mode', choices=['exact', 'measure'], default='exact')
    prime_p.add_argument('--oracle-output', type=Path,
                         help='in measure mode, the exact BMP written by this service for the same request')
    compare_p = sub.add_parser('compare', help='run the saved frame and compare with the retained raw/BMP')
    service = sub.add_parser('serve', help='oracle-first same-frame slot service (experimental)')
    for cmd in (compare_p, service):
        cmd.add_argument('--model', type=Path, required=True)
        cmd.add_argument('--output-dir', type=Path, required=True, help='must not already exist')
        cmd.add_argument('--reference', type=Path, default=DEFAULT_REFERENCE)
        cmd.add_argument('--reference-mode', choices=['retained', 'synthetic'], default='retained')
        cmd.add_argument('--device', choices=['cuda'], default='cuda')
        cmd.add_argument('--comparison-mode', choices=['exact', 'measure'], default='exact')
    service.add_argument('--slot', type=Path, required=True)
    service.add_argument('--ipc-dir', type=Path, required=True, help='existing, empty, private directory')
    service.add_argument('--timeout', type=float, default=180)
    service.add_argument('--max-vram-used-mib', type=int, default=0, help='0 disables global VRAM use limit')
    service.add_argument('--min-vram-free-mib', type=int, default=0, help='0 disables free VRAM limit')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    if hasattr(args, 'timeout') and args.timeout <= 0:
        raise ValueError('timeout must be positive')
    if args.command == 'prime':
        if (args.comparison_mode == 'measure') != (args.oracle_output is not None):
            raise ValueError('measure mode requires --oracle-output; exact mode must not specify it')
    if args.command == 'serve' and (args.max_vram_used_mib < 0 or args.min_vram_free_mib < 0):
        raise ValueError('VRAM limits must not be negative')
    if args.command == 'init-slot':
        slot.create(args.slot)
        return 0
    if args.command == 'compare':
        return run_compare(args)
    if args.command == 'prime':
        return prime(args)
    return serve(args)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(2)
