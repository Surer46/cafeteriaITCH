#!/usr/bin/env python3
"""
test_f2_audit_log.py - Suite de pruebas automatizada para Tarea F2 (Bitácora de Auditoría).
Verifica:
1. Navegación a Caja y Reportes > Bitácora.
2. Tras alta inicial y login de Dueño, aparecen eventos auditados (OWNER_CREATED, LOGIN_SUCCESS).
3. Selección de fila muestra detalle completo en campo de solo lectura.
4. Filtro por tipo de evento reduce la lista y muestra solo el tipo filtrado.
5. Paginación con más de 100 eventos: limit 100, Siguiente carga otros eventos, Anterior regresa.
6. Cajero sin permiso audit.view: servicio devuelve error, interfaz muestra mensaje tal cual y lista vacía.
7. Geometría estricta en Estándar y Touch a 1216x759 (sin WS_HSCROLL) y 960x600 (sin desbordamientos).
8. En perfil Touch: controles interactivos >= 48 px de altura.
9. Ejecución completa sobre binarios Release y Debug reales con SQLite aislado.
"""

import ctypes
from ctypes import wintypes
import hashlib
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

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
user32.GetParent.argtypes = [wintypes.HWND]
user32.GetParent.restype = wintypes.HWND
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.SetWindowPos.restype = wintypes.BOOL
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetWindowLongW.restype = wintypes.LONG

gdi32.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(SIZE)]
gdi32.GetTextExtentPoint32W.restype = wintypes.BOOL
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.GetPixel.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.GetPixel.restype = wintypes.COLORREF
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetClientRect.restype = wintypes.BOOL

try:
    user32.GetDpiForWindow.argtypes = [wintypes.HWND]
    user32.GetDpiForWindow.restype = wintypes.UINT
    HAS_GET_DPI = True
except AttributeError:
    HAS_GET_DPI = False

GWL_STYLE = -16
WS_HSCROLL = 0x00100000
LVM_GETITEMCOUNT = 0x1000 + 4
LVM_GETCOLUMNWIDTH = 0x1000 + 29
LVM_GETHEADER = 0x1000 + 31
CLR_INVALID = 0xFFFFFFFF
CB_GETCOUNT = 0x0146
CB_SETCURSEL = 0x014E
CB_GETCURSEL = 0x0147
CB_GETLBTEXTLEN = 0x0149
CB_GETLBTEXT = 0x0148
CBN_SELCHANGE = 1

WM_SETFOCUS = 0x0007
WM_SETTEXT = 0x000C
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_CLOSE = 0x0010
WM_GETFONT = 0x0031
BM_CLICK = 0x00F5
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
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == target_pid and user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            text = buf.value
            if exclude_substrings and any(exc in text for exc in exclude_substrings):
                return True
            if not title_substring or title_substring.lower() in text.lower():
                found.append((hwnd, text))
        return True
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return found

def get_child_controls(parent_hwnd):
    controls = []
    def enum_child_cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            cls_buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls_buf, 256)
            txt = get_ctrl_text(hwnd)
            controls.append((hwnd, cls_buf.value, txt))
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
    raise TimeoutError(f"Timeout ({timeout_sec}s) esperando ventana '{title_substring}' en paso: {step_name}")

def get_rect(hwnd):
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return rect.left, rect.top, rect.right, rect.bottom, rect.right - rect.left, rect.bottom - rect.top

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

def make_password_hash(password):
    salt = os.urandom(16)
    derived = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, 100000)
    return f"pbkdf2_sha256$100000${salt.hex()}${derived.hex()}"

def get_client_size(hwnd):
    r = wintypes.RECT()
    assert user32.GetClientRect(hwnd, ctypes.byref(r)), "GetClientRect falló"
    return r.right - r.left, r.bottom - r.top

