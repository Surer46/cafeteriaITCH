#!/usr/bin/env python3
"""
test_f7_ingredients_panel.py - Prueba GUI real para Bloque F7 (Inventario > Ingredientes).

Verifica Release y Debug con ejecutables nativos y bases SQLite aisladas:
- Escalado de pantalla exacto al 100% (96 DPI) antes de correr pruebas GUI.
- Estado vacio inicial invitando a crear el primer ingrediente.
- Alta de Peso (mg), Volumen (ml) y Piezas (pz), validando valores exactos en SQLite (500 g -> 500000).
- Edicion de nombre y umbral con persistencia tras cerrar y reabrir.
- Rechazo de nombre vacio, desbordamiento y nombre duplicado mostrando el mensaje del servicio.
- Formateo de existencias sembradas por SQL (1.5 kg, 250 g, 2.5 L, 7 pz, 450 mg, Sin aviso) y estados
  Poco inventario / Suficiente segun get_stock_alerts().
- Usuario Cajero en solo lectura, controles deshabilitados e intento de guardado sin alterar SQLite.
- Ausencia de controles para modificar existencias y de jerga tecnica (MASS, VOLUME, COUNT, IDs, mg salvo < 1000 mg).
- Medicion geometrica Win32 en Estandar y Touch a 1200x720 y 960x600: encabezados completos, sin WS_HSCROLL,
  holgura <= 8 px, sin recortes ni solapamientos, campos centrados (<= 3 px), alturas Touch (>= 48 px campos/botones,
  >= 40 px filas de lista) y Guardar visible sin desplazarse.
"""

import ctypes
from ctypes import wintypes
import os
import re
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RELEASE_EXE = os.path.join(REPO_ROOT, "build_release", "app_desktop.exe")
DEBUG_EXE = os.path.join(REPO_ROOT, "build_debug", "app_desktop.exe")

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
ULONG_PTR = ctypes.c_size_t

user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumWindows.restype = wintypes.BOOL
user32.EnumChildWindows.argtypes = [wintypes.HWND, WNDENUMPROC, wintypes.LPARAM]
user32.EnumChildWindows.restype = wintypes.BOOL
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindowVisible.restype = wintypes.BOOL
user32.IsWindowEnabled.argtypes = [wintypes.HWND]
user32.IsWindowEnabled.restype = wintypes.BOOL
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.GetDlgCtrlID.argtypes = [wintypes.HWND]
user32.GetDlgCtrlID.restype = ctypes.c_int
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL
user32.MoveWindow.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.BOOL]
user32.MoveWindow.restype = wintypes.BOOL
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.restype = ctypes.c_int
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetClassNameW.restype = ctypes.c_int
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongW.restype = ctypes.c_long
user32.SendMessageTimeoutW.argtypes = [
    wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
    wintypes.UINT, wintypes.UINT, ctypes.POINTER(ULONG_PTR)
]
user32.SendMessageTimeoutW.restype = wintypes.LPARAM
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.PrintWindow.restype = wintypes.BOOL

gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.DeleteDC.restype = wintypes.BOOL
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteObject.restype = wintypes.BOOL
gdi32.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(wintypes.SIZE)]
gdi32.GetTextExtentPoint32W.restype = wintypes.BOOL
gdi32.GetDeviceCaps.argtypes = [wintypes.HDC, ctypes.c_int]
gdi32.GetDeviceCaps.restype = ctypes.c_int
gdi32.CreateDIBSection.argtypes = [
    wintypes.HDC, ctypes.c_void_p, wintypes.UINT,
    ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD
]
gdi32.CreateDIBSection.restype = wintypes.HBITMAP
gdi32.GetObjectW.argtypes = [wintypes.HGDIOBJ, ctypes.c_int, ctypes.c_void_p]
gdi32.GetObjectW.restype = ctypes.c_int
gdi32.CreateFontIndirectW.argtypes = [ctypes.c_void_p]
gdi32.CreateFontIndirectW.restype = wintypes.HFONT
gdi32.AddFontResourceExW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p]
gdi32.AddFontResourceExW.restype = ctypes.c_int

FR_PRIVATE = 0x10
_FONTS_REGISTERED = False


class LOGFONTW(ctypes.Structure):
    _fields_ = [
        ("lfHeight", ctypes.c_long),
        ("lfWidth", ctypes.c_long),
        ("lfEscapement", ctypes.c_long),
        ("lfOrientation", ctypes.c_long),
        ("lfWeight", ctypes.c_long),
        ("lfItalic", ctypes.c_byte),
        ("lfUnderline", ctypes.c_byte),
        ("lfStrikeOut", ctypes.c_byte),
        ("lfCharSet", ctypes.c_byte),
        ("lfOutPrecision", ctypes.c_byte),
        ("lfClipPrecision", ctypes.c_byte),
        ("lfQuality", ctypes.c_byte),
        ("lfPitchAndFamily", ctypes.c_byte),
        ("lfFaceName", ctypes.c_wchar * 32),
    ]

kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
kernel32.VirtualFreeEx.restype = wintypes.BOOL
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.restype = wintypes.BOOL
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = wintypes.BOOL

WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
WM_SETTEXT = 0x000C
WM_GETFONT = 0x0031
BM_CLICK = 0x00F5
CB_SETCURSEL = 0x014E
CB_GETCOUNT = 0x0146
CB_GETLBTEXTLEN = 0x0149
CB_GETLBTEXT = 0x0148
CBN_SELCHANGE = 1
CBN_SELENDOK = 9
GWL_STYLE = -16
WS_HSCROLL = 0x00100000
SMTO_ABORTIFHUNG = 0x0002

LVM_GETITEMCOUNT = 0x1004
LVM_GETCOLUMNWIDTH = 0x101D
LVM_GETHEADER = 0x101F
LVM_GETITEMRECT = 0x100E
LVM_SETITEMSTATE = 0x102B
LVM_GETITEMTEXTW = 0x1073
HDM_GETITEMCOUNT = 0x1200
HDM_GETITEMW = 0x120B

LVIS_FOCUSED = 0x0001
LVIS_SELECTED = 0x0002
LVIF_STATE = 0x0008
HDI_TEXT = 0x0002
PW_RENDERFULLCONTENT = 0x00000002

PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04


class LVITEMW(ctypes.Structure):
    _fields_ = [
        ("mask", wintypes.UINT),
        ("iItem", ctypes.c_int),
        ("iSubItem", ctypes.c_int),
        ("state", wintypes.UINT),
        ("stateMask", wintypes.UINT),
        ("pszText", ctypes.c_void_p),
        ("cchTextMax", ctypes.c_int),
        ("iImage", ctypes.c_int),
        ("lParam", ctypes.c_ssize_t),
        ("iIndent", ctypes.c_int),
        ("iGroupId", ctypes.c_int),
        ("cColumns", wintypes.UINT),
        ("puColumns", ctypes.c_void_p),
        ("piColFmt", ctypes.c_void_p),
        ("iGroup", ctypes.c_int),
    ]


class HDITEMW(ctypes.Structure):
    _fields_ = [
        ("mask", wintypes.UINT),
        ("cxy", ctypes.c_int),
        ("pszText", ctypes.c_void_p),
        ("hbm", wintypes.HBITMAP),
        ("cchTextMax", ctypes.c_int),
        ("fmt", ctypes.c_int),
        ("lParam", ctypes.c_ssize_t),
        ("iImage", ctypes.c_int),
        ("iOrder", ctypes.c_int),
        ("type", wintypes.UINT),
        ("pvFilter", ctypes.c_void_p),
        ("state", wintypes.UINT),
    ]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", ctypes.c_long),
        ("biHeight", ctypes.c_long),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", ctypes.c_long),
        ("biYPelsPerMeter", ctypes.c_long),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [
        ("bmiHeader", BITMAPINFOHEADER),
        ("bmiColors", wintypes.DWORD * 3),
    ]


def ensure_dpi_100():
    try:
        user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        user32.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        user32.SetProcessDPIAware()

    hdc = user32.GetDC(None)
    dpi_x = gdi32.GetDeviceCaps(hdc, 88)
    dpi_y = gdi32.GetDeviceCaps(hdc, 90)
    user32.ReleaseDC(None, hdc)
    if dpi_x != 96 or dpi_y != 96:
        pct = round(dpi_x * 100 / 96)
        raise RuntimeError(
            f"Escalado de pantalla invalido para pruebas GUI: {dpi_x}x{dpi_y} DPI ({pct}%). "
            "Se requiere exactamente 100% (96 DPI)."
        )


def ensure_no_app_running():
    out = subprocess.check_output(
        ["tasklist", "/FI", "IMAGENAME eq app_desktop.exe"],
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if "app_desktop.exe" in out.lower():
        raise RuntimeError(f"Ya existe un proceso app_desktop.exe en ejecucion:\n{out}")


def send_msg(hwnd, msg, wparam=0, lparam=0, timeout_ms=2500):
    res = ULONG_PTR()
    ok = user32.SendMessageTimeoutW(
        hwnd, msg, wintypes.WPARAM(wparam), wintypes.LPARAM(lparam),
        SMTO_ABORTIFHUNG, timeout_ms, ctypes.byref(res)
    )
    if not ok:
        raise TimeoutError(f"SendMessageTimeoutW fallo en hwnd={hwnd:#x}, msg={msg:#x}")
    return res.value


def get_text(hwnd):
    ln = user32.GetWindowTextLengthW(hwnd)
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    user32.GetWindowTextW(hwnd, buf, ln + 2)
    return buf.value


def get_class(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def get_rect(hwnd):
    rc = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rc))
    return (rc.left, rc.top, rc.right, rc.bottom)


def get_client_size(hwnd):
    rc = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    return (rc.right - rc.left, rc.bottom - rc.top)


def find_windows_by_pid(pid, title_sub=""):
    found = []

    @WNDENUMPROC
    def cb(hwnd, _lp):
        if not user32.IsWindowVisible(hwnd):
            return True
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            t = get_text(hwnd)
            if not title_sub or title_sub in t:
                found.append((hwnd, t))
        return True

    user32.EnumWindows(cb, 0)
    return found


def wait_for_window(pid, title_sub, timeout=12.0, exclude=()):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for hwnd, title in find_windows_by_pid(pid, title_sub):
            if any(ex in title for ex in exclude):
                continue
            return hwnd, title
        time.sleep(0.15)
    raise TimeoutError(f"Timeout esperando ventana '{title_sub}' (activas={find_windows_by_pid(pid)})")


