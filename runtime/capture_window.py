"""Bounded private HWND-only MP4 capture; no screen capture, input or focus changes.

PrintWindow can be unsupported or black for a D3D game: an MP4 is not a visual PASS.
Native Windows behavior is not yet tested.
"""
import argparse
import ctypes
from ctypes import wintypes as wt
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

FLOOR = 2147483648 + 512 * 1024 * 1024
MAX_BYTES = 1024 * 1024 * 1024


def bind(dll, name, result, arguments):
    f = getattr(dll, name)
    f.restype, f.argtypes = result, arguments
    return f


def api():
    if sys.platform != 'win32':
        raise RuntimeError('native Windows required')
    return ctypes.WinDLL('user32', use_last_error=True), ctypes.WinDLL('gdi32', use_last_error=True)


def check_window(user, hwnd, pid):
    is_window = bind(user, 'IsWindow', wt.BOOL, [wt.HWND])
    is_visible = bind(user, 'IsWindowVisible', wt.BOOL, [wt.HWND])
    owner = bind(user, 'GetWindowThreadProcessId', wt.DWORD, [wt.HWND, ctypes.POINTER(wt.DWORD)])
    rectfn = bind(user, 'GetClientRect', wt.BOOL, [wt.HWND, ctypes.POINTER(wt.RECT)])
    actual, rect = wt.DWORD(), wt.RECT()
    if (not is_window(hwnd) or not is_visible(hwnd) or not owner(hwnd, ctypes.byref(actual)) or
            actual.value != pid or not rectfn(hwnd, ctypes.byref(rect)) or
            (rect.right - rect.left, rect.bottom - rect.top) != (960, 540)):
        raise RuntimeError('owned visible 960x540 game client window lost or changed')


def find_window(pid):
    user, _ = api()
    owner = bind(user, 'GetWindowThreadProcessId', wt.DWORD, [wt.HWND, ctypes.POINTER(wt.DWORD)])
    callback_type = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    enum = bind(user, 'EnumWindows', wt.BOOL, [callback_type, wt.LPARAM])
    found = []

    @callback_type
    def visit(hwnd, unused):
        del unused
        actual = wt.DWORD()
        if owner(hwnd, ctypes.byref(actual)) and actual.value == pid:
            try:
                check_window(user, hwnd, pid)
                found.append(int(hwnd))
            except RuntimeError:
                pass
        return True

    if not enum(visit, 0) or len(found) != 1:
        raise RuntimeError('expected exactly one visible 960x540 window owned by the launched game PID')
    return found[0]


class BasicLimit(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_int64), ('PerJobUserTimeLimit', ctypes.c_int64),
                ('LimitFlags', wt.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]


class IOCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint64) for n in ('ReadOperationCount', 'WriteOperationCount',
                'OtherOperationCount', 'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]


class ExtendedLimit(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IOCounters),
                ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


def create_kill_job():
    if sys.platform != 'win32':
        raise RuntimeError('native Windows required')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    create = bind(kernel, 'CreateJobObjectW', wt.HANDLE, [ctypes.c_void_p, wt.LPCWSTR])
    configure = bind(kernel, 'SetInformationJobObject', wt.BOOL, [wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD])
    job = create(None, None)
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    limits = ExtendedLimit()
    limits.BasicLimitInformation.LimitFlags = 0x2000
    if not configure(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
        error = ctypes.get_last_error()
        close_job(kernel, job)
        raise ctypes.WinError(error)
    return kernel, job


def assign_job(kernel, job, proc):
    assign = bind(kernel, 'AssignProcessToJobObject', wt.BOOL, [wt.HANDLE, wt.HANDLE])
    if not assign(job, wt.HANDLE(int(proc._handle))):
        raise ctypes.WinError(ctypes.get_last_error())


def close_job(kernel, job):
    close = bind(kernel, 'CloseHandle', wt.BOOL, [wt.HANDLE])
    if not close(job):
        raise ctypes.WinError(ctypes.get_last_error())


def pin_process(pid, expected_started=None, job=None):
    """Keep the exact process object alive so its PID cannot be reused during capture."""
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    open_process = bind(kernel, 'OpenProcess', wt.HANDLE, [wt.DWORD, wt.BOOL, wt.DWORD])
    times = bind(kernel, 'GetProcessTimes', wt.BOOL,
                 [wt.HANDLE, ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME),
                  ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME)])
    handle = open_process(0x1000 | 0x100000, False, pid)  # QUERY_LIMITED_INFORMATION | SYNCHRONIZE
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        created, exited, kernel_time, user_time = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
        if not times(handle, ctypes.byref(created), ctypes.byref(exited), ctypes.byref(kernel_time), ctypes.byref(user_time)):
            raise ctypes.WinError(ctypes.get_last_error())
        started = (int(created.dwHighDateTime) << 32) | int(created.dwLowDateTime)
        if expected_started is not None and started != expected_started:
            raise RuntimeError('game PID creation identity changed')
        if job is not None:
            member = wt.BOOL()
            in_job = bind(kernel, 'IsProcessInJob', wt.BOOL, [wt.HANDLE, wt.HANDLE, ctypes.POINTER(wt.BOOL)])
            if not in_job(handle, job, ctypes.byref(member)) or not member.value:
                raise RuntimeError('game PID is not in the owned game job')
        check_process(kernel, handle)
        return kernel, handle, started
    except BaseException:
        close_job(kernel, handle)
        raise


