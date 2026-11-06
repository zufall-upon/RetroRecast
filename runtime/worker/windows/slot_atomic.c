/* SPDX-License-Identifier: GPL-3.0-or-later
 * Private KF1 stage addition, 2026-11-04. See ../../licenses/bridge-LICENSE.txt.
 * A compiler-intrinsic wrapper, not a new IPC protocol or D3D9 adapter.
 */
#include <windows.h>

#if !defined(__x86_64__) && !defined(_M_X64)
#error This helper is for the Windows x64 Python worker only
#endif
_Static_assert(sizeof(LONG) == 4, "slot state must remain 32 bits");

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

__declspec(dllexport) LONG kf1_slot_compare_exchange(volatile LONG *destination,
                                                  LONG exchange, LONG comparand) {
    return InterlockedCompareExchange(destination, exchange, comparand);
}

__declspec(dllexport) LONG kf1_slot_exchange(volatile LONG *destination, LONG value) {
    return InterlockedExchange(destination, value);
}
