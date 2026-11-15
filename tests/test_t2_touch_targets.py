#!/usr/bin/env python3
"""
test_t2_touch_targets.py - Prueba GUI de vigilancia de objetivos tactiles y filas de listas (Bloque T2).

Verifica Release y Debug con ejecutables nativos y bases SQLite aisladas bajo cualquier escalado de Windows:
- Conciencia DPI por monitor (SetProcessDpiAwarenessContext(-4)) y umbrales escalados por GetDpiForWindow/96.
a) Inventario de objetivos tactiles (>= 48x48 logicos, sin solapamientos) en perfil Tactil a 1200x720 logicos,
   escrito en qa_visual/touch_audit.txt. La prueba FALLA ante cualquier control fuera de la lista de excepciones.
b) Recorrido ampliado por estados adicionales (carrito con linea seleccionada, turno abierto, bitacora con filas
   y desplegable abierto, usuarios seleccionado + dialogo nuevo usuario, roles seleccionado con interruptores,
   configuracion con ambos desplegables abiertos, ingredientes seleccionado + formulario nuevo, y dialogos de
   cobro, ticket e historial de tickets). Verifica tambien opciones >= 40 de alto al desplegar listas.
c) Filas de listas (LVM_GETITEMRECT): cada lista con filas mide >= 40 logicos por fila.
d) Excepciones explicitas: falla si aparece una violacion no listada o si una excepcion listada ya no se reproduce.
"""

import ctypes
from ctypes import wintypes
import os
import shutil
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
    WM_CLOSE,
    WM_KEYDOWN,
    WM_KEYUP,
    WM_LBUTTONDOWN,
    WM_LBUTTONUP,
    GWL_STYLE,
    WS_TABSTOP,
    WS_VSCROLL,
    ES_READONLY,
    LVM_GETITEMCOUNT,
    VK_ESCAPE,
    SCROLLINFO,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_dpi,
    get_window_scale,
    ensure_no_app_running,
    send_msg,
    get_text,
    get_class,
    get_rect,
    get_client_size,
    get_client_screen_rect,
    set_client_logical_size,
    find_windows_by_pid,
    wait_for_window,
    enum_children,
    set_edit_text,
    click_button,
    post_click_button,
    click_control_at,
    select_list_row,
    get_list_item_rect,
    rects_overlap,
    measure_text_extent,
    to_px,
    click_sidebar_section,
    click_subnav,
)

AUDIT_PATH = os.path.join(REPO_ROOT, "qa_visual", "touch_audit.txt")

winspool = ctypes.WinDLL("winspool.drv", use_last_error=True)

winspool.EnumPrintersW.argtypes = [
    wintypes.DWORD, wintypes.LPWSTR, wintypes.DWORD, ctypes.c_void_p,
    wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD)
]
winspool.EnumPrintersW.restype = wintypes.BOOL

# Anchos logicos de icono (en unidades 96 DPI, perfil Tactil) para cada boton con Theme::setup_button_icon.
# Margen aplicado por Theme::setup_button_icon en Tactil: FromDIP(10).
BUTTON_ICON_LOGICAL_WIDTHS_BY_STATE = {
    ("Configuración", "Estándar"): 20,
    ("Bitácora", "Buscar"): 18,
    ("Dialogo Historial", "Buscar"): 16,
    ("Roles y permisos", "Guardar"): 18,
}
BUTTON_ICON_LOGICAL_WIDTHS = {
    "Touch": 18,
    "Estándar": 18,
    "Estándar (Activa)": 20,
    "Táctil": 20,
    "Táctil (Activa)": 20,
    "Punto de Venta": 20,
    "Inventario": 20,
    "Caja y Reportes": 20,
    "BackOffice": 20,
    "Buscar": 20,
    "Agregar al Carrito": 22,
    "Editar cantidad": 20,
    "Editar nota": 20,
    "Quitar línea": 20,
    "Más acciones ▾": 20,
    "Cobrar": 26,
    "Cancelar pedido": 18,
    "Abrir Turno de Caja": 18,
    "Cerrar Turno de Caja": 18,
    "Nuevo Usuario": 18,
    "Cambiar Contraseña": 16,
    "Ver Avatar": 16,
    "Nuevo rol": 18,
    "Eliminar": 18,
    "Guardar Configuración": 18,
    "Confirmar Venta": 20,
    "Imprimir": 18,
    "Reimprimir": 18,
}
TOUCH_ICON_MARGIN_LOGICAL = 10

