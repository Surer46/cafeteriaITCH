#!/usr/bin/env python3
"""
test_t11_products_recipes.py - Prueba GUI integral de Inventario > Productos y recetas (Bloque T11 / I3).

Verifica Release y Debug con ejecutables reales y SQLite aislado bajo 96 DPI (100%) o 120 DPI (125%):
1. Dueño:
   - Crear producto directo y preparado; comprobar price_cents, category, is_prepared en SQLite.
   - Error de servicio por SKU duplicado sin insertar fila extra en SQLite.
   - Editar precio y categoría; dar de baja y reactivar con confirmación y filtro "Ver dados de baja".
   - Receta con dos ingredientes en unidades distintas (18 g y 200 ml -> 18000 mg y 200 ml en SQLite); quitar uno y guardar.
   - Costo desconocido sin compras ("Falta registrar una compra de:", sin margen) y costo/margen real tras registrar compra en Reabastecimiento (incluyendo margen negativo).
   - Subir imagen PNG válida (diálogo nativo) y quitarla; verificar SQLite product_images.
2. Geometría y Táctil:
   - Estándar y Táctil a 1200x720 y 960x600 (GetWindowRect, GetTextExtent, LVM_GETCOLUMNWIDTH, sin WS_HSCROLL).
   - Teclado numérico T4 en Precio y Cantidad de receta; mínimos >= 48 lógicos en Táctil.
   - Lenguaje llano (sin mg, base_unit, MASS, VOLUME, COUNT, inventory., prod-).
3. Permisos:
   - Cajero ve "Sin permiso para administrar productos y recetas" sin controles de edición.
   - Rol con inventory.manage_products accede y administra catálogo/recetas.
"""

import ctypes
from ctypes import wintypes
import os
import re
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from gui_dpi import (
    RELEASE_EXE,
    DEBUG_EXE,
    user32,
    kernel32,
    WM_CLOSE,
    WM_COMMAND,
    WM_KEYDOWN,
    WM_KEYUP,
    VK_RETURN,
    GWL_STYLE,
    WS_HSCROLL,
    LVM_GETITEMCOUNT,
    HDM_GETITEMCOUNT,
    PROCESS_VM_OPERATION,
    PROCESS_VM_READ,
    PROCESS_VM_WRITE,
    MEM_COMMIT,
    MEM_RESERVE,
    MEM_RELEASE,
    PAGE_READWRITE,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    format_dpi_info,
    get_window_scale,
    min_px,
    max_px,
    ensure_no_app_running,
    send_msg,
    get_text,
    get_rect,
    get_client_size,
    move_window_logical,
    wait_for_window,
    enum_children,
    set_edit_text,
    click_button,
    post_click_button,
    click_control_at,
    select_list_row,
    get_list_item_rect,
    measure_text_extent,
    click_sidebar_section,
    click_subnav,
    find_windows_by_pid,
)

user32.GetDlgCtrlID.argtypes = [wintypes.HWND]
user32.GetDlgCtrlID.restype = ctypes.c_int

CB_SETCURSEL = 0x014E
CB_GETCOUNT = 0x0146
CB_GETLBTEXTLEN = 0x0149
CB_GETLBTEXT = 0x0148
CBN_SELCHANGE = 1
CBN_SELENDOK = 9

LVM_GETCOLUMNWIDTH = 0x101D
LVM_GETHEADER = 0x101F
LVM_GETITEMTEXTW = 0x1073
HDM_GETITEMW = 0x120B
HDI_TEXT = 0x0002
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_LBUTTONDOWN = 0x0201


def get_edit_text(hwnd_edit):
    ln = send_msg(hwnd_edit, WM_GETTEXTLENGTH, 0, 0)
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    send_msg(hwnd_edit, WM_GETTEXT, ln + 2, ctypes.addressof(buf))
    return buf.value


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


def write_test_png(path, width=24, height=24):
    raw_rows = []
    for _ in range(height):
        raw_rows.append(b"\x00" + (b"\xb9\x1c\x1c" * width))
    idat_data = zlib.compress(b"".join(raw_rows))

    def chunk(ctype, data):
        crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png_bytes = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat_data) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png_bytes)


def is_descendant(hwnd_child, hwnd_ancestor):
    cur = hwnd_child
    while cur:
        if cur == hwnd_ancestor:
            return True
        cur = user32.GetParent(cur)
    return False


