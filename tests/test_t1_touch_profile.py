#!/usr/bin/env python3
"""
test_t1_touch_profile.py - Prueba GUI real para Bloque T1 (Perfil Tactil en Configuracion y dialogos).

Verifica Release y Debug con ejecutables nativos y bases SQLite aisladas bajo cualquier escalado de Windows:
- Conciencia DPI por monitor (SetProcessDpiAwarenessContext(-4)) y umbrales escalados por GetDpiForWindow/96.
a) Primer arranque sin station_profile.cfg: Alta del Dueno en Estandar, sin recortes ni controles fuera.
b) BackOffice > Configuracion: opciones Estandar y Tactil, pantalla de impresion incrustada; cambiar a Tactil
   actualiza station_profile.cfg, barra lateral (>= 56 logicos), boton de encabezado, opcion marcada y vista actual.
c) Producto sembrado por SQL, agregar linea en Venta, cambiar perfil en Configuracion y volver a Venta:
   la linea persiste en pantalla y en SQLite.
d) Volver a Estandar desde boton del encabezado estando en Configuracion: opcion marcada cambia a Estandar.
e) Cerrar en Tactil y reabrir: Login en Tactil (>= 48 campos, >= 56 boton, centrado vertical, fuente Tactil),
   y tras entrar Configuracion muestra Tactil marcada.
f) Primer arranque con station_profile.cfg='touch' escrito previamente: Alta Dueno en Tactil con mismas cotas.
g) Inventario informativo de objetivos tactiles en perfil Tactil (1200x720 logicos) en las 7 pantallas reales,
   escrito en qa_visual/touch_audit.txt (no falla la prueba).
"""

import ctypes
from ctypes import wintypes
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RELEASE_EXE = os.path.join(REPO_ROOT, "build_release", "app_desktop.exe")
DEBUG_EXE = os.path.join(REPO_ROOT, "build_debug", "app_desktop.exe")
AUDIT_PATH = os.path.join(REPO_ROOT, "qa_visual", "touch_audit.txt")

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
ULONG_PTR = ctypes.c_size_t
MONITOR_DEFAULTTONEAREST = 2

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
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL
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
user32.MonitorFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD]
user32.MonitorFromWindow.restype = wintypes.HMONITOR

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
WM_SETTEXT = 0x000C
WM_GETFONT = 0x0031
BM_CLICK = 0x00F5
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
GWL_STYLE = -16
WS_TABSTOP = 0x00010000
ES_READONLY = 0x0800
SMTO_ABORTIFHUNG = 0x0002
LVM_GETITEMCOUNT = 0x1004
LVM_GETITEMRECT = 0x100E
PW_RENDERFULLCONTENT = 0x00000002
FR_PRIVATE = 0x10
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04

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


class MONITORINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("rcMonitor", wintypes.RECT),
        ("rcWork", wintypes.RECT),
        ("dwFlags", wintypes.DWORD),
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


def enable_per_monitor_dpi():
    try:
        user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        user32.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
        if not user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):
            user32.SetProcessDPIAware()
    except Exception:
        user32.SetProcessDPIAware()


def get_system_dpi():
    hdc = user32.GetDC(None)
    dpi = gdi32.GetDeviceCaps(hdc, 88)
    user32.ReleaseDC(None, hdc)
    return dpi if dpi > 0 else 96


def get_window_dpi(hwnd):
    if hasattr(user32, "GetDpiForWindow"):
        user32.GetDpiForWindow.argtypes = [wintypes.HWND]
        user32.GetDpiForWindow.restype = wintypes.UINT
        dpi = user32.GetDpiForWindow(hwnd)
        if dpi > 0:
            return dpi
    return get_system_dpi()


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


def get_client_screen_rect(hwnd):
    rc = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    pt1 = wintypes.POINT(rc.left, rc.top)
    pt2 = wintypes.POINT(rc.right, rc.bottom)
    user32.ClientToScreen(hwnd, ctypes.byref(pt1))
    user32.ClientToScreen(hwnd, ctypes.byref(pt2))
    return (pt1.x, pt1.y, pt2.x, pt2.y)


