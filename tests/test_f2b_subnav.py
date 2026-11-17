#!/usr/bin/env python3
"""
test_f2b_subnav.py - Verificación rigurosa de la barra de subnavegación (Tarea F2b).

Comprobaciones exigidas:
1. Comprobar con tasklist que no haya app_desktop.exe corriendo (espera 30 s si lo hay).
2. Base en directorio temporal aislado y ejecutable real, tanto en Release como en Debug.
3. Alta inicial del Dueño.
4. Recorrer las 4 secciones: Punto de Venta, Inventario, Caja y Reportes, BackOffice.
5. Al cambiar de sección, la primera subsección debe quedar resaltada (fondo activo) y las demás inactivas.
6. Hacer clic en cada subsección visible para el Dueño:
   - Medir el color de fondo de cada botón de la subnav leyendo píxeles (GetPixel):
   - Exactamente un botón con el fondo activo (Theme::ColorSurface #FFFFFF) y es el pulsado.
   - Todos los demás botones con fondo inactivo (Theme::ColorBorderSubtle #E2E8F0).
   - Cobertura explícita de: 'Catálogo visual', 'Bitácora' y 'Configuración'.
7. Comprobar que 'Servidor de respaldos' no aparece para el Dueño en ningún momento.
8. Repetir en perfil Estándar y perfil Touch.
"""

import ctypes
from ctypes import wintypes
import time
import subprocess
import os
import sys
import tempfile
import shutil

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

from gui_dpi import (
    user32,
    gdi32,
    WM_SETTEXT,
    WM_CLOSE,
    BM_CLICK,
    GWL_STYLE,
    SWP_NOMOVE,
    SWP_NOZORDER,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_scale,
    get_rect,
    get_client_size,
    measure_text_extent,
    to_px,
    min_px,
    max_px,
    format_dpi_info,
)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.GetPixel.restype = wintypes.COLORREF

def ensure_no_app_running():
    for _ in range(3):
        res = subprocess.run(["tasklist", "/NH", "/FO", "CSV", "/FI", "IMAGENAME eq app_desktop.exe"],
                             capture_output=True, text=True)
        if any("app_desktop.exe" in line.lower() for line in res.stdout.splitlines()):
            print("[INFO] app_desktop.exe detectado en ejecución. Esperando 30 s...")
            time.sleep(30)
        else:
            return
    res = subprocess.run(["tasklist", "/NH", "/FO", "CSV", "/FI", "IMAGENAME eq app_desktop.exe"],
                         capture_output=True, text=True)
    if any("app_desktop.exe" in line.lower() for line in res.stdout.splitlines()):
        raise RuntimeError("app_desktop.exe sigue ejecutándose tras espera de 30 s")

def get_window_pid(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value

def find_windows_by_pid(target_pid, title_substring="", exclude_substrings=None):
    found = []
    def enum_cb(hwnd, _):
        if user32.IsWindowVisible(hwnd) and get_window_pid(hwnd) == target_pid:
            length = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            text = buf.value
            if not title_substring or title_substring.lower() in text.lower():
                if exclude_substrings and any(ex.lower() in text.lower() for ex in exclude_substrings):
                    return True
                found.append((hwnd, text))
        return True
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return found

def wait_for_window_by_pid(target_pid, title_substring, timeout=12.0, exclude_substrings=None, step_name=""):
    start = time.time()
    while time.time() - start < timeout:
        wins = find_windows_by_pid(target_pid, title_substring, exclude_substrings)
        if wins:
            return wins[0]
        time.sleep(0.2)
    all_wins = find_windows_by_pid(target_pid, "")
    print(f"[TIMEOUT {timeout}s] Esperando '{title_substring}' en PID {target_pid} ({step_name}). Visibles: {[t for _, t in all_wins]}")
    return None, ""

def get_child_controls(parent_hwnd):
    controls = []
    def enum_child(hwnd, _):
        cls_buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls_buf, 256)
        length = user32.GetWindowTextLengthW(hwnd)
        txt_buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, txt_buf, length + 1)
        controls.append((hwnd, cls_buf.value, txt_buf.value))
        return True
    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(enum_child), 0)
    return controls

