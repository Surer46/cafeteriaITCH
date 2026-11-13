#!/usr/bin/env python3
"""
test_f1_roles_panel.py - Verificación rigurosa de panel BackOffice > Roles y permisos (Puertas I6 / Tareas F1, F1b y F5).

Comprobaciones exigidas por especificación y auditoría:
1. Base temporal aislada y ejecutable real, tanto en Release como en Debug.
2. Rol Cajero (role-cashier):
   - Dueño puede editar permisos: interruptores y botón Guardar habilitados.
   - Nombre, Descripción y Eliminar deshabilitados.
   - Aviso explícito: "Rol de sistema: solo se pueden cambiar sus permisos."
   - Activar permiso nuevo (localizado por título leído de SQLite), guardar, cerrar, reabrir y comprobar persistencia íntegra.
3. Rol Dueño (role-owner):
   - Todo en solo lectura: Nombre, Descripción, Eliminar, Guardar e interruptores deshabilitados.
   - Clic en interruptor no cambia el estado.
   - Aviso explícito: "Rol de sistema (solo lectura: no editable ni eliminable)".
4. role-dev y backup.dev_config no aparecen en ningún control visible de la UI.
5. Columnas de lista "Roles Registrados":
   - LVM_GETCOLUMNWIDTH >= ancho medido del encabezado y texto más largo + 8 px (excepto Descripción con puntos suspensivos).
   - Sin estilo WS_HSCROLL visible en la lista en ambos perfiles (Estándar y Touch).
6. Matriz de permisos con ToggleSwitch (F5):
   - 20 interruptores visibles (uno por permiso visible en SQLite).
   - Tamaño del interruptor >= 44x24 en Estándar y >= 60x32 en Touch (GetWindowRect).
   - Alto de fila >= 52 en Estándar y >= 68 en Touch.
   - Título y descripción no vacíos, iguales a SQLite; título encima de descripción, sin recorte ni solapamiento con el interruptor.
   - Exactamente 4 encabezados de grupo en orden de la barra lateral: Punto de Venta, Inventario, Caja y Reportes, BackOffice.
   - Separador horizontal de 1 px detectado por píxeles entre cada par consecutivo de permisos del mismo grupo.
   - Centro vertical del interruptor a <= 3 px del centro de su fila.
   - Borde derecho de todos los interruptores alineado (<= 2 px) y ninguno recortado por el área visible.
   - Posición de la perilla leída por píxeles (derecha si BM_GETCHECK == 1, izquierda si 0).
   - Clic en interruptor, en título, en descripción y Espacio con foco cambian el estado.
   - Contador junto a Guardar coincide con el número de interruptores activados.
7. Roles creados por el Dueño:
   - Nombre, descripción, interruptores, Guardar y Eliminar 100% editables y persistentes.
8. Geometría estricta a 1200x720 y 960x600 en ambos perfiles; en Touch, filas de lista >= 40 px, campos >= 48 px centrados y botones >= 48 px.
"""

import ctypes
from ctypes import wintypes
import re
import time
import subprocess
import os
import sys
import sqlite3
import tempfile
import shutil

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

class SIZE(ctypes.Structure):
    _fields_ = [("cx", wintypes.LONG), ("cy", wintypes.LONG)]

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
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
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.restype = ctypes.c_int
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetClassNameW.restype = ctypes.c_int
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetForegroundWindow.restype = wintypes.BOOL
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.SetWindowPos.restype = wintypes.BOOL
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.GetWindowDC.argtypes = [wintypes.HWND]
user32.GetWindowDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int
user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
user32.PrintWindow.restype = wintypes.BOOL
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongW.restype = wintypes.LONG

gdi32.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(SIZE)]
gdi32.GetTextExtentPoint32W.restype = wintypes.BOOL
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.GetPixel.restype = wintypes.COLORREF
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.DeleteDC.restype = wintypes.BOOL
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteObject.restype = wintypes.BOOL

user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualAllocEx.restype = wintypes.LPVOID
kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
kernel32.VirtualFreeEx.restype = wintypes.BOOL
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.WriteProcessMemory.restype = wintypes.BOOL
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

try:
    user32.GetDpiForWindow.argtypes = [wintypes.HWND]
    user32.GetDpiForWindow.restype = wintypes.UINT
    HAS_GET_DPI = True
except AttributeError:
    HAS_GET_DPI = False

GWL_STYLE = -16
WS_HSCROLL = 0x00100000
LVM_GETCOLUMNWIDTH = 0x1000 + 29
LVM_GETNEXTITEM = 0x1000 + 12
LVM_GETITEMRECT = 0x1000 + 14
LVM_GETHEADER = 0x1000 + 31
LVIR_BOUNDS = 0
WM_VSCROLL = 0x0115
SB_LINEUP = 0
SB_LINEDOWN = 1
SB_TOP = 6
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT_RESERVE = 0x3000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
CLR_INVALID = 0xFFFFFFFF

WM_SETTEXT = 0x000C
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_CLOSE = 0x0010
WM_GETFONT = 0x0031
BM_CLICK = 0x00F5
BM_GETCHECK = 0x00F0
BM_SETCHECK = 0x00F1
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_SPACE = 0x20
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
SWP_NOZORDER = 0x0004
SWP_NOMOVE = 0x0002

SIDEBAR_SECTIONS = ["Punto de Venta", "Inventario", "Caja y Reportes", "BackOffice"]

def norm_ws(text):
    return " ".join(text.split())

def colorref_to_rgb(c):
    return (c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF)

def get_window_pid(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value

def find_windows_by_pid(target_pid, title_substring="", exclude_substrings=None):
    found = []
    def enum_cb(hwnd, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == target_pid and user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            text = buf.value
            if exclude_substrings and any(exc in text for exc in exclude_substrings):
                return True
            if not title_substring or title_substring.lower() in text.lower():
                found.append((hwnd, text))
        return True
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return found

def get_child_controls(parent_hwnd):
    controls = []
    def enum_child_cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            cls_buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls_buf, 256)
            txt = get_ctrl_text(hwnd)
            controls.append((hwnd, cls_buf.value, txt))
        return True
    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(enum_child_cb), 0)
    return controls

def get_direct_children(parent_hwnd):
    return [c for c in get_child_controls(parent_hwnd) if user32.GetParent(c[0]) == parent_hwnd]

def wait_for_window_by_pid(target_pid, title_substring, exclude_substrings=None, timeout_sec=12.0, step_name=""):
    start = time.time()
    while time.time() - start < timeout_sec:
        wins = find_windows_by_pid(target_pid, title_substring, exclude_substrings)
        if wins:
            return wins[0]
        time.sleep(0.2)
    raise TimeoutError(f"Timeout esperando ventana '{title_substring}' en PID {target_pid} (paso: {step_name})")

def get_rect(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom, (r.right - r.left), (r.bottom - r.top)

def measure_text_extent(hwnd, text):
    hdc = user32.GetDC(hwnd)
    if not hdc:
        return 0, 0
    hfont = user32.SendMessageW(hwnd, WM_GETFONT, 0, 0)
    old_font = 0
    if hfont:
        old_font = gdi32.SelectObject(hdc, hfont)
    sz = SIZE()
    gdi32.GetTextExtentPoint32W(hdc, text, len(text), ctypes.byref(sz))
    if old_font:
        gdi32.SelectObject(hdc, old_font)
    user32.ReleaseDC(hwnd, hdc)
    return sz.cx, sz.cy

def set_text(hwnd, text):
    text_buf = ctypes.create_unicode_buffer(text)
    user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(text_buf))

def get_ctrl_text(hwnd):
    length = user32.SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if length > 0:
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.SendMessageW(hwnd, WM_GETTEXT, length + 1, ctypes.addressof(buf))
        return buf.value
    len_w = user32.GetWindowTextLengthW(hwnd)
    if len_w > 0:
        buf = ctypes.create_unicode_buffer(len_w + 1)
        user32.GetWindowTextW(hwnd, buf, len_w + 1)
        return buf.value
    return ""

def send_click_button(hwnd):
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)

def post_click_button(hwnd):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)