def get_work_area_for_window(hwnd):
    hmon = user32.MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    user32.GetMonitorInfoW.argtypes = [wintypes.HMONITOR, ctypes.POINTER(MONITORINFO)]
    user32.GetMonitorInfoW.restype = wintypes.BOOL
    ok = user32.GetMonitorInfoW(hmon, ctypes.byref(mi))
    assert ok, "GetMonitorInfoW fallo"
    r = mi.rcWork
    return (r.left, r.top, r.right, r.bottom)


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


def post_click_button(hwnd):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)


def set_edit_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    addr = ctypes.cast(buf, ctypes.c_void_p).value
    send_msg(hwnd, WM_SETTEXT, 0, addr)


def select_list_row(hwnd_list, row_index):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    assert hproc, "OpenProcess fallo en select_list_row"
    try:
        rem = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(wintypes.RECT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            rc = wintypes.RECT(0, 0, 0, 0)
            wr = ctypes.c_size_t()
            kernel32.WriteProcessMemory(hproc, rem, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(wr))
            send_msg(hwnd_list, LVM_GETITEMRECT, row_index, rem)
            rd = ctypes.c_size_t()
            kernel32.ReadProcessMemory(hproc, rem, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(rd))
        finally:
            kernel32.VirtualFreeEx(hproc, rem, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)
    cx = rc.left + min(40, max(2, (rc.right - rc.left) // 2))
    cy = (rc.top + rc.bottom) // 2
    lp = (cy << 16) | (cx & 0xFFFF)
    user32.PostMessageW(hwnd_list, WM_LBUTTONDOWN, 1, lp)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_list, WM_LBUTTONUP, 0, lp)
    time.sleep(0.3)


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


def get_control_logfont(hwnd):
    hfont = send_msg(hwnd, WM_GETFONT, 0, 0)
    if not hfont:
        return None
    lf = LOGFONTW()
    if gdi32.GetObjectW(hfont, ctypes.sizeof(lf), ctypes.byref(lf)) > 0:
        return lf
    return None


def measure_text_extent(hwnd_for_font, text):
    _ensure_local_fonts()
    hdc = user32.GetDC(hwnd_for_font)
    lf = get_control_logfont(hwnd_for_font)
    hf_local = None
    old = None
    if lf is not None:
        hf_local = gdi32.CreateFontIndirectW(ctypes.byref(lf))
        if hf_local:
            old = gdi32.SelectObject(hdc, hf_local)
            assert old, f"SelectObject fallo para '{lf.lfFaceName}'"
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
            for x in range(4, w - 4):
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


def rects_overlap(r1, r2):
    return (r1[0] < r2[2] - 1) and (r2[0] < r1[2] - 1) and (r1[1] < r2[3] - 1) and (r2[1] < r1[3] - 1)


def is_descendant(hwnd, ancestor):
    cur = hwnd
    while cur:
        if cur == ancestor:
            return True
        cur = user32.GetParent(cur)
    return False


def check_dialog_geometry(hwnd_dlg, is_touch, expected_edits, label):
    dpi = get_window_dpi(hwnd_dlg)
    scale = dpi / 96.0
    dlg_rc = get_rect(hwnd_dlg)
    work_rc = get_work_area_for_window(hwnd_dlg)

    assert dlg_rc[0] >= work_rc[0] and dlg_rc[1] >= work_rc[1] and dlg_rc[2] <= work_rc[2] and dlg_rc[3] <= work_rc[3], (
        f"[{label}] Dialogo fuera del area de trabajo: dlg={dlg_rc}, work={work_rc}"
    )

    cl_rc = get_client_screen_rect(hwnd_dlg)
    children = enum_children(hwnd_dlg)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]

    edits = [h for (h, c, _t) in visible if c.lower() == "edit"]
    buttons = [(h, t) for (h, c, t) in visible if c.lower() == "button"]
    assert len(edits) == expected_edits, f"[{label}] Edits esperados={expected_edits}, encontrados={len(edits)}"
    assert len(buttons) >= 1, f"[{label}] Boton principal no encontrado"
    btn_main, btn_main_txt = buttons[0]

    # Comprobar contencion en area cliente y textos sin recorte
    for h, c, t in visible:
        r = get_rect(h)
        assert r[0] >= cl_rc[0] - 1 and r[1] >= cl_rc[1] - 1 and r[2] <= cl_rc[2] + 1 and r[3] <= cl_rc[3] + 1, (
            f"[{label}] Control fuera del dialogo ({c} '{t}'): rect={r}, client={cl_rc}"
        )
        cl = c.lower()
        if cl in ("static", "button") and t.strip():
            crc = wintypes.RECT()
            user32.GetClientRect(h, ctypes.byref(crc))
            cw = crc.right - crc.left
            max_line_w = max(measure_text_extent(h, ln)[0] for ln in t.splitlines() if ln.strip())
            assert max_line_w <= cw + 2, (
                f"[{label}] Texto recortado en {cl} '{t}': text_w={max_line_w} > client_w={cw}"
            )

    # Comprobar no solapamientos entre hermanos reales
    for i in range(len(visible)):
        h1, c1, t1 = visible[i]
        p1 = user32.GetParent(h1)
        r1 = get_rect(h1)
        for j in range(i + 1, len(visible)):
            h2, c2, t2 = visible[j]
            if user32.GetParent(h2) != p1:
                continue
            r2 = get_rect(h2)
            assert not rects_overlap(r1, r2), (
                f"[{label}] Solapamiento entre hermanos ({c1} '{t1}' {r1}) y ({c2} '{t2}' {r2})"
            )

    # Medir campos (en Tactil su contenedor es el padre inmediato del Edit) y boton principal
    field_heights = []
    for ed in edits:
        parent_ed = user32.GetParent(ed)
        target = parent_ed if (is_touch and parent_ed != hwnd_dlg) else ed
        rc_f = get_rect(target)
        field_heights.append(rc_f[3] - rc_f[1])

    btn_rc = get_rect(btn_main)
    btn_h = btn_rc[3] - btn_rc[1]

    if is_touch:
        min_field_px = 48.0 * scale
        min_btn_px = 56.0 * scale
        for idx, fh in enumerate(field_heights):
            assert fh >= min_field_px - 0.5, (
                f"[{label}] Campo {idx} mide {fh}px (< {min_field_px:.1f}px a DPI {dpi})"
            )
        assert btn_h >= min_btn_px - 0.5, (
            f"[{label}] Boton principal '{btn_main_txt}' mide {btn_h}px (< {min_btn_px:.1f}px a DPI {dpi})"
        )

        # Fuente de cuerpo Tactil (12pt ~= 16px a 96 DPI)
        lf = get_control_logfont(edits[0])
        assert lf is not None, f"[{label}] Sin fuente en campo de texto"
        expected_lf_h = round(16.0 * scale)
        assert abs(lf.lfHeight) >= expected_lf_h - 1, (
            f"[{label}] Fuente de campo demasiado pequena: lfHeight={lf.lfHeight}, esperado >= {expected_lf_h} (12pt)"
        )

        # Centrado vertical de texto en campo (probado sobre primer campo visible no enmascarado)
        first_wrap = user32.GetParent(edits[0])
        set_edit_text(edits[0], "Prueba")
        time.sleep(0.15)
        ink = measure_ink_vertical_center(first_wrap)
        set_edit_text(edits[0], "")
        time.sleep(0.05)
        assert ink is not None, f"[{label}] No se detecto tinta en campo de texto"
        ink_mid, box_mid = ink
        assert abs(ink_mid - box_mid) <= 3.5 * scale, (
            f"[{label}] Texto no centrado verticalmente: ink_mid={ink_mid:.1f}, box_mid={box_mid:.1f}"
        )

    return {
        "dpi": dpi,
        "scale": scale,
        "dlg_w_log": (dlg_rc[2] - dlg_rc[0]) / scale,
        "dlg_h_log": (dlg_rc[3] - dlg_rc[1]) / scale,
        "field_hs_log": [round(fh / scale, 1) for fh in field_heights],
        "btn_h_log": round(btn_h / scale, 1),
    }


def click_sidebar_section(hwnd_main, section_label):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn = next((h for (h, t) in btns if t == section_label), None)
    assert btn is not None, f"Boton sidebar '{section_label}' no encontrado"
    click_button(btn)
    time.sleep(0.35)


def click_subnav(hwnd_main, sub_label):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn = next((h for (h, t) in btns if t == sub_label), None)
    assert btn is not None, f"Boton subnavegacion '{sub_label}' no encontrado"
    click_button(btn)
    time.sleep(0.45)


def find_header_profile_button(hwnd_main):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and t in ("Touch", "Estándar")]
    assert btns, "Boton de perfil en encabezado no encontrado"
    btn, txt = min(btns, key=lambda item: get_rect(item[0])[1])
    return btn, txt