def set_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(buf))

def send_click_button(hwnd):
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)

def measure_button_bg_rgb(h_btn):
    """Mide el color de fondo RGB de un botón leyendo píxeles del control o de pantalla."""
    r = wintypes.RECT()
    user32.GetClientRect(h_btn, ctypes.byref(r))
    cw = r.right - r.left
    ch = r.bottom - r.top
    scale = get_window_scale(h_btn)
    ox8 = to_px(8, scale)
    oy6 = to_px(6, scale)
    ox10 = to_px(10, scale)
    oy8 = to_px(8, scale)

    # Puntos de muestra en la zona de fondo (esquinas superiores e inferiores, lejos del texto centrado)
    pts = [
        (ox8, oy6),
        (cw - ox8, oy6),
        (ox10, oy8),
        (cw - ox10, oy8),
        (ox8, ch - oy8),
        (cw - ox8, ch - oy8),
    ]

    # Intentar con DC de cliente
    hdc_client = user32.GetDC(h_btn)
    if hdc_client:
        for x, y in pts:
            if 0 < x < cw and 0 < y < ch:
                val = gdi32.GetPixel(hdc_client, x, y)
                if val != 0xFFFFFFFF and val >= 0:
                    user32.ReleaseDC(h_btn, hdc_client)
                    return (val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF)
        user32.ReleaseDC(h_btn, hdc_client)

    # Si no, intentar con DC de pantalla convirtiendo coordenadas
    hdc_screen = user32.GetDC(0)
    if hdc_screen:
        for x, y in pts:
            if 0 < x < cw and 0 < y < ch:
                pt = wintypes.POINT(x, y)
                user32.ClientToScreen(h_btn, ctypes.byref(pt))
                val = gdi32.GetPixel(hdc_screen, pt.x, pt.y)
                if val != 0xFFFFFFFF and val >= 0:
                    user32.ReleaseDC(0, hdc_screen)
                    return (val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF)
        user32.ReleaseDC(0, hdc_screen)

    return None

def is_active_bg(rgb):
    """Theme::ColorSurface es #FFFFFF (255, 255, 255). Activo si es blanco puro o casi puro."""
    if not rgb:
        return False
    r, g, b = rgb
    return r >= 248 and g >= 248 and b >= 248

def is_inactive_bg(rgb):
    """Theme::ColorBorderSubtle es #E2E8F0 (226, 232, 240). Inactivo si tiene tono gris suave."""
    if not rgb:
        return False
    r, g, b = rgb
    return (not is_active_bg(rgb)) and (215 <= r <= 238) and (220 <= g <= 240) and (230 <= b <= 250)