def select_combo_by_prefix(hwnd_combo, prefix):
    count = send_msg(hwnd_combo, CB_GETCOUNT, 0, 0)
    chosen = -1
    for i in range(count):
        ln = send_msg(hwnd_combo, CB_GETLBTEXTLEN, i, 0)
        buf = ctypes.create_unicode_buffer(ln + 2)
        send_msg(hwnd_combo, CB_GETLBTEXT, i, ctypes.cast(buf, ctypes.c_void_p).value)
        if buf.value == prefix or buf.value.startswith(prefix):
            chosen = i
            break
    if chosen < 0:
        raise AssertionError(f"Opción '{prefix}' no encontrada en ComboBox")
    send_msg(hwnd_combo, CB_SETCURSEL, chosen, 0)
    parent = user32.GetParent(hwnd_combo)
    ctrl_id = user32.GetDlgCtrlID(hwnd_combo)
    send_msg(parent, WM_COMMAND, (CBN_SELCHANGE << 16) | (ctrl_id & 0xFFFF), hwnd_combo)
    send_msg(parent, WM_COMMAND, (CBN_SELENDOK << 16) | (ctrl_id & 0xFFFF), hwnd_combo)
    time.sleep(0.15)


def list_headers(hwnd_list):
    hwnd_hdr = send_msg(hwnd_list, LVM_GETHEADER, 0, 0)
    if not hwnd_hdr:
        return [], 0
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
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


def read_list_rows(hwnd_list, num_cols):
    count = send_msg(hwnd_list, LVM_GETITEMCOUNT, 0, 0)
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    rows = []
    try:
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(LVITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        rem_buf = kernel32.VirtualAllocEx(hproc, None, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            for r in range(count):
                vals = []
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
                    vals.append(out.value)
                rows.append(tuple(vals))
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(hproc, rem_buf, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)
    return rows


def open_inventory_sub(hwnd_main, sub_label):
    click_sidebar_section(hwnd_main, "Inventario")
    time.sleep(0.35)
    click_subnav(hwnd_main, sub_label)
    time.sleep(0.45)


def set_profile_touch(hwnd_main, want_touch, reopen_sub=None):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    scale = get_window_scale(hwnd_main)
    ref_btn = next((h for (h, t) in btns if t in ("Nuevo producto", "Actualizar", "Registrar compra")), None)
    if ref_btn is None:
        is_touch = any(t == "Estándar" for (_h, t) in btns)
    else:
        is_touch = (get_rect(ref_btn)[3] - get_rect(ref_btn)[1]) >= min_px(48, scale)
    if is_touch == want_touch:
        return
    toggle = next((h for (h, t) in btns if "Touch" in t or "Estándar" in t), None)
    assert toggle is not None, "No se encontró botón de perfil Touch/Estándar"
    click_button(toggle)
    time.sleep(0.45)
    if reopen_sub:
        open_inventory_sub(hwnd_main, reopen_sub)


def locate_products_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    btn_new = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Nuevo producto"), None)
    assert btn_new is not None, "Botón 'Nuevo producto' no encontrado"
    list_card = user32.GetParent(btn_new)
    panel = user32.GetParent(list_card)

    lists = sorted(
        [(h, get_rect(h)) for (h, c, _t) in children if c.lower() == "syslistview32" and is_descendant(h, panel)],
        key=lambda x: x[1][0],
    )
    prod_list = lists[0][0] if lists else None
    recipe_list = lists[1][0] if len(lists) >= 2 and user32.IsWindowVisible(lists[1][0]) else None

    left_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, list_card)],
        key=lambda x: x[1][1],
    )
    right_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel) and not is_descendant(h, list_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    # En derecha, orden vertical/horizontal: fila 1 (Nombre, Precio), fila 2 (SKU, Código), fila 3 (Categoría), fila receta (Cantidad)
    right_combos = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel) and not is_descendant(h, list_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    left_combos = [
        h for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, list_card)
    ]
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, panel)}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, panel) and t]

    return {
        "panel": panel,
        "list": prod_list,
        "recipe_list": recipe_list,
        "txt_search": left_edits[0][0] if left_edits else None,
        "txt_name": right_edits[0][0] if len(right_edits) >= 1 else None,
        "txt_price": right_edits[1][0] if len(right_edits) >= 2 else None,
        "txt_sku": right_edits[2][0] if len(right_edits) >= 3 else None,
        "txt_barcode": right_edits[3][0] if len(right_edits) >= 4 else None,
        "txt_category": right_edits[4][0] if len(right_edits) >= 5 else None,
        "txt_recipe_qty": right_edits[5][0] if len(right_edits) >= 6 else None,
        "combo_filter_cat": left_combos[0] if left_combos else None,
        "combo_cat": right_combos[0][0] if len(right_combos) >= 1 else None,
        "combo_recipe_ing": right_combos[1][0] if len(right_combos) >= 2 else None,
        "combo_recipe_unit": right_combos[2][0] if len(right_combos) >= 3 else None,
        "btns": btns,
        "statics": statics,
    }


