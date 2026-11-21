#!/usr/bin/env python3
"""
measure_fullscreen_space.py - Medición objetiva de aprovechamiento de espacio en pantalla completa (T16 / I7 Fase 1).

Ejecuta build_release/app_desktop.exe en modo pantalla completa (station_fullscreen.cfg)
con SQLite aislado y datos deterministas, en perfil Estándar y perfil Táctil, recorriendo
todas las subsecciones reales. Guarda capturas en qa_visual/fullscreen/ y escribe
qa_visual/fullscreen/medidas.txt con una fila por pantalla y perfil.
"""

import ctypes
from ctypes import wintypes
import datetime
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gui_dpi import (
    user32,
    gdi32,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    format_dpi_info,
    get_window_dpi,
    get_window_scale,
    measure_text_extent,
    min_px,
    select_list_row,
)

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumWindows.restype = wintypes.BOOL
user32.EnumChildWindows.argtypes = [wintypes.HWND, WNDENUMPROC, wintypes.LPARAM]
user32.EnumChildWindows.restype = wintypes.BOOL
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindowVisible.restype = wintypes.BOOL
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongW.restype = ctypes.c_long
user32.GetWindowDC.argtypes = [wintypes.HWND]
user32.GetWindowDC.restype = wintypes.HDC
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.PrintWindow.restype = wintypes.BOOL
user32.MonitorFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD]
user32.MonitorFromWindow.restype = wintypes.HMONITOR

gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.GetDIBits.argtypes = [
    wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
    ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT,
]
gdi32.GetDIBits.restype = ctypes.c_int

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

GWL_STYLE = -16
WS_CAPTION = 0x00C00000
WS_VSCROLL = 0x00200000
WS_HSCROLL = 0x00100000
WM_SETTEXT = 0x000C
WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
BM_CLICK = 0x00F5
MONITOR_DEFAULTTONEAREST = 2

LVM_FIRST = 0x1000
LVM_GETITEMCOUNT = LVM_FIRST + 4
LVM_GETITEMRECT = LVM_FIRST + 14
LVM_GETCOLUMNWIDTH = LVM_FIRST + 29
LVM_GETHEADER = LVM_FIRST + 31
LVM_GETITEMTEXTW = LVM_FIRST + 115
LVIR_BOUNDS = 0

HDM_FIRST = 0x1200
HDM_GETITEMCOUNT = HDM_FIRST + 0
HDM_GETITEMW = HDM_FIRST + 11
HDI_TEXT = 0x0002

PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04

BG_RGB = (245, 245, 247)
BG_TOL = 6


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
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
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
        ("hbm", wintypes.HANDLE),
        ("cchTextMax", ctypes.c_int),
        ("fmt", ctypes.c_int),
        ("lParam", ctypes.c_ssize_t),
        ("iImage", ctypes.c_int),
        ("iOrder", ctypes.c_int),
        ("type", wintypes.UINT),
        ("pvFilter", ctypes.c_void_p),
        ("state", wintypes.UINT),
    ]


def get_window_pid(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


def get_rect(hwnd):
    rc = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rc))
    return (rc.left, rc.top, rc.right, rc.bottom)


def get_text(hwnd):
    ln = user32.GetWindowTextLengthW(hwnd)
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    user32.GetWindowTextW(hwnd, buf, ln + 2)
    return buf.value


def get_class(hwnd):
    buf = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, buf, 128)
    return buf.value


def find_windows_by_pid(pid, title_sub="", exclude=None):
    found = []

    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd) and get_window_pid(hwnd) == pid:
            t = get_text(hwnd)
            if exclude and any(ex.lower() in t.lower() for ex in exclude):
                return True
            if not title_sub or title_sub.lower() in t.lower():
                found.append((hwnd, t))
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def wait_window(pid, title_sub, exclude=None, timeout=12.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        wins = find_windows_by_pid(pid, title_sub, exclude)
        if wins:
            return wins[0]
        time.sleep(0.15)
    raise TimeoutError(f"Timeout esperando ventana '{title_sub}' (PID {pid})")


def enum_children(parent_hwnd):
    out = []

    def cb(hwnd, _):
        out.append((hwnd, get_class(hwnd), get_text(hwnd)))
        return True

    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(cb), 0)
    return out


def set_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(buf))


def send_click(hwnd):
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)


def post_click(hwnd):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)


def get_monitor_size(hwnd):
    hmon = user32.MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    user32.GetMonitorInfoW(hmon, ctypes.byref(mi))
    r = mi.rcMonitor
    return (r.right - r.left, r.bottom - r.top, (r.left, r.top, r.right, r.bottom))


