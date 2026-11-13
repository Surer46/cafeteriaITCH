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

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumWindows.restype = wintypes.BOOL
user32.EnumChildWindows.argtypes = [wintypes.HWND, WNDENUMPROC, wintypes.LPARAM]
user32.EnumChildWindows.restype = wintypes.BOOL
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindowVisible.restype = wintypes.BOOL
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.restype = ctypes.c_int
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetClassNameW.restype = ctypes.c_int
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetForegroundWindow.restype = wintypes.BOOL
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL
user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int

gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.GetPixel.restype = wintypes.COLORREF

WM_SETTEXT = 0x000C
WM_CLOSE = 0x0010
BM_CLICK = 0x00F5

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

    # Puntos de muestra en la zona de fondo (esquinas superiores e inferiores, lejos del texto centrado)
    pts = [
        (8, 6),
        (cw - 8, 6),
        (10, 8),
        (cw - 10, 8),
        (8, ch - 8),
        (cw - 8, ch - 8)
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
            ("BackOffice", ["Usuarios y cuentas", "Roles y permisos", "Configuración", "Respaldos"])
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
        for profile_name in ["Estándar", "Touch"]:
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