def _click_keypad_btn(hwnd_btn):
    l, t, r, b = get_rect(hwnd_btn)
    lparam = ((max(1, (b - t) // 2)) << 16) | (max(1, (r - l) // 2) & 0xFFFF)
    user32.PostMessageW(hwnd_btn, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.03)
    post_click_button(hwnd_btn)


def enter_keypad_digits(hwnd_main, edit_hwnd, seq):
    click_control_at(edit_hwnd)
    time.sleep(0.25)
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    assert any(t == "Listo" for (_h, t) in btns), "Teclado numérico no visible"
    btn_clr = next((h for (h, t) in btns if t == "Limpiar"), None)
    assert btn_clr is not None, "Botón Limpiar de teclado numérico no encontrado"
    _click_keypad_btn(btn_clr)
    time.sleep(0.1)
    for ch in seq:
        btns_now = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
        target = next((h for (h, t) in btns_now if t == ch), None)
        assert target is not None, f"Tecla '{ch}' no encontrada en teclado numérico"
        _click_keypad_btn(target)
        time.sleep(0.08)
    btns_end = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_ok = next((h for (h, t) in btns_end if t == "Listo"), None)
    assert btn_ok is not None, "Botón Listo de teclado numérico no encontrado"
    _click_keypad_btn(btn_ok)
    time.sleep(0.2)


def accept_file_dialog(pid, file_path):
    deadline = time.time() + 8.0
    hwnd_fd = None
    while time.time() < deadline:
        wins = [h for (h, t) in find_windows_by_pid(pid) if "Seleccionar imagen" in t or "Abrir" in t]
        if wins:
            hwnd_fd = wins[0]
            break
        time.sleep(0.15)
    assert hwnd_fd is not None, "No apareció el diálogo de selección de archivo"
    time.sleep(0.45)
    children = enum_children(hwnd_fd)
    edits = [h for (h, c, _t) in children if c.lower() == "edit" and user32.IsWindowVisible(h)]
    assert edits, "No se encontró Edit de ruta en diálogo de archivo"

    def _pcls(hw):
        ph = user32.GetParent(hw)
        if not ph:
            return ""
        buf = ctypes.create_unicode_buffer(128)
        user32.GetClassNameW(ph, buf, 128)
        return buf.value.lower()

    target_edit = next((h for h in edits if "combobox" in _pcls(h)), max(edits, key=lambda h: get_rect(h)[1]))
    set_edit_text(target_edit, file_path)
    time.sleep(0.15)
    btn_open = next(
        (h for (h, c, t) in children if c.lower() == "button" and user32.IsWindowVisible(h) and ("Abrir" in t or "Open" in t or t == "&Abrir")),
        None,
    )
    if btn_open is not None:
        post_click_button(btn_open)
    else:
        user32.PostMessageW(target_edit, WM_KEYDOWN, VK_RETURN, 0)
        user32.PostMessageW(target_edit, WM_KEYUP, VK_RETURN, 0)
    time.sleep(0.5)


def confirm_message_dialog(pid):
    hwnd_dlg, _ = wait_for_window(pid, "Confirmar cambio de estado", timeout=6.0)
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_dlg) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_yes = next((h for (h, t) in btns if "Sí" in t or "Yes" in t), btns[0][0])
    post_click_button(btn_yes)
    time.sleep(0.4)


def assert_panel_geometry(hwnd_main, panel_hwnd, lists_spec, touch_btns, touch_mode, width, height):
    scale = move_window_logical(hwnd_main, 20, 20, width, height)
    time.sleep(0.55)
    p_rect = get_rect(panel_hwnd)
    main_rc = get_rect(hwnd_main)
    edge_tol = max_px(2, scale)
    max_slack_px = max_px(8, scale)
    min_touch_px = min_px(48, scale)
    min_row_px = min_px(40, scale)

    for lbl, h_ctrl in touch_btns:
        if h_ctrl is None or not user32.IsWindowVisible(h_ctrl):
            continue
        r = get_rect(h_ctrl)
        assert r[2] <= p_rect[2] + edge_tol and r[3] <= p_rect[3] + edge_tol, (
            f"Control '{lbl}' se sale del panel en {width}x{height}: ctrl={r}, panel={p_rect}"
        )
        assert r[2] <= main_rc[2] + edge_tol and r[3] <= main_rc[3] + edge_tol, (
            f"Control '{lbl}' se sale de la ventana en {width}x{height}: ctrl={r}, main={main_rc}"
        )
        if touch_mode:
            assert (r[3] - r[1]) >= min_touch_px, (
                f"Control Táctil '{lbl}' mide {r[3] - r[1]} px (< {min_touch_px} px)"
            )

    for lst_hwnd, expected_headers in lists_spec:
        if lst_hwnd is None or not user32.IsWindowVisible(lst_hwnd):
            continue
        headers, hdr_hwnd = list_headers(lst_hwnd)
        assert headers == expected_headers, f"Encabezados inesperados: {headers} vs {expected_headers}"
        cw, _ = get_client_size(lst_hwnd)
        widths = [send_msg(lst_hwnd, LVM_GETCOLUMNWIDTH, i, 0) for i in range(len(headers))]
        for i, title in enumerate(headers):
            hw, _ = measure_text_extent(hdr_hwnd, title)
            assert widths[i] >= hw, f"Columna {i} ('{title}') recortada: width={widths[i]} < hdr={hw}"
        assert sum(widths) <= cw, f"Suma columnas {sum(widths)} > client_w {cw}"
        assert (cw - sum(widths)) <= max_slack_px, f"Holgura excesiva en lista: {cw - sum(widths)} > {max_slack_px}"
        style = user32.GetWindowLongW(lst_hwnd, GWL_STYLE)
        assert (style & WS_HSCROLL) == 0, f"Lista con WS_HSCROLL en {width}x{height} touch={touch_mode}"
        if touch_mode and send_msg(lst_hwnd, LVM_GETITEMCOUNT, 0, 0) > 0:
            irc = get_list_item_rect(lst_hwnd, 0)
            row_h = irc[3] - irc[1]
            assert row_h >= min_row_px, f"Fila Táctil mide {row_h} px (< {min_row_px} px)"

    leaves = []
    for (h, cls, txt) in enum_children(panel_hwnd):
        if not user32.IsWindowVisible(h):
            continue
        r = get_rect(h)
        if r[0] < p_rect[0] - edge_tol or r[1] < p_rect[1] - edge_tol or r[2] > p_rect[2] + edge_tol or r[3] > p_rect[3] + edge_tol:
            continue
        cl = cls.lower()
        if cl in ("static", "button", "edit", "combobox", "syslistview32"):
            leaves.append((h, cl, txt, r))
            if cl in ("static", "button") and txt and "\n" not in txt:
                tw, _ = measure_text_extent(h, txt)
                cw, _ = get_client_size(h)
                assert tw <= cw + edge_tol, f"Texto recortado en {cl} '{txt}': text_w={tw} > client_w={cw}"

    for i in range(len(leaves)):
        h1, c1, t1, r1 = leaves[i]
        for j in range(i + 1, len(leaves)):
            h2, c2, t2, r2 = leaves[j]
            if user32.GetParent(h1) == h2 or user32.GetParent(h2) == h1:
                continue
            ix = max(0, min(r1[2], r2[2]) - max(r1[0], r2[0]))
            iy = max(0, min(r1[3], r2[3]) - max(r1[1], r2[1]))
            assert ix * iy == 0, f"Solapamiento detectado: ({c1}, '{t1}', {r1}) vs ({c2}, '{t2}', {r2})"


def assert_no_jargon_in_panel(panel_hwnd, lists_cols):
    texts = [(cls, txt) for (h, cls, txt) in enum_children(panel_hwnd) if user32.IsWindowVisible(h) and txt]
    for lst_hwnd, ncols in lists_cols:
        if lst_hwnd and user32.IsWindowVisible(lst_hwnd):
            for row in read_list_rows(lst_hwnd, ncols):
                for cell in row:
                    texts.append(("cell", cell))
    forbidden = ["MASS", "VOLUME", "COUNT", "base_unit", "inventory.", "prod-", "ing-"]
    for kind, txt in texts:
        for bad in forbidden:
            assert bad not in txt, f"Jerga prohibida '{bad}' en {kind}: '{txt}'"
        assert not re.search(r"\bmg\b", txt), f"'mg' técnico visible en {kind}: '{txt}'"


def login_window(exe_path, work_dir, env, username, password):
    p = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    hwnd_l, _ = wait_for_window(p.pid, "Iniciar Sesión")
    lc = enum_children(hwnd_l)
    edits = [h for (h, c, _t) in lc if c.lower() == "edit"]
    btns = [h for (h, c, _t) in lc if c.lower() == "button"]
    set_edit_text(edits[0], username)
    set_edit_text(edits[1], password)
    click_button(btns[0])
    hwnd_m, _ = wait_for_window(p.pid, "Cafetería ITCH II", exclude=("Iniciar Sesión", "Primer Inicio"))
    return p, hwnd_m


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba T11 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t11_{label.lower()}_", ignore_cleanup_errors=True) as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        png_path = os.path.join(data_dir, "producto_t11.png")
        write_test_png(png_path)

        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"

        procs = []
        owner_user = "dueno_t11"
        owner_pass = "ClaveT11!2026"

        try:
            # 1. Primer inicio: alta de Dueño
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_first, _ = wait_for_window(p1.pid, "Primer Inicio")
            fc = enum_children(hwnd_first)
            fe = [h for (h, c, _t) in fc if c.lower() == "edit"]
            fb = [h for (h, c, _t) in fc if c.lower() == "button"]
            set_edit_text(fe[0], "Dueno T11")
            set_edit_text(fe[1], owner_user)
            set_edit_text(fe[2], "dueno_t11@cafeteria.local")
            set_edit_text(fe[3], owner_pass)
            set_edit_text(fe[4], owner_pass)
            click_button(fb[0])
            hwnd_main, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"))
            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p1.wait(timeout=6)

            # Sembrar ingredientes + usuarios Cajero y CatálogoCustom en SQLite
            con = sqlite3.connect(db_path)
            try:
                cur = con.cursor()
                cur.executemany(
                    "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) VALUES (?, ?, ?, ?, ?, ?)",
                    [
                        ("ing-cafe-t11", "Café grano", "MASS", "mg", 0, 100000),
                        ("ing-leche-t11", "Leche entera", "VOLUME", "ml", 0, 500),
                    ]
                )
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-cajero-t11', 'cajero_t11', 'Cajero T11', 'cajero_t11@cafeteria.local', password_hash, 'role-cashier', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,)
                )
                cur.execute(
                    "INSERT INTO roles (id, name, description, is_system) VALUES ('role-cat-t11', 'Catálogo T11', 'Solo productos y recetas', 0)"
                )
                cur.executemany(
                    "INSERT INTO role_permissions (role_id, permission_id) VALUES ('role-cat-t11', ?)",
                    [("inventory.manage_products",), ("inventory.view",)]
                )
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-cat-t11', 'catalogo_t11', 'Catalogo T11', 'catalogo_t11@cafeteria.local', password_hash, 'role-cat-t11', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,)
                )
                con.commit()
            finally:
                con.close()

            # 2. Sesión Dueño: Productos y recetas
            p2, hwnd_main2 = login_window(exe_path, work_dir, env, owner_user, owner_pass)
            procs.append(p2)
            open_inventory_sub(hwnd_main2, "Productos y recetas")
            set_profile_touch(hwnd_main2, False, reopen_sub="Productos y recetas")
            pc = locate_products_controls(hwnd_main2)

            # Crear producto directo: Galleta Chispas ($18.00)
            set_edit_text(pc["txt_name"], "Galleta Chispas")
            set_edit_text(pc["txt_price"], "18.00")
            set_edit_text(pc["txt_sku"], "GAL-01")
            set_edit_text(pc["txt_category"], "Repostería")
            click_button(pc["btns"]["Directo"])
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)

            # Crear producto preparado: Latte Clásico ($55.00)
            pc = locate_products_controls(hwnd_main2)
            click_button(pc["btns"]["Nuevo producto"])
            time.sleep(0.2)
            pc = locate_products_controls(hwnd_main2)
            set_edit_text(pc["txt_name"], "Latte Clásico")
            set_edit_text(pc["txt_price"], "55.00")
            set_edit_text(pc["txt_sku"], "LAT-01")
            set_edit_text(pc["txt_barcode"], "750999000111")
            set_edit_text(pc["txt_category"], "Bebidas")
            click_button(pc["btns"]["Preparado"])
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                rows_db = con.execute(
                    "SELECT name, sku, category, price_cents, is_prepared, is_active FROM products ORDER BY sku ASC"
                ).fetchall()
            finally:
                con.close()
            assert ("Galleta Chispas", "GAL-01", "Repostería", 1800, 0, 1) in rows_db, rows_db
            assert ("Latte Clásico", "LAT-01", "Bebidas", 5500, 1, 1) in rows_db, rows_db

            # SKU repetido: error del servicio y no crea nada
            pc = locate_products_controls(hwnd_main2)
            click_button(pc["btns"]["Nuevo producto"])
            time.sleep(0.2)
            pc = locate_products_controls(hwnd_main2)
            set_edit_text(pc["txt_name"], "Copia Latte")
            set_edit_text(pc["txt_price"], "40.00")
            set_edit_text(pc["txt_sku"], "LAT-01")
            set_edit_text(pc["txt_category"], "Bebidas")
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.3)
            pc = locate_products_controls(hwnd_main2)
            assert any("SKU ya existe" in s for s in pc["statics"]), f"Debe mostrar error de SKU repetido: {pc['statics']}"
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT COUNT(*) FROM products").fetchone()[0] == 2
            con.close()

            # Seleccionar Latte Clásico y editar precio ($60.00) + categoría ("Bebidas Calientes")
            prod_rows = read_list_rows(pc["list"], 5)
            latte_idx = next(i for i, r in enumerate(prod_rows) if r[0] == "Latte Clásico")
            select_list_row(pc["list"], latte_idx)
            time.sleep(0.3)
            pc = locate_products_controls(hwnd_main2)
            set_edit_text(pc["txt_price"], "60.00")
            set_edit_text(pc["txt_category"], "Bebidas Calientes")
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT price_cents, category FROM products WHERE sku='LAT-01'").fetchone() == (6000, "Bebidas Calientes")
            con.close()

            # Dar de baja Galleta Chispas -> confirmar -> filtrar Ver dados de baja -> Reactivar
            pc = locate_products_controls(hwnd_main2)
            prod_rows = read_list_rows(pc["list"], 5)
            gal_idx = next(i for i, r in enumerate(prod_rows) if r[0] == "Galleta Chispas")
            select_list_row(pc["list"], gal_idx)
            time.sleep(0.25)
            pc = locate_products_controls(hwnd_main2)
            post_click_button(pc["btns"]["Dar de baja"])
            confirm_message_dialog(p2.pid)
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT is_active FROM products WHERE sku='GAL-01'").fetchone()[0] == 0
            con.close()

            pc = locate_products_controls(hwnd_main2)
            assert all(r[0] != "Galleta Chispas" for r in read_list_rows(pc["list"], 5)), "Dado de baja debe ocultarse por defecto"
            click_button(pc["btns"]["Ver dados de baja"])
            time.sleep(0.25)
            pc = locate_products_controls(hwnd_main2)
            prod_rows_all = read_list_rows(pc["list"], 5)
            gal_row = next(r for r in prod_rows_all if r[0] == "Galleta Chispas")
            assert gal_row[4] == "Dado de baja", gal_row
            gal_idx2 = prod_rows_all.index(gal_row)
            select_list_row(pc["list"], gal_idx2)
            time.sleep(0.25)
            pc = locate_products_controls(hwnd_main2)
            post_click_button(pc["btns"]["Reactivar"])
            confirm_message_dialog(p2.pid)
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT is_active FROM products WHERE sku='GAL-01'").fetchone()[0] == 1
            con.close()
            pc = locate_products_controls(hwnd_main2)
            click_button(pc["btns"]["Viendo dados de baja"])
            time.sleep(0.2)

            # Imagen PNG: subir y quitar
            pc = locate_products_controls(hwnd_main2)
            prod_rows = read_list_rows(pc["list"], 5)
            latte_idx = next(i for i, r in enumerate(prod_rows) if r[0] == "Latte Clásico")
            select_list_row(pc["list"], latte_idx)
            time.sleep(0.3)
            pc = locate_products_controls(hwnd_main2)
            post_click_button(pc["btns"]["Elegir imagen"])
            accept_file_dialog(p2.pid, png_path)
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT COUNT(*) FROM product_images").fetchone()[0] == 1, "Imagen PNG no persistió en SQLite"
            con.close()
            pc = locate_products_controls(hwnd_main2)
            assert any("Imagen asignada" in s for s in pc["statics"]), pc["statics"]
            click_button(pc["btns"]["Quitar imagen"])
            time.sleep(0.3)
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT COUNT(*) FROM product_images").fetchone()[0] == 0, "Quitar imagen no borró fila de SQLite"
            con.close()

            # Receta: añadir 18 g de Café grano y 200 ml de Leche entera -> guardar -> validar SQLite
            pc = locate_products_controls(hwnd_main2)
            select_combo_by_prefix(pc["combo_recipe_ing"], "Café grano")
            select_combo_by_prefix(pc["combo_recipe_unit"], "g")
            set_edit_text(pc["txt_recipe_qty"], "18")
            click_button(pc["btns"]["Añadir o cambiar"])
            time.sleep(0.2)

            select_combo_by_prefix(pc["combo_recipe_ing"], "Leche entera")
            select_combo_by_prefix(pc["combo_recipe_unit"], "ml")
            set_edit_text(pc["txt_recipe_qty"], "200")
            click_button(pc["btns"]["Añadir o cambiar"])
            time.sleep(0.2)

            click_button(pc["btns"]["Guardar receta"])
            time.sleep(0.35)
            con = sqlite3.connect(db_path)
            try:
                rcp_items = con.execute(
                    """
                    SELECT i.name, ri.quantity_base_units
                    FROM recipe_items ri
                    JOIN recipes r ON r.id = ri.recipe_id
                    JOIN products p ON p.id = r.product_id
                    JOIN ingredients i ON i.id = ri.ingredient_id
                    WHERE p.sku = 'LAT-01'
                    AND r.version = (SELECT MAX(version) FROM recipes WHERE product_id = p.id)
                    ORDER BY i.name ASC
                    """
                ).fetchall()
            finally:
                con.close()
            assert rcp_items == [("Café grano", 18000), ("Leche entera", 200)], f"Receta incorrecta en SQLite: {rcp_items}"

            # Sin compras: aviso de costo faltante y SIN margen
            pc = locate_products_controls(hwnd_main2)
            assert any("Falta registrar una compra de:" in s and "Café grano" in s and "Leche entera" in s for s in pc["statics"]), pc["statics"]
            assert not any("Margen:" in s for s in pc["statics"]), f"No debe mostrar Margen cuando falta costo: {pc['statics']}"

            # Quitar Leche entera y volver a guardar -> verificar SQLite
            rcp_ui = read_list_rows(pc["recipe_list"], 2)
            leche_rcp_idx = next(i for i, r in enumerate(rcp_ui) if r[0] == "Leche entera")
            select_list_row(pc["recipe_list"], leche_rcp_idx)
            time.sleep(0.2)
            click_button(pc["btns"]["Quitar"])
            time.sleep(0.2)
            click_button(pc["btns"]["Guardar receta"])
            time.sleep(0.35)
            con = sqlite3.connect(db_path)
            try:
                rcp_items2 = con.execute(
                    """
                    SELECT i.name, ri.quantity_base_units
                    FROM recipe_items ri
                    JOIN recipes r ON r.id = ri.recipe_id
                    JOIN products p ON p.id = r.product_id
                    JOIN ingredients i ON i.id = ri.ingredient_id
                    WHERE p.sku = 'LAT-01'
                    AND r.version = (SELECT MAX(version) FROM recipes WHERE product_id = p.id)
                    """
                ).fetchall()
            finally:
                con.close()
            assert rcp_items2 == [("Café grano", 18000)], f"Tras quitar Leche entera debió quedar solo Café grano: {rcp_items2}"

            # Registrar compra real de Café grano vía Reabastecimiento (1 kg por $200.00 => 18 g = $3.60)
            open_inventory_sub(hwnd_main2, "Reabastecimiento")
            rs_children = enum_children(hwnd_main2)
            rs_btn = next(h for (h, c, t) in rs_children if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Registrar compra")
            rs_panel = user32.GetParent(rs_btn)
            rs_combos = sorted([(h, get_rect(h)) for (h, c, _t) in enum_children(rs_panel) if user32.IsWindowVisible(h) and c.lower() == "combobox"], key=lambda x: x[1][1])
            rs_edits = sorted([(h, get_rect(h)) for (h, c, _t) in enum_children(rs_panel) if user32.IsWindowVisible(h) and c.lower() == "edit"], key=lambda x: x[1][1])
            select_combo_by_prefix(rs_combos[0][0], "Café grano")
            select_combo_by_prefix(rs_combos[1][0], "kg")
            set_edit_text(rs_edits[0][0], "1")
            set_edit_text(rs_edits[1][0], "200.00")
            click_button(rs_btn)
            time.sleep(0.35)

            # Volver a Productos y recetas y comprobar costo por porción ($3.60) y margen ($56.40)
            open_inventory_sub(hwnd_main2, "Productos y recetas")
            pc = locate_products_controls(hwnd_main2)
            prod_rows = read_list_rows(pc["list"], 5)
            latte_idx = next(i for i, r in enumerate(prod_rows) if r[0] == "Latte Clásico")
            select_list_row(pc["list"], latte_idx)
            time.sleep(0.35)
            pc = locate_products_controls(hwnd_main2)
            assert any("Costo por porción: $3.60" in s for s in pc["statics"]), f"Falta costo por porción $3.60: {pc['statics']}"
            assert any("Margen: $56.40" in s for s in pc["statics"]), f"Falta margen $56.40: {pc['statics']}"
            assert_no_jargon_in_panel(pc["panel"], [(pc["list"], 5), (pc["recipe_list"], 2)])

            # Margen negativo al bajar precio a $2.00 -> -$1.60
            set_edit_text(pc["txt_price"], "2.00")
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)
            pc = locate_products_controls(hwnd_main2)
            assert any("Margen: -$1.60" in s for s in pc["statics"]), f"Falta margen negativo -$1.60: {pc['statics']}"
            # Restaurar precio $60.00
            set_edit_text(pc["txt_price"], "60.00")
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)

            # Filtros: búsqueda por SKU y por categoría
            pc = locate_products_controls(hwnd_main2)
            set_edit_text(pc["txt_search"], "GAL-01")
            time.sleep(0.25)
            assert len(read_list_rows(pc["list"], 5)) == 1, "Búsqueda por SKU falló"
            set_edit_text(pc["txt_search"], "")
            time.sleep(0.25)

            # Geometría Estándar 1200x720 y 960x600
            pc = locate_products_controls(hwnd_main2)
            assert_panel_geometry(
                hwnd_main2, pc["panel"],
                [(pc["list"], ["Nombre", "Categoría", "Precio", "Tipo", "Estado"])],
                [("btn_new", pc["btns"]["Nuevo producto"]), ("btn_refresh", pc["btns"]["Actualizar"])],
                False, 1200, 720,
            )
            assert_panel_geometry(
                hwnd_main2, pc["panel"],
                [(pc["list"], ["Nombre", "Categoría", "Precio", "Tipo", "Estado"])],
                [("btn_new", pc["btns"]["Nuevo producto"]), ("btn_refresh", pc["btns"]["Actualizar"])],
                False, 960, 600,
            )

            # 3. Perfil Táctil + teclado numérico T4 en Precio y Cantidad de receta + geometría 1200x720 y 960x600
            set_profile_touch(hwnd_main2, True, reopen_sub="Productos y recetas")
            pc_t = locate_products_controls(hwnd_main2)
            latte_idx_t = next(i for i, r in enumerate(read_list_rows(pc_t["list"], 5)) if r[0] == "Latte Clásico")
            select_list_row(pc_t["list"], latte_idx_t)
            time.sleep(0.3)
            pc_t = locate_products_controls(hwnd_main2)

            enter_keypad_digits(hwnd_main2, pc_t["txt_price"], "58.50")
            assert get_edit_text(pc_t["txt_price"]) == "58.50", f"Keypad no actualizó Precio: '{get_edit_text(pc_t['txt_price'])}'"
            enter_keypad_digits(hwnd_main2, pc_t["txt_recipe_qty"], "20")
            assert get_edit_text(pc_t["txt_recipe_qty"]) == "20", f"Keypad no actualizó Cantidad de receta: '{get_edit_text(pc_t['txt_recipe_qty'])}'"

            pc_t = locate_products_controls(hwnd_main2)
            assert_panel_geometry(
                hwnd_main2, pc_t["panel"],
                [(pc_t["list"], ["Nombre", "Categoría", "Precio", "Tipo", "Estado"])],
                [
                    ("btn_new", pc_t["btns"]["Nuevo producto"]),
                    ("btn_refresh", pc_t["btns"]["Actualizar"]),
                    ("btn_toggle_inactive", pc_t["btns"]["Ver dados de baja"]),
                    ("btn_save", pc_t["btns"]["Guardar"]),
                ],
                True, 1200, 720,
            )
            assert_panel_geometry(
                hwnd_main2, pc_t["panel"],
                [(pc_t["list"], ["Nombre", "Categoría", "Precio", "Tipo", "Estado"])],
                [
                    ("btn_new", pc_t["btns"]["Nuevo producto"]),
                    ("btn_refresh", pc_t["btns"]["Actualizar"]),
                    ("btn_toggle_inactive", pc_t["btns"]["Ver dados de baja"]),
                ],
                True, 960, 600,
            )

            user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
            p2.wait(timeout=6)

            # 4. Permisos: Cajero ve "Sin permiso para administrar productos y recetas"
            p3, hwnd_main3 = login_window(exe_path, work_dir, env, "cajero_t11", owner_pass)
            procs.append(p3)
            open_inventory_sub(hwnd_main3, "Productos y recetas")
            vis_c = [(c.lower(), t) for (h, c, t) in enum_children(hwnd_main3) if user32.IsWindowVisible(h)]
            assert not any(c == "button" and t == "Guardar" for (c, t) in vis_c), "Cajero no debe ver formulario de productos"
            assert not any(c == "syslistview32" for (c, _t) in vis_c), "Cajero no debe ver lista de productos"
            assert any(c == "static" and "Sin permiso para administrar productos y recetas" in t for (c, t) in vis_c), vis_c
            user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
            p3.wait(timeout=6)

            # 5. Permisos: Rol con inventory.manage_products sí entra
            p4, hwnd_main4 = login_window(exe_path, work_dir, env, "catalogo_t11", owner_pass)
            procs.append(p4)
            open_inventory_sub(hwnd_main4, "Productos y recetas")
            set_profile_touch(hwnd_main4, False, reopen_sub="Productos y recetas")
            pc_role = locate_products_controls(hwnd_main4)
            assert send_msg(pc_role["list"], LVM_GETITEMCOUNT, 0, 0) == 2, "Rol con inventory.manage_products debe ver productos"
            user32.PostMessageW(hwnd_main4, WM_CLOSE, 0, 0)
            p4.wait(timeout=6)

            print(f"[OK] {label} completo.", flush=True)
        finally:
            for p in procs:
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=3)
                    except Exception:
                        p.kill()


def main():
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {format_dpi_info(sys_dpi)}", flush=True)
    ensure_no_app_running()
    run_suite("Release", RELEASE_EXE)
    ensure_no_app_running()
    run_suite("Debug", DEBUG_EXE)
    print("=== PRUEBA T11 COMPLETA EXITOSA (Release y Debug) ===", flush=True)


if __name__ == "__main__":
    main()
