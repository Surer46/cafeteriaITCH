#!/usr/bin/env python3
"""
test_t10_restock_bar.py - Prueba GUI integral de Inventario > Reabastecimiento y Barra (Bloque T10 / I3).

Verifica Release y Debug con ejecutables reales y SQLite aislado bajo 96 DPI (100%) o 120 DPI (125%):
1. Reabastecimiento (Dueño):
   - Alta de ingrediente (Limón, Peso) y compra de 1 kg por $50.00 (5000 centavos).
   - Validación en pantalla y error del servicio por entrada inválida o incompleta.
   - Verificación en SQLite: purchase_entries = 1000000 mg, 5000 centavos; ingredients.current_stock = 1000000 mg.
   - Aparición en lista "Compras recientes" con fecha, ingrediente, cantidad, costo y actor; sin costo por unidad calculado en UI.
   - Lenguaje llano (sin mg/base_unit/ids internos fuera de existencias < 1000 mg en Ingredientes).
2. Barra (Dueño):
   - Buscador por nombre ("Limón" / "Café") y estados visibles ("Suficiente", "Poco", "Agotado").
   - Merma válida (200 g) con motivo obligatorio: descuento real en SQLite, movimiento "-200 g" en lista y evento STOCK_WASTE en auditoría.
   - Merma sin motivo y merma > existencia: rechazadas sin alterar SQLite y mostrando error en español.
   - Ajuste Sumar (+500 g) y Restar (-100 g): botón de signo (sin escribir signo), descuento/suma real en SQLite y evento STOCK_ADJUSTMENT.
   - Cambio a perfil Táctil: objetivos táctiles (>= 48x48 lógicos), filas >= 40 lógicos, teclado numérico propio de T4 en Cantidad (Reabastecimiento y Barra) y Total pagado (Reabastecimiento).
3. Geometría Estándar y Táctil en 1200x720 y 960x600:
   - GetWindowRect, GetTextExtentPoint32W, LVM_GETCOLUMNWIDTH, sin WS_HSCROLL, sin solapamientos ni recortes.
4. Permisos:
   - Cajero (sin inventory.*): aviso sin permiso en Reabastecimiento y Barra, sin formularios.
   - Rol personalizado con solo inventory.view + inventory.adjust: en Barra ve existencias y registra merma/ajuste en SQLite; en Reabastecimiento no puede registrar compras ("Sin permiso para registrar compras").
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

from gui_dpi import (
    RELEASE_EXE,
    DEBUG_EXE,
    user32,
    kernel32,
    WM_CLOSE,
    WM_COMMAND,
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
    to_px,
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
    # Botón de cabecera muestra "Touch" o "Estándar"; además el botón de la subsección o Actualizar mide >= 48 en Touch
    ref_btn = next((h for (h, t) in btns if t in ("Actualizar", "Registrar compra", "Ingredientes")), None)
    if ref_btn is None:
        # En cabecera: cuando está en Estándar, el botón de perfil dice "Touch"; cuando está en Touch dice "Estándar"
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


def locate_restock_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    btn_reg = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Registrar compra"), None)
    btn_ref = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Actualizar"), None)
    anchor = btn_reg or btn_ref
    assert anchor is not None, "No se encontraron botones de Reabastecimiento"
    panel = user32.GetParent(user32.GetParent(anchor))
    lists = [h for (h, c, _t) in children if c.lower() == "syslistview32" and is_descendant(h, panel)]
    edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel)],
        key=lambda item: item[1][1]
    )
    combos = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel)],
        key=lambda item: item[1][1]
    )
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, panel) and t]
    return {
        "panel": panel,
        "btn_register": btn_reg,
        "btn_refresh": btn_ref,
        "list": lists[0] if lists else None,
        "txt_qty": edits[0][0] if len(edits) >= 1 else None,
        "txt_cost": edits[1][0] if len(edits) >= 2 else None,
        "combo_ing": combos[0][0] if len(combos) >= 1 else None,
        "combo_unit": combos[1][0] if len(combos) >= 2 else None,
        "statics": statics,
        "visible": visible,
    }


def locate_bar_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    btn_ref = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Actualizar"), None)
    assert btn_ref is not None, "No se encontró botón Actualizar en Barra"
    panel = user32.GetParent(user32.GetParent(btn_ref))
    lists = sorted(
        [(h, get_rect(h)) for (h, c, _t) in children if c.lower() == "syslistview32" and is_descendant(h, panel)],
        key=lambda item: item[1][1]
    )
    edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel)],
        key=lambda item: (item[1][0], item[1][1])
    )
    # Buscar izquierda = buscador; derecha arriba = cantidad; derecha abajo = motivo
    left_edits = sorted([it for it in edits if it[1][0] < get_rect(panel)[0] + (get_rect(panel)[2] - get_rect(panel)[0]) // 2], key=lambda it: it[1][1])
    right_edits = sorted([it for it in edits if it[1][0] >= get_rect(panel)[0] + (get_rect(panel)[2] - get_rect(panel)[0]) // 2], key=lambda it: it[1][1])
    combos = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel)],
        key=lambda item: item[1][1]
    )
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, panel)}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, panel) and t]
    return {
        "panel": panel,
        "btn_refresh": btn_ref,
        "stock_list": lists[0][0] if len(lists) >= 1 else None,
        "mov_list": lists[1][0] if len(lists) >= 2 else None,
        "txt_search": left_edits[0][0] if left_edits else None,
        "txt_qty": right_edits[0][0] if len(right_edits) >= 1 else None,
        "txt_reason": right_edits[1][0] if len(right_edits) >= 2 else None,
        "combo_ing": combos[0][0] if len(combos) >= 1 else None,
        "combo_unit": combos[1][0] if len(combos) >= 2 else None,
        "btns": btns,
        "statics": statics,
        "visible": visible,
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


def assert_no_jargon_in_panel(panel_hwnd, num_cols_list):
    texts = []
    for (h, cls, txt) in enum_children(panel_hwnd):
        if user32.IsWindowVisible(h) and txt:
            texts.append((cls, txt))
    for lst_hwnd, ncols in num_cols_list:
        if lst_hwnd and user32.IsWindowVisible(lst_hwnd):
            for row in read_list_rows(lst_hwnd, ncols):
                for cell in row:
                    texts.append(("cell", cell))
    forbidden = ["MASS", "VOLUME", "COUNT", "base_unit", "inventory.", "WASTE", "ADJUSTMENT", "PURCHASE", "SALE"]
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
    print(f"\n=== Ejecutando prueba T10 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t10_{label.lower()}_", ignore_cleanup_errors=True) as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"

        procs = []
        owner_user = "dueno_t10"
        owner_pass = "ClaveT10!2026"

        try:
            # 1. Primer inicio: alta de Dueño
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_first, _ = wait_for_window(p1.pid, "Primer Inicio")
            fc = enum_children(hwnd_first)
            fe = [h for (h, c, _t) in fc if c.lower() == "edit"]
            fb = [h for (h, c, _t) in fc if c.lower() == "button"]
            set_edit_text(fe[0], "Dueno T10")
            set_edit_text(fe[1], owner_user)
            set_edit_text(fe[2], "dueno_t10@cafeteria.local")
            set_edit_text(fe[3], owner_pass)
            set_edit_text(fe[4], owner_pass)
            click_button(fb[0])
            hwnd_main, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"))

            # Crear ingrediente Limón desde Inventario > Ingredientes
            open_inventory_sub(hwnd_main, "Ingredientes")
            ing_btns = {t: h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)}
            click_button(ing_btns["Nuevo ingrediente"])
            time.sleep(0.2)
            ing_panel = user32.GetParent(ing_btns["Nuevo ingrediente"])
            ing_edits = sorted([(h, get_rect(h)) for (h, c, _t) in enum_children(ing_panel) if user32.IsWindowVisible(h) and c.lower() == "edit"], key=lambda x: x[1][1])
            ing_combo = next(h for (h, c, _t) in enum_children(ing_panel) if user32.IsWindowVisible(h) and c.lower() == "combobox")
            set_edit_text(ing_edits[0][0], "Limón")
            select_combo_by_prefix(ing_combo, "Peso")
            set_edit_text(ing_edits[1][0], "500")
            ing_save = next(h for (h, c, t) in enum_children(ing_panel) if c.lower() == "button" and t == "Guardar")
            click_button(ing_save)
            time.sleep(0.35)

            # Sembrar Café (Agotado) y Leche (Poco) + usuarios Cajero y BarraCustom directo en SQLite cerrando primero o tras cerrar
            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p1.wait(timeout=6)

            con = sqlite3.connect(db_path)
            try:
                cur = con.cursor()
                cur.executemany(
                    "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) VALUES (?, ?, ?, ?, ?, ?)",
                    [
                        ("seed-cafe", "Café Pluma Hidalgo de Grano", "MASS", "mg", 0, 500000),
                        ("seed-leche", "Leche entera", "VOLUME", "ml", 300, 1000),
                    ]
                )
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-cajero-t10', 'cajero_t10', 'Cajero T10', 'cajero_t10@cafeteria.local', password_hash, 'role-cashier', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,)
                )
                cur.execute(
                    "INSERT INTO roles (id, name, description, is_system) VALUES ('role-bar-t10', 'Encargado Barra T10', 'Solo existencias y barra', 0)"
                )
                cur.executemany(
                    "INSERT INTO role_permissions (role_id, permission_id) VALUES ('role-bar-t10', ?)",
                    [("inventory.view",), ("inventory.adjust",)]
                )
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-bar-t10', 'barra_t10', 'Barra T10', 'barra_t10@cafeteria.local', password_hash, 'role-bar-t10', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,)
                )
                con.commit()
            finally:
                con.close()

            # 2. Sesion Dueño: Reabastecimiento
            p2, hwnd_main2 = login_window(exe_path, work_dir, env, owner_user, owner_pass)
            procs.append(p2)
            open_inventory_sub(hwnd_main2, "Reabastecimiento")
            set_profile_touch(hwnd_main2, False, reopen_sub="Reabastecimiento")
            rc = locate_restock_controls(hwnd_main2)

            # Error por campos vacíos / costo inválido
            select_combo_by_prefix(rc["combo_ing"], "Limón")
            select_combo_by_prefix(rc["combo_unit"], "kg")
            set_edit_text(rc["txt_qty"], "")
            set_edit_text(rc["txt_cost"], "50.00")
            click_button(rc["btn_register"])
            time.sleep(0.25)
            rc = locate_restock_controls(hwnd_main2)
            assert any("cantidad" in s.lower() for s in rc["statics"]), f"Debe mostrar validación de cantidad: {rc['statics']}"

            # Registrar compra válida: 1 kg de Limón por $50.00
            set_edit_text(rc["txt_qty"], "1")
            set_edit_text(rc["txt_cost"], "50.00")
            click_button(rc["btn_register"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            pur_rows = con.execute("SELECT quantity_purchased, purchase_unit, base_units_purchased, total_cost_cents FROM purchase_entries").fetchall()
            lemon_stock = con.execute("SELECT current_stock FROM ingredients WHERE name='Limón'").fetchone()[0]
            con.close()
            assert pur_rows == [(1, "kg", 1000000, 5000)], f"Compra incorrecta en SQLite: {pur_rows}"
            assert lemon_stock == 1000000, f"Existencia de Limón incorrecta tras compra: {lemon_stock}"

            rc = locate_restock_controls(hwnd_main2)
            assert get_edit_text(rc["txt_qty"]) == "" and get_edit_text(rc["txt_cost"]) == "", "El formulario debe limpiarse tras registrar compra"
            assert any("Compra registrada" in s for s in rc["statics"]), f"Falta confirmación de compra: {rc['statics']}"
            ui_purchases = read_list_rows(rc["list"], 5)
            assert len(ui_purchases) == 1, f"Se esperaba 1 compra en lista: {ui_purchases}"
            assert ui_purchases[0][1] == "Limón" and ui_purchases[0][2] == "1 kg" and ui_purchases[0][3] == "$50.00" and ui_purchases[0][4] == "Dueno T10", ui_purchases[0]

            # Segunda compra en g y tercera compra de café en kg para comprobar que cada fila conserva su unidad original (nunca convierte "1 kg" en "1000 g")
            select_combo_by_prefix(rc["combo_ing"], "Limón")
            select_combo_by_prefix(rc["combo_unit"], "g")
            set_edit_text(rc["txt_qty"], "500")
            set_edit_text(rc["txt_cost"], "25.00")
            click_button(rc["btn_register"])
            time.sleep(0.3)
            rc = locate_restock_controls(hwnd_main2)
            select_combo_by_prefix(rc["combo_ing"], "Café Pluma Hidalgo de Grano")
            select_combo_by_prefix(rc["combo_unit"], "kg")
            set_edit_text(rc["txt_qty"], "1")
            set_edit_text(rc["txt_cost"], "120.00")
            click_button(rc["btn_register"])
            time.sleep(0.3)

            rc = locate_restock_controls(hwnd_main2)
            ui_purchases = read_list_rows(rc["list"], 5)
            assert len(ui_purchases) == 3, f"Se esperaban 3 compras en lista: {ui_purchases}"
            pur_units = {(r[1], r[2], r[3]) for r in ui_purchases}
            assert ("Limón", "1 kg", "$50.00") in pur_units, f"Compra '1 kg' alterada: {pur_units}"
            assert ("Limón", "500 g", "$25.00") in pur_units, f"Compra '500 g' alterada: {pur_units}"
            assert ("Café Pluma Hidalgo de Grano", "1 kg", "$120.00") in pur_units, f"Compra café '1 kg' alterada: {pur_units}"
            assert not any("$/" in s or "unitario" in s.lower() for s in rc["statics"]), "No se debe calcular ni mostrar costo unitario"
            assert_no_jargon_in_panel(rc["panel"], [(rc["list"], 5)])

            # Restaurar stock esperado de Limón para las aserciones de Barra (1000000 mg) ajustando BD no hace falta: ahora tiene 1.5 kg y Café 1 kg -> suficiente/agotado según umbral
            # Geometría Estándar Reabastecimiento
            assert_panel_geometry(
                hwnd_main2, rc["panel"],
                [(rc["list"], ["Fecha", "Ingrediente", "Cantidad", "Costo", "Registró"])],
                [("btn_register", rc["btn_register"]), ("btn_refresh", rc["btn_refresh"])],
                False, 1200, 720,
            )
            assert_panel_geometry(
                hwnd_main2, rc["panel"],
                [(rc["list"], ["Fecha", "Ingrediente", "Cantidad", "Costo", "Registró"])],
                [("btn_register", rc["btn_register"]), ("btn_refresh", rc["btn_refresh"])],
                False, 960, 600,
            )

            # 3. Sesion Dueño: Barra
            open_inventory_sub(hwnd_main2, "Barra")
            bc = locate_bar_controls(hwnd_main2)
            stock_rows = {r[0]: r[1:] for r in read_list_rows(bc["stock_list"], 3)}
            assert stock_rows["Limón"][1] == "Suficiente", stock_rows["Limón"]
            assert stock_rows["Leche entera"][1] == "Poco", stock_rows["Leche entera"]
            assert stock_rows["Café Pluma Hidalgo de Grano"][1] == "Suficiente", stock_rows["Café Pluma Hidalgo de Grano"]

            # Buscador
            set_edit_text(bc["txt_search"], "Café")
            time.sleep(0.2)
            filtered = read_list_rows(bc["stock_list"], 3)
            assert len(filtered) == 1 and filtered[0][0] == "Café Pluma Hidalgo de Grano", f"Filtro de búsqueda falló: {filtered}"
            set_edit_text(bc["txt_search"], "")
            time.sleep(0.2)

            # Merma sin motivo -> rechazo
            select_combo_by_prefix(bc["combo_ing"], "Limón")
            select_combo_by_prefix(bc["combo_unit"], "g")
            set_edit_text(bc["txt_qty"], "200")
            set_edit_text(bc["txt_reason"], "")
            click_button(bc["btns"]["Registrar merma"])
            time.sleep(0.25)
            bc = locate_bar_controls(hwnd_main2)
            assert any("motivo" in s.lower() for s in bc["statics"]), f"Debe exigir motivo: {bc['statics']}"

            # Merma > existencia -> error del servicio, SQLite intacto
            set_edit_text(bc["txt_qty"], "2000")
            set_edit_text(bc["txt_reason"], "Se cayó caja")
            click_button(bc["btns"]["Registrar merma"])
            time.sleep(0.25)
            bc = locate_bar_controls(hwnd_main2)
            assert any("insuficiente" in s.lower() for s in bc["statics"]), f"Debe mostrar error por existencia insuficiente: {bc['statics']}"
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT current_stock FROM ingredients WHERE name='Limón'").fetchone()[0] == 1500000
            con.close()

            # Merma válida: 200 g de Café Pluma Hidalgo de Grano con motivo completo (nombre largo + motivo largo)
            select_combo_by_prefix(bc["combo_ing"], "Café Pluma Hidalgo de Grano")
            select_combo_by_prefix(bc["combo_unit"], "g")
            set_edit_text(bc["txt_qty"], "200")
            waste_reason_long = "Fruta machucada en traslado"
            set_edit_text(bc["txt_reason"], waste_reason_long)
            click_button(bc["btns"]["Registrar merma"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                cafe_after_waste = con.execute("SELECT current_stock FROM ingredients WHERE name='Café Pluma Hidalgo de Grano'").fetchone()[0]
                waste_mov = con.execute("SELECT movement_type, quantity_base_units, reason FROM stock_movements WHERE movement_type='WASTE'").fetchall()
                waste_audit = con.execute("SELECT event_type, details FROM audit_events WHERE event_type='STOCK_WASTE'").fetchall()
            finally:
                con.close()
            assert cafe_after_waste == 800000, f"Café tras merma 200 g debe ser 800000 mg, obtuvo {cafe_after_waste}"
            assert len(waste_mov) == 1 and waste_mov[0][1] == -200000 and waste_mov[0][2] == waste_reason_long, waste_mov
            assert len(waste_audit) == 1, f"Falta evento STOCK_WASTE en audit_events: {waste_audit}"

            # Modo Ajuste: Sumar 500 g y luego Restar 100 g sobre Limón
            click_button(bc["btns"]["Ajuste"])
            time.sleep(0.2)
            bc = locate_bar_controls(hwnd_main2)
            assert "Registrar ajuste" in bc["btns"], "Botón debe cambiar a 'Registrar ajuste'"
            assert "Sumar" in bc["btns"] and "Restar" in bc["btns"], "Modo Ajuste debe mostrar botones Sumar y Restar"
            select_combo_by_prefix(bc["combo_ing"], "Limón")
            select_combo_by_prefix(bc["combo_unit"], "g")
            click_button(bc["btns"]["Sumar"])
            set_edit_text(bc["txt_qty"], "500")
            set_edit_text(bc["txt_reason"], "Conteo físico positivo")
            click_button(bc["btns"]["Registrar ajuste"])
            time.sleep(0.3)

            bc = locate_bar_controls(hwnd_main2)
            click_button(bc["btns"]["Restar"])
            select_combo_by_prefix(bc["combo_ing"], "Limón")
            select_combo_by_prefix(bc["combo_unit"], "g")
            set_edit_text(bc["txt_qty"], "100")
            set_edit_text(bc["txt_reason"], "Conteo físico negativo")
            click_button(bc["btns"]["Registrar ajuste"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                lemon_after_adj = con.execute("SELECT current_stock FROM ingredients WHERE name='Limón'").fetchone()[0]
                adj_movs = con.execute("SELECT quantity_base_units, reason FROM stock_movements WHERE movement_type='ADJUSTMENT' ORDER BY created_at ASC").fetchall()
                adj_audits = con.execute("SELECT event_type FROM audit_events WHERE event_type='STOCK_ADJUSTMENT'").fetchall()
            finally:
                con.close()
            assert lemon_after_adj == 1900000, f"Limón tras 1.5kg +500g -100g debe ser 1900000 mg, obtuvo {lemon_after_adj}"
            assert set(adj_movs) == {(500000, "Conteo físico positivo"), (-100000, "Conteo físico negativo")}, adj_movs
            assert len(adj_audits) == 2, f"Faltan eventos STOCK_ADJUSTMENT: {adj_audits}"

            bc = locate_bar_controls(hwnd_main2)
            mov_rows = read_list_rows(bc["mov_list"], 5)
            assert len(mov_rows) == 3, f"Se esperaban 3 movimientos (1 merma, 2 ajustes): {mov_rows}"
            qty_cells = {r[2] for r in mov_rows}
            assert "-200 g" in qty_cells and "+500 g" in qty_cells and "-100 g" in qty_cells, f"Cantidades con signo inesperadas: {qty_cells}"
            assert_no_jargon_in_panel(bc["panel"], [(bc["stock_list"], 3), (bc["mov_list"], 5)])

            # Geometría Estándar Barra + ancho real de Motivo en 1200x720 + lectura completa en 960x600
            assert_panel_geometry(
                hwnd_main2, bc["panel"],
                [
                    (bc["stock_list"], ["Ingrediente", "Existencia", "Estado"]),
                    (bc["mov_list"], ["Fecha", "Ingrediente", "Cantidad", "Motivo", "Quién"]),
                ],
                [("btn_refresh", bc["btn_refresh"]), ("btn_submit", bc["btns"]["Registrar ajuste"])],
                False, 1200, 720,
            )
            reason_tw_std, _ = measure_text_extent(bc["mov_list"], waste_reason_long)
            reason_col_std = send_msg(bc["mov_list"], LVM_GETCOLUMNWIDTH, 3, 0)
            ing_col_std = send_msg(bc["mov_list"], LVM_GETCOLUMNWIDTH, 1, 0)
            print(f"[METRICA][Estándar 1200x720] Motivo col={reason_col_std}px, text_w={reason_tw_std}px, Ingrediente col={ing_col_std}px", flush=True)
            assert reason_col_std >= reason_tw_std, f"Motivo recortado en Estándar 1200x720: col={reason_col_std} < text_w={reason_tw_std}"

            assert_panel_geometry(
                hwnd_main2, bc["panel"],
                [
                    (bc["stock_list"], ["Ingrediente", "Existencia", "Estado"]),
                    (bc["mov_list"], ["Fecha", "Ingrediente", "Cantidad", "Motivo", "Quién"]),
                ],
                [("btn_refresh", bc["btn_refresh"]), ("btn_submit", bc["btns"]["Registrar ajuste"])],
                False, 960, 600,
            )
            waste_row_idx = next(idx for idx, r in enumerate(read_list_rows(bc["mov_list"], 5)) if waste_reason_long in r[3])
            select_list_row(bc["mov_list"], waste_row_idx)
            time.sleep(0.25)
            bc_sel = locate_bar_controls(hwnd_main2)
            assert any(waste_reason_long in " ".join(s.split()) for s in bc_sel["statics"]), (
                f"Al seleccionar fila en 960x600 debe leerse completo '{waste_reason_long}': {bc_sel['statics']}"
            )

            # 4. Cambiar a perfil Táctil y validar teclado numérico T4 + geometría 1200x720 y 960x600
            set_profile_touch(hwnd_main2, True, reopen_sub="Reabastecimiento")
            rc_t = locate_restock_controls(hwnd_main2)
            enter_keypad_digits(hwnd_main2, rc_t["txt_qty"], "2")
            assert get_edit_text(rc_t["txt_qty"]) == "2", f"Keypad no actualizó Cantidad en Reabastecimiento: '{get_edit_text(rc_t['txt_qty'])}'"
            enter_keypad_digits(hwnd_main2, rc_t["txt_cost"], "35.50")
            assert get_edit_text(rc_t["txt_cost"]) == "35.50", f"Keypad no actualizó Costo en Reabastecimiento: '{get_edit_text(rc_t['txt_cost'])}'"

            rc_t = locate_restock_controls(hwnd_main2)
            assert_panel_geometry(
                hwnd_main2, rc_t["panel"],
                [(rc_t["list"], ["Fecha", "Ingrediente", "Cantidad", "Costo", "Registró"])],
                [
                    ("btn_register", rc_t["btn_register"]),
                    ("btn_refresh", rc_t["btn_refresh"]),
                    ("combo_ing", rc_t["combo_ing"]),
                    ("combo_unit", rc_t["combo_unit"]),
                    ("txt_qty_wrap", user32.GetParent(rc_t["txt_qty"])),
                    ("txt_cost_wrap", user32.GetParent(rc_t["txt_cost"])),
                ],
                True, 1200, 720,
            )
            assert_panel_geometry(
                hwnd_main2, rc_t["panel"],
                [(rc_t["list"], ["Fecha", "Ingrediente", "Cantidad", "Costo", "Registró"])],
                [
                    ("btn_register", rc_t["btn_register"]),
                    ("btn_refresh", rc_t["btn_refresh"]),
                    ("combo_ing", rc_t["combo_ing"]),
                    ("combo_unit", rc_t["combo_unit"]),
                    ("txt_qty_wrap", user32.GetParent(rc_t["txt_qty"])),
                    ("txt_cost_wrap", user32.GetParent(rc_t["txt_cost"])),
                ],
                True, 960, 600,
            )

            open_inventory_sub(hwnd_main2, "Barra")
            bc_t = locate_bar_controls(hwnd_main2)
            enter_keypad_digits(hwnd_main2, bc_t["txt_qty"], "50")
            assert get_edit_text(bc_t["txt_qty"]) == "50", f"Keypad no actualizó Cantidad en Barra: '{get_edit_text(bc_t['txt_qty'])}'"
            bc_t = locate_bar_controls(hwnd_main2)
            assert_panel_geometry(
                hwnd_main2, bc_t["panel"],
                [
                    (bc_t["stock_list"], ["Ingrediente", "Existencia", "Estado"]),
                    (bc_t["mov_list"], ["Fecha", "Ingrediente", "Cantidad", "Motivo", "Quién"]),
                ],
                [
                    ("btn_refresh", bc_t["btn_refresh"]),
                    ("btn_merma", bc_t["btns"]["Merma"]),
                    ("btn_ajuste", bc_t["btns"]["Ajuste"]),
                    ("btn_submit", bc_t["btns"]["Registrar merma"]),
                    ("combo_ing", bc_t["combo_ing"]),
                    ("combo_unit", bc_t["combo_unit"]),
                    ("txt_qty_wrap", user32.GetParent(bc_t["txt_qty"])),
                    ("txt_reason_wrap", user32.GetParent(bc_t["txt_reason"])),
                ],
                True, 1200, 720,
            )
            reason_tw_tch, _ = measure_text_extent(bc_t["mov_list"], waste_reason_long)
            reason_col_tch = send_msg(bc_t["mov_list"], LVM_GETCOLUMNWIDTH, 3, 0)
            ing_col_tch = send_msg(bc_t["mov_list"], LVM_GETCOLUMNWIDTH, 1, 0)
            print(f"[METRICA][Táctil 1200x720] Motivo col={reason_col_tch}px, text_w={reason_tw_tch}px, Ingrediente col={ing_col_tch}px", flush=True)
            assert reason_col_tch >= reason_tw_tch, f"Motivo recortado en Táctil 1200x720: col={reason_col_tch} < text_w={reason_tw_tch}"

            assert_panel_geometry(
                hwnd_main2, bc_t["panel"],
                [
                    (bc_t["stock_list"], ["Ingrediente", "Existencia", "Estado"]),
                    (bc_t["mov_list"], ["Fecha", "Ingrediente", "Cantidad", "Motivo", "Quién"]),
                ],
                [
                    ("btn_refresh", bc_t["btn_refresh"]),
                    ("btn_merma", bc_t["btns"]["Merma"]),
                    ("btn_ajuste", bc_t["btns"]["Ajuste"]),
                    ("btn_submit", bc_t["btns"]["Registrar merma"]),
                    ("combo_ing", bc_t["combo_ing"]),
                    ("combo_unit", bc_t["combo_unit"]),
                    ("txt_qty_wrap", user32.GetParent(bc_t["txt_qty"])),
                    ("txt_reason_wrap", user32.GetParent(bc_t["txt_reason"])),
                ],
                True, 960, 600,
            )
            waste_row_idx_t = next(idx for idx, r in enumerate(read_list_rows(bc_t["mov_list"], 5)) if waste_reason_long in r[3])
            select_list_row(bc_t["mov_list"], waste_row_idx_t)
            time.sleep(0.25)
            bc_t_sel = locate_bar_controls(hwnd_main2)
            assert any(waste_reason_long in " ".join(s.split()) for s in bc_t_sel["statics"]), (
                f"Al seleccionar fila en Táctil 960x600 debe leerse completo '{waste_reason_long}': {bc_t_sel['statics']}"
            )

            user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
            p2.wait(timeout=6)

            # 5. Sesion Cajero: sin permisos de inventario
            p3, hwnd_main3 = login_window(exe_path, work_dir, env, "cajero_t10", owner_pass)
            procs.append(p3)
            open_inventory_sub(hwnd_main3, "Reabastecimiento")
            vis_r = [(c.lower(), t) for (h, c, t) in enum_children(hwnd_main3) if user32.IsWindowVisible(h)]
            assert not any(c == "button" and t == "Registrar compra" for (c, t) in vis_r), "Cajero no debe ver formulario de compra"
            assert not any(c == "edit" for (c, _t) in vis_r), "Cajero no debe ver campos de entrada en Reabastecimiento"
            assert any(c == "static" and "Sin permiso para registrar compras" in t for (c, t) in vis_r), f"Falta aviso de permiso en Reabastecimiento: {vis_r}"

            open_inventory_sub(hwnd_main3, "Barra")
            vis_b = [(c.lower(), t) for (h, c, t) in enum_children(hwnd_main3) if user32.IsWindowVisible(h)]
            assert not any(c == "button" and t in ("Registrar merma", "Registrar ajuste") for (c, t) in vis_b), "Cajero no debe ver formulario de merma/ajuste"
            assert not any(c == "syslistview32" for (c, _t) in vis_b), "Cajero sin inventory.view no debe ver listas en Barra"
            assert any(c == "static" and "Sin permiso para ver existencias" in t for (c, t) in vis_b), f"Falta aviso de permiso en Barra: {vis_b}"
            user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
            p3.wait(timeout=6)

            # 6. Sesion Rol personalizado (inventory.view + inventory.adjust)
            p4, hwnd_main4 = login_window(exe_path, work_dir, env, "barra_t10", owner_pass)
            procs.append(p4)
            open_inventory_sub(hwnd_main4, "Reabastecimiento")
            vis_rb = [(c.lower(), t) for (h, c, t) in enum_children(hwnd_main4) if user32.IsWindowVisible(h)]
            assert not any(c == "button" and t == "Registrar compra" for (c, t) in vis_rb), "Rol Barra no debe ver formulario de compra"
            assert any(c == "static" and "Sin permiso para registrar compras" in t for (c, t) in vis_rb), vis_rb

            open_inventory_sub(hwnd_main4, "Barra")
            set_profile_touch(hwnd_main4, False, reopen_sub="Barra")
            bc_b = locate_bar_controls(hwnd_main4)
            assert "Registrar merma" in bc_b["btns"], "Rol Barra con inventory.adjust debe ver formulario de merma"
            assert send_msg(bc_b["stock_list"], LVM_GETITEMCOUNT, 0, 0) == 3, "Rol Barra con inventory.view debe ver existencias"
            select_combo_by_prefix(bc_b["combo_ing"], "Limón")
            select_combo_by_prefix(bc_b["combo_unit"], "g")
            set_edit_text(bc_b["txt_qty"], "100")
            set_edit_text(bc_b["txt_reason"], "Merma registrada por barra")
            click_button(bc_b["btns"]["Registrar merma"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            lemon_final = con.execute("SELECT current_stock FROM ingredients WHERE name='Limón'").fetchone()[0]
            con.close()
            assert lemon_final == 1800000, f"Rol Barra debió registrar merma 100 g: esperado 1800000, obtuvo {lemon_final}"

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
    print("=== PRUEBA T10 COMPLETA EXITOSA (Release y Debug) ===", flush=True)


if __name__ == "__main__":
    main()
