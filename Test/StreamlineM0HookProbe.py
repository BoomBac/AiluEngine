"""Windows x64 debugger for the SDK 2.14.1 production Present path; only launches its own Editor.

Usage: python StreamlineM0HookProbe.py EDITOR PROJECT OUTPUT SDK_ROOT [EXIT_AFTER_FRAMES]
Software breakpoints are process-local; the owned process is terminated on timeout/error.
The signed production slHookPresent wrapper has one direct call to presentCommon. Its wrapper
instruction sequence also matches the SDK development build whose matching PDB names the callee.
"""
import ctypes as ct
from ctypes import wintypes as wt
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

editor, project, output, sdk_root, *frame_limit = sys.argv[1:]
output = Path(output).resolve()
output.mkdir(parents=True, exist_ok=True)
kernel = ct.WinDLL('kernel32', use_last_error=True)
kernel.GetCurrentProcess.restype = wt.HANDLE
kernel.OpenThread.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel.OpenThread.restype = wt.HANDLE
kernel.CloseHandle.argtypes = [wt.HANDLE]
kernel.ReadProcessMemory.argtypes = [wt.HANDLE, ct.c_void_p, ct.c_void_p, ct.c_size_t, ct.c_void_p]
kernel.WriteProcessMemory.argtypes = kernel.ReadProcessMemory.argtypes
kernel.FlushInstructionCache.argtypes = [wt.HANDLE, ct.c_void_p, ct.c_size_t]
kernel.GetThreadContext.argtypes = [wt.HANDLE, ct.c_void_p]
kernel.SetThreadContext.argtypes = [wt.HANDLE, ct.c_void_p]
kernel.WaitForDebugEvent.argtypes = [ct.c_void_p, wt.DWORD]
kernel.ContinueDebugEvent.argtypes = [wt.DWORD, wt.DWORD, wt.DWORD]
kernel.GetFinalPathNameByHandleW.argtypes = [wt.HANDLE, wt.LPWSTR, wt.DWORD, wt.DWORD]
kernel.DebugActiveProcess.argtypes = [wt.DWORD]
psapi = ct.WinDLL('psapi', use_last_error=True)
psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HANDLE, wt.LPWSTR, wt.DWORD]


def Check(result):
    if not result:
        raise ct.WinError(ct.get_last_error())
    return result


def Symbols(dll_path, symbol_path, mask):
    dbg = ct.WinDLL('C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/dbghelp.dll', use_last_error=True)
    handle = kernel.GetCurrentProcess()
    dbg.SymInitializeW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.BOOL]
    dbg.SymCleanup.argtypes = [wt.HANDLE]
    dbg.SymLoadModuleExW.argtypes = [wt.HANDLE, wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR,
                                   ct.c_ulonglong, wt.DWORD, ct.c_void_p, wt.DWORD]
    dbg.SymLoadModuleExW.restype = ct.c_ulonglong
    callback_type = ct.WINFUNCTYPE(wt.BOOL, ct.c_void_p, wt.ULONG, ct.c_void_p)
    found = {}

    def Collect(info, size, context):
        name = ct.string_at(info + 84).decode()
        found[name] = ct.c_ulonglong.from_address(info + 56).value - 0x10000000
        return True

    callback = callback_type(Collect)
    dbg.SymEnumSymbols.argtypes = [wt.HANDLE, ct.c_ulonglong, wt.LPCSTR, callback_type, ct.c_void_p]
    Check(dbg.SymInitializeW(handle, str(symbol_path), False))
    try:
        base = Check(dbg.SymLoadModuleExW(handle, None, str(dll_path), None, 0x10000000, 0, None, 0))
        Check(dbg.SymEnumSymbols(handle, base, mask.encode(), callback, None))
        return found
    finally:
        dbg.SymCleanup(handle)