# Excepciones cerradas en T3: todos los controles tactiles deben cumplir >= 48 x 48 logicos.
KNOWN_EXCEPTIONS = {}


user32.GetScrollInfo.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.POINTER(SCROLLINFO)]
user32.GetScrollInfo.restype = wintypes.BOOL


def count_installed_printers():
    needed = wintypes.DWORD(0)
    count = wintypes.DWORD(0)
    winspool.EnumPrintersW(2 | 4, None, 2, None, 0, ctypes.byref(needed), ctypes.byref(count))
    if needed.value == 0:
        return 0
    buf = (ctypes.c_ubyte * needed.value)()
    if not winspool.EnumPrintersW(2 | 4, None, 2, ctypes.byref(buf), needed.value, ctypes.byref(needed), ctypes.byref(count)):
        return 0
    return int(count.value)


def _button_icon_extra_px(state_name, clean_label, scale):
    icon_log = BUTTON_ICON_LOGICAL_WIDTHS_BY_STATE.get(
        (state_name, clean_label),
        BUTTON_ICON_LOGICAL_WIDTHS.get(clean_label, 0),
    )
    if icon_log <= 0:
        return 0
    return to_px(icon_log, scale) + to_px(TOUCH_ICON_MARGIN_LOGICAL, scale)


def _measure_button_fit(hwnd_btn, raw_label, state_name, scale):
    cw, _ch = get_client_size(hwnd_btn)
    lines = [ln.strip() for ln in raw_label.splitlines() if ln.strip()]
    text_w = max((measure_text_extent(hwnd_btn, ln)[0] for ln in lines), default=0)
    icon_extra = _button_icon_extra_px(state_name, raw_label.strip(), scale)
    required_w = text_w + icon_extra
    return {
        "fits": required_w <= cw,
        "text_w": text_w,
        "icon_extra": icon_extra,
        "required_w": required_w,
        "client_w": cw,
    }


