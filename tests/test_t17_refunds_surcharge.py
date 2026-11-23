#!/usr/bin/env python3
"""
test_t17_refunds_surcharge.py - Prueba GUI del bloque T17 (interfaz de B18): reembolsos y cargo por tarjeta.

Ejecutable real, SQLite aislado (CAFETERIA_DATA_DIR), CAFETERIA_UPDATE_CHECK=0, en Release y Debug y en los perfiles
Estándar y Táctil. Verifica:
 1. Configuración > «Cobro con tarjeta (Mercado Pago)»: 4.06 % + $4.00 se guardan como 406 y 400 en SQLite, el ejemplo en
    vivo lo calcula el servicio, un texto inválido o fuera de rango muestra el mensaje y el cajero no ve la tarjeta.
 2. Cobro con tarjeta: desglose con «Cargo por pago con tarjeta (4.06 % + $4.00)» y total con cargo en el cobro, el botón y el
    ticket; cobro en efectivo sin cargo.
 3. Historial de tickets: columna «Reembolsado» y botón «Reembolsar» solo con pos.refund y con un ticket elegido.
 4. Reembolso parcial en efectivo (preparado, «Ya se preparó» marcada): motivo vacío rechazado con el mensaje del servicio, folio,
    importe, historial y efectivo esperado del corte; doble clic en confirmar no duplica.
 5. Reembolso con tarjeta: avisa que el cargo no se devuelve y que la devolución se hace por Mercado Pago.
 6. Los diálogos caben en la pantalla, sin texto cortado ni barra horizontal en la tabla de reembolso.
Las capturas quedan en qa_visual/t17/.
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
    WS_HSCROLL,
    GWL_STYLE,
    LVM_GETITEMCOUNT,
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
    set_client_logical_size,
    find_windows_by_pid,
    wait_for_window,
    enum_children,
    set_edit_text,
    click_button,
    post_click_button,
    select_list_row,
    click_sidebar_section,
    click_subnav,
    get_window_scale,
)
from fullscreen_layout_lib import detect_clipped_static_texts, capture_window_img

WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
BM_GETCHECK = 0x00F0
LVM_GETITEMTEXTW = 0x1073
LVM_GETHEADER = 0x101F
HDM_GETITEMCOUNT = 0x1200
HDM_GETITEMW = 0x120B
HDI_TEXT = 0x0002
CB_GETCOUNT = 0x0146
VK_DOWN = 0x28
VK_RETURN = 0x0D
KEYEVENTF_KEYUP = 0x0002
QA_DIR = os.path.join(REPO_ROOT, "qa_visual", "t17")


def money(cents):
    return "$%d.%02d" % (cents // 100, cents % 100)


class LVITEMW(ctypes.Structure):
    _fields_ = [("mask", wintypes.UINT), ("iItem", ctypes.c_int), ("iSubItem", ctypes.c_int), ("state", wintypes.UINT),
                ("stateMask", wintypes.UINT), ("pszText", wintypes.LPWSTR), ("cchTextMax", ctypes.c_int),
                ("iImage", ctypes.c_int), ("lParam", wintypes.LPARAM), ("iIndent", ctypes.c_int),
                ("iGroupId", ctypes.c_int), ("cColumns", wintypes.UINT), ("puColumns", ctypes.c_void_p),
                ("piColFmt", ctypes.c_void_p), ("iGroup", ctypes.c_int)]


class HDITEMW(ctypes.Structure):
    _fields_ = [("mask", wintypes.UINT), ("cxy", ctypes.c_int), ("pszText", wintypes.LPWSTR), ("hbm", wintypes.HANDLE),
                ("cchTextMax", ctypes.c_int), ("fmt", ctypes.c_int), ("lParam", wintypes.LPARAM), ("iImage", ctypes.c_int),
                ("iOrder", ctypes.c_int), ("type", wintypes.UINT), ("pvFilter", ctypes.c_void_p), ("state", wintypes.UINT)]


def get_edit_text(hwnd):
    ln = send_msg(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    send_msg(hwnd, WM_GETTEXT, ln + 2, ctypes.addressof(buf))
    return buf.value


def _remote(hwnd, fn):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    try:
        return fn(hproc)
    finally:
        kernel32.CloseHandle(hproc)


def read_list_rows(hwnd_list, num_cols):
    count = send_msg(hwnd_list, LVM_GETITEMCOUNT, 0, 0)

    def go(hproc):
        rows = []
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
                    w = ctypes.c_size_t()
                    kernel32.WriteProcessMemory(hproc, rem_item, ctypes.byref(lvi), ctypes.sizeof(lvi), ctypes.byref(w))
                    send_msg(hwnd_list, LVM_GETITEMTEXTW, r, rem_item)
                    out = ctypes.create_unicode_buffer(512)
                    rd = ctypes.c_size_t()
                    kernel32.ReadProcessMemory(hproc, rem_buf, out, 1024, ctypes.byref(rd))
                    vals.append(out.value)
                rows.append(tuple(vals))
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(hproc, rem_buf, 0, MEM_RELEASE)
        return rows

    return _remote(hwnd_list, go)


def read_list_headers(hwnd_list):
    hdr = send_msg(hwnd_list, LVM_GETHEADER, 0, 0)
    count = send_msg(hdr, HDM_GETITEMCOUNT, 0, 0)

    def go(hproc):
        heads = []
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(HDITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        rem_buf = kernel32.VirtualAllocEx(hproc, None, 512, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            for col in range(count):
                item = HDITEMW()
                item.mask = HDI_TEXT
                item.pszText = rem_buf
                item.cchTextMax = 250
                w = ctypes.c_size_t()
                kernel32.WriteProcessMemory(hproc, rem_item, ctypes.byref(item), ctypes.sizeof(item), ctypes.byref(w))
                send_msg(hdr, HDM_GETITEMW, col, rem_item)
                out = ctypes.create_unicode_buffer(256)
                rd = ctypes.c_size_t()
                kernel32.ReadProcessMemory(hproc, rem_buf, out, 512, ctypes.byref(rd))
                heads.append(out.value)
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(hproc, rem_buf, 0, MEM_RELEASE)
        return heads

    return _remote(hwnd_list, go)


def visible(hwnd):
    return [(h, c, t) for (h, c, t) in enum_children(hwnd) if user32.IsWindowVisible(h)]


def statics_of(hwnd):
    return [t for (h, c, t) in visible(hwnd) if c.lower() == "static" and t]


def buttons_of(hwnd):
    return {t: h for (h, c, t) in visible(hwnd) if c.lower() == "button" and t}


def joined(hwnd):
    return "\n".join(statics_of(hwnd))


def find_button(hwnd, prefix, exact=False):
    for (h, c, t) in visible(hwnd):
        if c.lower() == "button" and t and ((t == prefix) if exact else t.startswith(prefix)):
            return h
    return None


def find_window_with_button(pid, button_text, timeout=8.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        for (w, _t) in find_windows_by_pid(pid):
            if find_button(w, button_text, exact=True):
                return w
        time.sleep(0.1)
    raise AssertionError(f"No apareció una ventana con el botón '{button_text}'")


def wait_messagebox_and_read(pid, title_sub, btn_pref="Aceptar", timeout=8.0):
    hwnd_dlg, _ = wait_for_window(pid, title_sub, timeout=timeout)
    time.sleep(0.15)
    children = enum_children(hwnd_dlg)
    statics = [t for (_h, c, t) in children if c.lower() == "static" and user32.IsWindowVisible(_h) and t]
    btns = [(h, t) for (h, c, t) in children if c.lower() == "button" and user32.IsWindowVisible(h)]
    target = next((h for (h, t) in btns if btn_pref in t), None)
    if target is None and btn_pref == "Sí":
        target = next((h for (h, t) in btns if "Yes" in t), None)
    if target is None and btns:
        target = btns[0][0]
    post_click_button(target)
    time.sleep(0.35)
    return "\n".join(statics)


def login_window(exe_path, work_dir, env, username, password):
    proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    hwnd_login, _ = wait_for_window(proc.pid, "Iniciar Sesión", timeout=12.0)
    edits = sorted([(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_login) if c.lower() == "edit" and user32.IsWindowVisible(h)],
                   key=lambda x: x[1][1])
    set_edit_text(edits[0][0], username)
    set_edit_text(edits[1][0], password)
    btn = next(h for (h, c, t) in enum_children(hwnd_login) if c.lower() == "button" and "Iniciar Sesión" in t)
    click_button(btn)
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
    time.sleep(0.5)


def monitor_rect(hwnd):
    class MONITORINFO(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("rcMonitor", wintypes.RECT), ("rcWork", wintypes.RECT), ("dwFlags", wintypes.DWORD)]
    hmon = user32.MonitorFromWindow(hwnd, 2)
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    user32.GetMonitorInfoW(hmon, ctypes.byref(mi))
    r = mi.rcWork
    return (r.left, r.top, r.right, r.bottom)


def assert_dialog_ok(hwnd, name, shot):
    """El diálogo cabe en el área de trabajo, sin texto cortado y sin barras horizontales."""
    l, t, r, b = get_rect(hwnd)
    ml, mt, mr, mb = monitor_rect(hwnd)
    assert l >= ml - 2 and t >= mt - 2 and r <= mr + 2 and b <= mb + 2, f"{name} se sale de la pantalla: {(l, t, r, b)} vs {(ml, mt, mr, mb)}"
    clipped = detect_clipped_static_texts(hwnd)
    assert clipped in (None, "", "-", []), f"Texto cortado en {name}: {clipped}"
    for (h, c, _t) in enum_children(hwnd):
        if user32.IsWindowVisible(h):
            assert (user32.GetWindowLongW(h, GWL_STYLE) & WS_HSCROLL) == 0, f"Barra horizontal en {name} ({c})"
    if shot:
        os.makedirs(QA_DIR, exist_ok=True)
        capture_window_img(hwnd, os.path.join(QA_DIR, shot))
    return (r - l, b - t)


def db_one(db_path, sql, args=()):
    con = sqlite3.connect(db_path)
    try:
        row = con.execute(sql, args).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def db_rows(db_path, sql, args=()):
    con = sqlite3.connect(db_path)
    try:
        return con.execute(sql, args).fetchall()
    finally:
        con.close()


def last_folio(db_path):
    return db_one(db_path, "SELECT folio FROM sales ORDER BY created_at DESC, rowid DESC LIMIT 1")


def choose_method(pid, hwnd_chk, method_label):
    idx_map = {"Efectivo": 0, "Tarjeta": 1, "Transferencia": 2}
    cb_h = next(h for (h, c, t) in enum_children(hwnd_chk) if user32.IsWindowVisible(h) and ("combobox" in c.lower() or t == "comboBox"))
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
    assert popup_h is not None, "El desplegable de método de pago no abrió"
    time.sleep(0.18)
    target = next((h for (h, _c, _t) in enum_children(popup_h) if user32.IsWindowVisible(h)), popup_h)
    trc = wintypes.RECT()
    user32.GetClientRect(target, ctypes.byref(trc))
    tw = max(10, trc.right - trc.left)
    th = max(30, trc.bottom - trc.top)
    lparam = (int((idx_map[method_label] + 0.5) * th / 3.0) << 16) | ((tw // 2) & 0xFFFF)
    user32.PostMessageW(target, 0x0200, 0, lparam)
    time.sleep(0.05)
    user32.PostMessageW(target, 0x0201, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(target, 0x0202, 0, lparam)
    for _ in range(30):
        if not user32.IsWindowVisible(popup_h):
            break
        time.sleep(0.05)
    time.sleep(0.25)


def open_checkout(pid, hwnd_main, search_text, qty):
    click_sidebar_section(hwnd_main, "Punto de Venta")
    time.sleep(0.3)
    vis = visible(hwnd_main)
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
    time.sleep(0.3)
    return hwnd_chk


def finish_sale(pid, hwnd_chk):
    btn = next(h for (h, c, t) in enum_children(hwnd_chk) if c.lower() == "button" and t.startswith("Confirmar Venta") and user32.IsWindowVisible(h))
    post_click_button(btn)
    hwnd_tkt, _ = wait_for_window(pid, "Venta Registrada", timeout=8.0)
    time.sleep(0.2)
    ticket_text = "\n".join(get_edit_text(h) for (h, c, _t) in enum_children(hwnd_tkt) if c.lower() == "edit")
    click_button(next(h for (h, c, t) in enum_children(hwnd_tkt) if c.lower() == "button" and "Ahora no" in t))
    time.sleep(0.3)
    return ticket_text


def open_ticket_history(pid, hwnd_main):
    click_sidebar_section(hwnd_main, "Punto de Venta")
    time.sleep(0.3)
    btn = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Más acciones" in t)
    user32.SetForegroundWindow(hwnd_main)
    time.sleep(0.2)
    post_click_button(btn)
    # menú nativo (#32768): Abajo + Enter eligen «Historial y reimpresión de tickets...»
    menu = 0
    for _ in range(40):
        menu = user32.FindWindowW("#32768", None)
        if menu and user32.IsWindowVisible(menu):
            break
        time.sleep(0.05)
    assert menu, "No se abrió el menú «Más acciones»"
    time.sleep(0.3)
    for vk in (VK_DOWN, VK_RETURN):
        user32.PostMessageW(menu, 0x0100, vk, 0)   # WM_KEYDOWN
        time.sleep(0.05)
        user32.PostMessageW(menu, 0x0101, vk, 0)   # WM_KEYUP
        time.sleep(0.25)
    hwnd_hist, _ = wait_for_window(pid, "Historial de Tickets", timeout=8.0)
    time.sleep(0.4)
    return hwnd_hist


def history_list(hwnd_hist):
    return next(h for (h, c, t) in visible(hwnd_hist) if c.lower() == "syslistview32")


def select_ticket(hwnd_hist, folio):
    lst = history_list(hwnd_hist)
    rows = read_list_rows(lst, 7)
    idx = next(i for i, r in enumerate(rows) if r[0] == folio)
    select_list_row(lst, idx, settle=0.25)
    return rows[idx]


def edit_below(hwnd, label_text):
    lab = next((h for (h, c, t) in visible(hwnd) if c.lower() == "static" and t.strip().startswith(label_text)), None)
    assert lab is not None, f"No se encontró la etiqueta '{label_text}'"
    lr = get_rect(lab)
    cands = [(get_rect(h), h) for (h, c, t) in visible(hwnd) if c.lower() == "edit" and get_rect(h)[1] >= lr[3] - 6 and abs(get_rect(h)[0] - lr[0]) < 40]
    assert cands, f"No hay campo bajo '{label_text}'"
    return min(cands, key=lambda x: x[0][1])[1]


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba T17 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)
    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t17_{label.lower()}_", ignore_cleanup_errors=True) as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"
        procs = []
        owner_user, owner_pass = "dueno_t17", "ClaveT17!2026"
        now_str = time.strftime("%Y-%m-%d %H:%M:%S")
        try:
            # 1. Alta del Dueño
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_init, _ = wait_for_window(p1.pid, "Primer Inicio", timeout=12.0)
            init_edits = sorted([(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_init) if c.lower() == "edit" and user32.IsWindowVisible(h)],
                                key=lambda x: x[1][1])
            for (h, _r), text in zip(init_edits, ["Dueno T17", owner_user, "dueno@t17.local", owner_pass, owner_pass]):
                set_edit_text(h, text)
            btn_init = next(h for (h, c, _t) in enum_children(hwnd_init) if c.lower() == "button" and user32.IsWindowVisible(h))
            click_button(btn_init)
            hwnd_m1, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio",), timeout=15.0)
            user32.PostMessageW(hwnd_m1, WM_CLOSE, 0, 0)
            p1.wait(timeout=8)

            # 2. Sembrado: dos productos, una receta y un cajero (sin pos.refund ni config.manage)
            con = sqlite3.connect(db_path)
            try:
                cur = con.cursor()
                cur.execute("INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) "
                            "VALUES ('ing-t17', 'Pan T17', 'MASS', 'mg', 100000, 0)")
                cur.executemany(
                    "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, is_combo, created_at) "
                    "VALUES (?, ?, NULL, ?, 'Pruebas', ?, ?, 1, 0, ?)",
                    [("prod-t17-a", "T17-A", "Pastel T17", 10000, 0, now_str), ("prod-t17-s", "T17-S", "Sandwich T17", 5000, 1, now_str)])
                cur.execute("INSERT INTO recipes (id, product_id, version, created_at) VALUES ('rcp-t17', 'prod-t17-s', 1, ?)", (now_str,))
                cur.execute("INSERT INTO recipe_items (recipe_id, ingredient_id, quantity_base_units) VALUES ('rcp-t17', 'ing-t17', 100)")
                cur.execute("INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at) "
                            "SELECT 'user-caj-t17', 'cajero_t17', 'Cajero T17', 'caj@t17.local', password_hash, 'role-cashier', 1, created_at "
                            "FROM users WHERE username = ? LIMIT 1", (owner_user,))
                con.commit()
            finally:
                con.close()

            # 3. Dueño: Configuración del cargo, turno y ventas
            p2, hwnd_main = login_window(exe_path, work_dir, env, owner_user, owner_pass)
            procs.append(p2)
            set_profile_touch(hwnd_main, False)

            for touch in (False, True):
                prof = "Táctil" if touch else "Estándar"
                tag = f"{label.lower()}_{'tactil' if touch else 'estandar'}"
                print(f"\n--- Perfil {prof} ---", flush=True)
                set_profile_touch(hwnd_main, touch)

                # --- Configuración del cargo por tarjeta
                click_sidebar_section(hwnd_main, "BackOffice")
                click_subnav(hwnd_main, "Configuración")
                time.sleep(0.5)
                assert any(t == "Cobro con tarjeta (Mercado Pago)" for t in statics_of(hwnd_main)), "Falta la tarjeta «Cobro con tarjeta (Mercado Pago)»"
                pct = edit_below(hwnd_main, "Porcentaje")
                fixed = edit_below(hwnd_main, "Monto fijo")
                btn_save = next(h for (h, c, t) in visible(hwnd_main) if c.lower() == "button" and t == "Guardar")
                if not touch:
                    # texto inválido y fuera de rango: mensajes propios y del servicio
                    set_edit_text(pct, "abc")
                    click_button(btn_save)
                    time.sleep(0.3)
                    assert "Escribe el porcentaje con números y hasta dos decimales" in joined(hwnd_main), "Falta el mensaje de porcentaje inválido"
                    set_edit_text(pct, "25")
                    click_button(btn_save)
                    time.sleep(0.3)
                    assert "El porcentaje del cargo por tarjeta debe estar entre 0 % y 20.00 %." in joined(hwnd_main), "Falta el mensaje del servicio"
                    assert db_one(db_path, "SELECT card_surcharge_bp FROM payment_settings") == 0, "Un valor inválido no se guarda"
                    set_edit_text(pct, "4.06")
                    set_edit_text(fixed, "4.00")
                    time.sleep(0.3)
                    assert "En una venta de $100.00 se cobrarían $8.06." in joined(hwnd_main), f"Ejemplo en vivo: {joined(hwnd_main)}"
                    click_button(btn_save)
                    time.sleep(0.4)
                    assert "Cambios guardados." in joined(hwnd_main)
                    assert db_rows(db_path, "SELECT card_surcharge_bp, card_surcharge_fixed_cents FROM payment_settings")[0] == (406, 400), "4.06 % + $4.00 -> 406 y 400"
                else:
                    assert get_edit_text(pct) == "4.06" and get_edit_text(fixed) == "4.00", "La configuración guardada se carga al abrir"
                time.sleep(0.2)
                os.makedirs(QA_DIR, exist_ok=True)
                capture_window_img(hwnd_main, os.path.join(QA_DIR, f"{tag}_configuracion.png"))

                if not touch:
                    click_sidebar_section(hwnd_main, "Caja y Reportes")
                    click_subnav(hwnd_main, "Caja y turnos")
                    time.sleep(0.3)
                    open_edit = [h for (h, c, _t) in visible(hwnd_main) if c.lower() == "edit"][0]
                    set_edit_text(open_edit, "500.00")
                    btn_open = next(h for (h, c, t) in visible(hwnd_main) if c.lower() == "button" and "Abrir Turno de Caja" in t)
                    post_click_button(btn_open)
                    wait_messagebox_and_read(p2.pid, "Turno Abierto", "Aceptar")

                # --- Cobro con tarjeta: desglose y total con cargo
                hwnd_chk = open_checkout(p2.pid, hwnd_main, "Pastel T17", 1)
                choose_method(p2.pid, hwnd_chk, "Tarjeta")
                txt = joined(hwnd_chk)
                assert "Cargo por pago con tarjeta (4.06 % + $4.00)" in txt, f"Falta el cargo en el cobro: {txt}"
                assert "$8.06" in txt and "$108.06" in txt, f"Importes del cobro con tarjeta: {txt}"
                btn_conf = find_button(hwnd_chk, "Confirmar Venta")
                assert btn_conf and "$108.06" in next(t for (h, c, t) in visible(hwnd_chk) if h == btn_conf), "El botón muestra el total con cargo"
                assert_dialog_ok(hwnd_chk, f"Cobro con tarjeta ({prof})", f"{tag}_cobro_tarjeta.png")
                ticket_text = finish_sale(p2.pid, hwnd_chk)
                assert "Cargo por pago con tarjeta" in ticket_text and "$108.06" in ticket_text, f"Ticket con cargo: {ticket_text}"
                card_folio = last_folio(db_path)
                assert db_one(db_path, "SELECT amount_cents FROM payment_records WHERE sale_id = (SELECT id FROM sales WHERE folio = ?)", (card_folio,)) == 10806
                assert db_one(db_path, "SELECT card_surcharge_cents FROM sales WHERE folio = ?", (card_folio,)) == 806

                # --- Cobro en efectivo: sin cargo
                hwnd_chk = open_checkout(p2.pid, hwnd_main, "Pastel T17", 1)
                txt = joined(hwnd_chk)
                assert "Cargo por pago con tarjeta" not in txt and "$100.00" in txt, f"El efectivo no lleva cargo: {txt}"
                assert_dialog_ok(hwnd_chk, f"Cobro en efectivo ({prof})", f"{tag}_cobro_efectivo.png")
                finish_sale(p2.pid, hwnd_chk)
                assert db_one(db_path, "SELECT card_surcharge_cents FROM sales WHERE folio = ?", (last_folio(db_path),)) == 0

                # --- Venta de un preparado para reembolsar en efectivo
                hwnd_chk = open_checkout(p2.pid, hwnd_main, "Sandwich T17", 3)
                finish_sale(p2.pid, hwnd_chk)
                sandwich_folio = last_folio(db_path)
                cash_before = db_one(db_path, "SELECT COALESCE(SUM(amount_cents), 0) FROM refunds WHERE payment_method = 'EFECTIVO'")
                refunds_before = db_one(db_path, "SELECT COUNT(*) FROM refunds")

                # --- Historial: columna y botón
                hwnd_hist = open_ticket_history(p2.pid, hwnd_main)
                heads = read_list_headers(history_list(hwnd_hist))
                assert "Reembolsado" in heads, f"Falta la columna «Reembolsado»: {heads}"
                btn_refund = find_button(hwnd_hist, "Reembolsar", exact=True)
                assert btn_refund is not None, "El Dueño ve el botón «Reembolsar»"
                assert not user32.IsWindowEnabled(btn_refund), "«Reembolsar» deshabilitado sin ticket elegido"
                select_ticket(hwnd_hist, sandwich_folio)
                time.sleep(0.2)
                assert user32.IsWindowEnabled(btn_refund), "«Reembolsar» habilitado con un ticket elegido"
                assert_dialog_ok(hwnd_hist, f"Historial ({prof})", f"{tag}_historial.png")
                post_click_button(btn_refund)
                hwnd_ref, _ = wait_for_window(p2.pid, "Reembolso de venta", timeout=8.0)
                time.sleep(0.4)
                dw, dh = assert_dialog_ok(hwnd_ref, f"Reembolso ({prof})", f"{tag}_reembolso.png")
                print(f"  [OK] Diálogo de reembolso {prof}: {dw}x{dh} px", flush=True)

                edits = sorted([(get_rect(h), h) for (h, c, t) in visible(hwnd_ref) if c.lower() == "edit"], key=lambda x: (x[0][1], x[0][0]))
                qty_edit, reason_edit = edits[0][1], edits[-1][1]
                chk = find_button(hwnd_ref, "Ya se preparó", exact=True)
                assert chk is not None and send_msg(chk, BM_GETCHECK, 0, 0) == 1, "«Ya se preparó» viene marcada"
                txt = joined(hwnd_ref)
                assert "Los ingredientes" in txt or "ingredientes" in txt, "La línea de ayuda explica el efecto de desmarcar"
                btn_do = find_button(hwnd_ref, "Reembolsar", exact=True)
                assert not user32.IsWindowEnabled(btn_do), "Sin piezas elegidas no se puede reembolsar"
                set_edit_text(qty_edit, "2")
                time.sleep(0.4)
                txt = joined(hwnd_ref)
                assert "Importe a devolver: $100.00" in txt, f"Importe en vivo: {txt}"
                assert "Se devuelve en efectivo de la caja." in txt
                # motivo vacío: mensaje del servicio y nada escrito
                post_click_button(btn_do)
                time.sleep(0.5)
                assert "Escribe el motivo del reembolso (de 3 a 200 caracteres)." in joined(hwnd_ref), "Motivo vacío: mensaje del servicio"
                assert db_one(db_path, "SELECT COUNT(*) FROM refunds") == refunds_before, "El motivo vacío no escribe nada"
                set_edit_text(reason_edit, "Pedido equivocado")
                time.sleep(0.3)
                # doble clic en confirmar: un solo reembolso
                user32.PostMessageW(btn_do, 0x00F5, 0, 0)
                user32.PostMessageW(btn_do, 0x00F5, 0, 0)
                wait_messagebox_and_read(p2.pid, "Confirmar reembolso", "Sí", timeout=8.0)
                # (el cuadro nativo no expone su texto; el folio y el importe se comprueban en el comprobante y en SQLite)
                wait_messagebox_and_read(p2.pid, "Reembolso registrado", "Aceptar", timeout=8.0)
                hwnd_rcp = find_window_with_button(p2.pid, "Ahora no")
                rcp = "\n".join(get_edit_text(h) for (h, c, _t) in enum_children(hwnd_rcp) if c.lower() == "edit")
                assert "COMPROBANTE DE REEMBOLSO" in rcp and "TOTAL REEMBOLSADO:" in rcp and sandwich_folio + "-R1" in rcp, f"Comprobante: {rcp}"
                click_button(find_button(hwnd_rcp, "Ahora no", exact=True))
                time.sleep(0.5)
                time.sleep(0.4)
                assert db_one(db_path, "SELECT COUNT(*) FROM refunds") == refunds_before + 1, "Un doble clic no duplica el reembolso"
                assert db_one(db_path, "SELECT amount_cents FROM refunds WHERE folio = ?", (sandwich_folio + "-R1",)) == 10000
                assert db_one(db_path, "SELECT COUNT(*) FROM stock_movements WHERE movement_type = 'WASTE' AND reason LIKE ?", (f"Reembolso {sandwich_folio}%",)) == 1, "Ya preparado: merma"
                # historial recargado con «Reembolsado»
                row = next(r for r in read_list_rows(history_list(hwnd_hist), 7) if r[0] == sandwich_folio)
                assert row[3] == "$100.00", f"Columna «Reembolsado»: {row}"
                # reembolso con tarjeta: el cargo no se devuelve
                select_ticket(hwnd_hist, card_folio)
                post_click_button(find_button(hwnd_hist, "Reembolsar", exact=True))
                hwnd_ref2, _ = wait_for_window(p2.pid, "Reembolso de venta", timeout=8.0)
                time.sleep(0.4)
                edits2 = sorted([(get_rect(h), h) for (h, c, t) in visible(hwnd_ref2) if c.lower() == "edit"], key=lambda x: (x[0][1], x[0][0]))
                set_edit_text(edits2[0][1], "1")
                set_edit_text(edits2[-1][1], "Cliente cambió de opinión")
                time.sleep(0.4)
                txt2 = joined(hwnd_ref2)
                assert "Devolver por Mercado Pago; aquí solo se registra." in txt2, f"Método tarjeta: {txt2}"
                assert "El cargo por tarjeta ($8.06) no se devuelve." in txt2, f"Aviso del cargo: {txt2}"
                assert "Importe a devolver: $100.00" in txt2
                assert_dialog_ok(hwnd_ref2, f"Reembolso con tarjeta ({prof})", f"{tag}_reembolso_tarjeta.png")
                post_click_button(find_button(hwnd_ref2, "Reembolsar", exact=True))
                wait_messagebox_and_read(p2.pid, "Confirmar reembolso", "Sí", timeout=8.0)
                wait_messagebox_and_read(p2.pid, "Reembolso registrado", "Aceptar", timeout=8.0)
                hwnd_rcp2 = find_window_with_button(p2.pid, "Ahora no")
                rcp2 = "\n".join(get_edit_text(h) for (h, c, _t) in enum_children(hwnd_rcp2) if c.lower() == "edit")
                assert "no se devuelve" in rcp2 or "se devuelve" in rcp2, f"Comprobante con tarjeta: {rcp2}"
                click_button(find_button(hwnd_rcp2, "Ahora no", exact=True))
                time.sleep(0.5)
                assert db_one(db_path, "SELECT payment_method FROM refunds WHERE folio = ?", (card_folio + "-R1",)) == "TARJETA"
                assert db_one(db_path, "SELECT shift_id FROM refunds WHERE folio = ?", (card_folio + "-R1",)) is not None
                click_button(find_button(hwnd_hist, "Cerrar", exact=True))
                time.sleep(0.4)

                # --- El corte descuenta los reembolsos en efectivo
                click_sidebar_section(hwnd_main, "Caja y Reportes")
                click_subnav(hwnd_main, "Caja y turnos")
                time.sleep(0.5)
                cash_refunds = db_one(db_path, "SELECT COALESCE(SUM(amount_cents), 0) FROM refunds WHERE payment_method = 'EFECTIVO'")
                cash_sales = db_one(db_path, "SELECT COALESCE(SUM(p.amount_cents), 0) FROM payment_records p WHERE p.payment_method = 'EFECTIVO'")
                expected = 50000 + cash_sales - cash_refunds
                assert any(money(expected) in t for t in statics_of(hwnd_main)), f"El corte debe mostrar {money(expected)} de efectivo esperado: {statics_of(hwnd_main)}"
                assert cash_refunds == cash_before + 10000, "Se registró un reembolso en efectivo de $100.00"

            # 4. Cajero sin pos.refund: no ve el botón
            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p2.wait(timeout=10)
            p3, hwnd_c = login_window(exe_path, work_dir, env, "cajero_t17", owner_pass)
            procs.append(p3)
            hwnd_hist_c = open_ticket_history(p3.pid, hwnd_c)
            assert find_button(hwnd_hist_c, "Reembolsar", exact=True) is None, "El cajero no debe ver «Reembolsar»"
            assert "Reembolsado" in read_list_headers(history_list(hwnd_hist_c)), "La columna sí se muestra"
            click_button(find_button(hwnd_hist_c, "Cerrar", exact=True))
            time.sleep(0.3)
            click_sidebar_section(hwnd_c, "BackOffice") if any(t == "BackOffice" for (_h, _c, t) in visible(hwnd_c)) else None
            print(f"[OK] {label}: Configuración, cobro con cargo, reembolsos y permisos verificados en ambos perfiles.", flush=True)
            user32.PostMessageW(hwnd_c, WM_CLOSE, 0, 0)
            p3.wait(timeout=10)
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
    if "--release-only" in sys.argv:
        print("\n[EXITO] Prueba T17 superada en Release (sin Debug por --release-only).", flush=True)
        return
    ensure_no_app_running()
    run_suite("Debug", DEBUG_EXE)
    print("\n[EXITO TOTAL] Prueba T17 superada en Release y Debug.", flush=True)


if __name__ == "__main__":
    main()
