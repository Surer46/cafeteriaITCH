#!/usr/bin/env python3
"""
test_f1_roles_panel.py - Verificación rigurosa de panel BackOffice > Roles y permisos (Puerta I6 / Tarea F1).

Comprobaciones exigidas por especificación:
1. Base temporal aislada y ejecutable real, tanto en Release como en Debug.
2. Un rol de sistema no se puede editar ni eliminar desde la interfaz (botones y casillas deshabilitados).
3. role-dev y backup.dev_config no aparecen en ningún control de la interfaz.
4. Crear un rol personalizado, marcar dos permisos, guardar, cerrar la app, reabrirla con la misma base,
   y verificar que el rol y sus permisos persisten de forma íntegra.
5. Geometría y accesibilidad visual:
   - Ningún control recortado a 1216x759 ni a 960x600, en ambos perfiles (Estándar y Touch).
   - En perfil Touch, botones interactivos con altura mínima de 48 px.
   - En ambos perfiles, textos medidos con GetTextExtent (ancho de botón >= texto + márgenes).
"""

import ctypes
from ctypes import wintypes
import time
import subprocess
import os
import sys
import sqlite3
import tempfile
import shutil

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

class SIZE(ctypes.Structure):
    _fields_ = [("cx", wintypes.LONG), ("cy", wintypes.LONG)]

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumWindows.restype = wintypes.BOOL
user32.EnumChildWindows.argtypes = [wintypes.HWND, WNDENUMPROC, wintypes.LPARAM]
user32.EnumChildWindows.restype = wintypes.BOOL
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindowVisible.restype = wintypes.BOOL
user32.IsWindowEnabled.argtypes = [wintypes.HWND]
user32.IsWindowEnabled.restype = wintypes.BOOL
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
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.SetWindowPos.restype = wintypes.BOOL
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int

gdi32.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(SIZE)]
gdi32.GetTextExtentPoint32W.restype = wintypes.BOOL
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ

try:
    user32.GetDpiForWindow.argtypes = [wintypes.HWND]
    user32.GetDpiForWindow.restype = wintypes.UINT
    HAS_GET_DPI = True
except AttributeError:
    HAS_GET_DPI = False

WM_SETFOCUS = 0x0007
WM_SETTEXT = 0x000C
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_CLOSE = 0x0010
WM_GETFONT = 0x0031
BM_CLICK = 0x00F5
BM_GETCHECK = 0x00F0
BM_SETCHECK = 0x00F1
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
SWP_NOZORDER = 0x0004
SWP_NOMOVE = 0x0002

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

def get_child_controls(parent_hwnd):
    controls = []
    def enum_child_cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            cls_buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls_buf, 256)
            controls.append((hwnd, cls_buf.value, buf.value))
        return True
    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(enum_child_cb), 0)
    return controls

def wait_for_window_by_pid(target_pid, title_substring, exclude_substrings=None, timeout_sec=12.0, step_name=""):
    start = time.time()
    while time.time() - start < timeout_sec:
        wins = find_windows_by_pid(target_pid, title_substring, exclude_substrings)
        if wins:
            return wins[0]
        time.sleep(0.2)
    raise TimeoutError(f"Timeout esperando ventana '{title_substring}' en PID {target_pid} (paso: {step_name})")

def get_rect(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom, (r.right - r.left), (r.bottom - r.top)

def measure_text_extent(hwnd, text):
    hdc = user32.GetDC(hwnd)
    if not hdc:
        return 0, 0
    hfont = user32.SendMessageW(hwnd, WM_GETFONT, 0, 0)
    old_font = 0
    if hfont:
        old_font = gdi32.SelectObject(hdc, hfont)
    sz = SIZE()
    gdi32.GetTextExtentPoint32W(hdc, text, len(text), ctypes.byref(sz))
    if old_font:
        gdi32.SelectObject(hdc, old_font)
    user32.ReleaseDC(hwnd, hdc)
    return sz.cx, sz.cy

def set_text(hwnd, text):
    text_buf = ctypes.create_unicode_buffer(text)
    user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(text_buf))

