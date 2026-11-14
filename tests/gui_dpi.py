#!/usr/bin/env python3
"""
gui_dpi.py - Utilidades compartidas de DPI, medición tipográfica Roboto y Win32 para pruebas GUI.

Garantiza:
- Conciencia DPI por monitor (SetProcessDpiAwarenessContext(-4)) antes de medir o mover ventanas.
- Validación estricta de escalado soportado: únicamente 96 DPI (100%) o 120 DPI (125%).
- Conversión explícita unidades lógicas <-> píxeles físicos (ceil para mínimos, floor para máximos).
- Registro privado de fuentes Roboto reales (assets/fonts) y creación local de HFONT para
  GetTextExtentPoint32W (los handles HFONT obtenidos por WM_GETFONT son locales al proceso
  remoto y fallan al pasarlos a SelectObject desde Python).
- Selección de filas de listas vía LVM_GETITEMRECT sin coordenadas ni alturas fijas.
"""

import ctypes
from ctypes import wintypes
import math
import os
import subprocess
import time

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RELEASE_EXE = os.path.join(REPO_ROOT, "build_release", "app_desktop.exe")
DEBUG_EXE = os.path.join(REPO_ROOT, "build_debug", "app_desktop.exe")
SUPPORTED_DPIS = (96, 120)

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
ULONG_PTR = ctypes.c_size_t

MONITOR_DEFAULTTONEAREST = 2
WM_CLOSE = 0x0010
WM_SETTEXT = 0x000C
WM_GETFONT = 0x0031
WM_COMMAND = 0x0111
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
BM_CLICK = 0x00F5
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
GWL_STYLE = -16
WS_TABSTOP = 0x00010000
WS_HSCROLL = 0x00100000
WS_VSCROLL = 0x00200000
ES_READONLY = 0x0800
SMTO_ABORTIFHUNG = 0x0002
SWP_NOMOVE = 0x0002
SWP_NOZORDER = 0x0004
LVM_GETITEMCOUNT = 0x1004
LVM_GETITEMRECT = 0x100E
LVM_SETITEMSTATE = 0x102B
LVIF_STATE = 0x0008
LVIS_SELECTED = 0x0002
LVIS_FOCUSED = 0x0001
HDM_GETITEMCOUNT = 0x1200
PW_RENDERFULLCONTENT = 0x00000002
FR_PRIVATE = 0x10
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04

VK_RETURN = 0x0D
VK_ESCAPE = 0x1B
VK_DOWN = 0x28

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
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.SetWindowPos.restype = wintypes.BOOL
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
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM
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
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetForegroundWindow.restype = wintypes.BOOL
user32.GetFocus.argtypes = []
user32.GetFocus.restype = wintypes.HWND
user32.keybd_event.argtypes = [wintypes.BYTE, wintypes.BYTE, wintypes.DWORD, ULONG_PTR]
user32.keybd_event.restype = None

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


class LVITEMW(ctypes.Structure):
    _fields_ = [
        ("mask", wintypes.UINT),
        ("iItem", ctypes.c_int),
        ("iSubItem", ctypes.c_int),
        ("state", wintypes.UINT),
        ("stateMask", wintypes.UINT),
        ("pszText", wintypes.LPWSTR),
        ("cchTextMax", ctypes.c_int),
        ("iImage", ctypes.c_int),
        ("lParam", wintypes.LPARAM),
    ]


class SCROLLINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.UINT),
        ("fMask", wintypes.UINT),
        ("nMin", ctypes.c_int),
        ("nMax", ctypes.c_int),
        ("nPage", wintypes.UINT),
        ("nPos", ctypes.c_int),
        ("nTrackPos", ctypes.c_int),
    ]


user32.GetScrollInfo.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.POINTER(SCROLLINFO)]
user32.GetScrollInfo.restype = wintypes.BOOL

_DPI_INITIALIZED = False
_FONTS_REGISTERED = False


def enable_per_monitor_dpi():
    """Declara conciencia DPI Per-Monitor v2 antes de crear o medir ventanas."""
    global _DPI_INITIALIZED
    if _DPI_INITIALIZED:
        return
    try:
        user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        user32.SetProcessDpiAwarenessContext.restype = wintypes.BOOL
        if not user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):
            user32.SetProcessDPIAware()
    except Exception:
        user32.SetProcessDPIAware()
    _DPI_INITIALIZED = True


