"""Run the original controller's read-only game-copy conflict checks."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'runtime'))
import controller


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--system', type=Path, required=True)
    p.add_argument('--run-dir', type=Path, required=True)
    args = p.parse_args()
    system, out = controller.validate_game(args.system, args.run_dir)
    if out.exists() or out.is_symlink():
        raise FileExistsError('run directory already exists')
    print(json.dumps({'dry_run': True, 'system': str(system), 'run_dir': str(out),
                      'would_create': ['d3d9.dll', 'd3d9_chain.dll', 'live-ipc',
                                       'playable-ipc-path.txt', 'playable-mode.txt', 'manual-boundary.txt'],
                      'model_chain_smoke_and_gpu_checked': False,
                      'windows_game_or_safety_verified': False}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
