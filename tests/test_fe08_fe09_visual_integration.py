#!/usr/bin/env python3
"""
test_fe08_fe09_visual_integration.py
Verificación rigurosa de geometría, textos íntegros y adaptabilidad FE-08 / FE-09:
1. Ausencia de recortes y comprobación geométrica de controles vs contenedores (Win32 Rects).
2. Verificación de importes grandes íntegros ($2,900.00) con GetTextExtentPoint32W (control_w >= text_extent_w).
3. Matriz de pruebas:
   - 1200x720 en Perfil Estándar con nombres/notas largos e importe grande.
   - 1200x720 en Perfil Touch (txt_notes ancho >= 350px, touch targets >= 48px, Más acciones visible).
   - 960x600 (ventana estrecha) en Perfil Touch (sin desbordamiento horizontal).
   - 960x600 en Perfil Estándar (sin desbordamiento horizontal).
4. Detección y reporte de escalas DPI (100%, 125%, 150%, 200%).
5. Persistencia del perfil por puesto (station_profile.cfg) y conservación del carrito al alternar.
6. Aislamiento de base de datos SQLite y gestión de procesos por PID con timeout estricto.
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

WM_SETTEXT = 0x000C
WM_CLOSE = 0x0010
WM_COMMAND = 0x0111
WM_GETFONT = 0x0031
BM_CLICK = 0x00F5
SWP_NOZORDER = 0x0004
SWP_NOMOVE = 0x0002

def get_window_pid(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value

def find_windows_by_pid(target_pid, title_substring=""):
    found = []
    def enum_cb(hwnd, _):
        if user32.IsWindowVisible(hwnd) and get_window_pid(hwnd) == target_pid:
            length = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            text = buf.value
            if not title_substring or title_substring.lower() in text.lower():
                found.append((hwnd, text))
        return True
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return found

def wait_for_window_by_pid(target_pid, title_substring, timeout=12.0, exclude_substrings=None, step_name=""):
    start = time.time()
    while time.time() - start < timeout:
        wins = find_windows_by_pid(target_pid, title_substring)
        if exclude_substrings:
            wins = [w for w in wins if not any(ex.lower() in w[1].lower() for ex in exclude_substrings)]
        if wins:
            return wins[0]
        time.sleep(0.2)
    all_proc_wins = find_windows_by_pid(target_pid, "")
    titles = [t for (_, t) in all_proc_wins]
    print(f"[TIMEOUT {timeout}s] Esperando ventana '{title_substring}' en PID {target_pid} para '{step_name}'. Ventanas visibles: {titles}")
    return None, ""

def get_child_controls(parent_hwnd):
    controls = []
    def enum_child(hwnd, _):
        class_buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, class_buf, 256)
        length = user32.GetWindowTextLengthW(hwnd)
        text_buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, text_buf, length + 1)
        controls.append((hwnd, class_buf.value, text_buf.value))
        return True
    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(enum_child), 0)
    return controls

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

def send_click_button(hwnd):
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)

def post_click_button(hwnd):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)

def get_window_dpi(hwnd):
    if HAS_GET_DPI:
        dpi = user32.GetDpiForWindow(hwnd)
        if dpi > 0:
            return dpi
    hdc = user32.GetDC(hwnd)
    if hdc:
        dpi = gdi32.GetDeviceCaps(hdc, 88) # LOGPIXELSX
        user32.ReleaseDC(hwnd, hdc)
        return dpi
    return 96

def seed_demo_catalog(db_path):
    con = sqlite3.connect(db_path)
    cur = con.cursor()
    cur.execute("""
        INSERT OR IGNORE INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold)
        VALUES 
            ('ing-01', 'Pan Bolillo Especial Artesanal', 'COUNT', 'pz', 500, 50),
            ('ing-02', 'Jamon Especial Premium de Pierna', 'MASS', 'mg', 10000000, 1000000);
    """)
    cur.execute("""
        INSERT OR IGNORE INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
        VALUES
            ('prod-01', 'TOR-01', '750100001001', 'Torta Especial Gourmet ITCH Doble Porción con Ingredientes Seleccionados', 'Alimentos', 2500, 1, 1, '2026-09-30 08:00:00'),
            ('prod-02', 'CAF-01', '750100001002', 'Café de Olla Tradicional de Grano 16oz', 'Bebidas', 2000, 1, 1, '2026-09-30 08:00:00');
    """)
    cur.execute("""
        INSERT OR IGNORE INTO recipes (id, product_id, version, created_at)
        VALUES ('rec-01', 'prod-01', 1, '2026-09-30 08:00:00');
    """)
    cur.execute("""
        INSERT OR IGNORE INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units)
        VALUES ('ri-01', 'rec-01', 'ing-01', 1);
    """)
    con.commit()
    con.close()

def run_test_for_binary(exe_path, work_dir, label):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN GEOMÉTRICA Y VISUAL FE-08 / FE-09 [{label}]:")
    print(f"  Binario:  {exe_path}")
    print(f"=======================================================")
    assert os.path.exists(exe_path), f"Binario no encontrado: {exe_path}"

    test_data_dir = tempfile.mkdtemp(prefix="cafeteria_geom_test_")
    db_path = os.path.join(test_data_dir, "cafeteria.db")
    station_cfg = os.path.join(test_data_dir, "station_profile.cfg")
    p1 = None
    p2 = None
    p3 = None

    try:
        env = os.environ.copy()
        env["PATH"] = (
            r"D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll;"
            + work_dir + ";" + env.get("PATH", "")
        )
        env["CAFETERIA_DATA_DIR"] = test_data_dir

        # -------------------------------------------------------------
        # SESIÓN 1: Alta inicial del Dueño
        # -------------------------------------------------------------
        print("\n--- SESIÓN 1: Alta inicial del Dueño ---")
        p1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid1 = p1.pid
        hwnd_owner, _ = wait_for_window_by_pid(pid1, "Primer Inicio", step_name="Diálogo Primer Inicio")
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

        hwnd_main1, _ = wait_for_window_by_pid(pid1, "Cafetería ITCH II", exclude_substrings=["Primer Inicio", "Iniciar Sesión"], step_name="MainWindow alta inicial")
        assert hwnd_main1 is not None, "MainWindow no abrió tras alta inicial"
        print("[OK] Alta inicial completada y MainWindow abierta.")
        time.sleep(0.8)

        user32.PostMessageW(hwnd_main1, WM_CLOSE, 0, 0)
        p1.wait(timeout=5.0)
        p1 = None
        print("[OK] Sesión 1 cerrada limpiamente.")

        seed_demo_catalog(db_path)

        # -------------------------------------------------------------
        # SESIÓN 2: Pruebas Geométricas Estrictas (1200x720 y 960x600)
        # -------------------------------------------------------------
        print("\n--- SESIÓN 2: Login y Verificación Geométrica Estricta ---")
        p2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid2 = p2.pid

        hwnd_login, _ = wait_for_window_by_pid(pid2, "Iniciar Sesión", step_name="Diálogo Login Sesión 2")
        assert hwnd_login is not None, "Diálogo de login no apareció en sesión 2"
        l_ctrls = get_child_controls(hwnd_login)
        l_edits = [h for (h, cls, txt) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in l_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], "admin_owner")
        set_text(l_edits[1], "ClaveSegura123!")
        time.sleep(0.3)
        post_click_button(l_btns[0])

        hwnd_main2, _ = wait_for_window_by_pid(pid2, "Cafetería ITCH II", exclude_substrings=["Primer Inicio", "Iniciar Sesión"], step_name="MainWindow sesión 2")
        assert hwnd_main2 is not None, "MainWindow no abrió tras login en sesión 2"
        time.sleep(1.0)
        print("[OK] MainWindow activa y autenticada.")

        # Detección y reporte de DPI
        dpi = get_window_dpi(hwnd_main2)
        scale_pct = int(round(dpi / 96.0 * 100))
        print(f"[DPI] DPI detectado en ventana principal: {dpi} ({scale_pct}% de escala).")
        print(f"  - Escala {scale_pct}%: Verificación activa en ejecución.")
        for sc in [100, 125, 150, 200]:
            if sc != scale_pct:
                print(f"  - Escala {sc}%: [NO VERIFICADO] Entorno Win32 sin cambio dinámico de DPI físico en ejecución sin reinicio de sesión.")

        # Abrir Turno en Caja
        main_ctrls = get_child_controls(hwnd_main2)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_caja = next((h for (h, t) in btns if "Caja y Reportes" in t), None)
        assert btn_caja is not None, "Botón 'Caja y Reportes' no encontrado"
        send_click_button(btn_caja)
        time.sleep(0.8)

        shift_ctrls = get_child_controls(hwnd_main2)
        shift_btns = [(h, txt) for (h, cls, txt) in shift_ctrls if cls.lower() == "button"]
        btn_abrir = next((h for (h, t) in shift_btns if "Abrir Turno" in t), None)
        if btn_abrir:
            post_click_button(btn_abrir)
            for _ in range(30):
                time.sleep(0.2)
                dlg_conf = find_windows_by_pid(pid2, "Turno Abierto")
                if dlg_conf:
                    dlg_btns = [h for (h, cls, txt) in get_child_controls(dlg_conf[0][0]) if cls.lower() == "button"]
                    if dlg_btns: post_click_button(dlg_btns[0])
                    else: user32.PostMessageW(dlg_conf[0][0], WM_COMMAND, 1, 0)
                    break
            time.sleep(0.6)
            print("[OK] Turno de caja abierto.")

        # Regresar a Punto de Venta
        main_ctrls = get_child_controls(hwnd_main2)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_pos = next((h for (h, t) in btns if "Punto de Venta" in t), None)
        assert btn_pos is not None, "Botón 'Punto de Venta' no encontrado"
        send_click_button(btn_pos)
        time.sleep(0.8)

        # -------------------------------------------------------------
        # CASO 1: 1200x720 en Perfil Estándar con Importe Grande y Textos Largos
        # -------------------------------------------------------------
        print("\n--- CASO 1: Perfil Estándar a 1200x720 (Geometría e Importe Grande) ---")
        user32.SetWindowPos(hwnd_main2, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)

        # Agregar 116 unidades de producto con nombre largo y notas largas
        pos_ctrls = get_child_controls(hwnd_main2)
        p_edits = [h for (h, cls, txt) in pos_ctrls if cls.lower() == "edit"]
        p_btns = [(h, txt) for (h, cls, txt) in pos_ctrls if cls.lower() == "button"]
        btn_find = next((h for (h, t) in p_btns if "Buscar" in t), None)
        btn_add = next((h for (h, t) in p_btns if "Agregar al Carrito" in t), None)

        set_text(p_edits[0], "Torta Especial")
        send_click_button(btn_find)
        time.sleep(0.2)
        set_text(p_edits[1], "116")
        set_text(p_edits[2], "sin cebolla, con extra chipotle, bien tostado y para llevar en empaque térmico especial")
        send_click_button(btn_add)
        time.sleep(0.6)

        # Obtener controles del POS actualizados
        pos_ctrls_after = get_child_controls(hwnd_main2)
        static_texts = [(h, txt) for (h, cls, txt) in pos_ctrls_after if cls.lower() == "static"]
        
        # Localizar etiquetas de Subtotal y Total ($2,900.00)
        total_statics = [h for (h, txt) in static_texts if "$2,900.00" in txt]
        assert len(total_statics) >= 2, f"No se encontraron etiquetas con importe grande '$2,900.00' (encontrados: {[t for (_, t) in static_texts if '$' in t]})"
        
        lbl_subtotal_hwnd = total_statics[0]
        lbl_total_hwnd = total_statics[-1] # El último con $2,900.00 es el TOTAL

        # Obtener contenedor de la tarjeta de cuenta (padre de lbl_total_hwnd)
        card_hwnd = user32.GetParent(lbl_total_hwnd)
        assert card_hwnd != 0, "No se pudo obtener el contenedor de la tarjeta de cuenta"

        card_l, card_t, card_r, card_b, card_w, card_h = get_rect(card_hwnd)
        tot_l, tot_t, tot_r, tot_b, tot_w, tot_h = get_rect(lbl_total_hwnd)
        sub_l, sub_t, sub_r, sub_b, sub_w, sub_h = get_rect(lbl_subtotal_hwnd)

        print(f"  Geometría Card: L={card_l}, R={card_r}, W={card_w}px")
        print(f"  Geometría TOTAL: L={tot_l}, R={tot_r}, W={tot_w}px (Margen der={card_r - tot_r}px)")
        print(f"  Geometría Subtotal: L={sub_l}, R={sub_r}, W={sub_w}px (Margen der={card_r - sub_r}px)")

        # Comprobación geométrica: TOTAL y Subtotal deben estar dentro de la tarjeta con margen derecho >= 12px
        assert tot_r <= card_r - 12, f"TOTAL recortado o pegado al borde derecho: tot_r={tot_r} > card_r-12={card_r-12}"
        assert sub_r <= card_r - 12, f"Subtotal recortado o pegado al borde derecho: sub_r={sub_r} > card_r-12={card_r-12}"
        assert tot_l >= card_l + 12, f"TOTAL desbordado a la izquierda: tot_l={tot_l} < card_l+12={card_l+12}"

        # Comprobación de texto íntegro y extensión métrica GDI
        ext_w, ext_h = measure_text_extent(lbl_total_hwnd, "$2,900.00")
        print(f"  Métrica texto TOTAL '$2,900.00': ExtentW={ext_w}px, ControlW={tot_w}px")
        assert tot_w >= ext_w, f"Control TOTAL ({tot_w}px) más estrecho que su texto ({ext_w}px) -> Recorte de glifos"
        print("[OK] En Estándar a 1200x720: TOTAL ($2,900.00) íntegro, sin recorte métrico ni geométrico.")

        # Verificar botones contextuales y Más acciones
        p_btns_std = [(h, txt) for (h, cls, txt) in pos_ctrls_after if cls.lower() == "button"]
        btn_more_std = next((h for (h, t) in p_btns_std if "Más acciones" in t), None)
        assert btn_more_std is not None, "Botón 'Más acciones' no encontrado en Estándar"
        ml, mt, mr, mb, mw, mh = get_rect(btn_more_std)
        assert mr <= card_l, f"Botón Más acciones ({mr}) invade o sobrepasa tarjeta de cuenta ({card_l})"
        assert mw >= 120, f"Botón Más acciones demasiado estrecho ({mw}px < 120px)"
        print(f"[OK] Botón 'Más acciones' en Estándar: W={mw}px, totalmente visible sin colisión.")

        # -------------------------------------------------------------
        # CASO 2: 1200x720 en Perfil Touch (txt_notes, touch targets y Más acciones)
        # -------------------------------------------------------------
        print("\n--- CASO 2: Perfil Touch a 1200x720 (Campo Notas, Objetivos Táctiles y Más Acciones) ---")
        btn_touch_toggle = next((h for (h, t) in p_btns_std if "Touch" in t or "Estándar" in t), None)
        assert btn_touch_toggle is not None, "Toggle de perfil Touch no encontrado"
        send_click_button(btn_touch_toggle)
        time.sleep(0.8)

        # Verificar persistencia en archivo station_profile.cfg
        assert os.path.exists(station_cfg), "station_profile.cfg no se creó al alternar a Touch"
        with open(station_cfg, "r", encoding="utf-8") as f:
            cfg_content = f.read().strip()
        assert cfg_content == "touch", f"station_profile.cfg esperado 'touch', obtenido '{cfg_content}'"
        print("[OK] station_profile.cfg persistido con valor 'touch'.")

        # Verificar conservación del carrito
        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT COUNT(*), SUM(quantity) FROM cart_items")
        cart_row = cur.fetchone()
        con.close()
        assert cart_row[0] == 1 and cart_row[1] == 116, f"Carrito corrupto al cambiar a Touch: {cart_row}"
        print(f"[OK] Carrito conservado al 100%: {cart_row[0]} línea, {cart_row[1]} unidades.")

        # Controles en Touch
        touch_ctrls = get_child_controls(hwnd_main2)
        t_edits = [h for (h, cls, txt) in touch_ctrls if cls.lower() == "edit"]
        t_btns = [(h, txt) for (h, cls, txt) in touch_ctrls if cls.lower() == "button"]

        # txt_notes_ en Touch (es el tercer edit: [0]=search, [1]=qty, [2]=notes)
        assert len(t_edits) >= 3, f"Faltan campos Edit en Touch: {len(t_edits)}"
        txt_notes_touch = t_edits[2]
        parent = user32.GetParent(txt_notes_touch)
        target_notes = parent if parent else txt_notes_touch
        nl, nt, nr, nb, nw, nh = get_rect(target_notes)
        print(f"  Geometría txt_notes en Touch: W={nw}px, H={nh}px")
        # El campo de notas debe ser amplio (fila completa, >= 350px útil a 1200x720) y táctil (H >= 40px)
        assert nw >= 350, f"Campo de notas en Touch demasiado estrecho: {nw}px < 350px"
        assert nh >= 40, f"Campo de notas en Touch demasiado bajo: {nh}px < 40px"
        print(f"[OK] Campo de notas en Touch tiene ancho amplio ({nw}px >= 350px) y altura táctil ({nh}px).")

        # Verificar botón Más acciones en Touch
        btn_more_touch = next((h for (h, t) in t_btns if "Más acciones" in t), None)
        assert btn_more_touch is not None, "Botón 'Más acciones' no encontrado en Touch"
        tml, tmt, tmr, tmb, tmw, tmh = get_rect(btn_more_touch)
        print(f"  Geometría Más acciones en Touch: W={tmw}px, H={tmh}px, R={tmr}px")
        assert tmw >= 135, f"Más acciones cortado en Touch: ancho {tmw}px < 135px"
        assert tmh >= 48, f"Más acciones altura no táctil: {tmh}px < 48px"

        # Verificar tarjeta y total en Touch
        t_statics = [(h, txt) for (h, cls, txt) in touch_ctrls if cls.lower() == "static"]
        t_totals = [h for (h, txt) in t_statics if "$2,900.00" in txt]
        assert len(t_totals) >= 2, f"Total $2,900.00 no encontrado en Touch"
        lbl_ttotal = t_totals[-1]
        tcard_hwnd = user32.GetParent(lbl_ttotal)
        tcard_l, tcard_t, tcard_r, tcard_b, tcard_w, tcard_h = get_rect(tcard_hwnd)
        ttot_l, ttot_t, ttot_r, ttot_b, ttot_w, ttot_h = get_rect(lbl_ttotal)
        print(f"  Geometría Card Touch: W={tcard_w}px, TOTAL R={ttot_r}px (Margen der={tcard_r - ttot_r}px)")
        assert ttot_r <= tcard_r - 12, f"TOTAL en Touch recortado: ttot_r={ttot_r} > tcard_r-12={tcard_r-12}"
        ext_tw, ext_th = measure_text_extent(lbl_ttotal, "$2,900.00")
        assert ttot_w >= ext_tw, f"TOTAL en Touch más estrecho que texto: {ttot_w}px < {ext_tw}px"
        print("[OK] TOTAL ($2,900.00) en Touch íntegro sin recortes métricos ni geométricos.")

        # Verificar objetivos táctiles de botones en Touch (>= 48px de altura)
        touch_action_btns = [
            ("Editar cantidad", next((h for (h, t) in t_btns if "Editar cantidad" in t), None)),
            ("Editar nota", next((h for (h, t) in t_btns if "Editar nota" in t), None)),
            ("Quitar línea", next((h for (h, t) in t_btns if "Quitar línea" in t), None)),
            ("Más acciones", btn_more_touch),
            ("Agregar al Carrito", next((h for (h, t) in t_btns if "Agregar al Carrito" in t), None)),
            ("Cobrar", next((h for (h, t) in t_btns if "Cobrar" in t), None)),
            ("Cancelar pedido", next((h for (h, t) in t_btns if "Cancelar pedido" in t), None)),
        ]
        for name, btn_h in touch_action_btns:
            assert btn_h is not None, f"Botón '{name}' no encontrado en perfil Touch"
            _, _, _, _, bw, bh = get_rect(btn_h)
            print(f"  Objetivo táctil '{name}': {bw}x{bh}px (mínimo 48px)")
            assert bh >= 48, f"Botón '{name}' no cumple altura táctil mínima: {bh}px < 48px"
            assert bw >= 48, f"Botón '{name}' no cumple ancho táctil mínimo: {bw}px < 48px"
        print("[OK] Todos los botones interactivos en Touch cumplen requisitos de accesibilidad táctil (>=48px).")

        # Verificar altura del selector de producto igual a la de búsqueda (tolerancia 2 px)
        txt_search_touch = t_edits[0]
        search_box_parent = user32.GetParent(txt_search_touch)
        _, search_top, _, _, search_w, search_h = get_rect(search_box_parent if search_box_parent else txt_search_touch)
        selector_ctrl = next((
            h for (h, cls, txt) in touch_ctrls
            if ("combo" in cls.lower() or "combo" in txt.lower()) and abs(get_rect(h)[1] - search_top) <= 15
        ), None)
        assert selector_ctrl is not None, "Selector de productos no encontrado en fila de búsqueda Touch"
        _, _, _, _, sel_w, sel_h = get_rect(selector_ctrl)
        print(f"  Geometría Selector de Producto en Touch: W={sel_w}px, H={sel_h}px (Búsqueda H={search_h}px)")
        assert abs(sel_h - search_h) <= 2, f"Altura del selector ({sel_h}px) no coincide con búsqueda ({search_h}px) con tolerancia de 2px"
        print(f"[OK] Selector de producto tiene altura táctil {sel_h}px igual a búsqueda ({search_h}px, tol <= 2px).")

        # Verificar ancho de columna 0 (SKU) en la tabla del carrito >= GetTextExtent("BAG-01") + 8 px
        t_lists = [h for (h, cls, txt) in touch_ctrls if "listview" in cls.lower() or "list" in cls.lower()]
        assert len(t_lists) >= 1, "Tabla del carrito no encontrada en Touch"
        cart_list_hwnd = t_lists[0]
        col0_w = user32.SendMessageW(cart_list_hwnd, 0x101D, 0, 0)
        sku_tw, sku_th = measure_text_extent(cart_list_hwnd, "BAG-01")
        print(f"  Columna SKU (0) en Touch: ancho={col0_w}px, text_extent('BAG-01')={sku_tw}px (mínimo={sku_tw + 8}px)")
        assert col0_w >= sku_tw + 8, f"Columna SKU demasiado estrecha: {col0_w}px < {sku_tw + 8}px"
        print(f"[OK] Columna SKU en Touch ({col0_w}px >= {sku_tw + 8}px) previene truncado de BAG-01.")

        # -------------------------------------------------------------
        # CASO 3: Ventana Estrecha (960x600) en Perfil Touch
        # -------------------------------------------------------------
        print("\n--- CASO 3: Ventana Estrecha (960x600) en Perfil Touch ---")
        user32.SetWindowPos(hwnd_main2, 0, 0, 0, 960, 600, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.5)

        main_l, main_t, main_r, main_b, main_w, main_h = get_rect(hwnd_main2)
        n_ctrls_touch = get_child_controls(hwnd_main2)
        n_edits_touch = [h for (h, cls, txt) in n_ctrls_touch if cls.lower() == "edit"]
        n_notes = n_edits_touch[2]
        n_parent = user32.GetParent(n_notes)
        _, _, _, _, nnw, _ = get_rect(n_parent if n_parent else n_notes)
        assert nnw >= 200, f"Campo de notas en 960x600 Touch demasiado estrecho: {nnw}px < 200px"
        
        n_totals = [h for (h, cls, txt) in n_ctrls_touch if cls.lower() == "static" and "$2,900.00" in txt]
        assert len(n_totals) >= 2, "Total $2,900.00 no encontrado en 960x600 Touch"
        n_card = user32.GetParent(n_totals[-1])
        nc_l, nc_t, nc_r, nc_b, nc_w, nc_h = get_rect(n_card)
        nt_l, nt_t, nt_r, nt_b, nt_w, nt_h = get_rect(n_totals[-1])
        
        # Tarjeta debe caber dentro del marco de MainWindow
        assert nc_r <= main_r, f"Tarjeta de cuenta sobrepasa la ventana principal en 960x600: card_r={nc_r} > win_r={main_r}"
        assert nt_r <= nc_r - 10, f"TOTAL recortado en 960x600 Touch: total_r={nt_r} > card_r-10={nc_r-10}"
        print(f"[OK] Ventana estrecha 960x600 en Touch: Tarjeta contenida (W={nc_w}px), notas útiles (W={nnw}px), TOTAL sin recorte.")

        # -------------------------------------------------------------
        # CASO 4: Ventana Estrecha (960x600) en Perfil Estándar
        # -------------------------------------------------------------
        print("\n--- CASO 4: Ventana Estrecha (960x600) en Perfil Estándar ---")
        n_btns_touch = [(h, txt) for (h, cls, txt) in n_ctrls_touch if cls.lower() == "button"]
        btn_toggle_to_std = next((h for (h, t) in n_btns_touch if "Estándar" in t), None)
        assert btn_toggle_to_std is not None, "Botón toggle a Estándar no encontrado en 960x600"
        send_click_button(btn_toggle_to_std)
        time.sleep(0.8)

        n_ctrls_std = get_child_controls(hwnd_main2)
        n_std_totals = [h for (h, cls, txt) in n_ctrls_std if cls.lower() == "static" and "$2,900.00" in txt]
        assert len(n_std_totals) >= 2, "Total $2,900.00 no encontrado en 960x600 Estándar"
        n_std_card = user32.GetParent(n_std_totals[-1])
        nsc_l, nsc_t, nsc_r, nsc_b, nsc_w, nsc_h = get_rect(n_std_card)
        nst_l, nst_t, nst_r, nst_b, nst_w, nst_h = get_rect(n_std_totals[-1])

        assert nsc_r <= main_r, f"Tarjeta de cuenta sobrepasa ventana en 960x600 Estándar: {nsc_r} > {main_r}"
        assert nst_r <= nsc_r - 10, f"TOTAL recortado en 960x600 Estándar: {nst_r} > {nsc_r-10}"
        print(f"[OK] Ventana estrecha 960x600 en Estándar: Tarjeta contenida (W={nsc_w}px), TOTAL sin recorte.")

        # Restaurar tamaño a 1200x720 y volver a Touch para prueba de persistencia en arranque
        user32.SetWindowPos(hwnd_main2, 0, 0, 0, 1200, 720, SWP_NOMOVE | SWP_NOZORDER)
        time.sleep(0.3)
        n_btns_std = [(h, txt) for (h, cls, txt) in n_ctrls_std if cls.lower() == "button"]
        btn_toggle_to_touch = next((h for (h, t) in n_btns_std if "Touch" in t), None)
        if btn_toggle_to_touch:
            send_click_button(btn_toggle_to_touch)
            time.sleep(0.6)

        user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
        p2.wait(timeout=5.0)
        p2 = None
        print("[OK] Sesión 2 cerrada limpiamente.")

        # -------------------------------------------------------------
        # SESIÓN 3: Comprobación de Arranque con Perfil Recordado
        # -------------------------------------------------------------
        print("\n--- SESIÓN 3: Arranque con Perfil Recordado (Persistencia por puesto) ---")
        p3 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        pid3 = p3.pid
        hwnd_login3, _ = wait_for_window_by_pid(pid3, "Iniciar Sesión", step_name="Diálogo Login Sesión 3")
        assert hwnd_login3 is not None, "Diálogo de login no apareció en sesión 3"
        l_ctrls3 = get_child_controls(hwnd_login3)
        l_edits3 = [h for (h, cls, txt) in l_ctrls3 if cls.lower() == "edit"]
        l_btns3 = [h for (h, cls, txt) in l_ctrls3 if cls.lower() == "button"]
        set_text(l_edits3[0], "admin_owner")
        set_text(l_edits3[1], "ClaveSegura123!")
        time.sleep(0.3)
        post_click_button(l_btns3[0])

        hwnd_main3, _ = wait_for_window_by_pid(pid3, "Cafetería ITCH II", exclude_substrings=["Primer Inicio", "Iniciar Sesión"], step_name="MainWindow sesión 3")
        assert hwnd_main3 is not None, "MainWindow no abrió tras login en sesión 3"
        time.sleep(1.0)

        main_ctrls3 = get_child_controls(hwnd_main3)
        btn_toggle3 = next((txt for (h, cls, txt) in main_ctrls3 if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)), None)
        assert btn_toggle3 == "Estándar", f"MainWindow debió arrancar en Touch (mostrando 'Estándar' en botón toggle), obtenido: '{btn_toggle3}'"
        print(f"[OK] MainWindow arrancó automáticamente en perfil Touch recordado ('{btn_toggle3}' en botón toggle).")

        user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
        p3.wait(timeout=5.0)
        p3 = None
        print("[OK] Sesión 3 cerrada limpiamente.")

        print(f"\n>>> TODAS LAS PRUEBAS GEOMÉTRICAS Y VISUALES FE-08/FE-09 PASARON EXITOSAMENTE EN [{label}] <<<")

    finally:
        for p in [p1, p2, p3]:
            if p and p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2.0)
                except: p.kill()
        if os.path.exists(test_data_dir):
            shutil.rmtree(test_data_dir, ignore_errors=True)
            print(f"[OK] Directorio temporal aislado limpiado: {test_data_dir}")

def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    release_exe = os.path.join(repo_root, "build_release", "app_desktop.exe")
    debug_exe = os.path.join(repo_root, "build_debug", "app_desktop.exe")

    run_test_for_binary(release_exe, os.path.join(repo_root, "build_release"), "RELEASE")
    run_test_for_binary(debug_exe, os.path.join(repo_root, "build_debug"), "DEBUG")

if __name__ == "__main__":
    main()
