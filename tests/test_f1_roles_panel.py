#!/usr/bin/env python3
"""
test_f1_roles_panel.py - Verificación rigurosa de panel BackOffice > Roles y permisos (Puertas I6 / Tareas F1 y F1b).

Comprobaciones exigidas por especificación y auditoría F1b:
1. Base temporal aislada y ejecutable real, tanto en Release como en Debug.
2. Rol Cajero (role-cashier):
   - Dueño puede editar permisos: casillas y botón Guardar habilitados.
   - Nombre, Descripción y Eliminar deshabilitados.
   - Aviso explícito: "Rol de sistema: solo se pueden cambiar sus permisos."
   - Marcar permiso nuevo, guardar, cerrar, reabrir y comprobar persistencia íntegra.
3. Rol Dueño (role-owner):
   - Todo en solo lectura: Nombre, Descripción, Eliminar, Guardar y casillas deshabilitados.
   - Aviso explícito: "Rol de sistema (solo lectura: no editable ni eliminable)".
4. role-dev y backup.dev_config no aparecen en ningún control visible de la UI.
5. Columnas de lista "Roles Registrados":
   - LVM_GETCOLUMNWIDTH >= ancho medido del encabezado y texto más largo + 8 px (excepto Descripción con puntos suspensivos).
   - Sin estilo WS_HSCROLL visible en la lista a 1216x759 en ambos perfiles (Estándar y Touch).
6. Matriz de permisos:
   - Casilla alineada con la primera línea de texto (estilo BS_TOP y tolerancia <= 3 px).
   - Texto completo visible (ancho >= texto medido o alto suficiente para cubrir todas las líneas envueltas).
7. Roles creados por el Dueño:
   - Nombre, descripción, casillas, Guardar y Eliminar 100% editables y persistentes.
8. Geometría estricta a 1216x759 y 960x600 en ambos perfiles; en Touch, controles interactivos con altura >= 48 px.
"""

import ctypes
from ctypes import wintypes
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
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongW.restype = wintypes.LONG

gdi32.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(SIZE)]
gdi32.GetTextExtentPoint32W.restype = wintypes.BOOL
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.GetPixel.restype = wintypes.COLORREF

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
BS_CHECKBOX = 0x0002
BS_AUTOCHECKBOX = 0x0003
BS_3STATE = 0x0005
BS_AUTO3STATE = 0x0006
BS_TOP = 0x0400
LVM_GETCOLUMNWIDTH = 0x1000 + 29
LVM_GETNEXTITEM = 0x1000 + 12
LVM_GETITEMRECT = 0x1000 + 14
LVM_GETHEADER = 0x1000 + 31
LVIR_BOUNDS = 0
WM_VSCROLL = 0x0115
SB_LINEUP = 0
SB_LINEDOWN = 1
PROCESS_VM_OPERATION = 0x0008
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT_RESERVE = 0x3000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
CLR_INVALID = 0xFFFFFFFF

WM_SETFOCUS = 0x0007
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
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
SWP_NOZORDER = 0x0004
SWP_NOMOVE = 0x0002

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

def remote_get_item_rect(hwnd_list, index, code=LVIR_BOUNDS):
    """LVM_GETITEMRECT entre procesos: el RECT debe vivir en la memoria de la app, no en la de la prueba.
    Cualquier fallo de OpenProcess/VirtualAllocEx/WriteProcessMemory/ReadProcessMemory FALLA la prueba
    (nunca se omite la comprobación). La memoria remota se libera siempre con VirtualFreeEx."""
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
        rect_in = wintypes.RECT(code, 0, 0, 0)  # LVM_GETITEMRECT recibe el código en rc.left
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

