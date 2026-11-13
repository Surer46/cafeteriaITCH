#!/usr/bin/env python3
"""
diagnose_startup.py - Diagnóstico seguro y acotado del arranque de app_desktop.exe.
Cumple estrictamente los requisitos:
1. Declaración de argtypes y restype para todas las APIs Win32.
2. Límite global de 30 segundos y límite de 150 eventos de depuración.
3. Tratamiento de excepciones con DBG_EXCEPTION_NOT_HANDLED.
4. Mapeo de direcciones de excepción al módulo DLL correspondiente.
5. Detección de bucles (misma excepción/dirección consecutiva) y detención inmediata.
6. Limpieza garantizada únicamente del proceso creado y sus handles.
7. Base de datos SQLite aislada en directorio temporal.
"""

import ctypes
from ctypes import wintypes
import time
import os
import sys
import tempfile
import shutil

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# -----------------------------------------------------------------------------
# 1. Declaración estricta de funciones y tipos Win32
# -----------------------------------------------------------------------------
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

DEBUG_ONLY_THIS_PROCESS = 0x00000002
CREATE_SUSPENDED = 0x00000004

EXCEPTION_DEBUG_EVENT = 1
CREATE_THREAD_DEBUG_EVENT = 2
CREATE_PROCESS_DEBUG_EVENT = 3
EXIT_THREAD_DEBUG_EVENT = 4
EXIT_PROCESS_DEBUG_EVENT = 5
LOAD_DLL_DEBUG_EVENT = 6
UNLOAD_DLL_DEBUG_EVENT = 7
OUTPUT_DEBUG_STRING_EVENT = 8
RIP_EVENT = 9

DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001

class STARTUPINFOW(ctypes.Structure):
    _fields_ = [
        ('cb', wintypes.DWORD), ('lpReserved', wintypes.LPWSTR),
        ('lpDesktop', wintypes.LPWSTR), ('lpTitle', wintypes.LPWSTR),
        ('dwX', wintypes.DWORD), ('dwY', wintypes.DWORD),
        ('dwXSize', wintypes.DWORD), ('dwYSize', wintypes.DWORD),
        ('dwXCountChars', wintypes.DWORD), ('dwYCountChars', wintypes.DWORD),
        ('dwFillAttribute', wintypes.DWORD), ('dwFlags', wintypes.DWORD),
        ('wShowWindow', wintypes.WORD), ('cbReserved2', wintypes.WORD),
        ('lpReserved2', ctypes.c_void_p), ('hStdInput', wintypes.HANDLE),
        ('hStdOutput', wintypes.HANDLE), ('hStdError', wintypes.HANDLE)
    ]

class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [
        ('hProcess', wintypes.HANDLE), ('hThread', wintypes.HANDLE),
        ('dwProcessId', wintypes.DWORD), ('dwThreadId', wintypes.DWORD)
    ]

class LOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('hFile', wintypes.HANDLE), ('lpBaseOfDll', ctypes.c_void_p),
        ('dwDebugInfoFileOffset', wintypes.DWORD), ('nDebugInfoSize', wintypes.DWORD),
        ('lpImageName', ctypes.c_void_p), ('fUnicode', wintypes.WORD)
    ]

class UNLOAD_DLL_DEBUG_INFO(ctypes.Structure):
    _fields_ = [('lpBaseOfDll', ctypes.c_void_p)]

class CREATE_PROCESS_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('hFile', wintypes.HANDLE), ('hProcess', wintypes.HANDLE),
        ('hThread', wintypes.HANDLE), ('lpBaseOfImage', ctypes.c_void_p),
        ('dwDebugInfoFileOffset', wintypes.DWORD), ('nDebugInfoSize', wintypes.DWORD),
        ('lpThreadLocalBase', ctypes.c_void_p), ('lpStartAddress', ctypes.c_void_p),
        ('lpImageName', ctypes.c_void_p), ('fUnicode', wintypes.WORD)
    ]

class EXCEPTION_RECORD(ctypes.Structure):
    pass
EXCEPTION_RECORD._fields_ = [
    ('ExceptionCode', wintypes.DWORD), ('ExceptionFlags', wintypes.DWORD),
    ('ExceptionRecord', ctypes.POINTER(EXCEPTION_RECORD)),
    ('ExceptionAddress', ctypes.c_void_p), ('NumberParameters', wintypes.DWORD),
    ('ExceptionInformation', ctypes.c_void_p * 15)
]

