#!/usr/bin/env python3
"""
test_t16_fullscreen_layout.py - Prueba permanente de aprovechamiento de espacio en pantalla completa y ventana normal (T16d / T16b / I7).

Ejecuta build_release/app_desktop.exe con SQLite aislado, datos deterministas de ejemplo y
CAFETERIA_UPDATE_CHECK=0, recorriendo las 17 subsecciones reales en:
  - Pantalla completa (Estándar y Táctil)
  - Ventana normal 1200x720 y 960x600 (Estándar y Táctil)

Umbrales (sección 3 de Prompts/plan-2026-11-21-T16-neon.md — NO RELAJAR):
  - Cobertura horizontal >= 93.0 % (union_w_pct)
  - Cobertura vertical   >= 88.0 % (union_h_pct)
  - Franja vacía horizontal <= 5.0 % (max_h_gap_pct)
  - Franja vacía vertical   <= 8.0 % (max_v_gap_pct)
  - Sin barra vertical en pantalla completa (vscroll=NO), salvo scroll_perms_ en Roles
    identificada de forma específica (ver comentario en ROLE_SCROLL_EXCEPTION)
  - Sin barra horizontal en ventana normal ni pantalla completa (hscroll=NO)
  - Sin texto cortado en wxStaticText visibles (texto_cortado=-)
  - Llenado horizontal de columnas >= 95.0 % en todas las listas visibles (SysListView32)
  - Sin columna recortada mientras otra de la misma lista tiene >= 40 px lógicos de sobra
    (convertidos con tests/gui_dpi.py: 40 px a 100 %, 50 px a 125 %)
  - Roles y permisos: >= 10/22 interruptores enteramente visibles en pantalla completa (Estándar y Táctil)
    y >= 4 filas de permisos enteramente visibles en Táctil 1200x720 (en reposo y con mensaje de validación visible)

REGLA PARA BLOQUES SIGUIENTES (T16c y posteriores):
  Cuando T16c termine una pantalla, la pasa a estado "terminada" en SCREEN_STATES
  y quita todas sus entradas de KNOWN_EXCEPTIONS. Nunca se relaja un umbral ni se agrega
  una excepción para una pantalla terminada.
"""

import ctypes
from ctypes import wintypes
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gui_dpi import (
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    format_dpi_info,
    get_window_dpi,
    get_window_scale,
    measure_text_extent,
    min_px,
    select_list_row,
    set_window_logical_size,
)
from fullscreen_layout_lib import (
    GWL_STYLE,
    MAX_H_GAP_PCT,
    MAX_V_GAP_PCT,
    MIN_UNION_H_PCT,
    MIN_UNION_W_PCT,
    SB_HORZ,
    SB_VERT,
    SCROLLINFO,
    SIF_PAGE,
    SIF_RANGE,
    WM_CLOSE,
    WS_CAPTION,
    WS_HSCROLL,
    WS_VSCROLL,
    bootstrap_isolated_db,
    capture_window_img,
    click_button_text,
    detect_clipped_static_texts,
    detect_panel_scrollbars,
    enum_children,
    find_content_container,
    get_monitor_size,
    get_rect,
    is_descendant,
    iterate_all_17_screens,
    measure_view,
    post_click,
    read_listview_texts,
    set_text,
    user32,
    wait_content_list,
    wait_window,
)

user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL

MIN_LIST_COLS_FILL_PCT = 95.0
SPARE_COL_LOGICAL_PX = 40
ROLES_MIN_FULLSCREEN_TOGGLES = 10
ROLES_EXPECTED_TOTAL_TOGGLES = 22
ROLES_MIN_TOUCH_1200_ROWS = 4
ROLES_SECTION_HEADERS = {"Punto de Venta", "Inventario", "Caja y Reportes", "BackOffice"}