def verify_list_columns(hwnd_list, profile_name):
    """Anchos de columna: encabezado medido con la fuente REAL del encabezado, contenido con la de la lista,
    sin WS_HSCROLL y sin holgura desaprovechada contra el área cliente (sin barra vertical)."""
    style = user32.GetWindowLongW(hwnd_list, GWL_STYLE)
    assert (style & WS_HSCROLL) == 0, f"Lista tiene barra horizontal WS_HSCROLL visible a 1216x759 en {profile_name}"

    hdr = user32.SendMessageW(hwnd_list, LVM_GETHEADER, 0, 0)
    assert hdr, f"LVM_GETHEADER no devolvió el encabezado de la lista en {profile_name}"

    col_widths = [user32.SendMessageW(hwnd_list, LVM_GETCOLUMNWIDTH, i, 0) for i in range(4)]
    titles = ["Nombre", "Descripción", "Sistema", "Permisos"]
    for i, title in enumerate(titles):
        hdr_ext, _ = measure_text_extent(hdr, title)
        assert hdr_ext > 0, f"No se pudo medir el encabezado '{title}' con la fuente del encabezado en {profile_name}"
        assert col_widths[i] >= hdr_ext + 8, (
            f"Encabezado '{title}' recortado: columna {col_widths[i]}px < {hdr_ext + 8}px "
            f"(texto {hdr_ext}px con la fuente del encabezado) en {profile_name}")

    # Col 0: Nombre (contenido con la fuente de la lista)
    ext_cajero, _ = measure_text_extent(hwnd_list, "Cajero")
    ext_dueno, _ = measure_text_extent(hwnd_list, "Dueño")
    max_col0 = max(ext_cajero, ext_dueno)
    assert col_widths[0] >= max_col0 + 8, f"Columna 0 ('Nombre') {col_widths[0]}px < {max_col0 + 8}px en {profile_name}"

    # Col 2: Sistema
    ext_si, _ = measure_text_extent(hwnd_list, "Sí")
    assert col_widths[2] >= ext_si + 8, f"Columna 2 ('Sistema') {col_widths[2]}px < {ext_si + 8}px en {profile_name}"

    # Col 3: Permisos
    ext_num, _ = measure_text_extent(hwnd_list, "16")
    assert col_widths[3] >= ext_num + 8, f"Columna 3 ('Permisos') {col_widths[3]}px < {ext_num + 8}px en {profile_name}"

    # Holgura: la suma de columnas debe usar el área cliente (sin barra vertical) con a lo sumo 8 px libres
    client_w, _ = get_client_size(hwnd_list)
    total = sum(col_widths)
    assert total <= client_w, f"Columnas ({total}px) exceden el área cliente ({client_w}px) en {profile_name}"
    assert client_w - total <= 8, (
        f"Espacio sobrante sin aprovechar: {client_w - total}px > 8px (columnas {total}px, cliente {client_w}px) en {profile_name}")

    print(f"  [{profile_name}] Columnas medidas: N={col_widths[0]}px, D={col_widths[1]}px, S={col_widths[2]}px, P={col_widths[3]}px "
          f"(cliente {client_w}px, holgura {client_w - total}px, sin WS_HSCROLL)")

def ensure_checkbox_visible(h_cb):
    """Desplaza (WM_VSCROLL) la ventana con scroll que contiene la casilla hasta dejarla completamente visible.
    Si no lo logra, FALLA: una casilla fuera de vista no se puede medir con GetPixel."""
    parent = user32.GetParent(h_cb)
    assert parent, "La casilla no tiene ventana padre"
    for _ in range(400):
        _, ct, _, cb, _, _ = get_rect(h_cb)
        rc = wintypes.RECT()
        assert user32.GetClientRect(parent, ctypes.byref(rc)), "GetClientRect del padre falló"
        pt = wintypes.POINT(0, 0)
        assert user32.ClientToScreen(parent, ctypes.byref(pt)), "ClientToScreen del padre falló"
        top, bottom = pt.y, pt.y + rc.bottom
        if ct >= top and cb <= bottom:
            time.sleep(0.12)
            return
        user32.SendMessageW(parent, WM_VSCROLL, SB_LINEDOWN if cb > bottom else SB_LINEUP, 0)
        time.sleep(0.02)
    raise AssertionError(f"No se pudo traer a la vista la casilla '{get_ctrl_text(h_cb)[:25]}' con WM_VSCROLL")