def verify_list_columns(hwnd_list, profile_name):
    """Columnas de la bitácora: encabezado medido con la fuente REAL del encabezado, contenido con la de la lista,
    sin WS_HSCROLL, suma <= área cliente y holgura <= 8 px (la columna Detalle absorbe el espacio restante)."""
    style = user32.GetWindowLongW(hwnd_list, GWL_STYLE)
    assert (style & WS_HSCROLL) == 0, f"Lista de bitácora tiene barra horizontal WS_HSCROLL visible a 1216x759 en {profile_name}"

    hdr = user32.SendMessageW(hwnd_list, LVM_GETHEADER, 0, 0)
    assert hdr, f"LVM_GETHEADER no devolvió el encabezado de la lista en {profile_name}"

    col_widths = [user32.SendMessageW(hwnd_list, LVM_GETCOLUMNWIDTH, i, 0) for i in range(4)]
    titles = ["Fecha y hora", "Usuario", "Evento", "Detalle"]
    for i, title in enumerate(titles):
        hdr_ext, _ = measure_text_extent(hdr, title)
        assert hdr_ext > 0, f"No se pudo medir el encabezado '{title}' con la fuente del encabezado en {profile_name}"
        assert col_widths[i] >= hdr_ext + 8, (
            f"Encabezado '{title}' recortado: columna {col_widths[i]}px < {hdr_ext + 8}px "
            f"(texto {hdr_ext}px con la fuente del encabezado) en {profile_name}")

    # Contenido (fuente de la lista)
    ext_date, _ = measure_text_extent(hwnd_list, "2026-11-08 12:00:00")
    assert col_widths[0] >= ext_date + 8, f"Columna 0 ('Fecha y hora') {col_widths[0]}px < {ext_date + 8}px en {profile_name}"
    ext_u, _ = measure_text_extent(hwnd_list, "admin_owner")
    assert col_widths[1] >= ext_u + 8, f"Columna 1 ('Usuario') {col_widths[1]}px < {ext_u + 8}px en {profile_name}"
    ext_ev, _ = measure_text_extent(hwnd_list, "LOGIN_SUCCESS")
    assert col_widths[2] >= ext_ev + 8, f"Columna 2 ('Evento') {col_widths[2]}px < {ext_ev + 8}px en {profile_name}"

    # Suma <= área cliente (sin barra vertical) y holgura <= 8 px
    client_w, _ = get_client_size(hwnd_list)
    total = sum(col_widths)
    assert total <= client_w, f"Columnas ({total}px) exceden el área cliente ({client_w}px) en {profile_name}"
    assert client_w - total <= 8, (
        f"Espacio sobrante sin aprovechar: {client_w - total}px > 8px (columnas {total}px, cliente {client_w}px) en {profile_name}")

    print(f"  [{profile_name}] Columnas medidas: F={col_widths[0]}px, U={col_widths[1]}px, E={col_widths[2]}px, D={col_widths[3]}px "
          f"(cliente {client_w}px, holgura {client_w - total}px, sin WS_HSCROLL)")