def get_system_dpi():
    enable_per_monitor_dpi()
    hdc = user32.GetDC(None)
    dpi = gdi32.GetDeviceCaps(hdc, 88)
    user32.ReleaseDC(None, hdc)
    return int(dpi) if dpi > 0 else 96


def get_window_dpi(hwnd):
    enable_per_monitor_dpi()
    if hasattr(user32, "GetDpiForWindow"):
        user32.GetDpiForWindow.argtypes = [wintypes.HWND]
        user32.GetDpiForWindow.restype = wintypes.UINT
        dpi = int(user32.GetDpiForWindow(hwnd))
        if dpi > 0:
            return dpi
    return get_system_dpi()


def validate_dpi(dpi):
    """Acepta únicamente 96 DPI (100%) o 120 DPI (125%); falla con mensaje claro en cualquier otro."""
    if dpi not in SUPPORTED_DPIS:
        pct = round(dpi * 100 / 96)
        raise RuntimeError(
            f"Escalado DPI no soportado para pruebas GUI: {dpi} DPI ({pct}%). "
            f"Solo se admite 96 DPI (100%) o 120 DPI (125%)."
        )
    return dpi


def ensure_supported_system_dpi():
    dpi = validate_dpi(get_system_dpi())
    return dpi, dpi / 96.0


def get_window_scale(hwnd):
    dpi = validate_dpi(get_window_dpi(hwnd))
    return dpi / 96.0


def format_dpi_info(dpi=None):
    if dpi is None:
        dpi = get_system_dpi()
    pct = round(dpi * 100 / 96)
    return f"DPI={dpi} ({pct}%, escala={dpi / 96.0:.2f})"


def to_px(logical, scale):
    """Convierte unidades lógicas (96 DPI) a píxeles físicos redondeados al entero más cercano."""
    return int(round(logical * scale))


def min_px(logical, scale):
    """Convierte un mínimo lógico a píxeles físicos usando ceil (cero relajación)."""
    return int(math.ceil(logical * scale))


def max_px(logical, scale):
    """Convierte un máximo lógico a píxeles físicos usando floor (cero relajación)."""
    return int(math.floor(logical * scale))


def px_to_logical(px, scale):
    return px / scale