# ---------------------------------------------------------------------------
# Tabla de estado por pantalla (17 subsecciones)
# Estados permitidos: "terminada", "excepcion", "pendiente T16b", "pendiente T16c"
# ---------------------------------------------------------------------------
SCREEN_STATES = {
    # Terminadas en T16a
    "configuracion": {
        "estado": "terminada",
        "bloque": "T16a",
    },
    "respaldos": {
        "estado": "terminada",
        "bloque": "T16a",
    },
    # Excepción aprobada por el usuario (2026-11-21): Actualizaciones se muestra en dos
    # columnas ocupando toda la altura sin inventar contenido nuevo para rellenar el interior
    # vacío de las tarjetas; por ello no se comprueba la franja vertical (max_v_gap_pct).
    "actualizaciones": {
        "estado": "excepcion",
        "bloque": "T16a",
        "motivo": "Interior vacío de las dos tarjetas por decisión del usuario (2026-11-21); no se inventa contenido.",
    },
    # Terminadas en T16b (BackOffice Usuarios y Roles)
    "usuarios": {
        "estado": "terminada",
        "bloque": "T16b",
    },
    "roles": {
        "estado": "terminada",
        "bloque": "T16b",
    },
    # Pendientes T16c (Inventario, POS, Caja y Reportes)
    "venta": {"estado": "pendiente T16c", "bloque": "T16c"},
    "existencias": {"estado": "pendiente T16c", "bloque": "T16c"},
    "reabastecimiento": {"estado": "pendiente T16c", "bloque": "T16c"},
    "productos_y_recetas": {"estado": "pendiente T16c", "bloque": "T16c"},
    "ingredientes": {"estado": "pendiente T16c", "bloque": "T16c"},
    "promociones": {"estado": "pendiente T16c", "bloque": "T16c"},
    "combos": {"estado": "pendiente T16c", "bloque": "T16c"},
    "barra": {"estado": "pendiente T16c", "bloque": "T16c"},
    "caja_corte": {"estado": "pendiente T16c", "bloque": "T16c"},
    "caja_historial": {"estado": "pendiente T16c", "bloque": "T16c"},
    "bitacora": {"estado": "pendiente T16c", "bloque": "T16c"},
    "reportes": {"estado": "pendiente T16c", "bloque": "T16c"},
}