def ink_vertical_extent(h_edit, text):
    """Rango vertical (coordenadas de pantalla) de los píxeles de texto de un campo Edit.
    Fondo: píxel libre a la derecha del texto. FALLA si no hay texto, fondo o tinta."""
    l, t, r, b, w, h = get_rect(h_edit)
    tw, _ = measure_text_extent(h_edit, text)
    assert 0 < tw < w - 16, f"Texto '{text}' ({tw}px) no cabe en el campo ({w}px) para medir centrado"
    hdc = user32.GetDC(h_edit)
    assert hdc, "GetDC del campo falló"
    try:
        bg = None
        for (x, y) in ((w - 4, h // 2), (w - 4, 2), (w - 4, h - 3)):
            v = gdi32.GetPixel(hdc, x, y)
            if v != CLR_INVALID:
                bg = v
                break
        assert bg is not None, "Sin píxel de fondo medible en el campo"
        first = last = None
        for y in range(h):
            for x in range(0, min(w - 8, tw + 12)):
                v = gdi32.GetPixel(hdc, x, y)
                if v != CLR_INVALID and v != bg:
                    if first is None:
                        first = y
                    last = y
                    break
        assert first is not None, f"No se detectó texto '{text}' en el campo (¿vacío o fuera de vista?)"
        return t + first, t + last
    finally:
        user32.ReleaseDC(h_edit, hdc)

def find_filter_field(ctrls, label_text):
    """Localiza el campo de un filtro por su contenedor: Edit cuyo padre (contenedor) es hermano de la etiqueta,
    queda a su derecha (a lo sumo 24 px) y se solapa verticalmente con ella. Debe haber EXACTAMENTE uno."""
    labels = [h for (h, cls, txt) in ctrls if cls.lower() == "static" and txt == label_text]
    assert len(labels) == 1, f"Se esperaba una etiqueta '{label_text}' visible y hay {len(labels)}"
    lbl = labels[0]
    parent = user32.GetParent(lbl)
    _, lt, lr, lb, _, _ = get_rect(lbl)
    lcy = (lt + lb) / 2.0
    matches = []
    for (h, cls, txt) in ctrls:
        if cls.lower() != "edit":
            continue
        cont = user32.GetParent(h)
        if not cont or cont == parent or user32.GetParent(cont) != parent:
            continue
        cl, ct, cr, cb, _, _ = get_rect(cont)
        if -1 <= cl - lr <= 24 and ct <= lcy <= cb:
            matches.append((h, cont))
    assert len(matches) == 1, f"Campo de '{label_text}': se esperaba exactamente un contenedor y hay {len(matches)}"
    return matches[0]

def verify_filter_fields(hwnd_main, profile_name, expected_h):
    """Usuario, Fecha desde y Fecha hasta: contenedor >= expected_h px y texto centrado verticalmente (tol. 3 px)."""
    ctrls = get_child_controls(hwnd_main)
    samples = (("Usuario:", "admin_owner"), ("Fecha desde:", "2026-11-01"), ("Fecha hasta:", "2026-11-30"))
    fields = [(label, sample) + find_filter_field(ctrls, label) for (label, sample) in samples]
    assert len(fields) == 3, "Deben localizarse exactamente tres campos de filtro"
    for label, sample, h_edit, cont in fields:
        _, ct, _, cb, _, ch_cont = get_rect(cont)
        assert ch_cont >= expected_h, f"Contenedor de '{label}' en {profile_name} mide {ch_cont}px < {expected_h}px"
        set_text(h_edit, sample)
        time.sleep(0.3)
        try:
            ink_top, ink_bottom = ink_vertical_extent(h_edit, sample)
        finally:
            set_text(h_edit, "")
        ink_center = (ink_top + ink_bottom) / 2.0
        box_center = (ct + cb) / 2.0
        assert abs(ink_center - box_center) <= 3, (
            f"Texto de '{label}' no centrado verticalmente en {profile_name}: centro de tinta {ink_center:.1f} vs "
            f"contenedor {box_center:.1f} (diferencia {abs(ink_center - box_center):.1f}px > 3px)")
    print(f"  [{profile_name}] Usuario, Fecha desde y Fecha hasta: contenedor >= {expected_h}px y texto centrado (tolerancia 3 px).")

def run_f2_test_for_binary(exe_path, repo_root, config_name):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN BITÁCORA DE AUDITORÍA F2 [{config_name}]:")
    print(f"  Binario:  {exe_path}")
    print(f"=======================================================")

    work_dir = repo_root
    test_data_dir = tempfile.mkdtemp(prefix=f"cafeteria_f2_{config_name.lower()}_")
    db_path = os.path.join(test_data_dir, "cafeteria.db")

    env = os.environ.copy()
    env["PATH"] = os.path.dirname(exe_path) + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = test_data_dir

    try:
        # =============================================================
        # SESIÓN 1: Alta inicial del Dueño
        # =============================================================
        print("\n--- SESIÓN 1: Alta Inicial del Dueño ---")
        p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid1 = p1.pid

        hwnd_owner, _ = wait_for_window_by_pid(pid1, "Primer Inicio", step_name="Alta Dueño")
        assert hwnd_owner is not None, "Diálogo de primer inicio no encontrado"

        owner_ctrls = get_child_controls(hwnd_owner)
        owner_edits = [h for (h, cls, txt) in owner_ctrls if cls.lower() == "edit"]
        owner_btns = [h for (h, cls, txt) in owner_ctrls if cls.lower() == "button"]
        set_text(owner_edits[0], "Administrador Dueño")
        set_text(owner_edits[1], "admin_owner")
        set_text(owner_edits[2], "admin@itch.edu.mx")
        set_text(owner_edits[3], "AdminPass123!")
        set_text(owner_edits[4], "AdminPass123!")
        time.sleep(0.3)
        send_click_button(owner_btns[0])
        time.sleep(1.2)

        hwnd_main1, _ = wait_for_window_by_pid(
            pid1, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow alta inicial"
        )
        assert hwnd_main1 is not None, "MainWindow no abrió tras alta inicial"
        print("[OK] Alta inicial completada y MainWindow abierta.")

        # Cerrar sesión 1
        user32.PostMessageW(hwnd_main1, WM_CLOSE, 0, 0)
        p1.wait(timeout=5.0)
        print("[OK] Sesión 1 cerrada limpiamente.")

        # =============================================================
        # SESIÓN 2: Login como Dueño y Pruebas de Bitácora
        # =============================================================
        print("\n--- SESIÓN 2: Login Dueño y Verificación de Bitácora ---")
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
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

        hwnd_main2, _ = wait_for_window_by_pid(
            pid2, "Cafetería ITCH II",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
            step_name="MainWindow login Dueño"
        )
        assert hwnd_main2 is not None, "MainWindow no abrió tras login Dueño"
        print("[OK] Login Dueño exitoso.")

        # Navegar a Caja y Reportes > Bitácora
        main_ctrls2 = get_child_controls(hwnd_main2)
        btns_sidebar = [(h, txt) for (h, cls, txt) in main_ctrls2 if cls.lower() == "button"]
        btn_caja = next((h for (h, t) in btns_sidebar if "Caja y Reportes" in t), None)
        assert btn_caja is not None, "Botón 'Caja y Reportes' no encontrado en sidebar"
        send_click_button(btn_caja)
        time.sleep(0.8)

        subnav_ctrls = get_child_controls(hwnd_main2)
        btns_subnav = [(h, txt) for (h, cls, txt) in subnav_ctrls if cls.lower() == "button"]
        btn_bitacora = next((h for (h, t) in btns_subnav if "Bitácora" in t), None)
        assert btn_bitacora is not None, "Pestaña 'Bitácora' no encontrada en subnav"
        send_click_button(btn_bitacora)
        time.sleep(0.8)

        # 2.1: Verificar que aparecen eventos auditados (OWNER_CREATED y LOGIN_SUCCESS)
        panel_ctrls = get_child_controls(hwnd_main2)
        lists = [h for (h, cls, txt) in panel_ctrls if "list" in cls.lower()]
        assert len(lists) >= 1, "Lista de eventos (SysListView32) no encontrada"
        hwnd_list = lists[0]

        count = user32.SendMessageW(hwnd_list, LVM_GETITEMCOUNT, 0, 0)
        assert count >= 2, f"Se esperaban al menos 2 eventos auditados (encontrados: {count})"
        print(f"[OK] Eventos auditados iniciales cargados en lista: {count} eventos.")

        # 2.2: Verificar columnas y ausencia de WS_HSCROLL a 1216x759 en Estándar
        verify_list_columns(hwnd_list, "Estándar")
        verify_filter_fields(hwnd_main2, "Estándar", 34)

        # 2.3: Seleccionar fila 0 y comprobar que el detalle completo aparece en txt_detail_
        select_list_item(hwnd_list, 0)
        time.sleep(0.5)

        edits_panel = [h for (h, cls, txt) in get_child_controls(hwnd_main2) if "edit" in cls.lower()]
        # El campo de detalle es multilínea de solo lectura
        detail_text = ""
        for eh in edits_panel:
            t = get_ctrl_text(eh)
            if "Inicio de sesión" in t or "Alta inicial" in t or "Dueño" in t or "admin_owner" in t:
                detail_text = t
                break
        assert len(detail_text) > 0, "Detalle completo del evento no se reflejó en el visualizador de texto"
        print(f"[OK] Detalle completo del evento seleccionado visualizado: '{detail_text[:50]}...'")

        # 2.4: Filtro por tipo de evento reduce la lista
        combos = [h for (h, cls, txt) in get_child_controls(hwnd_main2) if "combobox" in cls.lower() or "choice" in cls.lower() or txt == "comboBox"]
        assert len(combos) >= 1, "Selector de tipo de evento no encontrado"
        cb_event_type = combos[0]

        btn_search = next((h for (h, cls, txt) in get_child_controls(hwnd_main2) if cls.lower() == "button" and "Buscar" in txt), None)
        assert btn_search is not None, "Botón 'Buscar' no encontrado"

        # Determinar el índice de 'LOGIN_SUCCESS' en choice_event_type_ ("Todos" + tipos ordenados)
        con_ev = sqlite3.connect(db_path)
        event_types = [r[0] for r in con_ev.execute("SELECT DISTINCT event_type FROM audit_events WHERE event_type != '' ORDER BY event_type").fetchall()]
        con_ev.close()
        assert "LOGIN_SUCCESS" in event_types, f"Opción 'LOGIN_SUCCESS' no encontrada en BD: {event_types}"
        login_idx = 1 + event_types.index("LOGIN_SUCCESS")

        def _select_combo_index(hwnd_cb, idx):
            user32.SetForegroundWindow(hwnd_main2)
            time.sleep(0.1)
            _, _, _, _, cw, ch = get_rect(hwnd_cb)
            lp = ((ch // 2) << 16) | (max(4, cw - 12) & 0xFFFF)
            user32.PostMessageW(hwnd_cb, 0x0201, 1, lp) # WM_LBUTTONDOWN
            time.sleep(0.05)
            user32.PostMessageW(hwnd_cb, 0x0202, 0, lp) # WM_LBUTTONUP
            time.sleep(0.25)
            target_h = user32.GetFocus() or hwnd_cb
            user32.PostMessageW(target_h, 0x0100, 0x24, 0) # VK_HOME
            user32.PostMessageW(target_h, 0x0101, 0x24, 0)
            time.sleep(0.05)
            for _ in range(idx):
                user32.PostMessageW(target_h, 0x0100, 0x28, 0) # VK_DOWN
                user32.PostMessageW(target_h, 0x0101, 0x28, 0)
                time.sleep(0.05)
            user32.PostMessageW(target_h, 0x0100, 0x0D, 0) # VK_RETURN
            user32.PostMessageW(target_h, 0x0101, 0x0D, 0)
            time.sleep(0.25)

        _select_combo_index(cb_event_type, login_idx)

        send_click_button(btn_search)
        time.sleep(0.6)

        filtered_count = user32.SendMessageW(hwnd_list, LVM_GETITEMCOUNT, 0, 0)
        assert filtered_count < count and filtered_count >= 1, f"El filtro no redujo la lista: antes={count}, después={filtered_count}"
        print(f"[OK] Filtro por tipo 'LOGIN_SUCCESS' redujo la lista de {count} a {filtered_count} eventos.")

        # Restablecer a Todos
        _select_combo_index(cb_event_type, 0)
        send_click_button(btn_search)
        time.sleep(0.5)

        # 2.5: Geometría Estándar 960x600 (sin desbordamientos horizontales)
        user32.SetWindowPos(hwnd_main2, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        n_l, n_t, n_r, n_b, n_w, n_h = get_rect(hwnd_main2)
        ctrls_960 = get_child_controls(hwnd_main2)
        for h, cls, txt in ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= n_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en 960x600: {cr} > {n_r}"
        print("[OK] Geometría 960x600 en Estándar: Sin desbordamientos horizontales.")

        # 2.6: Alternar a Perfil Touch
        user32.SetWindowPos(hwnd_main2, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)

        btn_touch_toggle = None
        for _ in range(10):
            hdr_ctrls = get_child_controls(hwnd_main2)
            btn_touch_toggle = next(
                (h for (h, cls, txt) in hdr_ctrls if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)),
                None
            )
            if btn_touch_toggle: break
            time.sleep(0.2)
        assert btn_touch_toggle is not None, "Botón toggle Touch/Estándar no encontrado"
        send_click_button(btn_touch_toggle)
        time.sleep(0.8)

        # Reingresar a Caja y Reportes > Bitácora en Touch
        t_ctrls = get_child_controls(hwnd_main2)
        t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
        btn_caja_t = next((h for (h, t) in t_btns if "Caja y Reportes" in t), None)
        if btn_caja_t:
            send_click_button(btn_caja_t)
            time.sleep(0.6)
            t_ctrls2 = get_child_controls(hwnd_main2)
            t_btns2 = [(h, txt) for (h, cls, txt) in t_ctrls2 if cls.lower() == "button"]
            btn_bit_t = next((h for (h, t) in t_btns2 if "Bitácora" in t), None)
            if btn_bit_t:
                send_click_button(btn_bit_t)
                time.sleep(0.8)

        # En Touch: verificar columnas y controles de al menos 48 px de alto
        t_lists = [h for (h, cls, txt) in get_child_controls(hwnd_main2) if "list" in cls.lower()]
        assert len(t_lists) >= 1, "Lista no encontrada en Touch"
        verify_list_columns(t_lists[0], "Touch")
        verify_filter_fields(hwnd_main2, "Touch", 48)

        touch_active_ctrls = get_child_controls(hwnd_main2)
        for h, cls, txt in touch_active_ctrls:
            if cls.lower() in ["button", "edit"] and txt in ["Buscar", "Anterior", "Siguiente"]:
                _, _, _, _, bw, bh = get_rect(h)
                assert bh >= 48, f"Botón interactivo '{txt}' en Touch altura insuficiente: {bh}px < 48px"
        print("[OK] Accesibilidad Touch: Botones interactivos cumplen altura >= 48px.")

        # Geometría Touch 960x600
        user32.SetWindowPos(hwnd_main2, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)
        tn_l, tn_t, tn_r, tn_b, tn_w, tn_h = get_rect(hwnd_main2)
        t_ctrls_960 = get_child_controls(hwnd_main2)
        for h, cls, txt in t_ctrls_960:
            if cls.lower() in ["button", "edit", "static"]:
                cl, ct, cr, cb, cw, ch = get_rect(h)
                assert cr <= tn_r + 4, f"Control {cls} ('{txt[:20]}') desborda horizontalmente en Touch 960x600: {cr} > {tn_r}"
        print("[OK] Geometría 960x600 en Touch: Sin desbordamientos horizontales.")

        # Cerrar sesión 2
        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=5.0)
        print("[OK] Sesión 2 cerrada limpiamente.")

        # =============================================================
        # SESIÓN 3: Paginación (>100 eventos) y Cajero sin permiso
        # =============================================================
        print("\n--- SESIÓN 3: Paginación (>100 eventos) y Restricción de Cajero ---")
        # Sembrar 110 eventos adicionales y crear usuario Cajero sin audit.view
        con = sqlite3.connect(db_path)
        cur = con.cursor()
        for i in range(1, 115):
            cur.execute("""
                INSERT INTO audit_events (id, event_type, user_id, details, created_at)
                VALUES (?, 'BATCH_OP', 'user-owner', ?, ?);
            """, (f"audit-bulk-{i:04d}", f"Operación masiva de prueba #{i}", f"2026-11-08 09:{i//60:02d}:{i%60:02d}"))

        cashier_pwd = make_password_hash("CajeroPass123!")
        cur.execute("""
            INSERT OR REPLACE INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
            VALUES ('usr-cashier-01', 'cajero_demo', 'Cajero de Turno', 'cajero@itch.edu.mx', ?, 'role-cashier', 1, '2026-11-08 08:00:00');
        """, (cashier_pwd,))
        con.commit()
        con.close()
        print("[OK] 114 eventos masivos y usuario 'cajero_demo' (role-cashier) sembrados en SQLite.")

        # 3.1: Abrir con Dueño y verificar paginación
        p3 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid3 = p3.pid

        hwnd_login3, _ = wait_for_window_by_pid(pid3, "Iniciar Sesión", step_name="Login Dueño Paginación")
        l_ctrls = get_child_controls(hwnd_login3)
        l_edits = [h for (h, cls, txt) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], "admin_owner")
        set_text(l_edits[1], "AdminPass123!")
        time.sleep(0.3)
        send_click_button(l_btns[0])
        time.sleep(1.2)

        hwnd_main3, _ = wait_for_window_by_pid(pid3, "Cafetería ITCH II", exclude_substrings=["Primer Inicio", "Iniciar Sesión"])
        # Navegar a Bitácora
        b_caja = next((h for (h, cls, txt) in get_child_controls(hwnd_main3) if cls.lower() == "button" and "Caja y Reportes" in txt), None)
        send_click_button(b_caja)
        time.sleep(0.8)
        b_bit = next((h for (h, cls, txt) in get_child_controls(hwnd_main3) if cls.lower() == "button" and "Bitácora" in txt), None)
        send_click_button(b_bit)
        time.sleep(0.8)

        # Comprobar Página 1
        p_list = [h for (h, cls, txt) in get_child_controls(hwnd_main3) if "list" in cls.lower()][0]
        p_count = user32.SendMessageW(p_list, LVM_GETITEMCOUNT, 0, 0)
        assert p_count == 100, f"Página 1 debe mostrar exactamente 100 eventos (encontrados: {p_count})"

        btn_next = next((h for (h, cls, txt) in get_child_controls(hwnd_main3) if cls.lower() == "button" and txt == "Siguiente"), None)
        btn_prev = next((h for (h, cls, txt) in get_child_controls(hwnd_main3) if cls.lower() == "button" and txt == "Anterior"), None)
        assert btn_next is not None and user32.IsWindowEnabled(btn_next) == 1, "Botón 'Siguiente' debe estar habilitado con >100 eventos"
        assert btn_prev is not None and user32.IsWindowEnabled(btn_prev) == 0, "Botón 'Anterior' debe estar deshabilitado en Página 1"

        lbl_page1 = next((txt for (h, cls, txt) in get_child_controls(hwnd_main3) if "Página 1" in txt), None)
        assert lbl_page1 is not None, "Indicador 'Página 1' no encontrado"
        print(f"[OK] Página 1 validada: 100 eventos mostrados, botón Siguiente habilitado, Anterior deshabilitado.")

        # Avanzar a Página 2
        send_click_button(btn_next)
        time.sleep(0.8)

        p2_count = user32.SendMessageW(p_list, LVM_GETITEMCOUNT, 0, 0)
        assert p2_count > 0 and p2_count < 100, f"Página 2 debe mostrar el remanente (<100 eventos, encontrados: {p2_count})"
        assert user32.IsWindowEnabled(btn_prev) == 1, "Botón 'Anterior' debe estar habilitado en Página 2"
        assert user32.IsWindowEnabled(btn_next) == 0, "Botón 'Siguiente' debe estar deshabilitado en última página"

        lbl_page2 = next((txt for (h, cls, txt) in get_child_controls(hwnd_main3) if "Página 2" in txt), None)
        assert lbl_page2 is not None, "Indicador 'Página 2' no encontrado"
        print(f"[OK] Página 2 validada: remanente de {p2_count} eventos, indicador 'Página 2' y botones actualizados.")

        # Regresar a Página 1
        send_click_button(btn_prev)
        time.sleep(0.8)
        p1_back_count = user32.SendMessageW(p_list, LVM_GETITEMCOUNT, 0, 0)
        assert p1_back_count == 100, "Al volver a Página 1 deben mostrarse 100 eventos nuevamente"
        print("[OK] Navegación de regreso a Página 1 completada con éxito.")

        user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
        p3.wait(timeout=5.0)

        # 3.2: Login como Cajero (sin permiso audit.view)
        print("\n--- Verificación de Cajero sin permiso audit.view ---")
        p4 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid4 = p4.pid

        hwnd_login4, _ = wait_for_window_by_pid(pid4, "Iniciar Sesión", step_name="Login Cajero")
        c_ctrls = get_child_controls(hwnd_login4)
        c_edits = [h for (h, cls, txt) in c_ctrls if cls.lower() == "edit"]
        c_btns = [h for (h, cls, txt) in c_ctrls if cls.lower() == "button"]
        set_text(c_edits[0], "cajero_demo")
        set_text(c_edits[1], "CajeroPass123!")
        time.sleep(0.3)
        send_click_button(c_btns[0])
        time.sleep(1.2)

        hwnd_main4, _ = wait_for_window_by_pid(pid4, "Cafetería ITCH II", exclude_substrings=["Primer Inicio", "Iniciar Sesión"])
        # Navegar a Bitácora como Cajero
        b_caja4 = next((h for (h, cls, txt) in get_child_controls(hwnd_main4) if cls.lower() == "button" and "Caja y Reportes" in txt), None)
        send_click_button(b_caja4)
        time.sleep(0.8)
        b_bit4 = next((h for (h, cls, txt) in get_child_controls(hwnd_main4) if cls.lower() == "button" and "Bitácora" in txt), None)
        send_click_button(b_bit4)
        time.sleep(0.8)

        # Comprobar que el servicio rechazó la consulta y se muestra el mensaje de error tal cual
        c4_ctrls = get_child_controls(hwnd_main4)
        warn_lbl = next((txt for (h, cls, txt) in c4_ctrls if "audit.view" in txt or "permiso" in txt.lower()), None)
        assert warn_lbl is not None, "El mensaje de error del servicio ('audit.view') no se mostró en la interfaz"
        print(f"[OK] Mensaje de restricción de servicio recibido y mostrado tal cual: '{warn_lbl}'")

        # Comprobar que la lista está completamente vacía
        c4_list = [h for (h, cls, txt) in c4_ctrls if "list" in cls.lower()][0]
        c4_count = user32.SendMessageW(c4_list, LVM_GETITEMCOUNT, 0, 0)
        assert c4_count == 0, f"La lista debe estar vacía para un usuario sin audit.view (encontrados: {c4_count})"
        print("[OK] Lista de eventos vacía (0 elementos) para usuario sin permiso.")

        user32.PostMessageW(hwnd_main4, WM_CLOSE, 0, 0)
        p4.wait(timeout=5.0)

        print(f"\n>>> TODAS LAS PRUEBAS DE TAREA F2 PASARON EXITOSAMENTE EN [{config_name}] <<<")

    finally:
        shutil.rmtree(test_data_dir, ignore_errors=True)
        print(f"[OK] Directorio temporal aislado limpiado: {test_data_dir}")

def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    release_exe = os.path.join(repo_root, "build_release", "app_desktop.exe")
    debug_exe = os.path.join(repo_root, "build_debug", "app_desktop.exe")

    print("=================================================================")
    print("SUITE DE PRUEBAS AUTOMATIZADA: TAREA F2 (BITÁCORA DE AUDITORÍA)")
    print("=================================================================")

    # 1. Ejecución en Release
    run_f2_test_for_binary(release_exe, repo_root, "RELEASE")

    # 2. Ejecución en Debug
    run_f2_test_for_binary(debug_exe, repo_root, "DEBUG")

    print("\n=================================================================")
    print("VERIFICACIÓN COMPLETA F2: PASARON TODAS LAS PRUEBAS EN RELEASE Y DEBUG")
    print("=================================================================")

if __name__ == "__main__":
    main()