def click_control_at(hwnd, x=None, y=None):
    _, _, _, _, w, h = get_rect(hwnd)
    cx = (w // 2) if x is None else max(1, min(w - 2, x))
    cy = (h // 2) if y is None else max(1, min(h - 2, y))
    lparam = (cy << 16) | (cx & 0xFFFF)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)
    time.sleep(0.25)

def remote_get_item_rect(hwnd_list, index, code=LVIR_BOUNDS):
    """LVM_GETITEMRECT entre procesos: el RECT debe vivir en la memoria de la app, no en la de la prueba."""
    pid = get_window_pid(hwnd_list)
    access = PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION
    hproc = kernel32.OpenProcess(access, False, pid)
    if not hproc:
        raise AssertionError(f"LVM_GETITEMRECT: OpenProcess({pid}) falló (error {ctypes.get_last_error()})")
    remote = None
    try:
        size = ctypes.sizeof(wintypes.RECT)
        remote = kernel32.VirtualAllocEx(hproc, None, size, MEM_COMMIT_RESERVE, PAGE_READWRITE)
        if not remote:
            raise AssertionError(f"LVM_GETITEMRECT: VirtualAllocEx falló (error {ctypes.get_last_error()})")
        rect_in = wintypes.RECT(code, 0, 0, 0)
        written = ctypes.c_size_t(0)
        if not kernel32.WriteProcessMemory(hproc, remote, ctypes.byref(rect_in), size, ctypes.byref(written)) or written.value != size:
            raise AssertionError(f"LVM_GETITEMRECT: WriteProcessMemory falló (error {ctypes.get_last_error()}, escritos {written.value}/{size})")
        ok = user32.SendMessageW(hwnd_list, LVM_GETITEMRECT, index, remote)
        if not ok:
            raise AssertionError(f"LVM_GETITEMRECT devolvió FALSE para el ítem {index}")
        rect_out = wintypes.RECT()
        read = ctypes.c_size_t(0)
        if not kernel32.ReadProcessMemory(hproc, remote, ctypes.byref(rect_out), size, ctypes.byref(read)) or read.value != size:
            raise AssertionError(f"LVM_GETITEMRECT: ReadProcessMemory falló (error {ctypes.get_last_error()}, leídos {read.value}/{size})")
        return rect_out.left, rect_out.top, rect_out.right, rect_out.bottom
    finally:
        if remote:
            if not kernel32.VirtualFreeEx(hproc, remote, 0, MEM_RELEASE):
                print(f"  [AVISO] VirtualFreeEx falló (error {ctypes.get_last_error()}); memoria remota de 16 bytes sin liberar")
        kernel32.CloseHandle(hproc)

def select_list_item(hwnd_list, row_index):
    """Envía clic de ratón al centro de la fila indicada del SysListView32 (posición real vía LVM_GETITEMRECT)."""
    left, top, right, bottom = remote_get_item_rect(hwnd_list, row_index)
    x = left + min(40, max(1, (right - left) // 2))
    y = (top + bottom) // 2
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd_list, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_list, WM_LBUTTONUP, 0, lparam)
    time.sleep(0.3)
    sel = user32.SendMessageW(hwnd_list, LVM_GETNEXTITEM, -1, 0x0002)
    assert sel == row_index, f"El clic no seleccionó la fila {row_index} (seleccionada: {sel})"

def get_client_size(hwnd):
    r = wintypes.RECT()
    assert user32.GetClientRect(hwnd, ctypes.byref(r)), "GetClientRect falló"
    return r.right - r.left, r.bottom - r.top

def get_client_screen_rect(hwnd):
    rc = wintypes.RECT()
    assert user32.GetClientRect(hwnd, ctypes.byref(rc)), "GetClientRect falló"
    pt = wintypes.POINT(0, 0)
    assert user32.ClientToScreen(hwnd, ctypes.byref(pt)), "ClientToScreen falló"
    return pt.x, pt.y, pt.x + rc.right, pt.y + rc.bottom

def get_window_dpi(hwnd):
    if HAS_GET_DPI:
        dpi = user32.GetDpiForWindow(hwnd)
        if dpi > 0:
            return dpi
    hdc = user32.GetDC(hwnd)
    if hdc:
        dpi = gdi32.GetDeviceCaps(hdc, 88)
        user32.ReleaseDC(hwnd, hdc)
        return dpi
    return 96

def load_visible_permissions_from_db(db_path):
    con = sqlite3.connect(db_path)
    cur = con.cursor()
    cur.execute("""
        SELECT id, title, description, section, sort_order
        FROM permissions
        WHERE id != 'backup.dev_config'
        ORDER BY sort_order ASC
    """)
    rows = cur.fetchall()
    con.close()
    assert len(rows) > 0, "No se encontraron permisos visibles en SQLite"
    return [
        {"id": r[0], "title": r[1], "description": r[2], "section": r[3], "sort_order": r[4]}
        for r in rows
    ]

def collect_permission_rows(parent_hwnd, db_perms):
    """Descubre cada fila de permiso en la UI usando el título leído de la tabla permissions de SQLite."""
    ctrls = get_child_controls(parent_hwnd)
    by_title = {norm_ws(p["title"]): p for p in db_perms}

    rows = []
    seen_sw = set()
    for hwnd_sw, cls, txt in ctrls:
        if "button" in cls.lower() or cls.lower() in ("static", "edit", "syslistview32"):
            continue
        ntxt = norm_ws(txt)
        if not ntxt or ntxt not in by_title or hwnd_sw in seen_sw:
            continue
        h_row = user32.GetParent(hwnd_sw)
        if not h_row:
            continue
        row_children = get_direct_children(h_row)
        statics = [c for c in row_children if c[1].lower() == "static"]
        if len(statics) != 2:
            continue
        statics_sorted = sorted(statics, key=lambda c: get_rect(c[0])[1])
        h_title = statics_sorted[0][0]
        h_desc = statics_sorted[1][0]
        seen_sw.add(hwnd_sw)
        rows.append({
            "perm": by_title[ntxt],
            "h_scroll": user32.GetParent(h_row),
            "h_row": h_row,
            "h_title": h_title,
            "h_desc": h_desc,
            "h_sw": hwnd_sw,
        })

    # Ordenar visualmente de arriba a abajo
    rows.sort(key=lambda r: get_rect(r["h_row"])[1])
    return rows

def find_toggle_by_perm_id(parent_hwnd, db_perms, perm_id):
    rows = collect_permission_rows(parent_hwnd, db_perms)
    match = next((r for r in rows if r["perm"]["id"] == perm_id), None)
    assert match is not None, f"Interruptor para permiso '{perm_id}' no encontrado en la UI"
    return match

def ensure_rect_visible_in_scroll(h_scroll, target_top, target_bottom, label=""):
    """Desplaza h_scroll con WM_VSCROLL hasta que [target_top, target_bottom] quede dentro del área visible."""
    for _ in range(500):
        _, s_top, _, s_bottom = get_client_screen_rect(h_scroll)
        if target_top >= s_top and target_bottom <= s_bottom:
            time.sleep(0.08)
            return
        cmd = SB_LINEDOWN if target_bottom > s_bottom else SB_LINEUP
        user32.SendMessageW(h_scroll, WM_VSCROLL, cmd, 0)
        time.sleep(0.02)
        # Actualizar coordenadas relativas tras scroll: no aplica si el caller pasa coords fijas; usar ensure_row_visible
    raise AssertionError(f"No se pudo traer a la vista '{label}' con WM_VSCROLL")

def ensure_row_visible(h_scroll, h_row, label=""):
    for _ in range(500):
        _, rt, _, rb, _, _ = get_rect(h_row)
        _, s_top, _, s_bottom = get_client_screen_rect(h_scroll)
        if rt >= s_top and rb <= s_bottom:
            time.sleep(0.08)
            return
        cmd = SB_LINEDOWN if rb > s_bottom else SB_LINEUP
        user32.SendMessageW(h_scroll, WM_VSCROLL, cmd, 0)
        time.sleep(0.02)
    raise AssertionError(f"No se pudo traer la fila '{label}' a la vista con WM_VSCROLL")

def ensure_pair_boundary_visible(h_scroll, h_row_a, h_row_b, label=""):
    ensure_row_visible(h_scroll, h_row_a, label)
    for _ in range(200):
        _, _, _, ab, _, _ = get_rect(h_row_a)
        _, bt, _, _, _, _ = get_rect(h_row_b)
        _, s_top, _, s_bottom = get_client_screen_rect(h_scroll)
        if ab - 3 >= s_top and bt + 3 <= s_bottom:
            time.sleep(0.08)
            return
        cmd = SB_LINEDOWN if bt + 3 > s_bottom else SB_LINEUP
        user32.SendMessageW(h_scroll, WM_VSCROLL, cmd, 0)
        time.sleep(0.02)
    raise AssertionError(f"No se pudo traer el separador '{label}' a la vista con WM_VSCROLL")

def verify_knob_pixels(h_sw, expected_checked, label):
    """Lee píxeles del interruptor para verificar en qué lado está la perilla blanca."""
    _, _, _, _, sw_w, sw_h = get_rect(h_sw)
    hdc = user32.GetDC(h_sw)
    assert hdc, f"GetDC falló para interruptor '{label}'"
    try:
        mid_x = sw_w // 2
        left_knob = 0
        right_knob = 0
        for y in range(4, max(5, sw_h - 4)):
            for x in range(4, max(5, mid_x - 2)):
                v = gdi32.GetPixel(hdc, x, y)
                if v != CLR_INVALID:
                    r, g, b = colorref_to_rgb(v)
                    if min(r, g, b) >= 242:
                        left_knob += 1
            for x in range(mid_x + 2, max(mid_x + 3, sw_w - 4)):
                v = gdi32.GetPixel(hdc, x, y)
                if v != CLR_INVALID:
                    r, g, b = colorref_to_rgb(v)
                    if min(r, g, b) >= 242:
                        right_knob += 1
    finally:
        user32.ReleaseDC(h_sw, hdc)

    assert left_knob >= 10 or right_knob >= 10, (
        f"Perilla no detectada en interruptor '{label}' (izq={left_knob}px, der={right_knob}px, {sw_w}x{sw_h})"
    )
    if expected_checked:
        assert right_knob > left_knob and right_knob >= 10, (
            f"Interruptor '{label}' activado (BM_GETCHECK=1) pero perilla no está a la derecha "
            f"(izq={left_knob}px, der={right_knob}px)"
        )
    else:
        assert left_knob > right_knob and left_knob >= 10, (
            f"Interruptor '{label}' desactivado (BM_GETCHECK=0) pero perilla no está a la izquierda "
            f"(izq={left_knob}px, der={right_knob}px)"
        )

def verify_separator_pixels(hwnd_main, h_scroll, row_a, row_b):
    """Detecta por píxeles la línea separadora horizontal entre row_a y row_b del mismo grupo."""
    ensure_pair_boundary_visible(h_scroll, row_a["h_row"], row_b["h_row"],
                                 f"{row_a['perm']['id']} -> {row_b['perm']['id']}")

    ml, mt, _, _, mw, mh = get_rect(hwnd_main)
    rl, _, rr, ab, _, _ = get_rect(row_a["h_row"])
    _, bt, _, _, _, _ = get_rect(row_b["h_row"])
    assert bt >= ab, f"Fila '{row_b['perm']['id']}' solapa verticalmente a '{row_a['perm']['id']}': {bt} < {ab}"

    hwnd_dc = user32.GetWindowDC(hwnd_main)
    assert hwnd_dc, "GetWindowDC falló al inspeccionar separador"
    mem_dc = gdi32.CreateCompatibleDC(hwnd_dc)
    bmp = gdi32.CreateCompatibleBitmap(hwnd_dc, mw, mh)
    old_bmp = gdi32.SelectObject(mem_dc, bmp)
    try:
        ok = user32.PrintWindow(hwnd_main, mem_dc, 2)
        assert ok, "PrintWindow falló al inspeccionar separador"
        xs = [rl + 12, (rl + rr) // 2, rr - 12]
        xs = [x for x in xs if rl < x < rr]
        found_sep = False
        seen_samples = []
        for y in range(ab - 1, bt + 2):
            hits = 0
            row_cols = []
            for x in xs:
                v = gdi32.GetPixel(mem_dc, x - ml, y - mt)
                if v == CLR_INVALID:
                    continue
                r, g, b = colorref_to_rgb(v)
                row_cols.append((r, g, b))
                # Theme::ColorBorderSubtle = #E2E8F0 (226, 232, 240); fondo = #FFFFFF (255, 255, 255)
                if abs(r - 226) <= 10 and abs(g - 232) <= 10 and abs(b - 240) <= 10:
                    hits += 1
            seen_samples.append((y, row_cols))
            if hits == len(xs) and hits > 0:
                found_sep = True
                break
        assert found_sep, (
            f"No se detectó línea separadora horizontal (#E2E8F0) por píxeles entre "
            f"'{row_a['perm']['id']}' (bottom={ab}) y '{row_b['perm']['id']}' (top={bt}); muestras={seen_samples}"
        )
    finally:
        gdi32.SelectObject(mem_dc, old_bmp)
        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(hwnd_main, hwnd_dc)

def verify_list_columns(hwnd_list, profile_name):
    """Anchos de columna: encabezado medido con la fuente REAL del encabezado, contenido con la de la lista."""
    style = user32.GetWindowLongW(hwnd_list, GWL_STYLE)
    assert (style & WS_HSCROLL) == 0, f"Lista tiene barra horizontal WS_HSCROLL visible en {profile_name}"

    hdr = user32.SendMessageW(hwnd_list, LVM_GETHEADER, 0, 0)
    assert hdr, f"LVM_GETHEADER no devolvió el encabezado de la lista en {profile_name}"

    col_widths = [user32.SendMessageW(hwnd_list, LVM_GETCOLUMNWIDTH, i, 0) for i in range(4)]
    titles = ["Nombre", "Descripción", "Sistema", "Permisos"]
    for i, title in enumerate(titles):
        hdr_ext, _ = measure_text_extent(hdr, title)
        assert hdr_ext > 0, f"No se pudo medir el encabezado '{title}' en {profile_name}"
        assert col_widths[i] >= hdr_ext + 8, (
            f"Encabezado '{title}' recortado: columna {col_widths[i]}px < {hdr_ext + 8}px en {profile_name}")

    ext_cajero, _ = measure_text_extent(hwnd_list, "Cajero")
    ext_dueno, _ = measure_text_extent(hwnd_list, "Dueño")
    max_col0 = max(ext_cajero, ext_dueno)
    assert col_widths[0] >= max_col0 + 8, f"Columna 0 ('Nombre') {col_widths[0]}px < {max_col0 + 8}px en {profile_name}"

    ext_si, _ = measure_text_extent(hwnd_list, "Sí")
    assert col_widths[2] >= ext_si + 8, f"Columna 2 ('Sistema') {col_widths[2]}px < {ext_si + 8}px en {profile_name}"

    ext_num, _ = measure_text_extent(hwnd_list, "20")
    assert col_widths[3] >= ext_num + 8, f"Columna 3 ('Permisos') {col_widths[3]}px < {ext_num + 8}px en {profile_name}"

    client_w, _ = get_client_size(hwnd_list)
    total = sum(col_widths)
    assert total <= client_w, f"Columnas ({total}px) exceden el área cliente ({client_w}px) en {profile_name}"
    assert client_w - total <= 8, (
        f"Espacio sobrante sin aprovechar: {client_w - total}px > 8px (columnas {total}px, cliente {client_w}px) en {profile_name}")

    print(f"  [{profile_name}] Columnas medidas: N={col_widths[0]}px, D={col_widths[1]}px, S={col_widths[2]}px, P={col_widths[3]}px "
          f"(cliente {client_w}px, holgura {client_w - total}px, sin WS_HSCROLL)")

def get_perm_counter_text(hwnd_main):
    ctrls = get_child_controls(hwnd_main)
    return next(
        (txt for (_, cls, txt) in ctrls if cls.lower() == "static" and re.fullmatch(r"\d+ de \d+ permisos", txt)),
        None,
    )

def verify_permission_matrix(hwnd_main, db_perms, profile_name, check_pixels=True):
    """Verificación geométrica y visual completa de los interruptores, filas, textos, encabezados y separadores."""
    rows = collect_permission_rows(hwnd_main, db_perms)
    assert len(rows) == len(db_perms), (
        f"Se esperaban {len(db_perms)} interruptores visibles en {profile_name}, encontrados {len(rows)}"
    )

    h_scroll = rows[0]["h_scroll"]
    assert h_scroll, f"Contenedor con scroll no encontrado en {profile_name}"
    user32.SendMessageW(h_scroll, WM_VSCROLL, SB_TOP, 0)
    time.sleep(0.1)

    # 1. Exactamente 4 encabezados de grupo en el orden de la barra lateral
    scroll_statics = [c for c in get_direct_children(h_scroll) if c[1].lower() == "static"]
    group_headers = [
        (h, txt, get_rect(h)[1]) for (h, _, txt) in scroll_statics
        if txt in SIDEBAR_SECTIONS
    ]
    group_headers.sort(key=lambda g: g[2])
    found_sections = [g[1] for g in group_headers]
    assert found_sections == SIDEBAR_SECTIONS, (
        f"Encabezados de grupo incorrectos en {profile_name}: {found_sections} != {SIDEBAR_SECTIONS}"
    )

    min_sw_w = 60 if profile_name == "Touch" else 44
    min_sw_h = 32 if profile_name == "Touch" else 24
    min_row_h = 68 if profile_name == "Touch" else 52

    s_left, _, s_right, _ = get_client_screen_rect(h_scroll)
    right_edges = []

    for r in rows:
        p = r["perm"]
        rl, rt, rr, rb, rw, rh = get_rect(r["h_row"])
        tl, tt, tr, tb, tw, th = get_rect(r["h_title"])
        dl, dt, dr, db_y, dw, dh = get_rect(r["h_desc"])
        sl, st, sr, sb, sw, sh = get_rect(r["h_sw"])

        # Tamaño del interruptor y alto de fila
        assert sw >= min_sw_w and sh >= min_sw_h, (
            f"Interruptor '{p['id']}' en {profile_name} demasiado pequeño: {sw}x{sh} < {min_sw_w}x{min_sw_h}"
        )
        assert rh >= min_row_h, (
            f"Fila '{p['id']}' en {profile_name} altura insuficiente: {rh}px < {min_row_h}px"
        )

        # Título y descripción no vacíos, iguales a SQLite, sin solaparse ni recortarse
        ui_title = get_ctrl_text(r["h_title"])
        ui_desc = get_ctrl_text(r["h_desc"])
        assert ui_title.strip() and norm_ws(ui_title) == norm_ws(p["title"]), (
            f"Título distinto a SQLite en '{p['id']}': '{ui_title}' != '{p['title']}'"
        )
        assert ui_desc.strip() and norm_ws(ui_desc) == norm_ws(p["description"]), (
            f"Descripción distinta a SQLite en '{p['id']}': '{ui_desc}' != '{p['description']}'"
        )
        assert tb <= dt, (
            f"Título no queda encima de descripción en '{p['id']}': tb={tb} > dt={dt}"
        )
        assert tr <= sl and dr <= sl, (
            f"Texto solapa al interruptor en '{p['id']}': tr={tr}, dr={dr}, sl={sl}"
        )

        title_lines = [ln for ln in ui_title.split("\n") if ln]
        for ln in title_lines:
            lw, lh = measure_text_extent(r["h_title"], ln)
            assert tw >= lw, f"Título '{ln}' recortado horizontalmente en '{p['id']}': {tw}px < {lw}px"
        _, one_th = measure_text_extent(r["h_title"], title_lines[0])
        assert th >= len(title_lines) * one_th - 2, (
            f"Título '{p['id']}' recortado verticalmente: {th}px < {len(title_lines) * one_th}px"
        )

        desc_lines = [ln for ln in ui_desc.split("\n") if ln]
        for ln in desc_lines:
            lw, lh = measure_text_extent(r["h_desc"], ln)
            assert dw >= lw, f"Descripción '{ln[:25]}' recortada horizontalmente en '{p['id']}': {dw}px < {lw}px"
        _, one_dh = measure_text_extent(r["h_desc"], desc_lines[0])
        assert dh >= len(desc_lines) * one_dh - 2, (
            f"Descripción '{p['id']}' recortada verticalmente: {dh}px < {len(desc_lines) * one_dh}px"
        )

        # Centro vertical del interruptor a <= 3 px del centro de su fila
        row_cy = (rt + rb) / 2.0
        sw_cy = (st + sb) / 2.0
        assert abs(sw_cy - row_cy) <= 3.0, (
            f"Interruptor '{p['id']}' descentrado verticalmente en {profile_name}: "
            f"centro interruptor {sw_cy:.1f} vs fila {row_cy:.1f} (dif {abs(sw_cy - row_cy):.1f}px > 3px)"
        )

        # Borde derecho dentro del área visible
        assert sr <= s_right and sl >= s_left, (
            f"Interruptor '{p['id']}' recortado por área visible en {profile_name}: [{sl}, {sr}] vs [{s_left}, {s_right}]"
        )
        right_edges.append(sr)

        # Posición de la perilla leída por píxeles
        if check_pixels:
            ensure_row_visible(h_scroll, r["h_row"], p["id"])
            user32.SetForegroundWindow(hwnd_main)
            time.sleep(0.03)
            chk = user32.SendMessageW(r["h_sw"], BM_GETCHECK, 0, 0)
            verify_knob_pixels(r["h_sw"], chk == 1, f"{p['id']} ({profile_name})")

    assert max(right_edges) - min(right_edges) <= 2, (
        f"Bordes derechos de interruptores desalineados en {profile_name}: "
        f"min={min(right_edges)}, max={max(right_edges)} (dif={max(right_edges)-min(right_edges)}px > 2px)"
    )

    # Separador horizontal por píxeles entre cada par consecutivo de permisos del mismo grupo
    if check_pixels:
        for i in range(len(rows) - 1):
            if rows[i]["perm"]["section"] == rows[i + 1]["perm"]["section"]:
                verify_separator_pixels(hwnd_main, h_scroll, rows[i], rows[i + 1])

    # Contador junto a Guardar coincide con el número de interruptores activados
    on_count = sum(1 for r in rows if user32.SendMessageW(r["h_sw"], BM_GETCHECK, 0, 0) == 1)
    expected_counter = f"{on_count} de {len(rows)} permisos"
    counter_lbl = get_perm_counter_text(hwnd_main)
    assert counter_lbl == expected_counter, (
        f"Contador junto a Guardar incorrecto en {profile_name}: '{counter_lbl}' != '{expected_counter}'"
    )

    sample_sw = get_rect(rows[0]["h_sw"])
    sample_row = get_rect(rows[0]["h_row"])
    print(f"  [{profile_name}] {len(rows)} interruptores ({sample_sw[4]}x{sample_sw[5]}px), "
          f"filas (>={sample_row[5]}px), 4 grupos, separadores, alineación y perillas por píxel verificados. Contador: '{counter_lbl}'.")
    return rows

def ink_vertical_extent(h_edit, text):
    """Rango vertical (coordenadas de pantalla) de los píxeles de texto de un campo Edit."""
    l, t, r, b, w, h = get_rect(h_edit)
    tw, _ = measure_text_extent(h_edit, text)
    assert 0 < tw < w - 16, f"Texto '{text}' ({tw}px) no cabe en el campo ({w}px) para medir centrado"
    hdc = user32.GetDC(h_edit)
    assert hdc, "GetDC del campo falló"
    try:
        bg = None
        for (x, y) in ((w - 4, h // 2), (w - 4, 2), (w - 4, h - 3)):
            v = gdi32.GetPixel(hdc, x, y)
            if v != CLR_INVALID:
                bg = v
                break
        assert bg is not None, "Sin píxel de fondo medible en el campo"
        first = last = None
        for y in range(h):
            for x in range(0, min(w - 8, tw + 12)):
                v = gdi32.GetPixel(hdc, x, y)
                if v != CLR_INVALID and v != bg:
                    if first is None:
                        first = y
                    last = y
                    break
        assert first is not None, f"No se detectó texto '{text}' en el campo (¿vacío o fuera de vista?)"
        return t + first, t + last
    finally:
        user32.ReleaseDC(h_edit, hdc)

def verify_touch_roles_list_and_fields(hwnd_main, hwnd_list, texts=("Supervisor Táctil", "Prueba de centrado")):
    """Touch: filas de la lista >= 40 px; Nombre y Descripción editables y centrados verticalmente (tol. 3 px)."""
    for row in (0, 1):
        _, top, _, bottom = remote_get_item_rect(hwnd_list, row)
        assert bottom - top >= 40, f"Fila {row} de la lista de roles en Touch mide {bottom - top}px < 40px"
    print("  [Touch] Filas de la lista de roles >= 40 px (LVM_GETITEMRECT).")

    ctrls = get_child_controls(hwnd_main)
    btn_new = next((h for (h, cls, txt) in ctrls if cls.lower() == "button" and txt == "Nuevo rol"), None)
    assert btn_new is not None, "Botón 'Nuevo rol' no encontrado en Touch"
    send_click_button(btn_new)
    time.sleep(0.5)

    edits = [h for (h, cls, txt) in get_child_controls(hwnd_main) if cls.lower() == "edit"]
    assert len(edits) >= 2, f"Campos Nombre/Descripción no encontrados en Touch ({len(edits)})"
    for h_edit, text, label in ((edits[0], texts[0], "Nombre"), (edits[1], texts[1], "Descripción")):
        assert user32.IsWindowEnabled(h_edit) == 1, f"Campo {label} debe estar habilitado para un rol nuevo en Touch"
        set_text(h_edit, text)
        time.sleep(0.3)
        assert get_ctrl_text(h_edit) == text, f"Campo {label} no conservó el texto escrito en Touch"

        container = user32.GetParent(h_edit)
        assert container, f"Campo {label} sin contenedor"
        _, ct, _, cb, _, ch_cont = get_rect(container)
        assert ch_cont >= 48, f"Contenedor de {label} en Touch mide {ch_cont}px < 48px"
        ink_top, ink_bottom = ink_vertical_extent(h_edit, text)
        ink_center = (ink_top + ink_bottom) / 2.0
        box_center = (ct + cb) / 2.0
        assert abs(ink_center - box_center) <= 3, (
            f"Texto de {label} no centrado verticalmente: centro de tinta {ink_center:.1f} vs contenedor {box_center:.1f} "
            f"(diferencia {abs(ink_center - box_center):.1f}px > 3px)")
    print("  [Touch] Nombre y Descripción editables y centrados verticalmente (tolerancia 3 px).")

def verify_guardar_visible_without_scroll(hwnd_main):
    _, mt, _, mb, _, _ = get_rect(hwnd_main)
    btn_save = next((h for (h, cls, txt) in get_child_controls(hwnd_main) if cls.lower() == "button" and txt == "Guardar"), None)
    assert btn_save is not None, "Botón 'Guardar' no encontrado"
    _, bt, _, bb, _, _ = get_rect(btn_save)
    assert bt >= mt and bb <= mb, f"Botón 'Guardar' no visible sin desplazarse: [{bt}, {bb}] vs ventana [{mt}, {mb}]"

def run_f1_test_for_binary(exe_path, work_dir, config_name):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN ROLES Y PERMISOS F1 / F1b / F5 [{config_name}]:")
    print(f"  Binario:  {exe_path}")
    print(f"=======================================================")
    assert os.path.exists(exe_path), f"Binario no encontrado: {exe_path}"

    test_data_dir = tempfile.mkdtemp(prefix=f"cafeteria_f1_{config_name.lower()}_")
    db_path = os.path.join(test_data_dir, "cafeteria.db")

    p1 = None
    p2 = None

    try:
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = test_data_dir

        # =============================================================
        # SESIÓN 1: Alta inicial del Dueño y Pruebas del Panel de Roles
        # =============================================================
        print("\n--- SESIÓN 1: Alta Inicial del Dueño ---")
        p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid1 = p1.pid

        hwnd_owner, _ = wait_for_window_by_pid(pid1, "Primer Inicio", step_name="Alta Dueño")
        assert hwnd_owner is not None, "Diálogo de primer inicio no encontrado"

        owner_ctrls = get_child_controls(hwnd_owner)
        owner_edits = [h for (h, cls, txt) in owner_ctrls if cls.lower() == "edit"]
        owner_btns = [h for (h, cls, txt) in owner_ctrls if cls.lower() == "button"]
        set_text(owner_edits[0], "Administrador General")
        set_text(owner_edits[1], "admin_owner")
        set_text(owner_edits[2], "admin@itch.edu.mx")
        set_text(owner_edits[3], "ClaveSegura123!")
        set_text(owner_edits[4], "ClaveSegura123!")
        time.sleep(0.3)
        send_click_button(owner_btns[0])
        time.sleep(1.2)

        hwnd_main1, _ = wait_for_window_by_pid(
            pid1, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow alta inicial"
        )
        assert hwnd_main1 is not None, "MainWindow no abrió tras alta inicial"
        print("[OK] Alta inicial completada y MainWindow abierta.")

        dpi1 = get_window_dpi(hwnd_main1)
        print(f"[DPI] DPI detectado: {dpi1} ({int(round(dpi1/96.0*100))}%).")

        db_perms = load_visible_permissions_from_db(db_path)
        assert len(db_perms) == 20, f"Se esperaban 20 permisos visibles en SQLite, encontrados {len(db_perms)}"

        # Navegar a BackOffice > Roles y permisos
        main_ctrls1 = get_child_controls(hwnd_main1)
        main_btns1 = [(h, txt) for (h, cls, txt) in main_ctrls1 if cls.lower() == "button"]
        btn_bo1 = next((h for (h, t) in main_btns1 if "BackOffice" in t), None)
        assert btn_bo1 is not None, "Botón 'BackOffice' no encontrado en sidebar"
        send_click_button(btn_bo1)
        time.sleep(0.8)

        bo_ctrls = get_child_controls(hwnd_main1)
        bo_btns = [(h, txt) for (h, cls, txt) in bo_ctrls if cls.lower() == "button"]
        btn_roles = next((h for (h, t) in bo_btns if "Roles y permisos" in t), None)
        assert btn_roles is not None, "Pestaña 'Roles y permisos' no encontrada en subnav"
        send_click_button(btn_roles)
        time.sleep(0.8)

        # -------------------------------------------------------------
        # CASO 1: role-dev y backup.dev_config no aparecen en la interfaz
        # -------------------------------------------------------------
        print("\n--- CASO 1: Verificación de exclusión de role-dev y backup.dev_config ---")
        panel_ctrls = get_child_controls(hwnd_main1)
        for h, cls, txt in panel_ctrls:
            assert "role-dev" not in txt, f"ERROR: 'role-dev' expuesto en control {cls} ('{txt}')"
            assert "backup.dev_config" not in txt, f"ERROR: 'backup.dev_config' expuesto en control {cls} ('{txt}')"
        print("[OK] Ni 'role-dev' ni 'backup.dev_config' aparecen en ningún control visible de la UI.")

        # -------------------------------------------------------------
        # CASO 2: Roles de Sistema (Cajero editable, Dueño solo lectura) + Interacciones F5
        # -------------------------------------------------------------
        print("\n--- CASO 2: Rol Cajero (interruptores editables) y Rol Dueño (solo lectura) ---")
        lists = [h for (h, cls, txt) in panel_ctrls if "list" in cls.lower()]
        assert len(lists) >= 1, "Lista de roles (SysListView32) no encontrada"
        hwnd_role_list = lists[0]

        btn_save = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn_del = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        btn_new = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Nuevo rol"), None)
        assert btn_save is not None and btn_del is not None and btn_new is not None

        # 2.1: Rol Cajero (fila 0)
        select_list_item(hwnd_role_list, 0)
        time.sleep(0.5)

        c_ctrls = get_child_controls(hwnd_main1)
        c_edits = [h for (h, cls, txt) in c_ctrls if cls.lower() == "edit"]
        assert len(c_edits) >= 2, "Campos de texto no encontrados para Cajero"
        assert user32.IsWindowEnabled(c_edits[0]) == 0, "Campo Nombre de Cajero debe estar deshabilitado"
        assert user32.IsWindowEnabled(c_edits[1]) == 0, "Campo Descripción de Cajero debe estar deshabilitado"
        assert user32.IsWindowEnabled(btn_del) == 0, "Botón 'Eliminar' debe estar deshabilitado para rol Cajero"
        assert user32.IsWindowEnabled(btn_save) == 1, "Botón 'Guardar' debe estar habilitado para rol Cajero"

        lbl_notice = next((txt for (h, cls, txt) in c_ctrls if "solo se pueden cambiar sus permisos" in txt), None)
        assert lbl_notice is not None, "Aviso 'Rol de sistema: solo se pueden cambiar sus permisos.' no encontrado"
        print(f"[OK] Aviso Cajero confirmado: '{lbl_notice}'")

        # Interacción completa sobre 'audit.view': clic interruptor, clic título, clic descripción y Espacio con foco
        row_audit = find_toggle_by_perm_id(hwnd_main1, db_perms, "audit.view")
        sw_audit = row_audit["h_sw"]
        assert user32.IsWindowEnabled(sw_audit) == 1, "Interruptor de permisos debe estar habilitado para Cajero"
        assert user32.SendMessageW(sw_audit, BM_GETCHECK, 0, 0) == 0, "Permiso 'audit.view' debe iniciar desactivado para Cajero"

        ensure_row_visible(row_audit["h_scroll"], row_audit["h_row"], "audit.view")

        # 1) Clic en interruptor (0 -> 1)
        click_control_at(sw_audit)
        assert user32.SendMessageW(sw_audit, BM_GETCHECK, 0, 0) == 1, "Clic en interruptor no activó 'audit.view'"

        # 2) Clic en título (1 -> 0)
        click_control_at(row_audit["h_title"], x=12, y=8)
        assert user32.SendMessageW(sw_audit, BM_GETCHECK, 0, 0) == 0, "Clic en título no desactivó 'audit.view'"

        # 3) Clic en descripción (0 -> 1)
        click_control_at(row_audit["h_desc"], x=12, y=8)
        assert user32.SendMessageW(sw_audit, BM_GETCHECK, 0, 0) == 1, "Clic en descripción no activó 'audit.view'"

        # 4) Tecla Espacio con foco (1 -> 0 y luego 0 -> 1 para dejarlo activado)
        user32.PostMessageW(sw_audit, WM_KEYDOWN, VK_SPACE, 0x00390001)
        time.sleep(0.05)
        user32.PostMessageW(sw_audit, WM_KEYUP, VK_SPACE, 0xC0390001)
        time.sleep(0.25)
        assert user32.SendMessageW(sw_audit, BM_GETCHECK, 0, 0) == 0, "Espacio con el foco no desactivó 'audit.view'"

        user32.PostMessageW(sw_audit, WM_KEYDOWN, VK_SPACE, 0x00390001)
        time.sleep(0.05)
        user32.PostMessageW(sw_audit, WM_KEYUP, VK_SPACE, 0xC0390001)
        time.sleep(0.25)
        assert user32.SendMessageW(sw_audit, BM_GETCHECK, 0, 0) == 1, "Espacio con el foco no activó 'audit.view'"
        print("[OK] Clic en interruptor, clic en título, clic en descripción y Espacio con foco probados en 'audit.view'.")

        # Guardar cambios del Cajero
        send_click_button(btn_save)
        time.sleep(0.8)

        con_c = sqlite3.connect(db_path)
        cur_c = con_c.cursor()
        cur_c.execute("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-cashier' AND permission_id = 'audit.view';")
        assert cur_c.fetchone()[0] == 1, "Permiso 'audit.view' no se persistió en role_permissions para Cajero"
        con_c.close()
        print("[OK] Cajero: Permiso 'audit.view' añadido y persistido exitosamente en SQLite.")

        # 2.2: Rol Dueño (fila 1)
        select_list_item(hwnd_role_list, 1)
        time.sleep(0.5)

        d_ctrls = get_child_controls(hwnd_main1)
        d_edits = [h for (h, cls, txt) in d_ctrls if cls.lower() == "edit"]
        assert user32.IsWindowEnabled(d_edits[0]) == 0, "Campo Nombre de Dueño debe estar deshabilitado"
        assert user32.IsWindowEnabled(d_edits[1]) == 0, "Campo Descripción de Dueño debe estar deshabilitado"
        assert user32.IsWindowEnabled(btn_save) == 0, "Botón 'Guardar' debe estar deshabilitado para Dueño"
        assert user32.IsWindowEnabled(btn_del) == 0, "Botón 'Eliminar' debe estar deshabilitado para Dueño"

        row_sell_d = find_toggle_by_perm_id(hwnd_main1, db_perms, "pos.sell")
        assert user32.IsWindowEnabled(row_sell_d["h_sw"]) == 0, "Interruptores de permisos deben estar deshabilitados para Dueño"
        before_d = user32.SendMessageW(row_sell_d["h_sw"], BM_GETCHECK, 0, 0)
        assert before_d == 1, "Dueño debe tener 'pos.sell' activado"
        ensure_row_visible(row_sell_d["h_scroll"], row_sell_d["h_row"], "pos.sell (Dueño)")
        click_control_at(row_sell_d["h_sw"])
        assert user32.SendMessageW(row_sell_d["h_sw"], BM_GETCHECK, 0, 0) == before_d, (
            "Con rol Dueño, un clic en el interruptor cambió su estado"
        )
        click_control_at(row_sell_d["h_title"], x=12, y=8)
        assert user32.SendMessageW(row_sell_d["h_sw"], BM_GETCHECK, 0, 0) == before_d, (
            "Con rol Dueño, un clic en el título cambió su estado"
        )

        lbl_notice_d = next((txt for (h, cls, txt) in d_ctrls if "Rol de sistema (solo lectura" in txt), None)
        assert lbl_notice_d is not None, "Aviso de solo lectura para Dueño no encontrado"
        print(f"[OK] Rol Dueño en solo lectura estricta (clic no cambia estado): '{lbl_notice_d}'")

        # -------------------------------------------------------------
        # CASO 3: Geometría Estricta, Columnas e Interruptores en Estándar y Touch
        # -------------------------------------------------------------
        print("\n--- CASO 3: Verificación Geométrica, Columnas e Interruptores (Estándar y Touch) ---")
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        verify_list_columns(hwnd_role_list, "Estándar")
        verify_permission_matrix(hwnd_main1, db_perms, "Estándar", check_pixels=True)
        verify_guardar_visible_without_scroll(hwnd_main1)

        main_l, main_t, main_r, main_b, main_w, main_h = get_rect(hwnd_main1)
        active_ctrls = get_child_controls(hwnd_main1)
        panel_buttons = [
            (h, txt) for (h, cls, txt) in active_ctrls
            if cls.lower() == "button" and txt in ["Nuevo rol", "Actualizar", "Guardar", "Eliminar"]
        ]
        for bh, btxt in panel_buttons:
            bl, bt, br, bb, bw, bh_h = get_rect(bh)
            assert br <= main_r, f"Botón '{btxt}' excede el margen derecho de la ventana: {br} > {main_r}"
            tw, th = measure_text_extent(bh, btxt)
            assert bw >= tw + 10, f"Botón '{btxt}' recortado horizontalmente: ancho {bw}px < texto {tw}px + 10"
            assert bh_h >= 34, f"Botón '{btxt}' en Estándar altura insuficiente: {bh_h}px < 34px"

        # 3.2: Ventana Estrecha 960x600 en Estándar
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        verify_guardar_visible_without_scroll(hwnd_main1)
        verify_permission_matrix(hwnd_main1, db_perms, "Estándar", check_pixels=False)
        n_l, n_t, n_r, n_b, n_w, n_h = get_rect(hwnd_main1)
        ctrls_960 = get_child_controls(hwnd_main1)
        for h, cls, txt in ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= n_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en 960x600: {cr} > {n_r}"
        print("[OK] Geometría 960x600 en Estándar: Sin desbordamientos horizontales y Guardar visible.")

        # 3.3: Alternar a Perfil Touch
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)

        btn_touch_toggle = None
        for _ in range(20):
            hdr_ctrls = get_child_controls(hwnd_main1)
            btn_touch_toggle = next(
                (h for (h, cls, txt) in hdr_ctrls if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)),
                None
            )
            if btn_touch_toggle:
                break
            time.sleep(0.2)
        assert btn_touch_toggle is not None, "Botón alternador de perfil Touch/Estándar no encontrado"
        send_click_button(btn_touch_toggle)
        time.sleep(0.8)

        t_ctrls = get_child_controls(hwnd_main1)
        t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
        btn_bo_t = next((h for (h, t) in t_btns if "BackOffice" in t), None)
        if btn_bo_t:
            send_click_button(btn_bo_t)
            time.sleep(0.6)
            t_ctrls = get_child_controls(hwnd_main1)
            t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
            btn_roles_t = next((h for (h, t) in t_btns if "Roles y permisos" in t), None)
            if btn_roles_t:
                send_click_button(btn_roles_t)
                time.sleep(0.8)

        t_lists = [h for (h, cls, txt) in get_child_controls(hwnd_main1) if "list" in cls.lower()]
        assert len(t_lists) >= 1, "Lista de roles no encontrada en Touch"
        verify_list_columns(t_lists[0], "Touch")
        verify_permission_matrix(hwnd_main1, db_perms, "Touch", check_pixels=True)
        verify_guardar_visible_without_scroll(hwnd_main1)
        verify_touch_roles_list_and_fields(hwnd_main1, t_lists[0])

        touch_ctrls = get_child_controls(hwnd_main1)
        touch_action_btns = [
            (h, txt) for (h, cls, txt) in touch_ctrls
            if cls.lower() == "button" and txt in ["Nuevo rol", "Actualizar", "Guardar", "Eliminar"]
        ]
        assert len(touch_action_btns) >= 4, f"No se encontraron los 4 botones en Touch (encontrados: {len(touch_action_btns)})"
        for bh, btxt in touch_action_btns:
            _, _, _, _, bw, bh_h = get_rect(bh)
            assert bh_h >= 48, f"ERROR: Botón '{btxt}' en Touch tiene altura {bh_h}px < 48px obligatorio"

        # 3.4: Ventana estrecha 960x600 en Touch
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        verify_guardar_visible_without_scroll(hwnd_main1)
        verify_permission_matrix(hwnd_main1, db_perms, "Touch", check_pixels=False)
        tn_l, tn_t, tn_r, tn_b, tn_w, tn_h = get_rect(hwnd_main1)
        t_ctrls_960 = get_child_controls(hwnd_main1)
        for h, cls, txt in t_ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= tn_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en Touch 960x600: {cr} > {tn_r}"
        print("[OK] Geometría 960x600 en Touch: Controles contenidos sin recortes y Guardar visible.")

        # Regresar a perfil Estándar y tamaño normal para creación de rol
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.3)
        btn_touch_toggle2 = None
        for _ in range(20):
            ctrls2 = get_child_controls(hwnd_main1)
            btn_touch_toggle2 = next(
                (h for (h, cls, txt) in ctrls2 if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)),
                None
            )
            if btn_touch_toggle2:
                break
            time.sleep(0.2)
        if btn_touch_toggle2:
            send_click_button(btn_touch_toggle2)
            time.sleep(0.8)

        r_ctrls = get_child_controls(hwnd_main1)
        r_btns = [(h, txt) for (h, cls, txt) in r_ctrls if cls.lower() == "button"]
        btn_bo_r = next((h for (h, t) in r_btns if "BackOffice" in t), None)
        if btn_bo_r:
            send_click_button(btn_bo_r)
            time.sleep(0.5)
            r_ctrls = get_child_controls(hwnd_main1)
            r_btns = [(h, txt) for (h, cls, txt) in r_ctrls if cls.lower() == "button"]
            btn_roles_r = next((h for (h, t) in r_btns if "Roles y permisos" in t), None)
            if btn_roles_r:
                send_click_button(btn_roles_r)
                time.sleep(0.8)

        # -------------------------------------------------------------
        # CASO 4: Crear Rol Personalizado, Activar 2 Permisos y Guardar
        # -------------------------------------------------------------
        print("\n--- CASO 4: Crear Rol Personalizado, Activar 2 Permisos y Guardar ---")
        cur_ctrls = get_child_controls(hwnd_main1)
        btn_new_act = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and txt == "Nuevo rol"), None)
        assert btn_new_act is not None, "Botón 'Nuevo rol' no encontrado"
        send_click_button(btn_new_act)
        time.sleep(0.4)

        cur_ctrls = get_child_controls(hwnd_main1)
        edits = [h for (h, cls, txt) in cur_ctrls if cls.lower() == "edit"]
        assert len(edits) >= 2, f"Campos de texto insuficientes ({len(edits)}) para nombre y descripción"
        txt_name = edits[0]
        txt_desc = edits[1]

        assert user32.IsWindowEnabled(txt_name) == 1, "Campo Nombre debe estar habilitado para nuevo rol"
        assert user32.IsWindowEnabled(txt_desc) == 1, "Campo Descripción debe estar habilitado para nuevo rol"

        test_role_name = "Supervisor de Barra"
        test_role_desc = "Supervisión de recetas y stock"
        set_text(txt_name, test_role_name)
        set_text(txt_desc, test_role_desc)
        time.sleep(0.2)

        row_sell = find_toggle_by_perm_id(hwnd_main1, db_perms, "pos.sell")
        row_inv = find_toggle_by_perm_id(hwnd_main1, db_perms, "inventory.view")

        assert user32.IsWindowEnabled(row_sell["h_sw"]) == 1, "Interruptor 'pos.sell' debe estar habilitado"
        assert user32.IsWindowEnabled(row_inv["h_sw"]) == 1, "Interruptor 'inventory.view' debe estar habilitado"

        ensure_row_visible(row_sell["h_scroll"], row_sell["h_row"], "pos.sell")
        click_control_at(row_sell["h_sw"])
        ensure_row_visible(row_inv["h_scroll"], row_inv["h_row"], "inventory.view")
        click_control_at(row_inv["h_sw"])

        assert user32.SendMessageW(row_sell["h_sw"], BM_GETCHECK, 0, 0) == 1, "Interruptor 'pos.sell' debe quedar activado"
        assert user32.SendMessageW(row_inv["h_sw"], BM_GETCHECK, 0, 0) == 1, "Interruptor 'inventory.view' debe quedar activado"

        ctrls_after_toggle = get_child_controls(hwnd_main1)
        counter_2 = get_perm_counter_text(hwnd_main1)
        assert counter_2 == "2 de 20 permisos", f"Contador esperado '2 de 20 permisos', encontrado '{counter_2}'"
        print("[OK] Dos permisos activados exitosamente ('pos.sell' e 'inventory.view'); contador = '2 de 20 permisos'.")

        btn_save_act = next((h for (h, cls, txt) in ctrls_after_toggle if cls.lower() == "button" and txt == "Guardar"), None)
        assert btn_save_act is not None and user32.IsWindowEnabled(btn_save_act) == 1

        send_click_button(btn_save_act)
        time.sleep(0.8)

        con1 = sqlite3.connect(db_path)
        cur1 = con1.cursor()
        cur1.execute("SELECT id, name, description, is_system FROM roles WHERE name = ?;", (test_role_name,))
        row_role = cur1.fetchone()
        assert row_role is not None, f"El rol '{test_role_name}' no se insertó en la base de datos"
        assert row_role[3] == 0, "El nuevo rol no debe ser de sistema (is_system=0)"
        created_role_id = row_role[0]

        cur1.execute("SELECT permission_id FROM role_permissions WHERE role_id = ? ORDER BY permission_id ASC;", (created_role_id,))
        saved_perms = [r[0] for r in cur1.fetchall()]
        assert "pos.sell" in saved_perms, "Permiso 'pos.sell' no guardado en role_permissions"
        assert "inventory.view" in saved_perms, "Permiso 'inventory.view' no guardado en role_permissions"
        assert len(saved_perms) == 2, f"Se esperaban 2 permisos, encontrados {len(saved_perms)}: {saved_perms}"
        con1.close()
        print(f"[OK] Rol '{test_role_name}' persistido en SQLite con 2 permisos ({saved_perms}).")

        user32.PostMessageW(hwnd_main1, WM_CLOSE, 0, 0)
        p1.wait(timeout=5)
        print("[OK] Sesión 1 cerrada limpiamente.")

        # =============================================================
        # SESIÓN 2: Reapertura y Comprobación de Persistencia en la UI
        # =============================================================
        print("\n--- SESIÓN 2: Reapertura y Verificación de Persistencia en UI ---")
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid2 = p2.pid

        hwnd_login, _ = wait_for_window_by_pid(pid2, "Iniciar Sesión", step_name="Login Reapertura")
        assert hwnd_login is not None, "Diálogo de login no apareció en reapertura"
        l_ctrls = get_child_controls(hwnd_login)
        l_edits = [h for (h, cls, txt) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], "admin_owner")
        set_text(l_edits[1], "ClaveSegura123!")
        time.sleep(0.3)
        post_click_button(l_btns[0])

        hwnd_main2, _ = wait_for_window_by_pid(
            pid2, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow reapertura"
        )
        assert hwnd_main2 is not None, "MainWindow no abrió tras login en reapertura"
        time.sleep(1.0)

        m2_ctrls = get_child_controls(hwnd_main2)
        m2_btns = [(h, txt) for (h, cls, txt) in m2_ctrls if cls.lower() == "button"]
        btn_bo2 = next((h for (h, t) in m2_btns if "BackOffice" in t), None)
        assert btn_bo2 is not None, "Botón 'BackOffice' no encontrado en sesión 2"
        send_click_button(btn_bo2)
        time.sleep(0.8)

        m2_ctrls = get_child_controls(hwnd_main2)
        m2_btns = [(h, txt) for (h, cls, txt) in m2_ctrls if cls.lower() == "button"]
        btn_roles2 = next((h for (h, t) in m2_btns if "Roles y permisos" in t), None)
        assert btn_roles2 is not None, "Pestaña 'Roles y permisos' no encontrada en sesión 2"
        send_click_button(btn_roles2)
        time.sleep(0.8)

        panel2_ctrls = get_child_controls(hwnd_main2)
        lists2 = [(h, cls, txt) for (h, cls, txt) in panel2_ctrls if "list" in cls.lower()]
        assert len(lists2) >= 1, "Lista de roles no encontrada en sesión 2"
        hwnd_list2 = lists2[0][0]

        # 2.1 Verificar persistencia en Cajero (fila 0)
        select_list_item(hwnd_list2, 1)
        time.sleep(0.3)
        select_list_item(hwnd_list2, 0)
        time.sleep(0.5)
        assert user32.SendMessageW(hwnd_list2, LVM_GETNEXTITEM, -1, 0x0002) == 0, "Cajero (fila 0) no quedó seleccionado tras reabrir"

        p2_c_ctrls = get_child_controls(hwnd_main2)
        p2_c_edits = [h for (h, cls, txt) in p2_c_ctrls if cls.lower() == "edit"]
        assert len(p2_c_edits) >= 2, "Campos de texto de Cajero no encontrados en sesión 2"
        assert get_ctrl_text(p2_c_edits[0]) == "Cajero"
        assert get_ctrl_text(p2_c_edits[1]) == "Operador de caja y punto de venta"
        assert user32.IsWindowEnabled(p2_c_edits[0]) == 0
        assert user32.IsWindowEnabled(p2_c_edits[1]) == 0

        btn2_c_save = next((h for (h, cls, txt) in p2_c_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn2_c_del = next((h for (h, cls, txt) in p2_c_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        assert btn2_c_save is not None and user32.IsWindowEnabled(btn2_c_save) == 1, "Guardar debe estar habilitado para Cajero"
        assert btn2_c_del is not None and user32.IsWindowEnabled(btn2_c_del) == 0, "Eliminar debe estar deshabilitado para Cajero"

        row2_audit = find_toggle_by_perm_id(hwnd_main2, db_perms, "audit.view")
        assert user32.SendMessageW(row2_audit["h_sw"], BM_GETCHECK, 0, 0) == 1, (
            "Permiso 'audit.view' asignado a Cajero debe permanecer activado tras reapertura"
        )
        con_c2 = sqlite3.connect(db_path)
        cur_c2 = con_c2.cursor()
        cur_c2.execute("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-cashier' AND permission_id = 'audit.view';")
        assert cur_c2.fetchone()[0] == 1, "Tras reabrir, SQLite no conserva 'audit.view' del Cajero"
        con_c2.close()
        print("[OK] Cajero: Permisos modificados persisten íntegros tras reinicio (UI tras reselección + SQLite).")

        # 2.2 Verificar Dueño en solo lectura (fila 1)
        select_list_item(hwnd_list2, 1)
        time.sleep(0.5)
        p2_d_ctrls = get_child_controls(hwnd_main2)
        p2_d_edits = [h for (h, cls, txt) in p2_d_ctrls if cls.lower() == "edit"]
        assert user32.IsWindowEnabled(p2_d_edits[0]) == 0
        assert user32.IsWindowEnabled(p2_d_edits[1]) == 0
        btn2_d_save = next((h for (h, cls, txt) in p2_d_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn2_d_del = next((h for (h, cls, txt) in p2_d_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        assert user32.IsWindowEnabled(btn2_d_save) == 0
        assert user32.IsWindowEnabled(btn2_d_del) == 0

        # 2.3 Verificar Rol Personalizado (fila 2: Supervisor de Barra)
        select_list_item(hwnd_list2, 2)
        time.sleep(0.5)

        sel_after = user32.SendMessageW(hwnd_list2, LVM_GETNEXTITEM, -1, 0x0002)
        assert sel_after == 2, f"Fila 2 no quedó seleccionada en la lista (seleccionada: {sel_after})"

        p2_ctrls = get_child_controls(hwnd_main2)
        p2_edits = [h for (h, cls, txt) in p2_ctrls if cls.lower() == "edit"]
        assert len(p2_edits) >= 2, "Campos de texto no encontrados en sesión 2"

        name_val = get_ctrl_text(p2_edits[0])
        desc_val = get_ctrl_text(p2_edits[1])
        assert name_val == test_role_name, f"Nombre del rol no coincide: '{name_val}' != '{test_role_name}'"
        assert desc_val == test_role_desc, f"Descripción del rol no coincide: '{desc_val}' != '{test_role_desc}'"
        assert user32.IsWindowEnabled(p2_edits[0]) == 1, "Nombre de rol personalizado debe ser editable"
        assert user32.IsWindowEnabled(p2_edits[1]) == 1, "Descripción de rol personalizado debe ser editable"
        print(f"[OK] Rol personalizado '{name_val}' cargado íntegramente desde persistencia.")

        row2_sell = find_toggle_by_perm_id(hwnd_main2, db_perms, "pos.sell")
        row2_inv = find_toggle_by_perm_id(hwnd_main2, db_perms, "inventory.view")
        row2_shift = find_toggle_by_perm_id(hwnd_main2, db_perms, "shift.operate")

        assert user32.SendMessageW(row2_sell["h_sw"], BM_GETCHECK, 0, 0) == 1, "Permiso 'pos.sell' debe permanecer activado"
        assert user32.SendMessageW(row2_inv["h_sw"], BM_GETCHECK, 0, 0) == 1, "Permiso 'inventory.view' debe permanecer activado"
        assert user32.SendMessageW(row2_shift["h_sw"], BM_GETCHECK, 0, 0) == 0, "Permiso 'shift.operate' debe permanecer desactivado"

        counter2 = get_perm_counter_text(hwnd_main2)
        assert counter2 == "2 de 20 permisos", f"Contador tras reapertura incorrecto: '{counter2}'"

        btn2_save = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn2_del = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        assert btn2_save is not None and user32.IsWindowEnabled(btn2_save) == 1, "Botón Guardar debe estar habilitado para rol personalizable"
        assert btn2_del is not None and user32.IsWindowEnabled(btn2_del) == 1, "Botón Eliminar debe estar habilitado para rol personalizable"
        print("[OK] Permisos y acciones del rol personalizado persisten y son editables.")

        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=5)
        print("[OK] Sesión 2 cerrada limpiamente.")

        print(f"\n>>> TODAS LAS PRUEBAS DE TAREA F1 / F1b / F5 PASARON EXITOSAMENTE EN [{config_name}] <<<")

    finally:
        for p in [p1, p2]:
            if p and p.poll() is None:
                p.kill()
        shutil.rmtree(test_data_dir, ignore_errors=True)
        print(f"[OK] Directorio temporal aislado limpiado: {test_data_dir}")

def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    release_exe = os.path.join(repo_root, "build_release", "app_desktop.exe")
    debug_exe = os.path.join(repo_root, "build_debug", "app_desktop.exe")

    print("=================================================================")
    print("SUITE DE PRUEBAS AUTOMATIZADA: TAREA F1 / F1b / F5 (ROLES Y PERMISOS)")
    print("=================================================================")

    run_f1_test_for_binary(release_exe, repo_root, "RELEASE")
    run_f1_test_for_binary(debug_exe, repo_root, "DEBUG")

    print("\n=================================================================")
    print("VERIFICACIÓN COMPLETA F1 / F1b / F5: PASARON TODAS LAS PRUEBAS EN RELEASE Y DEBUG")
    print("=================================================================")

if __name__ == "__main__":
    main()