# ---------------------------------------------------------------------------
# Excepciones conocidas SOLO para pantallas pendientes (T16c).
# Clave: (pantalla, perfil, tamano, metrica)
#   - tamano: "fullscreen", "1200x720", "960x600" o "toggle_fullscreen"
#   - metrica: "union_w", "union_h", "gap_h", "gap_v", "vscroll", "hscroll",
#              "texto_cortado", "lv_cols_fill", "lv_clip_sobra"
# Prohibido agregar una pantalla con estado "terminada" a esta lista.
# ---------------------------------------------------------------------------
KNOWN_EXCEPTIONS = {
    # --- Pantalla completa ---
    ("productos_y_recetas", "Estandar", "fullscreen", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Falta registrar una compra..." recortada (48>25px, 2L)',
    },
    ("productos_y_recetas", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Detalle de receta en Táctil requiere desplazamiento vertical (costo/margen debajo)",
    },
    ("promociones", "Estandar", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Panel de Promociones muestra barra vertical en pantalla completa Estándar",
    },
    ("promociones", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Panel de Promociones muestra barra vertical en pantalla completa Táctil",
    },
    ("caja_historial", "Estandar", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Historial de cortes muestra barra vertical en pantalla completa Estándar",
    },
    ("caja_historial", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Historial de cortes muestra barra vertical en pantalla completa Táctil",
    },
    ("venta", "Estandar", "fullscreen", "lv_clip_sobra"): {
        "bloque": "T16c",
        "detalle": "Tabla de venta recorta Producto mientras sobra espacio en Notas",
    },
    ("venta", "Tactil", "fullscreen", "lv_clip_sobra"): {
        "bloque": "T16c",
        "detalle": "Tabla de venta recorta Producto mientras sobra espacio en Notas",
    },
    ("productos_y_recetas", "Tactil", "fullscreen", "lv_clip_sobra"): {
        "bloque": "T16c",
        "detalle": "Tabla de productos recorta Nombre mientras sobra espacio en Estado",
    },
    ("combos", "Tactil", "fullscreen", "lv_clip_sobra"): {
        "bloque": "T16c",
        "detalle": "Tabla de combos recorta Partes mientras sobra espacio en Estado",
    },
    # --- Ventana normal (1200x720 y 960x600) ---
    ("promociones", "Estandar", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Toca o haz clic en los prod..." recortada en 960x600 Estándar',
    },
    ("venta", "Tactil", "1200x720", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta de producto largo "Agua Embotellada Natural 60..." recortada en 1200x720 Táctil',
    },
    ("productos_y_recetas", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Categoría (elige una o escr..." recortada en 960x600 Táctil',
    },
    ("combos", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Categoría (elige una o escr..." recortada en 960x600 Táctil',
    },
    ("caja_corte", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Motivo de diferencia (oblig..." recortada en 960x600 Táctil',
    },
    ("bitacora", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Registro cronológico inmuta..." recortada en 960x600 Táctil',
    },
}


def validate_config_integrity():
    """Garantiza la regla permanente: ninguna pantalla terminada puede estar en KNOWN_EXCEPTIONS."""
    for key, info in KNOWN_EXCEPTIONS.items():
        screen = key[0]
        assert screen in SCREEN_STATES, f"Excepción conocida referencia pantalla inexistente: {screen}"
        st = SCREEN_STATES[screen]["estado"]
        assert st != "terminada", (
            f"Regla violada: la pantalla terminada '{screen}' no puede tener excepciones en KNOWN_EXCEPTIONS ({key}, {info})."
        )


def get_client_screen_rect(hwnd):
    rc = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    pt = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return (pt.x, pt.y, pt.x + (rc.right - rc.left), pt.y + (rc.bottom - rc.top))


def has_scrollbar_axis(hwnd, axis):
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    flag = WS_HSCROLL if axis == SB_HORZ else WS_VSCROLL
    if style & flag:
        return True
    si = SCROLLINFO()
    si.cbSize = ctypes.sizeof(SCROLLINFO)
    si.fMask = SIF_RANGE | SIF_PAGE
    if user32.GetScrollInfo(hwnd, axis, ctypes.byref(si)):
        if si.nMax > si.nMin and (si.nPage == 0 or (si.nMax - si.nMin + 1) > int(si.nPage)):
            return True
    return False


def detect_scrollbars_excluding(content_hwnd, excluded_vscroll_hwnds=None):
    """Detecta scrollbars en wxWindowNR visibles dentro de content_hwnd, excluyendo solo los HWND indicados en vertical."""
    excluded = excluded_vscroll_hwnds or set()
    has_h = False
    has_v = False
    for h, cls, _txt in enum_children(content_hwnd):
        if cls.lower() != "wxwindownr" or not user32.IsWindowVisible(h):
            continue
        if has_scrollbar_axis(h, SB_HORZ):
            has_h = True
        if h not in excluded and has_scrollbar_axis(h, SB_VERT):
            has_v = True
    return has_h, has_v


def inspect_listviews(content_hwnd, scale):
    """Mide llenado horizontal exacto y conflicto CLIP+SOBRA (>=40 px lógicos) de cada SysListView32 visible."""
    pad = min_px(12, scale)
    spare_thr = min_px(SPARE_COL_LOGICAL_PX, scale)
    results = []
    for h, cls, _txt in enum_children(content_hwnd):
        if cls.lower() != "syslistview32" or not user32.IsWindowVisible(h):
            continue
        r = get_rect(h)
        if r[2] <= r[0] or r[3] <= r[1]:
            continue
        res = read_listview_texts(h)
        if len(res) != 6:
            continue
        headers, col_ws, rows, _item_cnt, _row_h, hdr_hwnd = res
        crc = wintypes.RECT()
        user32.GetClientRect(h, ctypes.byref(crc))
        list_cw = crc.right - crc.left
        sum_col_w = sum(col_ws)
        fill_pct = (100.0 * sum_col_w / list_cw) if list_cw > 0 else 0.0
        has_lv_hscroll = (user32.GetWindowLongW(h, GWL_STYLE) & WS_HSCROLL) != 0

        clipped_cols = []
        spare_cols = []
        for idx, cw_col in enumerate(col_ws):
            h_txt = headers[idx] if idx < len(headers) else f"c{idx}"
            max_tw = measure_text_extent(hdr_hwnd, h_txt)[0] + pad if (hdr_hwnd and h_txt) else 0
            for row in rows:
                if idx < len(row) and row[idx]:
                    tw = measure_text_extent(h, row[idx])[0] + pad
                    if tw > max_tw:
                        max_tw = tw
            if max_tw > cw_col:
                clipped_cols.append(f"{h_txt}({max_tw}>{cw_col})")
            elif cw_col - max_tw >= spare_thr:
                spare_cols.append(f"{h_txt}(+{cw_col - max_tw})")

        results.append({
            "fill_pct": fill_pct,
            "has_hscroll": has_lv_hscroll,
            "clipped_cols": clipped_cols,
            "spare_cols": spare_cols,
            "spare_thr_px": spare_thr,
        })
    return results


def inspect_roles_scroll_perms(content_hwnd):
    """
    Identifica específicamente scroll_perms_ en BackOffice -> Roles y permisos y cuenta
    interruptores y filas de permisos enteramente visibles dentro de su área cliente.

    Motivo de la excepción acotada a scroll_perms_ (aprobada por el orquestador en T16b):
    BackofficeRolesPanel no tiene desplazamiento de página; el único contenedor con scroll
    vertical permitido es la lista interna de 22 permisos (funcionalmente equivalente a un
    SysListView32). Cualquier otra barra vertical en la pantalla de Roles sigue fallando,
    y a cambio se exigen mínimos de densidad (>=10/22 interruptores en pantalla completa y
    >=4 filas en Táctil 1200x720).
    """
    all_kids = enum_children(content_hwnd)
    vis_kids = []
    parents_with_vis_child = set()
    for h, cls, txt in all_kids:
        if user32.IsWindowVisible(h):
            r = get_rect(h)
            if r[2] > r[0] and r[3] > r[1]:
                vis_kids.append((h, cls, txt, r))
                parents_with_vis_child.add(user32.GetParent(h))

    # scroll_perms_ es el único wxWindowNR visible cuyos hijos directos Static incluyen
    # los 4 encabezados de sección de la barra lateral.
    candidates = []
    for h, cls, _txt, _r in vis_kids:
        if cls.lower() != "wxwindownr":
            continue
        direct_statics = {
            t for (_ch, c, t, _cr) in vis_kids
            if user32.GetParent(_ch) == h and c.lower() == "static"
        }
        if ROLES_SECTION_HEADERS.issubset(direct_statics):
            candidates.append(h)

    if len(candidates) != 1:
        return {
            "found": False,
            "candidate_count": len(candidates),
            "scroll_hwnd": 0,
            "other_vscroll": True,
            "any_hscroll": True,
            "toggles_in_view": 0,
            "toggles_total": 0,
            "rows_in_view": 0,
            "rows_total": 0,
        }

    h_scroll = candidates[0]
    _sl, st, _sr, sb = get_client_screen_rect(h_scroll)
    any_h, other_v = detect_scrollbars_excluding(content_hwnd, excluded_vscroll_hwnds={h_scroll})

    toggles_total = 0
    toggles_in_view = 0
    row_hwnds = set()
    rows_in_view = 0
    for ch_h, ch_cls, _ch_t, ch_r in vis_kids:
        if ch_cls.lower() != "wxwindownr" or ch_h in parents_with_vis_child:
            continue
        if not is_descendant(ch_h, h_scroll):
            continue
        cw_k = ch_r[2] - ch_r[0]
        ch_k = ch_r[3] - ch_r[1]
        if 24 <= cw_k <= 120 and 16 <= ch_k <= 80:
            toggles_total += 1
            if ch_r[1] >= st and ch_r[3] <= sb:
                toggles_in_view += 1
            h_row = user32.GetParent(ch_h)
            if h_row and user32.GetParent(h_row) == h_scroll and h_row not in row_hwnds:
                row_hwnds.add(h_row)
                rr = get_rect(h_row)
                if rr[1] >= st and rr[3] <= sb:
                    rows_in_view += 1

    return {
        "found": True,
        "candidate_count": 1,
        "scroll_hwnd": h_scroll,
        "other_vscroll": other_v,
        "any_hscroll": any_h,
        "toggles_in_view": toggles_in_view,
        "toggles_total": toggles_total,
        "rows_in_view": rows_in_view,
        "rows_total": len(row_hwnds),
    }


def run_test_fullscreen_pass(exe_path, work_dir, data_dir, profile_cfg, profile_label, username, password):
    with open(os.path.join(data_dir, "station_fullscreen.cfg"), "w", encoding="utf-8") as f:
        f.write("fullscreen\n")
    with open(os.path.join(data_dir, "station_profile.cfg"), "w", encoding="utf-8") as f:
        f.write(f"{profile_cfg}\n")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = data_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    rows = []
    mon_info = None
    try:
        hwnd_login, _ = wait_window(proc.pid, "Iniciar Sesión", timeout=12.0)
        l_ctrls = enum_children(hwnd_login)
        l_edits = [h for (h, cls, _t) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, _t) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], username)
        set_text(l_edits[1], password)
        time.sleep(0.2)
        post_click(l_btns[0])

        hwnd_main, _ = wait_window(proc.pid, "Cafetería ITCH II", exclude=["Primer Inicio", "Iniciar Sesión"], timeout=12.0)
        time.sleep(0.6)

        style = user32.GetWindowLongW(hwnd_main, GWL_STYLE)
        assert (style & WS_CAPTION) == 0, f"[{profile_label}] La ventana no arrancó en pantalla completa (WS_CAPTION activo)"
        mon_w, mon_h, _mon_rc = get_monitor_size(hwnd_main)
        scale = get_window_scale(hwnd_main)
        dpi = get_window_dpi(hwnd_main)
        mon_info = (mon_w, mon_h, dpi, scale)

        def snap(slug):
            time.sleep(0.25)
            img, w_rc = capture_window_img(hwnd_main, None)
            m = measure_view(hwnd_main, img, w_rc, profile_label, slug, scale)
            c_hwnd, _ = find_content_container(hwnd_main)
            m["lv_metrics"] = inspect_listviews(c_hwnd, scale)
            if slug == "roles":
                m["roles_info"] = inspect_roles_scroll_perms(c_hwnd)
            rows.append(m)

        iterate_all_17_screens(hwnd_main, snap, add_cart_item=True)

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=6.0)
        return rows, mon_info
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except Exception:
                proc.kill()
                proc.wait(timeout=2.0)


