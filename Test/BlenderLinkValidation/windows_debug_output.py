"""Read this run's OutputDebugString messages through the standard DBWIN transport."""
import ctypes
import struct
import subprocess
import threading
from ctypes import wintypes

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.CreateFileMappingW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR]
kernel.CreateFileMappingW.restype = wintypes.HANDLE
kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
kernel.CreateEventW.restype = wintypes.HANDLE
kernel.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]
kernel.MapViewOfFile.restype = ctypes.c_void_p
kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel.SetEvent.argtypes = [wintypes.HANDLE]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]

def start_editor(command, cwd, stdout, env, output):
    handles = []
    view = None
    try:
        for name in ("DBWIN_BUFFER_READY", "DBWIN_DATA_READY"):
            handle = kernel.CreateEventW(None, False, False, name)
            if not handle:
                raise ctypes.WinError(ctypes.get_last_error())
            handles.append(handle)
            if ctypes.get_last_error() == 183:
                raise RuntimeError("An existing DBWIN collector is active; refusing to interfere")
        mapping = kernel.CreateFileMappingW(wintypes.HANDLE(-1), None, 4, 0, 4096, "DBWIN_BUFFER")
        if not mapping:
            raise ctypes.WinError(ctypes.get_last_error())
        handles.append(mapping)
        if ctypes.get_last_error() == 183:
            raise RuntimeError("Existing DBWIN buffer detected")
        view = kernel.MapViewOfFile(mapping, 4, 0, 0, 4096)
        if not view:
            raise ctypes.WinError(ctypes.get_last_error())
        ready, data_event = handles[:2]
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=subprocess.STDOUT, env=env,
                                   startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    except BaseException:
        if view:
            kernel.UnmapViewOfFile(view)
        for handle in reversed(handles):
            kernel.CloseHandle(handle)
        raise

    def collect():
        try:
            with output.open("w", encoding="utf-8", buffering=1) as log:
                while process.poll() is None:
                    kernel.SetEvent(ready)
                    if kernel.WaitForSingleObject(data_event, 250) != 0:
                        continue
                    raw = ctypes.string_at(view, 4096)
                    if struct.unpack_from("<I", raw)[0] == process.pid:
                        log.write(raw[4:].split(b"\0", 1)[0].decode("mbcs", errors="replace"))
        finally:
            kernel.UnmapViewOfFile(view)
            for handle in reversed(handles):
                kernel.CloseHandle(handle)

    threading.Thread(target=collect, daemon=True).start()
    return process