def check_process(kernel, handle):
    wait = bind(kernel, 'WaitForSingleObject', wt.DWORD, [wt.HANDLE, wt.DWORD])
    if wait(handle, 0) != 258:  # WAIT_TIMEOUT: the pinned process is still alive
        raise RuntimeError('owned game process is no longer alive')


def supervise(args):
    if sys.platform != 'win32':
        raise RuntimeError('native Windows required')
    user, _ = api()
    if not 1 <= args.duration <= 200 or not 1 <= args.fps <= 30:
        raise ValueError('invalid bounded recording duration or frame rate')
    if (args.output.exists() or args.output.is_symlink() or args.output.suffix.lower() != '.mp4'
            or args.output.with_suffix('.timing.json').exists()
            or args.output.with_suffix('.timing.json').is_symlink() or not args.ffmpeg.is_file()):
        raise ValueError('new MP4 path and existing ffmpeg.exe required')
    if shutil.disk_usage(args.output.parent).free < FLOOR:
        raise RuntimeError('free-space floor + margin not met')
    process_kernel, process, _ = pin_process(args.pid, args.started)
    kernel = job = None
    child = None
    go = args.output.with_suffix('.capture-go')
    try:
        check_window(user, args.hwnd, args.pid)
        kernel, job = create_kill_job()
        # The gated capture body uses only stdlib. Avoid the venv redirector's
        # pre-gate child creation, so the real body/encoder inherit this Job.
        cmd = [sys._base_executable, '-B', str(Path(__file__).resolve()), 'child', '--pid', str(args.pid),
               '--hwnd', str(args.hwnd), '--started', str(args.started), '--ffmpeg', str(args.ffmpeg), '--output', str(args.output),
               '--duration', str(args.duration), '--fps', str(args.fps), '--stop', str(args.stop), '--go', str(go)]
        child = subprocess.Popen(cmd)
        try:
            assign_job(kernel, job, child)
        except Exception:
            child.terminate()
            child.wait(timeout=10)
            raise
        with go.open('xb') as f:
            f.write(b'owned job assigned\n')
        deadline = time.monotonic() + args.duration + 20
        while child.poll() is None:
            check_process(process_kernel, process)
            if time.monotonic() > deadline or shutil.disk_usage(args.output.parent).free < FLOOR:
                raise TimeoutError('capture deadline or disk floor')
            time.sleep(.1)
        return child.returncode
    finally:
        if job:
            close_job(kernel, job)  # closes owned child and its ffmpeg even on timeout/interruption
        if child and child.poll() is None:
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=10)
        close_job(process_kernel, process)


class BitmapInfoHeader(ctypes.Structure):
    _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG),
                ('biPlanes', wt.WORD), ('biBitCount', wt.WORD), ('biCompression', wt.DWORD),
                ('biSizeImage', wt.DWORD), ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
                ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]


class BitmapInfo(ctypes.Structure):
    _fields_ = [('header', BitmapInfoHeader), ('colours', wt.DWORD * 3)]


def write_clocked_frames(pipe, pixels, count, elapsed, fps, duration):
    """Hold the last obtained image across missed CFR slots; never speed up time.

    The duration cap bounds catch-up even if writes themselves are slow. A stuck
    pipe still belongs to the supervisor's bounded, kill-on-close capture Job.
    """
    target = min(math.ceil(duration * fps), math.ceil(max(0, elapsed) * fps))
    while count < target:
        pipe.write(pixels)
        count += 1
    return count


def finish_encoder(ffmpeg):
    """Finalize a partial MP4 before bounded escalation; never turn capture loss into success."""
    if ffmpeg is None or ffmpeg.poll() is not None:
        return
    try:
        if ffmpeg.stdin is not None:
            ffmpeg.stdin.close()
    except OSError:
        # The encoder may already have closed its pipe. It still needs reaping.
        pass
    try:
        ffmpeg.wait(timeout=15)
    except subprocess.TimeoutExpired:
        ffmpeg.terminate()
        try:
            ffmpeg.wait(timeout=10)
        except subprocess.TimeoutExpired:
            ffmpeg.kill()
            ffmpeg.wait(timeout=10)