def run_test_windowed_pass(exe_path, work_dir, data_dir, profile_cfg, profile_label, username, password):
    """Pasada en ventana normal (1200x720 y 960x600) recorriendo las 17 subsecciones + comprobaciones específicas de Roles."""
    with open(os.path.join(data_dir, "station_fullscreen.cfg"), "w", encoding="utf-8") as f:
        f.write("windowed\n")
    with open(os.path.join(data_dir, "station_profile.cfg"), "w", encoding="utf-8") as f:
        f.write(f"{profile_cfg}\n")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = data_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    rows = []
    try:
        hwnd_login, _ = wait_window(proc.pid, "Iniciar Sesión", timeout=12.0)
        l_ctrls = enum_children(hwnd_login)
        l_edits = [h for (h, cls, _t) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, _t) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], username)
        set_text(l_edits[1], password)
        time.sleep(0.2)
        post_click(l_btns[0])

        hwnd_main, _ = wait_window(proc.pid, "Cafetería ITCH II", exclude=["Primer Inicio", "Iniciar Sesión"], timeout=12.0)
        time.sleep(0.5)

        first_size = True
        for w_log, h_log in [(1200, 720), (960, 600)]:
            set_window_logical_size(hwnd_main, w_log, h_log)
            time.sleep(0.35)

            def measure_win(slug):
                time.sleep(0.25)
                c_hwnd, _ = find_content_container(hwnd_main)
                has_h, has_v = detect_panel_scrollbars(c_hwnd)
                txt_clip = detect_clipped_static_texts(c_hwnd)
                row = {
                    "profile": profile_label,
                    "size": f"{w_log}x{h_log}",
                    "screen": slug,
                    "hscroll": "SI" if has_h else "NO",
                    "vscroll": "SI" if has_v else "NO",
                    "texto_cortado": txt_clip,
                }
                if slug == "roles" and w_log == 1200 and profile_label == "Tactil":
                    row["roles_info"] = inspect_roles_scroll_perms(c_hwnd)
                    # Verificar además con lbl_form_feedback_ visible (guardar con nombre vacío):
                    # no puede aparecer barra vertical de página ni bajar de 4 filas de permisos.
                    click_button_text(hwnd_main, "Nuevo rol", exact=True)
                    time.sleep(0.25)
                    click_button_text(hwnd_main, "Guardar", exact=True)
                    time.sleep(0.25)
                    c_fb, _ = find_content_container(hwnd_main)
                    fb_visible = any(
                        cls.lower() == "static"
                        and user32.IsWindowVisible(h)
                        and "nombre del rol es requerido" in txt.lower()
                        for (h, cls, txt) in enum_children(c_fb)
                    )
                    fb_roles = inspect_roles_scroll_perms(c_fb)
                    fb_clip = detect_clipped_static_texts(c_fb)
                    row["roles_feedback_check"] = {
                        "feedback_visible": fb_visible,
                        "roles_info": fb_roles,
                        "texto_cortado": fb_clip,
                    }
                    # Restaurar selección del primer rol para dejar estado limpio
                    lvs = wait_content_list(hwnd_main, timeout=2.0)
                    if lvs:
                        select_list_row(lvs[0], 0, settle=0.2)

                rows.append(row)

                if w_log == 1200 and slug == "configuracion":
                    click_button_text(hwnd_main, "Usar pantalla completa", exact=True)
                    time.sleep(0.45)
                    c_fs, _ = find_content_container(hwnd_main)
                    fs_h, fs_v = detect_panel_scrollbars(c_fs)
                    fs_txt = detect_clipped_static_texts(c_fs)
                    rows.append({
                        "profile": profile_label,
                        "size": "toggle_fullscreen",
                        "screen": "configuracion",
                        "hscroll": "SI" if fs_h else "NO",
                        "vscroll": "SI" if fs_v else "NO",
                        "texto_cortado": fs_txt,
                    })
                    click_button_text(hwnd_main, "Salir de pantalla completa", exact=True)
                    time.sleep(0.4)

            iterate_all_17_screens(hwnd_main, measure_win, add_cart_item=first_size)
            first_size = False

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=6.0)
        return rows
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except Exception:
                proc.kill()
                proc.wait(timeout=2.0)