def enum_children(hwnd):
    res = []

    @WNDENUMPROC
    def cb(child, _lp):
        res.append((child, get_class(child), get_text(child)))
        return True

    user32.EnumChildWindows(hwnd, cb, 0)
    return res


def click_button(hwnd):
    send_msg(hwnd, BM_CLICK, 0, 0)


def set_edit_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    addr = ctypes.cast(buf, ctypes.c_void_p).value
    send_msg(hwnd, WM_SETTEXT, 0, addr)


def select_combo_by_text(hwnd_combo, target_text):
    count = send_msg(hwnd_combo, CB_GETCOUNT, 0, 0)
    chosen = -1
    for i in range(count):
        ln = send_msg(hwnd_combo, CB_GETLBTEXTLEN, i, 0)
        buf = ctypes.create_unicode_buffer(ln + 2)
        send_msg(hwnd_combo, CB_GETLBTEXT, i, ctypes.cast(buf, ctypes.c_void_p).value)
        if buf.value == target_text:
            chosen = i
            break
    if chosen < 0:
        raise AssertionError(f"Opcion '{target_text}' no encontrada en ComboBox")
    send_msg(hwnd_combo, CB_SETCURSEL, chosen, 0)
    parent = user32.GetParent(hwnd_combo)
    ctrl_id = user32.GetDlgCtrlID(hwnd_combo)
    wparam_chg = (CBN_SELCHANGE << 16) | (ctrl_id & 0xFFFF)
    send_msg(parent, WM_COMMAND, wparam_chg, hwnd_combo)
    wparam_ok = (CBN_SELENDOK << 16) | (ctrl_id & 0xFFFF)
    send_msg(parent, WM_COMMAND, wparam_ok, hwnd_combo)
    time.sleep(0.15)


