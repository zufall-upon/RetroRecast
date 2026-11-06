"""Wait for job assignment, launch exactly the supplied child, report PID, wait."""
import argparse
from pathlib import Path
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--go', type=Path, required=True)
    p.add_argument('--pid-file', type=Path, required=True)
    p.add_argument('command', nargs=argparse.REMAINDER)
    args = p.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        p.error('child command required')
    deadline = time.monotonic() + 15
    while not args.go.exists():
        if time.monotonic() >= deadline:
            raise TimeoutError('job assignment was not confirmed')
        time.sleep(.05)
    child = subprocess.Popen(command)
    with args.pid_file.open('x', encoding='ascii') as f:
        f.write(str(child.pid) + '\n')
    return child.wait()


if __name__ == '__main__':
    raise SystemExit(main())
