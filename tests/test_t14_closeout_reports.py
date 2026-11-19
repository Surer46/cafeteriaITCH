#!/usr/bin/env python3
"""
test_t14_closeout_reports.py - Prueba GUI de integración del Bloque T14 (I4).

Verifica en Release y Debug (SQLite aislado, CAFETERIA_UPDATE_CHECK=0):
1. Caja y turnos (arqueo, movimientos de caja, permisos e historial):
   - Fondo inicial $500.00 + 3 ventas reales POS (efectivo, tarjeta y con descuento) + Entrada ($50.00) + Salida ($40.00).
   - Salida mayor que efectivo esperado rechazada con mensaje del servicio ("No hay tanto efectivo en caja para esa salida").
   - Vista previa coincide exactamente con SQLite.
   - Cajero intenta cerrar turno del Dueño: ve vista previa y botón; rechazo con mensaje del servicio y turno sigue abierto.
   - Cierre con faltante sin motivo rechazado (turno sigue abierto en SQLite); con motivo persiste difference_cents y difference_reason en SQLite.
   - Segundo turno con cierre exacto ("Cuadra", difference_cents = 0).
   - Historial "Cortes anteriores": listado con rango/presets y detalle get_closeout con movimientos.
2. Reportes (los 7 reportes, rangos, totales, exportación CSV y permisos):
   - Todos los 7 reportes con datos sembrados/reales; totales iguales a SQLite (neto = bruto - descuento = pagos).
   - Inventario sin compras en "Mermas y ajustes" muestra "Sin costo conocido" (nunca $0.00).
   - Rango invertido y rango > 366 días muestran el error del servicio.
   - Exportar a CSV en carpeta temporal: BOM UTF-8, separador coma, encabezado y filas esperadas.
   - Segundo intento al mismo archivo: Cancelar no modifica archivo; Reemplazar sí lo actualiza.
   - Cajero sin reports.view: "Sin permiso para ver reportes".
3. Geometría Estándar y Táctil (1200x720 y 960x600), columnas medidas, WS_HSCROLL=0 en Reportes a 1200x720 y teclado T4.
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
WS_VSCROLL = 0x00200000


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
            time.sleep(0.22)
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
    proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    hwnd_login, _ = wait_for_window(proc.pid, "Iniciar Sesión", timeout=12.0)
    edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_login) if c.lower() == "edit" and user32.IsWindowVisible(h)],
        key=lambda x: x[1][1],
    )
    set_edit_text(edits[0][0], username)
    set_edit_text(edits[1][0], password)
    btn_login = next(h for (h, c, t) in enum_children(hwnd_login) if c.lower() == "button" and "Iniciar Sesión" in t)
    click_button(btn_login)
    hwnd_main, _ = wait_for_window(proc.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
    set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
    time.sleep(0.35)
    return proc, hwnd_main


def set_profile_touch(hwnd_main, want_touch):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    is_touch = any(t == "Estándar" for (_h, t) in btns)
    if is_touch == want_touch:
        return
    toggle = next(h for (h, t) in btns if "Touch" in t or "Estándar" in t)
    click_button(toggle)
    time.sleep(0.45)


def wait_messagebox_and_read(pid, title_sub, btn_pref="Aceptar", timeout=8.0):
    hwnd_dlg, _ = wait_for_window(pid, title_sub, timeout=timeout)
    time.sleep(0.15)
    children = enum_children(hwnd_dlg)
    statics = [t for (_h, c, t) in children if c.lower() == "static" and user32.IsWindowVisible(_h) and t]
    btns = [(h, t) for (h, c, t) in children if c.lower() == "button" and user32.IsWindowVisible(h)]
    target = next((h for (h, t) in btns if btn_pref in t), None)
    if target is None and btn_pref == "Sí":
        target = next((h for (h, t) in btns if "Yes" in t), None)
    if target is None and btn_pref == "Cancelar":
        target = next((h for (h, t) in btns if "No" in t or "Cancel" in t), None)
    if target is None and btns:
        target = btns[0][0]
    post_click_button(target)
    time.sleep(0.35)
    return "\n".join(statics)


def accept_save_file_dialog(pid, title_sub, target_path, timeout=8.0):
    hwnd_fd, _ = wait_for_window(pid, title_sub, timeout=timeout)
    time.sleep(0.4)
    children = enum_children(hwnd_fd)
    edits = [h for (h, c, _t) in children if c.lower() == "edit" and user32.IsWindowVisible(h)]
    assert edits, "No se encontró Edit de ruta en diálogo guardar archivo"

    def _pcls(hw):
        ph = user32.GetParent(hw)
        if not ph:
            return ""
        buf = ctypes.create_unicode_buffer(128)
        user32.GetClassNameW(ph, buf, 128)
        return buf.value.lower()

    target_edit = next((h for h in edits if "combobox" in _pcls(h)), max(edits, key=lambda h: get_rect(h)[1]))
    set_edit_text(target_edit, target_path)
    time.sleep(0.15)
    btn_save = user32.GetDlgItem(hwnd_fd, 1)
    if not btn_save:
        btn_save = next(
            (h for (h, c, t) in children if c.lower() == "button" and user32.IsWindowVisible(h) and ("Guardar" in t or "Save" in t)),
            None,
        )
    if btn_save:
        post_click_button(btn_save)
    else:
        user32.PostMessageW(target_edit, WM_KEYDOWN, VK_RETURN, 0)
        user32.PostMessageW(target_edit, WM_KEYUP, VK_RETURN, 0)
    time.sleep(0.45)


def check_no_jargon(texts, ctx):
    forbidden = [
        "INFLOW", "OUTFLOW", "SalesByDay", "SalesByProduct", "SalesByPaymentMethod",
        "SalesByCashier", "InventoryLosses", "InventoryMovements", "PurchasesSummary",
        "PURCHASE", "WASTE", "ADJUSTMENT", "EFECTIVO", "TARJETA", "TRANSFERENCIA",
        "reg-01", "shift-", "user-",
    ]
    for t in texts:
        for bad in forbidden:
            assert bad not in t, f"Jerga prohibida '{bad}' en {ctx}: '{t}'"


def find_vscroll_ancestor(hwnd, root_hwnd):
    cur = user32.GetParent(hwnd)
    while cur and cur != root_hwnd:
        st = user32.GetWindowLongW(cur, GWL_STYLE)
        if (st & WS_VSCROLL) != 0:
            return cur
        cur = user32.GetParent(cur)
    return None


def assert_geometry(hwnd_main, panel_hwnd, touch_mode, width, height, lists_to_check=None, require_no_hscroll=True):
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
            assert (vst & WS_HSCROLL) == 0, f"Scroll con barra horizontal WS_HSCROLL en {width}x{height}"
            assert vrc[2] <= main_rc[2] + edge_tol and vrc[3] <= main_rc[3] + edge_tol, (
                f"Contenedor scroll desborda en {width}x{height}: {vrc} vs {main_rc}"
            )
            assert r[0] >= vrc[0] - edge_tol and r[2] <= vrc[2] + edge_tol, (
                f"Botón '{t}' desborda horizontalmente en {width}x{height}: {r} vs {vrc}"
            )
        else:
            assert r[2] <= main_rc[2] + edge_tol and r[3] <= main_rc[3] + edge_tol, (
                f"Botón '{t}' desborda en {width}x{height}: {r} vs {main_rc}"
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
                f"Botón táctil '{t}' < 48x48 log en {width}x{height}: {bw}x{bh} (< {min_touch_px})"
            )

    for check_list in (lists_to_check or []):
        if check_list and user32.IsWindowVisible(check_list):
            headers, hwnd_hdr = get_list_headers(check_list)
            if require_no_hscroll:
                style = user32.GetWindowLongW(check_list, GWL_STYLE)
                cws = [get_list_column_width(check_list, i) for i in range(len(headers))]
                clw, _ = get_client_size(check_list)
                assert (style & WS_HSCROLL) == 0, f"Lista {headers} widths={cws} sum={sum(cws)} cw={clw} muestra barra horizontal WS_HSCROLL en {width}x{height}"
            for idx, htxt in enumerate(headers):
                cw = get_list_column_width(check_list, idx)
                tw, _ = measure_text_extent(hwnd_hdr, htxt)
                assert cw >= tw + min_px(4, scale), (
                    f"Encabezado '{htxt}' (col {idx}) recortado en {width}x{height}: col_w={cw} < tw={tw}"
                )


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


def locate_open_shift_controls(hwnd_main):
    visible = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
    btn_close = next((h for (h, c, t) in visible if c.lower() == "button" and "Cerrar Turno de Caja" in t), None)
    assert btn_close is not None, "Botón 'Cerrar Turno de Caja' no encontrado"
    co_card = user32.GetParent(btn_close)
    open_wrap = user32.GetParent(co_card)
    cur_panel = user32.GetParent(open_wrap)
    cash_panel = user32.GetParent(cur_panel)

    btn_reg = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Registrar movimiento"), None)
    assert btn_reg is not None, "Botón 'Registrar movimiento' no encontrado"
    mv_card = user32.GetParent(btn_reg)

    mv_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, mv_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    co_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, co_card)],
        key=lambda x: (x[1][1], x[1][0]),
    )
    mv_lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, mv_card)]
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, cash_panel) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, cash_panel) and t]
    return {
        "panel": cash_panel,
        "txt_move_amount": mv_edits[0][0],
        "txt_move_desc": mv_edits[1][0],
        "movements_list": mv_lists[0] if mv_lists else None,
        "txt_final_cash": co_edits[0][0],
        "txt_close_reason": co_edits[1][0],
        "btns": btns,
        "statics": statics,
    }


def locate_history_controls(hwnd_main):
    visible = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
    btn_search = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Buscar"), None)
    assert btn_search is not None, "Botón Buscar en Historial de cortes no encontrado"
    flt_card = user32.GetParent(btn_search)
    pnl_hist = user32.GetParent(flt_card)
    cash_panel = user32.GetParent(pnl_hist)

    edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, flt_card)],
        key=lambda x: x[1][0],
    )
    lists = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, pnl_hist)],
        key=lambda x: x[1][1],
    )
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, cash_panel) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, pnl_hist) and t]
    return {
        "panel": cash_panel,
        "txt_from": edits[0][0],
        "txt_to": edits[1][0],
        "history_list": lists[0][0] if lists else None,
        "moves_list": lists[1][0] if len(lists) >= 2 else None,
        "btns": btns,
        "statics": statics,
    }


def locate_reports_controls(hwnd_main):
    visible = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
    btn_export = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Exportar a CSV"), None)
    assert btn_export is not None, "Botón 'Exportar a CSV' no encontrado"
    card = user32.GetParent(btn_export)
    panel = user32.GetParent(card)

    edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, card)],
        key=lambda x: x[1][0],
    )
    combos = [h for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, card)]
    lists = [h for (h, c, _t) in visible if c.lower() == "syslistview32" and is_descendant(h, card)]
    btns = {t: h for (h, c, t) in visible if c.lower() == "button" and is_descendant(h, card) and t}
    statics = [t for (h, c, t) in visible if c.lower() == "static" and is_descendant(h, card) and t]
    return {
        "panel": panel,
        "combo_kind": combos[0] if combos else None,
        "txt_from": edits[0][0],
        "txt_to": edits[1][0],
        "list": lists[0] if lists else None,
        "btns": btns,
        "statics": statics,
    }


def perform_pos_sale(pid, hwnd_main, search_text, qty, method_label="Efectivo"):
    click_sidebar_section(hwnd_main, "Punto de Venta")
    time.sleep(0.3)
    vis = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
    edits = [h for (h, c, _t) in vis if c.lower() == "edit"]
    btns = {t: h for (h, c, t) in vis if c.lower() == "button" and t}
    set_edit_text(edits[0], search_text)
    click_button(btns["Buscar"])
    time.sleep(0.2)
    set_edit_text(edits[1], str(qty))
    click_button(btns["Agregar al Carrito"])
    time.sleep(0.25)

    post_click_button(btns["Cobrar"])
    hwnd_chk, _ = wait_for_window(pid, "Cobro de Venta", timeout=8.0)
    if method_label != "Efectivo":
        idx_map = {"Efectivo": 0, "Tarjeta": 1, "Transferencia": 2}
        idx_m = idx_map[method_label]
        cb_h = next(
            h for (h, c, t) in enum_children(hwnd_chk)
            if user32.IsWindowVisible(h) and ("combobox" in c.lower() or t == "comboBox")
        )
        known = {w for (w, _t) in find_windows_by_pid(pid)}
        crc = get_rect(cb_h)
        lp_open = (((crc[3] - crc[1]) // 2) << 16) | (max(4, (crc[2] - crc[0]) - 12) & 0xFFFF)
        user32.PostMessageW(cb_h, 0x0201, 1, lp_open)
        time.sleep(0.05)
        user32.PostMessageW(cb_h, 0x0202, 0, lp_open)
        popup_h = None
        for _ in range(40):
            for (w, _t) in find_windows_by_pid(pid):
                if w not in known:
                    popup_h = w
                    break
            if popup_h is not None:
                break
            time.sleep(0.05)
        assert popup_h is not None, "Desplegable método de pago no abrió"
        time.sleep(0.18)
        target = next((h for (h, _c, _t) in enum_children(popup_h) if user32.IsWindowVisible(h)), popup_h)
        trc = wintypes.RECT()
        user32.GetClientRect(target, ctypes.byref(trc))
        tw = max(10, trc.right - trc.left)
        th = max(30, trc.bottom - trc.top)
        lparam = (int((idx_m + 0.5) * th / 3.0) << 16) | ((tw // 2) & 0xFFFF)
        user32.PostMessageW(target, 0x0200, 0, lparam)
        time.sleep(0.05)
        user32.PostMessageW(target, 0x0201, 1, lparam)
        time.sleep(0.05)
        user32.PostMessageW(target, 0x0202, 0, lparam)
        for _ in range(30):
            if not user32.IsWindowVisible(popup_h):
                break
            time.sleep(0.05)
        time.sleep(0.18)
    btn_conf = next(h for (h, c, t) in enum_children(hwnd_chk) if c.lower() == "button" and "Confirmar Venta" in t)
    post_click_button(btn_conf)
    hwnd_tkt, _ = wait_for_window(pid, "Venta Registrada", timeout=8.0)
    click_button(next(h for (h, c, t) in enum_children(hwnd_tkt) if c.lower() == "button" and "Ahora no" in t))
    time.sleep(0.3)


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba T14 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t14_{label.lower()}_", ignore_cleanup_errors=True) as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        csv_dir = os.path.join(data_dir, "csv_export")
        os.makedirs(csv_dir, exist_ok=True)
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"

        procs = []
        owner_user = "dueno_t14"
        owner_pass = "ClaveT14!2026"
        now_str = time.strftime("%Y-%m-%d %H:%M:%S")
        yday_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - 86400))
        today_str = now_str[:10]

        try:
            # 1. Alta inicial Dueño
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_init, _ = wait_for_window(p1.pid, "Primer Inicio", timeout=12.0)
            init_edits = sorted(
                [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_init) if c.lower() == "edit" and user32.IsWindowVisible(h)],
                key=lambda x: x[1][1],
            )
            set_edit_text(init_edits[0][0], "Dueno T14")
            set_edit_text(init_edits[1][0], owner_user)
            set_edit_text(init_edits[2][0], "dueno@t14.local")
            set_edit_text(init_edits[3][0], owner_pass)
            set_edit_text(init_edits[4][0], owner_pass)
            btn_init = next(h for (h, c, _t) in enum_children(hwnd_init) if c.lower() == "button" and user32.IsWindowVisible(h))
            click_button(btn_init)
            hwnd_m1, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio",), timeout=15.0)
            user32.PostMessageW(hwnd_m1, WM_CLOSE, 0, 0)
            p1.wait(timeout=8)

            # 2. Sembrado de catálogo, ingredientes (con y sin compras), merma, ajuste, promoción 10% y Cajero
            con = sqlite3.connect(db_path)
            try:
                cur = con.cursor()
                cur.executemany(
                    "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold, stock_value_micro, last_avg_value_micro, last_avg_units) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    [
                        ("ing-cafe-t14", "Café Grano T14", "MASS", "mg", 950000, 100000, 19000000000, 20000000000, 1000000),
                        ("ing-harina-t14", "Harina Sin Costo", "MASS", "mg", 500000, 50000, 0, 0, 0),
                        ("ing-agua-t14", "Agua Pieza T14", "COUNT", "pz", 3, 5, 0, 0, 0),
                    ],
                )
                cur.execute(
                    "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) "
                    "VALUES ('pur-t14-1', 'ing-cafe-t14', 1, 'kg', 20000, 1000000, ?)",
                    (now_str,),
                )
                cur.executemany(
                    "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) VALUES (?, ?, ?, ?, ?, ?)",
                    [
                        ("mov-t14-pur", "ing-cafe-t14", "PURCHASE", 1000000, "Compra inicial Café", now_str),
                        ("mov-t14-wst", "ing-cafe-t14", "WASTE", -100000, "Derrame prueba", now_str),
                        ("mov-t14-adj", "ing-cafe-t14", "ADJUSTMENT", 50000, "Sobrepaso conteo", now_str),
                        ("mov-t14-unk", "ing-harina-t14", "WASTE", -25000, "Merma sin compra", now_str),
                        ("mov-t14-agua", "ing-agua-t14", "PURCHASE", 3, "Compra ayer agua", yday_str),
                    ],
                )
                cur.executemany(
                    "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, is_combo, created_at) VALUES (?, ?, ?, ?, ?, ?, 0, 1, 0, ?)",
                    [
                        ("prod-t14-a", "T14-A", None, "Café Americano T14", "Bebidas", 10000, now_str),
                        ("prod-t14-b", "T14-B", None, "Pastel T14", "Postres", 8000, now_str),
                        ("prod-t14-c", "T14-C", None, "Té Verde T14", "Bebidas Promo", 10000, now_str),
                        ("prod-t14-poco", "T14-POCO", None, "Agua Botella T14", "Bebidas", 1500, now_str),
                    ],
                )
                cur.execute(
                    "INSERT INTO recipes (id, product_id, version, created_at) VALUES ('rcp-agua-t14', 'prod-t14-poco', 1, ?)",
                    (now_str,),
                )
                cur.execute(
                    "INSERT INTO recipe_items (recipe_id, ingredient_id, quantity_base_units) VALUES ('rcp-agua-t14', 'ing-agua-t14', 1)"
                )
                # Promoción 10% sobre Té Verde T14 -> $100 - $10 = $90
                cur.execute(
                    "INSERT INTO promotions (id, name, kind, percent_bp, scope_kind, priority, is_active, created_at, updated_at) "
                    "VALUES ('promo-t14-10', 'Descuento 10% Té', 'PERCENT', 1000, 'PRODUCT', 5, 1, ?, ?)",
                    (now_str, now_str),
                )
                cur.execute(
                    "INSERT INTO promotion_targets (promotion_id, product_id) VALUES ('promo-t14-10', 'prod-t14-c')"
                )
                # Usuario Cajero (tiene shift.operate, SIN shift.close_others ni reports.view)
                cur.execute(
                    """
                    INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                    SELECT 'user-caj-t14', 'cajero_t14', 'Cajero T14', 'caj@t14.local', password_hash, 'role-cashier', 1, created_at
                    FROM users WHERE username = ? LIMIT 1
                    """,
                    (owner_user,),
                )
                con.commit()
            finally:
                con.close()

            # 3. Login como Dueño, abrir turno ($500.00), hacer 3 ventas reales (efectivo, tarjeta, con descuento)
            p2, hwnd_main = login_window(exe_path, work_dir, env, owner_user, owner_pass)
            procs.append(p2)
            set_profile_touch(hwnd_main, False)

            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)
            sh_open_edits = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            set_edit_text(sh_open_edits[0], "500.00")
            btn_open = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno de Caja" in t)
            post_click_button(btn_open)
            wait_messagebox_and_read(p2.pid, "Turno Abierto", "Aceptar")

            # Venta 1: Café Americano $100.00 (Efectivo)
            perform_pos_sale(p2.pid, hwnd_main, "Café Americano T14", 1, "Efectivo")
            # Venta 2: Pastel $80.00 (Tarjeta)
            perform_pos_sale(p2.pid, hwnd_main, "Pastel T14", 1, "Tarjeta")
            # Venta 3: Té Verde $100.00 - 10% ($10.00) = $90.00 (Efectivo)
            perform_pos_sale(p2.pid, hwnd_main, "Té Verde T14", 1, "Efectivo")

            # 4. Caja y turnos: Entrada ($50.00), Salida excedida (rechazada), Salida válida ($40.00)
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.35)
            sh = locate_open_shift_controls(hwnd_main)
            check_no_jargon(sh["statics"], "Caja abierta inicial")
            assert get_edit_text(sh["txt_final_cash"]) == "", "Efectivo contado debe iniciar vacío"
            assert not any("Faltan" in s or "Sobran" in s or "Cuadra" in s for s in sh["statics"]), (
                f"No debe mostrar diferencia antes de escribir contado: {sh['statics']}"
            )

            # Entrada $50.00
            set_edit_text(sh["txt_move_amount"], "50.00")
            set_edit_text(sh["txt_move_desc"], "Cambio inicial extra")
            click_button(sh["btns"]["Registrar movimiento"])
            time.sleep(0.3)

            # Salida excesiva $9999.00 -> debe mostrar error del servicio
            click_button(sh["btns"]["Salida de efectivo"])
            time.sleep(0.15)
            set_edit_text(sh["txt_move_amount"], "9999.00")
            set_edit_text(sh["txt_move_desc"], "Retiro imposible")
            click_button(sh["btns"]["Registrar movimiento"])
            time.sleep(0.3)
            sh = locate_open_shift_controls(hwnd_main)
            assert any("No hay tanto efectivo en caja" in s for s in sh["statics"]), (
                f"Falta mensaje exacto de salida excesiva: {sh['statics']}"
            )

            # Salida válida $40.00
            set_edit_text(sh["txt_move_amount"], "40.00")
            set_edit_text(sh["txt_move_desc"], "Compra hielo")
            click_button(sh["btns"]["Registrar movimiento"])
            time.sleep(0.3)

            sh = locate_open_shift_controls(hwnd_main)
            mv_rows = read_list_rows(sh["movements_list"], 4)
            check_no_jargon([cell for r in mv_rows for cell in r] + sh["statics"], "Caja movimientos")
            assert len(mv_rows) == 2, f"Se esperaban 2 movimientos: {mv_rows}"
            assert mv_rows[0][1] == "Entrada" and mv_rows[0][2] == "$50.00" and mv_rows[0][3] == "Cambio inicial extra", mv_rows[0]
            assert mv_rows[1][1] == "Salida" and mv_rows[1][2] == "$40.00" and mv_rows[1][3] == "Compra hielo", mv_rows[1]

            # Verificar vista previa contra SQLite:
            # initial=50000, cash=10000+9000=19000, card=8000, gross=28000, disc=1000, net=27000
            # inflow=5000, outflow=4000 -> expected = 50000 + 19000 + 5000 - 4000 = 70000 ($700.00)
            joined_sh = "\n".join(sh["statics"])
            assert "Fondo inicial: $500.00" in joined_sh, joined_sh
            assert "Ventas: 3" in joined_sh and "Bruto: $280.00" in joined_sh and "Descuento: $10.00" in joined_sh and "Neto: $270.00" in joined_sh, joined_sh
            assert "Efectivo $190.00" in joined_sh and "Tarjeta $80.00" in joined_sh, joined_sh
            assert "Entradas: $50.00" in joined_sh and "Salidas: $40.00" in joined_sh, joined_sh
            assert "Efectivo esperado: $700.00" in joined_sh, joined_sh

            # 5. Cajero intenta cerrar el turno abierto por el Dueño -> ve vista previa + botón, servicio rechaza
            p_caj, hwnd_caj = login_window(exe_path, work_dir, env, "cajero_t14", owner_pass)
            procs.append(p_caj)
            click_sidebar_section(hwnd_caj, "Caja y Reportes")
            click_subnav(hwnd_caj, "Caja y turnos")
            time.sleep(0.35)
            sh_caj = locate_open_shift_controls(hwnd_caj)
            assert any("Efectivo esperado: $700.00" in s for s in sh_caj["statics"]), "Cajero debe ver vista previa"
            set_edit_text(sh_caj["txt_final_cash"], "700.00")
            post_click_button(sh_caj["btns"]["Cerrar Turno de Caja"])
            wait_messagebox_and_read(p_caj.pid, "Confirmar Cierre de Turno", "Sí")
            err_other = wait_messagebox_and_read(p_caj.pid, "Error de Cierre", "Aceptar")
            sh_caj_after = locate_open_shift_controls(hwnd_caj)
            expected_other = "Ese turno lo abrió otra persona. Solo quien lo abrió, el Dueño o un rol con permiso para cerrar turnos de otros puede cerrarlo."
            norm_statics_caj = [" ".join(s.split()) for s in sh_caj_after["statics"]]
            assert any(expected_other in s for s in norm_statics_caj), (
                f"Mensaje exacto de rechazo al cerrar turno ajeno no coincide: {sh_caj_after['statics']}"
            )
            if err_other.strip():
                assert expected_other in err_other, err_other
            con = sqlite3.connect(db_path)
            still_open = con.execute("SELECT is_open FROM shifts ORDER BY opened_at DESC LIMIT 1").fetchone()[0]
            con.close()
            assert still_open == 1, "El turno debe permanecer abierto tras rechazo a Cajero"

            # Cajero tampoco tiene permiso en Reportes (lo comprobamos de paso aquí)
            click_subnav(hwnd_caj, "Reportes")
            time.sleep(0.35)
            caj_rep_statics = [t for (h, c, t) in enum_children(hwnd_caj) if c.lower() == "static" and user32.IsWindowVisible(h) and t]
            assert any("Sin permiso para ver reportes" in s for s in caj_rep_statics), caj_rep_statics
            assert not any(c.lower() == "syslistview32" and user32.IsWindowVisible(h) for (h, c, _t) in enum_children(hwnd_caj))
            user32.PostMessageW(hwnd_caj, WM_CLOSE, 0, 0)
            p_caj.wait(timeout=8)
            print(f"[OK] {label} Rechazo cerrar turno ajeno + bloqueo reportes para Cajero verificados.", flush=True)

            # 6. Dueño intenta cerrar con faltante ($685.00) SIN motivo -> rechazado; CON motivo -> guardado en SQLite
            set_edit_text(sh["txt_final_cash"], "685.00")
            set_edit_text(sh["txt_close_reason"], "")
            time.sleep(0.15)
            sh_diff = locate_open_shift_controls(hwnd_main)
            assert any("Faltan $15.00" in s for s in sh_diff["statics"]), f"Falta vista previa 'Faltan $15.00': {sh_diff['statics']}"

            post_click_button(sh["btns"]["Cerrar Turno de Caja"])
            wait_messagebox_and_read(p2.pid, "Confirmar Cierre de Turno", "Sí")
            err_rsn = wait_messagebox_and_read(p2.pid, "Error de Cierre", "Aceptar")
            sh_err_after = locate_open_shift_controls(hwnd_main)
            assert "motivo" in err_rsn.lower() or any("motivo" in s.lower() and "obligatorio" not in s.lower() or "3" in s for s in sh_err_after["statics"]), (
                f"Se esperaba rechazo por falta de motivo: {sh_err_after['statics']}"
            )
            con = sqlite3.connect(db_path)
            assert con.execute("SELECT is_open FROM shifts ORDER BY opened_at DESC LIMIT 1").fetchone()[0] == 1
            con.close()

            set_edit_text(sh["txt_close_reason"], "Faltante de caja prueba")
            post_click_button(sh["btns"]["Cerrar Turno de Caja"])
            wait_messagebox_and_read(p2.pid, "Confirmar Cierre de Turno", "Sí")
            wait_messagebox_and_read(p2.pid, "Turno Cerrado", "Aceptar")
            closed_statics_1 = "\n".join(t for (h, c, t) in enum_children(hwnd_main) if c.lower() == "static" and user32.IsWindowVisible(h) and t)
            assert "Faltan $15.00" in closed_statics_1 and "Dueno T14" in closed_statics_1, closed_statics_1

            con = sqlite3.connect(db_path)
            srow = con.execute(
                "SELECT is_open, expected_cash_cents, final_cash_cents, difference_cents, difference_reason FROM shifts ORDER BY opened_at DESC LIMIT 1"
            ).fetchone()
            con.close()
            assert srow == (0, 70000, 68500, -1500, "Faltante de caja prueba"), f"SQLite corte 1 inesperado: {srow}"

            # Segundo turno con cierre exacto ("Cuadra", 0 diferencia)
            sh_re = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            set_edit_text(sh_re[0], "200.00")
            post_click_button(next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno de Caja" in t))
            wait_messagebox_and_read(p2.pid, "Turno Abierto", "Aceptar")
            sh2 = locate_open_shift_controls(hwnd_main)
            set_edit_text(sh2["txt_final_cash"], "200.00")
            post_click_button(sh2["btns"]["Cerrar Turno de Caja"])
            wait_messagebox_and_read(p2.pid, "Confirmar Cierre de Turno", "Sí")
            wait_messagebox_and_read(p2.pid, "Turno Cerrado", "Aceptar")
            closed_statics_2 = "\n".join(t for (h, c, t) in enum_children(hwnd_main) if c.lower() == "static" and user32.IsWindowVisible(h) and t)
            assert "Cuadra" in closed_statics_2, closed_statics_2

            con = sqlite3.connect(db_path)
            srow2 = con.execute(
                "SELECT is_open, expected_cash_cents, final_cash_cents, difference_cents, difference_reason FROM shifts ORDER BY opened_at DESC LIMIT 1"
            ).fetchone()
            con.close()
            assert srow2 == (0, 20000, 20000, 0, ""), f"SQLite corte 2 ('Cuadra') inesperado: {srow2}"

            # 7. Historial "Cortes anteriores" + geometría Estándar/Táctil de Caja y Cortes
            btn_hist = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Cortes anteriores")
            click_button(btn_hist)
            time.sleep(0.35)
            hc = locate_history_controls(hwnd_main)
            hist_rows = read_list_rows(hc["history_list"], 7)
            check_no_jargon([c for r in hist_rows for c in r] + hc["statics"], "Historial cortes")
            assert len(hist_rows) >= 2, f"Se esperaban >=2 cortes en historial: {hist_rows}"
            idx_shortage = next(i for i, r in enumerate(hist_rows) if "Faltan $15.00" in r[5] and r[6] == "Faltante de caja prueba")
            select_list_row(hc["history_list"], idx_shortage)
            time.sleep(0.25)
            hc = locate_history_controls(hwnd_main)
            hmv_rows = read_list_rows(hc["moves_list"], 4)
            assert len(hmv_rows) == 2 and hmv_rows[0][1] == "Entrada" and hmv_rows[1][1] == "Salida", hmv_rows
            assert_geometry(hwnd_main, hc["panel"], False, 1200, 720, [hc["history_list"], hc["moves_list"]])
            hmv_rc_1200 = get_rect(hc["moves_list"])
            assert (hmv_rc_1200[3] - hmv_rc_1200[1]) >= 90, f"hist_movements_list recortada verticalmente en 1200x720: {hmv_rc_1200}"
            assert_geometry(hwnd_main, hc["panel"], False, 960, 600, [hc["history_list"], hc["moves_list"]])
            hmv_rc_960 = get_rect(hc["moves_list"])
            assert (hmv_rc_960[3] - hmv_rc_960[1]) >= 90, f"hist_movements_list recortada verticalmente en 960x600: {hmv_rc_960}"

            # Geometría Táctil + teclado numérico T4 en Caja (abrir + movimiento + arqueo) y Cortes
            set_profile_touch(hwnd_main, True)
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.3)
            sh_t_init = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            click_keypad_seq(hwnd_main, sh_t_init[0], ["1", "5", "0"])
            assert get_edit_text(sh_t_init[0]) == "150"
            post_click_button(next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno de Caja" in t))
            wait_messagebox_and_read(p2.pid, "Turno Abierto", "Aceptar")

            sh_t = locate_open_shift_controls(hwnd_main)
            click_keypad_seq(hwnd_main, sh_t["txt_move_amount"], ["2", "5"])
            assert get_edit_text(sh_t["txt_move_amount"]) == "25"
            click_keypad_seq(hwnd_main, sh_t["txt_final_cash"], ["1", "5", "0"])
            assert get_edit_text(sh_t["txt_final_cash"]) == "150"
            assert_geometry(hwnd_main, sh_t["panel"], True, 1200, 720, [sh_t["movements_list"]])
            assert_geometry(hwnd_main, sh_t["panel"], True, 960, 600, [sh_t["movements_list"]])

            click_button(sh_t["btns"]["Cortes anteriores"])
            time.sleep(0.35)
            hc_t = locate_history_controls(hwnd_main)
            hist_rows_t = read_list_rows(hc_t["history_list"], 7)
            idx_short_t = next(i for i, r in enumerate(hist_rows_t) if "Faltan $15.00" in r[5])
            select_list_row(hc_t["history_list"], idx_short_t)
            time.sleep(0.25)
            hc_t = locate_history_controls(hwnd_main)
            assert_geometry(hwnd_main, hc_t["panel"], True, 1200, 720, [hc_t["history_list"], hc_t["moves_list"]])
            assert_geometry(hwnd_main, hc_t["panel"], True, 960, 600, [hc_t["history_list"], hc_t["moves_list"]])

            set_profile_touch(hwnd_main, False)
            set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
            print(f"[OK] {label} Caja, movimientos, arqueo, SQLite, historial y Táctil T4 verificados.", flush=True)

            # 8. Reportes: los 7 reportes, rangos inválidos, CSV (BOM/coma/Conflict) y geometría
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Reportes")
            time.sleep(0.4)

            # Verificar SQLite bruto/desc/neto de hoy
            con = sqlite3.connect(db_path)
            db_net, db_disc = con.execute("SELECT COALESCE(SUM(total_cents),0), COALESCE(SUM(discount_cents),0) FROM sales").fetchone()
            db_pay = con.execute("SELECT COALESCE(SUM(amount_cents),0) FROM payment_records").fetchone()[0]
            con.close()
            assert (db_net, db_disc, db_pay) == (27000, 1000, 27000), (db_net, db_disc, db_pay)

            rp = locate_reports_controls(hwnd_main)
            # 8.1 Ventas por día
            rows_day = read_list_rows(rp["list"], 8)
            check_no_jargon([c for r in rows_day for c in r] + rp["statics"], "Ventas por día")
            assert len(rows_day) == 2 and rows_day[1][0] == "Total", rows_day
            assert rows_day[1][1] == "3" and rows_day[1][2] == "$280.00" and rows_day[1][3] == "$10.00" and rows_day[1][4] == "$270.00", rows_day[1]
            assert_geometry(hwnd_main, rp["panel"], False, 1200, 720, [rp["list"]], require_no_hscroll=True)

            # 8.2 Ventas por producto
            select_combo_by_prefix(rp["combo_kind"], "Ventas por producto")
            rp = locate_reports_controls(hwnd_main)
            rows_prod = read_list_rows(rp["list"], 5)
            assert rows_prod[-1] == ("Total", "3", "$280.00", "$10.00", "$270.00"), rows_prod

            # 8.3 Ventas por método de pago
            select_combo_by_prefix(rp["combo_kind"], "Ventas por método de pago")
            rp = locate_reports_controls(hwnd_main)
            rows_pay = read_list_rows(rp["list"], 3)
            check_no_jargon([c for r in rows_pay for c in r], "Ventas por método")
            assert any(r[0] == "Efectivo" and r[2] == "$190.00" for r in rows_pay), rows_pay
            assert any(r[0] == "Tarjeta" and r[2] == "$80.00" for r in rows_pay), rows_pay
            assert rows_pay[-1][0] == "Total" and rows_pay[-1][2] == "$270.00", rows_pay

            # 8.4 Ventas por cajero
            select_combo_by_prefix(rp["combo_kind"], "Ventas por cajero")
            rp = locate_reports_controls(hwnd_main)
            rows_cash = read_list_rows(rp["list"], 5)
            assert rows_cash[0][0] == "Dueno T14" and rows_cash[-1] == ("Total", "3", "$280.00", "$10.00", "$270.00"), rows_cash

            # 8.5 Mermas y ajustes (con costo conocido y "Sin costo conocido")
            select_combo_by_prefix(rp["combo_kind"], "Mermas y ajustes")
            rp = locate_reports_controls(hwnd_main)
            rows_loss = read_list_rows(rp["list"], 7)
            check_no_jargon([c for r in rows_loss for c in r], "Mermas y ajustes")
            row_harina = next(r for r in rows_loss if r[0] == "Harina Sin Costo")
            assert row_harina[1] == "25 g" and row_harina[2] == "Sin costo conocido", row_harina
            row_cafe_loss = next(r for r in rows_loss if r[0] == "Café Grano T14")
            assert row_cafe_loss[1] == "100 g" and row_cafe_loss[3] == "50 g" and row_cafe_loss[2] == "$20.00", row_cafe_loss

            # 8.6 Movimientos de inventario
            select_combo_by_prefix(rp["combo_kind"], "Movimientos de inventario")
            rp = locate_reports_controls(hwnd_main)
            rows_mov = read_list_rows(rp["list"], 6)
            check_no_jargon([c for r in rows_mov for c in r], "Movimientos")
            assert any(r[2] == "Compra" and r[3] == "+1 kg" for r in rows_mov), rows_mov
            assert any(r[2] == "Merma" and r[3] == "-100 g" for r in rows_mov), rows_mov

            # 8.7 Compras
            select_combo_by_prefix(rp["combo_kind"], "Compras")
            rp = locate_reports_controls(hwnd_main)
            rows_pur = read_list_rows(rp["list"], 4)
            assert rows_pur[0] == ("Café Grano T14", "1", "1 kg", "$200.00"), rows_pur
            assert rows_pur[-1] == ("Total", "", "", "$200.00"), rows_pur

            # Rango invertido y > 366 días
            select_combo_by_prefix(rp["combo_kind"], "Ventas por día")
            rp = locate_reports_controls(hwnd_main)
            set_edit_text(rp["txt_from"], "2026-12-10")
            set_edit_text(rp["txt_to"], "2026-12-01")
            click_button(rp["btns"]["Consultar"])
            time.sleep(0.25)
            rp = locate_reports_controls(hwnd_main)
            assert any("anterior" in s.lower() or "rango" in s.lower() or "desde" in s.lower() for s in rp["statics"]), rp["statics"]

            set_edit_text(rp["txt_from"], "2024-01-01")
            set_edit_text(rp["txt_to"], "2026-11-18")
            click_button(rp["btns"]["Consultar"])
            time.sleep(0.25)
            rp = locate_reports_controls(hwnd_main)
            assert any("366" in s for s in rp["statics"]), f"Falta error de >366 días: {rp['statics']}"

            click_button(rp["btns"]["Hoy"])
            time.sleep(0.25)

            # Exportar a CSV (archivo nuevo, Cancelar conflicto, Reemplazar conflicto)
            csv_path = os.path.join(csv_dir, "ventas_hoy_t14.csv")
            post_click_button(rp["btns"]["Exportar a CSV"])
            accept_save_file_dialog(p2.pid, "Exportar reporte a CSV", csv_path)
            rp = locate_reports_controls(hwnd_main)
            assert any("Reporte guardado: ventas_hoy_t14.csv (2 filas)" in s for s in rp["statics"]), rp["statics"]
            with open(csv_path, "rb") as fh:
                raw_csv = fh.read()
            assert raw_csv.startswith(b"\xef\xbb\xbf"), "CSV sin BOM UTF-8"
            csv_lines = raw_csv.decode("utf-8-sig").splitlines()
            assert len(csv_lines) == 3 and "," in csv_lines[0] and today_str in csv_lines[1] and "Total" in csv_lines[2], csv_lines

            # Escribir marca propia para validar que Cancelar NO sobrescribe
            with open(csv_path, "ab") as fh:
                fh.write(b"#MARCA_NO_CAMBIAR\r\n")
            prev_bytes = open(csv_path, "rb").read()

            post_click_button(rp["btns"]["Exportar a CSV"])
            accept_save_file_dialog(p2.pid, "Exportar reporte a CSV", csv_path)
            wait_messagebox_and_read(p2.pid, "Archivo existente", "Cancelar")
            assert open(csv_path, "rb").read() == prev_bytes, "Cancelar no debe modificar el archivo existente"

            post_click_button(rp["btns"]["Exportar a CSV"])
            accept_save_file_dialog(p2.pid, "Exportar reporte a CSV", csv_path)
            wait_messagebox_and_read(p2.pid, "Archivo existente", "Reemplazar")
            assert open(csv_path, "rb").read() == raw_csv, "Reemplazar sí debe sobrescribir el CSV"

            # Geometría Táctil Reportes 1200x720 (sin WS_HSCROLL) y 960x600
            set_profile_touch(hwnd_main, True)
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Reportes")
            time.sleep(0.35)
            rp_t = locate_reports_controls(hwnd_main)
            assert_geometry(hwnd_main, rp_t["panel"], True, 1200, 720, [rp_t["list"]], require_no_hscroll=True)
            assert_geometry(hwnd_main, rp_t["panel"], True, 960, 600, [rp_t["list"]], require_no_hscroll=False)
            print(f"[OK] {label} 7 reportes, rangos, CSV (BOM/Conflict) y geometría verificados.", flush=True)

            # 9. Existencias (punto 4 / B16): producto directo en 'Poco' y último movimiento comparado con SQLite
            set_profile_touch(hwnd_main, False)
            set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Existencias")
            time.sleep(0.4)
            st_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
            assert st_lists, "Lista de Existencias no encontrada"
            st_rows = read_list_rows(st_lists[0], 5)
            row_agua = next((r for r in st_rows if r[0] == "Agua Botella T14"), None)
            assert row_agua is not None, f"Producto 'Agua Botella T14' no encontrado en Existencias: {st_rows}"
            con = sqlite3.connect(db_path)
            db_last_agua = con.execute(
                "SELECT MAX(created_at) FROM stock_movements WHERE ingredient_id = 'ing-agua-t14'"
            ).fetchone()[0]
            con.close()
            assert row_agua[1] == "Directo" and row_agua[2] == "3 piezas" and row_agua[3] == "Poco" and row_agua[4] == db_last_agua, (
                f"Existencias B16 esperado ('Directo', '3 piezas', 'Poco', '{db_last_agua}'), obtuvo {row_agua}"
            )
            print(f"[OK] {label} Existencias B16 ('Poco' + último movimiento '{db_last_agua}') verificado.", flush=True)

            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p2.wait(timeout=8)
        finally:
            for p in procs:
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=4)
                    except Exception:
                        p.kill()


def main():
    dpi, scale = ensure_supported_system_dpi()
    print(f"Sistema verificado con {format_dpi_info(dpi)}", flush=True)
    ensure_no_app_running()
    run_suite("Release", RELEASE_EXE)
    ensure_no_app_running()
    run_suite("Debug", DEBUG_EXE)
    print("\n[EXITO TOTAL] Prueba T14 superada en Release y Debug.", flush=True)


if __name__ == "__main__":
    main()
