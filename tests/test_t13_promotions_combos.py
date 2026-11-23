#!/usr/bin/env python3
"""
test_t13_promotions_combos.py - Prueba GUI de integración del Bloque T13 (I3).

Verifica en Release y Debug (SQLite aislado, CAFETERIA_UPDATE_CHECK=0):
1. Promociones (listado, creación, edición, activación/desactivación):
   - Porcentaje 12.5 % -> percent_bp == 1250 en SQLite (enteros sin double).
   - Monto menos por pieza $5.00 -> fixed_off_cents == 500 en SQLite.
   - Lleva 2, paga 1 con días (Lun, Mar) y horario -> buy_qty=2, pay_qty=1, weekdays_mask, time_from_min/time_to_min.
   - Textos de la lista coinciden con summary_text y validity_text; estados en lenguaje llano.
   - Edición de prioridad y desactivación con confirmación; aplicación real de 2x1 en POS Venta.
2. Combos (listado, creación, edición, costo/margen, baja/reactivación y venta):
   - Creación con 2 partes, edición de precio y partes.
   - Costo y margen con componente sin costo conocido ("Falta conocer el costo de: ...", sin margen)
     y con costos conocidos ("Costo estimado:", "Margen:").
   - Baja lógica y reactivación con confirmación y filtro "Ver dados de baja".
   - Venta real del combo en POS descuenta ingredientes en SQLite.
3. Permisos:
   - Rol sin inventory.manage_products: oculta costo y margen del combo (no los muestra en cero).
   - Cajero sin inventory.promotions: "Sin permiso para administrar promociones".
4. Geometría Estándar y Táctil en 1200x720 y 960x600, columnas medidas y teclado numérico T4.
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
    REPO_ROOT,
    RELEASE_EXE,
    DEBUG_EXE,
    user32,
    kernel32,
    WM_CLOSE,
    WM_KEYDOWN,
    WM_KEYUP,
    WM_LBUTTONDOWN,
    WM_LBUTTONUP,
    VK_RETURN,
    WS_HSCROLL,
    GWL_STYLE,
    LVM_GETITEMCOUNT,
    HDM_GETITEMCOUNT,
    PROCESS_VM_OPERATION,
    PROCESS_VM_READ,
    PROCESS_VM_WRITE,
    MEM_COMMIT,
    MEM_RESERVE,
    MEM_RELEASE,
    PAGE_READWRITE,
    ensure_supported_system_dpi,
    ensure_no_app_running,
    format_dpi_info,
    send_msg,
    get_rect,
    get_client_size,
    set_client_logical_size,
    find_windows_by_pid,
    wait_for_window,
    enum_children,
    set_edit_text,
    click_button,
    post_click_button,
    click_control_at,
    select_list_row,
    measure_text_extent,
    min_px,
    max_px,
    is_descendant,
    click_sidebar_section,
    click_subnav,
)

WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
LVM_GETCOLUMNWIDTH = 0x101D
LVM_GETITEMTEXTW = 0x1073
LVM_GETHEADER = 0x101F
HDM_GETITEMW = 0x120B
HDI_TEXT = 0x0002
CB_GETCOUNT = 0x0146
CB_GETLBTEXT = 0x0148
CB_GETLBTEXTLEN = 0x0149
CB_SETCURSEL = 0x014E
BM_GETCHECK = 0x00F0
BST_CHECKED = 1


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
        ("pszText", wintypes.LPWSTR),
        ("hbm", wintypes.HANDLE),
        ("cchTextMax", ctypes.c_int),
        ("fmt", ctypes.c_int),
        ("lParam", wintypes.LPARAM),
        ("iImage", ctypes.c_int),
        ("iOrder", ctypes.c_int),
        ("type", wintypes.UINT),
        ("pvFilter", ctypes.c_void_p),
        ("state", wintypes.UINT),
    ]


def get_edit_text(hwnd):
    ln = send_msg(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    send_msg(hwnd, WM_GETTEXT, ln + 2, ctypes.addressof(buf))
    return buf.value


def select_combo_by_prefix(hwnd_combo, prefix):
    count = send_msg(hwnd_combo, CB_GETCOUNT, 0, 0)
    for idx in range(count):
        ln = send_msg(hwnd_combo, CB_GETLBTEXTLEN, idx, 0)
        buf = ctypes.create_unicode_buffer(max(1, ln + 2))
        send_msg(hwnd_combo, CB_GETLBTEXT, idx, ctypes.addressof(buf))
        if buf.value == prefix or buf.value.startswith(prefix):
            send_msg(hwnd_combo, CB_SETCURSEL, idx, 0)
            ctrl_id = user32.GetDlgCtrlID(hwnd_combo)
            parent = user32.GetParent(hwnd_combo)
            send_msg(parent, 0x0111, (1 << 16) | (ctrl_id & 0xFFFF), hwnd_combo)
            send_msg(parent, 0x0111, (9 << 16) | (ctrl_id & 0xFFFF), hwnd_combo)
            time.sleep(0.18)
            return buf.value
    raise AssertionError(f"Opción '{prefix}' no encontrada en combo (count={count})")


def get_list_column_width(hwnd_list, col_idx):
    return send_msg(hwnd_list, LVM_GETCOLUMNWIDTH, col_idx, 0)


def get_list_headers(hwnd_list):
    hwnd_hdr = send_msg(hwnd_list, LVM_GETHEADER, 0, 0)
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
    if not hwnd_list:
        return []
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


def set_profile_touch(hwnd_main, want_touch):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    is_touch = any(t == "Estándar" for (_h, t) in btns)
    if is_touch == want_touch:
        return
    toggle = next((h for (h, t) in btns if "Touch" in t or "Estándar" in t), None)
    assert toggle is not None, "Botón toggle Touch/Estándar no encontrado"
    click_button(toggle)
    time.sleep(0.55)


def confirm_message_dialog(pid, title_contains="Confirmar"):
    hwnd_dlg, _ = wait_for_window(pid, title_contains, timeout=6.0)
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_dlg) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_yes = next((h for (h, t) in btns if "Sí" in t or "Yes" in t), btns[0][0])
    post_click_button(btn_yes)
    time.sleep(0.4)


def check_no_jargon(texts, ctx):
    forbidden = ["PERCENT", "FIXED", "NXM", "PRODUCT", "CATEGORY", "basis_points", "promo-", "prod-"]
    for t in texts:
        for bad in forbidden:
            assert bad not in t, f"Jerga prohibida '{bad}' en {ctx}: '{t}'"


WS_VSCROLL = 0x00200000


def find_vscroll_ancestor(hwnd, root_hwnd):
    cur = user32.GetParent(hwnd)
    while cur and cur != root_hwnd:
        st = user32.GetWindowLongW(cur, GWL_STYLE)
        if (st & WS_VSCROLL) != 0:
            return cur
        cur = user32.GetParent(cur)
    return None


def assert_geometry(hwnd_main, panel_hwnd, touch_mode, width, height, check_list=None):
    scale = set_client_logical_size(hwnd_main, width, height, x_log=16, y_log=16)
    time.sleep(0.5)
    main_rc = get_rect(hwnd_main)
    edge_tol = max_px(4, scale)
    min_touch_px = min_px(48, scale)

    children = enum_children(hwnd_main)
    btns = [
        (h, t, get_rect(h)) for (h, c, t) in children
        if c.lower() == "button" and user32.IsWindowVisible(h) and is_descendant(h, panel_hwnd)
    ]
    for h, t, r in btns:
        vscroll_anc = find_vscroll_ancestor(h, panel_hwnd)
        if vscroll_anc:
            vrc = get_rect(vscroll_anc)
            vst = user32.GetWindowLongW(vscroll_anc, GWL_STYLE)
            assert (vst & WS_HSCROLL) == 0, f"Panel con scroll muestra barra horizontal WS_HSCROLL en {width}x{height}"
            assert vrc[2] <= main_rc[2] + edge_tol and vrc[3] <= main_rc[3] + edge_tol, (
                f"Contenedor desplazable desborda en {width}x{height}: rect={vrc}, win={main_rc}"
            )
            assert r[0] >= vrc[0] - edge_tol and r[2] <= vrc[2] + edge_tol, (
                f"Control '{t}' desborda horizontalmente del panel en {width}x{height}: rect={r}, scroll={vrc}"
            )
        else:
            assert r[2] <= main_rc[2] + edge_tol and r[3] <= main_rc[3] + edge_tol, (
                f"Control '{t}' desborda en {width}x{height}: rect={r}, win={main_rc}"
            )
        if t and user32.IsWindowEnabled(h):
            tw, _ = measure_text_extent(h, t)
            cw, _ = get_client_size(h)
            if tw > cw + edge_tol:
                raise AssertionError(f"Texto recortado en '{t}' ({width}x{height}): tw={tw} > cw={cw}")
        if touch_mode and user32.IsWindowEnabled(h):
            bw = r[2] - r[0]
            bh = r[3] - r[1]
            assert bw >= min_touch_px and bh >= min_touch_px, (
                f"Control táctil '{t}' < 48x48 log en {width}x{height}: {bw}x{bh} (< {min_touch_px})"
            )

    if check_list and user32.IsWindowVisible(check_list):
        style = user32.GetWindowLongW(check_list, GWL_STYLE)
        assert (style & WS_HSCROLL) == 0, f"Lista muestra barra horizontal WS_HSCROLL en {width}x{height}"
        headers, hwnd_hdr = get_list_headers(check_list)
        for idx, htxt in enumerate(headers):
            cw = get_list_column_width(check_list, idx)
            tw, _ = measure_text_extent(hwnd_hdr, htxt)
            assert cw >= tw + min_px(6, scale), (
                f"Encabezado '{htxt}' (col {idx}) recortado en {width}x{height}: col_w={cw} < tw={tw}"
            )


def locate_promo_tab_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    btn_tab_p = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Promociones"), None)
    assert btn_tab_p is not None, "Pestaña 'Promociones' no encontrada"
    pnl_main = user32.GetParent(btn_tab_p)
    panel = user32.GetParent(pnl_main)

    btn_new = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Nueva promoción"), None)
    assert btn_new is not None, "Botón 'Nueva promoción' no encontrado"
    list_card = user32.GetParent(btn_new)

    left_lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, list_card)]
    right_lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, panel) and not is_descendant(h, list_card)]
    promo_list = left_lists[0] if left_lists else None
    scope_list = right_lists[0] if right_lists else None

    left_edits = [h for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, list_card)]
    right_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel) and not is_descendant(h, list_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    right_combos = [h for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel) and not is_descendant(h, list_card)]
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, panel) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, panel) and t]

    return {
        "panel": panel,
        "list": promo_list,
        "scope_list": scope_list,
        "txt_search": left_edits[0],
        "right_edits": [h for h, _r in right_edits],
        "combo_cat": right_combos[0] if right_combos else None,
        "btns": btns,
        "statics": statics,
    }


def locate_combo_tab_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    btn_tab_c = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Combos"), None)
    assert btn_tab_c is not None, "Pestaña 'Combos' no encontrada"
    panel = user32.GetParent(user32.GetParent(btn_tab_c))

    btn_new = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Nuevo combo"), None)
    assert btn_new is not None, "Botón 'Nuevo combo' no encontrado"
    list_card = user32.GetParent(btn_new)

    left_lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, list_card)]
    right_lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, panel) and not is_descendant(h, list_card)]
    left_edits = [h for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, list_card)]
    right_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel) and not is_descendant(h, list_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    right_combos = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel) and not is_descendant(h, list_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, panel) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, panel) and t]

    parts_list = right_lists[0] if right_lists else None
    parts_y_bottom = get_rect(parts_list)[3] if parts_list else None
    txt_category = None
    txt_part_qty = None
    for h_ed, rc_ed in right_edits[3:]:
        if parts_y_bottom is not None and rc_ed[1] >= parts_y_bottom - 4:
            txt_part_qty = h_ed
        elif txt_category is None:
            txt_category = h_ed
    if txt_part_qty is None and len(right_edits) >= 4:
        txt_part_qty = right_edits[-1][0]

    return {
        "panel": panel,
        "list": left_lists[0] if left_lists else None,
        "parts_list": parts_list,
        "txt_search": left_edits[0],
        "txt_name": right_edits[0][0],
        "txt_price": right_edits[1][0],
        "txt_sku": right_edits[2][0],
        "txt_category": txt_category,
        "txt_part_qty": txt_part_qty,
        "combo_cat": right_combos[0][0] if len(right_combos) >= 1 else None,
        "combo_part_prod": right_combos[1][0] if len(right_combos) >= 2 else None,
        "btns": btns,
        "statics": statics,
    }


def set_combo_category(hwnd_main, cc, category_name):
    combo = cc["combo_cat"]
    assert combo is not None, "Desplegable de categoría de combo no encontrado"
    count = send_msg(combo, CB_GETCOUNT, 0, 0)
    items = []
    for idx in range(count):
        ln = send_msg(combo, CB_GETLBTEXTLEN, idx, 0)
        buf = ctypes.create_unicode_buffer(max(1, ln + 2))
        send_msg(combo, CB_GETLBTEXT, idx, ctypes.addressof(buf))
        items.append(buf.value)
    if category_name in items:
        select_combo_by_prefix(combo, category_name)
    else:
        select_combo_by_prefix(combo, "Nueva categoría...")
        time.sleep(0.15)
        cc_now = locate_combo_tab_controls(hwnd_main)
        assert cc_now["txt_category"] is not None, "Campo de texto de nueva categoría de combo no visible"
        set_edit_text(cc_now["txt_category"], category_name)


def click_keypad_seq(hwnd_main, edit_hwnd, seq):
    click_control_at(edit_hwnd)
    time.sleep(0.25)
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    assert any(t == "Listo" for (_h, t) in btns), "Teclado numérico no apareció"
    btn_clr = next(h for (h, t) in btns if t == "Limpiar")
    l, t, r, b = get_rect(btn_clr)
    lp = ((max(1, (b - t) // 2)) << 16) | (max(1, (r - l) // 2) & 0xFFFF)
    user32.PostMessageW(btn_clr, WM_LBUTTONDOWN, 1, lp)
    time.sleep(0.03)
    post_click_button(btn_clr)
    time.sleep(0.1)
    for ch in seq:
        bnow = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
        target = next(h for (h, t) in bnow if t == ch)
        l, t, r, b = get_rect(target)
        lp = ((max(1, (b - t) // 2)) << 16) | (max(1, (r - l) // 2) & 0xFFFF)
        user32.PostMessageW(target, WM_LBUTTONDOWN, 1, lp)
        time.sleep(0.03)
        post_click_button(target)
        time.sleep(0.08)
    bend = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_ok = next(h for (h, t) in bend if t == "Listo")
    click_button(btn_ok)
    time.sleep(0.2)


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba T13 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t13_{label.lower()}_", ignore_cleanup_errors=True) as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"

        procs = []
        owner_user = "dueno_t13"
        owner_pass = "ClaveT13!2026"

        try:
            # 1. Alta inicial Dueño
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_first, _ = wait_for_window(p1.pid, "Primer Inicio")
            fc = enum_children(hwnd_first)
            fe = [h for (h, c, _t) in fc if c.lower() == "edit"]
            fb = [h for (h, c, _t) in fc if c.lower() == "button"]
            set_edit_text(fe[0], "Dueno T13")
            set_edit_text(fe[1], owner_user)
            set_edit_text(fe[2], "dueno_t13@cafeteria.local")
            set_edit_text(fe[3], owner_pass)
            set_edit_text(fe[4], owner_pass)
            click_button(fb[0])
            hwnd_m1, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"))
            user32.PostMessageW(hwnd_m1, WM_CLOSE, 0, 0)
            p1.wait(timeout=6)

            # 2. Sembrar ingredientes, productos y usuarios en SQLite
            con = sqlite3.connect(db_path)
            now_str = time.strftime("%Y-%m-%d %H:%M:%S")
            try:
                cur = con.cursor()
                cur.executemany(
                    "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold, stock_value_micro, last_avg_value_micro, last_avg_units) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    [
                        ("ing-cafe-t13", "Café grano", "MASS", "mg", 1000000, 100000, 20000000000, 20000000000, 1000000),  # 1 kg = $200 -> 20 g = $4.00
                        ("ing-gal-t13", "Galleta Avena Pz", "COUNT", "pz", 20, 2, 0, 0, 0),       # Sin compra aún (costo desconocido)
                    ],
                )
                # Compra real de Café grano: 1 kg por $200.00 ($0.20/g -> 20 g = $4.00/porción)
                cur.execute(
                    "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) "
                    "VALUES ('pur-cafe-t13', 'ing-cafe-t13', 1, 'kg', 20000, 1000000, ?)",
                    (now_str,),
                )
                cur.execute(
                    "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) "
                    "VALUES ('mov-cafe-t13', 'ing-cafe-t13', 'PURCHASE', 1000000, 'Compra Café grano', ?)",
                    (now_str,),
                )
                cur.executemany(
                    "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, is_combo, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    [
                        ("prod-cafe-t13", "CAF-13", None, "Café Americano", "Bebidas", 4000, 1, 1, 0, now_str),
                        ("prod-gal-t13", "GAL-13", None, "Galleta Avena", "Panadería", 2000, 0, 1, 0, now_str),
                    ],
                )
                cur.executemany(
                    "INSERT INTO recipes (id, product_id, version, created_at) VALUES (?, ?, 1, ?)",
                    [
                        ("rcp-cafe-t13", "prod-cafe-t13", now_str),
                        ("rcp-gal-t13", "prod-gal-t13", now_str),
                    ],
                )
                cur.executemany(
                    "INSERT INTO recipe_items (recipe_id, ingredient_id, quantity_base_units) VALUES (?, ?, ?)",
                    [
                        ("rcp-cafe-t13", "ing-cafe-t13", 20000),  # 20 g -> $4.00
                        ("rcp-gal-t13", "ing-gal-t13", 1),
                    ],
                )
                # Usuario Cajero (sin inventory.promotions)
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-caj-t13', 'cajero_t13', 'Cajero T13', 'caj@t13.local', password_hash, 'role-cashier', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,),
                )
                # Rol con inventory.promotions + inventory.view pero SIN inventory.manage_products
                cur.execute(
                    "INSERT INTO roles (id, name, description, is_system) VALUES ('role-promo-noprod', 'Solo Promociones', 'Sin costos', 0)"
                )
                cur.executemany(
                    "INSERT INTO role_permissions (role_id, permission_id) VALUES ('role-promo-noprod', ?)",
                    [("inventory.promotions",), ("inventory.view",)],
                )
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-pnoprod-t13', 'promo_noprods', 'Promo Sin Costo', 'pn@t13.local', password_hash, 'role-promo-noprod', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,),
                )
                con.commit()
            finally:
                con.close()

            # 3. Login como Dueño y abrir Inventario -> Promociones y combos
            p2, hwnd_main = login_window(exe_path, work_dir, env, owner_user, owner_pass)
            procs.append(p2)
            set_profile_touch(hwnd_main, False)

            # Abrir turno para poder vender más adelante
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            sh_edits = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            set_edit_text(sh_edits[0], "500.00")
            btn_open = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno de Caja" in t)
            post_click_button(btn_open)
            hwnd_turno, _ = wait_for_window(p2.pid, "Turno Abierto", timeout=8.0)
            click_button(next(h for (h, c, _t) in enum_children(hwnd_turno) if c.lower() == "button"))
            time.sleep(0.3)

            click_sidebar_section(hwnd_main, "Inventario")
            time.sleep(0.35)
            click_subnav(hwnd_main, "Promociones y combos")
            time.sleep(0.45)

            # --- 3A. Crear Porcentaje 12.5 % en categoría Bebidas ---
            pc = locate_promo_tab_controls(hwnd_main)
            assert any("mayor prioridad" in s for s in pc["statics"]), pc["statics"]
            set_edit_text(pc["right_edits"][0], "Descuento 12.5%")
            click_button(pc["btns"]["Porcentaje"])
            time.sleep(0.15)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][1], "12.5")
            click_button(pc["btns"]["Una categoría"])
            time.sleep(0.15)
            pc = locate_promo_tab_controls(hwnd_main)
            select_combo_by_prefix(pc["combo_cat"], "Bebidas")
            set_edit_text(pc["right_edits"][2], "5")  # prioridad
            click_button(pc["btns"]["Guardar promoción"])
            time.sleep(0.35)

            # --- 3B. Crear Monto fijo $5.00 en Galleta Avena ---
            pc = locate_promo_tab_controls(hwnd_main)
            click_button(pc["btns"]["Nueva promoción"])
            time.sleep(0.2)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][0], "Menos $5 Galleta")
            click_button(pc["btns"]["Monto menos por pieza"])
            time.sleep(0.15)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][1], "5.00")
            scope_rows = read_list_rows(pc["scope_list"], 2)
            gal_sidx = next(i for i, r in enumerate(scope_rows) if r[1] == "Galleta Avena")
            select_list_row(pc["scope_list"], gal_sidx)
            time.sleep(0.15)
            set_edit_text(pc["right_edits"][2], "3")  # prioridad
            click_button(pc["btns"]["Guardar promoción"])
            time.sleep(0.35)

            # --- 3C. Crear 2x1 con horario y días (Lun, Mar) sobre Café Americano ---
            pc = locate_promo_tab_controls(hwnd_main)
            click_button(pc["btns"]["Nueva promoción"])
            time.sleep(0.2)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][0], "2x1 Café Horario")
            click_button(pc["btns"]["Lleva N, paga M"])
            time.sleep(0.15)
            pc = locate_promo_tab_controls(hwnd_main)
            # right_edits: [0]=Nombre, [1]=N, [2]=M, [3]=Prioridad, [4]=Desde fecha, [5]=Hasta fecha, [6]=Desde hora, [7]=Hasta hora
            set_edit_text(pc["right_edits"][1], "2")
            set_edit_text(pc["right_edits"][2], "1")
            scope_rows = read_list_rows(pc["scope_list"], 2)
            cafe_sidx = next(i for i, r in enumerate(scope_rows) if r[1] == "Café Americano")
            select_list_row(pc["scope_list"], cafe_sidx)
            time.sleep(0.15)
            set_edit_text(pc["right_edits"][3], "10")
            click_button(pc["btns"]["Lun"])
            click_button(pc["btns"]["Mar"])
            set_edit_text(pc["right_edits"][6], "07:00")
            set_edit_text(pc["right_edits"][7], "11:00")
            click_button(pc["btns"]["Guardar promoción"])
            time.sleep(0.35)

            # Validar en SQLite los enteros exactos y días
            con = sqlite3.connect(db_path)
            try:
                rows_db = con.execute(
                    "SELECT name, kind, percent_bp, fixed_off_cents, buy_qty, pay_qty, priority, weekdays_mask, time_from_min, time_to_min FROM promotions ORDER BY priority DESC"
                ).fetchall()
            finally:
                con.close()
            assert ("2x1 Café Horario", "NXM", 0, 0, 2, 1, 10, 3, 420, 660) in rows_db, rows_db
            assert ("Descuento 12.5%", "PERCENT", 1250, 0, 0, 0, 5, 0, None, None) in rows_db, rows_db
            assert ("Menos $5 Galleta", "FIXED", 0, 500, 0, 0, 3, 0, None, None) in rows_db, rows_db

            # Verificar lista en UI: summary_text y validity_text exactos y sin jerga
            pc = locate_promo_tab_controls(hwnd_main)
            ui_promos = read_list_rows(pc["list"], 6)
            check_no_jargon([cell for row in ui_promos for cell in row] + pc["statics"], "Promociones")
            p_by_name = {r[0]: r for r in ui_promos}
            assert p_by_name["Descuento 12.5%"][1] == "12.5 % de descuento" and p_by_name["Descuento 12.5%"][3] == "Siempre" and p_by_name["Descuento 12.5%"][5] == "Vigente ahora", p_by_name["Descuento 12.5%"]
            assert p_by_name["Menos $5 Galleta"][1] == "$5.00 menos en cada pieza" and p_by_name["Menos $5 Galleta"][2] == "Galleta Avena", p_by_name["Menos $5 Galleta"]
            assert p_by_name["2x1 Café Horario"][1] == "Lleva 2 y paga 1" and "07:00" in p_by_name["2x1 Café Horario"][3], p_by_name["2x1 Café Horario"]

            # --- 3D. Editar prioridad de "Descuento 12.5%" a 12 + desactivar "Menos $5 Galleta" ---
            d_idx = next(i for i, r in enumerate(ui_promos) if r[0] == "Descuento 12.5%")
            select_list_row(pc["list"], d_idx)
            time.sleep(0.25)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][2], "12")
            click_button(pc["btns"]["Guardar promoción"])
            time.sleep(0.3)

            pc = locate_promo_tab_controls(hwnd_main)
            ui_promos2 = read_list_rows(pc["list"], 6)
            g_idx = next(i for i, r in enumerate(ui_promos2) if r[0] == "Menos $5 Galleta")
            select_list_row(pc["list"], g_idx)
            time.sleep(0.25)
            pc = locate_promo_tab_controls(hwnd_main)
            post_click_button(pc["btns"]["Desactivar"])
            confirm_message_dialog(p2.pid, "Confirmar estado de promoción")
            pc = locate_promo_tab_controls(hwnd_main)
            ui_promos3 = read_list_rows(pc["list"], 6)
            assert dict((r[0], r[5]) for r in ui_promos3)["Menos $5 Galleta"] == "Desactivada", ui_promos3

            con = sqlite3.connect(db_path)
            try:
                assert con.execute("SELECT priority FROM promotions WHERE name='Descuento 12.5%'").fetchone()[0] == 12
                assert con.execute("SELECT is_active FROM promotions WHERE name='Menos $5 Galleta'").fetchone()[0] == 0
            finally:
                con.close()

            # --- 3E. Crear 2x1 vigente siempre ("2x1 Café Real", prioridad 20) y verificar en POS Venta ---
            click_button(pc["btns"]["Nueva promoción"])
            time.sleep(0.2)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][0], "2x1 Café Real")
            click_button(pc["btns"]["Lleva N, paga M"])
            time.sleep(0.15)
            pc = locate_promo_tab_controls(hwnd_main)
            set_edit_text(pc["right_edits"][1], "2")
            set_edit_text(pc["right_edits"][2], "1")
            scope_rows = read_list_rows(pc["scope_list"], 2)
            cafe_sidx2 = next(i for i, r in enumerate(scope_rows) if r[1] == "Café Americano")
            select_list_row(pc["scope_list"], cafe_sidx2)
            time.sleep(0.15)
            set_edit_text(pc["right_edits"][3], "20")
            click_button(pc["btns"]["Guardar promoción"])
            time.sleep(0.3)

            # Geometría Promociones Estándar y Táctil (con teclado numérico)
            pc = locate_promo_tab_controls(hwnd_main)
            assert_geometry(hwnd_main, pc["panel"], False, 1200, 720, check_list=pc["list"])
            assert_geometry(hwnd_main, pc["panel"], False, 960, 600)
            set_profile_touch(hwnd_main, True)
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Promociones y combos")
            pc_t = locate_promo_tab_controls(hwnd_main)
            assert_geometry(hwnd_main, pc_t["panel"], True, 1200, 720, check_list=pc_t["list"])
            # Teclado numérico T4 en Porcentaje
            click_button(pc_t["btns"]["Porcentaje"])
            time.sleep(0.15)
            pc_t = locate_promo_tab_controls(hwnd_main)
            click_keypad_seq(hwnd_main, pc_t["right_edits"][1], ["1", "5"])
            assert get_edit_text(pc_t["right_edits"][1]) == "15"
            assert_geometry(hwnd_main, pc_t["panel"], True, 960, 600)
            set_profile_touch(hwnd_main, False)
            set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
            time.sleep(0.3)
            print(f"[OK] {label} Promociones: alta, edición, desactivación, SQLite y geometría verificadas.", flush=True)

            # --- 4. Combos: crear con 2 partes, ver costo desconocido, editar, ver costo/margen conocido, baja/reactivar ---
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Promociones y combos")
            pc = locate_promo_tab_controls(hwnd_main)
            click_button(pc["btns"]["Combos"])
            time.sleep(0.35)

            cc = locate_combo_tab_controls(hwnd_main)
            set_edit_text(cc["txt_name"], "Combo Desayuno")
            set_edit_text(cc["txt_price"], "90.00")
            set_edit_text(cc["txt_sku"], "CMB-13")
            set_combo_category(hwnd_main, cc, "Combos")
            cc = locate_combo_tab_controls(hwnd_main)

            select_combo_by_prefix(cc["combo_part_prod"], "Café Americano")
            set_edit_text(cc["txt_part_qty"], "2")
            click_button(cc["btns"]["Agregar parte"])
            time.sleep(0.15)

            select_combo_by_prefix(cc["combo_part_prod"], "Galleta Avena")
            set_edit_text(cc["txt_part_qty"], "1")
            click_button(cc["btns"]["Agregar parte"])
            time.sleep(0.15)

            click_button(cc["btns"]["Guardar combo"])
            time.sleep(0.35)

            cc = locate_combo_tab_controls(hwnd_main)
            cmb_rows = read_list_rows(cc["list"], 5)
            check_no_jargon([cell for row in cmb_rows for cell in row] + cc["statics"], "Combos")
            assert any(r[0] == "Combo Desayuno" and r[1] == "$90.00" and "2 x Café Americano, 1 x Galleta Avena" == r[2] for r in cmb_rows), cmb_rows
            # Galleta Avena no tiene compras -> falta costo y NO margen
            assert any("Falta conocer el costo de:" in s and "Galleta Avena" in s for s in cc["statics"]), cc["statics"]
            assert not any("Margen:" in s for s in cc["statics"]), f"No debe mostrar Margen cuando falta costo: {cc['statics']}"

            # Registrar compra de Galleta Avena Pz ($5.00 cada pieza) y editar combo a precio $80.00 con 1 x Café + 1 x Galleta
            con = sqlite3.connect(db_path)
            try:
                con.execute(
                    "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) "
                    "VALUES ('pur-gal-t13', 'ing-gal-t13', 10, 'piezas', 5000, 10, ?)",
                    (now_str,),
                )
                con.execute(
                    "UPDATE ingredients SET stock_value_micro = 10000000000, last_avg_value_micro = 5000000000, last_avg_units = 10 WHERE id = 'ing-gal-t13'"
                )
                con.commit()
            finally:
                con.close()

            select_combo_by_prefix(cc["combo_part_prod"], "Café Americano")
            set_edit_text(cc["txt_part_qty"], "1")
            click_button(cc["btns"]["Agregar parte"])
            time.sleep(0.15)
            set_edit_text(cc["txt_price"], "80.00")
            click_button(cc["btns"]["Guardar combo"])
            time.sleep(0.35)

            cc = locate_combo_tab_controls(hwnd_main)
            # Costo = $4.00 + $5.00 = $9.00; Margen = $80.00 - $9.00 = $71.00
            assert any("Costo estimado: $9.00" in s for s in cc["statics"]), cc["statics"]
            assert any("Margen: $71.00" in s for s in cc["statics"]), cc["statics"]

            # Dar de baja -> Ver dados de baja -> Reactivar
            post_click_button(cc["btns"]["Dar de baja"])
            confirm_message_dialog(p2.pid, "Confirmar cambio de estado")
            cc = locate_combo_tab_controls(hwnd_main)
            assert all(r[0] != "Combo Desayuno" for r in read_list_rows(cc["list"], 5)), "Dado de baja debe ocultarse"
            click_button(cc["btns"]["Ver dados de baja"])
            time.sleep(0.25)
            cc = locate_combo_tab_controls(hwnd_main)
            cmb_all = read_list_rows(cc["list"], 5)
            cd_idx = next(i for i, r in enumerate(cmb_all) if r[0] == "Combo Desayuno" and r[4] == "Dado de baja")
            select_list_row(cc["list"], cd_idx)
            time.sleep(0.2)
            cc = locate_combo_tab_controls(hwnd_main)
            post_click_button(cc["btns"]["Reactivar"])
            confirm_message_dialog(p2.pid, "Confirmar cambio de estado")
            cc = locate_combo_tab_controls(hwnd_main)
            click_button(cc["btns"]["Viendo dados de baja"])
            time.sleep(0.2)

            # Geometría Combos Estándar y Táctil (con teclado numérico en Precio)
            cc = locate_combo_tab_controls(hwnd_main)
            assert_geometry(hwnd_main, cc["panel"], False, 1200, 720, check_list=cc["list"])
            assert_geometry(hwnd_main, cc["panel"], False, 960, 600)
            set_profile_touch(hwnd_main, True)
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Promociones y combos")
            pc_tc = locate_promo_tab_controls(hwnd_main)
            click_button(pc_tc["btns"]["Combos"])
            time.sleep(0.35)
            cc_t = locate_combo_tab_controls(hwnd_main)
            assert_geometry(hwnd_main, cc_t["panel"], True, 1200, 720, check_list=cc_t["list"])
            click_keypad_seq(hwnd_main, cc_t["txt_price"], ["8", "5"])
            assert get_edit_text(cc_t["txt_price"]) == "85"
            assert_geometry(hwnd_main, cc_t["panel"], True, 960, 600)
            set_profile_touch(hwnd_main, False)
            set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
            time.sleep(0.3)
            print(f"[OK] {label} Combos: creación, edición, costo/margen, baja/reactivación y geometría verificadas.", flush=True)

            # --- 5. Venta real en POS: 2x1 Café Real + Combo Desayuno y descuento de ingredientes en SQLite ---
            click_sidebar_section(hwnd_main, "Punto de Venta")
            time.sleep(0.35)
            pos_children = enum_children(hwnd_main)
            pos_vis = [(h, c, t) for (h, c, t) in pos_children if user32.IsWindowVisible(h)]
            pos_edits = [h for (h, c, _t) in pos_vis if c.lower() == "edit"]
            pos_btns = {t: h for (h, c, t) in pos_vis if c.lower() == "button" and t}
            pos_lists = [h for (h, c, _t) in pos_vis if c.lower() == "syslistview32"]

            set_edit_text(pos_edits[0], "Café Americano")
            click_button(pos_btns["Buscar"])
            time.sleep(0.2)
            set_edit_text(pos_edits[1], "2")
            click_button(pos_btns["Agregar al Carrito"])
            time.sleep(0.3)

            pos_rows = read_list_rows(pos_lists[0], 7)
            assert any("2x1 Café Real: -$40.00" == r[5] for r in pos_rows), f"2x1 creado por pantalla no aplicó en POS: {pos_rows}"

            set_edit_text(pos_edits[0], "Combo Desayuno")
            click_button(pos_btns["Buscar"])
            time.sleep(0.2)
            set_edit_text(pos_edits[1], "1")
            click_button(pos_btns["Agregar al Carrito"])
            time.sleep(0.3)

            post_click_button(pos_btns["Cobrar"])
            hwnd_chk, _ = wait_for_window(p2.pid, "Cobro de Venta", timeout=8.0)
            btn_conf = next(h for (h, c, t) in enum_children(hwnd_chk) if c.lower() == "button" and "Confirmar Venta" in t)
            post_click_button(btn_conf)
            hwnd_tkt, _ = wait_for_window(p2.pid, "Venta Registrada", timeout=8.0)
            click_button(next(h for (h, c, t) in enum_children(hwnd_tkt) if c.lower() == "button" and "Ahora no" in t))
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                sale_row = con.execute("SELECT discount_cents, total_cents FROM sales ORDER BY rowid DESC LIMIT 1").fetchone()
                cafe_stock = con.execute("SELECT current_stock FROM ingredients WHERE id='ing-cafe-t13'").fetchone()[0]
                gal_stock = con.execute("SELECT current_stock FROM ingredients WHERE id='ing-gal-t13'").fetchone()[0]
            finally:
                con.close()
            # 2 Cafés (2*4000 - 4000 = 4000) + 1 Combo (8000) = total 12000, desc 4000
            assert sale_row == (4000, 12000), f"Venta esperada (4000, 12000), obtuvo {sale_row}"
            # Café consumido: 2 por POS + 1 por combo = 3 * 20g = 60g -> 1,000,000 - 60,000 = 940,000 mg
            assert cafe_stock == 940000, f"Stock Café grano esperado 940000 mg, obtuvo {cafe_stock}"
            # Galleta consumida: 1 por combo -> 20 - 1 = 19 pz
            assert gal_stock == 19, f"Stock Galleta esperado 19 pz, obtuvo {gal_stock}"
            print(f"[OK] {label} Venta POS de 2x1 + Combo y descuento de ingredientes en SQLite verificados.", flush=True)

            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p2.wait(timeout=6)

            # --- 6. Rol con inventory.promotions pero SIN inventory.manage_products ---
            p3, hwnd_np = login_window(exe_path, work_dir, env, "promo_noprods", owner_pass)
            procs.append(p3)
            click_sidebar_section(hwnd_np, "Inventario")
            click_subnav(hwnd_np, "Promociones y combos")
            pc_np = locate_promo_tab_controls(hwnd_np)
            click_button(pc_np["btns"]["Combos"])
            time.sleep(0.35)
            cc_np = locate_combo_tab_controls(hwnd_np)
            select_list_row(cc_np["list"], 0)
            time.sleep(0.25)
            cc_np = locate_combo_tab_controls(hwnd_np)
            assert not any("Costo" in s or "Margen" in s for s in cc_np["statics"]), (
                f"Sin inventory.manage_products no debe mostrarse costo ni margen: {cc_np['statics']}"
            )
            user32.PostMessageW(hwnd_np, WM_CLOSE, 0, 0)
            p3.wait(timeout=6)
            print(f"[OK] {label} Ocultamiento de costo/margen sin inventory.manage_products verificado.", flush=True)

            # --- 7. Cajero sin inventory.promotions ---
            p4, hwnd_caj = login_window(exe_path, work_dir, env, "cajero_t13", owner_pass)
            procs.append(p4)
            click_sidebar_section(hwnd_caj, "Inventario")
            click_subnav(hwnd_caj, "Promociones y combos")
            caj_statics = [t for (h, c, t) in enum_children(hwnd_caj) if c.lower() == "static" and user32.IsWindowVisible(h) and t]
            assert any("Sin permiso para administrar promociones" in s for s in caj_statics), caj_statics
            user32.PostMessageW(hwnd_caj, WM_CLOSE, 0, 0)
            p4.wait(timeout=6)
            print(f"[OK] {label} Bloqueo a Cajero sin inventory.promotions verificado.", flush=True)

        finally:
            for p in procs:
                if p.poll() is None:
                    p.kill()


def main():
    dpi, scale = ensure_supported_system_dpi()
    print(f"Sistema verificado con {format_dpi_info(dpi)} (escala={scale:.2f})")
    ensure_no_app_running()
    run_suite("Release", RELEASE_EXE)
    ensure_no_app_running()
    run_suite("Debug", DEBUG_EXE)
    print("\n[EXITO TOTAL] Prueba T13 superada en Release y Debug.")


if __name__ == "__main__":
    main()
