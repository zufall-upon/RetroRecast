"""Trial-029 file-backed single-slot protocol; sequentially consistent atomics."""
import ctypes
import mmap
import os
from pathlib import Path
import struct
import sys
import time

WIDTH, HEIGHT = 960, 540
PIXELS = WIDTH * HEIGHT * 4
HEADER = 64
SIZE = HEADER + 2 * PIXELS
FREE, REQUEST, BUSY, RESPONSE, ERROR = range(5)
VERSION = 1

if sys.platform == 'win32':
    # The integer Interlocked functions are compiler intrinsics on Windows x64,
    # not kernel32 exports. Load our tiny intrinsic wrapper by absolute path;
    # never search the current directory for this or any dependency.
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError('Windows slot worker requires 64-bit Python')
    _native = ctypes.WinDLL(str(Path(__file__).resolve().parent / 'windows' / 'slot_atomic_x64.dll'),
                           winmode=0x900)
    _cas = _native.kf1_slot_compare_exchange
    _cas.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.c_int32]
    _cas.restype = ctypes.c_int32
    _exchange = _native.kf1_slot_exchange
    _exchange.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32]
    _exchange.restype = ctypes.c_int32
else:
    _atomic = ctypes.CDLL('libatomic.so.1')
    _load = _atomic.__atomic_load_4
    _load.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _load.restype = ctypes.c_uint32
    _store = _atomic.__atomic_store_4
    _store.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int]
    _store.restype = None
    _cas = _atomic.__atomic_compare_exchange_4
    _cas.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_bool,
                     ctypes.c_int, ctypes.c_int]
    _cas.restype = ctypes.c_bool


def create(path):
    flags = os.O_RDWR | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags, 0o600)
    try:
        os.ftruncate(fd, SIZE)
        with mmap.mmap(fd, SIZE, access=mmap.ACCESS_WRITE) as m:
            struct.pack_into('<8I', m, 0, FREE, VERSION, 0, 0, 0, 0, 0, 0)
            m.flush()
    finally:
        os.close(fd)


class Slot:
    def __init__(self, path):
        self.fd = os.open(Path(path), os.O_RDWR | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0))
        try:
            if os.fstat(self.fd).st_size != SIZE:
                raise ValueError('shared memory size mismatch')
            self.map = mmap.mmap(self.fd, SIZE, access=mmap.ACCESS_WRITE)
        except BaseException:
            os.close(self.fd)
            raise
        self._word = ctypes.c_uint32.from_buffer(self.map, 0)
        self._address = ctypes.addressof(self._word)
        if struct.unpack_from('<I', self.map, 4)[0] != VERSION:
            self.close()
            raise ValueError('shared memory version mismatch')

    def close(self):
        del self._word
        self.map.close()
        os.close(self.fd)

    def state(self):
        if sys.platform == 'win32':
            return _cas(ctypes.cast(self._address, ctypes.POINTER(ctypes.c_int32)), 0, 0)
        return _load(self._address, 5)

    def publish(self, value):
        if sys.platform == 'win32':
            _exchange(ctypes.cast(self._address, ctypes.POINTER(ctypes.c_int32)), value)
        else:
            _store(self._address, value, 5)

    def receive(self, expected_sequence):
        if sys.platform == 'win32':
            claimed = _cas(ctypes.cast(self._address, ctypes.POINTER(ctypes.c_int32)), BUSY, REQUEST) == REQUEST
        else:
            expected = ctypes.c_uint32(REQUEST)
            claimed = bool(_cas(self._address, ctypes.byref(expected), BUSY, False, 5, 5))
        if not claimed:
            return None
        _, version, sequence, length, _, _, _, diagnostic = struct.unpack_from('<8I', self.map)
        if version != VERSION or sequence != expected_sequence or length != PIXELS:
            self.publish(ERROR)
            raise ValueError('request sequence, version or complete payload mismatch')
        return sequence, bytes(self.map[HEADER:HEADER + PIXELS]), diagnostic

    def request(self, sequence, payload, diagnostic=0):
        if self.state() != FREE or len(payload) != PIXELS or sequence <= 0:
            raise ValueError('request ownership or payload mismatch')
        self.map[HEADER:HEADER + PIXELS] = payload
        struct.pack_into('<6I', self.map, 8, sequence, len(payload), 0, 0, 0, diagnostic)
        self.publish(REQUEST)

    def response(self, sequence):
        if self.state() != RESPONSE:
            raise ValueError('response not ready')
        version, reply, length = struct.unpack_from('<I8xII', self.map, 4)
        if version != VERSION or reply != sequence or length != PIXELS:
            self.publish(ERROR)
            raise ValueError('response sequence, version or complete payload mismatch')
        return bytes(self.map[HEADER + PIXELS:HEADER + 2 * PIXELS])

    def respond(self, sequence, payload):
        if self.state() != BUSY or len(payload) != PIXELS:
            self.publish(ERROR)
            raise ValueError('response ownership or payload mismatch')
        self.map[HEADER + PIXELS:HEADER + 2 * PIXELS] = payload
        struct.pack_into('<II', self.map, 16, sequence, len(payload))
        self.publish(RESPONSE)


def wait_for(slot, target, timeout, stop=lambda: False):
    deadline = time.monotonic() + timeout
    while not stop():
        state = slot.state()
        if state == target:
            return True
        if state == ERROR:
            raise RuntimeError('peer reported IPC failure')
        if time.monotonic() >= deadline:
            raise TimeoutError('shared-slot deadline')
        time.sleep(.001)
    return False