class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ('ExceptionRecord', EXCEPTION_RECORD),
        ('dwFirstChance', wintypes.DWORD)
    ]

class DEBUG_EVENT_UNION(ctypes.Union):
    _fields_ = [
        ('Exception', EXCEPTION_DEBUG_INFO),
        ('CreateProcessInfo', CREATE_PROCESS_DEBUG_INFO),
        ('LoadDll', LOAD_DLL_DEBUG_INFO),
        ('UnloadDll', UNLOAD_DLL_DEBUG_INFO),
    ]

class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [
        ('dwDebugEventCode', wintypes.DWORD),
        ('dwProcessId', wintypes.DWORD),
        ('dwThreadId', wintypes.DWORD),
        ('u', DEBUG_EVENT_UNION)
    ]

# Firmas de funciones kernel32
kernel32.CreateProcessW.argtypes = [
    wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
    wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
    ctypes.POINTER(STARTUPINFOW), ctypes.POINTER(PROCESS_INFORMATION)
]
kernel32.CreateProcessW.restype = wintypes.BOOL

kernel32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wintypes.DWORD]
kernel32.WaitForDebugEvent.restype = wintypes.BOOL

kernel32.ContinueDebugEvent.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]
kernel32.ContinueDebugEvent.restype = wintypes.BOOL

kernel32.DebugActiveProcessStop.argtypes = [wintypes.DWORD]
kernel32.DebugActiveProcessStop.restype = wintypes.BOOL

kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel32.TerminateProcess.restype = wintypes.BOOL

kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

kernel32.GetFinalPathNameByHandleW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD]
kernel32.GetFinalPathNameByHandleW.restype = wintypes.DWORD

def resolve_module_path(hFile, base_addr):
    if hFile and hFile != wintypes.HANDLE(0).value and hFile != -1:
        buf = ctypes.create_unicode_buffer(1024)
        n = kernel32.GetFinalPathNameByHandleW(hFile, buf, 1024, 0)
        if n > 0:
            p = buf.value
            if p.startswith("\\\\?\\"):
                p = p[4:]
            return p
    return f"<modulo_en_{hex(base_addr if base_addr else 0)}>"