def CommonRva(dll_path):
    module = ct.WinDLL(str(dll_path))
    function = module.slGetPluginFunction
    function.argtypes = [ct.c_char_p]
    function.restype = ct.c_void_p
    hook = function(b'slHookPresent')
    if not hook:
        raise RuntimeError('SDK slHookPresent is missing')
    code = ct.string_at(hook, 37)
    prefix = bytes.fromhex('4883ec480f57c0f30f7f44242048c744243000000000418bc8e8')
    if not code.startswith(prefix) or code[30:37] != bytes.fromhex('9033c04883c448'):
        raise RuntimeError('Unsupported SDK wrapper instructions; do not guess a breakpoint address')
    target = hook + 30 + struct.unpack_from('<i', code, 26)[0]
    return target - module._handle


sdk_root = Path(sdk_root)
development_dll = sdk_root / 'bin/x64/development/sl.common.dll'
development_symbols = Symbols(development_dll, sdk_root / 'symbols', 'sl::presentCommon')
if development_symbols.get('sl::presentCommon') != CommonRva(development_dll):
    raise RuntimeError('SDK development symbols do not confirm the wrapper callee')
common_rva = CommonRva(Path(editor).parent / 'Streamline/sl.common.dll')
host_symbols = Symbols(Path(editor).parent / 'Engine_d.dll',
                       Path(editor).parent, '*D3DSwapchainTexture::Present')
host_name = 'Ailu::RHI::DX12::D3DSwapchainTexture::Present'
if host_name not in host_symbols:
    raise RuntimeError('Engine Present symbol is missing')
(output / 'symbol-evidence.json').write_text(json.dumps({
    'sdk_development_present_common_rva': development_symbols['sl::presentCommon'],
    'production_present_common_rva': common_rva, 'host_present_rva': host_symbols[host_name]}, indent=2))

startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
arguments = [editor, project]
if frame_limit:
    arguments += ['--exit-after-frames', frame_limit[0]]
process = subprocess.Popen(arguments, cwd=output, startupinfo=startup)
handle = wt.HANDLE(int(process._handle))
breakpoints = {}
stepping = {}
threads = {}
hits = []
counts = {'host_present': 0, 'present_common': 0}
event = ct.create_string_buffer(176)  # x64 DEBUG_EVENT (16-byte header + 160-byte union).
started = time.monotonic()
exited = False
debug_log = (output / 'debug-output.txt').open('w', encoding='utf-8')


def Read(address, size):
    data = ct.create_string_buffer(size)
    Check(kernel.ReadProcessMemory(handle, address, data, size, None))
    return data.raw


def Write(address, data):
    Check(kernel.WriteProcessMemory(handle, address, data, len(data), None))
    Check(kernel.FlushInstructionCache(handle, address, len(data)))


def Context(thread_id):
    if thread_id not in threads:
        threads[thread_id] = Check(kernel.OpenThread(0x1A, False, thread_id))
    storage = ct.create_string_buffer(1248)
    address = (ct.addressof(storage) + 15) & ~15
    ct.c_uint32.from_address(address + 48).value = 0x100001  # CONTEXT_AMD64 | CONTEXT_CONTROL.
    Check(kernel.GetThreadContext(threads[thread_id], address))
    return storage, address