def find_settings_profile_button(hwnd_main, label_text):
    hdr_btn, _ = find_header_profile_button(hwnd_main)
    hdr_bottom = get_rect(hdr_btn)[3]
    btns = [
        (h, t) for (h, c, t) in enum_children(hwnd_main)
        if c.lower() == "button" and user32.IsWindowVisible(h) and t == label_text and get_rect(h)[1] >= hdr_bottom
    ]
    assert btns, f"Boton opcion '{label_text}' en Configuracion no encontrado"
    btn, _ = max(btns, key=lambda item: get_rect(item[0])[1])
    return btn


def audit_screen_touch_targets(hwnd_main, screen_name, scale):
    """Recopila objetivos tactiles visibles y habilitados en el contenido de la pantalla."""
    children = enum_children(hwnd_main)
    visible_enabled = [
        (h, c, t) for (h, c, t) in children
        if user32.IsWindowVisible(h) and user32.IsWindowEnabled(h)
    ]

    # Mapear wrappers de campos (wxPanel que contiene un unico Edit)
    child_count_by_parent = {}
    for h, _c, _t in children:
        p = user32.GetParent(h)
        child_count_by_parent[p] = child_count_by_parent.get(p, 0) + 1

    items = []
    seen = set()

    for h, c, t in visible_enabled:
        cl = c.lower()
        target_h = h
        kind = None
        label = t.strip()

        if cl == "button":
            kind = "button"
            if not label:
                label = "<button>"
        elif cl == "edit":
            style = user32.GetWindowLongW(h, GWL_STYLE)
            if (style & ES_READONLY) != 0:
                continue
            parent = user32.GetParent(h)
            if parent and parent != hwnd_main and child_count_by_parent.get(parent, 0) == 1:
                p_cls = get_class(parent).lower()
                if p_cls.startswith("wxwindow"):
                    target_h = parent
            kind = "edit"
            label = f"Edit:{label}" if label else "<edit>"
        elif cl == "combobox":
            kind = "combobox"
            label = f"Combo:{label}" if label else "<combobox>"
        elif cl.startswith("wxwindow"):
            # ToggleSwitch o wxOwnerDrawnComboBox (control hoja interactivo con WS_TABSTOP)
            style = user32.GetWindowLongW(h, GWL_STYLE)
            rc_tmp = get_rect(h)
            w_tmp = rc_tmp[2] - rc_tmp[0]
            h_tmp = rc_tmp[3] - rc_tmp[1]
            # Evitar paneles contenedores grandes de fondo
            is_leaf = child_count_by_parent.get(h, 0) == 0
            if (style & WS_TABSTOP) != 0 and (is_leaf or (h_tmp < 80 * scale and w_tmp < 600 * scale)):
                kind = "switch/combo"
                label = label if label else f"<{c}>"

        if kind is None or target_h in seen:
            continue
        seen.add(target_h)

        rc = get_rect(target_h)
        w_log = (rc[2] - rc[0]) / scale
        h_log = (rc[3] - rc[1]) / scale
        small = (w_log < 48.0 - 0.5) or (h_log < 48.0 - 0.5)
        items.append({
            "hwnd": target_h,
            "parent": user32.GetParent(target_h),
            "rect": rc,
            "kind": kind,
            "label": label.replace("\n", " "),
            "w_log": round(w_log, 1),
            "h_log": round(h_log, 1),
            "small": small,
        })

    # Solapamientos entre pares hermanos dentro de los controles auditados
    overlaps = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            if items[i]["parent"] == items[j]["parent"] and rects_overlap(items[i]["rect"], items[j]["rect"]):
                overlaps.append((items[i]["label"], items[j]["label"]))
                items[i]["overlap"] = True
                items[j]["overlap"] = True

    for it in items:
        it.setdefault("overlap", False)

    return items, overlaps