def main():
    config = "release"
    if len(sys.argv) > 1 and sys.argv[1].lower() == "debug":
        config = "debug"

    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    build_dir = os.path.join(root_dir, f"build_{config}")
    exe_path = os.path.join(build_dir, "app_desktop.exe")

    if not os.path.exists(exe_path):
        print(f"[ERROR] Ejecutable no encontrado: {exe_path}", flush=True)
        sys.exit(1)

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_diag_")
    print(f"=== DIAGNÓSTICO DE ARRANQUE ({config.upper()}) ===", flush=True)
    print(f"Ejecutable: {exe_path}", flush=True)
    print(f"Directorio temporal SQLite: {temp_dir}", flush=True)

    env_path = build_dir + ";" + os.environ.get("PATH", "")
    os.environ["PATH"] = env_path
    os.environ["CAFETERIA_DATA_DIR"] = temp_dir

    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()

    created_proc_handle = None
    created_thread_handle = None
    created_pid = None

    loaded_modules = {} # base_addr -> path

    try:
        ok = kernel32.CreateProcessW(
            exe_path, None, None, None, False,
            DEBUG_ONLY_THIS_PROCESS, None, build_dir,
            ctypes.byref(si), ctypes.byref(pi)
        )
        if not ok:
            err = ctypes.get_last_error()
            print(f"[ERROR] CreateProcessW falló con código: {err}", flush=True)
            return

        created_proc_handle = pi.hProcess
        created_thread_handle = pi.hThread
        created_pid = pi.dwProcessId
        print(f"[OK] Proceso creado para depuración. PID: {created_pid}", flush=True)

        evt = DEBUG_EVENT()
        start_time = time.time()
        max_duration = 30.0  # Límite global de 30s
        max_events = 150     # Límite de eventos
        event_count = 0

        last_exception_key = None
        consecutive_same_exception = 0

        while True:
            elapsed = time.time() - start_time
            if elapsed > max_duration:
                print(f"[LÍMITE] Tiempo máximo alcanzado ({max_duration}s). Deteniendo diagnóstico.", flush=True)
                break

            if event_count >= max_events:
                print(f"[LÍMITE] Límite de eventos alcanzado ({max_events}). Deteniendo diagnóstico.", flush=True)
                break

            # Esperar evento con timeout de 1000ms
            if not kernel32.WaitForDebugEvent(ctypes.byref(evt), 1000):
                continue

            event_count += 1
            code = evt.dwDebugEventCode
            continue_status = DBG_CONTINUE

            if code == CREATE_PROCESS_DEBUG_EVENT:
                info = evt.u.CreateProcessInfo
                mod_path = resolve_module_path(info.hFile, info.lpBaseOfImage)
                loaded_modules[info.lpBaseOfImage] = mod_path
                print(f"[PROCESO] Base: {hex(info.lpBaseOfImage if info.lpBaseOfImage else 0)} -> {mod_path}", flush=True)
                if info.hFile: kernel32.CloseHandle(info.hFile)

            elif code == LOAD_DLL_DEBUG_EVENT:
                info = evt.u.LoadDll
                mod_path = resolve_module_path(info.hFile, info.lpBaseOfDll)
                loaded_modules[info.lpBaseOfDll] = mod_path
                print(f"[DLL] {hex(info.lpBaseOfDll if info.lpBaseOfDll else 0)} : {mod_path}", flush=True)
                if info.hFile: kernel32.CloseHandle(info.hFile)

            elif code == UNLOAD_DLL_DEBUG_EVENT:
                info = evt.u.UnloadDll
                loaded_modules.pop(info.lpBaseOfDll, None)

            elif code == EXCEPTION_DEBUG_EVENT:
                info = evt.u.Exception
                rec = info.ExceptionRecord
                first_chance = (info.dwFirstChance != 0)
                exc_code = rec.ExceptionCode
                exc_addr = rec.ExceptionAddress if rec.ExceptionAddress else 0

                # Identificar módulo correspondiente por dirección
                closest_mod = "Desconocido"
                closest_base = 0
                for b, m in loaded_modules.items():
                    if b and exc_addr >= b and b > closest_base:
                        closest_base = b
                        closest_mod = m

                print(f"[EXCEPCIÓN] Código: {hex(exc_code)} | Dirección: {hex(exc_addr)} | FirstChance: {first_chance} | Módulo: {closest_mod}", flush=True)
                for i in range(min(rec.NumberParameters, 4)):
                    print(f"   Param[{i}]: {hex(rec.ExceptionInformation[i] if rec.ExceptionInformation[i] else 0)}", flush=True)

                current_key = (exc_code, exc_addr)
                if current_key == last_exception_key:
                    consecutive_same_exception += 1
                else:
                    last_exception_key = current_key
                    consecutive_same_exception = 1

                if consecutive_same_exception >= 3:
                    print(f"[BUCLE DETECTADO] Excepción idéntica repetida {consecutive_same_exception} veces consecutivas. Deteniendo depuración para evitar bucle.", flush=True)
                    continue_status = DBG_EXCEPTION_NOT_HANDLED
                    kernel32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, continue_status)
                    break

                # No usar DBG_CONTINUE para excepciones no resueltas
                continue_status = DBG_EXCEPTION_NOT_HANDLED

            elif code == EXIT_PROCESS_DEBUG_EVENT:
                print(f"[SALIDA] El proceso terminó normalmente o por excepción fatal.", flush=True)
                kernel32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, DBG_CONTINUE)
                break

            kernel32.ContinueDebugEvent(evt.dwProcessId, evt.dwThreadId, continue_status)

    finally:
        # 6. Limpieza garantizada únicamente del proceso creado y sus handles
        if created_pid:
            kernel32.DebugActiveProcessStop(created_pid)
        if created_proc_handle:
            kernel32.TerminateProcess(created_proc_handle, 1)
            kernel32.CloseHandle(created_proc_handle)
        if created_thread_handle:
            kernel32.CloseHandle(created_thread_handle)

        if os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
                print(f"[OK] Directorio temporal SQLite limpio: {temp_dir}", flush=True)
            except Exception as e:
                print(f"[AVISO] No se pudo limpiar temp_dir: {e}", flush=True)

if __name__ == '__main__':
    main()
