#!/usr/bin/env python3
"""
test_t12_discounts_stock.py - Prueba GUI de integración del Bloque T12 (I3).

Verifica en Release y Debug (con SQLite aislado y CAFETERIA_UPDATE_CHECK=0):
1. Venta (PosPanel):
   - Descuento por línea (2x1 en 3 piezas -> 1 gratis; porcentaje sobre línea), precio original,
     descuento con nombre de promoción ("2x1 Café: -$35.00") y total de línea.
   - Resumen: Subtotal, Descuento (visible solo cuando > 0) y Total neto iguales al servicio.
   - Geometría sin recortes en Estándar y Táctil a 1200x720 y 960x600.
2. Cobro (CheckoutDialog) y Ticket (TicketPrintDialog):
   - Muestra descuento aplicado y total neto en Cobro; vista previa del Ticket muestra
     línea(s) "Desc." y fila "Descuento:"; SQLite guarda sales.discount_cents y total neto.
   - Conflicto cuando las promociones cambian entre vista previa y cobro: aviso con el
     mensaje del servicio, sin guardar venta, sin reintento automático, carrito actualizado
     al nuevo total y cobro manual posterior exitoso.
3. Existencias (InventoryStockPanel):
   - Una sola lista con los cuatro tipos ("Ingrediente", "Directo", "Preparado", "Combo"),
     existencia legible (g/kg, ml/l, piezas, N porciones y "Sin control de existencia"),
     estados ("Suficiente", "Poco", "Agotado", "Sin control") y último movimiento.
   - Búsqueda por nombre, filtro por tipo y "Solo con poco o agotado".
   - Geometría y anchos de columna medidos (1200x720 y 960x600 en Estándar y Táctil, sin WS_HSCROLL).
   - Permiso inventory.view: Cajero ve "Sin permiso para ver existencias".
4. Productos y recetas (InventoryProductsPanel):
   - Casilla "Controlar existencia" marcada por omisión al crear producto directo nuevo
     (crea ingrediente por piezas y enlace en SQLite).
   - Producto directo sin existencia controlada (casilla desmarcada al crear): muestra estado,
     explicación corta y botón "Controlar existencia" que activa el control en SQLite.
   - Combos ocultos en Productos y recetas.
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
    VK_DOWN,
    VK_RETURN,
    BM_CLICK,
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
    get_window_scale,
    send_msg,
    get_text,
    get_rect,
    get_client_size,
    move_window_logical,
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


def read_dialog_texts(hwnd):
    children = enum_children(hwnd)
    texts = [t for (h, _c, t) in children if user32.IsWindowVisible(h) and t]
    if any(c == "DirectUIHWND" for (_h, c, _t) in children):
        try:
            ps_cmd = (
                "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
                "Add-Type -AssemblyName UIAutomationClient; "
                f"$el=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]{int(hwnd)}); "
                "$el.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
                "[System.Windows.Automation.Condition]::TrueCondition)"
                "|ForEach-Object{$_.Current.Name}|Where-Object{$_}"
            )
            out = subprocess.check_output(
                ["powershell", "-NoProfile", "-Command", ps_cmd],
                text=True, encoding="utf-8", timeout=6,
            )
            for ln in out.splitlines():
                ln = ln.strip()
                if ln and ln not in texts:
                    texts.append(ln)
        except Exception:
            pass
    return texts


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
            time.sleep(0.2)
            return buf.value
    raise AssertionError(f"Opción '{prefix}' no encontrada en combo (count={count})")


def select_touch_combo_by_prefix(hwnd_combo, prefix):
    rect = get_rect(hwnd_combo)
    # Clic en lado derecho para desplegar lista TouchComboBox / wxOwnerDrawnComboBox
    click_control_at(hwnd_combo, x_off=max(10, (rect[2] - rect[0]) - 12))
    time.sleep(0.25)
    # Fallback estándar con teclado si la lista usa popup wxVListBox
    for _ in range(12):
        # Intentar leer el texto actual vía WM_GETTEXT (owner drawn combo guarda texto seleccionado)
        cur_txt = get_edit_text(hwnd_combo)
        if cur_txt.startswith(prefix):
            return cur_txt
        user32.PostMessageW(hwnd_combo, WM_KEYDOWN, VK_DOWN, 0)
        user32.PostMessageW(hwnd_combo, WM_KEYUP, VK_DOWN, 0)
        time.sleep(0.08)
    user32.PostMessageW(hwnd_combo, WM_KEYDOWN, VK_RETURN, 0)
    user32.PostMessageW(hwnd_combo, WM_KEYUP, VK_RETURN, 0)
    time.sleep(0.15)
    return get_edit_text(hwnd_combo)


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


def assert_controls_inside_no_overlap(hwnd_main, touch_mode, width, height, list_specs=None, scope_hwnd=None):
    scale = set_client_logical_size(hwnd_main, width, height, x_log=16, y_log=16)
    time.sleep(0.5)
    main_rc = get_rect(hwnd_main)
    edge_tol = max_px(4, scale)
    min_touch_px = min_px(48, scale)

    children = enum_children(hwnd_main)
    visible_btns = [
        (h, t, get_rect(h)) for (h, c, t) in children
        if c.lower() == "button" and user32.IsWindowVisible(h)
        and (scope_hwnd is None or is_descendant(h, scope_hwnd))
    ]
    for h, t, r in visible_btns:
        assert r[2] <= main_rc[2] + edge_tol and r[3] <= main_rc[3] + edge_tol, (
            f"Botón '{t}' se sale de ventana {width}x{height}: btn={r}, win={main_rc}"
        )
        if t and user32.IsWindowEnabled(h):
            tw, _ = measure_text_extent(h, t)
            cw, _ = get_client_size(h)
            assert tw <= cw + edge_tol, f"Texto recortado en botón '{t}' ({width}x{height}): tw={tw} > cw={cw}"
        if touch_mode and user32.IsWindowEnabled(h):
            bh = r[3] - r[1]
            bw = r[2] - r[0]
            assert bw >= min_touch_px and bh >= min_touch_px, (
                f"Botón táctil '{t}' menor a 48x48 log en {width}x{height}: {bw}x{bh}px (< {min_touch_px})"
            )

    if list_specs:
        for hwnd_list, check_no_hscroll, col_indices in list_specs:
            if not hwnd_list or not user32.IsWindowVisible(hwnd_list):
                continue
            if check_no_hscroll:
                style = user32.GetWindowLongW(hwnd_list, GWL_STYLE)
                assert (style & WS_HSCROLL) == 0, f"Lista muestra barra horizontal WS_HSCROLL en {width}x{height}"
                headers, hwnd_hdr = get_list_headers(hwnd_list)
                for idx, htxt in enumerate(headers):
                    cw = get_list_column_width(hwnd_list, idx)
                    tw, _ = measure_text_extent(hwnd_hdr, htxt)
                    assert cw >= tw + min_px(8, scale), (
                        f"Encabezado '{htxt}' (col {idx}) recortado en {width}x{height}: col_w={cw} < tw={tw}"
                    )
            if col_indices:
                rows = read_list_rows(hwnd_list, col_indices["ncols"])
                for col_idx in col_indices["check_cols"]:
                    cw = get_list_column_width(hwnd_list, col_idx)
                    for row in rows:
                        cell = row[col_idx]
                        if not cell:
                            continue
                        tw, _ = measure_text_extent(hwnd_list, cell)
                        assert cw >= tw + min_px(6, scale), (
                            f"Celda col {col_idx} '{cell}' recortada en {width}x{height}: col_w={cw} < tw={tw}"
                        )


def check_no_jargon(texts, ctx):
    forbidden = ["PERCENT", "NXM", "FIXED", "basis_points", "MASS", "VOLUME", "COUNT", "base_unit", "inventory.", "stock_tracked", "stock_units"]
    for t in texts:
        for bad in forbidden:
            assert bad not in t, f"Jerga prohibida '{bad}' en {ctx}: '{t}'"
        assert not re.search(r"\bmg\b", t), f"'mg' técnico visible en {ctx}: '{t}'"
        assert not re.search(r"\bpz\b", t), f"'pz' técnico visible en {ctx}: '{t}'"


def find_pos_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    edits = [h for (h, c, _t) in visible if c.lower() == "edit"]
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and t}
    lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32"]
    statics = [t for (h, c, t) in visible if c.lower() == "static" and t]
    return {
        "panel": user32.GetParent(lists[0]) if lists else None,
        "txt_search": edits[0],
        "txt_qty": edits[1],
        "txt_notes": edits[2],
        "btns": btns,
        "list": lists[0],
        "statics": statics,
    }


def locate_stock_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]
    btn_ref = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Actualizar"), None)
    assert btn_ref is not None, "Botón 'Actualizar' en Existencias no encontrado"
    card = user32.GetParent(btn_ref)
    edits = [h for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, card)]
    combos = [h for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, card)]
    wx_combos = [
        h for (h, c, _t) in visible
        if c.lower().startswith("wxwindow") and is_descendant(h, card) and h != card
        and (get_rect(h)[3] - get_rect(h)[1]) < 90
    ]
    lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, card)]
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, card) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, card) and t]
    return {
        "card": card,
        "txt_search": edits[0],
        "combo": combos[0] if combos else None,
        "touch_combo": wx_combos[0] if wx_combos else None,
        "list": lists[0] if lists else None,
        "btns": btns,
        "statics": statics,
    }


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
    left_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, list_card)],
        key=lambda x: x[1][1],
    )
    right_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel) and not is_descendant(h, list_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, panel) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, panel) and t]
    return {
        "panel": panel,
        "list": lists[0][0] if lists else None,
        "txt_search": left_edits[0][0] if left_edits else None,
        "txt_name": right_edits[0][0],
        "txt_price": right_edits[1][0],
        "txt_sku": right_edits[2][0],
        "txt_barcode": right_edits[3][0],
        "txt_category": right_edits[4][0],
        "btns": btns,
        "statics": statics,
    }


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba T12 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t12_{label.lower()}_", ignore_cleanup_errors=True) as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"

        procs = []
        owner_user = "dueno_t12"
        owner_pass = "ClaveT12!2026"

        try:
            # 1. Primer inicio: alta de Dueño
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_first, _ = wait_for_window(p1.pid, "Primer Inicio")
            fc = enum_children(hwnd_first)
            fe = [h for (h, c, _t) in fc if c.lower() == "edit"]
            fb = [h for (h, c, _t) in fc if c.lower() == "button"]
            set_edit_text(fe[0], "Dueno T12")
            set_edit_text(fe[1], owner_user)
            set_edit_text(fe[2], "dueno_t12@cafeteria.local")
            set_edit_text(fe[3], owner_pass)
            set_edit_text(fe[4], owner_pass)
            click_button(fb[0])
            hwnd_m1, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"))
            user32.PostMessageW(hwnd_m1, WM_CLOSE, 0, 0)
            p1.wait(timeout=6)

            # 2. Sembrar en SQLite: ingredientes (3 dimensiones + OK/LOW/OUT + last_movement_at),
            #    productos (Preparado, Directo con/sin control, Combo) y 2 promociones (2x1 y %),
            #    más usuario Cajero sin inventory.view.
            con = sqlite3.connect(db_path)
            now_str = time.strftime("%Y-%m-%d %H:%M:%S")
            try:
                cur = con.cursor()
                cur.executemany(
                    "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) VALUES (?, ?, ?, ?, ?, ?)",
                    [
                        ("ing-leche-t12", "Leche entera", "VOLUME", "ml", 3500, 1000),       # 3.5 l -> Suficiente
                        ("ing-azucar-t12", "Azúcar refinada", "MASS", "mg", 450000, 500000),  # 450 g <= 500 g -> Poco
                        ("ing-vaso-t12", "Vaso 12oz", "COUNT", "pz", 0, 10),                  # 0 piezas -> Agotado
                        ("ing-cafe-t12", "Café molido", "MASS", "mg", 1500000, 200000),       # 1.5 kg -> Suficiente
                        ("ing-gal-t12", "Galleta Avena Pz", "COUNT", "pz", 12, 2),            # Enlazado a directo
                    ],
                )
                cur.execute(
                    "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) VALUES (?, ?, ?, ?, ?, ?)",
                    ("mov-t12-1", "ing-leche-t12", "PURCHASE", 3500, "Compra inicial", now_str),
                )
                cur.executemany(
                    "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, is_combo, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    [
                        ("prod-cafe-t12", "CAF-12", None, "Café Americano", "Bebidas", 3500, 1, 1, 0, now_str),
                        ("prod-gal-t12", "GAL-12", None, "Galleta Avena", "Panadería", 2000, 0, 1, 0, now_str),
                        ("prod-ref-t12", "REF-12", None, "Refresco Lata", "Bebidas", 2500, 0, 1, 0, now_str),
                        ("prod-cmb-t12", "CMB-12", None, "Combo Mañana", "Combos", 5000, 0, 1, 1, now_str),
                    ],
                )
                cur.executemany(
                    "INSERT INTO recipes (id, product_id, version, created_at) VALUES (?, ?, 1, ?)",
                    [
                        ("rcp-cafe-t12", "prod-cafe-t12", now_str),
                        ("rcp-gal-t12", "prod-gal-t12", now_str),
                    ],
                )
                cur.executemany(
                    "INSERT INTO recipe_items (recipe_id, ingredient_id, quantity_base_units) VALUES (?, ?, ?)",
                    [
                        ("rcp-cafe-t12", "ing-cafe-t12", 15000),  # 15 g por taza -> 100 porciones
                        ("rcp-gal-t12", "ing-gal-t12", 1),        # 1 pieza -> 12 piezas
                    ],
                )
                cur.executemany(
                    "INSERT INTO combo_items (combo_product_id, component_product_id, quantity) VALUES (?, ?, ?)",
                    [
                        ("prod-cmb-t12", "prod-cafe-t12", 1),
                        ("prod-cmb-t12", "prod-gal-t12", 1),
                    ],
                )
                cur.executemany(
                    """
                    INSERT INTO promotions
                    (id, name, kind, percent_bp, fixed_off_cents, buy_qty, pay_qty, scope_kind, priority, is_active, weekdays_mask, created_at, updated_at)
                    VALUES (?, ?, ?, ?, 0, ?, ?, 'PRODUCT', ?, 1, 0, ?, ?)
                    """,
                    [
                        ("promo-2x1-t12", "2x1 Café", "NXM", 0, 2, 1, 10, now_str, now_str),
                        ("promo-10pct-t12", "10% Galletas", "PERCENT", 1000, 0, 0, 5, now_str, now_str),
                    ],
                )
                cur.executemany(
                    "INSERT INTO promotion_targets (promotion_id, product_id, category) VALUES (?, ?, NULL)",
                    [
                        ("promo-2x1-t12", "prod-cafe-t12"),
                        ("promo-10pct-t12", "prod-gal-t12"),
                    ],
                )
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-cajero-t12', 'cajero_t12', 'Cajero T12', 'cajero_t12@cafeteria.local', password_hash, 'role-cashier', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,),
                )
                con.commit()
            finally:
                con.close()

            # 3. Iniciar sesión como Dueño y abrir turno de caja
            p2, hwnd_main = login_window(exe_path, work_dir, env, owner_user, owner_pass)
            procs.append(p2)
            set_profile_touch(hwnd_main, False)

            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            sh_edits = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            set_edit_text(sh_edits[0], "500.00")
            btn_open = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno de Caja" in t)
            post_click_button(btn_open)
            hwnd_turno, _ = wait_for_window(p2.pid, "Turno Abierto", timeout=8.0)
            click_button(next(h for (h, c, _t) in enum_children(hwnd_turno) if c.lower() == "button"))
            time.sleep(0.3)

            # 4. VENTA: carrito con 2x1 (3x $35 = $105 -> -$35 -> $70) + porcentaje (2x $20 = $40 -> -$4 -> $36)
            click_sidebar_section(hwnd_main, "Punto de Venta")
            time.sleep(0.35)
            pos = find_pos_controls(hwnd_main)

            set_edit_text(pos["txt_search"], "Café Americano")
            click_button(pos["btns"]["Buscar"])
            time.sleep(0.25)
            set_edit_text(pos["txt_qty"], "3")
            click_button(pos["btns"]["Agregar al Carrito"])
            time.sleep(0.35)

            set_edit_text(pos["txt_search"], "Galleta Avena")
            click_button(pos["btns"]["Buscar"])
            time.sleep(0.25)
            set_edit_text(pos["txt_qty"], "2")
            click_button(pos["btns"]["Agregar al Carrito"])
            time.sleep(0.35)

            pos = find_pos_controls(hwnd_main)
            cart_rows = read_list_rows(pos["list"], 7)
            assert len(cart_rows) == 2, f"Se esperaban 2 líneas en carrito: {cart_rows}"
            cafe_row = next(r for r in cart_rows if "Café Americano" in r[1])
            gal_row = next(r for r in cart_rows if "Galleta Avena" in r[1])
            assert cafe_row[4] == "$105.00" and "2x1 Café: -$35.00" == cafe_row[5] and cafe_row[6] == "$70.00", cafe_row
            assert gal_row[4] == "$40.00" and "10% Galletas: -$4.00" == gal_row[5] and gal_row[6] == "$36.00", gal_row

            assert "$145.00" in pos["statics"], pos["statics"]
            assert "-$39.00" in pos["statics"], pos["statics"]
            assert "$106.00" in pos["statics"], pos["statics"]
            check_no_jargon(pos["statics"] + [cell for row in cart_rows for cell in row], "PosPanel")

            # Geometría Estándar 1200x720 y 960x600 (y Táctil 1200x720 y 960x600)
            assert_controls_inside_no_overlap(
                hwnd_main, False, 1200, 720,
                list_specs=[(pos["list"], True, {"ncols": 7, "check_cols": [0, 4, 5, 6]})],
                scope_hwnd=pos["panel"],
            )
            assert_controls_inside_no_overlap(hwnd_main, False, 960, 600, scope_hwnd=pos["panel"])
            pos_960 = find_pos_controls(hwnd_main)
            assert any("2x1 Café: -$35.00" in s for s in pos_960["statics"]), (
                f"En 960x600 la línea seleccionada debe mostrar '2x1 Café: -$35.00': {pos_960['statics']}"
            )

            set_profile_touch(hwnd_main, True)
            pos_t = find_pos_controls(hwnd_main)
            assert_controls_inside_no_overlap(
                hwnd_main, True, 1200, 720,
                list_specs=[(pos_t["list"], True, {"ncols": 7, "check_cols": [0, 4, 5, 6]})],
                scope_hwnd=pos_t["panel"],
            )
            assert_controls_inside_no_overlap(hwnd_main, True, 960, 600, scope_hwnd=pos_t["panel"])
            set_profile_touch(hwnd_main, False)
            set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
            time.sleep(0.3)

            # 5. COBRO y TICKET con descuentos
            pos = find_pos_controls(hwnd_main)
            post_click_button(pos["btns"]["Cobrar"])
            hwnd_chk, _ = wait_for_window(p2.pid, "Cobro de Venta", timeout=8.0)
            chk_children = enum_children(hwnd_chk)
            chk_statics = [t for (h, c, t) in chk_children if c.lower() == "static" and user32.IsWindowVisible(h) and t]
            assert any("-$39.00" in s for s in chk_statics), f"Cobro no muestra descuento -$39.00: {chk_statics}"
            assert "$106.00" in chk_statics, f"Cobro no muestra total neto $106.00: {chk_statics}"

            btn_conf = next(h for (h, c, t) in chk_children if c.lower() == "button" and "Confirmar Venta" in t)
            post_click_button(btn_conf)
            hwnd_tkt, _ = wait_for_window(p2.pid, "Venta Registrada", timeout=8.0)
            tkt_edits = [h for (h, c, _t) in enum_children(hwnd_tkt) if c.lower() == "edit"]
            assert tkt_edits, "Vista previa del ticket no encontrada"
            tkt_text = get_edit_text(tkt_edits[0])
            assert "Desc." in tkt_text and "2x1 Café" in tkt_text and "-$35.00" in tkt_text, tkt_text
            assert "Descuento:" in tkt_text and "$39.00" in tkt_text, tkt_text
            assert "$106.00" in tkt_text, tkt_text

            btn_not_now = next(h for (h, c, t) in enum_children(hwnd_tkt) if c.lower() == "button" and "Ahora no" in t)
            click_button(btn_not_now)
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                sale_row = con.execute("SELECT discount_cents, total_cents FROM sales ORDER BY rowid DESC LIMIT 1").fetchone()
            finally:
                con.close()
            assert sale_row == (3900, 10600), f"SQLite debe guardar discount_cents=3900, total_cents=10600, obtuvo {sale_row}"
            print(f"[OK] {label} Venta con 2x1 + % y Ticket verificados ($145.00 - $39.00 = $106.00).", flush=True)

            # 6. CONFLICTO: promoción se desactiva entre vista previa y cobro
            pos = find_pos_controls(hwnd_main)
            set_edit_text(pos["txt_search"], "Café Americano")
            click_button(pos["btns"]["Buscar"])
            time.sleep(0.25)
            set_edit_text(pos["txt_qty"], "3")
            click_button(pos["btns"]["Agregar al Carrito"])
            time.sleep(0.35)

            pos = find_pos_controls(hwnd_main)
            assert "$70.00" in pos["statics"] and "-$35.00" in pos["statics"], pos["statics"]
            post_click_button(pos["btns"]["Cobrar"])
            hwnd_chk2, _ = wait_for_window(p2.pid, "Cobro de Venta", timeout=8.0)

            # Desactivar 2x1 Café antes de confirmar el cobro
            con = sqlite3.connect(db_path)
            try:
                sales_before = con.execute("SELECT COUNT(*) FROM sales").fetchone()[0]
                con.execute("UPDATE promotions SET is_active = 0 WHERE id = 'promo-2x1-t12'")
                con.commit()
            finally:
                con.close()

            btn_conf2 = next(h for (h, c, t) in enum_children(hwnd_chk2) if c.lower() == "button" and "Confirmar Venta" in t)
            post_click_button(btn_conf2)
            hwnd_warn, _ = wait_for_window(p2.pid, "Promociones Actualizadas", timeout=8.0)
            warn_statics = read_dialog_texts(hwnd_warn)
            assert any("Las promociones cambiaron desde que se armó la cuenta. Revise el total antes de cobrar." in s for s in warn_statics), warn_statics

            con = sqlite3.connect(db_path)
            try:
                sales_mid = con.execute("SELECT COUNT(*) FROM sales").fetchone()[0]
            finally:
                con.close()
            assert sales_mid == sales_before, "Conflict no debe guardar la venta ni reintentar automáticamente"

            click_button(next(h for (h, c, _t) in enum_children(hwnd_warn) if c.lower() == "button"))
            time.sleep(0.35)

            pos_after = find_pos_controls(hwnd_main)
            assert "$105.00" in pos_after["statics"], f"Tras conflicto el carrito debe mostrar $105.00: {pos_after['statics']}"
            assert not any("-$" in s for s in pos_after["statics"]), f"Con descuento 0 no debe verse fila Descuento: {pos_after['statics']}"

            # Volver a cobrar manualmente con el total nuevo
            post_click_button(pos_after["btns"]["Cobrar"])
            hwnd_chk3, _ = wait_for_window(p2.pid, "Cobro de Venta", timeout=8.0)
            btn_conf3 = next(h for (h, c, t) in enum_children(hwnd_chk3) if c.lower() == "button" and "Confirmar Venta" in t)
            post_click_button(btn_conf3)
            hwnd_tkt3, _ = wait_for_window(p2.pid, "Venta Registrada", timeout=8.0)
            click_button(next(h for (h, c, t) in enum_children(hwnd_tkt3) if c.lower() == "button" and "Ahora no" in t))
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                sale_new = con.execute("SELECT discount_cents, total_cents FROM sales ORDER BY rowid DESC LIMIT 1").fetchone()
            finally:
                con.close()
            assert sale_new == (0, 10500), f"El segundo cobro debió guardar (0, 10500), obtuvo {sale_new}"
            print(f"[OK] {label} Conflicto de promoción verificado: sin venta, sin reintento, cobro manual $105.00.", flush=True)

            # 7. EXISTENCIAS: 4 tipos, formatos, estados, filtros y geometría
            click_sidebar_section(hwnd_main, "Inventario")
            time.sleep(0.35)
            click_subnav(hwnd_main, "Existencias")
            time.sleep(0.45)

            st = locate_stock_controls(hwnd_main)
            all_stock_rows = read_list_rows(st["list"], 5)
            check_no_jargon([cell for row in all_stock_rows for cell in row] + st["statics"], "Existencias")

            by_name = {r[0]: r for r in all_stock_rows}
            assert "Leche entera" in by_name and by_name["Leche entera"][1] == "Ingrediente" and by_name["Leche entera"][2] == "3.5 l" and by_name["Leche entera"][3] == "Suficiente" and by_name["Leche entera"][4] != "Sin movimientos", by_name.get("Leche entera")
            assert "Azúcar refinada" in by_name and by_name["Azúcar refinada"][1] == "Ingrediente" and by_name["Azúcar refinada"][2] == "450 g" and by_name["Azúcar refinada"][3] == "Poco", by_name.get("Azúcar refinada")
            assert "Vaso 12oz" in by_name and by_name["Vaso 12oz"][1] == "Ingrediente" and by_name["Vaso 12oz"][2] == "0 piezas" and by_name["Vaso 12oz"][3] == "Agotado", by_name.get("Vaso 12oz")
            assert "Café molido" in by_name and "kg" in by_name["Café molido"][2], by_name.get("Café molido")
            assert "Café Americano" in by_name and by_name["Café Americano"][1] == "Preparado" and "porci" in by_name["Café Americano"][2], by_name.get("Café Americano")
            assert "Galleta Avena" in by_name and by_name["Galleta Avena"][1] == "Directo" and "pieza" in by_name["Galleta Avena"][2], by_name.get("Galleta Avena")
            assert "Refresco Lata" in by_name and by_name["Refresco Lata"][1] == "Directo" and by_name["Refresco Lata"][2] == "Sin control de existencia", by_name.get("Refresco Lata")
            assert "Combo Mañana" in by_name and by_name["Combo Mañana"][1] == "Combo" and "porci" in by_name["Combo Mañana"][2], by_name.get("Combo Mañana")

            # Filtro por tipo: Combo
            select_combo_by_prefix(st["combo"], "Combo")
            combo_rows = read_list_rows(st["list"], 5)
            assert len(combo_rows) == 1 and combo_rows[0][0] == "Combo Mañana", combo_rows

            # Volver a Todos + filtro "Solo con poco o agotado"
            select_combo_by_prefix(st["combo"], "Todos los tipos")
            click_button(st["btns"]["Solo con poco o agotado"])
            time.sleep(0.25)
            low_rows = read_list_rows(st["list"], 5)
            low_names = {r[0] for r in low_rows}
            assert "Azúcar refinada" in low_names and "Vaso 12oz" in low_names, low_names
            assert "Leche entera" not in low_names and "Refresco Lata" not in low_names, low_names

            # Quitar filtro "Solo con poco o agotado" y buscar por nombre
            click_button(st["btns"]["Solo con poco o agotado"])
            time.sleep(0.25)
            set_edit_text(st["txt_search"], "Combo")
            time.sleep(0.25)
            search_rows = read_list_rows(st["list"], 5)
            assert [r[0] for r in search_rows] == ["Combo Mañana"], search_rows
            set_edit_text(st["txt_search"], "")
            time.sleep(0.25)

            # Geometría Existencias (Estándar + Táctil en 1200x720 y 960x600)
            for w, h in ((1200, 720), (960, 600)):
                st_g = locate_stock_controls(hwnd_main)
                assert_controls_inside_no_overlap(
                    hwnd_main, False, w, h,
                    list_specs=[(st_g["list"], True, {"ncols": 5, "check_cols": [1, 2, 3, 4]})],
                    scope_hwnd=st_g["card"],
                )
            set_profile_touch(hwnd_main, True)
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Existencias")
            for w, h in ((1200, 720), (960, 600)):
                st_t = locate_stock_controls(hwnd_main)
                assert_controls_inside_no_overlap(
                    hwnd_main, True, w, h,
                    list_specs=[(st_t["list"], True, {"ncols": 5, "check_cols": [1, 2, 3, 4]})],
                    scope_hwnd=st_t["card"],
                )
            set_profile_touch(hwnd_main, False)
            set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
            time.sleep(0.3)
            print(f"[OK] {label} Existencias: 4 tipos, filtros, formatos y geometría 1200x720/960x600 verificados.", flush=True)

            # 8. PRODUCTOS Y RECETAS: directos con y sin control + combos ocultos
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Productos y recetas")
            pc = locate_products_controls(hwnd_main)
            prod_names_ui = {r[0] for r in read_list_rows(pc["list"], 5)}
            assert "Combo Mañana" not in prod_names_ui, f"Los combos deben seguir ocultos en Productos y recetas: {prod_names_ui}"

            # Casilla "Controlar existencia" marcada por omisión al crear producto directo nuevo
            chk_handle = pc["btns"].get("Controlar existencia")
            assert chk_handle is not None, f"Casilla 'Controlar existencia' no encontrada: {pc['btns']}"
            chk_val = send_msg(chk_handle, BM_GETCHECK, 0, 0)
            chk_sty = user32.GetWindowLongW(chk_handle, GWL_STYLE)
            assert chk_val == BST_CHECKED, f"La casilla 'Controlar existencia' debe venir marcada por omisión (got={chk_val}, style={chk_sty:#x})"

            set_edit_text(pc["txt_name"], "Jugo Naranja")
            set_edit_text(pc["txt_price"], "30.00")
            set_edit_text(pc["txt_sku"], "JUG-12")
            set_edit_text(pc["txt_category"], "Bebidas")
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            try:
                jugo_ing = con.execute("SELECT id, unit_dimension, base_unit FROM ingredients WHERE name = 'Jugo Naranja'").fetchone()
            finally:
                con.close()
            assert jugo_ing is not None and jugo_ing[1] == "COUNT" and jugo_ing[2] == "pz", f"Debió crear ingrediente por piezas para Jugo Naranja: {jugo_ing}"

            # Crear producto directo desmarcando "Controlar existencia" -> no crea ingrediente
            pc = locate_products_controls(hwnd_main)
            click_button(pc["btns"]["Nuevo producto"])
            time.sleep(0.2)
            pc = locate_products_controls(hwnd_main)
            chk_handle2 = pc["btns"].get("Controlar existencia")
            assert chk_handle2 is not None and send_msg(chk_handle2, BM_GETCHECK, 0, 0) == BST_CHECKED
            click_button(chk_handle2)
            time.sleep(0.15)
            assert send_msg(chk_handle2, BM_GETCHECK, 0, 0) == 0, "Debió desmarcarse la casilla 'Controlar existencia'"
            set_edit_text(pc["txt_name"], "Agua Natural")
            set_edit_text(pc["txt_price"], "15.00")
            set_edit_text(pc["txt_sku"], "AGU-12")
            set_edit_text(pc["txt_category"], "Bebidas")
            click_button(pc["btns"]["Guardar"])
            time.sleep(0.35)
            con = sqlite3.connect(db_path)
            try:
                agua_ing = con.execute("SELECT COUNT(*) FROM ingredients WHERE name = 'Agua Natural'").fetchone()[0]
            finally:
                con.close()
            assert agua_ing == 0, "Desmarcar 'Controlar existencia' no debía crear ingrediente en SQLite"

            # Seleccionar Refresco Lata (directo sin stock_tracked) -> ver explicación y activar con botón
            pc = locate_products_controls(hwnd_main)
            prod_rows = read_list_rows(pc["list"], 5)
            ref_idx = next(i for i, r in enumerate(prod_rows) if r[0] == "Refresco Lata")
            select_list_row(pc["list"], ref_idx)
            time.sleep(0.3)
            pc = locate_products_controls(hwnd_main)
            assert any("Sin control de existencia" in s for s in pc["statics"]), pc["statics"]
            assert any("Se creará un ingrediente por piezas con el mismo nombre para registrar compras y mermas." in s for s in pc["statics"]), pc["statics"]
            btn_enable = pc["btns"].get("Controlar existencia")
            assert btn_enable is not None, f"Falta botón 'Controlar existencia' en directo sin control: {pc['btns']}"
            click_button(btn_enable)
            time.sleep(0.35)

            pc = locate_products_controls(hwnd_main)
            assert any("Existencia controlada (0 piezas)" in s for s in pc["statics"]), pc["statics"]
            con = sqlite3.connect(db_path)
            try:
                ref_ing = con.execute("SELECT id, unit_dimension, base_unit FROM ingredients WHERE name = 'Refresco Lata'").fetchone()
            finally:
                con.close()
            assert ref_ing is not None and ref_ing[1] == "COUNT" and ref_ing[2] == "pz", f"enable_stock_for_direct_product no creó ingrediente por piezas: {ref_ing}"
            print(f"[OK] {label} Productos y recetas: directo con control por omisión y activación posterior verificados.", flush=True)

            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p2.wait(timeout=6)

            # 9. PERMISOS DE CAJERO en Existencias: sin inventory.view
            p3, hwnd_caj = login_window(exe_path, work_dir, env, "cajero_t12", owner_pass)
            procs.append(p3)
            click_sidebar_section(hwnd_caj, "Inventario")
            click_subnav(hwnd_caj, "Existencias")
            caj_statics = [t for (h, c, t) in enum_children(hwnd_caj) if c.lower() == "static" and user32.IsWindowVisible(h) and t]
            assert any("Sin permiso para ver existencias" in s for s in caj_statics), f"Cajero debe ver 'Sin permiso para ver existencias': {caj_statics}"
            caj_lists = [h for (h, c, _t) in enum_children(hwnd_caj) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
            assert not caj_lists, "Cajero sin inventory.view no debe ver lista de existencias"
            user32.PostMessageW(hwnd_caj, WM_CLOSE, 0, 0)
            p3.wait(timeout=6)
            print(f"[OK] {label} Permisos de Cajero en Existencias verificados.", flush=True)

        except Exception:
            for p in procs:
                if p.poll() is None:
                    for wh, wt in find_windows_by_pid(p.pid):
                        stx = [t for (ch, c, t) in enum_children(wh) if c.lower() == "static" and user32.IsWindowVisible(ch) and t]
                        print(f"[DEBUG WIN pid={p.pid}] '{wt}' statics={stx}", flush=True)
            raise
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
    print("\n[EXITO TOTAL] Prueba T12 superada en Release y Debug.")


if __name__ == "__main__":
    main()
