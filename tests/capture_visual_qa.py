#!/usr/bin/env python3
"""
capture_visual_qa.py - Captura automatizada de pantallas para QA visual (FE-09).
Genera capturas PNG de alta calidad de las vistas rediseñadas con el tema Cupertino / ITCH II:
1. POS en perfil Estándar (con carrito poblado y notas de preparación)
2. POS en perfil Touch (con controles ampliados de 52px)
3. Diálogo de Cobro (CheckoutDialog con cálculo de cambio)
4. Diálogo de Impresión (TicketPrintDialog con pregunta explícita)
5. Panel de Caja y Turnos (CashShiftPanel con estado y fondo inicial)
6. Panel de BackOffice (BackofficeUsersPanel con cuentas y roles)
7. Panel de BackOffice > Configuración (80 mm)
8. Panel de BackOffice > Configuración (55 mm)
9. Panel de BackOffice > Roles y Permisos (BackofficeRolesPanel con lista y matriz)
10. Panel de BackOffice > Roles y Permisos en perfil Touch (BackofficeRolesPanel ampliado)
11. Panel de Caja y Reportes > Bitácora (AuditLogPanel con filtros, lista y detalle)
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
from PIL import Image

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
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.ShowWindow.restype = wintypes.BOOL
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowRect.restype = wintypes.BOOL

WM_SETTEXT = 0x000C
WM_CLOSE = 0x0010
BM_CLICK = 0x00F5
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ('biSize', wintypes.DWORD),
        ('biWidth', wintypes.LONG),
        ('biHeight', wintypes.LONG),
        ('biPlanes', wintypes.WORD),
        ('biBitCount', wintypes.WORD),
        ('biCompression', wintypes.DWORD),
        ('biSizeImage', wintypes.DWORD),
        ('biXPelsPerMeter', wintypes.LONG),
        ('biYPelsPerMeter', wintypes.LONG),
        ('biClrUsed', wintypes.DWORD),
        ('biClrImportant', wintypes.DWORD)
    ]

class BITMAPINFO(ctypes.Structure):
    _fields_ = [
        ('bmiHeader', BITMAPINFOHEADER),
        ('bmiColors', wintypes.DWORD * 3)
    ]

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

def set_text(hwnd, text):
    text_buf = ctypes.create_unicode_buffer(text)
    user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(text_buf))

def send_click_button(hwnd):
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)

def post_click_button(hwnd):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)

def select_list_item(hwnd_list, row_index):
    """Envía clic de ratón a la fila indicada del SysListView32."""
    y = 35 + row_index * 22
    x = 40
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd_list, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_list, WM_LBUTTONUP, 0, lparam)
    time.sleep(0.3)

def capture_window(hwnd, output_path):
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.4)
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w = rect.right - rect.left
    h = rect.bottom - rect.top
    if w <= 0 or h <= 0:
        print(f"  [ERROR] Dimensiones de ventana inválidas: {w}x{h}")
        return
    hwnd_dc = user32.GetWindowDC(hwnd)
    mem_dc = gdi32.CreateCompatibleDC(hwnd_dc)
    bmp = gdi32.CreateCompatibleBitmap(hwnd_dc, w, h)
    old_bmp = gdi32.SelectObject(mem_dc, bmp)
    
    # Renderizar contenido completo
    user32.PrintWindow(hwnd, mem_dc, 2)
    
    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = w
    bmi.bmiHeader.biHeight = -h
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = 0
    
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem_dc, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    
    gdi32.SelectObject(mem_dc, old_bmp)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(hwnd, hwnd_dc)
    
    img = Image.frombuffer('RGBA', (w, h), buf, 'raw', 'BGRA', 0, 1)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.convert('RGB').save(output_path)
    print(f"  [CAPTURADO] {output_path} ({w}x{h})")

def seed_demo_catalog(db_path):
    con = sqlite3.connect(db_path)
    cur = con.cursor()
    cur.execute("""
        INSERT OR IGNORE INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold)
        VALUES 
            ('ing-01', 'Pan Baguette Artesanal', 'COUNT', 'pz', 40, 10),
            ('ing-02', 'Jamón Serrano Seleccionado', 'MASS', 'mg', 4000000, 500000),
            ('ing-03', 'Queso Gouda Madurado', 'MASS', 'mg', 3000000, 300000),
            ('ing-04', 'Café Pluma Hidalgo de Grano', 'MASS', 'mg', 5000000, 500000);
    """)
    cur.execute("""
        INSERT OR IGNORE INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
        VALUES
            ('prod-01', 'BAG-01', '750100002001', 'Baguette Serrano y Gouda', 'Alimentos', 6500, 1, 1, '2026-09-30 08:00:00'),
            ('prod-02', 'CAF-01', '750100002002', 'Café Americano Pluma 12oz', 'Bebidas', 3500, 1, 1, '2026-09-30 08:00:00'),
            ('prod-03', 'CAP-01', '750100002003', 'Capuchino Vainilla 16oz', 'Bebidas', 4800, 1, 1, '2026-09-30 08:00:00');
    """)
    cur.execute("""
        INSERT OR IGNORE INTO recipes (id, product_id, version, created_at)
        VALUES 
            ('rec-01', 'prod-01', 1, '2026-09-30 08:00:00'),
            ('rec-02', 'prod-02', 1, '2026-09-30 08:00:00');
    """)
    cur.execute("""
        INSERT OR IGNORE INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units)
        VALUES 
            ('ri-01', 'rec-01', 'ing-01', 1),
            ('ri-02', 'rec-01', 'ing-02', 60000),
            ('ri-03', 'rec-01', 'ing-03', 40000),
            ('ri-04', 'rec-02', 'ing-04', 18000);
    """)
    con.commit()
    con.close()

def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    exe_path = os.path.join(repo_root, "build_release", "app_desktop.exe")
    work_dir = os.path.join(repo_root, "build_release")
    out_dir = os.path.join(repo_root, "qa_visual", "screenshots")

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_qa_vis_")
    db_path = os.path.join(temp_dir, "cafeteria.db")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_dir

    print("=== INICIANDO CAPTURA DE PANTALLAS PARA QA VISUAL ===")
    proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
    time.sleep(1.5)

    try:
        # 1. Alta Dueño
        wins = find_windows_by_pid(proc.pid, "Primer Inicio")
        assert wins, "Diálogo Primer Inicio no encontrado"
        hwnd_init = wins[0][0]
        ctrls = get_child_controls(hwnd_init)
        edits = [h for (h, cls, txt) in ctrls if cls.lower() == "edit"]
        btns = [h for (h, cls, txt) in ctrls if cls.lower() == "button"]
        set_text(edits[0], "Administrador General")
        set_text(edits[1], "admin_qa")
        set_text(edits[2], "admin@itch.edu.mx")
        set_text(edits[3], "AdminSecure2026!")
        set_text(edits[4], "AdminSecure2026!")
        time.sleep(0.3)
        post_click_button(btns[0])
        time.sleep(1.5)

        # MainWindow
        mw_wins = find_windows_by_pid(proc.pid, "Cafetería ITCH II")
        assert mw_wins, "MainWindow no encontrada"
        hwnd_main = mw_wins[0][0]
        user32.SetForegroundWindow(hwnd_main)
        time.sleep(0.5)

        # Cerrar para sembrar catálogo
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=5)

        seed_demo_catalog(db_path)

        # Reabrir con catálogo listo
        proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        time.sleep(1.5)

        # Login
        lg_wins = find_windows_by_pid(proc.pid, "Iniciar Sesión")
        assert lg_wins, "Login no encontrado"
        hwnd_lg = lg_wins[0][0]
        lg_ctrls = get_child_controls(hwnd_lg)
        l_edits = [h for (h, cls, txt) in lg_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in lg_ctrls if cls.lower() == "button"]
        set_text(l_edits[0], "admin_qa")
        set_text(l_edits[1], "AdminSecure2026!")
        time.sleep(0.3)
        post_click_button(l_btns[0])
        time.sleep(1.5)

        mw_wins = find_windows_by_pid(proc.pid, "Cafetería ITCH II")
        assert mw_wins, "MainWindow tras login no encontrada"
        hwnd_main = mw_wins[0][0]

        # Abrir turno en Caja
        main_ctrls = get_child_controls(hwnd_main)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_caja = next((h for (h, t) in btns if "Caja y Reportes" in t), None)
        send_click_button(btn_caja)
        time.sleep(0.8)

        # CashShiftPanel
        shift_ctrls = get_child_controls(hwnd_main)
        shift_btns = [(h, txt) for (h, cls, txt) in shift_ctrls if cls.lower() == "button"]
        shift_edits = [h for (h, cls, txt) in shift_ctrls if cls.lower() == "edit"]
        if shift_edits:
            set_text(shift_edits[0], "500.00")
            time.sleep(0.2)
        btn_open = next((h for (h, t) in shift_btns if "Abrir Turno de Caja" in t), None)
        post_click_button(btn_open)
        time.sleep(1.0)
        # Cerrar diálogo 'Turno Abierto'
        dlg_open = find_windows_by_pid(proc.pid, "Turno Abierto")
        if dlg_open:
            d_btns = [h for (h, cls, txt) in get_child_controls(dlg_open[0][0]) if cls.lower() == "button"]
            if d_btns: post_click_button(d_btns[0])
            time.sleep(0.5)

        # CAPTURA 1: Panel de Caja y Turnos (Abierto con movimientos)
        capture_window(hwnd_main, os.path.join(out_dir, "05_cash_shift_panel.png"))

        # CAPTURA 11: Caja y Reportes > Bitácora
        caja_ctrls = get_child_controls(hwnd_main)
        caja_btns = [(h, txt) for (h, cls, txt) in caja_ctrls if cls.lower() == "button"]
        btn_bitacora = next((h for (h, t) in caja_btns if "Bitácora" in t), None)
        if btn_bitacora:
            send_click_button(btn_bitacora)
            time.sleep(0.8)
            lists = [h for (h, cls, txt) in get_child_controls(hwnd_main) if "list" in cls.lower()]
            if lists:
                select_list_item(lists[0], 0)
                time.sleep(0.4)
            capture_window(hwnd_main, os.path.join(out_dir, "11_audit_log_panel.png"))

        # Ir a Punto de Venta
        main_ctrls = get_child_controls(hwnd_main)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_pos = next((h for (h, t) in btns if "Punto de Venta" in t), None)
        send_click_button(btn_pos)
        time.sleep(0.8)

        # Agregar artículos con notas
        pos_ctrls = get_child_controls(hwnd_main)
        p_edits = [h for (h, cls, txt) in pos_ctrls if cls.lower() == "edit"]
        p_btns = [(h, txt) for (h, cls, txt) in pos_ctrls if cls.lower() == "button"]
        btn_find = next((h for (h, t) in p_btns if "Buscar" in t), None)
        btn_add = next((h for (h, t) in p_btns if "Agregar al Carrito" in t), None)

        set_text(p_edits[0], "Baguette")
        send_click_button(btn_find)
        time.sleep(0.2)
        set_text(p_edits[1], "2")
        set_text(p_edits[2], "bien tostado, sin aderezo")
        send_click_button(btn_add)
        time.sleep(0.3)

        set_text(p_edits[0], "Capuchino")
        send_click_button(btn_find)
        time.sleep(0.2)
        set_text(p_edits[1], "1")
        set_text(p_edits[2], "leche deslactosada, canela")
        send_click_button(btn_add)
        time.sleep(0.5)

        # CAPTURA 2: POS en Perfil Estándar con Carrito y Notas
        capture_window(hwnd_main, os.path.join(out_dir, "01_pos_standard.png"))

        # Cambiar a Perfil Touch
        btn_touch_toggle = next((h for (h, t) in p_btns if "Touch" in t or "Estándar" in t), None)
        if btn_touch_toggle:
            send_click_button(btn_touch_toggle)
            time.sleep(0.8)
            # CAPTURA 3: POS en Perfil Touch
            capture_window(hwnd_main, os.path.join(out_dir, "02_pos_touch.png"))
            # Regresar a Estándar
            send_click_button(btn_touch_toggle)
            time.sleep(0.5)

        # CAPTURA 6: Ir a BackOffice > Usuarios y cuentas
        main_ctrls = get_child_controls(hwnd_main)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_bo = next((h for (h, t) in btns if "BackOffice" in t), None)
        if btn_bo:
            send_click_button(btn_bo)
            time.sleep(0.8)
            capture_window(hwnd_main, os.path.join(out_dir, "06_backoffice_users_panel.png"))

            # CAPTURA 9: BackOffice > Roles y permisos (con Cajero seleccionado)
            main_ctrls = get_child_controls(hwnd_main)
            btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
            btn_roles = next((h for (h, t) in btns if "Roles y permisos" in t), None)
            if btn_roles:
                send_click_button(btn_roles)
                time.sleep(0.8)
                lists = [h for (h, cls, txt) in get_child_controls(hwnd_main) if "list" in cls.lower()]
                if lists:
                    select_list_item(lists[0], 0)
                    time.sleep(0.5)
                capture_window(hwnd_main, os.path.join(out_dir, "09_backoffice_roles_panel.png"))

                # CAPTURA 10: BackOffice > Roles y permisos en Perfil Touch (con Cajero seleccionado)
                hdr_ctrls = get_child_controls(hwnd_main)
                btn_touch = next((h for (h, cls, txt) in hdr_ctrls if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)), None)
                if btn_touch:
                    send_click_button(btn_touch)
                    time.sleep(0.8)
                    t_ctrls = get_child_controls(hwnd_main)
                    t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
                    btn_bo_t = next((h for (h, t) in t_btns if "BackOffice" in t), None)
                    if btn_bo_t:
                        send_click_button(btn_bo_t)
                        time.sleep(0.6)
                        t_ctrls = get_child_controls(hwnd_main)
                        t_btns = [(h, txt) for (h, cls, txt) in t_ctrls if cls.lower() == "button"]
                        btn_roles_t = next((h for (h, t) in t_btns if "Roles y permisos" in t), None)
                        if btn_roles_t:
                            send_click_button(btn_roles_t)
                            time.sleep(0.8)
                            t_lists = [h for (h, cls, txt) in get_child_controls(hwnd_main) if "list" in cls.lower()]
                            if t_lists:
                                select_list_item(t_lists[0], 0)
                                time.sleep(0.5)
                            capture_window(hwnd_main, os.path.join(out_dir, "10_backoffice_roles_panel_touch.png"))

                    # Regresar a perfil Estándar
                    hdr_ctrls2 = get_child_controls(hwnd_main)
                    btn_touch2 = next((h for (h, cls, txt) in hdr_ctrls2 if cls.lower() == "button" and ("Touch" in txt or "Estándar" in txt)), None)
                    if btn_touch2:
                        send_click_button(btn_touch2)
                        time.sleep(0.8)
                        # Re-navegar a BackOffice para continuar el flujo normal de capturas
                        b_ctrls = get_child_controls(hwnd_main)
                        b_btns = [(h, txt) for (h, cls, txt) in b_ctrls if cls.lower() == "button"]
                        btn_bo_back = next((h for (h, t) in b_btns if "BackOffice" in t), None)
                        if btn_bo_back:
                            send_click_button(btn_bo_back)
                            time.sleep(0.6)

            # CAPTURA 7 & 8: BackOffice > Configuración (80 mm y 55 mm)
            main_ctrls = get_child_controls(hwnd_main)
            btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
            btn_cfg = next((h for (h, t) in btns if "Configuración" in t), None)
            if btn_cfg:
                send_click_button(btn_cfg)
                time.sleep(0.8)
                capture_window(hwnd_main, os.path.join(out_dir, "07_print_config_80mm.png"))

                # Seleccionar SOLO el combo de perfil (identificado por "Perfil ..."), dejando la impresora como esté
                cfg_ctrls = get_child_controls(hwnd_main)
                combos = [h for (h, cls, txt) in cfg_ctrls if "combobox" in cls.lower()]
                cb_profile = None
                for cb in combos:
                    # Leer texto con WM_GETTEXTLENGTH + WM_GETTEXT
                    cb_l = user32.SendMessageW(cb, 0x000E, 0, 0)
                    if cb_l > 0:
                        cb_b = ctypes.create_unicode_buffer(cb_l + 1)
                        user32.SendMessageW(cb, 0x000D, cb_l + 1, ctypes.addressof(cb_b))
                        if "Perfil " in cb_b.value:
                            cb_profile = cb
                            break
                    # Alternativa por CB_GETLBTEXT (índice 0)
                    lb_len = user32.SendMessageW(cb, 0x0149, 0, 0) # CB_GETLBTEXTLEN
                    if lb_len > 0:
                        lb_buf = ctypes.create_unicode_buffer(lb_len + 1)
                        user32.SendMessageW(cb, 0x0148, 0, ctypes.addressof(lb_buf))
                        if "Perfil " in lb_buf.value:
                            cb_profile = cb
                            break

                assert cb_profile is not None, "Combobox de perfil de papel térmico ('Perfil ...') no encontrado"

                # Mostrar dropdown y seleccionar 55 mm con VK_DOWN + VK_RETURN (dispara CBN_SELCHANGE y wxEVT_CHOICE real)
                user32.SetForegroundWindow(hwnd_main)
                user32.SetFocus(cb_profile)
                time.sleep(0.2)
                user32.SendMessageW(cb_profile, 0x014F, 1, 0) # CB_SHOWDROPDOWN True
                time.sleep(0.2)
                user32.SendMessageW(cb_profile, 0x0100, 0x28, 0) # WM_KEYDOWN VK_DOWN
                user32.SendMessageW(cb_profile, 0x0101, 0x28, 0) # WM_KEYUP VK_DOWN
                time.sleep(0.1)
                user32.SendMessageW(cb_profile, 0x0100, 0x0D, 0) # WM_KEYDOWN VK_RETURN
                user32.SendMessageW(cb_profile, 0x0101, 0x0D, 0) # WM_KEYUP VK_RETURN
                time.sleep(0.6)

                # Leer el texto de la vista previa interactiva con WM_GETTEXTLENGTH + WM_GETTEXT y verificar que mide 28 caracteres
                edits = [h for (h, cls, txt) in get_child_controls(hwnd_main) if "edit" in cls.lower()]
                preview_text = ""
                for ed in edits:
                    ed_len = user32.SendMessageW(ed, 0x000E, 0, 0) # WM_GETTEXTLENGTH
                    if ed_len > 80:
                        buf = ctypes.create_unicode_buffer(ed_len + 1)
                        user32.SendMessageW(ed, 0x000D, ed_len + 1, ctypes.addressof(buf)) # WM_GETTEXT
                        if "Cafetería ITCH II" in buf.value or "V-DEMO" in buf.value or "==" in buf.value:
                            preview_text = buf.value
                            break

                assert preview_text, "No se encontró el texto de la vista previa en el panel de configuración"
                lines = preview_text.splitlines()
                sep_lines = [l.strip() for l in lines if l.strip() and all(c == '=' for c in l.strip())]
                assert len(sep_lines) > 0, "No se encontraron líneas de separación '=' en la vista previa"
                sep_len = len(sep_lines[0])
                print(f"[QA Vista Previa 55 mm] Longitud detectada de línea de separación: {sep_len} columnas")
                assert sep_len == 28, f"La línea de '=' no mide 28 caracteres (mide {sep_len})"

                capture_window(hwnd_main, os.path.join(out_dir, "08_print_config_55mm.png"))

        # Regresar a Punto de Venta
        main_ctrls = get_child_controls(hwnd_main)
        btns = [(h, txt) for (h, cls, txt) in main_ctrls if cls.lower() == "button"]
        btn_pos = next((h for (h, t) in btns if "Punto de Venta" in t), None)
        if btn_pos:
            send_click_button(btn_pos)
            time.sleep(0.8)

        # Abrir Cobro (CheckoutDialog)
        pos_ctrls = get_child_controls(hwnd_main)
        p_btns = [(h, txt) for (h, cls, txt) in pos_ctrls if cls.lower() == "button"]
        btn_cobrar = next((h for (h, t) in p_btns if "Cobrar" in t), None)
        post_click_button(btn_cobrar)
        time.sleep(0.8)

        chk_wins = find_windows_by_pid(proc.pid, "Cobro de Venta")
        assert chk_wins, "CheckoutDialog no encontrado"
        hwnd_chk = chk_wins[0][0]
        chk_ctrls = get_child_controls(hwnd_chk)
        c_edits = [h for (h, cls, txt) in chk_ctrls if cls.lower() == "edit"]
        if c_edits:
            set_text(c_edits[0], "200.00")
            time.sleep(0.3)

        # CAPTURA 4: Diálogo de Cobro con desglose y cambio calculado
        capture_window(hwnd_chk, os.path.join(out_dir, "03_checkout_dialog.png"))

        # Confirmar cobro para abrir TicketPrintDialog
        c_btns = [(h, txt) for (h, cls, txt) in chk_ctrls if cls.lower() == "button"]
        btn_conf = next((h for (h, t) in c_btns if "Confirmar Venta" in t), None)
        post_click_button(btn_conf)
        time.sleep(0.8)

        t_wins = find_windows_by_pid(proc.pid, "Venta Registrada")
        assert t_wins, "TicketPrintDialog no encontrado"
        hwnd_t = t_wins[0][0]

        # CAPTURA 5: Diálogo de Impresión de Ticket
        capture_window(hwnd_t, os.path.join(out_dir, "04_ticket_dialog.png"))

        # Terminar proceso limpiamente
        user32.PostMessageW(hwnd_t, WM_CLOSE, 0, 0)
        time.sleep(0.5)
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=5)
        print("=== TODAS LAS CAPTURAS DE QA VISUAL COMPLETADAS EXITOSAMENTE ===")

    finally:
        if proc.poll() is None:
            proc.kill()
        shutil.rmtree(temp_dir, ignore_errors=True)

if __name__ == "__main__":
    main()