def list_headers(hwnd_list):
    hwnd_hdr = send_msg(hwnd_list, LVM_GETHEADER, 0, 0)
    if not hwnd_hdr:
        return [], 0
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    if not hproc:
        raise OSError("OpenProcess fallo en list_headers")
    headers = []
    try:
        count = send_msg(hwnd_hdr, HDM_GETITEMCOUNT, 0, 0)
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(HDITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        rem_buf = kernel32.VirtualAllocEx(hproc, None, 512, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            for col in range(count):
                item = HDITEMW()
                item.mask = HDI_TEXT
                item.pszText = rem_buf
                item.cchTextMax = 250
                written = ctypes.c_size_t()
                kernel32.WriteProcessMemory(hproc, rem_item, ctypes.byref(item), ctypes.sizeof(item), ctypes.byref(written))
                send_msg(hwnd_hdr, HDM_GETITEMW, col, rem_item)
                out = ctypes.create_unicode_buffer(256)
                read = ctypes.c_size_t()
                kernel32.ReadProcessMemory(hproc, rem_buf, out, 512, ctypes.byref(read))
                headers.append(out.value)
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(hproc, rem_buf, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)
    return headers, hwnd_hdr


def read_list_rows(hwnd_list, num_cols=4):
    count = send_msg(hwnd_list, LVM_GETITEMCOUNT, 0, 0)
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    if not hproc:
        raise OSError("OpenProcess fallo en read_list_rows")
    rows = []
    try:
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(LVITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        rem_buf = kernel32.VirtualAllocEx(hproc, None, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            for r in range(count):
                row_vals = []
                for c in range(num_cols):
                    lvi = LVITEMW()
                    lvi.iSubItem = c
                    lvi.pszText = rem_buf
                    lvi.cchTextMax = 500
                    written = ctypes.c_size_t()
                    kernel32.WriteProcessMemory(hproc, rem_item, ctypes.byref(lvi), ctypes.sizeof(lvi), ctypes.byref(written))
                    send_msg(hwnd_list, LVM_GETITEMTEXTW, r, rem_item)
                    out = ctypes.create_unicode_buffer(512)
                    read = ctypes.c_size_t()
                    kernel32.ReadProcessMemory(hproc, rem_buf, out, 1024, ctypes.byref(read))
                    row_vals.append(out.value)
                rows.append(tuple(row_vals))
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(hproc, rem_buf, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)
    return rows


def select_list_row(hwnd_list, index):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE, False, pid.value)
    try:
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(LVITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            lvi = LVITEMW()
            lvi.mask = LVIF_STATE
            lvi.state = LVIS_SELECTED | LVIS_FOCUSED
            lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED
            written = ctypes.c_size_t()
            kernel32.WriteProcessMemory(hproc, rem_item, ctypes.byref(lvi), ctypes.sizeof(lvi), ctypes.byref(written))
            send_msg(hwnd_list, LVM_SETITEMSTATE, index, rem_item)
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)


def get_list_item_rect(hwnd_list, index):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    try:
        rem_rc = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(wintypes.RECT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            rc = wintypes.RECT(0, 0, 0, 0)
            written = ctypes.c_size_t()
            kernel32.WriteProcessMemory(hproc, rem_rc, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(written))
            send_msg(hwnd_list, LVM_GETITEMRECT, index, rem_rc)
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(hproc, rem_rc, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(read))
            return (rc.left, rc.top, rc.right, rc.bottom)
        finally:
            kernel32.VirtualFreeEx(hproc, rem_rc, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)


def _ensure_local_fonts():
    global _FONTS_REGISTERED
    if _FONTS_REGISTERED:
        return
    fonts_dir = os.path.join(REPO_ROOT, "assets", "fonts")
    for fname in ("Roboto-Regular.ttf", "Roboto-Medium.ttf", "Roboto-Bold.ttf"):
        fpath = os.path.join(fonts_dir, fname)
        if os.path.exists(fpath):
            gdi32.AddFontResourceExW(fpath, FR_PRIVATE, None)
    _FONTS_REGISTERED = True


def measure_text_extent(hwnd_for_font, text):
    _ensure_local_fonts()
    hdc = user32.GetDC(hwnd_for_font)
    hfont = send_msg(hwnd_for_font, WM_GETFONT, 0, 0)
    hf_local = None
    old = None
    if hfont:
        lf = LOGFONTW()
        if gdi32.GetObjectW(hfont, ctypes.sizeof(lf), ctypes.byref(lf)) > 0:
            hf_local = gdi32.CreateFontIndirectW(ctypes.byref(lf))
            if hf_local:
                old = gdi32.SelectObject(hdc, hf_local)
                assert old, f"SelectObject fallo para fuente '{lf.lfFaceName}'"
    sz = wintypes.SIZE()
    gdi32.GetTextExtentPoint32W(hdc, text, len(text), ctypes.byref(sz))
    if old:
        gdi32.SelectObject(hdc, old)
    if hf_local:
        gdi32.DeleteObject(hf_local)
    user32.ReleaseDC(hwnd_for_font, hdc)
    return sz.cx, sz.cy


def measure_ink_vertical_center(hwnd):
    rc = get_rect(hwnd)
    w = rc[2] - rc[0]
    h = rc[3] - rc[1]
    if w <= 4 or h <= 4:
        return None
    hdc_win = user32.GetDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_win)
    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = w
    bmi.bmiHeader.biHeight = -h
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bits = ctypes.c_void_p()
    hbmp = gdi32.CreateDIBSection(hdc_mem, ctypes.byref(bmi), 0, ctypes.byref(bits), None, 0)
    old = gdi32.SelectObject(hdc_mem, hbmp)
    try:
        user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT)
        raw = (ctypes.c_uint32 * (w * h)).from_address(bits.value)
        bg = raw[2 * w + 2]
        bg_r, bg_g, bg_b = (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF
        min_y, max_y = h, -1
        for y in range(2, h - 2):
            for x in range(4, w - 18):
                px = raw[y * w + x]
                r, g, b = (px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF
                if abs(r - bg_r) + abs(g - bg_g) + abs(b - bg_b) > 45:
                    if y < min_y:
                        min_y = y
                    if y > max_y:
                        max_y = y
        if max_y < min_y:
            return None
        return (min_y + max_y) / 2.0, h / 2.0
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(hwnd, hdc_win)


def is_descendant(hwnd, ancestor):
    cur = hwnd
    while cur:
        if cur == ancestor:
            return True
        cur = user32.GetParent(cur)
    return False


def locate_ingredients_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]

    btn_new = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Nuevo ingrediente"), None)
    btn_refresh = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Actualizar"), None)
    btn_save = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Guardar"), None)
    assert btn_new is not None and btn_refresh is not None and btn_save is not None, "Faltan botones principales"

    panel_hwnd = user32.GetParent(btn_new)
    panel_buttons = {t for (h, c, t) in children if c.lower() == "button" and is_descendant(h, panel_hwnd)}
    assert panel_buttons == {"Nuevo ingrediente", "Actualizar", "Guardar"}, (
        f"Botones inesperados en Ingredientes (no debe existir control de existencias): {panel_buttons}"
    )
    save_rect = get_rect(btn_save)
    panel_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel_hwnd)],
        key=lambda item: item[1][1]
    )
    assert len(panel_edits) == 2, f"Se esperaban exactamente 2 Edit (Nombre y Aviso), encontrados {len(panel_edits)}"
    txt_name = panel_edits[0][0]
    txt_threshold = panel_edits[1][0]

    combos = [h for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel_hwnd)]
    lists = [h for (h, c, _t) in children if c.lower() == "syslistview32" and is_descendant(h, panel_hwnd)]
    assert len(lists) == 1, f"Se esperaba 1 SysListView32 de ingredientes, encontrados {len(lists)}"

    return {
        "panel": panel_hwnd,
        "btn_new": btn_new,
        "btn_refresh": btn_refresh,
        "btn_save": btn_save,
        "txt_name": txt_name,
        "txt_threshold": txt_threshold,
        "combo": combos[0] if combos else None,
        "list": lists[0],
        "visible": visible,
        "save_rect": save_rect,
    }


def open_ingredients_screen(hwnd_main):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_inv = next((h for (h, t) in btns if "Inventario" in t), None)
    assert btn_inv is not None, "No se encontro boton Inventario en sidebar"
    click_button(btn_inv)
    time.sleep(0.35)
    btns2 = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_ing = next((h for (h, t) in btns2 if t == "Ingredientes"), None)
    assert btn_ing is not None, "No se encontro subseccion Ingredientes"
    click_button(btn_ing)
    time.sleep(0.45)