def ensure_no_app_running():
    out = subprocess.check_output(
        ["tasklist", "/FI", "IMAGENAME eq app_desktop.exe"],
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if "app_desktop.exe" in out.lower():
        raise RuntimeError(f"Ya existe un proceso app_desktop.exe en ejecución:\n{out}")


def send_msg(hwnd, msg, wparam=0, lparam=0, timeout_ms=2500):
    res = ULONG_PTR()
    ok = user32.SendMessageTimeoutW(
        hwnd, msg, wintypes.WPARAM(wparam), wintypes.LPARAM(lparam),
        SMTO_ABORTIFHUNG, timeout_ms, ctypes.byref(res)
    )
    if not ok:
        raise TimeoutError(f"SendMessageTimeoutW falló en hwnd={hwnd:#x}, msg={msg:#x}")
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
    assert ok, "GetMonitorInfoW falló"
    r = mi.rcWork
    return (r.left, r.top, r.right, r.bottom)


def set_window_logical_size(hwnd, w_log, h_log):
    """Redimensiona el rectángulo externo de hwnd escalando dimensiones lógicas por GetDpiForWindow/96."""
    scale = get_window_scale(hwnd)
    user32.SetWindowPos(
        hwnd, 0, 0, 0, to_px(w_log, scale), to_px(h_log, scale),
        SWP_NOMOVE | SWP_NOZORDER,
    )
    return scale


def move_window_logical(hwnd, x_log, y_log, w_log, h_log):
    """Posiciona y redimensiona el rectángulo externo de hwnd usando unidades lógicas."""
    scale = get_window_scale(hwnd)
    user32.MoveWindow(
        hwnd,
        to_px(x_log, scale),
        to_px(y_log, scale),
        to_px(w_log, scale),
        to_px(h_log, scale),
        True,
    )
    return scale


def set_client_logical_size(hwnd, w_log, h_log, x_log=16, y_log=16):
    """Fija el área cliente exacta de hwnd a (w_log x h_log) unidades lógicas."""
    scale = get_window_scale(hwnd)
    w_rc = get_rect(hwnd)
    c_rc = get_client_screen_rect(hwnd)
    frame_w = (w_rc[2] - w_rc[0]) - (c_rc[2] - c_rc[0])
    frame_h = (w_rc[3] - w_rc[1]) - (c_rc[3] - c_rc[1])
    user32.MoveWindow(
        hwnd,
        to_px(x_log, scale),
        to_px(y_log, scale),
        to_px(w_log, scale) + frame_w,
        to_px(h_log, scale) + frame_h,
        True,
    )
    return scale


def find_windows_by_pid(pid, title_sub="", exclude=()):
    found = []

    @WNDENUMPROC
    def cb(hwnd, _lp):
        if not user32.IsWindowVisible(hwnd):
            return True
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            title = get_text(hwnd)
            if title_sub and title_sub not in title:
                return True
            if any(ex in title for ex in exclude):
                return True
            found.append((hwnd, title))
        return True

    user32.EnumWindows(cb, 0)
    return found


def wait_for_window(pid, title_sub="", timeout=12.0, exclude=()):
    t0 = time.time()
    while time.time() - t0 < timeout:
        wins = find_windows_by_pid(pid, title_sub, exclude)
        if wins:
            return wins[0]
        time.sleep(0.15)
    all_wins = find_windows_by_pid(pid, "", ())
    raise TimeoutError(f"Timeout esperando ventana '{title_sub}' (PID={pid}); visibles={all_wins}")


def enum_children(hwnd_parent):
    out = []

    @WNDENUMPROC
    def cb(hwnd, _lp):
        out.append((hwnd, get_class(hwnd), get_text(hwnd)))
        return True

    user32.EnumChildWindows(hwnd_parent, cb, 0)
    return out


def set_edit_text(hwnd_edit, text):
    buf = ctypes.create_unicode_buffer(text)
    send_msg(hwnd_edit, WM_SETTEXT, 0, ctypes.addressof(buf))


def click_button(hwnd_btn):
    send_msg(hwnd_btn, BM_CLICK, 0, 0)


def post_click_button(hwnd_btn):
    user32.PostMessageW(hwnd_btn, BM_CLICK, 0, 0)


def click_control_at(hwnd, x_log=None, y_log=None):
    """Envía clic izquierdo dentro del control escalando coordenadas lógicas o usando el centro."""
    if x_log is None or y_log is None:
        cw, ch = get_client_size(hwnd)
        px = max(1, cw // 2) if x_log is None else to_px(x_log, get_window_scale(hwnd))
        py = max(1, ch // 2) if y_log is None else to_px(y_log, get_window_scale(hwnd))
    else:
        scale = get_window_scale(hwnd)
        px = to_px(x_log, scale)
        py = to_px(y_log, scale)
    lparam = (py << 16) | (px & 0xFFFF)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)
    time.sleep(0.05)


def get_list_item_rect(hwnd_list, row_index):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    assert hproc, "OpenProcess falló en get_list_item_rect"
    try:
        rem_rc = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(wintypes.RECT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            rc = wintypes.RECT(0, 0, 0, 0)
            written = ctypes.c_size_t()
            kernel32.WriteProcessMemory(hproc, rem_rc, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(written))
            ok = send_msg(hwnd_list, LVM_GETITEMRECT, row_index, rem_rc)
            assert ok, f"LVM_GETITEMRECT falló para fila {row_index}"
            read = ctypes.c_size_t()
            kernel32.ReadProcessMemory(hproc, rem_rc, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(read))
            return (rc.left, rc.top, rc.right, rc.bottom)
        finally:
            kernel32.VirtualFreeEx(hproc, rem_rc, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)


def select_list_row(hwnd_list, row_index, settle=0.35):
    """Selecciona una fila de SysListView32 usando las coordenadas reales de LVM_GETITEMRECT."""
    left, top, right, bottom = get_list_item_rect(hwnd_list, row_index)
    scale = get_window_scale(hwnd_list)
    x = left + min(to_px(40, scale), max(2, (right - left) // 2))
    y = (top + bottom) // 2
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd_list, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_list, WM_LBUTTONUP, 0, lparam)
    time.sleep(settle)


def ensure_local_roboto_fonts():
    """Registra en privado las fuentes Roboto del proyecto para medir texto localmente en Python."""
    global _FONTS_REGISTERED
    if _FONTS_REGISTERED:
        return
    fonts_dir = os.path.join(REPO_ROOT, "assets", "fonts")
    for fname in ("Roboto-Regular.ttf", "Roboto-Medium.ttf", "Roboto-Bold.ttf"):
        fpath = os.path.join(fonts_dir, fname)
        assert os.path.exists(fpath), f"Fuente Roboto requerida no encontrada: {fpath}"
        added = gdi32.AddFontResourceExW(fpath, FR_PRIVATE, None)
        assert added > 0, f"AddFontResourceExW falló para {fpath}"
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
    """
    Mide ancho y alto en píxeles usando un HFONT local creado con LOGFONTW real
    y las fuentes Roboto del proyecto registradas en el proceso de prueba.
    """
    ensure_local_roboto_fonts()
    hdc = user32.GetDC(hwnd_for_font)
    assert hdc, f"GetDC falló en hwnd={hwnd_for_font:#x}"
    lf = get_control_logfont(hwnd_for_font)
    if lf is None:
        dpi = get_window_dpi(hwnd_for_font)
        lf = LOGFONTW()
        lf.lfHeight = -round(11 * dpi / 72.0)
        lf.lfWeight = 400
        lf.lfFaceName = "Roboto"
    hf_local = gdi32.CreateFontIndirectW(ctypes.byref(lf))
    assert hf_local, f"CreateFontIndirectW falló para '{lf.lfFaceName}' h={lf.lfHeight}"
    old = gdi32.SelectObject(hdc, hf_local)
    assert old, f"SelectObject falló para '{lf.lfFaceName}'"
    sz = wintypes.SIZE()
    try:
        gdi32.GetTextExtentPoint32W(hdc, text, len(text), ctypes.byref(sz))
    finally:
        gdi32.SelectObject(hdc, old)
        gdi32.DeleteObject(hf_local)
        user32.ReleaseDC(hwnd_for_font, hdc)
    return sz.cx, sz.cy


def capture_window_pixels(hwnd):
    """Captura píxeles BGRA (uint32) del rectángulo completo de hwnd mediante PrintWindow."""
    rc = get_rect(hwnd)
    w = rc[2] - rc[0]
    h = rc[3] - rc[1]
    if w <= 0 or h <= 0:
        return 0, 0, []
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
        raw = list((ctypes.c_uint32 * (w * h)).from_address(bits.value))
        return w, h, raw
    finally:
        gdi32.SelectObject(hdc_mem, old)
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(hwnd, hdc_win)


def measure_ink_vertical_center(hwnd):
    """Devuelve (ink_mid, box_mid) en píxeles de ventana, ignorando bordes escalados por DPI."""
    scale = get_window_scale(hwnd)
    w, h, raw = capture_window_pixels(hwnd)
    min_dim = to_px(4, scale)
    if w <= min_dim or h <= min_dim or not raw:
        return None
    bx = min(w - 1, max(1, to_px(2, scale)))
    by = min(h - 1, max(1, to_px(2, scale)))
    bg = raw[by * w + bx]
    bg_r, bg_g, bg_b = (bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF
    y0 = min(h // 2, max(1, to_px(2, scale)))
    y1 = max(y0, h - y0)
    x0 = min(w // 2, max(1, to_px(4, scale)))
    x1 = max(x0, w - x0)
    min_y, max_y = h, -1
    for y in range(y0, y1):
        row_off = y * w
        for x in range(x0, x1):
            px = raw[row_off + x]
            r, g, b = (px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF
            if abs(r - bg_r) + abs(g - bg_g) + abs(b - bg_b) > 45:
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y
    if max_y < min_y:
        return None
    return (min_y + max_y) / 2.0, h / 2.0


def rects_overlap(r1, r2, min_overlap_px=2):
    ix1 = max(r1[0], r2[0])
    iy1 = max(r1[1], r2[1])
    ix2 = min(r1[2], r2[2])
    iy2 = min(r1[3], r2[3])
    return (ix2 - ix1) >= min_overlap_px and (iy2 - iy1) >= min_overlap_px


def is_descendant(hwnd, ancestor):
    cur = hwnd
    while cur:
        if cur == ancestor:
            return True
        cur = user32.GetParent(cur)
    return False


def click_sidebar_section(hwnd_main, section_label, settle=0.45):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn = next((h for (h, t) in btns if t == section_label), None)
    assert btn is not None, f"Botón sidebar '{section_label}' no encontrado"
    click_button(btn)
    time.sleep(settle)


def click_subnav(hwnd_main, sub_label, settle=0.45):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn = next((h for (h, t) in btns if t == sub_label), None)
    assert btn is not None, f"Botón subnavegación '{sub_label}' no encontrado"
    click_button(btn)
    time.sleep(settle)


# Declarar conciencia DPI en cuanto cualquier prueba importe este módulo.
enable_per_monitor_dpi()