def run_f2b_test_for_binary(exe_path, repo_root, config_name):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN BARRA DE SUBNAVEGACIÓN F2b [{config_name}]:")
    print(f"  Binario:  {exe_path}")
    print(f"=======================================================")

    ensure_no_app_running()

    test_data_dir = tempfile.mkdtemp(prefix=f"cafeteria_f2b_{config_name.lower()}_")
    env = os.environ.copy()
    env["PATH"] = os.path.dirname(exe_path) + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = test_data_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    p1 = None
    p2 = None
    try:
        # -------------------------------------------------------------
        # SESIÓN 1: Alta inicial del Dueño
        # -------------------------------------------------------------
        print("\n--- SESIÓN 1: Alta inicial del Dueño ---")
        p1 = subprocess.Popen([exe_path], cwd=repo_root, env=env)
        pid1 = p1.pid

        hwnd_onboarding, _ = wait_for_window_by_pid(pid1, "Primer Inicio", step_name="Onboarding")
        assert hwnd_onboarding is not None, "Diálogo de Primer Inicio no apareció"

        ctrls = get_child_controls(hwnd_onboarding)
        edits = [h for (h, cls, txt) in ctrls if cls.lower() == "edit"]
        btns = [h for (h, cls, txt) in ctrls if cls.lower() == "button"]
        assert len(edits) >= 5 and len(btns) >= 1, f"Controles de alta inicial no encontrados (edits: {len(edits)})"

        set_text(edits[0], "Administrador Dueño")
        set_text(edits[1], "admin_owner")
        set_text(edits[2], "admin@itch.edu.mx")
        set_text(edits[3], "AdminPass123!")
        set_text(edits[4], "AdminPass123!")
        time.sleep(0.3)
        send_click_button(btns[0])
        time.sleep(1.2)

        hwnd_main1, _ = wait_for_window_by_pid(
            pid1, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow tras Onboarding"
        )
        assert hwnd_main1 is not None, "MainWindow no abrió tras alta inicial"
        print("[OK] Alta inicial completada y MainWindow abierta.")

        user32.PostMessageW(hwnd_main1, WM_CLOSE, 0, 0)
        p1.wait(timeout=5.0)
        p1 = None
        print("[OK] Sesión 1 cerrada limpiamente.")

        # -------------------------------------------------------------
        # SESIÓN 2: Login Dueño y Recorrido de Subnavegación
        # -------------------------------------------------------------
        print("\n--- SESIÓN 2: Login Dueño y Verificación de Subnavegación ---")
        ensure_no_app_running()

        p2 = subprocess.Popen([exe_path], cwd=repo_root, env=env)
        pid2 = p2.pid

        hwnd_login, _ = wait_for_window_by_pid(pid2, "Iniciar Sesión", step_name="Login Dueño")
        assert hwnd_login is not None, "Diálogo de login no encontrado"

        login_ctrls = get_child_controls(hwnd_login)
        login_edits = [h for (h, cls, txt) in login_ctrls if cls.lower() == "edit"]
        login_btns = [h for (h, cls, txt) in login_ctrls if cls.lower() == "button"]
        set_text(login_edits[0], "admin_owner")
        set_text(login_edits[1], "AdminPass123!")
        time.sleep(0.3)
        send_click_button(login_btns[0])
        time.sleep(1.2)

        hwnd_main, _ = wait_for_window_by_pid(
            pid2, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow login Dueño"
        )
        assert hwnd_main is not None, "MainWindow no abrió tras login Dueño"
        user32.SetForegroundWindow(hwnd_main)
        time.sleep(0.5)

        # Definición de secciones y subsecciones esperadas para el Dueño
        SECTIONS_INFO = [
            ("Punto de Venta", ["Venta", "Catálogo visual"]),
            ("Inventario", ["Existencias", "Reabastecimiento", "Productos y recetas",
                            "Ingredientes", "Promociones y combos", "Barra"]),
            ("Caja y Reportes", ["Caja y turnos", "Bitácora", "Reportes"]),
            ("BackOffice", ["Usuarios y cuentas", "Roles y permisos", "Configuración", "Respaldos", "Actualizaciones"])
        ]

        def get_subnav_buttons(expected_subs):
            """Obtiene los botones pertenecientes a la barra de subnavegación comparando padres y etiquetas."""
            ctrls = get_child_controls(hwnd_main)
            btn_ctrls = [(h, txt) for (h, cls, txt) in ctrls if cls.lower() == "button"]

            # Comprobar que 'Servidor de respaldos' NUNCA aparece para el Dueño
            for h, txt in btn_ctrls:
                assert "Servidor de respaldos" not in txt, (
                    f"VIOLACIÓN DE PERMISOS: 'Servidor de respaldos' visible para Dueño (HWND {h})"
                )

            # Buscar botones cuyas etiquetas coincidan con las subsecciones esperadas
            matching = []
            for sub in expected_subs:
                btn_h = next((h for (h, t) in btn_ctrls if t == sub), None)
                assert btn_h is not None, f"Botón de subsección '{sub}' no encontrado en MainWindow"
                matching.append((sub, btn_h))

            # Todos los botones de subnav deben compartir el mismo padre (SubnavBar)
            parents = {user32.GetParent(h) for _, h in matching}
            assert len(parents) == 1, f"Botones de subnav no comparten el mismo contenedor padre: {parents}"

            return matching

        def verify_subnav_state(expected_subs, active_sub_name, context_desc):
            """Mide los píxeles de cada botón de la barra de subnav y comprueba que exactamente uno está activo."""
            user32.SetForegroundWindow(hwnd_main)
            time.sleep(0.3)
            sub_btns = get_subnav_buttons(expected_subs)

            active_found = []
            inactive_found = []

            for sub_name, h_btn in sub_btns:
                rgb = measure_button_bg_rgb(h_btn)
                assert rgb is not None, f"No se pudo medir el color de fondo de '{sub_name}' ({context_desc})"

                if is_active_bg(rgb):
                    active_found.append((sub_name, rgb))
                elif is_inactive_bg(rgb):
                    inactive_found.append((sub_name, rgb))
                else:
                    # Color no reconocido
                    raise AssertionError(
                        f"Botón '{sub_name}' tiene color inesperado RGB={rgb} ({context_desc})"
                    )

            # EXACTAMENTE un botón activo, y debe ser active_sub_name
            assert len(active_found) == 1, (
                f"[{context_desc}] Se esperaba exactamente 1 botón activo (para '{active_sub_name}'), "
                f"pero se encontraron {len(active_found)} activos: {active_found}. "
                f"Inactivos encontrados: {[s for s, _ in inactive_found]}"
            )

            detected_active_name, detected_rgb = active_found[0]
            assert detected_active_name == active_sub_name, (
                f"[{context_desc}] Botón activo incorrecto: esperado '{active_sub_name}', "
                f"detectado '{detected_active_name}' (RGB={detected_rgb})"
            )

            # Todos los demás inactivos
            expected_inactive_count = len(expected_subs) - 1
            assert len(inactive_found) == expected_inactive_count, (
                f"[{context_desc}] Se esperaban {expected_inactive_count} inactivos, "
                f"obtenidos {len(inactive_found)}: {[s for s, _ in inactive_found]}"
            )

            print(f"    [OK Píxel] '{active_sub_name}' activo RGB={detected_rgb}; {len(inactive_found)} botones inactivos correctamente.")

        # Recorrer en perfiles Estándar y Touch
        orig_wr = get_rect(hwnd_main)
        orig_w, orig_h = orig_wr[2] - orig_wr[0], orig_wr[3] - orig_wr[1]
        for profile_name in ["Estándar", "Touch"]:
            user32.SetWindowPos(hwnd_main, 0, 0, 0, orig_w, orig_h, SWP_NOMOVE | SWP_NOZORDER)
            user32.SendMessageW(hwnd_main, 0, 0, 0)
            time.sleep(0.25)
            print(f"\n--- Verificación en Perfil {profile_name} ---")

            if profile_name == "Touch":
                # Cambiar a perfil Touch mediante botón en HeaderPanel
                ctrls = get_child_controls(hwnd_main)
                btns = [(h, txt) for (h, cls, txt) in ctrls if cls.lower() == "button"]
                btn_touch = next((h for (h, t) in btns if t == "Touch"), None)
                assert btn_touch is not None, "Botón toggle a Touch no encontrado en header"
                send_click_button(btn_touch)
                time.sleep(0.8)
                print("[OK] Cambiado a perfil Touch.")

            for sec_name, sub_list in SECTIONS_INFO:
                print(f"\n  [Sección: {sec_name}]")
                ctrls = get_child_controls(hwnd_main)
                btns = [(h, txt) for (h, cls, txt) in ctrls if cls.lower() == "button"]
                btn_sec = next((h for (h, t) in btns if t == sec_name), None)
                assert btn_sec is not None, f"Botón de sección '{sec_name}' no encontrado en sidebar"
                send_click_button(btn_sec)
                time.sleep(0.5)

                # Regla: al cambiar de sección, la primera subsección queda resaltada
                first_sub = sub_list[0]
                verify_subnav_state(sub_list, first_sub, f"{profile_name} - Cambio a sección {sec_name}")

                # Recorrer y hacer clic en cada subsección de la sección
                for sub_name in sub_list:
                    sub_btns = get_subnav_buttons(sub_list)
                    target_btn = next((h for (s, h) in sub_btns if s == sub_name), None)
                    assert target_btn is not None, f"Botón para '{sub_name}' no encontrado"

                    send_click_button(target_btn)
                    time.sleep(0.4)

                    verify_subnav_state(sub_list, sub_name, f"{profile_name} - Clic en {sub_name}")

            # Verificación geométrica de las 4 secciones en 1200x720 y 960x600 (GetWindowRect + GetTextExtent)
            is_touch_prof = (profile_name == "Touch")
            scale = get_window_scale(hwnd_main)
            for req_cw, req_ch in [(1200, 720), (960, 600)]:
                wr = get_rect(hwnd_main)
                cw_cur, ch_cur = get_client_size(hwnd_main)
                user32.SetWindowPos(
                    hwnd_main, 0, 0, 0,
                    to_px(req_cw, scale) + (wr[2] - wr[0] - cw_cur),
                    to_px(req_ch, scale) + (wr[3] - wr[1] - ch_cur),
                    SWP_NOMOVE | SWP_NOZORDER,
                )
                user32.SendMessageW(hwnd_main, 0, 0, 0)
                time.sleep(0.35)

                print(f"\n  [Geometría {profile_name} {req_cw}x{req_ch}]")
                for sec_name, sub_list in SECTIONS_INFO:
                    ctrls = get_child_controls(hwnd_main)
                    btn_sec = next((h for (h, cls, t) in ctrls if cls.lower() == "button" and t == sec_name), None)
                    assert btn_sec is not None, f"Botón '{sec_name}' no encontrado"
                    send_click_button(btn_sec)
                    time.sleep(0.35)

                    sub_btns = get_subnav_buttons(sub_list)
                    hwnd_track = user32.GetParent(sub_btns[0][1])
                    hwnd_scroll = user32.GetParent(hwnd_track)
                    hwnd_subnav = user32.GetParent(hwnd_scroll)
                    sn_l, sn_t, sn_r, sn_b = get_rect(hwnd_subnav)
                    sc_l, sc_t, sc_r, sc_b = get_rect(hwnd_scroll)
                    bar_h = sn_b - sn_t
                    scroll_w = sc_r - sc_l

                    # SubnavBar siempre en una sola fila (nunca roba altura al contenido)
                    max_bar_h = max_px(58 if is_touch_prof else 46, scale)
                    assert bar_h <= max_bar_h, (
                        f"[{profile_name} {req_cw}x{req_ch} {sec_name}] SubnavBar creció a {bar_h}px > {max_bar_h}px"
                    )

                    # Medir cada botón de pestaña con GetWindowRect + GetTextExtent
                    prev_r = None
                    first_t = None
                    total_btns_w = 0
                    for idx_b, (sub_name, h_btn) in enumerate(sub_btns):
                        bl, bt, br, bb = get_rect(h_btn)
                        bw, bh = br - bl, bb - bt
                        assert bh >= min_px(48 if is_touch_prof else 36, scale), (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Pestaña '{sub_name}' altura {bh}px baja"
                        )
                        assert bt >= sn_t - 1 and bb <= sn_b + 1, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Pestaña '{sub_name}' fuera verticalmente de SubnavBar"
                        )
                        if first_t is None:
                            first_t = bt
                        else:
                            assert abs(bt - first_t) <= 2, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Pestaña '{sub_name}' saltó de fila (y={bt} vs {first_t})"
                            )
                        if prev_r is not None:
                            assert bl >= prev_r - 1, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '{sub_name}' solapa anterior"
                            )
                        prev_r = br
                        tw, _ = measure_text_extent(h_btn, sub_name)
                        cw_btn, _ = get_client_size(h_btn)
                        assert cw_btn >= tw + min_px(10, scale), (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Texto '{sub_name}' recortado: cw={cw_btn} < tw={tw}+pad"
                        )
                        total_btns_w += bw + (0 if idx_b + 1 == len(sub_btns) else to_px(4, scale))

                    # Flechas '<' y '>' hijas directas de hwnd_subnav
                    subnav_children = get_child_controls(hwnd_subnav)
                    btn_prev = next((h for (h, cls, t) in subnav_children if cls.lower() == "button" and t == "<" and user32.GetParent(h) == hwnd_subnav), None)
                    btn_next = next((h for (h, cls, t) in subnav_children if cls.lower() == "button" and t == ">" and user32.GetParent(h) == hwnd_subnav), None)
                    assert btn_prev is not None and btn_next is not None, "Faltan botones de flecha en SubnavBar"
                    prev_vis = bool(user32.IsWindowVisible(btn_prev))
                    next_vis = bool(user32.IsWindowVisible(btn_next))

                    # Determinar si caben en el ancho interior de SubnavBar (entre márgenes)
                    inner_w = (sn_r - sn_l) - 2 * to_px(12 if is_touch_prof else 8, scale)
                    if total_btns_w <= inner_w:
                        assert not prev_vis and not next_vis, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Flechas visibles innecesariamente: btns={total_btns_w}px <= inner={inner_w}px"
                        )
                        for sub_name, h_btn in sub_btns:
                            bl, _, br, _ = get_rect(h_btn)
                            assert bl >= sc_l - 1 and br <= sc_r + 1, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '{sub_name}' desborda SubnavBar ({bl}..{br} vs {sc_l}..{sc_r})"
                            )
                        print(f"    [OK] {sec_name}: 1 fila sin flechas, bar_h={bar_h}px, btns={total_btns_w}px <= inner={inner_w}px (holgura={inner_w - total_btns_w}px)")
                    else:
                        assert prev_vis and next_vis, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Esperadas flechas visibles al desbordar 1 fila (btns={total_btns_w}px > inner={inner_w}px)"
                        )
                        for a_name, h_arr in [("<", btn_prev), (">", btn_next)]:
                            al, at, ar, ab = get_rect(h_arr)
                            aw, ah = ar - al, ab - at
                            min_arr = min_px(48 if is_touch_prof else 36, scale)
                            assert aw >= min_arr and ah >= min_arr, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Flecha '{a_name}' bajo mínimo: {aw}x{ah}px < {min_arr}px"
                            )
                            assert al >= sn_l - 1 and ar <= sn_r + 1 and at >= sn_t - 1 and ab <= sn_b + 1, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Flecha '{a_name}' fuera de SubnavBar"
                            )
                        # Probar desplazamiento con '>' (una pestaña por toque) hasta ver completa la última
                        max_taps = len(sub_btns)
                        next_taps = 0
                        while True:
                            last_l, _, last_r, _ = get_rect(sub_btns[-1][1])
                            if last_l >= sc_l - 1 and last_r <= sc_r + 1:
                                break
                            assert next_taps < max_taps, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Tras {next_taps} toques en '>' la última pestaña '{sub_btns[-1][0]}' sigue fuera ({last_l}..{last_r} vs {sc_l}..{sc_r})"
                            )
                            assert user32.IsWindowEnabled(btn_next), (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '>' deshabilitado antes de mostrar '{sub_btns[-1][0]}' ({last_l}..{last_r} vs {sc_l}..{sc_r})"
                            )
                            prev_first_l = get_rect(sub_btns[0][1])[0]
                            send_click_button(btn_next)
                            time.sleep(0.2)
                            next_taps += 1
                            new_first_l = get_rect(sub_btns[0][1])[0]
                            assert new_first_l < prev_first_l, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Toque #{next_taps} en '>' no desplazó (first_l={prev_first_l} -> {new_first_l})"
                            )
                        assert next_taps >= 1, f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Esperado al menos 1 toque en '>'"
                        # Al final '>' ya no debe avanzar más
                        assert not user32.IsWindowEnabled(btn_next), (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '>' siguió habilitado tras mostrar la última pestaña"
                        )
                        end_first_l = get_rect(sub_btns[0][1])[0]
                        send_click_button(btn_next)
                        time.sleep(0.15)
                        assert get_rect(sub_btns[0][1])[0] == end_first_l, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '>' siguió desplazando después del final"
                        )

                        # Regresar con '<' hasta mostrar completa la primera
                        prev_taps = 0
                        while True:
                            first_l, _, first_r, _ = get_rect(sub_btns[0][1])
                            if first_l >= sc_l - 1 and first_r <= sc_r + 1:
                                break
                            assert prev_taps < max_taps, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Tras {prev_taps} toques en '<' la primera pestaña '{sub_btns[0][0]}' sigue fuera ({first_l}..{first_r} vs {sc_l}..{sc_r})"
                            )
                            assert user32.IsWindowEnabled(btn_prev), (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '<' deshabilitado antes de regresar a '{sub_btns[0][0]}'"
                            )
                            old_first_l = first_l
                            send_click_button(btn_prev)
                            time.sleep(0.2)
                            prev_taps += 1
                            assert get_rect(sub_btns[0][1])[0] > old_first_l, (
                                f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Toque #{prev_taps} en '<' no desplazó hacia la izquierda"
                            )
                        assert prev_taps >= 1 and not user32.IsWindowEnabled(btn_prev), (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] '<' siguió habilitado tras regresar al inicio"
                        )

                        # Al elegir una pestaña parcialmente oculta, ensure_active_visible debe traerla completa a la vista
                        partial_btn = next(
                            ((sn, hb) for (sn, hb) in sub_btns if get_rect(hb)[0] < sc_r and get_rect(hb)[2] > sc_r + 1),
                            None,
                        )
                        if partial_btn is None:
                            partial_btn = next(
                                ((sn, hb) for (sn, hb) in sub_btns if get_rect(hb)[2] > sc_r + 1),
                                None,
                            )
                        assert partial_btn is not None, f"[{profile_name} {req_cw}x{req_ch} {sec_name}] No se encontró pestaña oculta/parcial para probar ensure_active_visible"
                        p_name, p_hwnd = partial_btn
                        send_click_button(p_hwnd)
                        time.sleep(0.25)
                        pl, _, pr, _ = get_rect(p_hwnd)
                        assert pl >= sc_l - 1 and pr <= sc_r + 1, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Al elegir '{p_name}' parcialmente oculta, ensure_active_visible no la trajo completa ({pl}..{pr} vs {sc_l}..{sc_r})"
                        )
                        # Y al volver a elegir la primera (ahora oculta por la izquierda), también vuelve completa a la vista
                        send_click_button(sub_btns[0][1])
                        time.sleep(0.25)
                        f0l, _, f0r, _ = get_rect(sub_btns[0][1])
                        assert f0l >= sc_l - 1 and f0r <= sc_r + 1, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Al elegir '{sub_btns[0][0]}' oculta a la izquierda, ensure_active_visible no la trajo completa ({f0l}..{f0r} vs {sc_l}..{sc_r})"
                        )
                        print(f"    [OK] {sec_name}: 1 fila con flechas '<'/'>', bar_h={bar_h}px, scroll={scroll_w}px, total_btns={total_btns_w}px, toques_='>'={next_taps}, toques_'<'={prev_taps} (desplazamiento + ensure_active_visible verificados)")

                    # En Inventario -> abrir Ingredientes y verificar botones visibles del contenido sin compresión
                    if sec_name == "Inventario":
                        btn_ing = next(h for s, h in sub_btns if s == "Ingredientes")
                        send_click_button(btn_ing)
                        time.sleep(0.35)
                        if prev_vis:
                            send_click_button(btn_prev)
                            time.sleep(0.2)

                    all_after = get_child_controls(hwnd_main)
                    content_btns = []
                    for h_cb, cls_cb, txt_cb in all_after:
                        if cls_cb.lower() != "button" or not user32.IsWindowVisible(h_cb):
                            continue
                        st_cb = user32.GetWindowLongW(h_cb, GWL_STYLE) & 0xF
                        if st_cb not in (0, 1, 0xB):  # Solo push buttons (no CheckBox/Radio/GroupBox)
                            continue
                        rc_cb = get_rect(h_cb)
                        if rc_cb[1] >= sn_b - 1 and rc_cb[0] >= sn_l - 5:
                            content_btns.append((h_cb, txt_cb))
                    min_content_bh = min_px(48 if is_touch_prof else 28, scale)
                    for h_cb, txt_cb in content_btns:
                        cl, ct, cr, cb = get_rect(h_cb)
                        cbw, cbh = cr - cl, cb - ct
                        if cbw <= 0 and cbh <= 0:
                            continue
                        assert cbh >= min_content_bh, (
                            f"[{profile_name} {req_cw}x{req_ch} {sec_name}] Control contenido '{txt_cb}' comprimido: h={cbh}px < {min_content_bh}px"
                        )
                    if sec_name == "Inventario":
                        h_save = next((h for h, t in content_btns if t == "Guardar"), None)
                        assert h_save is not None, f"[{profile_name} {req_cw}x{req_ch}] Botón 'Guardar' de Ingredientes no visible"
                        sv_l, sv_t, sv_r, sv_b = get_rect(h_save)
                        sv_h = sv_b - sv_t
                        min_save_h = min_px(48 if is_touch_prof else 36, scale)
                        assert sv_h >= min_save_h, (
                            f"[{profile_name} {req_cw}x{req_ch}] 'Guardar' comprimido: {sv_h}px < {min_save_h}px"
                        )

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        p2.wait(timeout=5.0)
        p2 = None
        print(f"\n>>> TODAS LAS PRUEBAS DE F2b PASARON EXITOSAMENTE EN [{config_name}] <<<")

    finally:
        for p in [p1, p2]:
            if p is not None and p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=2.0)
                except Exception:
                    p.kill()
        ensure_no_app_running()
        if os.path.exists(test_data_dir):
            try:
                shutil.rmtree(test_data_dir, ignore_errors=True)
            except Exception:
                pass

if __name__ == "__main__":
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {format_dpi_info(sys_dpi)}")
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    exe_release = os.path.join(repo_root, "build_release", "app_desktop.exe")
    exe_debug = os.path.join(repo_root, "build_debug", "app_desktop.exe")

    # Ejecutar en Release y Debug si ambos existen; si no, en el disponible
    configs = []
    if os.path.exists(exe_release):
        configs.append(("RELEASE", exe_release))
    if os.path.exists(exe_debug):
        configs.append(("DEBUG", exe_debug))

    if not configs:
        print("[ERROR] No se encontraron ejecutables en build_release ni build_debug")
        sys.exit(1)

    for cfg_name, exe_path in configs:
        run_f2b_test_for_binary(exe_path, repo_root, cfg_name)

    print("\n=================================================================")
    print("VERIFICACIÓN COMPLETA F2b: PRUEBA GUI FINALIZADA EXITOSAMENTE")
    print("=================================================================")