def measure_checkbox_glyph_and_text(h_cb, txt, cw, ch):
    """Devuelve ((glifo_top, glifo_bottom), (texto_top, texto_bottom)) en filas del control, o None en cada
    rango no detectado. Se compara contra el color de fondo REAL del control, nunca contra blanco.
    Fondo: píxel libre a la derecha del texto; si no lo hay, una esquina sin glifo ni texto; si no hay ninguno, FALLA."""
    longest = max(txt.split('\n'), key=len)
    tw, _ = measure_text_extent(h_cb, longest)
    hdc = user32.GetDC(h_cb)
    assert hdc, f"GetDC falló para la casilla '{txt[:25]}'"
    try:
        def px(x, y):
            if x < 0 or y < 0 or x >= cw or y >= ch:
                return None
            v = gdi32.GetPixel(hdc, x, y)
            return None if v == CLR_INVALID else v

        candidates = []
        if cw - 3 > tw + 24:                          # a la derecha del texto
            candidates += [(cw - 3, 2), (cw - 3, ch // 2), (cw - 3, ch - 3)]
        if ch >= 24:                                  # esquina inferior izquierda (bajo el glifo, sin texto)
            candidates.append((3, ch - 3))
        candidates += [(cw - 3, 2), (cw - 3, ch - 3)]  # esquinas derechas
        bg = None
        for (x, y) in candidates:
            v = px(x, y)
            if v is not None:
                bg = v
                break
        assert bg is not None, f"Casilla '{txt[:25]}': no hay ningún píxel libre para el color de fondo (¿fuera de vista?)"

        # Glifo: columnas 0..15, primeras filas del control
        glyph_rows = []
        for y in range(0, min(ch, 24)):
            if any((px(x, y) is not None and px(x, y) != bg) for x in range(0, 16)):
                glyph_rows.append(y)
        glyph = (glyph_rows[0], glyph_rows[-1]) if glyph_rows else None

        # Tinta de la primera línea de texto: a la derecha del glifo, dentro de una altura de línea
        _, line_h = measure_text_extent(h_cb, longest)
        text_rows = []
        for y in range(0, min(ch, max(line_h, 1) + 2)):
            if any((px(x, y) is not None and px(x, y) != bg) for x in range(18, min(cw - 4, tw + 22))):
                text_rows.append(y)
        text = (text_rows[0], text_rows[-1]) if text_rows else None
        return glyph, text
    finally:
        user32.ReleaseDC(h_cb, hdc)

def verify_permission_checkboxes(parent_hwnd, profile_name):
    """Verifica que cada casilla tenga BS_TOP, texto visible y glifo alineado con la primera línea (TODAS)."""
    ctrls = get_child_controls(parent_hwnd)
    cb_ctrls = [
        (h, txt) for (h, cls, txt) in ctrls
        if cls.lower() == "button" and (user32.GetWindowLongW(h, GWL_STYLE) & 0x0F) in (BS_CHECKBOX, BS_AUTOCHECKBOX, BS_3STATE, BS_AUTO3STATE) and len(txt) > 0
    ]
    assert len(cb_ctrls) >= 8, f"Casillas insuficientes ({len(cb_ctrls)}) en {profile_name}"

    for h_cb, txt in cb_ctrls:
        cl, ct, cr, cb, cw, ch = get_rect(h_cb)

        # 1. Estilo Win32: BS_TOP debe estar presente
        style = user32.GetWindowLongW(h_cb, GWL_STYLE)
        assert (style & BS_TOP) == BS_TOP, f"Casilla '{txt[:25]}' sin estilo BS_TOP (style={hex(style)})"

        # 2. Glifo alineado con la primera línea (tolerancia <= 3 px) en TODAS las casillas.
        #    Si no se detecta el glifo, la prueba FALLA.
        ensure_checkbox_visible(h_cb)
        cl, ct, cr, cb, cw, ch = get_rect(h_cb)
        glyph, text_ink = measure_checkbox_glyph_and_text(h_cb, txt, cw, ch)
        assert glyph is not None, f"Glifo de la casilla '{txt[:25]}' NO detectado en {profile_name} (no se omite la comprobación)"
        assert text_ink is not None, f"Tinta del texto de la casilla '{txt[:25]}' NO detectada en {profile_name} (no se omite la comprobación)"
        glyph_center = (glyph[0] + glyph[1]) / 2.0
        text_center = (text_ink[0] + text_ink[1]) / 2.0
        assert abs(glyph_center - text_center) <= 3, (
            f"Glifo de casilla '{txt[:20]}' no alineado con la primera línea: centro del glifo {glyph_center:.1f} "
            f"(filas {glyph[0]}-{glyph[1]}) vs centro del texto {text_center:.1f} (filas {text_ink[0]}-{text_ink[1]}); "
            f"diferencia {abs(glyph_center - text_center):.1f}px > 3px en {profile_name}")

        # 3. Texto completo visible
        lines = txt.count('\n') + 1
        if lines == 1:
            tw, th = measure_text_extent(h_cb, txt)
            assert cw >= tw + 16, f"Casilla '{txt}' recortada: {cw}px < {tw+16}px en {profile_name}"
        else:
            first_line = txt.split('\n')[0]
            tw1, th1 = measure_text_extent(h_cb, first_line)
            assert ch >= lines * th1, f"Casilla multilínea '{first_line[:20]}' altura insuficiente: {ch}px < {lines * th1}px"

    print(f"  [{profile_name}] {len(cb_ctrls)} casillas de permisos verificadas con BS_TOP y centro del glifo a <= 3 px del centro del texto de la primera línea.")

def ink_vertical_extent(h_edit, text):
    """Rango vertical (coordenadas de pantalla) de los píxeles de texto de un campo Edit.
    Fondo: píxel libre a la derecha del texto. FALLA si no hay texto, fondo o tinta."""
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

def run_f1_test_for_binary(exe_path, work_dir, config_name):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN ROLES Y PERMISOS F1 / F1b [{config_name}]:")
    print(f"  Binario:  {exe_path}")
    print(f"=======================================================")
    assert os.path.exists(exe_path), f"Binario no encontrado: {exe_path}"

    test_data_dir = tempfile.mkdtemp(prefix=f"cafeteria_f1_{config_name.lower()}_")
    db_path = os.path.join(test_data_dir, "cafeteria.db")

    p1 = None
    p2 = None

    try:
        env = os.environ.copy()
        env["PATH"] = (
            r"D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll;"
            + work_dir + ";" + env.get("PATH", "")
        )
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
        # CASO 2: Roles de Sistema (Cajero editable en permisos, Dueño solo lectura)
        # -------------------------------------------------------------
        print("\n--- CASO 2: Rol Cajero (permisos editables) y Rol Dueño (solo lectura) ---")
        lists = [h for (h, cls, txt) in panel_ctrls if "list" in cls.lower()]
        assert len(lists) >= 1, "Lista de roles (SysListView32) no encontrada"
        hwnd_role_list = lists[0]

        btn_save = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn_del = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        btn_new = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Nuevo rol"), None)

        assert btn_save is not None, "Botón 'Guardar' no encontrado"
        assert btn_del is not None, "Botón 'Eliminar' no encontrado"
        assert btn_new is not None, "Botón 'Nuevo rol' no encontrado"

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

        # Verificar aviso
        lbl_notice = next((txt for (h, cls, txt) in c_ctrls if "solo se pueden cambiar sus permisos" in txt), None)
        assert lbl_notice is not None, "Aviso 'Rol de sistema: solo se pueden cambiar sus permisos.' no encontrado"
        print(f"[OK] Aviso Cajero confirmado: '{lbl_notice}'")

        # Modificar permiso del Cajero: marcar 'audit.view' ('Consulta de bitácora y eventos auditados')
        cb_audit = next((h for (h, cls, txt) in c_ctrls if cls.lower() == "button" and "Consulta de bitácora" in txt), None)
        assert cb_audit is not None, "Casilla 'Consulta de bitácora' no encontrada"
        assert user32.IsWindowEnabled(cb_audit) == 1, "Casilla de permisos debe estar habilitada para Cajero"
        init_chk = user32.SendMessageW(cb_audit, BM_GETCHECK, 0, 0)
        assert init_chk == 0, "Permiso 'audit.view' no debería estar asignado inicialmente a Cajero"

        send_click_button(cb_audit)
        time.sleep(0.2)
        assert user32.SendMessageW(cb_audit, BM_GETCHECK, 0, 0) == 1, "Permiso 'audit.view' debe quedar marcado"

        # Guardar cambios del Cajero
        send_click_button(btn_save)
        time.sleep(0.8)

        # Comprobar persistencia inmediata en SQLite
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

        d_cb = next((h for (h, cls, txt) in d_ctrls if cls.lower() == "button" and "Venta y cobro" in txt), None)
        assert d_cb is not None and user32.IsWindowEnabled(d_cb) == 0, "Casillas de permisos deben estar deshabilitadas para Dueño"

        lbl_notice_d = next((txt for (h, cls, txt) in d_ctrls if "Rol de sistema (solo lectura" in txt), None)
        assert lbl_notice_d is not None, "Aviso de solo lectura para Dueño no encontrado"
        print(f"[OK] Rol Dueño en solo lectura estricta: '{lbl_notice_d}'")

        # -------------------------------------------------------------
        # CASO 3: Geometría Estricta, Columnas y Casillas en Estándar y Touch
        # -------------------------------------------------------------
        print("\n--- CASO 3: Verificación Geométrica, Columnas y Accesibilidad Táctil ---")
        # 3.1: En Estándar 1200x720 / 1216x759
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        verify_list_columns(hwnd_role_list, "Estándar")
        verify_permission_checkboxes(hwnd_main1, "Estándar")

        # Botones de acción en Estándar
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
        n_l, n_t, n_r, n_b, n_w, n_h = get_rect(hwnd_main1)
        ctrls_960 = get_child_controls(hwnd_main1)
        for h, cls, txt in ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= n_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en 960x600: {cr} > {n_r}"
        print("[OK] Geometría 960x600 en Estándar: Sin desbordamientos horizontales.")

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

        # Volver a ingresar a BackOffice > Roles y permisos tras recarga de perfil
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

        # Verificar columnas y casillas en Touch a 1216x759
        t_lists = [h for (h, cls, txt) in get_child_controls(hwnd_main1) if "list" in cls.lower()]
        assert len(t_lists) >= 1, "Lista de roles no encontrada en Touch"
        verify_list_columns(t_lists[0], "Touch")
        verify_permission_checkboxes(hwnd_main1, "Touch")
        verify_touch_roles_list_and_fields(hwnd_main1, t_lists[0])

        # En Touch: Verificar que todos los botones de acción del panel tengan al menos 48 px de alto
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
        tn_l, tn_t, tn_r, tn_b, tn_w, tn_h = get_rect(hwnd_main1)
        t_ctrls_960 = get_child_controls(hwnd_main1)
        for h, cls, txt in t_ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= tn_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en Touch 960x600: {cr} > {tn_r}"
        print("[OK] Geometría 960x600 en Touch: Controles contenidos sin recortes.")

        # Regresar a perfil Estándar y tamaño normal para la creación de rol
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

        # Reingresar a BackOffice > Roles y permisos
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
        # CASO 4: Crear Rol Personalizado, Marcar 2 Permisos y Guardar
        # -------------------------------------------------------------
        print("\n--- CASO 4: Crear Rol Personalizado, Marcar 2 Permisos y Guardar ---")
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

        cb_sell = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and "Venta y cobro" in txt), None)
        cb_inv = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and "Consulta de existencias" in txt), None)

        assert cb_sell is not None, "Casilla de permiso 'pos.sell' no encontrada"
        assert cb_inv is not None, "Casilla de permiso 'inventory.view' no encontrada"

        assert user32.IsWindowEnabled(cb_sell) == 1, "Casilla 'pos.sell' debe estar habilitada para edición"
        assert user32.IsWindowEnabled(cb_inv) == 1, "Casilla 'inventory.view' debe estar habilitada para edición"

        send_click_button(cb_sell)
        time.sleep(0.1)
        send_click_button(cb_inv)
        time.sleep(0.2)

        assert user32.SendMessageW(cb_sell, BM_GETCHECK, 0, 0) == 1, "Casilla 'pos.sell' debe estar marcada"
        assert user32.SendMessageW(cb_inv, BM_GETCHECK, 0, 0) == 1, "Casilla 'inventory.view' debe estar marcada"
        print("[OK] Dos permisos seleccionados exitosamente ('pos.sell' e 'inventory.view').")

        btn_save_act = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        assert btn_save_act is not None, "Botón 'Guardar' no encontrado"
        assert user32.IsWindowEnabled(btn_save_act) == 1, "Botón 'Guardar' debe estar habilitado"

        send_click_button(btn_save_act)
        time.sleep(0.8)

        # Validar persistencia inmediata en la base SQLite antes de cerrar
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

        # Cerrar sesión 1
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

        # Ir a BackOffice > Roles y permisos
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

        # 2.1 Verificar persistencia en Cajero (fila 0): primero otra fila y luego Cajero, para que la
        #     selección y la lectura de la casilla sean de la selección real y no del estado inicial.
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

        # Verificar que el permiso 'audit.view' persistió marcado
        cb2_audit = next((h for (h, cls, txt) in p2_c_ctrls if cls.lower() == "button" and "Consulta de bitácora" in txt), None)
        assert cb2_audit is not None, "Casilla 'Consulta de bitácora' no encontrada para Cajero"
        assert user32.SendMessageW(cb2_audit, BM_GETCHECK, 0, 0) == 1, "Permiso 'audit.view' asignado a Cajero debe permanecer marcado tras reapertura"
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

        cb2_sell = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and "Venta y cobro" in txt), None)
        cb2_inv = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and "Consulta de existencias" in txt), None)
        cb2_shift = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and "Apertura de turno" in txt), None)

        assert cb2_sell is not None and cb2_inv is not None and cb2_shift is not None
        assert user32.SendMessageW(cb2_sell, BM_GETCHECK, 0, 0) == 1, "Permiso 'pos.sell' debe permanecer marcado"
        assert user32.SendMessageW(cb2_inv, BM_GETCHECK, 0, 0) == 1, "Permiso 'inventory.view' debe permanecer marcado"
        assert user32.SendMessageW(cb2_shift, BM_GETCHECK, 0, 0) == 0, "Permiso 'shift.operate' no marcado debe permanecer desmarcado"

        btn2_save = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn2_del = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        assert btn2_save is not None and user32.IsWindowEnabled(btn2_save) == 1, "Botón Guardar debe estar habilitado para rol personalizable"
        assert btn2_del is not None and user32.IsWindowEnabled(btn2_del) == 1, "Botón Eliminar debe estar habilitado para rol personalizable"
        print("[OK] Permisos y acciones del rol personalizado persisten y son editables.")

        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=5)
        print("[OK] Sesión 2 cerrada limpiamente.")

        print(f"\n>>> TODAS LAS PRUEBAS DE TAREA F1 / F1b PASARON EXITOSAMENTE EN [{config_name}] <<<")

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
    print("SUITE DE PRUEBAS AUTOMATIZADA: TAREA F1 / F1b (ROLES Y PERMISOS)")
    print("=================================================================")

    # 1. Ejecución en Release
    run_f1_test_for_binary(release_exe, repo_root, "RELEASE")

    # 2. Ejecución en Debug
    run_f1_test_for_binary(debug_exe, repo_root, "DEBUG")

    print("\n=================================================================")
    print("VERIFICACIÓN COMPLETA F1 / F1b: PASARON TODAS LAS PRUEBAS EN RELEASE Y DEBUG")
    print("=================================================================")

if __name__ == "__main__":
    main()