def evaluate_checks(fs_rows, win_rows):
    """Evalúa todas las aserciones sobre las filas medidas y devuelve (failures, warnings, matched_known)."""
    failures = []
    warnings = []
    matched_known = set()

    def check_metric(screen, profile, size_label, metric, ok, fail_msg):
        key = (screen, profile, size_label, metric)
        known = KNOWN_EXCEPTIONS.get(key)
        if ok:
            if known is not None:
                matched_known.add(key)
                warnings.append(
                    f"[AVISO] Excepción conocida ({screen}, {profile}, {size_label}, {metric}, bloque={known['bloque']}) "
                    f"ya cumple, quitar de la lista."
                )
        else:
            if known is not None:
                matched_known.add(key)
            else:
                failures.append(fail_msg)

    # 1. Verificación en pantalla completa
    seen_fs = set()
    for m in fs_rows:
        screen = m["screen"]
        profile = m["profile"]
        seen_fs.add((screen, profile))
        assert screen in SCREEN_STATES, f"Pantalla no declarada en SCREEN_STATES: {screen}"
        estado = SCREEN_STATES[screen]["estado"]
        size_label = "fullscreen"

        check_metric(
            screen, profile, size_label, "union_w",
            m["union_w_pct"] >= MIN_UNION_W_PCT,
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=cobertura_ancho "
            f"medida={m['union_w_pct']:.1f}% umbral>={MIN_UNION_W_PCT:.1f}%",
        )
        check_metric(
            screen, profile, size_label, "union_h",
            m["union_h_pct"] >= MIN_UNION_H_PCT,
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=cobertura_alto "
            f"medida={m['union_h_pct']:.1f}% umbral>={MIN_UNION_H_PCT:.1f}%",
        )
        check_metric(
            screen, profile, size_label, "gap_h",
            m["max_h_gap_pct"] <= MAX_H_GAP_PCT,
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=franja_horizontal "
            f"medida={m['max_h_gap_pct']:.1f}% ({m['max_h_gap']}px) umbral<={MAX_H_GAP_PCT:.1f}%",
        )
        if estado != "excepcion":
            check_metric(
                screen, profile, size_label, "gap_v",
                m["max_v_gap_pct"] <= MAX_V_GAP_PCT,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=franja_vertical "
                f"medida={m['max_v_gap_pct']:.1f}% ({m['max_v_gap']}px) umbral<={MAX_V_GAP_PCT:.1f}%",
            )

        if screen == "roles":
            # Excepción acotada únicamente a la ventana scroll_perms_ identificada de forma específica
            # (equivalente interno a un SysListView32 de 22 permisos). Cualquier otra barra vertical falla.
            rinfo = m.get("roles_info", {})
            check_metric(
                screen, profile, size_label, "vscroll",
                rinfo.get("found", False) and not rinfo.get("other_vscroll", True),
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_vertical_pagina "
                f"scroll_perms_encontrado={rinfo.get('found')} otra_vscroll={rinfo.get('other_vscroll')}",
            )
            check_metric(
                screen, profile, size_label, "hscroll",
                not rinfo.get("any_hscroll", True),
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_horizontal "
                f"medida=SI umbral=NO",
            )
            in_view = rinfo.get("toggles_in_view", 0)
            total_t = rinfo.get("toggles_total", 0)
            check_metric(
                screen, profile, size_label, "roles_toggles_in_view",
                total_t == ROLES_EXPECTED_TOTAL_TOGGLES and in_view >= ROLES_MIN_FULLSCREEN_TOGGLES,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=interruptores_visibles "
                f"medida={in_view}/{total_t} umbral>={ROLES_MIN_FULLSCREEN_TOGGLES}/{ROLES_EXPECTED_TOTAL_TOGGLES}",
            )
        else:
            check_metric(
                screen, profile, size_label, "vscroll",
                m["vscroll"] == "NO",
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_vertical "
                f"medida=SI umbral=NO",
            )
            check_metric(
                screen, profile, size_label, "hscroll",
                m["hscroll"] == "NO",
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_horizontal "
                f"medida=SI umbral=NO",
            )

        check_metric(
            screen, profile, size_label, "texto_cortado",
            m["texto_cortado"] == "-",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=texto_cortado "
            f"medida={m['texto_cortado']} umbral=-",
        )

        # Criterio (1): columnas de cada lista llenan >= 95 % sin WS_HSCROLL
        lv_metrics = m.get("lv_metrics", [])
        if lv_metrics:
            bad_fill = [
                f"cols={lv['fill_pct']:.1f}%(hscroll={lv['has_hscroll']})"
                for lv in lv_metrics
                if lv["fill_pct"] < MIN_LIST_COLS_FILL_PCT or lv["has_hscroll"]
            ]
            check_metric(
                screen, profile, size_label, "lv_cols_fill",
                len(bad_fill) == 0,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=llenado_columnas "
                f"medida={'; '.join(bad_fill)} umbral>={MIN_LIST_COLS_FILL_PCT:.1f}% sin WS_HSCROLL",
            )

            # Criterio (2): ninguna columna recortada mientras otra en la misma lista tiene >= 40 px lógicos de sobra
            bad_clip_sobra = [
                f"CLIP[{','.join(lv['clipped_cols'])}]/SOBRA[{','.join(lv['spare_cols'])}](umbral={lv['spare_thr_px']}px)"
                for lv in lv_metrics
                if lv["clipped_cols"] and lv["spare_cols"]
            ]
            check_metric(
                screen, profile, size_label, "lv_clip_sobra",
                len(bad_clip_sobra) == 0,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=columna_recortada_con_sobra "
                f"medida={'; '.join(bad_clip_sobra)}",
            )

    # 2. Verificación en ventana normal (1200x720 y 960x600)
    seen_win = set()
    for w in win_rows:
        screen = w["screen"]
        profile = w["profile"]
        size_label = w["size"]
        seen_win.add((screen, profile, size_label))
        assert screen in SCREEN_STATES, f"Pantalla no declarada en SCREEN_STATES: {screen}"

        check_metric(
            screen, profile, size_label, "hscroll",
            w["hscroll"] == "NO",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_horizontal "
            f"medida=SI umbral=NO",
        )
        if size_label == "toggle_fullscreen":
            check_metric(
                screen, profile, size_label, "vscroll",
                w["vscroll"] == "NO",
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_vertical "
                f"medida=SI umbral=NO",
            )
        check_metric(
            screen, profile, size_label, "texto_cortado",
            w["texto_cortado"] == "-",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=texto_cortado "
            f"medida={w['texto_cortado']} umbral=-",
        )

        # Criterio (3) + Ajuste 3: Roles en Táctil 1200x720 (>= 4 filas enteras en reposo y con mensaje de validación)
        if screen == "roles" and profile == "Tactil" and size_label == "1200x720":
            rinfo = w.get("roles_info", {})
            check_metric(
                screen, profile, size_label, "roles_page_vscroll",
                rinfo.get("found", False) and not rinfo.get("other_vscroll", True),
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_vertical_pagina "
                f"scroll_perms_encontrado={rinfo.get('found')} otra_vscroll={rinfo.get('other_vscroll')}",
            )
            rows_vis = rinfo.get("rows_in_view", 0)
            check_metric(
                screen, profile, size_label, "roles_rows_in_view",
                rows_vis >= ROLES_MIN_TOUCH_1200_ROWS,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=filas_permisos_visibles "
                f"medida={rows_vis} umbral>={ROLES_MIN_TOUCH_1200_ROWS}",
            )

            fb = w.get("roles_feedback_check", {})
            fb_rinfo = fb.get("roles_info", {})
            check_metric(
                screen, profile, size_label, "roles_feedback_visible",
                fb.get("feedback_visible", False),
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=feedback_validacion_visible "
                f"medida=NO umbral=SI",
            )
            check_metric(
                screen, profile, size_label, "roles_feedback_page_vscroll",
                fb_rinfo.get("found", False) and not fb_rinfo.get("other_vscroll", True) and not fb_rinfo.get("any_hscroll", True),
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=scroll_con_feedback "
                f"otra_vscroll={fb_rinfo.get('other_vscroll')} hscroll={fb_rinfo.get('any_hscroll')}",
            )
            check_metric(
                screen, profile, size_label, "roles_feedback_texto_cortado",
                fb.get("texto_cortado") == "-",
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=texto_cortado_con_feedback "
                f"medida={fb.get('texto_cortado')} umbral=-",
            )
            fb_rows_vis = fb_rinfo.get("rows_in_view", 0)
            check_metric(
                screen, profile, size_label, "roles_feedback_rows_in_view",
                fb_rows_vis >= ROLES_MIN_TOUCH_1200_ROWS,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=filas_permisos_con_feedback "
                f"medida={fb_rows_vis} umbral>={ROLES_MIN_TOUCH_1200_ROWS}",
            )

    # Verificar que se cubrieron las 17 pantallas en todos los modos
    expected_screens = set(SCREEN_STATES.keys())
    for prof in ("Estandar", "Tactil"):
        missing_fs = expected_screens - {s for (s, p) in seen_fs if p == prof}
        if missing_fs:
            failures.append(f"[FALLO] Faltaron pantallas en fullscreen [{prof}]: {sorted(missing_fs)}")
        for sz in ("1200x720", "960x600"):
            missing_w = expected_screens - {s for (s, p, z) in seen_win if p == prof and z == sz}
            if missing_w:
                failures.append(f"[FALLO] Faltaron pantallas en ventana {sz} [{prof}]: {sorted(missing_w)}")

    return failures, warnings, matched_known