def child_capture(args):
    user, gdi = api()
    process_kernel, process, _ = pin_process(args.pid, args.started)
    deadline = time.monotonic() + 10
    while not args.go.exists():
        if time.monotonic() > deadline:
            raise TimeoutError('supervisor did not assign capture job')
        time.sleep(.05)
    check_window(user, args.hwnd, args.pid)
    getdc = bind(user, 'GetDC', wt.HDC, [wt.HWND])
    releasedc = bind(user, 'ReleaseDC', ctypes.c_int, [wt.HWND, wt.HDC])
    memdc = bind(gdi, 'CreateCompatibleDC', wt.HDC, [wt.HDC])
    deletedc = bind(gdi, 'DeleteDC', wt.BOOL, [wt.HDC])
    dib = bind(gdi, 'CreateDIBSection', wt.HBITMAP,
               [wt.HDC, ctypes.POINTER(BitmapInfo), wt.UINT, ctypes.POINTER(ctypes.c_void_p), wt.HANDLE, wt.DWORD])
    select = bind(gdi, 'SelectObject', wt.HANDLE, [wt.HDC, wt.HANDLE])
    delete = bind(gdi, 'DeleteObject', wt.BOOL, [wt.HANDLE])
    printwindow = bind(user, 'PrintWindow', wt.BOOL, [wt.HWND, wt.HDC, wt.UINT])
    source, memory, bitmap, old, ffmpeg = None, None, None, None, None
    try:
        source = getdc(args.hwnd)
        memory = memdc(source)
        info, pixels = BitmapInfo(), ctypes.c_void_p()
        info.header.biSize = ctypes.sizeof(BitmapInfoHeader)
        info.header.biWidth, info.header.biHeight = 960, -540
        info.header.biPlanes, info.header.biBitCount = 1, 32
        if not source or not memory:
            raise ctypes.WinError(ctypes.get_last_error())
        bitmap = dib(memory, ctypes.byref(info), 0, ctypes.byref(pixels), None, 0)
        if not bitmap or not pixels.value:
            raise ctypes.WinError(ctypes.get_last_error())
        old = select(memory, bitmap)
        if not old or old == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        cmd = [str(args.ffmpeg), '-hide_banner', '-loglevel', 'error', '-nostdin', '-f', 'rawvideo',
               '-pix_fmt', 'bgr0', '-video_size', '960x540', '-framerate', str(args.fps), '-i', 'pipe:0',
               '-an', '-c:v', 'libx264', '-preset', 'veryfast', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
               '-fs', str(MAX_BYTES), '-n', str(args.output)]
        ffmpeg = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        start = time.monotonic()
        frame = 0
        previous = None
        samples = []
        while time.monotonic() - start < args.duration and not args.stop.exists():
            check_process(process_kernel, process)
            check_window(user, args.hwnd, args.pid)
            if ffmpeg.poll() is not None:
                raise RuntimeError('ffmpeg exited during capture')
            if not printwindow(args.hwnd, memory, 3):  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
                raise ctypes.WinError(ctypes.get_last_error())
            current = ctypes.string_at(pixels, 960 * 540 * 4)
            elapsed = time.monotonic() - start
            samples.append(elapsed)
            # Slots before this acquisition use the last image actually
            # available then. The first acquired image covers initial latency.
            frame = write_clocked_frames(ffmpeg.stdin, previous if previous is not None else current,
                                         frame, elapsed, args.fps, args.duration)
            previous = current
            wait = start + frame / args.fps - time.monotonic()
            if wait > 0:
                time.sleep(wait)
        capture_elapsed = time.monotonic() - start
        if previous is not None:
            frame = write_clocked_frames(ffmpeg.stdin, previous, frame, capture_elapsed,
                                         args.fps, args.duration)
        ffmpeg.stdin.close()
        code = ffmpeg.wait(timeout=15)
        if code or frame == 0 or not args.output.is_file() or args.output.stat().st_size == 0:
            raise RuntimeError('ffmpeg failed or produced no frames')
        with args.output.with_suffix('.timing.json').open('x', encoding='utf-8') as f:
            json.dump({'frame_policy': 'elapsed-monotonic CFR; hold last obtained image, no motion interpolation',
                       'nominal_fps': args.fps, 'encoded_frames': frame, 'timeline_seconds': frame / args.fps,
                       'capture_elapsed_seconds': capture_elapsed, 'capture_completed_seconds': samples,
                       'initial_latency_policy': 'first obtained image covers initial acquisition latency',
                       'visual_quality_assessed': False}, f, indent=2, allow_nan=False)
            f.write('\n')
        print('MP4 saved; PrintWindow pixels and visual quality remain unverified', flush=True)
        return 0
    finally:
        try:
            finish_encoder(ffmpeg)
        finally:
            if old and memory:
                select(memory, old)
            if bitmap:
                delete(bitmap)
            if memory:
                deletedc(memory)
            if source:
                releasedc(args.hwnd, source)
            close_job(process_kernel, process)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('supervise', 'child'))
    p.add_argument('--pid', type=int, required=True)
    p.add_argument('--started', type=int, required=True, help='pinned Windows process creation FILETIME')
    p.add_argument('--hwnd', type=int, required=True)
    p.add_argument('--ffmpeg', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--duration', type=int, required=True)
    p.add_argument('--fps', type=int, default=20)
    p.add_argument('--stop', type=Path, required=True)
    p.add_argument('--go', type=Path)
    args = p.parse_args()
    if args.action == 'child' and args.go is None:
        p.error('child needs --go')
    return supervise(args) if args.action == 'supervise' else child_capture(args)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(type(exc).__name__ + ': ' + str(exc), file=sys.stderr)
        raise SystemExit(2)