def audit_window_touch_targets(hwnd_root, state_name, scale):
    """Audita controles interactivos visibles+habilitados y filas de listas en hwnd_root."""
    children = enum_children(hwnd_root)
    visible_enabled = [
        (h, c, t) for (h, c, t) in children
        if user32.IsWindowVisible(h) and user32.IsWindowEnabled(h)
    ]

    child_count_by_parent = {}
    for h, _c, _t in children:
        p = user32.GetParent(h)
        child_count_by_parent[p] = child_count_by_parent.get(p, 0) + 1

    items = []
    seen = set()

    for h, c, t in visible_enabled:
        cl = c.lower()
        target_h = h
        kind = None
        label = t.strip()

        if cl == "button":
            kind = "button"
            if not label:
                label = "<button>"
        elif cl == "edit":
            style = user32.GetWindowLongW(h, GWL_STYLE)
            if (style & ES_READONLY) != 0:
                continue
            parent = user32.GetParent(h)
            if parent and parent != hwnd_root and child_count_by_parent.get(parent, 0) == 1:
                p_cls = get_class(parent).lower()
                if p_cls.startswith("wxwindow"):
                    target_h = parent
            kind = "edit"
            label = f"Edit:{label}" if label else "<edit>"
        elif cl == "combobox":
            kind = "combobox"
            label = f"Combo:{label}" if label else "<combobox>"
        elif cl.startswith("wxwindow"):
            style = user32.GetWindowLongW(h, GWL_STYLE)
            rc_tmp = get_rect(h)
            w_tmp = rc_tmp[2] - rc_tmp[0]
            h_tmp = rc_tmp[3] - rc_tmp[1]
            is_leaf = child_count_by_parent.get(h, 0) == 0
            if (style & WS_TABSTOP) != 0 and (is_leaf or (h_tmp < 80 * scale and w_tmp < 600 * scale)):
                kind = "switch/combo"
                label = label if label else f"<{c}>"

        if kind is None or target_h in seen:
            continue
        seen.add(target_h)

        rc = get_rect(target_h)
        w_log = (rc[2] - rc[0]) / scale
        h_log = (rc[3] - rc[1]) / scale
        small = (w_log < 48.0 - 0.5) or (h_log < 48.0 - 0.5)
        fit_info = _measure_button_fit(target_h, t, state_name, scale) if kind == "button" and t.strip() else None
        items.append({
            "hwnd": target_h,
            "parent": user32.GetParent(target_h),
            "rect": rc,
            "kind": kind,
            "label": label.replace("\n", " "),
            "w_log": round(w_log, 1),
            "h_log": round(h_log, 1),
            "small": small,
            "overlap": False,
            "fit": fit_info,
        })

    # Solapamientos entre pares hermanos dentro de los controles auditados
    overlaps = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            if items[i]["parent"] == items[j]["parent"] and rects_overlap(items[i]["rect"], items[j]["rect"]):
                overlaps.append((items[i]["label"], items[j]["label"]))
                items[i]["overlap"] = True
                items[j]["overlap"] = True

    # Medicion de altura de filas de todas las listas visibles con filas
    list_rows = []
    vis_lists = [h for (h, c, _t) in children if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
    for li, hl in enumerate(vis_lists):
        cnt = send_msg(hl, LVM_GETITEMCOUNT, 0, 0)
        if cnt > 0:
            ir = get_list_item_rect(hl, 0)
            row_h_log = round((ir[3] - ir[1]) / scale, 1)
            row_w_log = round((ir[2] - ir[0]) / scale, 1)
            list_rows.append({
                "label": f"SysListView32#{li}",
                "count": cnt,
                "w_log": row_w_log,
                "h_log": row_h_log,
                "small_row": row_h_log < (40.0 - 0.5),
            })

    return items, overlaps, list_rows


def audit_combo_dropdown(pid, hwnd_main, hwnd_combo, combo_label, expected_items, scale):
    """Abre un wxOwnerDrawnComboBox, mide la altura por opcion del popup (>= 40 logicos) y lo cierra."""
    before_wins = {h for (h, _t) in find_windows_by_pid(pid)}
    user32.SetForegroundWindow(hwnd_main)
    time.sleep(0.1)
    rc = get_rect(hwnd_combo)
    cw = rc[2] - rc[0]
    ch = rc[3] - rc[1]
    lp = ((ch // 2) << 16) | (max(to_px(4, scale), cw - to_px(12, scale)) & 0xFFFF)
    user32.PostMessageW(hwnd_combo, WM_LBUTTONDOWN, 1, lp)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_combo, WM_LBUTTONUP, 0, lp)

    popup_hwnd = None
    for _ in range(20):
        now_wins = [h for (h, _t) in find_windows_by_pid(pid) if h not in before_wins]
        if now_wins:
            popup_hwnd = now_wins[0]
            break
        time.sleep(0.08)

    assert popup_hwnd is not None, f"Popup no se abrio para combo '{combo_label}'"
    time.sleep(0.15)

    p_children = enum_children(popup_hwnd)
    target = next((h for (h, _c, _t) in p_children if user32.IsWindowVisible(h)), popup_hwnd)
    c_rc = get_client_screen_rect(target)
    client_h_log = (c_rc[3] - c_rc[1]) / scale
    style = user32.GetWindowLongW(target, GWL_STYLE)
    if (style & WS_VSCROLL) != 0:
        si = SCROLLINFO()
        si.cbSize = ctypes.sizeof(SCROLLINFO)
        si.fMask = 0x0017  # SIF_ALL
        if user32.GetScrollInfo(target, 1, ctypes.byref(si)) and si.nPage > 0:
            visible_rows = int(si.nPage)
        else:
            visible_rows = max(1, expected_items)
    else:
        visible_rows = max(1, expected_items)

    item_h_log = round(client_h_log / visible_rows, 1)

    # Cerrar popup limpiamente con ESC
    focus_h = user32.GetFocus() or target
    user32.PostMessageW(focus_h, WM_KEYDOWN, VK_ESCAPE, 0)
    user32.PostMessageW(focus_h, WM_KEYUP, VK_ESCAPE, 0)
    time.sleep(0.2)

    return {
        "label": combo_label,
        "popup_h_log": round(client_h_log, 1),
        "visible_rows": visible_rows,
        "item_h_log": item_h_log,
        "small_opt": item_h_log < (40.0 - 0.5),
    }


def run_suite(label, exe_path, write_audit=False):
    ensure_no_app_running()
    assert os.path.exists(exe_path), f"Ejecutable no encontrado: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    temp_dir = tempfile.mkdtemp(prefix=f"cafeteria_t2_{label}_")
    db_path = os.path.join(temp_dir, "cafeteria.db")
    cfg_path = os.path.join(temp_dir, "station_profile.cfg")

    # Iniciar directamente en perfil Tactil
    with open(cfg_path, "w", encoding="utf-8") as f_cfg:
        f_cfg.write("touch\n")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_dir

    procs = []
    audit_lines = []
    summary = []
    violations = {}  # (state, kind, label) -> details dict
    text_fit_failures = []

    def record_audit(state_name, items, overlaps, list_rows, popup_checks=None):
        fit_bad = [it for it in items if it.get("fit") and not it["fit"]["fits"]]
        for it in fit_bad:
            f = it["fit"]
            text_fit_failures.append(
                f"{state_name} | button | '{it['label']}' | "
                f"text_w={f['text_w']} + icon_margin={f['icon_extra']} = {f['required_w']} > client_w={f['client_w']}"
            )
        flagged_cnt = (
            sum(1 for it in items if it["small"] or it["overlap"])
            + sum(1 for lr in list_rows if lr["small_row"])
            + sum(1 for pc in (popup_checks or []) if pc["small_opt"])
        )
        summary.append((state_name, len(items), flagged_cnt))
        print(f"[AUDIT][{label}] {state_name}: controles={len(items)}, listas={len(list_rows)}, "
              f"popups={len(popup_checks or [])}, marcados={flagged_cnt}")

        audit_lines.append("")
        audit_lines.append(f"=== {state_name} (total={len(items)}, marcados={flagged_cnt}) ===")

        def _add_violation(k_kind, k_label, val):
            base = (state_name, k_kind, k_label)
            if base not in violations:
                violations[base] = val
            else:
                idx = 2
                while (state_name, k_kind, f"{k_label}#{idx}") in violations:
                    idx += 1
                violations[(state_name, k_kind, f"{k_label}#{idx}")] = val

        for it in items:
            flags = []
            if it["small"]:
                flags.append("<48")
                _add_violation(it["kind"], it["label"], f"{it['w_log']}x{it['h_log']}")
            if it["overlap"]:
                flags.append("SOLAPA")
                _add_violation("overlap", it["label"], f"{it['w_log']}x{it['h_log']}")
            mark = ",".join(flags) if flags else "OK"
            audit_lines.append(f"  {state_name} | {it['kind']} | {it['label']} | {it['w_log']}x{it['h_log']} | {mark}")

        for l1, l2 in overlaps:
            audit_lines.append(f"  [SOLAPAMIENTO] '{l1}' <-> '{l2}'")

        for lr in list_rows:
            mark = "<40_FILA" if lr["small_row"] else "OK"
            if lr["small_row"]:
                _add_violation("list_row", lr["label"], f"fila={lr['h_log']}")
            audit_lines.append(f"  {state_name} | list_row | {lr['label']} ({lr['count']} filas) | {lr['w_log']}x{lr['h_log']} | {mark}")

        for pc in (popup_checks or []):
            mark = "<40_OPCION" if pc["small_opt"] else "OK"
            if pc["small_opt"]:
                _add_violation("popup_option", pc["label"], f"opcion={pc['item_h_log']} (popup={pc['popup_h_log']}/{pc['visible_rows']})")
            audit_lines.append(
                f"  {state_name} | popup_option | {pc['label']} (rows={pc['visible_rows']}) | h_opt={pc['item_h_log']} (popup={pc['popup_h_log']}) | {mark}"
            )

    try:
        print(f"\n=== [{label}] Iniciando suite T2 ({exe_path}) ===")

        # 1) Alta Dueno para inicializar BD en perfil Tactil
        p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p1)
        hwnd_owner, _ = wait_for_window(p1.pid, "Primer Inicio")
        ctrls_o = [c for c in enum_children(hwnd_owner) if user32.IsWindowVisible(c[0])]
        o_edits = [h for (h, c, _t) in ctrls_o if c.lower() == "edit"]
        o_btn = next(h for (h, c, _t) in ctrls_o if c.lower() == "button")
        set_edit_text(o_edits[0], "Dueño T2 Prueba")
        set_edit_text(o_edits[1], "dueno_t2")
        set_edit_text(o_edits[2], "dueno_t2@itch.edu.mx")
        set_edit_text(o_edits[3], "ClaveSeguraT2!2026")
        set_edit_text(o_edits[4], "ClaveSeguraT2!2026")
        time.sleep(0.15)
        post_click_button(o_btn)

        hwnd_m1, _ = wait_for_window(p1.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        user32.PostMessageW(hwnd_m1, WM_CLOSE, 0, 0)
        p1.wait(timeout=6)

        # Sembrar producto e ingrediente de prueba en SQLite
        con = sqlite3.connect(db_path)
        con.execute(
            """
            INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
            VALUES ('prod-t2-01', 'T2-01', '750100009902', 'Cafe T2 Tactil', 'Bebidas', 4500, 0, 1, '2026-11-13 10:00:00')
            """
        )
        con.execute(
            """
            INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold)
            VALUES ('ing-t2-01', 'Limón T2', 'MASS', 'MG', 1000000, 200000)
            """
        )
        con.commit()
        con.close()

        # 2) Relanzar en Tactil e iniciar sesion como Dueno
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p2)
        hwnd_login, _ = wait_for_window(p2.pid, "Iniciar Sesión")
        l_ctrls = [c for c in enum_children(hwnd_login) if user32.IsWindowVisible(c[0])]
        l_edits = [h for (h, c, _t) in l_ctrls if c.lower() == "edit"]
        l_btn = next(h for (h, c, _t) in l_ctrls if c.lower() == "button")
        set_edit_text(l_edits[0], "dueno_t2")
        set_edit_text(l_edits[1], "ClaveSeguraT2!2026")
        time.sleep(0.15)
        post_click_button(l_btn)

        hwnd_main, _ = wait_for_window(p2.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        time.sleep(0.45)

        dpi = get_window_dpi(hwnd_main)
        scale = get_window_scale(hwnd_main)
        set_client_logical_size(hwnd_main, 1200, 720, x_log=16, y_log=16)
        time.sleep(0.5)

        audit_lines.append(f"DPI={dpi} escala={scale:.2f} (1200x720 logicos, perfil Tactil)")

        # --- 1. Venta (inicial) ---
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Venta", scale)
        record_audit("Venta", items, overlaps, lrows)

        # --- 1b. Venta (con linea del carrito seleccionada) ---
        btn_add = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Agregar al Carrito" in t), None)
        assert btn_add is not None, "Boton Agregar al Carrito no encontrado"
        click_button(btn_add)
        time.sleep(0.4)
        pos_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        assert pos_lists and send_msg(pos_lists[0], LVM_GETITEMCOUNT, 0, 0) >= 1
        select_list_row(pos_lists[0], 0)
        time.sleep(0.35)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Venta (con linea)", scale)
        record_audit("Venta (con linea)", items, overlaps, lrows)

        # --- 1c. Venta (teclado numérico visible) ---
        pos_edits = [
            (h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)
        ]
        txt_qty_h = min(pos_edits, key=lambda x: x[1][2] - x[1][0])[0]
        click_control_at(txt_qty_h)
        time.sleep(0.25)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Venta (teclado)", scale)
        assert any(it["label"] == "Listo" for it in items), "Teclado numérico no visible en Venta"
        record_audit("Venta (teclado)", items, overlaps, lrows)

        # --- 2. Caja y turnos (inicial / cerrado) ---
        click_sidebar_section(hwnd_main, "Caja y Reportes")
        click_subnav(hwnd_main, "Caja y turnos")
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Caja y turnos", scale)
        record_audit("Caja y turnos", items, overlaps, lrows)

        cash_edits_init = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
        assert cash_edits_init, "Campo fondo inicial no encontrado"
        click_control_at(cash_edits_init[0])
        time.sleep(0.25)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Caja y turnos (teclado)", scale)
        assert any(it["label"] == "Listo" for it in items), "Teclado numérico no visible en Caja inicial"
        record_audit("Caja y turnos (teclado)", items, overlaps, lrows)
        btn_listo_c = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Listo"), None)
        if btn_listo_c:
            click_button(btn_listo_c)
            time.sleep(0.2)

        # --- 2b. Caja y turnos (turno abierto) ---
        btn_open_shift = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno de Caja" in t), None)
        assert btn_open_shift is not None, "Boton Abrir Turno de Caja no encontrado"
        post_click_button(btn_open_shift)
        hwnd_mb_open, _ = wait_for_window(p2.pid, "Turno Abierto", timeout=8.0)
        mb_btn = next((h for (h, c, _t) in enum_children(hwnd_mb_open) if c.lower() == "button"), None)
        if mb_btn:
            click_button(mb_btn)
        else:
            user32.PostMessageW(hwnd_mb_open, WM_CLOSE, 0, 0)
        time.sleep(0.45)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Caja y turnos (abierto)", scale)
        record_audit("Caja y turnos (abierto)", items, overlaps, lrows)

        cash_edits_open = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
        assert cash_edits_open, "Campo arqueo final no encontrado"
        click_control_at(cash_edits_open[0])
        time.sleep(0.25)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Caja y turnos (abierto, teclado)", scale)
        assert any(it["label"] == "Listo" for it in items), "Teclado numérico no visible en Caja final"
        record_audit("Caja y turnos (abierto, teclado)", items, overlaps, lrows)

        # --- 3. Bitacora (con filas + desplegable abierta) ---
        click_subnav(hwnd_main, "Bitácora")
        time.sleep(0.4)
        con_ev = sqlite3.connect(db_path)
        ev_cnt = 1 + con_ev.execute("SELECT COUNT(DISTINCT event_type) FROM audit_events WHERE event_type != ''").fetchone()[0]
        con_ev.close()
        cb_audit = next((h for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h) and ("combobox" in c.lower() or t == "comboBox")), None)
        assert cb_audit is not None, "Desplegable de Bitacora no encontrado"
        pop_audit = audit_combo_dropdown(p2.pid, hwnd_main, cb_audit, "Bitacora:TipoEvento", ev_cnt, scale)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Bitácora", scale)
        record_audit("Bitácora", items, overlaps, lrows, [pop_audit])

        # --- 4. Usuarios y cuentas (inicial + usuario seleccionado + Nuevo Usuario) ---
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Usuarios y cuentas")
        time.sleep(0.35)
        u_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        assert u_lists and send_msg(u_lists[0], LVM_GETITEMCOUNT, 0, 0) >= 1
        select_list_row(u_lists[0], 0)
        time.sleep(0.35)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Usuarios y cuentas", scale)
        record_audit("Usuarios y cuentas", items, overlaps, lrows)

        btn_new_user = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Nuevo Usuario" in t), None)
        assert btn_new_user is not None, "Boton Nuevo Usuario no encontrado"
        post_click_button(btn_new_user)
        hwnd_new_user, _ = wait_for_window(p2.pid, "Nuevo Usuario de Sistema", timeout=8.0)
        time.sleep(0.25)
        items_nu, overlaps_nu, lrows_nu = audit_window_touch_targets(hwnd_new_user, "Dialogo Nuevo Usuario", scale)
        record_audit("Dialogo Nuevo Usuario", items_nu, overlaps_nu, lrows_nu)
        user32.PostMessageW(hwnd_new_user, WM_CLOSE, 0, 0)
        time.sleep(0.35)

        # --- 5. Roles y permisos (con rol Cajero seleccionado -> interruptores visibles) ---
        click_subnav(hwnd_main, "Roles y permisos")
        time.sleep(0.35)
        r_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        if r_lists and send_msg(r_lists[0], LVM_GETITEMCOUNT, 0, 0) >= 1:
            select_list_row(r_lists[0], 0)
            time.sleep(0.35)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Roles y permisos", scale)
        record_audit("Roles y permisos", items, overlaps, lrows)

        # --- 6. Configuracion (y ambos desplegables abiertos) ---
        click_subnav(hwnd_main, "Configuración")
        time.sleep(0.4)
        cfg_combos = sorted(
            [h for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h) and ("combobox" in c.lower() or t == "comboBox")],
            key=lambda h: get_rect(h)[1]
        )
        assert len(cfg_combos) >= 2, f"Se esperaban 2 desplegables en Configuracion, encontrados {len(cfg_combos)}"
        printers_cnt = 1 + max(1, count_installed_printers())
        pop_printer = audit_combo_dropdown(p2.pid, hwnd_main, cfg_combos[0], "Configuracion:Impresora", printers_cnt, scale)
        pop_paper = audit_combo_dropdown(p2.pid, hwnd_main, cfg_combos[1], "Configuracion:PerfilPapel", 2, scale)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Configuración", scale)
        record_audit("Configuración", items, overlaps, lrows, [pop_printer, pop_paper])

        # --- 7. Ingredientes (inicial/seleccionado + formulario nuevo) ---
        click_sidebar_section(hwnd_main, "Inventario")
        click_subnav(hwnd_main, "Ingredientes")
        time.sleep(0.4)
        i_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        if i_lists and send_msg(i_lists[0], LVM_GETITEMCOUNT, 0, 0) >= 1:
            select_list_row(i_lists[0], 0)
            time.sleep(0.35)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Ingredientes", scale)
        record_audit("Ingredientes", items, overlaps, lrows)

        btn_new_ing = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Nuevo ingrediente"), None)
        assert btn_new_ing is not None, "Boton Nuevo ingrediente no encontrado"
        click_button(btn_new_ing)
        time.sleep(0.35)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Ingredientes (nuevo)", scale)
        record_audit("Ingredientes (nuevo)", items, overlaps, lrows)

        ing_edits = sorted(
            [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda x: x[1][1],
        )
        assert len(ing_edits) >= 2, "Campo de aviso bajo en Ingredientes no encontrado"
        click_control_at(ing_edits[1][0])
        time.sleep(0.25)
        items, overlaps, lrows = audit_window_touch_targets(hwnd_main, "Ingredientes (nuevo, teclado)", scale)
        assert any(it["label"] == "Listo" for it in items), "Teclado numérico no visible en Ingredientes"
        record_audit("Ingredientes (nuevo, teclado)", items, overlaps, lrows)

        # --- 8. Dialogos de Venta: Cobro, Ticket e Historial de tickets ---
        click_sidebar_section(hwnd_main, "Punto de Venta")
        time.sleep(0.45)

        btn_cobrar = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Cobrar" in t), None)
        assert btn_cobrar is not None and user32.IsWindowEnabled(btn_cobrar), "Boton Cobrar no disponible"
        post_click_button(btn_cobrar)
        hwnd_chk, _ = wait_for_window(p2.pid, "Cobro de Venta", timeout=8.0)
        time.sleep(0.25)
        items_chk, overlaps_chk, lrows_chk = audit_window_touch_targets(hwnd_chk, "Dialogo Cobro", scale)
        assert any(it["label"] == "Listo" for it in items_chk), "Teclado numérico embebido no visible en Dialogo Cobro"
        record_audit("Dialogo Cobro", items_chk, overlaps_chk, lrows_chk)

        btn_conf = next((h for (h, c, t) in enum_children(hwnd_chk) if c.lower() == "button" and user32.IsWindowVisible(h) and "Confirmar Venta" in t), None)
        assert btn_conf is not None
        post_click_button(btn_conf)
        hwnd_tkt, _ = wait_for_window(p2.pid, "Venta Registrada", timeout=8.0)
        time.sleep(0.25)
        items_tkt, overlaps_tkt, lrows_tkt = audit_window_touch_targets(hwnd_tkt, "Dialogo Ticket", scale)
        record_audit("Dialogo Ticket", items_tkt, overlaps_tkt, lrows_tkt)
        user32.PostMessageW(hwnd_tkt, WM_CLOSE, 0, 0)
        time.sleep(0.4)

        # Abrir Historial de tickets mediante comando 1001 (Mas acciones > Historial)
        btn_more = next((h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Más acciones" in t), None)
        assert btn_more is not None, "Boton Mas acciones no encontrado"
        user32.PostMessageW(hwnd_main, 0x0111, 1001, 0)
        hwnd_hist, _ = wait_for_window(p2.pid, "Historial de Tickets", timeout=8.0)
        time.sleep(0.3)
        items_h, overlaps_h, lrows_h = audit_window_touch_targets(hwnd_hist, "Dialogo Historial", scale)
        record_audit("Dialogo Historial", items_h, overlaps_h, lrows_h)
        user32.PostMessageW(hwnd_hist, WM_CLOSE, 0, 0)
        time.sleep(0.35)

        if write_audit:
            os.makedirs(os.path.dirname(AUDIT_PATH), exist_ok=True)
            with open(AUDIT_PATH, "w", encoding="utf-8") as f_aud:
                f_aud.write("\n".join(audit_lines) + "\n")
            print(f"[OK] Tabla escrita en {AUDIT_PATH}")

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        p2.wait(timeout=6)

        # Comprobar excepciones vs violaciones inesperadas
        unexpected = {k: v for k, v in violations.items() if k not in KNOWN_EXCEPTIONS}
        unreproduced = {k: v for k, v in KNOWN_EXCEPTIONS.items() if k not in violations}

        if KNOWN_EXCEPTIONS:
            print(f"\n[{label}] Excepciones registradas ({len(KNOWN_EXCEPTIONS)}):")
            for k, meta in KNOWN_EXCEPTIONS.items():
                measured = violations.get(k, "NO_REPRODUCIDA")
                print(f"  - {k[0]} | {k[1]} | {k[2]} | medido={measured} | archivo={meta['archivo']} | {meta['motivo']}")

        assert not text_fit_failures, (
            f"[{label}] Botones cuyo texto + icono + margen no cabe en su area cliente:\n"
            + "\n".join(f"  - {line}" for line in text_fit_failures)
        )
        assert not unexpected, (
            f"[{label}] Se encontraron objetivos fuera de la lista de excepciones:\n"
            + "\n".join(f"  - {k}: {v}" for k, v in sorted(unexpected.items()))
        )
        assert not unreproduced, (
            f"[{label}] Excepciones registradas que ya no se reprodujeron:\n"
            + "\n".join(f"  - {k}: {v}" for k, v in sorted(unreproduced.items()))
        )
        print(f"[{label}] Vigilancia T2 superada.")

    finally:
        for pr in procs:
            if pr.poll() is None:
                try:
                    pr.terminate()
                    pr.wait(timeout=3)
                except Exception:
                    try:
                        pr.kill()
                    except Exception:
                        pass
        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir, ignore_errors=True)


def main():
    enable_per_monitor_dpi()
    sys_dpi, sys_scale = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {sys_dpi} (escala={sys_scale:.2f})")

    run_suite("Release", RELEASE_EXE, write_audit=True)
    run_suite("Debug", DEBUG_EXE, write_audit=False)
    print("\n=== PRUEBA T2 OBJETIVOS TACTILES EXITOSA (Release y Debug) ===")


if __name__ == "__main__":
    main()