def main():
    t0 = time.time()
    validate_config_integrity()
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    work_dir = os.path.join(repo_root, "build_release")
    exe_path = os.path.join(work_dir, "app_desktop.exe")
    assert os.path.exists(exe_path), f"No se encontró el ejecutable Release: {exe_path}"

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_t16_layout_")
    username = "admin_t16"
    password = "AdminT16Layout2026!"

    try:
        bootstrap_isolated_db(exe_path, work_dir, temp_dir, username, password)

        rows_std, mon_std = run_test_fullscreen_pass(
            exe_path, work_dir, temp_dir, "standard", "Estandar", username, password
        )
        rows_tch, _mon_tch = run_test_fullscreen_pass(
            exe_path, work_dir, temp_dir, "touch", "Tactil", username, password
        )
        win_std = run_test_windowed_pass(
            exe_path, work_dir, temp_dir, "standard", "Estandar", username, password
        )
        win_tch = run_test_windowed_pass(
            exe_path, work_dir, temp_dir, "touch", "Tactil", username, password
        )

        mon_w, mon_h, dpi, _scale = mon_std
        print(
            f"=== T16 PRUEBA PERMANENTE DE PANTALLA COMPLETA Y VENTANA NORMAL "
            f"(Monitor: {mon_w}x{mon_h} px | {format_dpi_info(dpi)}) ===",
            flush=True,
        )
        print(
            f"Pantallas medidas: {len(rows_std) + len(rows_tch)} fullscreen (17x2) + "
            f"{len(win_std) + len(win_tch)} ventana (17x2x2 + toggle_fullscreen).",
            flush=True,
        )

        # Resumen de Roles para trazabilidad directa
        for m in rows_std + rows_tch:
            if m["screen"] == "roles":
                ri = m.get("roles_info", {})
                print(
                    f"  [ROLES fullscreen {m['profile']}] toggles_visibles={ri.get('toggles_in_view')}/{ri.get('toggles_total')} "
                    f"otra_vscroll={ri.get('other_vscroll')} hscroll={ri.get('any_hscroll')}",
                    flush=True,
                )
        for w in win_tch:
            if w["screen"] == "roles" and w["size"] == "1200x720":
                ri = w.get("roles_info", {})
                fb = w.get("roles_feedback_check", {})
                fbi = fb.get("roles_info", {})
                print(
                    f"  [ROLES 1200x720 Tactil] filas_visibles={ri.get('rows_in_view')} | "
                    f"con_feedback: visible={fb.get('feedback_visible')} filas_visibles={fbi.get('rows_in_view')} "
                    f"otra_vscroll={fbi.get('other_vscroll')} txt={fb.get('texto_cortado')}",
                    flush=True,
                )

        failures, warnings, matched_known = evaluate_checks(rows_std + rows_tch, win_std + win_tch)

        for w_msg in warnings:
            print(w_msg, flush=True)

        elapsed = time.time() - t0
        if failures:
            print(f"\n[ERROR] Se detectaron {len(failures)} fallo(s) de layout:", flush=True)
            for f_msg in failures:
                print(f"  {f_msg}", flush=True)
            print(f"\n=== PRUEBA T16 FALLIDA en {elapsed:.1f}s ===", flush=True)
            sys.exit(1)

        print(
            f"[OK] Todas las pantallas terminadas ({sum(1 for v in SCREEN_STATES.values() if v['estado'] == 'terminada')}), "
            f"excepción aprobada (1) e invariantes vigentes de pendientes pasaron "
            f"(excepciones conocidas activas={len(matched_known) - len(warnings)}, avisos={len(warnings)}).",
            flush=True,
        )
        print(f"=== PRUEBA T16 EXITOSA en {elapsed:.1f}s ===", flush=True)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