def run_suite(label, exe_path):
    ensure_no_app_running()
    assert os.path.exists(exe_path), f"Ejecutable no encontrado: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    temp_a = tempfile.mkdtemp(prefix=f"cafeteria_t1_{label}_")
    temp_f = tempfile.mkdtemp(prefix=f"cafeteria_t1_owner_touch_{label}_")
    db_path = os.path.join(temp_a, "cafeteria.db")
    cfg_path = os.path.join(temp_a, "station_profile.cfg")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_a

    owner_name = "Dueño T1 Prueba"
    owner_user = "dueno_t1"
    owner_email = "dueno_t1@itch.edu.mx"
    owner_pass = "ClaveSeguraT1!2026"

    procs = []
    metrics = {}

    try:
        print(f"\n=== [{label}] Iniciando suite T1 ({exe_path}) ===")

        # -----------------------------------------------------------------
        # Caso a: Primer arranque sin station_profile.cfg (Alta Dueno Estandar)
        # -----------------------------------------------------------------
        assert not os.path.exists(cfg_path), "station_profile.cfg no debe existir antes del caso a"
        p = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p)

        hwnd_owner_std, _ = wait_for_window(p.pid, "Primer Inicio")
        m_owner_std = check_dialog_geometry(hwnd_owner_std, is_touch=False, expected_edits=5, label=f"{label}:OwnerStd")
        metrics["owner_std"] = m_owner_std
        print(f"[OK][a] Primer Inicio (Estandar): DPI={m_owner_std['dpi']} escala={m_owner_std['scale']:.2f} "
              f"dlg={m_owner_std['dlg_w_log']:.0f}x{m_owner_std['dlg_h_log']:.0f} "
              f"campos={m_owner_std['field_hs_log']} btn={m_owner_std['btn_h_log']}")

        ctrls_o = [c for c in enum_children(hwnd_owner_std) if user32.IsWindowVisible(c[0])]
        o_edits = [h for (h, c, _t) in ctrls_o if c.lower() == "edit"]
        o_btn = next(h for (h, c, _t) in ctrls_o if c.lower() == "button")
        set_edit_text(o_edits[0], owner_name)
        set_edit_text(o_edits[1], owner_user)
        set_edit_text(o_edits[2], owner_email)
        set_edit_text(o_edits[3], owner_pass)
        set_edit_text(o_edits[4], owner_pass)
        time.sleep(0.2)
        post_click_button(o_btn)

        hwnd_main, _ = wait_for_window(p.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        main_dpi = get_window_dpi(hwnd_main)
        scale = main_dpi / 96.0
        time.sleep(0.4)

        # -----------------------------------------------------------------
        # Caso b: BackOffice > Configuracion -> elegir Tactil
        # -----------------------------------------------------------------
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")

        vis_cfg = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
        texts_cfg = [t for (_h, _c, t) in vis_cfg if t]
        assert any("Pantalla de este equipo" in t for t in texts_cfg), "Falta titulo 'Pantalla de este equipo'"
        assert any("ratón y teclado" in t.lower() for t in texts_cfg), "Falta explicacion de Estandar"
        assert any("dedo" in t.lower() for t in texts_cfg), "Falta explicacion de Tactil"
        assert any("solo afecta a este equipo" in t.lower() for t in texts_cfg), "Falta nota de equipo/reinicio"
        assert any("Configuración de Impresora Térmica" in t for t in texts_cfg), "Falta pantalla de impresion incrustada"
        assert any("Guardar Configuración" in t for t in texts_cfg), "Falta boton Guardar Configuración"

        btns_cfg = [(h, t) for (h, c, t) in vis_cfg if c.lower() == "button"]
        assert any("Estándar" in t and "Activa" in t for (_h, t) in btns_cfg), f"Estándar debe estar activa: {btns_cfg}"
        btn_touch_opt = find_settings_profile_button(hwnd_main, "Táctil")

        post_click_button(btn_touch_opt)
        time.sleep(0.6)

        assert os.path.exists(cfg_path), "station_profile.cfg no fue escrito al elegir Táctil"
        with open(cfg_path, "r", encoding="utf-8") as f:
            assert f.read().strip().lower() == "touch", "station_profile.cfg debe contener 'touch'"

        # Los 4 botones laterales >= 56 logicos
        vis_after = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
        for sec in ("Punto de Venta", "Inventario", "Caja y Reportes", "BackOffice"):
            sb = next((h for (h, c, t) in vis_after if c.lower() == "button" and t == sec), None)
            assert sb is not None, f"Falta boton sidebar '{sec}'"
            sr = get_rect(sb)
            assert (sr[3] - sr[1]) >= 56.0 * scale - 0.5, f"Sidebar '{sec}' mide {sr[3]-sr[1]}px (< {56*scale:.1f})"

        _, hdr_txt = find_header_profile_button(hwnd_main)
        assert hdr_txt == "Estándar", f"Boton encabezado debe decir 'Estándar', encontrado '{hdr_txt}'"

        btns_after = [(h, t) for (h, c, t) in vis_after if c.lower() == "button"]
        assert any("Táctil" in t and "Activa" in t for (_h, t) in btns_after), f"Táctil debe quedar activa: {btns_after}"
        assert any("Pantalla de este equipo" in t for (_h, _c, t) in vis_after), "Debe seguir en Configuracion"
        assert any("Configuración de Impresora Térmica" in t for (_h, _c, t) in vis_after), "Impresion debe seguir visible"
        print("[OK][b] BackOffice > Configuracion cambio a Tactil correctamente y conservo la vista.")

        # -----------------------------------------------------------------
        # Caso c: Carrito persistente tras cambio de perfil desde Configuracion
        # -----------------------------------------------------------------
        con = sqlite3.connect(db_path)
        con.execute(
            """
            INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
            VALUES ('prod-t1-01', 'T1-01', '750100009901', 'Cafe T1 Prueba', 'Bebidas', 4500, 0, 1, '2026-11-13 10:00:00')
            """
        )
        con.commit()
        con.close()

        click_sidebar_section(hwnd_main, "Punto de Venta")
        time.sleep(0.3)
        pos_btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
        btn_add = next((h for (h, t) in pos_btns if "Agregar al Carrito" in t), None)
        assert btn_add is not None, "Boton Agregar al Carrito no encontrado"
        click_button(btn_add)
        time.sleep(0.4)

        pos_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        assert len(pos_lists) >= 1 and send_msg(pos_lists[0], LVM_GETITEMCOUNT, 0, 0) == 1, "Linea no aparecio en tabla de Venta"

        con = sqlite3.connect(db_path)
        qty_before = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items WHERE product_id='prod-t1-01'").fetchone()[0]
        con.close()
        assert qty_before == 1, f"SQLite cart_items esperado 1, obtenido {qty_before}"

        # Cambiar perfil a Estandar desde Configuracion
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")
        btn_std_opt = find_settings_profile_button(hwnd_main, "Estándar")
        post_click_button(btn_std_opt)
        time.sleep(0.6)

        click_sidebar_section(hwnd_main, "Punto de Venta")
        time.sleep(0.35)
        pos_lists2 = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        assert len(pos_lists2) >= 1 and send_msg(pos_lists2[0], LVM_GETITEMCOUNT, 0, 0) == 1, "Carrito desaparecio de Venta tras cambio de perfil"
        con = sqlite3.connect(db_path)
        qty_after = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items WHERE product_id='prod-t1-01'").fetchone()[0]
        con.close()
        assert qty_after == 1, f"Carrito desaparecio de SQLite tras cambio de perfil: {qty_after}"
        print("[OK][c] Carrito en pantalla y SQLite conservado tras cambiar perfil desde Configuracion.")

        # -----------------------------------------------------------------
        # Caso d: Volver a Estandar con boton del encabezado estando en Configuracion
        # -----------------------------------------------------------------
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")
        btn_touch_opt2 = find_settings_profile_button(hwnd_main, "Táctil")
        post_click_button(btn_touch_opt2)
        time.sleep(0.6)

        hdr_btn, hdr_label = find_header_profile_button(hwnd_main)
        assert hdr_label == "Estándar", f"Encabezado debia decir 'Estándar', encontrado '{hdr_label}'"
        click_button(hdr_btn)
        time.sleep(0.6)

        vis_d = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
        btns_d = [(h, t) for (h, c, t) in vis_d if c.lower() == "button"]
        assert any("Estándar" in t and "Activa" in t for (_h, t) in btns_d), f"Estándar no quedo marcada en Configuracion: {btns_d}"
        _, hdr_after_d = find_header_profile_button(hwnd_main)
        assert hdr_after_d == "Touch"
        print("[OK][d] Al volver a Estandar con boton del encabezado, Configuracion marco Estandar.")

        # Medir Login en Estandar antes de pasar a Tactil para e
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        p.wait(timeout=6)

        p_ls = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p_ls)
        hwnd_login_std, _ = wait_for_window(p_ls.pid, "Iniciar Sesión")
        m_login_std = check_dialog_geometry(hwnd_login_std, is_touch=False, expected_edits=2, label=f"{label}:LoginStd")
        metrics["login_std"] = m_login_std
        print(f"[OK][StdLogin] Login (Estandar): dlg={m_login_std['dlg_w_log']:.0f}x{m_login_std['dlg_h_log']:.0f} "
              f"campos={m_login_std['field_hs_log']} btn={m_login_std['btn_h_log']}")

        ls_ctrls = [c for c in enum_children(hwnd_login_std) if user32.IsWindowVisible(c[0])]
        ls_edits = [h for (h, c, _t) in ls_ctrls if c.lower() == "edit"]
        ls_btn = next(h for (h, c, _t) in ls_ctrls if c.lower() == "button")
        set_edit_text(ls_edits[0], owner_user)
        set_edit_text(ls_edits[1], owner_pass)
        time.sleep(0.15)
        post_click_button(ls_btn)

        hwnd_main, _ = wait_for_window(p_ls.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        time.sleep(0.35)
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")
        btn_touch_opt3 = find_settings_profile_button(hwnd_main, "Táctil")
        post_click_button(btn_touch_opt3)
        time.sleep(0.6)
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        p_ls.wait(timeout=6)

        # -----------------------------------------------------------------
        # Caso e: Reabrir en Tactil -> Login en Tactil y Configuracion marcada
        # -----------------------------------------------------------------
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p2)
        hwnd_login_touch, _ = wait_for_window(p2.pid, "Iniciar Sesión")
        m_login_touch = check_dialog_geometry(hwnd_login_touch, is_touch=True, expected_edits=2, label=f"{label}:LoginTouch")
        metrics["login_touch"] = m_login_touch
        print(f"[OK][e] Login (Tactil): dlg={m_login_touch['dlg_w_log']:.0f}x{m_login_touch['dlg_h_log']:.0f} "
              f"campos={m_login_touch['field_hs_log']} btn={m_login_touch['btn_h_log']}")

        lt_ctrls = [c for c in enum_children(hwnd_login_touch) if user32.IsWindowVisible(c[0])]
        lt_edits = [h for (h, c, _t) in lt_ctrls if c.lower() == "edit"]
        lt_btn = next(h for (h, c, _t) in lt_ctrls if c.lower() == "button")
        set_edit_text(lt_edits[0], owner_user)
        set_edit_text(lt_edits[1], owner_pass)
        time.sleep(0.15)
        post_click_button(lt_btn)

        hwnd_main2, _ = wait_for_window(p2.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        time.sleep(0.35)
        click_sidebar_section(hwnd_main2, "BackOffice")
        click_subnav(hwnd_main2, "Configuración")
        vis_e = [(h, c, t) for (h, c, t) in enum_children(hwnd_main2) if user32.IsWindowVisible(h)]
        assert any(c.lower() == "button" and "Táctil" in t and "Activa" in t for (_h, c, t) in vis_e), (
            "Tras reabrir en Tactil, Configuracion debe mostrar Tactil marcada"
        )
        print("[OK][e] Tras entrar en Tactil, Configuracion mantiene Tactil marcada.")

        # -----------------------------------------------------------------
        # Caso f: Alta del Dueno en Tactil con station_profile.cfg preexistente
        # -----------------------------------------------------------------
        with open(cfg_path, "rb") as f_cfg:
            touch_cfg_bytes = f_cfg.read()
        with open(os.path.join(temp_f, "station_profile.cfg"), "wb") as f_out:
            f_out.write(touch_cfg_bytes)

        env_f = os.environ.copy()
        env_f["PATH"] = work_dir + ";" + env_f.get("PATH", "")
        env_f["CAFETERIA_DATA_DIR"] = temp_f

        p_f = subprocess.Popen([exe_path], cwd=work_dir, env=env_f)
        procs.append(p_f)
        hwnd_owner_touch, _ = wait_for_window(p_f.pid, "Primer Inicio")
        m_owner_touch = check_dialog_geometry(hwnd_owner_touch, is_touch=True, expected_edits=5, label=f"{label}:OwnerTouch")
        metrics["owner_touch"] = m_owner_touch
        print(f"[OK][f] Primer Inicio (Tactil): dlg={m_owner_touch['dlg_w_log']:.0f}x{m_owner_touch['dlg_h_log']:.0f} "
              f"campos={m_owner_touch['field_hs_log']} btn={m_owner_touch['btn_h_log']}")

        user32.PostMessageW(hwnd_owner_touch, WM_CLOSE, 0, 0)
        p_f.wait(timeout=5)

        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=6)

        print(f"[{label}] Resumen alturas logicas: {metrics}")
        return metrics

    finally:
        for pr in procs:
            if pr.poll() is None:
                try:
                    pr.terminate()
                    pr.wait(timeout=3)
                except Exception:
                    try:
                        pr.kill()
                    except Exception:
                        pass
        for td in (temp_a, temp_f):
            if td and os.path.exists(td):
                import shutil
                shutil.rmtree(td, ignore_errors=True)


def main():
    enable_per_monitor_dpi()
    sys_dpi = get_system_dpi()
    sys_scale = sys_dpi / 96.0
    print(f"DPI sistema detectado: {sys_dpi} (escala={sys_scale:.2f})")

    run_suite("Release", RELEASE_EXE)
    run_suite("Debug", DEBUG_EXE)
    print("\n=== PRUEBA T1 COMPLETA EXITOSA (Release y Debug) ===")


if __name__ == "__main__":
    main()