try:
    session_path = Path(project).parent / '.ailu/editor_session.json'
    while time.monotonic() - started < 120:
        if process.poll() is not None:
            raise RuntimeError('Owned Editor exited before automation initialization')
        try:
            if json.loads(session_path.read_text())['pid'] == process.pid:
                break
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(0.05)
    else:
        raise RuntimeError('Owned Editor did not initialize within 120 seconds')
    Check(kernel.DebugActiveProcess(process.pid))
    while time.monotonic() - started < 300:
        if not kernel.WaitForDebugEvent(event, 100):
            if ct.get_last_error() == 121:
                continue
            raise ct.WinError(ct.get_last_error())
        code, process_id, thread_id = struct.unpack_from('<III', event.raw)
        status = 0x10002  # DBG_CONTINUE.
        if code == 6:  # LOAD_DLL_DEBUG_EVENT.
            file_handle, base = struct.unpack_from('<QQ', event.raw, 16)
            name = ct.create_unicode_buffer(32768)
            path_found = file_handle and kernel.GetFinalPathNameByHandleW(file_handle, name, len(name), 0)
            if not path_found:
                path_found = psapi.GetModuleFileNameExW(handle, wt.HANDLE(base), name, len(name))
            if path_found:
                module_name = Path(name.value).name.lower()
                target = None
                if module_name == 'sl.common.dll':
                    target = ('present_common', base + common_rva)
                elif module_name == 'engine_d.dll':
                    target = ('host_present', base + host_symbols[host_name])
                if target:
                    label, address = target
                    breakpoints[address] = (label, Read(address, 1))
                    Write(address, b'\xcc')
                    if len(breakpoints) == 2:
                        (output / 'probe_ready.json').write_text(json.dumps({'pid': process.pid}))
            if file_handle:
                kernel.CloseHandle(file_handle)
        elif code == 1:  # EXCEPTION_DEBUG_EVENT.
            exception_code = struct.unpack_from('<I', event.raw, 16)[0]
            address = struct.unpack_from('<Q', event.raw, 32)[0]
            if exception_code == 0x80000003 and address in breakpoints:
                label, original = breakpoints[address]
                counts[label] += 1
                try:
                    phase = json.loads((output / 'probe_phase.json').read_text())['phase']
                except (OSError, ValueError, KeyError):
                    phase = 'startup'
                hits.append({'label': label, 'thread': thread_id, 'phase': phase,
                             'seconds': time.monotonic() - started, **counts})
                Write(address, original)
                storage, context = Context(thread_id)
                ct.c_ulonglong.from_address(context + 248).value = address  # Rip.
                ct.c_uint32.from_address(context + 68).value |= 0x100  # EFlags: single step.
                Check(kernel.SetThreadContext(threads[thread_id], context))
                stepping[thread_id] = address
            elif exception_code == 0x80000004 and thread_id in stepping:
                address = stepping.pop(thread_id)
                Write(address, b'\xcc')
                storage, context = Context(thread_id)
                ct.c_uint32.from_address(context + 68).value &= ~0x100
                Check(kernel.SetThreadContext(threads[thread_id], context))
            elif exception_code != 0x80000003:
                status = 0x80010001  # DBG_EXCEPTION_NOT_HANDLED.
        elif code == 8:  # OUTPUT_DEBUG_STRING_EVENT (includes DXGI live-object report).
            address, unicode, length = struct.unpack_from('<QHH', event.raw, 16)
            if address and length:
                data = Read(address, length * (2 if unicode else 1))
                debug_log.write(data.decode('utf-16-le' if unicode else 'mbcs', errors='replace').rstrip('\0') + '\n')
                debug_log.flush()
        elif code == 5:  # EXIT_PROCESS_DEBUG_EVENT.
            exited = True
            exit_code = struct.unpack_from('<I', event.raw, 16)[0]
        Check(kernel.ContinueDebugEvent(process_id, thread_id, status))
        if exited:
            break
    if not exited:
        raise RuntimeError('Owned Editor exceeded 300-second probe timeout')
finally:
    if not exited:
        process.kill()
    debug_log.close()
    for thread in threads.values():
        kernel.CloseHandle(thread)
    (output / 'hook-hits.json').write_text(json.dumps(hits, indent=2))
    (output / 'hook-summary.json').write_text(json.dumps({'counts': counts, 'exited': exited,
        'exit_code': exit_code if exited else None, 'breakpoints_installed': len(breakpoints)}, indent=2))

print(json.dumps({'exit_code': exit_code, **counts}))
if exit_code != 0 or counts['host_present'] == 0 or counts['host_present'] != counts['present_common']:
    raise SystemExit(1)
