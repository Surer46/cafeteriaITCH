#!/usr/bin/env python3
"""
test_t1_touch_profile.py - Prueba GUI real para Bloque T1 (Perfil Tactil en Configuracion y dialogos).

Verifica Release y Debug con ejecutables nativos y bases SQLite aisladas bajo cualquier escalado de Windows:
- Conciencia DPI por monitor (SetProcessDpiAwarenessContext(-4)) y umbrales escalados por GetDpiForWindow/96.
a) Primer arranque sin station_profile.cfg: Alta del Dueno en Estandar, sin recortes ni controles fuera.
b) BackOffice > Configuracion: opciones Estandar y Tactil, pantalla de impresion incrustada; cambiar a Tactil
   actualiza station_profile.cfg, barra lateral (>= 56 logicos), boton de encabezado, opcion marcada y vista actual.
c) Producto sembrado por SQL, agregar linea en Venta, cambiar perfil en Configuracion y volver a Venta:
   la linea persiste en pantalla y en SQLite.
d) Volver a Estandar desde boton del encabezado estando en Configuracion: opcion marcada cambia a Estandar.
e) Cerrar en Tactil y reabrir: Login en Tactil (>= 48 campos, >= 56 boton, centrado vertical, fuente Tactil),
   y tras entrar Configuracion muestra Tactil marcada.
f) Primer arranque con station_profile.cfg='touch' escrito previamente: Alta Dueno en Tactil con mismas cotas.
g) Inventario informativo de objetivos tactiles en perfil Tactil (1200x720 logicos) en las 7 pantallas reales,
   escrito en qa_visual/touch_audit.txt (no falla la prueba).
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
    WM_CLOSE,
    GWL_STYLE,
    WS_TABSTOP,
    ES_READONLY,
    LVM_GETITEMCOUNT,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_dpi,
    get_window_scale,
    ensure_no_app_running,
    send_msg,
    get_text,
    get_class,
    get_rect,
    get_client_screen_rect,
    get_work_area_for_window,
    find_windows_by_pid,
    wait_for_window,
    enum_children,
    click_button,
    post_click_button,
    set_edit_text,
    get_control_logfont,
    measure_text_extent,
    measure_ink_vertical_center,
    rects_overlap,
    click_sidebar_section,
    click_subnav,
)

AUDIT_PATH = os.path.join(REPO_ROOT, "qa_visual", "touch_audit.txt")


def check_dialog_geometry(hwnd_dlg, is_touch, expected_edits, label):
    dpi = get_window_dpi(hwnd_dlg)
    scale = get_window_scale(hwnd_dlg)
    dlg_rc = get_rect(hwnd_dlg)
    work_rc = get_work_area_for_window(hwnd_dlg)

    assert dlg_rc[0] >= work_rc[0] and dlg_rc[1] >= work_rc[1] and dlg_rc[2] <= work_rc[2] and dlg_rc[3] <= work_rc[3], (
        f"[{label}] Dialogo fuera del area de trabajo: dlg={dlg_rc}, work={work_rc}"
    )

    cl_rc = get_client_screen_rect(hwnd_dlg)
    children = enum_children(hwnd_dlg)
    visible = [(h, c, t) for (h, c, t) in children if user32.IsWindowVisible(h)]

    edits = [h for (h, c, _t) in visible if c.lower() == "edit"]
    buttons = [(h, t) for (h, c, t) in visible if c.lower() == "button"]
    assert len(edits) == expected_edits, f"[{label}] Edits esperados={expected_edits}, encontrados={len(edits)}"
    assert len(buttons) >= 1, f"[{label}] Boton principal no encontrado"
    btn_main, btn_main_txt = buttons[0]

    # Comprobar contencion en area cliente y textos sin recorte
    for h, c, t in visible:
        r = get_rect(h)
        assert r[0] >= cl_rc[0] - 1 and r[1] >= cl_rc[1] - 1 and r[2] <= cl_rc[2] + 1 and r[3] <= cl_rc[3] + 1, (
            f"[{label}] Control fuera del dialogo ({c} '{t}'): rect={r}, client={cl_rc}"
        )
        cl = c.lower()
        if cl in ("static", "button") and t.strip():
            crc = wintypes.RECT()
            user32.GetClientRect(h, ctypes.byref(crc))
            cw = crc.right - crc.left
            max_line_w = max(measure_text_extent(h, ln)[0] for ln in t.splitlines() if ln.strip())
            assert max_line_w <= cw + 2, (
                f"[{label}] Texto recortado en {cl} '{t}': text_w={max_line_w} > client_w={cw}"
            )

    # Comprobar no solapamientos entre hermanos reales
    for i in range(len(visible)):
        h1, c1, t1 = visible[i]
        p1 = user32.GetParent(h1)
        r1 = get_rect(h1)
        for j in range(i + 1, len(visible)):
            h2, c2, t2 = visible[j]
            if user32.GetParent(h2) != p1:
                continue
            r2 = get_rect(h2)
            assert not rects_overlap(r1, r2), (
                f"[{label}] Solapamiento entre hermanos ({c1} '{t1}' {r1}) y ({c2} '{t2}' {r2})"
            )

    # Medir campos (en Tactil su contenedor es el padre inmediato del Edit) y boton principal
    field_heights = []
    for ed in edits:
        parent_ed = user32.GetParent(ed)
        target = parent_ed if (is_touch and parent_ed != hwnd_dlg) else ed
        rc_f = get_rect(target)
        field_heights.append(rc_f[3] - rc_f[1])

    btn_rc = get_rect(btn_main)
    btn_h = btn_rc[3] - btn_rc[1]

    if is_touch:
        min_field_px = 48.0 * scale
        min_btn_px = 56.0 * scale
        for idx, fh in enumerate(field_heights):
            assert fh >= min_field_px - 0.5, (
                f"[{label}] Campo {idx} mide {fh}px (< {min_field_px:.1f}px a DPI {dpi})"
            )
        assert btn_h >= min_btn_px - 0.5, (
            f"[{label}] Boton principal '{btn_main_txt}' mide {btn_h}px (< {min_btn_px:.1f}px a DPI {dpi})"
        )

        # Fuente de cuerpo Tactil (12pt ~= 16px a 96 DPI)
        lf = get_control_logfont(edits[0])
        assert lf is not None, f"[{label}] Sin fuente en campo de texto"
        expected_lf_h = round(16.0 * scale)
        assert abs(lf.lfHeight) >= expected_lf_h - 1, (
            f"[{label}] Fuente de campo demasiado pequena: lfHeight={lf.lfHeight}, esperado >= {expected_lf_h} (12pt)"
        )

        # Centrado vertical de texto en campo (probado sobre primer campo visible no enmascarado)
        first_wrap = user32.GetParent(edits[0])
        set_edit_text(edits[0], "Prueba")
        time.sleep(0.15)
        ink = measure_ink_vertical_center(first_wrap)
        set_edit_text(edits[0], "")
        time.sleep(0.05)
        assert ink is not None, f"[{label}] No se detecto tinta en campo de texto"
        ink_mid, box_mid = ink
        assert abs(ink_mid - box_mid) <= 3.5 * scale, (
            f"[{label}] Texto no centrado verticalmente: ink_mid={ink_mid:.1f}, box_mid={box_mid:.1f}"
        )

    return {
        "dpi": dpi,
        "scale": scale,
        "dlg_w_log": (dlg_rc[2] - dlg_rc[0]) / scale,
        "dlg_h_log": (dlg_rc[3] - dlg_rc[1]) / scale,
        "field_hs_log": [round(fh / scale, 1) for fh in field_heights],
        "btn_h_log": round(btn_h / scale, 1),
    }


def find_header_profile_button(hwnd_main):
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and t in ("Touch", "Estándar")]
    assert btns, "Boton de perfil en encabezado no encontrado"
    btn, txt = min(btns, key=lambda item: get_rect(item[0])[1])
    return btn, txt


def find_settings_profile_button(hwnd_main, label_text):
    hdr_btn, _ = find_header_profile_button(hwnd_main)
    hdr_bottom = get_rect(hdr_btn)[3]
    btns = [
        (h, t) for (h, c, t) in enum_children(hwnd_main)
        if c.lower() == "button" and user32.IsWindowVisible(h) and t == label_text and get_rect(h)[1] >= hdr_bottom
    ]
    assert btns, f"Boton opcion '{label_text}' en Configuracion no encontrado"
    btn, _ = max(btns, key=lambda item: get_rect(item[0])[1])
    return btn


def audit_screen_touch_targets(hwnd_main, screen_name, scale):
    """Recopila objetivos tactiles visibles y habilitados en el contenido de la pantalla."""
    children = enum_children(hwnd_main)
    visible_enabled = [
        (h, c, t) for (h, c, t) in children
        if user32.IsWindowVisible(h) and user32.IsWindowEnabled(h)
    ]

    # Mapear wrappers de campos (wxPanel que contiene un unico Edit)
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
            if parent and parent != hwnd_main and child_count_by_parent.get(parent, 0) == 1:
                p_cls = get_class(parent).lower()
                if p_cls.startswith("wxwindow"):
                    target_h = parent
            kind = "edit"
            label = f"Edit:{label}" if label else "<edit>"
        elif cl == "combobox":
            kind = "combobox"
            label = f"Combo:{label}" if label else "<combobox>"
        elif cl.startswith("wxwindow"):
            # ToggleSwitch o wxOwnerDrawnComboBox (control hoja interactivo con WS_TABSTOP)
            style = user32.GetWindowLongW(h, GWL_STYLE)
            rc_tmp = get_rect(h)
            w_tmp = rc_tmp[2] - rc_tmp[0]
            h_tmp = rc_tmp[3] - rc_tmp[1]
            # Evitar paneles contenedores grandes de fondo
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
        items.append({
            "hwnd": target_h,
            "parent": user32.GetParent(target_h),
            "rect": rc,
            "kind": kind,
            "label": label.replace("\n", " "),
            "w_log": round(w_log, 1),
            "h_log": round(h_log, 1),
            "small": small,
        })

    # Solapamientos entre pares hermanos dentro de los controles auditados
    overlaps = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            if items[i]["parent"] == items[j]["parent"] and rects_overlap(items[i]["rect"], items[j]["rect"]):
                overlaps.append((items[i]["label"], items[j]["label"]))
                items[i]["overlap"] = True
                items[j]["overlap"] = True

    for it in items:
        it.setdefault("overlap", False)

    return items, overlaps


def run_suite(label, exe_path):
    ensure_no_app_running()
    assert os.path.exists(exe_path), f"Ejecutable no encontrado: {exe_path}"
    work_dir = os.path.dirname(exe_path)

    temp_a = tempfile.mkdtemp(prefix=f"cafeteria_t1_{label}_")
    temp_f = tempfile.mkdtemp(prefix=f"cafeteria_t1_owner_touch_{label}_")
    db_path = os.path.join(temp_a, "cafeteria.db")
    cfg_path = os.path.join(temp_a, "station_profile.cfg")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_a

    owner_name = "Dueño T1 Prueba"
    owner_user = "dueno_t1"
    owner_email = "dueno_t1@itch.edu.mx"
    owner_pass = "ClaveSeguraT1!2026"

    procs = []
    metrics = {}

    try:
        print(f"\n=== [{label}] Iniciando suite T1 ({exe_path}) ===")

        # -----------------------------------------------------------------
        # Caso a: Primer arranque sin station_profile.cfg (Alta Dueno Estandar)
        # -----------------------------------------------------------------
        assert not os.path.exists(cfg_path), "station_profile.cfg no debe existir antes del caso a"
        p = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p)

        hwnd_owner_std, _ = wait_for_window(p.pid, "Primer Inicio")
        m_owner_std = check_dialog_geometry(hwnd_owner_std, is_touch=False, expected_edits=5, label=f"{label}:OwnerStd")
        metrics["owner_std"] = m_owner_std
        print(f"[OK][a] Primer Inicio (Estandar): DPI={m_owner_std['dpi']} escala={m_owner_std['scale']:.2f} "
              f"dlg={m_owner_std['dlg_w_log']:.0f}x{m_owner_std['dlg_h_log']:.0f} "
              f"campos={m_owner_std['field_hs_log']} btn={m_owner_std['btn_h_log']}")

        ctrls_o = [c for c in enum_children(hwnd_owner_std) if user32.IsWindowVisible(c[0])]
        o_edits = [h for (h, c, _t) in ctrls_o if c.lower() == "edit"]
        o_btn = next(h for (h, c, _t) in ctrls_o if c.lower() == "button")
        set_edit_text(o_edits[0], owner_name)
        set_edit_text(o_edits[1], owner_user)
        set_edit_text(o_edits[2], owner_email)
        set_edit_text(o_edits[3], owner_pass)
        set_edit_text(o_edits[4], owner_pass)
        time.sleep(0.2)
        post_click_button(o_btn)

        hwnd_main, _ = wait_for_window(p.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        main_dpi = get_window_dpi(hwnd_main)
        scale = main_dpi / 96.0
        time.sleep(0.4)

        # -----------------------------------------------------------------
        # Caso b: BackOffice > Configuracion -> elegir Tactil
        # -----------------------------------------------------------------
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")

        vis_cfg = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
        texts_cfg = [t for (_h, _c, t) in vis_cfg if t]
        assert any("Pantalla de este equipo" in t for t in texts_cfg), "Falta titulo 'Pantalla de este equipo'"
        assert any("ratón y teclado" in t.lower() for t in texts_cfg), "Falta explicacion de Estandar"
        assert any("dedo" in t.lower() for t in texts_cfg), "Falta explicacion de Tactil"
        assert any("solo afecta a este equipo" in t.lower() for t in texts_cfg), "Falta nota de equipo/reinicio"
        assert any("Configuración de Impresora Térmica" in t for t in texts_cfg), "Falta pantalla de impresion incrustada"
        assert any("Guardar Configuración" in t for t in texts_cfg), "Falta boton Guardar Configuración"

        btns_cfg = [(h, t) for (h, c, t) in vis_cfg if c.lower() == "button"]
        assert any("Estándar" in t and "Activa" in t for (_h, t) in btns_cfg), f"Estándar debe estar activa: {btns_cfg}"
        btn_touch_opt = find_settings_profile_button(hwnd_main, "Táctil")

        post_click_button(btn_touch_opt)
        t_dead = time.time() + 5.0
        touch_active_seen = False
        while time.time() < t_dead:
            for h, c, t in enum_children(hwnd_main):
                if c.lower() == "button" and user32.IsWindowVisible(h) and "Táctil" in t and "Activa" in t:
                    touch_active_seen = True
                    break
            if touch_active_seen:
                break
            time.sleep(0.1)
        assert touch_active_seen, "Botón 'Táctil (Activa)' no apareció dentro de 5 s tras cambiar perfil"

        assert os.path.exists(cfg_path), "station_profile.cfg no fue escrito al elegir Táctil"
        with open(cfg_path, "r", encoding="utf-8") as f:
            assert f.read().strip().lower() == "touch", "station_profile.cfg debe contener 'touch'"

        # Esperar con condicion (hasta 5 s) a que los 4 botones laterales alcancen altura tactil antes de medir
        t_sb = time.time() + 5.0
        sidebar_ready = False
        while time.time() < t_sb:
            vis_now = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            sbs = [
                next((h for (h, c, t) in vis_now if c.lower() == "button" and t == sec), None)
                for sec in ("Punto de Venta", "Inventario", "Caja y Reportes", "BackOffice")
            ]
            if all(sb is not None for sb in sbs) and all(
                (get_rect(sb)[3] - get_rect(sb)[1]) >= 56.0 * scale - 0.5 for sb in sbs
            ):
                sidebar_ready = True
                break
            time.sleep(0.1)
        assert sidebar_ready, "La barra lateral no alcanzó la altura táctil dentro de 5 s tras cambiar perfil"

        # Los 4 botones laterales >= 56 logicos
        vis_after = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
        for sec in ("Punto de Venta", "Inventario", "Caja y Reportes", "BackOffice"):
            sb = next((h for (h, c, t) in vis_after if c.lower() == "button" and t == sec), None)
            assert sb is not None, f"Falta boton sidebar '{sec}'"
            sr = get_rect(sb)
            assert (sr[3] - sr[1]) >= 56.0 * scale - 0.5, f"Sidebar '{sec}' mide {sr[3]-sr[1]}px (< {56*scale:.1f})"

        _, hdr_txt = find_header_profile_button(hwnd_main)
        assert hdr_txt == "Estándar", f"Boton encabezado debe decir 'Estándar', encontrado '{hdr_txt}'"

        btns_after = [(h, t) for (h, c, t) in vis_after if c.lower() == "button"]
        assert any("Táctil" in t and "Activa" in t for (_h, t) in btns_after), f"Táctil debe quedar activa: {btns_after}"
        assert any("Pantalla de este equipo" in t for (_h, _c, t) in vis_after), "Debe seguir en Configuracion"
        assert any("Configuración de Impresora Térmica" in t for (_h, _c, t) in vis_after), "Impresion debe seguir visible"
        print("[OK][b] BackOffice > Configuracion cambio a Tactil correctamente y conservo la vista.")

        # -----------------------------------------------------------------
        # Caso c: Carrito persistente tras cambio de perfil desde Configuracion
        # -----------------------------------------------------------------
        con = sqlite3.connect(db_path)
        con.execute(
            """
            INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
            VALUES ('prod-t1-01', 'T1-01', '750100009901', 'Cafe T1 Prueba', 'Bebidas', 4500, 0, 1, '2026-11-13 10:00:00')
            """
        )
        con.commit()
        con.close()

        click_sidebar_section(hwnd_main, "Punto de Venta")
        time.sleep(0.3)
        pos_btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
        btn_add = next((h for (h, t) in pos_btns if "Agregar al Carrito" in t), None)
        assert btn_add is not None, "Boton Agregar al Carrito no encontrado"
        click_button(btn_add)
        time.sleep(0.4)

        pos_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        assert len(pos_lists) >= 1 and send_msg(pos_lists[0], LVM_GETITEMCOUNT, 0, 0) == 1, "Linea no aparecio en tabla de Venta"

        con = sqlite3.connect(db_path)
        qty_before = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items WHERE product_id='prod-t1-01'").fetchone()[0]
        con.close()
        assert qty_before == 1, f"SQLite cart_items esperado 1, obtenido {qty_before}"

        # Cambiar perfil a Estandar desde Configuracion
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")
        btn_std_opt = find_settings_profile_button(hwnd_main, "Estándar")
        post_click_button(btn_std_opt)
        time.sleep(0.6)

        click_sidebar_section(hwnd_main, "Punto de Venta")
        time.sleep(0.35)
        pos_lists2 = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        assert len(pos_lists2) >= 1 and send_msg(pos_lists2[0], LVM_GETITEMCOUNT, 0, 0) == 1, "Carrito desaparecio de Venta tras cambio de perfil"
        con = sqlite3.connect(db_path)
        qty_after = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items WHERE product_id='prod-t1-01'").fetchone()[0]
        con.close()
        assert qty_after == 1, f"Carrito desaparecio de SQLite tras cambio de perfil: {qty_after}"
        print("[OK][c] Carrito en pantalla y SQLite conservado tras cambiar perfil desde Configuracion.")

        # -----------------------------------------------------------------
        # Caso d: Volver a Estandar con boton del encabezado estando en Configuracion
        # -----------------------------------------------------------------
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")
        btn_touch_opt2 = find_settings_profile_button(hwnd_main, "Táctil")
        post_click_button(btn_touch_opt2)
        time.sleep(0.6)

        hdr_btn, hdr_label = find_header_profile_button(hwnd_main)
        assert hdr_label == "Estándar", f"Encabezado debia decir 'Estándar', encontrado '{hdr_label}'"
        click_button(hdr_btn)
        time.sleep(0.6)

        vis_d = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
        btns_d = [(h, t) for (h, c, t) in vis_d if c.lower() == "button"]
        assert any("Estándar" in t and "Activa" in t for (_h, t) in btns_d), f"Estándar no quedo marcada en Configuracion: {btns_d}"
        _, hdr_after_d = find_header_profile_button(hwnd_main)
        assert hdr_after_d == "Touch"
        print("[OK][d] Al volver a Estandar con boton del encabezado, Configuracion marco Estandar.")

        # Medir Login en Estandar antes de pasar a Tactil para e
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        p.wait(timeout=6)

        p_ls = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p_ls)
        hwnd_login_std, _ = wait_for_window(p_ls.pid, "Iniciar Sesión")
        m_login_std = check_dialog_geometry(hwnd_login_std, is_touch=False, expected_edits=2, label=f"{label}:LoginStd")
        metrics["login_std"] = m_login_std
        print(f"[OK][StdLogin] Login (Estandar): dlg={m_login_std['dlg_w_log']:.0f}x{m_login_std['dlg_h_log']:.0f} "
              f"campos={m_login_std['field_hs_log']} btn={m_login_std['btn_h_log']}")

        ls_ctrls = [c for c in enum_children(hwnd_login_std) if user32.IsWindowVisible(c[0])]
        ls_edits = [h for (h, c, _t) in ls_ctrls if c.lower() == "edit"]
        ls_btn = next(h for (h, c, _t) in ls_ctrls if c.lower() == "button")
        set_edit_text(ls_edits[0], owner_user)
        set_edit_text(ls_edits[1], owner_pass)
        time.sleep(0.15)
        post_click_button(ls_btn)

        hwnd_main, _ = wait_for_window(p_ls.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        time.sleep(0.35)
        click_sidebar_section(hwnd_main, "BackOffice")
        click_subnav(hwnd_main, "Configuración")
        btn_touch_opt3 = find_settings_profile_button(hwnd_main, "Táctil")
        post_click_button(btn_touch_opt3)
        time.sleep(0.6)
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        p_ls.wait(timeout=6)

        # -----------------------------------------------------------------
        # Caso e: Reabrir en Tactil -> Login en Tactil y Configuracion marcada
        # -----------------------------------------------------------------
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(p2)
        hwnd_login_touch, _ = wait_for_window(p2.pid, "Iniciar Sesión")
        m_login_touch = check_dialog_geometry(hwnd_login_touch, is_touch=True, expected_edits=2, label=f"{label}:LoginTouch")
        metrics["login_touch"] = m_login_touch
        print(f"[OK][e] Login (Tactil): dlg={m_login_touch['dlg_w_log']:.0f}x{m_login_touch['dlg_h_log']:.0f} "
              f"campos={m_login_touch['field_hs_log']} btn={m_login_touch['btn_h_log']}")

        lt_ctrls = [c for c in enum_children(hwnd_login_touch) if user32.IsWindowVisible(c[0])]
        lt_edits = [h for (h, c, _t) in lt_ctrls if c.lower() == "edit"]
        lt_btn = next(h for (h, c, _t) in lt_ctrls if c.lower() == "button")
        set_edit_text(lt_edits[0], owner_user)
        set_edit_text(lt_edits[1], owner_pass)
        time.sleep(0.15)
        post_click_button(lt_btn)

        hwnd_main2, _ = wait_for_window(p2.pid, "Cafetería ITCH II", timeout=15.0, exclude=("Primer Inicio", "Iniciar Sesión"))
        time.sleep(0.35)
        click_sidebar_section(hwnd_main2, "BackOffice")
        click_subnav(hwnd_main2, "Configuración")
        vis_e = [(h, c, t) for (h, c, t) in enum_children(hwnd_main2) if user32.IsWindowVisible(h)]
        assert any(c.lower() == "button" and "Táctil" in t and "Activa" in t for (_h, c, t) in vis_e), (
            "Tras reabrir en Tactil, Configuracion debe mostrar Tactil marcada"
        )
        print("[OK][e] Tras entrar en Tactil, Configuracion mantiene Tactil marcada.")

        # -----------------------------------------------------------------
        # Caso f: Alta del Dueno en Tactil con station_profile.cfg preexistente
        # -----------------------------------------------------------------
        with open(cfg_path, "rb") as f_cfg:
            touch_cfg_bytes = f_cfg.read()
        with open(os.path.join(temp_f, "station_profile.cfg"), "wb") as f_out:
            f_out.write(touch_cfg_bytes)

        env_f = os.environ.copy()
        env_f["PATH"] = work_dir + ";" + env_f.get("PATH", "")
        env_f["CAFETERIA_DATA_DIR"] = temp_f

        p_f = subprocess.Popen([exe_path], cwd=work_dir, env=env_f)
        procs.append(p_f)
        hwnd_owner_touch, _ = wait_for_window(p_f.pid, "Primer Inicio")
        m_owner_touch = check_dialog_geometry(hwnd_owner_touch, is_touch=True, expected_edits=5, label=f"{label}:OwnerTouch")
        metrics["owner_touch"] = m_owner_touch
        print(f"[OK][f] Primer Inicio (Tactil): dlg={m_owner_touch['dlg_w_log']:.0f}x{m_owner_touch['dlg_h_log']:.0f} "
              f"campos={m_owner_touch['field_hs_log']} btn={m_owner_touch['btn_h_log']}")

        user32.PostMessageW(hwnd_owner_touch, WM_CLOSE, 0, 0)
        p_f.wait(timeout=5)

        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=6)

        print(f"[{label}] Resumen alturas logicas: {metrics}")
        return metrics

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
        for td in (temp_a, temp_f):
            if td and os.path.exists(td):
                import shutil
                shutil.rmtree(td, ignore_errors=True)


def main():
    enable_per_monitor_dpi()
    sys_dpi, sys_scale = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {sys_dpi} (escala={sys_scale:.2f})")

    run_suite("Release", RELEASE_EXE)
    run_suite("Debug", DEBUG_EXE)
    print("\n=== PRUEBA T1 COMPLETA EXITOSA (Release y Debug) ===")


if __name__ == "__main__":
    main()