def capture_window_img(hwnd, out_path):
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.25)
    l, t, r, b = get_rect(hwnd)
    w, h = r - l, b - t
    assert w > 0 and h > 0, f"Rect inválido {w}x{h}"
    hdc = user32.GetWindowDC(hwnd)
    mem_dc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mem_dc, bmp)
    user32.PrintWindow(hwnd, mem_dc, 2)
    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = w
    bmi.bmiHeader.biHeight = -h
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem_dc, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    gdi32.SelectObject(mem_dc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(hwnd, hdc)
    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    return img, (l, t, r, b)


def find_content_container(hwnd_main):
    """Localiza content_container_ (hijo directo de MainWindow a la derecha de sidebar y debajo de subnav)."""
    ml, mt, mr, mb = get_rect(hwnd_main)
    direct = []
    for h, cls, txt in enum_children(hwnd_main):
        if user32.GetParent(h) == hwnd_main and user32.IsWindowVisible(h):
            r = get_rect(h)
            w, hh = r[2] - r[0], r[3] - r[1]
            if w > 0 and hh > 0:
                direct.append((h, cls, r, w * hh))
    # El contenedor de contenido está en el cuadrante inferior-derecho (left > ml + 100, top > mt + 80)
    candidates = [d for d in direct if d[2][0] > ml + 100 and d[2][1] > mt + 80]
    assert candidates, f"No se encontró content_container_ entre hijos directos: {direct}"
    best = max(candidates, key=lambda d: d[3])
    return best[0], best[2]


def read_listview_texts(hwnd_list, max_rows=15):
    """Lee encabezados y celdas de un SysListView32 en otro proceso."""
    pid = get_window_pid(hwnd_list)
    h_proc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid)
    if not h_proc:
        return [], [], []
    try:
        hdr = user32.SendMessageW(hwnd_list, LVM_GETHEADER, 0, 0)
        col_count = user32.SendMessageW(hdr, HDM_GETITEMCOUNT, 0, 0) if hdr else 0
        row_count = user32.SendMessageW(hwnd_list, LVM_GETITEMCOUNT, 0, 0)

        buf_sz = 1024
        remote_struct = kernel32.VirtualAllocEx(h_proc, None, 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        remote_text = kernel32.VirtualAllocEx(h_proc, None, buf_sz * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        if not remote_struct or not remote_text:
            return [], [], []

        headers = []
        col_widths = []
        try:
            for c in range(col_count):
                col_widths.append(user32.SendMessageW(hwnd_list, LVM_GETCOLUMNWIDTH, c, 0))
                hdi = HDITEMW()
                hdi.mask = HDI_TEXT
                hdi.pszText = remote_text
                hdi.cchTextMax = buf_sz
                written = ctypes.c_size_t(0)
                kernel32.WriteProcessMemory(h_proc, remote_struct, ctypes.byref(hdi), ctypes.sizeof(hdi), ctypes.byref(written))
                user32.SendMessageW(hdr, HDM_GETITEMW, c, remote_struct)
                local_buf = ctypes.create_unicode_buffer(buf_sz)
                read_n = ctypes.c_size_t(0)
                kernel32.ReadProcessMemory(h_proc, remote_text, local_buf, buf_sz * 2, ctypes.byref(read_n))
                headers.append(local_buf.value)

            rows = []
            for r in range(min(row_count, max_rows)):
                row_vals = []
                for c in range(col_count):
                    lvi = LVITEMW()
                    lvi.iSubItem = c
                    lvi.pszText = remote_text
                    lvi.cchTextMax = buf_sz
                    written = ctypes.c_size_t(0)
                    kernel32.WriteProcessMemory(h_proc, remote_struct, ctypes.byref(lvi), ctypes.sizeof(lvi), ctypes.byref(written))
                    user32.SendMessageW(hwnd_list, LVM_GETITEMTEXTW, r, remote_struct)
                    local_buf = ctypes.create_unicode_buffer(buf_sz)
                    read_n = ctypes.c_size_t(0)
                    kernel32.ReadProcessMemory(h_proc, remote_text, local_buf, buf_sz * 2, ctypes.byref(read_n))
                    row_vals.append(local_buf.value)
                rows.append(row_vals)

            # Medir alto de fila 0 vía LVM_GETITEMRECT
            row_h = 0
            if row_count > 0:
                rc = wintypes.RECT()
                rc.left = LVIR_BOUNDS
                written = ctypes.c_size_t(0)
                kernel32.WriteProcessMemory(h_proc, remote_struct, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(written))
                if user32.SendMessageW(hwnd_list, LVM_GETITEMRECT, 0, remote_struct):
                    read_n = ctypes.c_size_t(0)
                    kernel32.ReadProcessMemory(h_proc, remote_struct, ctypes.byref(rc), ctypes.sizeof(rc), ctypes.byref(read_n))
                    row_h = rc.bottom - rc.top
            return headers, col_widths, rows, row_count, row_h, hdr
        finally:
            kernel32.VirtualFreeEx(h_proc, remote_struct, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(h_proc, remote_text, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(h_proc)


def is_descendant(hwnd, ancestor):
    cur = user32.GetParent(hwnd)
    while cur:
        if cur == ancestor:
            return True
        cur = user32.GetParent(cur)
    return False


def longest_false_run(arr):
    best = 0
    cur = 0
    for v in arr:
        if not v:
            cur += 1
            if cur > best:
                best = cur
        else:
            cur = 0
    return best


def measure_view(hwnd_main, img, win_rect, profile, screen_id, scale):
    content_hwnd, c_rect = find_content_container(hwnd_main)
    cl, ct, cr, cb = c_rect
    cw, ch = cr - cl, cb - ct

    all_children = enum_children(content_hwnd)
    # Determinar qué controles tienen hijos visibles (contenedores puros)
    parents_with_vis_child = set()
    vis_children = []
    for h, cls, txt in all_children:
        if user32.IsWindowVisible(h):
            r = get_rect(h)
            if r[2] > r[0] and r[3] > r[1]:
                vis_children.append((h, cls, txt, r))
                parents_with_vis_child.add(user32.GetParent(h))

    LEAF_CLASSES = {
        "button", "edit", "static", "syslistview32", "combobox", "listbox",
        "msctls_progress32", "sysdatetimepick32", "sysheader32",
    }

    leaf_rects = []
    list_summaries = []
    scrolled_summaries = []

    for h, cls, txt, r in vis_children:
        cls_l = cls.lower()
        # Recortar al rectángulo de content_container_
        xl = max(cl, r[0])
        yt = max(ct, r[1])
        xr = min(cr, r[2])
        yb = min(cb, r[3])
        if xr <= xl or yb <= yt:
            continue

        is_leaf = cls_l in LEAF_CLASSES
        if not is_leaf and cls_l == "wxwindownr":
            has_kids = h in parents_with_vis_child
            w, hh = r[2] - r[0], r[3] - r[1]
            # Controles personalizados hoja (ToggleSwitch, TouchComboBox, preview de imagen)
            if not has_kids and w < cw * 0.6 and hh < ch * 0.6:
                is_leaf = True
        if is_leaf:
            leaf_rects.append((xl - cl, yt - ct, xr - cl, yb - ct))

        # Medir SysListView32
        if cls_l == "syslistview32":
            res = read_listview_texts(h)
            if len(res) == 6:
                headers, col_ws, rows, item_cnt, row_h, hdr_hwnd = res
                hdr_h = 0
                if hdr_hwnd:
                    hr = get_rect(hdr_hwnd)
                    hdr_h = max(0, hr[3] - hr[1])
                crc = wintypes.RECT()
                user32.GetClientRect(h, ctypes.byref(crc))
                body_h = max(0, (crc.bottom - crc.top) - hdr_h)
                eff_row_h = row_h if row_h > 0 else min_px(24, scale)
                vis_rows = body_h // eff_row_h if eff_row_h > 0 else 0

                pad = min_px(12, scale)
                spare_thr = min_px(40, scale)
                clipped_cols = []
                spare_cols = []
                for idx, cw_col in enumerate(col_ws):
                    h_txt = headers[idx] if idx < len(headers) else f"c{idx}"
                    max_tw = measure_text_extent(hdr_hwnd, h_txt)[0] + pad if (hdr_hwnd and h_txt) else 0
                    for row in rows:
                        if idx < len(row) and row[idx]:
                            tw = measure_text_extent(h, row[idx])[0] + pad
                            if tw > max_tw:
                                max_tw = tw
                    if max_tw > cw_col:
                        clipped_cols.append(f"{h_txt}({max_tw}>{cw_col})")
                    elif cw_col - max_tw >= spare_thr:
                        spare_cols.append(f"{h_txt}(+{cw_col - max_tw})")

                clip_vs_spare = ""
                if clipped_cols and spare_cols:
                    clip_vs_spare = f"CLIP[{','.join(clipped_cols)}]/SOBRA[{','.join(spare_cols)}]"
                elif clipped_cols:
                    clip_vs_spare = f"CLIP[{','.join(clipped_cols)}]"
                else:
                    clip_vs_spare = "OK"
                list_cw = crc.right - crc.left
                sum_col_w = sum(col_ws)
                fill_pct = (100.0 * sum_col_w / list_cw) if list_cw > 0 else 0.0
                list_summaries.append(f"LV({body_h}px,{vis_rows}f/{item_cnt}it,cols={fill_pct:.0f}%,{clip_vs_spare})")

        # Medir contenedores con scroll vertical (p.ej. lista de permisos en Roles)
        if cls_l == "wxwindownr":
            st = user32.GetWindowLongW(h, GWL_STYLE)
            if st & WS_VSCROLL:
                # Contar controles hijos visibles dentro del viewport de este scroll
                sh = yb - yt
                in_view = 0
                total_toggle = 0
                for ch_h, ch_cls, _ch_t, ch_r in vis_children:
                    if is_descendant(ch_h, h) and ch_cls.lower() == "wxwindownr" and ch_h not in parents_with_vis_child:
                        cw_k = ch_r[2] - ch_r[0]
                        ch_k = ch_r[3] - ch_r[1]
                        if 24 <= cw_k <= 120 and 16 <= ch_k <= 80:
                            total_toggle += 1
                            if ch_r[1] >= yt and ch_r[3] <= yb:
                                in_view += 1
                if total_toggle > 0:
                    scrolled_summaries.append(f"SCROLL({sh}px,{in_view}/{total_toggle}toggles)")

    # Unión de rectángulos y franjas vacías (control-rect)
    if leaf_rects:
        u_l = min(x0 for (x0, _, _, _) in leaf_rects)
        u_t = min(y0 for (_, y0, _, _) in leaf_rects)
        u_r = max(x1 for (_, _, x1, _) in leaf_rects)
        u_b = max(y1 for (_, _, _, y1) in leaf_rects)
        uw, uh = u_r - u_l, u_b - u_t
    else:
        uw, uh = 0, 0

    uw_pct = 100.0 * uw / cw if cw > 0 else 0.0
    uh_pct = 100.0 * uh / ch if ch > 0 else 0.0
    u_area_pct = 100.0 * (uw * uh) / (cw * ch) if (cw > 0 and ch > 0) else 0.0

    x_cov = [False] * cw
    y_cov = [False] * ch
    for x0, y0, x1, y1 in leaf_rects:
        for x in range(max(0, x0), min(cw, x1)):
            x_cov[x] = True
        for y in range(max(0, y0), min(ch, y1)):
            y_cov[y] = True

    max_h_gap = longest_false_run(x_cov)
    max_v_gap = longest_false_run(y_cov)
    max_h_gap_pct = 100.0 * max_h_gap / cw if cw > 0 else 0.0
    max_v_gap_pct = 100.0 * max_v_gap / ch if ch > 0 else 0.0

    # Medición PIL en el área de contenido
    wl, wt, _wr, _wb = win_rect
    crop_box = (max(0, cl - wl), max(0, ct - wt), min(img.width, cr - wl), min(img.height, cb - wt))
    crop = img.crop(crop_box)
    pw, ph = crop.size
    px = crop.load()
    non_bg = 0
    col_has_fg = [False] * pw
    row_has_fg = [False] * ph
    # Muestreo con paso 2 para rapidez y determinismo exacto
    step = 2
    sampled = 0
    for y in range(0, ph, step):
        for x in range(0, pw, step):
            sampled += 1
            r0, g0, b0 = px[x, y]
            if abs(r0 - BG_RGB[0]) > BG_TOL or abs(g0 - BG_RGB[1]) > BG_TOL or abs(b0 - BG_RGB[2]) > BG_TOL:
                non_bg += 1
                col_has_fg[x] = True
                if x + 1 < pw:
                    col_has_fg[x + 1] = True
                row_has_fg[y] = True
                if y + 1 < ph:
                    row_has_fg[y + 1] = True

    pil_non_bg_pct = 100.0 * non_bg / sampled if sampled > 0 else 0.0
    pil_h_bg = longest_false_run(col_has_fg)
    pil_v_bg = longest_false_run(row_has_fg)

    extra_lists = "; ".join(list_summaries + scrolled_summaries) if (list_summaries or scrolled_summaries) else "-"

    return {
        "profile": profile,
        "screen": screen_id,
        "content_w": cw,
        "content_h": ch,
        "union_w": uw,
        "union_h": uh,
        "union_w_pct": uw_pct,
        "union_h_pct": uh_pct,
        "union_area_pct": u_area_pct,
        "max_h_gap": max_h_gap,
        "max_h_gap_pct": max_h_gap_pct,
        "max_v_gap": max_v_gap,
        "max_v_gap_pct": max_v_gap_pct,
        "pil_non_bg_pct": pil_non_bg_pct,
        "pil_h_bg": pil_h_bg,
        "pil_v_bg": pil_v_bg,
        "lists": extra_lists,
    }


def seed_rich_data(db_path):
    """Siembra datos deterministas suficientes para llenar todas las listas y pantallas."""
    today = datetime.date.today().strftime("%Y-%m-%d")
    yesterday = (datetime.date.today() - datetime.timedelta(days=1)).strftime("%Y-%m-%d")
    con = sqlite3.connect(db_path)
    cur = con.cursor()

    owner_row = cur.execute("SELECT id, password_hash FROM users LIMIT 1").fetchone()
    owner_id, owner_hash = owner_row[0], owner_row[1]

    # Usuarios y rol personalizado
    cur.execute(
        """
        INSERT OR IGNORE INTO roles (id, name, description, is_system)
        VALUES ('role-encargado', 'Encargado de Turno', 'Supervisa inventario y cortes', 0);
        """
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
        VALUES
            ('usr-cajero-fs', 'cajero_fs', 'Carlos Cajero Matutino', 'cajero@itch.edu.mx', ?, 'role-cashier', 1, '2026-11-01 08:00:00'),
            ('usr-enc-fs', 'encargado_fs', 'Elena Encargada Barra', 'elena@itch.edu.mx', ?, 'role-encargado', 1, '2026-11-01 08:05:00');
        """,
        (owner_hash, owner_hash),
    )

    # Ingredientes (8)
    ingredients = [
        ("ing-01", "Pan Baguette Artesanal Grande", "COUNT", "pz", 40, 10, 400000000),
        ("ing-02", "Jamón Serrano Seleccionado", "MASS", "mg", 4000000, 500000, 1200000000),
        ("ing-03", "Queso Gouda Madurado", "MASS", "mg", 3000000, 300000, 900000000),
        ("ing-04", "Café Pluma Hidalgo de Grano", "MASS", "mg", 5000000, 500000, 1500000000),
        ("ing-05", "Leche Deslactosada Entera", "VOLUME", "ml", 12000, 2000, 360000000),
        ("ing-06", "Jarabe de Vainilla Francesa", "VOLUME", "ml", 3000, 500, 450000000),
        ("ing-07", "Pechuga de Pollo Asada", "MASS", "mg", 2500000, 400000, 750000000),
        ("ing-08", "Limón Persa Fresco", "MASS", "mg", 1000000, 200000, 50000000),
    ]
    for iid, name, dim, unit, stk, thr, val in ingredients:
        cur.execute(
            """
            INSERT OR IGNORE INTO ingredients
                (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold, stock_value_micro)
            VALUES (?, ?, ?, ?, ?, ?, ?);
            """,
            (iid, name, dim, unit, stk, thr, val),
        )

    # Productos (8: preparados, directos y combos)
    products = [
        ("prod-01", "BAG-01", "750100002001", "Baguette Serrano y Queso Gouda", "Alimentos", 6500, 1, 1, 0),
        ("prod-02", "CAF-01", "750100002002", "Café Americano Pluma 12oz", "Bebidas", 3500, 1, 1, 0),
        ("prod-03", "CAP-01", "750100002003", "Capuchino Vainilla Deslactosado 16oz", "Bebidas", 4800, 1, 1, 0),
        ("prod-04", "TOR-01", "750100002004", "Torta Especial de Pollo Asado", "Alimentos", 5800, 1, 1, 0),
        ("prod-05", "AGU-01", "750100002005", "Agua Embotellada Natural 600ml", "Bebidas", 1800, 0, 1, 0),
        ("prod-06", "GAL-01", "750100002006", "Galleta Artesanal Chispas Chocolate", "Postres", 2500, 0, 1, 0),
        ("prod-07", "CMB-01", "750100002007", "Combo Desayuno Baguette y Café", "Combos", 8900, 0, 1, 1),
        ("prod-08", "CMB-02", "750100002008", "Combo Tarde Capuchino y Galleta", "Combos", 6500, 0, 1, 1),
    ]
    for pid, sku, bc, name, cat, price, prep, act, combo in products:
        cur.execute(
            """
            INSERT OR IGNORE INTO products
                (id, sku, barcode, name, category, price_cents, is_prepared, is_active, is_combo, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, '2026-11-01 08:00:00');
            """,
            (pid, sku, bc, name, cat, price, prep, act, combo),
        )

    # Recetas
    cur.execute(
        """
        INSERT OR IGNORE INTO recipes (id, product_id, version, created_at)
        VALUES
            ('rec-01', 'prod-01', 1, '2026-11-01 08:00:00'),
            ('rec-02', 'prod-02', 1, '2026-11-01 08:00:00'),
            ('rec-03', 'prod-03', 1, '2026-11-01 08:00:00'),
            ('rec-04', 'prod-04', 1, '2026-11-01 08:00:00');
        """
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units)
        VALUES
            ('ri-01', 'rec-01', 'ing-01', 1),
            ('ri-02', 'rec-01', 'ing-02', 60000),
            ('ri-03', 'rec-01', 'ing-03', 40000),
            ('ri-04', 'rec-02', 'ing-04', 18000),
            ('ri-05', 'rec-03', 'ing-04', 18000),
            ('ri-06', 'rec-03', 'ing-05', 250),
            ('ri-07', 'rec-03', 'ing-06', 30),
            ('ri-08', 'rec-04', 'ing-01', 1),
            ('ri-09', 'rec-04', 'ing-07', 80000);
        """
    )

    # Combos
    cur.execute(
        """
        INSERT OR IGNORE INTO combo_items (combo_product_id, component_product_id, quantity)
        VALUES
            ('prod-07', 'prod-01', 1),
            ('prod-07', 'prod-02', 1),
            ('prod-08', 'prod-03', 1),
            ('prod-08', 'prod-06', 1);
        """
    )

    # Promociones (4)
    cur.execute(
        """
        INSERT OR IGNORE INTO promotions
            (id, name, kind, percent_bp, fixed_off_cents, buy_qty, pay_qty, scope_kind, priority, is_active, created_at, updated_at)
        VALUES
            ('promo-01', '15% en Bebidas Calientes', 'PERCENT', 1500, 0, 0, 0, 'CATEGORY', 10, 1, '2026-11-01 08:00:00', '2026-11-01 08:00:00'),
            ('promo-02', '2x1 en Galletas Artesanales', 'NXM', 0, 0, 2, 1, 'PRODUCT', 20, 1, '2026-11-01 08:00:00', '2026-11-01 08:00:00'),
            ('promo-03', '$10 menos en Baguette Serrano', 'FIXED', 0, 1000, 0, 0, 'PRODUCT', 15, 1, '2026-11-01 08:00:00', '2026-11-01 08:00:00'),
            ('promo-04', '10% en Alimentos de Barra', 'PERCENT', 1000, 0, 0, 0, 'CATEGORY', 5, 0, '2026-11-01 08:00:00', '2026-11-01 08:00:00');
        """
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO promotion_targets (promotion_id, product_id, category)
        VALUES
            ('promo-01', NULL, 'Bebidas'),
            ('promo-02', 'prod-06', NULL),
            ('promo-03', 'prod-01', NULL),
            ('promo-04', NULL, 'Alimentos');
        """
    )

    # Caja y turnos: 1 cerrado ayer + 1 abierto hoy
    reg_row = cur.execute("SELECT id FROM cash_registers LIMIT 1").fetchone()
    reg_id = reg_row[0] if reg_row else "reg-main"
    cur.execute(
        """
        INSERT OR IGNORE INTO shifts
            (id, register_id, cashier_user_id, opened_at, closed_at, initial_cash_cents, final_cash_cents, is_open,
             closed_by_user_id, closeout_saved, snap_sales_count, snap_gross_cents, snap_discount_cents,
             snap_cash_sales_cents, snap_card_sales_cents, snap_transfer_sales_cents,
             snap_inflow_cents, snap_outflow_cents, expected_cash_cents, difference_cents, difference_reason)
        VALUES
            ('shift-closed-01', ?, ?, ?, ?, 50000, 78000, 0,
             ?, 1, 3, 24000, 1000,
             23000, 0, 0,
             10000, 5000, 78000, 0, ''),
            ('shift-open-01', ?, ?, ?, NULL, 50000, NULL, 1,
             NULL, 0, 0, 0, 0,
             0, 0, 0,
             0, 0, NULL, NULL, '');
        """,
        (
            reg_id, owner_id, f"{yesterday} 08:00:00", f"{yesterday} 16:00:00", owner_id,
            reg_id, owner_id, f"{today} 08:00:00",
        ),
    )

    # Movimientos de caja en ambos turnos
    cur.execute(
        """
        INSERT OR IGNORE INTO cash_movements (id, shift_id, movement_type, amount_cents, description, created_at)
        VALUES
            ('cm-01', 'shift-closed-01', 'INFLOW', 10000, 'Fondo adicional cambio billetes', ?),
            ('cm-02', 'shift-closed-01', 'OUTFLOW', 5000, 'Compra de hielo y servilletas', ?),
            ('cm-03', 'shift-open-01', 'INFLOW', 15000, 'Cambio matutino monedas', ?),
            ('cm-04', 'shift-open-01', 'OUTFLOW', 4000, 'Compra de insumos urgentes', ?),
            ('cm-05', 'shift-open-01', 'SALE', 10000, 'Venta V-C1-00001', ?);
        """,
        (f"{yesterday} 10:00:00", f"{yesterday} 13:00:00", f"{today} 08:30:00", f"{today} 09:15:00", f"{today} 09:30:00"),
    )

    # Ventas hoy para Reportes
    cur.execute(
        """
        INSERT OR IGNORE INTO sales
            (id, folio, shift_id, user_id, discount_cents, total_cents, created_at, idempotency_key, cashier_name)
        VALUES
            ('sale-01', 'V-C1-00001', 'shift-open-01', ?, 0, 10000, ?, 'idem-fs-01', 'Administrador General'),
            ('sale-02', 'V-C1-00002', 'shift-open-01', ?, 0, 9600, ?, 'idem-fs-02', 'Administrador General');
        """,
        (owner_id, f"{today} 09:30:00", owner_id, f"{today} 10:15:00"),
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents, discount_cents)
        VALUES
            ('si-01', 'sale-01', 'prod-01', 'Baguette Serrano y Queso Gouda', 1, 6500, 6500, 0),
            ('si-02', 'sale-01', 'prod-02', 'Café Americano Pluma 12oz', 1, 3500, 3500, 0),
            ('si-03', 'sale-02', 'prod-03', 'Capuchino Vainilla Deslactosado 16oz', 2, 4800, 9600, 0);
        """
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO payment_records (id, sale_id, payment_method, amount_cents, cash_received_cents, change_cents, created_at)
        VALUES
            ('pay-01', 'sale-01', 'EFECTIVO', 10000, 10000, 0, ?),
            ('pay-02', 'sale-02', 'TARJETA', 9600, 0, 0, ?);
        """,
        (f"{today} 09:30:00", f"{today} 10:15:00"),
    )

    # Stock movements y compras para Existencias / Barra / Reabastecimiento
    cur.execute(
        """
        INSERT OR IGNORE INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at, actor_user_id)
        VALUES
            ('sm-01', 'ing-01', 'PURCHASE', 20, 'Compra panadería', ?, ?),
            ('sm-02', 'ing-04', 'PURCHASE', 1000000, 'Compra café grano', ?, ?);
        """,
        (f"{today} 08:10:00", owner_id, f"{today} 08:15:00", owner_id),
    )
    cur.execute(
        """
        INSERT OR IGNORE INTO purchase_entries
            (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at, actor_user_id)
        VALUES
            ('pe-01', 'ing-01', 20, 'pz', 18000, 20, ?, ?),
            ('pe-02', 'ing-04', 1, 'kg', 32000, 1000000, ?, ?),
            ('pe-03', 'ing-05', 6, 'l', 18000, 6000, ?, ?);
        """,
        (f"{today} 08:10:00", owner_id, f"{today} 08:15:00", owner_id, f"{today} 08:20:00", owner_id),
    )

    con.commit()
    con.close()


def click_button_text(hwnd_main, text_match, exact=False, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        for h, cls, txt in enum_children(hwnd_main):
            if cls.lower() == "button" and user32.IsWindowVisible(h):
                if (txt == text_match) if exact else (text_match in txt):
                    send_click(h)
                    time.sleep(0.35)
                    return h
        time.sleep(0.1)
    raise RuntimeError(f"Botón '{text_match}' no encontrado en {timeout}s")


def wait_content_list(hwnd_main, timeout=4.0):
    c_hwnd, _ = find_content_container(hwnd_main)
    deadline = time.time() + timeout
    while time.time() < deadline:
        lists = [h for (h, cls, _t) in enum_children(c_hwnd) if cls.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        if lists:
            return lists
        time.sleep(0.1)
    return []


def run_profile_pass(exe_path, work_dir, data_dir, out_dir, profile_cfg, profile_label, username, password):
    with open(os.path.join(data_dir, "station_fullscreen.cfg"), "w", encoding="utf-8") as f:
        f.write("fullscreen\n")
    with open(os.path.join(data_dir, "station_profile.cfg"), "w", encoding="utf-8") as f:
        f.write(f"{profile_cfg}\n")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = data_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    rows = []
    mon_info = None
    try:
        hwnd_login, _ = wait_window(proc.pid, "Iniciar Sesión", timeout=12.0)
        l_ctrls = enum_children(hwnd_login)
        l_edits = [h for (h, cls, _t) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, _t) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], username)
        set_text(l_edits[1], password)
        time.sleep(0.2)
        post_click(l_btns[0])

        hwnd_main, _ = wait_window(proc.pid, "Cafetería ITCH II", exclude=["Primer Inicio", "Iniciar Sesión"], timeout=12.0)
        time.sleep(0.6)

        style = user32.GetWindowLongW(hwnd_main, GWL_STYLE)
        assert (style & WS_CAPTION) == 0, f"[{profile_label}] La ventana no arrancó en pantalla completa (WS_CAPTION activo)"
        mon_w, mon_h, mon_rc = get_monitor_size(hwnd_main)
        scale = get_window_scale(hwnd_main)
        dpi = get_window_dpi(hwnd_main)
        mon_info = (mon_w, mon_h, dpi, scale)

        idx = 0

        def snap(slug):
            nonlocal idx
            idx += 1
            time.sleep(0.25)
            png_path = os.path.join(out_dir, f"{profile_cfg}_{idx:02d}_{slug}.png")
            img, w_rc = capture_window_img(hwnd_main, png_path)
            m = measure_view(hwnd_main, img, w_rc, profile_label, slug, scale)
            rows.append(m)
            print(
                f"  [{profile_label:8s}] {slug:20s} content={m['content_w']}x{m['content_h']} "
                f"union={m['union_w_pct']:5.1f}%x{m['union_h_pct']:5.1f}%({m['union_area_pct']:5.1f}%) "
                f"gapH={m['max_h_gap']:4d}px({m['max_h_gap_pct']:4.1f}%) gapV={m['max_v_gap']:4d}px({m['max_v_gap_pct']:4.1f}%) "
                f"PIL_fg={m['pil_non_bg_pct']:5.1f}% | {m['lists']}",
                flush=True,
            )

        # 1. Punto de Venta -> Venta (agregar 1 artículo al carrito para medir lista llena)
        click_button_text(hwnd_main, "Punto de Venta")
        click_button_text(hwnd_main, "Venta", exact=True)
        try:
            click_button_text(hwnd_main, "Agregar al Carrito", timeout=3.0)
        except Exception:
            pass
        snap("venta")

        # 2-8. Inventario
        click_button_text(hwnd_main, "Inventario")
        click_button_text(hwnd_main, "Existencias", exact=True)
        snap("existencias")

        click_button_text(hwnd_main, "Reabastecimiento", exact=True)
        snap("reabastecimiento")

        click_button_text(hwnd_main, "Productos y recetas", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 1, settle=0.25)
        snap("productos_y_recetas")

        click_button_text(hwnd_main, "Ingredientes", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 0, settle=0.25)
        snap("ingredientes")

        click_button_text(hwnd_main, "Promociones y combos", exact=True)
        click_button_text(hwnd_main, "Promociones", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 0, settle=0.25)
        snap("promociones")

        click_button_text(hwnd_main, "Combos", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 0, settle=0.25)
        snap("combos")

        click_button_text(hwnd_main, "Barra", exact=True)
        snap("barra")

        # 9-12. Caja y Reportes
        click_button_text(hwnd_main, "Caja y Reportes")
        click_button_text(hwnd_main, "Caja y turnos", exact=True)
        snap("caja_corte")

        click_button_text(hwnd_main, "Cortes anteriores", exact=True)
        click_button_text(hwnd_main, "Esta semana", exact=True)
        click_button_text(hwnd_main, "Buscar", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 0, settle=0.25)
        snap("caja_historial")

        click_button_text(hwnd_main, "Bitácora", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 0, settle=0.25)
        snap("bitacora")

        click_button_text(hwnd_main, "Reportes", exact=True)
        click_button_text(hwnd_main, "Hoy", exact=True)
        click_button_text(hwnd_main, "Consultar", exact=True)
        snap("reportes")

        # 13-17. BackOffice
        click_button_text(hwnd_main, "BackOffice")
        click_button_text(hwnd_main, "Usuarios y cuentas", exact=True)
        snap("usuarios")

        click_button_text(hwnd_main, "Roles y permisos", exact=True)
        lvs = wait_content_list(hwnd_main)
        if lvs:
            select_list_row(lvs[0], 0, settle=0.25)
        snap("roles")

        click_button_text(hwnd_main, "Configuración", exact=True)
        snap("configuracion")

        click_button_text(hwnd_main, "Respaldos", exact=True)
        snap("respaldos")

        click_button_text(hwnd_main, "Actualizaciones", exact=True)
        snap("actualizaciones")

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=6.0)
        return rows, mon_info
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except Exception:
                proc.kill()
                proc.wait(timeout=2.0)


def main():
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    work_dir = os.path.join(repo_root, "build_release")
    exe_path = os.path.join(work_dir, "app_desktop.exe")
    out_dir = os.path.join(repo_root, "qa_visual", "fullscreen")
    os.makedirs(out_dir, exist_ok=True)
    assert os.path.exists(exe_path), f"No se encontró {exe_path}"

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_fs_measure_")
    db_path = os.path.join(temp_dir, "cafeteria.db")
    username = "admin_fs"
    password = "AdminFsMeasure2026!"

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    print(f"=== MEDICIÓN DE PANTALLA COMPLETA ({format_dpi_info(sys_dpi)}) ===", flush=True)
    try:
        # Sesión 1: Alta de Dueño
        p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        try:
            hwnd_init, _ = wait_window(p1.pid, "Primer Inicio", timeout=12.0)
            ctrls = enum_children(hwnd_init)
            edits = [h for (h, cls, _t) in ctrls if cls.lower() == "edit"]
            btns = [h for (h, cls, _t) in ctrls if cls.lower() == "button"]
            set_text(edits[0], "Administrador General")
            set_text(edits[1], username)
            set_text(edits[2], "admin@itch.edu.mx")
            set_text(edits[3], password)
            set_text(edits[4], password)
            time.sleep(0.2)
            post_click(btns[0])
            hwnd_m1, _ = wait_window(p1.pid, "Cafetería ITCH II", exclude=["Primer Inicio", "Iniciar Sesión"], timeout=12.0)
            user32.PostMessageW(hwnd_m1, WM_CLOSE, 0, 0)
            p1.wait(timeout=6.0)
        finally:
            if p1.poll() is None:
                p1.kill()
                p1.wait(timeout=3.0)

        seed_rich_data(db_path)

        rows_std, mon_std = run_profile_pass(exe_path, work_dir, temp_dir, out_dir, "standard", "Estandar", username, password)
        rows_tch, mon_tch = run_profile_pass(exe_path, work_dir, temp_dir, out_dir, "touch", "Tactil", username, password)

        mon_w, mon_h, dpi, scale = mon_std
        medidas_path = os.path.join(out_dir, "medidas.txt")
        header = (
            f"# Medidas pantalla completa — Monitor: {mon_w}x{mon_h} px | {format_dpi_info(dpi)}\n"
            f"# Columnas: perfil | pantalla | content_px | union_px | union_w% | union_h% | union_area% | "
            f"max_gap_h_px(pct) | max_gap_v_px(pct) | pil_fg% | pil_bg_h_px | pil_bg_v_px | listas_tablas\n"
        )
        lines = [header]
        for m in rows_std + rows_tch:
            lines.append(
                f"{m['profile']:8s} | {m['screen']:20s} | "
                f"{m['content_w']:4d}x{m['content_h']:<4d} | "
                f"{m['union_w']:4d}x{m['union_h']:<4d} | "
                f"{m['union_w_pct']:5.1f}% | {m['union_h_pct']:5.1f}% | {m['union_area_pct']:5.1f}% | "
                f"{m['max_h_gap']:4d}px({m['max_h_gap_pct']:4.1f}%) | "
                f"{m['max_v_gap']:4d}px({m['max_v_gap_pct']:4.1f}%) | "
                f"{m['pil_non_bg_pct']:5.1f}% | {m['pil_h_bg']:4d}px | {m['pil_v_bg']:4d}px | "
                f"{m['lists']}\n"
            )
        with open(medidas_path, "w", encoding="utf-8") as f:
            f.writelines(lines)
        print(f"\n[OK] Medidas escritas en {medidas_path} ({len(rows_std) + len(rows_tch)} filas).", flush=True)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
