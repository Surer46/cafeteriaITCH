#!/usr/bin/env python3
"""
test_f7_ingredients_panel.py - Prueba GUI real para Bloque F7 (Inventario > Ingredientes).

Verifica Release y Debug con ejecutables nativos y bases SQLite aisladas:
- Escalado de pantalla exacto al 100% (96 DPI) antes de correr pruebas GUI.
- Estado vacio inicial invitando a crear el primer ingrediente.
- Alta de Peso (mg), Volumen (ml) y Piezas (pz), validando valores exactos en SQLite (500 g -> 500000).
- Edicion de nombre y umbral con persistencia tras cerrar y reabrir.
- Rechazo de nombre vacio, desbordamiento y nombre duplicado mostrando el mensaje del servicio.
- Formateo de existencias sembradas por SQL (1.5 kg, 250 g, 2.5 L, 7 pz, 450 mg, Sin aviso) y estados
  Poco inventario / Suficiente segun get_stock_alerts().
- Usuario Cajero en solo lectura, controles deshabilitados e intento de guardado sin alterar SQLite.
- Ausencia de controles para modificar existencias y de jerga tecnica (MASS, VOLUME, COUNT, IDs, mg salvo < 1000 mg).
- Medicion geometrica Win32 en Estandar y Touch a 1200x720 y 960x600: encabezados completos, sin WS_HSCROLL,
  holgura <= 8 px, sin recortes ni solapamientos, campos centrados (<= 3 px), alturas Touch (>= 48 px campos/botones,
  >= 40 px filas de lista) y Guardar visible sin desplazarse.
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
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_dpi,
    get_window_scale,
    format_dpi_info,
    to_px,
    min_px,
    max_px,
    move_window_logical,
    ensure_no_app_running,
    send_msg,
    get_text,
    get_class,
    get_rect,
    get_client_size,
    find_windows_by_pid,
    wait_for_window,
    enum_children,
    click_button,
    set_edit_text,
    get_list_item_rect,
    measure_text_extent,
    capture_window_pixels,
    is_descendant,
    WM_CLOSE,
    WM_COMMAND,
    GWL_STYLE,
    WS_HSCROLL,
    LVM_GETITEMCOUNT,
    LVM_GETITEMRECT,
    LVM_SETITEMSTATE,
    HDM_GETITEMCOUNT,
    LVIS_FOCUSED,
    LVIS_SELECTED,
    LVIF_STATE,
    PROCESS_VM_OPERATION,
    PROCESS_VM_READ,
    PROCESS_VM_WRITE,
    MEM_COMMIT,
    MEM_RESERVE,
    MEM_RELEASE,
    PAGE_READWRITE,
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


def select_combo_by_text(hwnd_combo, target_text):
    count = send_msg(hwnd_combo, CB_GETCOUNT, 0, 0)
    chosen = -1
    for i in range(count):
        ln = send_msg(hwnd_combo, CB_GETLBTEXTLEN, i, 0)
        buf = ctypes.create_unicode_buffer(ln + 2)
        send_msg(hwnd_combo, CB_GETLBTEXT, i, ctypes.cast(buf, ctypes.c_void_p).value)
        if buf.value == target_text:
            chosen = i
            break
    if chosen < 0:
        raise AssertionError(f"Opcion '{target_text}' no encontrada en ComboBox")
    send_msg(hwnd_combo, CB_SETCURSEL, chosen, 0)
    parent = user32.GetParent(hwnd_combo)
    ctrl_id = user32.GetDlgCtrlID(hwnd_combo)
    wparam_chg = (CBN_SELCHANGE << 16) | (ctrl_id & 0xFFFF)
    send_msg(parent, WM_COMMAND, wparam_chg, hwnd_combo)
    wparam_ok = (CBN_SELENDOK << 16) | (ctrl_id & 0xFFFF)
    send_msg(parent, WM_COMMAND, wparam_ok, hwnd_combo)
    time.sleep(0.15)


def list_headers(hwnd_list):
    hwnd_hdr = send_msg(hwnd_list, LVM_GETHEADER, 0, 0)
    if not hwnd_hdr:
        return [], 0
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    if not hproc:
        raise OSError("OpenProcess fallo en list_headers")
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


def read_list_rows(hwnd_list, num_cols=4):
    count = send_msg(hwnd_list, LVM_GETITEMCOUNT, 0, 0)
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid.value)
    if not hproc:
        raise OSError("OpenProcess fallo en read_list_rows")
    rows = []
    try:
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(LVITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        rem_buf = kernel32.VirtualAllocEx(hproc, None, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            for r in range(count):
                row_vals = []
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
                    row_vals.append(out.value)
                rows.append(tuple(row_vals))
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
            kernel32.VirtualFreeEx(hproc, rem_buf, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)
    return rows


def select_list_row(hwnd_list, index):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_list, ctypes.byref(pid))
    hproc = kernel32.OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE, False, pid.value)
    try:
        rem_item = kernel32.VirtualAllocEx(hproc, None, ctypes.sizeof(LVITEMW), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        try:
            lvi = LVITEMW()
            lvi.mask = LVIF_STATE
            lvi.state = LVIS_SELECTED | LVIS_FOCUSED
            lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED
            written = ctypes.c_size_t()
            kernel32.WriteProcessMemory(hproc, rem_item, ctypes.byref(lvi), ctypes.sizeof(lvi), ctypes.byref(written))
            send_msg(hwnd_list, LVM_SETITEMSTATE, index, rem_item)
        finally:
            kernel32.VirtualFreeEx(hproc, rem_item, 0, MEM_RELEASE)
    finally:
        kernel32.CloseHandle(hproc)


def measure_ink_vertical_center(hwnd):
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
    x1 = max(x0, w - to_px(18, scale))
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


def locate_ingredients_controls(hwnd_main):
    children = enum_children(hwnd_main)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]

    btn_new = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Nuevo ingrediente"), None)
    btn_refresh = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Actualizar"), None)
    btn_save = next((h for (h, c, t) in visible if c.lower() == "button" and t == "Guardar"), None)
    assert btn_new is not None and btn_refresh is not None and btn_save is not None, "Faltan botones principales"

    panel_hwnd = user32.GetParent(btn_new)
    panel_buttons = {t for (h, c, t) in children if c.lower() == "button" and is_descendant(h, panel_hwnd)}
    assert panel_buttons == {"Nuevo ingrediente", "Actualizar", "Guardar"}, (
        f"Botones inesperados en Ingredientes (no debe existir control de existencias): {panel_buttons}"
    )
    save_rect = get_rect(btn_save)
    panel_edits = sorted(
        [(h, get_rect(h)) for (h, c, _t) in visible if c.lower() == "edit" and is_descendant(h, panel_hwnd)],
        key=lambda item: item[1][1]
    )
    assert len(panel_edits) == 2, f"Se esperaban exactamente 2 Edit (Nombre y Aviso), encontrados {len(panel_edits)}"
    txt_name = panel_edits[0][0]
    txt_threshold = panel_edits[1][0]

    combos = [h for (h, c, _t) in visible if c.lower() == "combobox" and is_descendant(h, panel_hwnd)]
    lists = [h for (h, c, _t) in children if c.lower() == "syslistview32" and is_descendant(h, panel_hwnd)]
    assert len(lists) == 1, f"Se esperaba 1 SysListView32 de ingredientes, encontrados {len(lists)}"

    return {
        "panel": panel_hwnd,
        "btn_new": btn_new,
        "btn_refresh": btn_refresh,
        "btn_save": btn_save,
        "txt_name": txt_name,
        "txt_threshold": txt_threshold,
        "combo": combos[0] if combos else None,
        "list": lists[0],
        "visible": visible,
        "save_rect": save_rect,
    }


def open_ingredients_screen(hwnd_main):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_inv = next((h for (h, t) in btns if "Inventario" in t), None)
    assert btn_inv is not None, "No se encontro boton Inventario en sidebar"
    click_button(btn_inv)
    time.sleep(0.35)
    btns2 = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    btn_ing = next((h for (h, t) in btns2 if t == "Ingredientes"), None)
    assert btn_ing is not None, "No se encontro subseccion Ingredientes"
    click_button(btn_ing)
    time.sleep(0.45)


def set_ui_profile(hwnd_main, want_touch):
    scale = get_window_scale(hwnd_main)
    min_touch_btn_h = min_px(48, scale)
    ctrl = locate_ingredients_controls(hwnd_main)
    is_currently_touch = (get_rect(ctrl["btn_new"])[3] - get_rect(ctrl["btn_new"])[1]) >= min_touch_btn_h
    if is_currently_touch == want_touch:
        return
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    toggle = next((h for (h, t) in btns if "Touch" in t or "Estándar" in t), None)
    assert toggle is not None, "No se encontro boton de cambio de perfil Touch/Estandar"
    click_button(toggle)
    time.sleep(0.5)
    open_ingredients_screen(hwnd_main)
    ctrl2 = locate_ingredients_controls(hwnd_main)
    now_touch = (get_rect(ctrl2["btn_new"])[3] - get_rect(ctrl2["btn_new"])[1]) >= min_touch_btn_h
    assert now_touch == want_touch, f"Cambio de perfil a touch={want_touch} no surtio efecto"


def assert_no_jargon(hwnd_main, allowed_mg_stocks):
    scale = get_window_scale(hwnd_main)
    margin = to_px(2, scale)
    ctrl = locate_ingredients_controls(hwnd_main)
    p_rect = get_rect(ctrl["panel"])
    texts = []
    for (h, _c, t) in ctrl["visible"]:
        r = get_rect(h)
        if r[0] >= p_rect[0] - margin and r[1] >= p_rect[1] - margin and r[2] <= p_rect[2] + margin and r[3] <= p_rect[3] + margin:
            if t:
                texts.append(("ctrl", t))
    headers, _ = list_headers(ctrl["list"])
    for hdr in headers:
        texts.append(("header", hdr))
    if user32.IsWindowVisible(ctrl["list"]):
        rows = read_list_rows(ctrl["list"], 4)
        for row in rows:
            texts.append(("cell_name", row[0]))
            texts.append(("cell_stock", row[1]))
            texts.append(("cell_thresh", row[2]))
            texts.append(("cell_state", row[3]))

    forbidden = ["MASS", "VOLUME", "COUNT", "unidad base", "inventory.", "ing-", "f7-", "seed-"]
    for kind, txt in texts:
        for bad in forbidden:
            assert bad not in txt, f"Jerga tecnica prohibida '{bad}' en {kind}: '{txt}'"
        if re.search(r"\bmg\b", txt):
            assert kind == "cell_stock" and txt in allowed_mg_stocks, (
                f"'mg' suelto prohibido fuera de existencia < 1000 mg en {kind}: '{txt}'"
            )


def assert_geometry(hwnd_main, touch_mode, width, height):
    scale = move_window_logical(hwnd_main, 20, 20, width, height)
    time.sleep(0.6)
    ctrl = locate_ingredients_controls(hwnd_main)
    panel = ctrl["panel"]
    list_hwnd = ctrl["list"]
    p_rect = get_rect(panel)
    main_rc = get_rect(hwnd_main)

    # Guardar visible sin desplazarse
    sr = get_rect(ctrl["btn_save"])
    assert sr[2] <= p_rect[2] and sr[3] <= p_rect[3], f"Guardar se sale del panel: save={sr}, panel={p_rect}"
    assert sr[2] <= main_rc[2] and sr[3] <= main_rc[3], f"Guardar se sale de la ventana: save={sr}, main={main_rc}"

    list_cw = None
    col_widths = None
    row_h = None
    max_slack_px = max_px(8, scale)
    min_row_px = min_px(40, scale)
    min_touch_px = min_px(48, scale)
    # ListView columnas y scroll horizontal
    if user32.IsWindowVisible(list_hwnd):
        headers, hdr_hwnd = list_headers(list_hwnd)
        assert headers == ["Nombre", "Existencia", "Avisar con menos de", "Estado"], f"Encabezados incorrectos: {headers}"
        list_cw, _ = get_client_size(list_hwnd)
        col_widths = [send_msg(list_hwnd, LVM_GETCOLUMNWIDTH, i, 0) for i in range(4)]
        for i, title in enumerate(headers):
            hw, _ = measure_text_extent(hdr_hwnd, title)
            assert col_widths[i] >= hw, f"Columna {i} ('{title}') recortada: width={col_widths[i]} < hdr={hw}"
        col_sum = sum(col_widths)
        assert col_sum <= list_cw, f"Suma columnas {col_sum} > ancho cliente {list_cw}"
        assert (list_cw - col_sum) <= max_slack_px, f"Holgura excesiva: {list_cw - col_sum} px (> {max_slack_px} px)"
        style = user32.GetWindowLongW(list_hwnd, GWL_STYLE)
        assert (style & WS_HSCROLL) == 0, f"Lista con WS_HSCROLL en {width}x{height} touch={touch_mode}"

        row_h = None
        if touch_mode and send_msg(list_hwnd, LVM_GETITEMCOUNT, 0, 0) > 0:
            irc = get_list_item_rect(list_hwnd, 0)
            row_h = irc[3] - irc[1]
            assert row_h >= min_row_px, f"Altura fila Touch insuficiente: {row_h} px (< {min_row_px} px)"
        elif send_msg(list_hwnd, LVM_GETITEMCOUNT, 0, 0) > 0:
            irc = get_list_item_rect(list_hwnd, 0)
            row_h = irc[3] - irc[1]

    # Alturas Touch y centrado vertical
    wrap_name = user32.GetParent(ctrl["txt_name"])
    wrap_thresh = user32.GetParent(ctrl["txt_threshold"])
    if touch_mode:
        for label, h_ctrl in [
            ("btn_new", ctrl["btn_new"]),
            ("btn_refresh", ctrl["btn_refresh"]),
            ("btn_save", ctrl["btn_save"]),
            ("txt_name_wrap", wrap_name),
            ("txt_threshold_wrap", wrap_thresh),
        ]:
            r = get_rect(h_ctrl)
            assert (r[3] - r[1]) >= min_touch_px, f"Control Touch '{label}' mide {r[3] - r[1]} px (< {min_touch_px} px)"
        if ctrl["combo"] is not None:
            rc_c = get_rect(ctrl["combo"])
            assert (rc_c[3] - rc_c[1]) >= min_touch_px, f"ComboBox Touch mide {rc_c[3] - rc_c[1]} px (< {min_touch_px} px)"

    for name_lbl, ed_hwnd in [("txt_name", ctrl["txt_name"]), ("txt_threshold", ctrl["txt_threshold"])]:
        if get_text(ed_hwnd).strip():
            ink = measure_ink_vertical_center(user32.GetParent(ed_hwnd))
            if ink is not None:
                ink_mid, box_mid = ink
                assert abs(ink_mid - box_mid) <= 3.0 * scale, (
                    f"Texto no centrado en {name_lbl} ({width}x{height}): ink_mid={ink_mid:.1f}, box_mid={box_mid:.1f}"
                )

    # No recorte de texto en botones/etiquetas visibles ni solapamientos entre controles hermanos
    edge_tol = max_px(2, scale)
    leaves = []
    for (h, cls, txt) in ctrl["visible"]:
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

    return {
        "list_cw": list_cw if user32.IsWindowVisible(list_hwnd) else None,
        "col_widths": col_widths if user32.IsWindowVisible(list_hwnd) else None,
        "row_h": row_h if user32.IsWindowVisible(list_hwnd) else None,
        "btn_new_h": get_rect(ctrl["btn_new"])[3] - get_rect(ctrl["btn_new"])[1],
        "btn_save_h": get_rect(ctrl["btn_save"])[3] - get_rect(ctrl["btn_save"])[1],
        "txt_name_h": get_rect(wrap_name)[3] - get_rect(wrap_name)[1],
        "txt_threshold_h": get_rect(wrap_thresh)[3] - get_rect(wrap_thresh)[1],
        "combo_h": (get_rect(ctrl["combo"])[3] - get_rect(ctrl["combo"])[1]) if ctrl["combo"] is not None else None,
    }


def run_suite(label, exe_path):
    print(f"\n=== Ejecutando prueba F7 en {label}: {exe_path} ===", flush=True)
    assert os.path.exists(exe_path), f"No existe ejecutable: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_f7_{label.lower()}_") as data_dir:
        db_path = os.path.join(data_dir, "cafeteria.db")
        env = os.environ.copy()
        env["PATH"] = work_dir + ";" + env.get("PATH", "")
        env["CAFETERIA_DATA_DIR"] = data_dir

        procs = []
        owner_user = "dueno_f7"
        owner_pass = "ClaveDuenoF7!2026"

        try:
            # 1. Primer inicio: alta de Dueno
            p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p1)
            hwnd_first, _ = wait_for_window(p1.pid, "Primer Inicio")
            f_children = enum_children(hwnd_first)
            f_edits = [h for (h, c, _t) in f_children if c.lower() == "edit"]
            f_btns = [h for (h, c, _t) in f_children if c.lower() == "button"]
            assert len(f_edits) >= 5
            set_edit_text(f_edits[0], "Dueno Prueba F7")
            set_edit_text(f_edits[1], owner_user)
            set_edit_text(f_edits[2], "dueno_f7@cafeteria.local")
            set_edit_text(f_edits[3], owner_pass)
            set_edit_text(f_edits[4], owner_pass)
            click_button(f_btns[0])

            hwnd_main, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"))
            open_ingredients_screen(hwnd_main)

            # 2. Estado vacio inicial
            ctrl = locate_ingredients_controls(hwnd_main)
            assert not user32.IsWindowVisible(ctrl["list"]), "La lista debe ocultarse cuando no hay ingredientes"
            empty_txts = [t for (_h, c, t) in ctrl["visible"] if c.lower() == "static" and "Todavía no hay ingredientes" in t]
            assert empty_txts, "Debe mostrarse el mensaje de estado vacio invitando a crear el primero"
            assert_no_jargon(hwnd_main, {"Inicia en 0 al crearse"})

            # 3. Crear uno de cada tipo y validar SQLite
            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Harina de trigo")
            select_combo_by_text(ctrl["combo"], "Peso")
            set_edit_text(ctrl["txt_threshold"], "500")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Leche entera")
            select_combo_by_text(ctrl["combo"], "Volumen")
            unit_lbls = [t for (_h, c, t) in enum_children(ctrl["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and t == "mililitros"]
            assert unit_lbls, "Unidad debe cambiar a 'mililitros' al elegir Volumen"
            set_edit_text(ctrl["txt_threshold"], "1500")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Vaso mediano")
            select_combo_by_text(ctrl["combo"], "Piezas")
            unit_lbls_pz = [t for (_h, c, t) in enum_children(ctrl["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and t == "piezas"]
            assert unit_lbls_pz, "Unidad debe cambiar a 'piezas' al elegir Piezas"
            set_edit_text(ctrl["txt_threshold"], "20")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            con = sqlite3.connect(db_path)
            cur = con.cursor()
            cur.execute("SELECT name, unit_dimension, base_unit, low_stock_threshold, current_stock FROM ingredients ORDER BY name")
            db_rows = cur.fetchall()
            con.close()
            assert db_rows == [
                ("Harina de trigo", "MASS", "mg", 500000, 0),
                ("Leche entera", "VOLUME", "ml", 1500, 0),
                ("Vaso mediano", "COUNT", "pz", 20, 0),
            ], f"Filas SQLite inesperadas: {db_rows}"

            # 4. Editar nombre y umbral; ComboBox no editable; nombre vacio y repetido
            ctrl = locate_ingredients_controls(hwnd_main)
            rows = read_list_rows(ctrl["list"], 4)
            harina_idx = next(i for i, r in enumerate(rows) if r[0] == "Harina de trigo")
            select_list_row(ctrl["list"], harina_idx)
            time.sleep(0.3)
            ctrl = locate_ingredients_controls(hwnd_main)
            assert ctrl["combo"] is None, "Al editar, 'Como se mide' no debe quedar como ComboBox editable"

            set_edit_text(ctrl["txt_name"], "Harina refinada")
            set_edit_text(ctrl["txt_threshold"], "750")
            click_button(ctrl["btn_save"])
            time.sleep(0.35)

            # Nombre vacio en edicion
            set_edit_text(ctrl["txt_name"], "   ")
            click_button(ctrl["btn_save"])
            time.sleep(0.25)
            err_empty = [t for (_h, c, t) in enum_children(ctrl["panel"]) if c.lower() == "static" and "vacío" in t.lower()]
            assert err_empty, "Debe mostrar error al intentar guardar nombre vacio"

            # Nombre repetido al crear -> mensaje del servicio
            click_button(ctrl["btn_new"])
            time.sleep(0.25)
            ctrl = locate_ingredients_controls(hwnd_main)
            set_edit_text(ctrl["txt_name"], "Harina refinada")
            set_edit_text(ctrl["txt_threshold"], "100")
            click_button(ctrl["btn_save"])
            time.sleep(0.25)
            dup_msg = [t for (_h, c, t) in enum_children(ctrl["panel"]) if c.lower() == "static" and "Ya existe un ingrediente con ese nombre" in t]
            assert dup_msg, "Debe mostrarse el error exacto del servicio para nombre repetido"

            # Desbordamiento gramos -> mg
            set_edit_text(ctrl["txt_name"], "Azucar")
            set_edit_text(ctrl["txt_threshold"], "9999999999999999")
            click_button(ctrl["btn_save"])
            time.sleep(0.25)
            ovf_msg = [t for (_h, c, t) in enum_children(ctrl["panel"]) if c.lower() == "static" and "demasiado grande" in t]
            assert ovf_msg, "Debe mostrarse error claro por desbordamiento"

            # Cerrar y reabrir para verificar persistencia
            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            p1.wait(timeout=6)

            con = sqlite3.connect(db_path)
            cur = con.cursor()
            total_ing = cur.execute("SELECT COUNT(*) FROM ingredients").fetchone()[0]
            assert total_ing == 3, f"Errores de validacion no debieron guardar filas nuevas: COUNT={total_ing}"
            cur.execute("SELECT name, low_stock_threshold FROM ingredients WHERE unit_dimension='MASS'")
            mass_row = cur.fetchone()
            assert mass_row == ("Harina refinada", 750000), f"Persistencia fallida en SQLite: {mass_row}"
            # Sembrar casos requeridos de existencias y estados
            cur.executemany(
                "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) VALUES (?, ?, ?, ?, ?, ?)",
                [
                    ("seed-kg", "Cafe tostado", "MASS", "mg", 1500000, 1000000),
                    ("seed-g", "Canela molida", "MASS", "mg", 250000, 500000),
                    ("seed-vol", "Agua purificada", "VOLUME", "ml", 2500, 1000),
                    ("seed-pz", "Tapa plastica", "COUNT", "pz", 7, 10),
                    ("seed-zero", "Sal de grano", "MASS", "mg", 5000, 0),
                    ("seed-mg", "Azafran", "MASS", "mg", 450, 1000),
                ]
            )
            # Sembrar usuario Cajero con el mismo hash que Dueno para probar permisos
            cur.execute(
                """
                INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
                SELECT 'user-cajero-f7', 'cajero_f7', 'Cajero F7', 'cajero@cafeteria.local', password_hash, 'role-cashier', 1, created_at
                FROM users WHERE username = ? LIMIT 1
                """,
                (owner_user,)
            )
            con.commit()
            con.close()

            # 5. Reabrir como Dueno y comprobar existencias sembradas + geometria
            p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p2)
            hwnd_login, _ = wait_for_window(p2.pid, "Iniciar Sesión")
            l_children = enum_children(hwnd_login)
            l_edits = [h for (h, c, _t) in l_children if c.lower() == "edit"]
            l_btns = [h for (h, c, _t) in l_children if c.lower() == "button"]
            set_edit_text(l_edits[0], owner_user)
            set_edit_text(l_edits[1], owner_pass)
            click_button(l_btns[0])

            hwnd_main2, _ = wait_for_window(p2.pid, "Cafetería ITCH II", exclude=("Iniciar Sesión", "Primer Inicio"))
            open_ingredients_screen(hwnd_main2)
            ctrl = locate_ingredients_controls(hwnd_main2)
            ui_rows = {r[0]: r[1:] for r in read_list_rows(ctrl["list"], 4)}
            assert ui_rows["Harina refinada"] == ("0 mg", "750 g", "Poco inventario"), ui_rows["Harina refinada"]
            assert ui_rows["Cafe tostado"] == ("1.5 kg", "1 kg", "Suficiente"), ui_rows["Cafe tostado"]
            assert ui_rows["Canela molida"] == ("250 g", "500 g", "Poco inventario"), ui_rows["Canela molida"]
            assert ui_rows["Agua purificada"] == ("2.5 L", "1 L", "Suficiente"), ui_rows["Agua purificada"]
            assert ui_rows["Tapa plastica"] == ("7 pz", "10 pz", "Poco inventario"), ui_rows["Tapa plastica"]
            assert ui_rows["Sal de grano"] == ("5 g", "Sin aviso", "Suficiente"), ui_rows["Sal de grano"]
            assert ui_rows["Azafran"] == ("450 mg", "1 g", "Poco inventario"), ui_rows["Azafran"]
            assert_no_jargon(hwnd_main2, {"0 mg", "450 mg"})

            # Geometria en Estandar y Touch
            metrics = {}
            set_ui_profile(hwnd_main2, False)
            metrics[("Estandar", 1200, 720)] = assert_geometry(hwnd_main2, False, 1200, 720)
            metrics[("Estandar", 960, 600)] = assert_geometry(hwnd_main2, False, 960, 600)
            set_ui_profile(hwnd_main2, True)
            metrics[("Touch", 960, 600)] = assert_geometry(hwnd_main2, True, 960, 600)
            metrics[("Touch", 1200, 720)] = assert_geometry(hwnd_main2, True, 1200, 720)
            click_button(locate_ingredients_controls(hwnd_main2)["btn_new"])
            time.sleep(0.25)
            assert_geometry(hwnd_main2, True, 960, 600)

            user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
            p2.wait(timeout=6)

            # 6. Iniciar sesion como Cajero (sin permisos de inventario)
            p3 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            procs.append(p3)
            hwnd_login3, _ = wait_for_window(p3.pid, "Iniciar Sesión")
            l3_children = enum_children(hwnd_login3)
            l3_edits = [h for (h, c, _t) in l3_children if c.lower() == "edit"]
            l3_btns = [h for (h, c, _t) in l3_children if c.lower() == "button"]
            set_edit_text(l3_edits[0], "cajero_f7")
            set_edit_text(l3_edits[1], owner_pass)
            click_button(l3_btns[0])

            hwnd_main3, _ = wait_for_window(p3.pid, "Cafetería ITCH II", exclude=("Iniciar Sesión", "Primer Inicio"))
            open_ingredients_screen(hwnd_main3)
            ctrl_c = locate_ingredients_controls(hwnd_main3)
            assert not user32.IsWindowEnabled(ctrl_c["btn_new"]), "Nuevo ingrediente debe quedar deshabilitado para Cajero"
            assert not user32.IsWindowEnabled(ctrl_c["txt_name"]), "Nombre debe quedar deshabilitado para Cajero"
            assert not user32.IsWindowEnabled(ctrl_c["txt_threshold"]), "Aviso debe quedar deshabilitado para Cajero"
            assert not user32.IsWindowEnabled(ctrl_c["btn_save"]), "Guardar debe quedar deshabilitado para Cajero"
            ro_txts = [t for (_h, c, t) in enum_children(ctrl_c["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and "Solo lectura" in t]
            assert ro_txts, "Debe mostrarse aviso de solo lectura para Cajero"
            svc_err = [t for (_h, c, t) in enum_children(ctrl_c["panel"]) if user32.IsWindowVisible(_h) and c.lower() == "static" and "Permiso insuficiente" in t]
            assert svc_err, "Debe mostrarse el mensaje del servicio cuando list_ingredients falla"

            con = sqlite3.connect(db_path)
            before_snap = con.execute("SELECT id, name, low_stock_threshold, current_stock FROM ingredients ORDER BY id").fetchall()
            con.close()
            click_button(ctrl_c["btn_save"])
            time.sleep(0.2)
            con = sqlite3.connect(db_path)
            after_snap = con.execute("SELECT id, name, low_stock_threshold, current_stock FROM ingredients ORDER BY id").fetchall()
            con.close()
            assert before_snap == after_snap, "Un intento de guardar como Cajero no debe alterar SQLite"

            user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
            p3.wait(timeout=6)
            print(f"[OK] {label} completo. Metricas={metrics}", flush=True)
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
    print("=== PRUEBA F7 COMPLETA EXITOSA (Release y Debug) ===", flush=True)


if __name__ == "__main__":
    main()