def set_ui_profile(hwnd_main, want_touch):
    ctrl = locate_ingredients_controls(hwnd_main)
    is_currently_touch = (get_rect(ctrl["btn_new"])[3] - get_rect(ctrl["btn_new"])[1]) >= 48
    if is_currently_touch == want_touch:
        return
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    toggle = next((h for (h, t) in btns if "Touch" in t or "Estándar" in t), None)
    assert toggle is not None, "No se encontro boton de cambio de perfil Touch/Estandar"
    click_button(toggle)
    time.sleep(0.5)
    open_ingredients_screen(hwnd_main)
    ctrl2 = locate_ingredients_controls(hwnd_main)
    now_touch = (get_rect(ctrl2["btn_new"])[3] - get_rect(ctrl2["btn_new"])[1]) >= 48
    assert now_touch == want_touch, f"Cambio de perfil a touch={want_touch} no surtio efecto"


def assert_no_jargon(hwnd_main, allowed_mg_stocks):
    ctrl = locate_ingredients_controls(hwnd_main)
    p_rect = get_rect(ctrl["panel"])
    texts = []
    for (h, _c, t) in ctrl["visible"]:
        r = get_rect(h)
        if r[0] >= p_rect[0] - 2 and r[1] >= p_rect[1] - 2 and r[2] <= p_rect[2] + 2 and r[3] <= p_rect[3] + 2:
            if t:
                texts.append(("ctrl", t))
    headers, _ = list_headers(ctrl["list"])
    for hdr in headers:
        texts.append(("header", hdr))
    if user32.IsWindowVisible(ctrl["list"]):
        rows = read_list_rows(ctrl["list"], 4)
        for row in rows:
            texts.append(("cell_name", row[0]))
            texts.append(("cell_stock", row[1]))
            texts.append(("cell_thresh", row[2]))
            texts.append(("cell_state", row[3]))

    forbidden = ["MASS", "VOLUME", "COUNT", "unidad base", "inventory.", "ing-", "f7-", "seed-"]
    for kind, txt in texts:
        for bad in forbidden:
            assert bad not in txt, f"Jerga tecnica prohibida '{bad}' en {kind}: '{txt}'"
        if re.search(r"\bmg\b", txt):
            assert kind == "cell_stock" and txt in allowed_mg_stocks, (
                f"'mg' suelto prohibido fuera de existencia < 1000 mg en {kind}: '{txt}'"
            )