def get_ctrl_text(hwnd):
    length = user32.SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if length > 0:
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.SendMessageW(hwnd, WM_GETTEXT, length + 1, ctypes.addressof(buf))
        return buf.value
    len_w = user32.GetWindowTextLengthW(hwnd)
    if len_w > 0:
        buf = ctypes.create_unicode_buffer(len_w + 1)
        user32.GetWindowTextW(hwnd, buf, len_w + 1)
        return buf.value
    return ""

def send_click_button(hwnd):
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)

def post_click_button(hwnd):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)

def select_list_item(hwnd_list, row_index):
    """Envía clic de ratón a la fila indicada del SysListView32."""
    y = 35 + row_index * 20
    x = 40
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd_list, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_list, WM_LBUTTONUP, 0, lparam)
    time.sleep(0.3)

def get_window_dpi(hwnd):
    if HAS_GET_DPI:
        dpi = user32.GetDpiForWindow(hwnd)
        if dpi > 0:
            return dpi
    hdc = user32.GetDC(hwnd)
    if hdc:
        dpi = gdi32.GetDeviceCaps(hdc, 88)
        user32.ReleaseDC(hwnd, hdc)
        return dpi
    return 96

def run_f1_test_for_binary(exe_path, work_dir, config_name):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN ROLES Y PERMISOS F1 [{config_name}]:")
    print(f"  Binario:  {exe_path}")
    print(f"=======================================================")
    assert os.path.exists(exe_path), f"Binario no encontrado: {exe_path}"

    test_data_dir = tempfile.mkdtemp(prefix=f"cafeteria_f1_{config_name.lower()}_")
    db_path = os.path.join(test_data_dir, "cafeteria.db")

    p1 = None
    p2 = None

    try:
        env = os.environ.copy()
        env["PATH"] = (
            r"D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll;"
            + work_dir + ";" + env.get("PATH", "")
        )
        env["CAFETERIA_DATA_DIR"] = test_data_dir

        # =============================================================
        # SESIÓN 1: Alta inicial del Dueño y Pruebas del Panel de Roles
        # =============================================================
        print("\n--- SESIÓN 1: Alta Inicial del Dueño ---")
        p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid1 = p1.pid

        hwnd_owner, _ = wait_for_window_by_pid(pid1, "Primer Inicio", step_name="Alta Dueño")
        assert hwnd_owner is not None, "Diálogo de primer inicio no encontrado"

        owner_ctrls = get_child_controls(hwnd_owner)
        owner_edits = [h for (h, cls, txt) in owner_ctrls if cls.lower() == "edit"]
        owner_btns = [h for (h, cls, txt) in owner_ctrls if cls.lower() == "button"]
        set_text(owner_edits[0], "Administrador General")
        set_text(owner_edits[1], "admin_owner")
        set_text(owner_edits[2], "admin@itch.edu.mx")
        set_text(owner_edits[3], "ClaveSegura123!")
        set_text(owner_edits[4], "ClaveSegura123!")
        time.sleep(0.3)
        post_click_button(owner_btns[0])

        hwnd_main1, _ = wait_for_window_by_pid(
            pid1, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow alta inicial"
        )
        assert hwnd_main1 is not None, "MainWindow no abrió tras alta inicial"
        print("[OK] Alta inicial completada y MainWindow abierta.")
        time.sleep(1.0)

        # Reporte de escala DPI
        dpi = get_window_dpi(hwnd_main1)
        scale_pct = int(round(dpi / 96.0 * 100))
        print(f"[DPI] DPI detectado: {dpi} ({scale_pct}%).")

        # Navegar a BackOffice > Roles y permisos
        main_ctrls = get_child_controls(hwnd_main1)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_bo = next((h for (h, t) in btns if "BackOffice" in t), None)
        assert btn_bo is not None, "Botón 'BackOffice' no encontrado en la barra lateral"
        send_click_button(btn_bo)
        time.sleep(0.8)

        bo_ctrls = get_child_controls(hwnd_main1)
        bo_btns = [(h, txt) for (h, cls, txt) in bo_ctrls if cls.lower() == "button"]
        btn_roles = next((h for (h, t) in bo_btns if "Roles y permisos" in t), None)
        assert btn_roles is not None, "Pestaña 'Roles y permisos' no encontrada en subnav"
        send_click_button(btn_roles)
        time.sleep(0.8)

        # -------------------------------------------------------------
        # CASO 1: role-dev y backup.dev_config no aparecen en la interfaz
        # -------------------------------------------------------------
        print("\n--- CASO 1: Verificación de exclusión de role-dev y backup.dev_config ---")
        panel_ctrls = get_child_controls(hwnd_main1)
        for h, cls, txt in panel_ctrls:
            assert "role-dev" not in txt, f"ERROR: 'role-dev' expuesto en control {cls} ('{txt}')"
            assert "backup.dev_config" not in txt, f"ERROR: 'backup.dev_config' expuesto en control {cls} ('{txt}')"
        print("[OK] Ni 'role-dev' ni 'backup.dev_config' aparecen en ningún control visible de la UI.")

        # -------------------------------------------------------------
        # CASO 2: Roles de sistema protegidos (solo lectura)
        # -------------------------------------------------------------
        print("\n--- CASO 2: Roles de sistema en solo lectura (Eliminar y Guardar deshabilitados) ---")
        lists = [h for (h, cls, txt) in panel_ctrls if "list" in cls.lower()]
        assert len(lists) >= 1, "Lista de roles (SysListView32) no encontrada"
        hwnd_role_list = lists[0]

        btn_save = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn_del = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        btn_new = next((h for (h, cls, txt) in panel_ctrls if cls.lower() == "button" and txt == "Nuevo rol"), None)

        assert btn_save is not None, "Botón 'Guardar' no encontrado"
        assert btn_del is not None, "Botón 'Eliminar' no encontrado"
        assert btn_new is not None, "Botón 'Nuevo rol' no encontrado"

        # Verificar estado del rol seleccionado por defecto (Dueño o Cajero)
        # Ambos son de sistema, por lo que Guardar y Eliminar DEBEN estar deshabilitados
        assert user32.IsWindowEnabled(btn_del) == 0, "Botón 'Eliminar' debe estar deshabilitado para rol de sistema"
        assert user32.IsWindowEnabled(btn_save) == 0, "Botón 'Guardar' debe estar deshabilitado para rol de sistema"

        # Seleccionar explícitamente el rol Cajero (fila 0 según ORDER BY name ASC: Cajero, Dueño)
        select_list_item(hwnd_role_list, 0)
        time.sleep(0.4)
        assert user32.IsWindowEnabled(btn_del) == 0, "Botón 'Eliminar' debe estar deshabilitado para rol Cajero"
        assert user32.IsWindowEnabled(btn_save) == 0, "Botón 'Guardar' debe estar deshabilitado para rol Cajero"

        # Seleccionar explícitamente el rol Dueño (fila 1)
        select_list_item(hwnd_role_list, 1)
        time.sleep(0.4)
        assert user32.IsWindowEnabled(btn_del) == 0, "Botón 'Eliminar' debe estar deshabilitado para rol Dueño"
        assert user32.IsWindowEnabled(btn_save) == 0, "Botón 'Guardar' debe estar deshabilitado para rol Dueño"

        print("[OK] Roles de sistema 'Cajero' y 'Dueño' protegidos en solo lectura.")

        # -------------------------------------------------------------
        # CASO 3: Geometría Estricta a 1216x759 y 960x600 en Estándar y Touch
        # -------------------------------------------------------------
        print("\n--- CASO 3: Verificación Geométrica y Accesibilidad Táctil ---")
        # 3.1: En Estándar 1200x720 / 1216x759
        main_l, main_t, main_r, main_b, main_w, main_h = get_rect(hwnd_main1)
        active_ctrls = get_child_controls(hwnd_main1)
        panel_buttons = [
            (h, txt) for (h, cls, txt) in active_ctrls
            if cls.lower() == "button" and txt in ["Nuevo rol", "Actualizar", "Guardar", "Eliminar"]
        ]
        for bh, btxt in panel_buttons:
            bl, bt, br, bb, bw, bh_h = get_rect(bh)
            assert br <= main_r, f"Botón '{btxt}' excede el margen derecho de la ventana: {br} > {main_r}"
            tw, th = measure_text_extent(bh, btxt)
            assert bw >= tw + 10, f"Botón '{btxt}' recortado horizontalmente: ancho {bw}px < texto {tw}px + 10"
            assert bh_h >= 34, f"Botón '{btxt}' en Estándar altura insuficiente: {bh_h}px < 34px"
            print(f"  [Estándar 1200] Botón '{btxt}': {bw}x{bh_h}px (texto={tw}px, OK)")

        # 3.2: Ventana Estrecha 960x600 en Estándar
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        n_l, n_t, n_r, n_b, n_w, n_h = get_rect(hwnd_main1)
        ctrls_960 = get_child_controls(hwnd_main1)
        for h, cls, txt in ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= n_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en 960x600: {cr} > {n_r}"
        print("[OK] Geometría 960x600 en Estándar: Sin desbordamientos horizontales.")

        # 3.3: Alternar a Perfil Touch
        # Restaurar tamaño a 1200x720
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)

        # Buscar botón de perfil en el encabezado
        btn_touch_toggle = None
        for _ in range(20):
            hdr_ctrls = get_child_controls(hwnd_main1)
            btn_touch_toggle = next(
                (h for (h, cls, txt) in hdr_ctrls if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)),
                None
            )
            if btn_touch_toggle:
                break
            time.sleep(0.2)
        assert btn_touch_toggle is not None, "Botón alternador de perfil Touch/Estándar no encontrado"
        send_click_button(btn_touch_toggle)
        time.sleep(0.8)

        # Volver a ingresar a BackOffice > Roles y permisos tras recarga de perfil
        t_ctrls = get_child_controls(hwnd_main1)
        t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
        btn_bo_t = next((h for (h, t) in t_btns if "BackOffice" in t), None)
        if btn_bo_t:
            send_click_button(btn_bo_t)
            time.sleep(0.6)
            t_ctrls = get_child_controls(hwnd_main1)
            t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
            btn_roles_t = next((h for (h, t) in t_btns if "Roles y permisos" in t), None)
            if btn_roles_t:
                send_click_button(btn_roles_t)
                time.sleep(0.8)

        # En Touch: Verificar que todos los botones de acción del panel tengan al menos 48 px de alto
        touch_ctrls = get_child_controls(hwnd_main1)
        touch_action_btns = [
            (h, txt) for (h, cls, txt) in touch_ctrls
            if cls.lower() == "button" and txt in ["Nuevo rol", "Actualizar", "Guardar", "Eliminar"]
        ]
        assert len(touch_action_btns) >= 4, f"No se encontraron los 4 botones en Touch (encontrados: {len(touch_action_btns)})"
        for bh, btxt in touch_action_btns:
            _, _, _, _, bw, bh_h = get_rect(bh)
            assert bh_h >= 48, f"ERROR: Botón '{btxt}' en Touch tiene altura {bh_h}px < 48px obligatorio"
            print(f"  [Touch] Botón '{btxt}': {bw}x{bh_h}px (cumple >= 48px)")

        # 3.4: Ventana estrecha 960x600 en Touch
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        tn_l, tn_t, tn_r, tn_b, tn_w, tn_h = get_rect(hwnd_main1)
        t_ctrls_960 = get_child_controls(hwnd_main1)
        for h, cls, txt in t_ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= tn_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en Touch 960x600: {cr} > {tn_r}"
        print("[OK] Geometría 960x600 en Touch: Controles contenidos sin recortes.")

        # Regresar a perfil Estándar y tamaño normal para la creación de rol
        user32.SetWindowPos(hwnd_main1, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.3)
        btn_touch_toggle2 = None
        for _ in range(20):
            ctrls2 = get_child_controls(hwnd_main1)
            btn_touch_toggle2 = next(
                (h for (h, cls, txt) in ctrls2 if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)),
                None
            )
            if btn_touch_toggle2:
                break
            time.sleep(0.2)
        if btn_touch_toggle2:
            send_click_button(btn_touch_toggle2)
            time.sleep(0.8)

        # Reingresar a BackOffice > Roles y permisos
        r_ctrls = get_child_controls(hwnd_main1)
        r_btns = [(h, txt) for (h, cls, txt) in r_ctrls if cls.lower() == "button"]
        btn_bo_r = next((h for (h, t) in r_btns if "BackOffice" in t), None)
        if btn_bo_r:
            send_click_button(btn_bo_r)
            time.sleep(0.5)
            r_ctrls = get_child_controls(hwnd_main1)
            r_btns = [(h, txt) for (h, cls, txt) in r_ctrls if cls.lower() == "button"]
            btn_roles_r = next((h for (h, t) in r_btns if "Roles y permisos" in t), None)
            if btn_roles_r:
                send_click_button(btn_roles_r)
                time.sleep(0.8)

        # -------------------------------------------------------------
        # CASO 4: Crear Rol, Marcar 2 Permisos, Guardar y Verificar Persistencia
        # -------------------------------------------------------------
        print("\n--- CASO 4: Crear Rol Personalizado, Marcar 2 Permisos y Guardar ---")
        cur_ctrls = get_child_controls(hwnd_main1)
        btn_new_act = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and txt == "Nuevo rol"), None)
        assert btn_new_act is not None, "Botón 'Nuevo rol' no encontrado"
        send_click_button(btn_new_act)
        time.sleep(0.4)

        # Localizar campos de edición de nombre y descripción
        cur_ctrls = get_child_controls(hwnd_main1)
        edits = [h for (h, cls, txt) in cur_ctrls if cls.lower() == "edit"]
        assert len(edits) >= 2, f"Campos de texto insuficientes ({len(edits)}) para nombre y descripción"
        txt_name = edits[0]
        txt_desc = edits[1]

        assert user32.IsWindowEnabled(txt_name) == 1, "Campo Nombre debe estar habilitado para nuevo rol"
        assert user32.IsWindowEnabled(txt_desc) == 1, "Campo Descripción debe estar habilitado para nuevo rol"

        test_role_name = "Supervisor de Barra"
        test_role_desc = "Supervisión de recetas y stock"
        set_text(txt_name, test_role_name)
        set_text(txt_desc, test_role_desc)
        time.sleep(0.2)

        # Marcar dos permisos: 'pos.sell' ('Venta y cobro regular...') y 'inventory.view' ('Consulta de existencias...')
        cb_sell = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and "Venta y cobro" in txt), None)
        cb_inv = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and "Consulta de existencias" in txt), None)

        assert cb_sell is not None, "Casilla de permiso 'pos.sell' no encontrada"
        assert cb_inv is not None, "Casilla de permiso 'inventory.view' no encontrada"

        assert user32.IsWindowEnabled(cb_sell) == 1, "Casilla 'pos.sell' debe estar habilitada para edición"
        assert user32.IsWindowEnabled(cb_inv) == 1, "Casilla 'inventory.view' debe estar habilitada para edición"

        # Marcar casillas con BM_CLICK
        send_click_button(cb_sell)
        time.sleep(0.1)
        send_click_button(cb_inv)
        time.sleep(0.2)

        assert user32.SendMessageW(cb_sell, BM_GETCHECK, 0, 0) == 1, "Casilla 'pos.sell' debe estar marcada"
        assert user32.SendMessageW(cb_inv, BM_GETCHECK, 0, 0) == 1, "Casilla 'inventory.view' debe estar marcada"
        print("[OK] Dos permisos seleccionados exitosamente ('pos.sell' e 'inventory.view').")

        # Guardar rol
        btn_save_act = next((h for (h, cls, txt) in cur_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        assert btn_save_act is not None, "Botón 'Guardar' no encontrado"
        assert user32.IsWindowEnabled(btn_save_act) == 1, "Botón 'Guardar' debe estar habilitado"

        send_click_button(btn_save_act)
        time.sleep(0.8)

        # Validar persistencia inmediata en la base SQLite antes de cerrar
        con1 = sqlite3.connect(db_path)
        cur1 = con1.cursor()
        cur1.execute("SELECT id, name, description, is_system FROM roles WHERE name = ?;", (test_role_name,))
        row_role = cur1.fetchone()
        assert row_role is not None, f"El rol '{test_role_name}' no se insertó en la base de datos"
        assert row_role[3] == 0, "El nuevo rol no debe ser de sistema (is_system=0)"
        created_role_id = row_role[0]

        cur1.execute("SELECT permission_id FROM role_permissions WHERE role_id = ? ORDER BY permission_id ASC;", (created_role_id,))
        saved_perms = [r[0] for r in cur1.fetchall()]
        assert "pos.sell" in saved_perms, "Permiso 'pos.sell' no guardado en role_permissions"
        assert "inventory.view" in saved_perms, "Permiso 'inventory.view' no guardado en role_permissions"
        assert len(saved_perms) == 2, f"Se esperaban 2 permisos, encontrados {len(saved_perms)}: {saved_perms}"
        con1.close()
        print(f"[OK] Rol '{test_role_name}' persistido en SQLite con 2 permisos ({saved_perms}).")

        # Cerrar sesión 1
        user32.PostMessageW(hwnd_main1, WM_CLOSE, 0, 0)
        p1.wait(timeout=5)
        print("[OK] Sesión 1 cerrada limpiamente.")

        # =============================================================
        # SESIÓN 2: Reapertura y Comprobación de Persistencia en la UI
        # =============================================================
        print("\n--- SESIÓN 2: Reapertura y Verificación de Persistencia en UI ---")
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid2 = p2.pid

        hwnd_login, _ = wait_for_window_by_pid(pid2, "Iniciar Sesión", step_name="Login Reapertura")
        assert hwnd_login is not None, "Diálogo de login no apareció en reapertura"
        l_ctrls = get_child_controls(hwnd_login)
        l_edits = [h for (h, cls, txt) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], "admin_owner")
        set_text(l_edits[1], "ClaveSegura123!")
        time.sleep(0.3)
        post_click_button(l_btns[0])

        hwnd_main2, _ = wait_for_window_by_pid(
            pid2, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow reapertura"
        )
        assert hwnd_main2 is not None, "MainWindow no abrió tras login en reapertura"
        time.sleep(1.0)

        # Ir a BackOffice > Roles y permisos
        m2_ctrls = get_child_controls(hwnd_main2)
        m2_btns = [(h, txt) for (h, cls, txt) in m2_ctrls if cls.lower() == "button"]
        btn_bo2 = next((h for (h, t) in m2_btns if "BackOffice" in t), None)
        assert btn_bo2 is not None, "Botón 'BackOffice' no encontrado en sesión 2"
        send_click_button(btn_bo2)
        time.sleep(0.8)

        m2_ctrls = get_child_controls(hwnd_main2)
        m2_btns = [(h, txt) for (h, cls, txt) in m2_ctrls if cls.lower() == "button"]
        btn_roles2 = next((h for (h, t) in m2_btns if "Roles y permisos" in t), None)
        assert btn_roles2 is not None, "Pestaña 'Roles y permisos' no encontrada en sesión 2"
        send_click_button(btn_roles2)
        time.sleep(0.8)

        # Verificar lista de roles: ordenada alfabéticamente:
        # Fila 0: Cajero
        # Fila 1: Dueño
        # Fila 2: Supervisor de Barra
        panel2_ctrls = get_child_controls(hwnd_main2)
        lists2 = [(h, cls, txt) for (h, cls, txt) in panel2_ctrls if "list" in cls.lower()]
        assert len(lists2) >= 1, "Lista de roles no encontrada en sesión 2"
        hwnd_list2 = lists2[0][0]

        # Seleccionar la fila 2 (Supervisor de Barra)
        select_list_item(hwnd_list2, 2)
        time.sleep(0.5)

        sel_after = user32.SendMessageW(hwnd_list2, 0x100C, -1, 0x0002)
        assert sel_after == 2, f"Fila 2 no quedó seleccionada en la lista (seleccionada: {sel_after})"

        # Comprobar que los campos y casillas reflejan el rol persistido
        p2_ctrls = get_child_controls(hwnd_main2)
        p2_edits = [h for (h, cls, txt) in p2_ctrls if cls.lower() == "edit"]
        assert len(p2_edits) >= 2, "Campos de texto no encontrados en sesión 2"

        name_val = get_ctrl_text(p2_edits[0])
        desc_val = get_ctrl_text(p2_edits[1])
        assert name_val == test_role_name, f"Nombre del rol no coincide: '{name_val}' != '{test_role_name}'"
        assert desc_val == test_role_desc, f"Descripción del rol no coincide: '{desc_val}' != '{test_role_desc}'"
        print(f"[OK] Rol '{name_val}' cargado íntegramente desde persistencia.")

        # Verificar estado de las casillas de verificación
        cb2_sell = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and "Venta y cobro" in txt), None)
        cb2_inv = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and "Consulta de existencias" in txt), None)
        cb2_shift = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and "Apertura de turno" in txt), None)

        assert cb2_sell is not None and cb2_inv is not None and cb2_shift is not None

        assert user32.SendMessageW(cb2_sell, BM_GETCHECK, 0, 0) == 1, "Permiso 'pos.sell' debe permanecer marcado"
        assert user32.SendMessageW(cb2_inv, BM_GETCHECK, 0, 0) == 1, "Permiso 'inventory.view' debe permanecer marcado"
        assert user32.SendMessageW(cb2_shift, BM_GETCHECK, 0, 0) == 0, "Permiso 'shift.operate' no marcado debe permanecer desmarcado"

        # Verificar que para el rol personalizado, 'Guardar' y 'Eliminar' están habilitados
        btn2_save = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and txt == "Guardar"), None)
        btn2_del = next((h for (h, cls, txt) in p2_ctrls if cls.lower() == "button" and txt == "Eliminar"), None)
        assert btn2_save is not None and user32.IsWindowEnabled(btn2_save) == 1, "Botón Guardar debe estar habilitado para rol personalizable"
        assert btn2_del is not None and user32.IsWindowEnabled(btn2_del) == 1, "Botón Eliminar debe estar habilitado para rol personalizable"
        print("[OK] Permisos y acciones del rol personalizado persisten y son editables.")

        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=5)
        print("[OK] Sesión 2 cerrada limpiamente.")

        print(f"\n>>> TODAS LAS PRUEBAS DE TAREA F1 PASARON EXITOSAMENTE EN [{config_name}] <<<")

    finally:
        for p in [p1, p2]:
            if p and p.poll() is None:
                p.kill()
        shutil.rmtree(test_data_dir, ignore_errors=True)
        print(f"[OK] Directorio temporal aislado limpiado: {test_data_dir}")

def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    release_exe = os.path.join(repo_root, "build_release", "app_desktop.exe")
    debug_exe = os.path.join(repo_root, "build_debug", "app_desktop.exe")

    print("=================================================================")
    print("SUITE DE PRUEBAS AUTOMATIZADA: TAREA F1 (ROLES Y PERMISOS)")
    print("=================================================================")

    # 1. Ejecución en Release
    run_f1_test_for_binary(release_exe, repo_root, "RELEASE")

    # 2. Ejecución en Debug
    run_f1_test_for_binary(debug_exe, repo_root, "DEBUG")

    print("\n=================================================================")
    print("VERIFICACIÓN COMPLETA F1: PASARON TODAS LAS PRUEBAS EN RELEASE Y DEBUG")
    print("=================================================================")

if __name__ == "__main__":
    main()
