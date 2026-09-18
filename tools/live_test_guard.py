"""Serialize visible DKC1 verification and reject overlapping game windows."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import threading


def game_windows():
    if os.name != 'nt':
        return []
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    result = []

    @callback_type
    def visit(window, unused):
        name = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(window, name, len(name))
        if name.value == 'DKC1RecompWindow':
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(window, ctypes.byref(pid))
            result.append(pid.value)
        return True

    user.EnumWindows(visit, 0)
    return result


class LiveTestGuard:
    def __init__(self, output):
        self.output = Path(output)
        self.stop = threading.Event()
        self.foreign = set()
        self.thread = None
        self.handle = None

    def __enter__(self):
        if os.name == 'nt':
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
            self.kernel.CreateMutexW.restype = wintypes.HANDLE
            self.kernel.ReleaseMutex.argtypes = [wintypes.HANDLE]
            self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            self.handle = self.kernel.CreateMutexW(None, True, 'Local\\DKC1LiveVerification')
            if not self.handle or ctypes.get_last_error() == 183:
                if self.handle:
                    self.kernel.CloseHandle(self.handle)
                self.handle = None
                raise RuntimeError('Another visible verification owns the live-test mutex')
        existing = game_windows()
        if existing:
            self.__exit__(None, None, None)
            raise RuntimeError(f'Existing DKC1 window(s), refusing a concurrent run: {existing}')
        return self

    def track(self, pid):
        def monitor():
            while not self.stop.wait(.5):
                self.foreign.update(p for p in game_windows() if p != pid)
        self.thread = threading.Thread(target=monitor, daemon=True)
        self.thread.start()

    def __exit__(self, kind, error, traceback):
        self.stop.set()
        if self.thread:
            self.thread.join()
        if self.handle:
            self.kernel.ReleaseMutex(self.handle)
            self.kernel.CloseHandle(self.handle)
            self.handle = None
        if self.foreign:
            self.output.mkdir(parents=True, exist_ok=True)
            (self.output/'concurrent-processes.json').write_text(json.dumps({
                'accepted': False, 'foreign_game_pids': sorted(self.foreign),
                'reason': 'Another game window overlapped this run; normal root restoration and close retained.'}, indent=2))
            if kind is None:
                raise RuntimeError(f'Concurrent game windows invalidated timing: {sorted(self.foreign)}')