def assert_geometry(hwnd_main, touch_mode, width, height):
    user32.MoveWindow(hwnd_main, 20, 20, width, height, True)
    time.sleep(0.6)
    ctrl = locate_ingredients_controls(hwnd_main)
    panel = ctrl["panel"]
    list_hwnd = ctrl["list"]
    p_rect = get_rect(panel)
    main_rc = get_rect(hwnd_main)

    # Guardar visible sin desplazarse
    sr = get_rect(ctrl["btn_save"])
    assert sr[2] <= p_rect[2] and sr[3] <= p_rect[3], f"Guardar se sale del panel: save={sr}, panel={p_rect}"
    assert sr[2] <= main_rc[2] and sr[3] <= main_rc[3], f"Guardar se sale de la ventana: save={sr}, main={main_rc}"

    list_cw = None
    col_widths = None
    row_h = None
    # ListView columnas y scroll horizontal
    if user32.IsWindowVisible(list_hwnd):
        headers, hdr_hwnd = list_headers(list_hwnd)
        assert headers == ["Nombre", "Existencia", "Avisar con menos de", "Estado"], f"Encabezados incorrectos: {headers}"
        list_cw, _ = get_client_size(list_hwnd)
        col_widths = [send_msg(list_hwnd, LVM_GETCOLUMNWIDTH, i, 0) for i in range(4)]
        for i, title in enumerate(headers):
            hw, _ = measure_text_extent(hdr_hwnd, title)
            assert col_widths[i] >= hw, f"Columna {i} ('{title}') recortada: width={col_widths[i]} < hdr={hw}"
        col_sum = sum(col_widths)
        assert col_sum <= list_cw, f"Suma columnas {col_sum} > ancho cliente {list_cw}"
        assert (list_cw - col_sum) <= 8, f"Holgura excesiva: {list_cw - col_sum} px (> 8 px)"
        style = user32.GetWindowLongW(list_hwnd, GWL_STYLE)
        assert (style & WS_HSCROLL) == 0, f"Lista con WS_HSCROLL en {width}x{height} touch={touch_mode}"

        row_h = None
        if touch_mode and send_msg(list_hwnd, LVM_GETITEMCOUNT, 0, 0) > 0:
            irc = get_list_item_rect(list_hwnd, 0)
            row_h = irc[3] - irc[1]
            assert row_h >= 40, f"Altura fila Touch insuficiente: {row_h} px (< 40 px)"
        elif send_msg(list_hwnd, LVM_GETITEMCOUNT, 0, 0) > 0:
            irc = get_list_item_rect(list_hwnd, 0)
            row_h = irc[3] - irc[1]

    # Alturas Touch y centrado vertical
    wrap_name = user32.GetParent(ctrl["txt_name"])
    wrap_thresh = user32.GetParent(ctrl["txt_threshold"])
    if touch_mode:
        for label, h_ctrl in [
            ("btn_new", ctrl["btn_new"]),
            ("btn_refresh", ctrl["btn_refresh"]),
            ("btn_save", ctrl["btn_save"]),
            ("txt_name_wrap", wrap_name),
            ("txt_threshold_wrap", wrap_thresh),
        ]:
            r = get_rect(h_ctrl)
            assert (r[3] - r[1]) >= 48, f"Control Touch '{label}' mide {r[3] - r[1]} px (< 48 px)"
        if ctrl["combo"] is not None:
            rc_c = get_rect(ctrl["combo"])
            assert (rc_c[3] - rc_c[1]) >= 48, f"ComboBox Touch mide {rc_c[3] - rc_c[1]} px (< 48 px)"

    for name_lbl, ed_hwnd in [("txt_name", ctrl["txt_name"]), ("txt_threshold", ctrl["txt_threshold"])]:
        if get_text(ed_hwnd).strip():
            ink = measure_ink_vertical_center(user32.GetParent(ed_hwnd))
            if ink is not None:
                ink_mid, box_mid = ink
                assert abs(ink_mid - box_mid) <= 3.0, (
                    f"Texto no centrado en {name_lbl} ({width}x{height}): ink_mid={ink_mid:.1f}, box_mid={box_mid:.1f}"
                )

    # No recorte de texto en botones/etiquetas visibles ni solapamientos entre controles hermanos
    leaves = []
    for (h, cls, txt) in ctrl["visible"]:
        r = get_rect(h)
        if r[0] < p_rect[0] - 2 or r[1] < p_rect[1] - 2 or r[2] > p_rect[2] + 2 or r[3] > p_rect[3] + 2:
            continue
        cl = cls.lower()
        if cl in ("static", "button", "edit", "combobox", "syslistview32"):
            leaves.append((h, cl, txt, r))
            if cl in ("static", "button") and txt and "\n" not in txt:
                tw, _ = measure_text_extent(h, txt)
                cw, _ = get_client_size(h)
                assert tw <= cw + 2, f"Texto recortado en {cl} '{txt}': text_w={tw} > client_w={cw}"

    for i in range(len(leaves)):
        h1, c1, t1, r1 = leaves[i]
        for j in range(i + 1, len(leaves)):
            h2, c2, t2, r2 = leaves[j]
            if user32.GetParent(h1) == h2 or user32.GetParent(h2) == h1:
                continue
            ix = max(0, min(r1[2], r2[2]) - max(r1[0], r2[0]))
            iy = max(0, min(r1[3], r2[3]) - max(r1[1], r2[1]))
            assert ix * iy == 0, f"Solapamiento detectado: ({c1}, '{t1}', {r1}) vs ({c2}, '{t2}', {r2})"

    return {
        "list_cw": list_cw if user32.IsWindowVisible(list_hwnd) else None,
        "col_widths": col_widths if user32.IsWindowVisible(list_hwnd) else None,
        "row_h": row_h if user32.IsWindowVisible(list_hwnd) else None,
        "btn_new_h": get_rect(ctrl["btn_new"])[3] - get_rect(ctrl["btn_new"])[1],
        "btn_save_h": get_rect(ctrl["btn_save"])[3] - get_rect(ctrl["btn_save"])[1],
        "txt_name_h": get_rect(wrap_name)[3] - get_rect(wrap_name)[1],
        "txt_threshold_h": get_rect(wrap_thresh)[3] - get_rect(wrap_thresh)[1],
        "combo_h": (get_rect(ctrl["combo"])[3] - get_rect(ctrl["combo"])[1]) if ctrl["combo"] is not None else None,
    }


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba F7 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_f7_{label.lower()}_") as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir

        procs = []
        owner_user = "dueno_f7"
        owner_pass = "ClaveDuenoF7!2026"

        try:
            # 1. Primer inicio: alta de Dueno
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_first, _ = wait_for_window(p1.pid, "Primer Inicio")
            f_children = enum_children(hwnd_first)
            f_edits = [h for (h, c, _t) in f_children if c.lower() == "edit"]
            f_btns = [h for (h, c, _t) in f_children if c.lower() == "button"]
            assert len(f_edits) >= 5
            set_edit_text(f_edits[0], "Dueno Prueba F7")
            set_edit_text(f_edits[1], owner_user)
            set_edit_text(f_edits[2], "dueno_f7@cafeteria.local")
            set_edit_text(f_edits[3], owner_pass)
            set_edit_text(f_edits[4], owner_pass)
            click_button(f_btns[0])

            hwnd_main, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"))
            open_ingredients_screen(hwnd_main)

            # 2. Estado vacio inicial
            ctrl = locate_ingredients_controls(hwnd_main)
            assert not user32.IsWindowVisible(ctrl["list"]), "La lista debe ocultarse cuando no hay ingredientes"
            empty_txts = [t for (_h, c, t) in ctrl["visible"] if c.lower() == "static" and "Todavía no hay ingredientes" in t]
            assert empty_txts, "Debe mostrarse el mensaje de estado vacio invitando a crear el primero"
            assert_no_jargon(hwnd_main, {"Inicia en 0 al crearse"})

            # 3. Crear uno de cada tipo y validar SQLite
            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Harina de trigo")
            select_combo_by_text(ctrl["combo"], "Peso")
            set_edit_text(ctrl["txt_threshold"], "500")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Leche entera")
            select_combo_by_text(ctrl["combo"], "Volumen")
            unit_lbls = [t for (_h, c, t) in enum_children(ctrl["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and t == "mililitros"]
            assert unit_lbls, "Unidad debe cambiar a 'mililitros' al elegir Volumen"
            set_edit_text(ctrl["txt_threshold"], "1500")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Vaso mediano")
            select_combo_by_text(ctrl["combo"], "Piezas")
            unit_lbls_pz = [t for (_h, c, t) in enum_children(ctrl["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and t == "piezas"]
            assert unit_lbls_pz, "Unidad debe cambiar a 'piezas' al elegir Piezas"
            set_edit_text(ctrl["txt_threshold"], "20")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            cur = con.cursor()
            cur.execute("SELECT name, unit_dimension, base_unit, low_stock_threshold, current_stock FROM ingredients ORDER BY name")
            db_rows = cur.fetchall()
            con.close()
            assert db_rows == [
                ("Harina de trigo", "MASS", "mg", 500000, 0),
                ("Leche entera", "VOLUME", "ml", 1500, 0),
                ("Vaso mediano", "COUNT", "pz", 20, 0),
            ], f"Filas SQLite inesperadas: {db_rows}"

            # 4. Editar nombre y umbral; ComboBox no editable; nombre vacio y repetido
            ctrl = locate_ingredients_controls(hwnd_main)
            rows = read_list_rows(ctrl["list"], 4)
            harina_idx = next(i for i, r in enumerate(rows) if r[0] == "Harina de trigo")
            select_list_row(ctrl["list"], harina_idx)
            time.sleep(0.3)
            ctrl = locate_ingredients_controls(hwnd_main)
            assert ctrl["combo"] is None, "Al editar, 'Como se mide' no debe quedar como ComboBox editable"

            set_edit_text(ctrl["txt_name"], "Harina refinada")
            set_edit_text(ctrl["txt_threshold"], "750")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            # Nombre vacio en edicion
            set_edit_text(ctrl["txt_name"], "   ")
            click_button(ctrl["btn_save"])
            time.sleep(0.25)
            err_empty = [t for (_h, c, t) in enum_children(ctrl["panel"]) if c.lower() == "static" and "vacío" in t.lower()]
            assert err_empty, "Debe mostrar error al intentar guardar nombre vacio"

            # Nombre repetido al crear -> mensaje del servicio
            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Harina refinada")
            set_edit_text(ctrl["txt_threshold"], "100")
            click_button(ctrl["btn_save"])
            time.sleep(0.25)
            dup_msg = [t for (_h, c, t) in enum_children(ctrl["panel"]) if c.lower() == "static" and "Ya existe un ingrediente con ese nombre" in t]
            assert dup_msg, "Debe mostrarse el error exacto del servicio para nombre repetido"

            # Desbordamiento gramos -> mg
            set_edit_text(ctrl["txt_name"], "Azucar")
            set_edit_text(ctrl["txt_threshold"], "9999999999999999")
            click_button(ctrl["btn_save"])
            time.sleep(0.25)
            ovf_msg = [t for (_h, c, t) in enum_children(ctrl["panel"]) if c.lower() == "static" and "demasiado grande" in t]
            assert ovf_msg, "Debe mostrarse error claro por desbordamiento"

            # Cerrar y reabrir para verificar persistencia
            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p1.wait(timeout=6)

            con = sqlite3.connect(db_path)
            cur = con.cursor()
            total_ing = cur.execute("SELECT COUNT(*) FROM ingredients").fetchone()[0]
            assert total_ing == 3, f"Errores de validacion no debieron guardar filas nuevas: COUNT={total_ing}"
            cur.execute("SELECT name, low_stock_threshold FROM ingredients WHERE unit_dimension='MASS'")
            mass_row = cur.fetchone()
            assert mass_row == ("Harina refinada", 750000), f"Persistencia fallida en SQLite: {mass_row}"
            # Sembrar casos requeridos de existencias y estados
            cur.executemany(
                "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) VALUES (?, ?, ?, ?, ?, ?)",
                [
                    ("seed-kg", "Cafe tostado", "MASS", "mg", 1500000, 1000000),
                    ("seed-g", "Canela molida", "MASS", "mg", 250000, 500000),
                    ("seed-vol", "Agua purificada", "VOLUME", "ml", 2500, 1000),
                    ("seed-pz", "Tapa plastica", "COUNT", "pz", 7, 10),
                    ("seed-zero", "Sal de grano", "MASS", "mg", 5000, 0),
                    ("seed-mg", "Azafran", "MASS", "mg", 450, 1000),
                ]
            )
            # Sembrar usuario Cajero con el mismo hash que Dueno para probar permisos
            cur.execute(
                """
                INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                SELECT 'user-cajero-f7', 'cajero_f7', 'Cajero F7', 'cajero@cafeteria.local', password_hash, 'role-cashier', 1, created_at
                FROM users WHERE username = ? LIMIT 1
                """,
                (owner_user,)
            )
            con.commit()
            con.close()

            # 5. Reabrir como Dueno y comprobar existencias sembradas + geometria
            p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p2)
            hwnd_login, _ = wait_for_window(p2.pid, "Iniciar Sesión")
            l_children = enum_children(hwnd_login)
            l_edits = [h for (h, c, _t) in l_children if c.lower() == "edit"]
            l_btns = [h for (h, c, _t) in l_children if c.lower() == "button"]
            set_edit_text(l_edits[0], owner_user)
            set_edit_text(l_edits[1], owner_pass)
            click_button(l_btns[0])

            hwnd_main2, _ = wait_for_window(p2.pid, "Cafetería ITCH II", exclude=("Iniciar Sesión", "Primer Inicio"))
            open_ingredients_screen(hwnd_main2)
            ctrl = locate_ingredients_controls(hwnd_main2)
            ui_rows = {r[0]: r[1:] for r in read_list_rows(ctrl["list"], 4)}
            assert ui_rows["Harina refinada"] == ("0 mg", "750 g", "Poco inventario"), ui_rows["Harina refinada"]
            assert ui_rows["Cafe tostado"] == ("1.5 kg", "1 kg", "Suficiente"), ui_rows["Cafe tostado"]
            assert ui_rows["Canela molida"] == ("250 g", "500 g", "Poco inventario"), ui_rows["Canela molida"]
            assert ui_rows["Agua purificada"] == ("2.5 L", "1 L", "Suficiente"), ui_rows["Agua purificada"]
            assert ui_rows["Tapa plastica"] == ("7 pz", "10 pz", "Poco inventario"), ui_rows["Tapa plastica"]
            assert ui_rows["Sal de grano"] == ("5 g", "Sin aviso", "Suficiente"), ui_rows["Sal de grano"]
            assert ui_rows["Azafran"] == ("450 mg", "1 g", "Poco inventario"), ui_rows["Azafran"]
            assert_no_jargon(hwnd_main2, {"0 mg", "450 mg"})

            # Geometria en Estandar y Touch
            metrics = {}
            set_ui_profile(hwnd_main2, False)
            metrics[("Estandar", 1200, 720)] = assert_geometry(hwnd_main2, False, 1200, 720)
            metrics[("Estandar", 960, 600)] = assert_geometry(hwnd_main2, False, 960, 600)
            set_ui_profile(hwnd_main2, True)
            metrics[("Touch", 960, 600)] = assert_geometry(hwnd_main2, True, 960, 600)
            metrics[("Touch", 1200, 720)] = assert_geometry(hwnd_main2, True, 1200, 720)
            click_button(locate_ingredients_controls(hwnd_main2)["btn_new"])
            time.sleep(0.25)
            assert_geometry(hwnd_main2, True, 960, 600)

            user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
            p2.wait(timeout=6)

            # 6. Iniciar sesion como Cajero (sin permisos de inventario)
            p3 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p3)
            hwnd_login3, _ = wait_for_window(p3.pid, "Iniciar Sesión")
            l3_children = enum_children(hwnd_login3)
            l3_edits = [h for (h, c, _t) in l3_children if c.lower() == "edit"]
            l3_btns = [h for (h, c, _t) in l3_children if c.lower() == "button"]
            set_edit_text(l3_edits[0], "cajero_f7")
            set_edit_text(l3_edits[1], owner_pass)
            click_button(l3_btns[0])

            hwnd_main3, _ = wait_for_window(p3.pid, "Cafetería ITCH II", exclude=("Iniciar Sesión", "Primer Inicio"))
            open_ingredients_screen(hwnd_main3)
            ctrl_c = locate_ingredients_controls(hwnd_main3)
            assert not user32.IsWindowEnabled(ctrl_c["btn_new"]), "Nuevo ingrediente debe quedar deshabilitado para Cajero"
            assert not user32.IsWindowEnabled(ctrl_c["txt_name"]), "Nombre debe quedar deshabilitado para Cajero"
            assert not user32.IsWindowEnabled(ctrl_c["txt_threshold"]), "Aviso debe quedar deshabilitado para Cajero"
            assert not user32.IsWindowEnabled(ctrl_c["btn_save"]), "Guardar debe quedar deshabilitado para Cajero"
            ro_txts = [t for (_h, c, t) in enum_children(ctrl_c["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and "Solo lectura" in t]
            assert ro_txts, "Debe mostrarse aviso de solo lectura para Cajero"
            svc_err = [t for (_h, c, t) in enum_children(ctrl_c["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and "Permiso insuficiente" in t]
            assert svc_err, "Debe mostrarse el mensaje del servicio cuando list_ingredients falla"

            con = sqlite3.connect(db_path)
            before_snap = con.execute("SELECT id, name, low_stock_threshold, current_stock FROM ingredients ORDER BY id").fetchall()
            con.close()
            click_button(ctrl_c["btn_save"])
            time.sleep(0.2)
            con = sqlite3.connect(db_path)
            after_snap = con.execute("SELECT id, name, low_stock_threshold, current_stock FROM ingredients ORDER BY id").fetchall()
            con.close()
            assert before_snap == after_snap, "Un intento de guardar como Cajero no debe alterar SQLite"

            user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
            p3.wait(timeout=6)
            print(f"[OK] {label} completo. Metricas={metrics}", flush=True)
        finally:
            for p in procs:
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=3)
                    except Exception:
                        p.kill()


def main():
    ensure_dpi_100()
    ensure_no_app_running()
    run_suite("Release", RELEASE_EXE)
    ensure_no_app_running()
    run_suite("Debug", DEBUG_EXE)
    print("=== PRUEBA F7 COMPLETA EXITOSA (Release y Debug) ===", flush=True)


if __name__ == "__main__":
    main()
