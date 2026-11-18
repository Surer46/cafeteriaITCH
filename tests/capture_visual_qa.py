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
12. Panel de Inventario > Ingredientes en perfil Estándar (InventoryIngredientsPanel)
13. Panel de Inventario > Ingredientes en perfil Touch (InventoryIngredientsPanel ampliado)
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

from gui_dpi import (
    user32,
    gdi32,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_scale,
    format_dpi_info,
    min_px,
    to_px,
    select_list_row,
    enum_children,
    get_rect,
)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumWindows.restype = wintypes.BOOL
user32.EnumChildWindows.argtypes = [wintypes.HWND, WNDENUMPROC, wintypes.LPARAM]
user32.EnumChildWindows.restype = wintypes.BOOL
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.ShowWindow.restype = wintypes.BOOL
user32.GetWindowDC.argtypes = [wintypes.HWND]
user32.GetWindowDC.restype = wintypes.HDC
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.GetDIBits.argtypes = [
    wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
    ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT,
]
gdi32.GetDIBits.restype = ctypes.c_int

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
    """Envía clic de ratón a la fila indicada del SysListView32 usando LVM_GETITEMRECT."""
    select_list_row(hwnd_list, row_index, settle=0.3)

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
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    exe_path = os.path.join(repo_root, "build_release", "app_desktop.exe")
    work_dir = os.path.join(repo_root, "build_release")
    out_dir = os.path.join(repo_root, "qa_visual", "screenshots")

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_qa_vis_")
    db_path = os.path.join(temp_dir, "cafeteria.db")

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    print(f"=== INICIANDO CAPTURA DE PANTALLAS PARA QA VISUAL ({format_dpi_info(sys_dpi)}) ===")
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

                # Seleccionar SOLO el combo de perfil (bajo la etiqueta 'Perfil de Papel Térmico:')
                def _qa_rect(hwnd_x):
                    rc = wintypes.RECT()
                    user32.GetWindowRect(hwnd_x, ctypes.byref(rc))
                    return rc.left, rc.top, rc.right, rc.bottom, rc.right - rc.left, rc.bottom - rc.top

                scale_main = get_window_scale(hwnd_main)
                cfg_ctrls = get_child_controls(hwnd_main)
                combos = [h for (h, cls, txt) in cfg_ctrls if "combobox" in cls.lower() or txt == "comboBox"]
                cb_profile = None
                lbl_perf = next((h for (h, cls, txt) in cfg_ctrls if cls.lower() == "static" and "Perfil de Papel Térmico" in txt), None)
                if lbl_perf is not None:
                    lbl_bottom = _qa_rect(lbl_perf)[3]
                    below = [cb for cb in combos if _qa_rect(cb)[1] >= lbl_bottom - to_px(4, scale_main)]
                    if below:
                        cb_profile = min(below, key=lambda h: _qa_rect(h)[1])

                assert cb_profile is not None, "Combobox de perfil de papel térmico ('Perfil ...') no encontrado"

                # Mostrar dropdown y seleccionar 55 mm con VK_DOWN + VK_RETURN (dispara wxEVT_COMBOBOX real)
                user32.SetForegroundWindow(hwnd_main)
                time.sleep(0.15)
                _, _, _, _, cbp_w, cbp_h = _qa_rect(cb_profile)
                lp_cb = ((cbp_h // 2) << 16) | (max(to_px(4, scale_main), cbp_w - to_px(12, scale_main)) & 0xFFFF)
                user32.PostMessageW(cb_profile, 0x0201, 1, lp_cb) # WM_LBUTTONDOWN
                time.sleep(0.05)
                user32.PostMessageW(cb_profile, 0x0202, 0, lp_cb) # WM_LBUTTONUP
                time.sleep(0.25)
                target_cb = user32.GetFocus() or cb_profile
                user32.PostMessageW(target_cb, 0x0100, 0x28, 0) # WM_KEYDOWN VK_DOWN
                user32.PostMessageW(target_cb, 0x0101, 0x28, 0) # WM_KEYUP VK_DOWN
                time.sleep(0.1)
                user32.PostMessageW(target_cb, 0x0100, 0x0D, 0) # WM_KEYDOWN VK_RETURN
                user32.PostMessageW(target_cb, 0x0101, 0x0D, 0) # WM_KEYUP VK_RETURN
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

        # Terminar diálogo de ticket antes de capturas de Inventario > Ingredientes
        user32.PostMessageW(hwnd_t, WM_CLOSE, 0, 0)
        time.sleep(0.5)

        def _f7_list_buttons(hwnd):
            out = []
            buf_cls = ctypes.create_unicode_buffer(128)
            enum_proc_t = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def _cb(ch, _lp):
                if user32.IsWindowVisible(ch):
                    user32.GetClassNameW(ch, buf_cls, 128)
                    if buf_cls.value.lower() == "button":
                        ln = user32.GetWindowTextLengthW(ch)
                        txt = ""
                        if ln > 0:
                            tbuf = ctypes.create_unicode_buffer(ln + 2)
                            user32.GetWindowTextW(ch, tbuf, ln + 2)
                            txt = tbuf.value
                        out.append((ch, txt))
                return True
            cb_ref = enum_proc_t(_cb)
            user32.EnumChildWindows(wintypes.HWND(hwnd), cb_ref, wintypes.LPARAM(0))
            return out

        def _f7_btn_height(hwnd_btn):
            rc = wintypes.RECT()
            user32.GetWindowRect(wintypes.HWND(hwnd_btn), ctypes.byref(rc))
            return rc.bottom - rc.top

        def _f7_click_btn(hwnd_btn):
            user32.SendMessageTimeoutW(wintypes.HWND(hwnd_btn), 0x00F5, 0, 0, 0x0002, 2000, None)

        btn_inv = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Inventario" in t), None)
        assert btn_inv is not None, "Botón Inventario no encontrado"
        _f7_click_btn(btn_inv)
        time.sleep(0.4)
        btn_ing = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Ingredientes"), None)
        assert btn_ing is not None, "Subsección Ingredientes no encontrada"
        _f7_click_btn(btn_ing)
        time.sleep(0.6)

        btn_nuevo = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Nuevo ingrediente"), None)
        assert btn_nuevo is not None, "Botón Nuevo ingrediente no encontrado"
        scale_f7 = get_window_scale(hwnd_main)
        if _f7_btn_height(btn_nuevo) >= min_px(48, scale_f7):
            tbtn = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Touch" in t or "Estándar" in t), None)
            if tbtn:
                _f7_click_btn(tbtn)
                time.sleep(0.5)
                btn_inv = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Inventario" in t), None)
                if btn_inv:
                    _f7_click_btn(btn_inv)
                    time.sleep(0.35)
                btn_ing = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Ingredientes"), None)
                if btn_ing:
                    _f7_click_btn(btn_ing)
                    time.sleep(0.5)

        # CAPTURA 12: Inventario > Ingredientes (Estándar)
        capture_window(hwnd_main, os.path.join(out_dir, "12_inventory_ingredients.png"))

        # Cambiar a perfil Touch para CAPTURA 13
        btn_toggle = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Touch" in t or "Estándar" in t), None)
        assert btn_toggle is not None, "Botón cambio de perfil no encontrado"
        _f7_click_btn(btn_toggle)
        time.sleep(0.5)
        btn_inv2 = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Inventario" in t), None)
        if btn_inv2:
            _f7_click_btn(btn_inv2)
            time.sleep(0.35)
        btn_ing2 = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Ingredientes"), None)
        if btn_ing2:
            _f7_click_btn(btn_ing2)
            time.sleep(0.6)

        # CAPTURA 13: Inventario > Ingredientes (Touch)
        capture_window(hwnd_main, os.path.join(out_dir, "13_inventory_ingredients_touch.png"))

        # Cambiar temporalmente a Estándar para CAPTURA 27 (Reabastecimiento) y CAPTURA 28 (Barra)
        btn_to_std = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Estándar" in t), None)
        if btn_to_std:
            _f7_click_btn(btn_to_std)
            time.sleep(0.45)
        btn_inv_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Inventario" in t), None)
        if btn_inv_qa:
            _f7_click_btn(btn_inv_qa)
            time.sleep(0.35)
        btn_ing_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Ingredientes"), None)
        if btn_ing_qa:
            _f7_click_btn(btn_ing_qa)
            time.sleep(0.45)
        btn_new_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Nuevo ingrediente"), None)
        if btn_new_qa:
            _f7_click_btn(btn_new_qa)
            time.sleep(0.2)
            ing_qa_panel = user32.GetParent(btn_new_qa)
            ing_qa_edits = sorted(
                [(h, get_rect(h)) for (h, c, _t) in enum_children(ing_qa_panel) if user32.IsWindowVisible(h) and c.lower() == "edit"],
                key=lambda x: x[1][1],
            )
            if len(ing_qa_edits) >= 2:
                set_text(ing_qa_edits[0][0], "Limón")
                set_text(ing_qa_edits[1][0], "500")
                btn_save_qa = next((h for (h, c, t) in enum_children(ing_qa_panel) if c.lower() == "button" and t == "Guardar"), None)
                if btn_save_qa:
                    _f7_click_btn(btn_save_qa)
                    time.sleep(0.35)

        btn_restock_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Reabastecimiento"), None)
        assert btn_restock_qa is not None, "Subsección Reabastecimiento no encontrada para captura 27"
        _f7_click_btn(btn_restock_qa)
        time.sleep(0.45)
        btn_reg_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Registrar compra"), None)
        assert btn_reg_qa is not None, "Botón Registrar compra no encontrado para captura 27"
        restock_panel_qa = user32.GetParent(btn_reg_qa)
        restock_edits_qa = sorted(
            [(h, get_rect(h)) for (h, c, _t) in enum_children(restock_panel_qa) if user32.IsWindowVisible(h) and c.lower() == "edit"],
            key=lambda x: x[1][1],
        )
        if len(restock_edits_qa) >= 2:
            set_text(restock_edits_qa[0][0], "1000")
            set_text(restock_edits_qa[1][0], "50.00")
            _f7_click_btn(btn_reg_qa)
            time.sleep(0.4)
        # CAPTURA 27: Inventario > Reabastecimiento con lista
        capture_window(hwnd_main, os.path.join(out_dir, "27_inventory_restock.png"))

        btn_bar_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Barra"), None)
        assert btn_bar_qa is not None, "Subsección Barra no encontrada para captura 28"
        _f7_click_btn(btn_bar_qa)
        time.sleep(0.45)
        btn_merma_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Registrar merma"), None)
        assert btn_merma_qa is not None, "Botón Registrar merma no encontrado para captura 28"
        bar_form_qa = user32.GetParent(btn_merma_qa)
        bar_edits_qa = sorted(
            [(h, get_rect(h)) for (h, c, _t) in enum_children(bar_form_qa) if user32.IsWindowVisible(h) and c.lower() == "edit"],
            key=lambda x: x[1][1],
        )
        if len(bar_edits_qa) >= 2:
            set_text(bar_edits_qa[0][0], "200")
            set_text(bar_edits_qa[1][0], "Fruta machucada")
            _f7_click_btn(btn_merma_qa)
            time.sleep(0.4)
        # CAPTURA 28: Inventario > Barra con formulario de merma y movimientos
        capture_window(hwnd_main, os.path.join(out_dir, "28_inventory_bar.png"))

        # CAPTURA 29: Inventario > Productos y recetas (producto preparado con receta y costo)
        btn_prod_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Productos y recetas"), None)
        assert btn_prod_qa is not None, "Subsección Productos y recetas no encontrada para captura 29"
        _f7_click_btn(btn_prod_qa)
        time.sleep(0.5)
        pr_lists_qa = sorted(
            [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)],
            key=lambda x: x[1][0],
        )
        assert pr_lists_qa, "Lista de productos no encontrada para captura 29"
        select_list_row(pr_lists_qa[0][0], 0)
        time.sleep(0.4)
        capture_window(hwnd_main, os.path.join(out_dir, "29_inventory_products_recipes.png"))

        # CAPTURA 31: Inventario > Existencias (ingredientes, directo, preparado)
        btn_ex_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Existencias"), None)
        assert btn_ex_qa is not None, "Subsección Existencias no encontrada para captura 31"
        _f7_click_btn(btn_ex_qa)
        time.sleep(0.45)
        ex_lists_qa = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
        if ex_lists_qa:
            select_list_row(ex_lists_qa[0], 0)
            time.sleep(0.25)
        capture_window(hwnd_main, os.path.join(out_dir, "31_inventory_stock.png"))

        # Sembrar promoción 2x1 y combo de muestra antes de capturar Promociones (32), Combos (33) y POS (30)
        con_p = sqlite3.connect(db_path)
        try:
            cap_row = con_p.execute("SELECT id FROM products WHERE name LIKE '%Capuchino%' LIMIT 1").fetchone()
            bag_row = con_p.execute("SELECT id FROM products WHERE name LIKE '%Baguette%' LIMIT 1").fetchone()
            now_str = time.strftime("%Y-%m-%d %H:%M:%S")
            if cap_row:
                con_p.execute(
                    """
                    INSERT OR REPLACE INTO promotions
                    (id, name, kind, percent_bp, fixed_off_cents, buy_qty, pay_qty, scope_kind, priority, is_active, weekdays_mask, created_at, updated_at)
                    VALUES ('promo-qa-2x1', '2x1 Capuchino', 'NXM', 0, 0, 2, 1, 'PRODUCT', 10, 1, 0, ?, ?)
                    """,
                    (now_str, now_str),
                )
                con_p.execute("DELETE FROM promotion_targets WHERE promotion_id = 'promo-qa-2x1'")
                con_p.execute(
                    "INSERT INTO promotion_targets (promotion_id, product_id, category) VALUES ('promo-qa-2x1', ?, NULL)",
                    (cap_row[0],),
                )
            if cap_row and bag_row:
                con_p.execute(
                    """
                    INSERT OR REPLACE INTO products
                    (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at, updated_at, is_combo)
                    VALUES ('prod-qa-combo', 'CMB-QA', NULL, 'Combo Desayuno', 'Combos', 9500, 0, 1, ?, ?, 1)
                    """,
                    (now_str, now_str),
                )
                con_p.execute("DELETE FROM combo_items WHERE combo_product_id = 'prod-qa-combo'")
                con_p.execute(
                    "INSERT INTO combo_items (combo_product_id, component_product_id, quantity) VALUES ('prod-qa-combo', ?, 1)",
                    (bag_row[0],),
                )
                con_p.execute(
                    "INSERT INTO combo_items (combo_product_id, component_product_id, quantity) VALUES ('prod-qa-combo', ?, 1)",
                    (cap_row[0],),
                )
            con_p.commit()
        finally:
            con_p.close()

        # CAPTURA 32 y 33: Inventario > Promociones y combos
        btn_promo_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Promociones y combos"), None)
        assert btn_promo_qa is not None, "Subsección Promociones y combos no encontrada para capturas 32-33"
        _f7_click_btn(btn_promo_qa)
        time.sleep(0.45)
        promo_lists_qa = sorted(
            [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)],
            key=lambda x: x[1][0],
        )
        if promo_lists_qa:
            select_list_row(promo_lists_qa[0][0], 0)
            time.sleep(0.3)
        capture_window(hwnd_main, os.path.join(out_dir, "32_inventory_promotions.png"))

        btn_tab_cmb_qa = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Combos"), None)
        assert btn_tab_cmb_qa is not None, "Pestaña Combos no encontrada para captura 33"
        _f7_click_btn(btn_tab_cmb_qa)
        time.sleep(0.4)
        cmb_lists_qa = sorted(
            [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)],
            key=lambda x: x[1][0],
        )
        if cmb_lists_qa:
            select_list_row(cmb_lists_qa[0][0], 0)
            time.sleep(0.3)
        capture_window(hwnd_main, os.path.join(out_dir, "33_inventory_combos.png"))

        # CAPTURA 30: Punto de Venta con promoción aplicada (2x1 Capuchino + descuento en carrito)

        btn_pos_30 = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Punto de Venta" in t), None)
        assert btn_pos_30 is not None, "Botón Punto de Venta no encontrado para captura 30"
        _f7_click_btn(btn_pos_30)
        time.sleep(0.45)
        pos_c30 = get_child_controls(hwnd_main)
        edits_30 = [h for (h, cls, _t) in pos_c30 if cls.lower() == "edit" and user32.IsWindowVisible(h)]
        btns_30 = [(h, t) for (h, cls, t) in pos_c30 if cls.lower() == "button" and user32.IsWindowVisible(h)]
        btn_find_30 = next((h for (h, t) in btns_30 if "Buscar" in t), None)
        btn_add_30 = next((h for (h, t) in btns_30 if "Agregar al Carrito" in t), None)
        if len(edits_30) >= 2 and btn_find_30 and btn_add_30:
            set_text(edits_30[0], "Capuchino")
            _f7_click_btn(btn_find_30)
            time.sleep(0.25)
            set_text(edits_30[1], "3")
            _f7_click_btn(btn_add_30)
            time.sleep(0.35)
        capture_window(hwnd_main, os.path.join(out_dir, "30_pos_discount.png"))

        # Volver a Inventario > Productos y recetas antes de restaurar Touch para no alterar pasos 15+
        btn_inv_back = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Inventario" in t), None)
        if btn_inv_back:
            _f7_click_btn(btn_inv_back)
            time.sleep(0.35)

        # Restaurar perfil Touch antes de BackOffice > Configuracion
        btn_to_touch = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Touch" in t), None)
        if btn_to_touch:
            _f7_click_btn(btn_to_touch)
            time.sleep(0.45)

        # Navegar a BackOffice > Configuracion (sigue en Touch)
        btn_bo2 = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "BackOffice" in t), None)
        assert btn_bo2 is not None, "Botón BackOffice no encontrado para T1"
        _f7_click_btn(btn_bo2)
        time.sleep(0.45)
        btn_cfg = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Configuración"), None)
        assert btn_cfg is not None, "Subsección Configuración no encontrada para T1"
        _f7_click_btn(btn_cfg)
        time.sleep(0.6)

        # CAPTURA 15: BackOffice > Configuración (Táctil)
        capture_window(hwnd_main, os.path.join(out_dir, "15_settings_touch.png"))

        def _t1_bottommost_btn(label_txt):
            matches = [h for (h, t) in _f7_list_buttons(hwnd_main) if t == label_txt]
            if not matches:
                return None
            def _top(hwnd_b):
                r = wintypes.RECT()
                user32.GetWindowRect(wintypes.HWND(hwnd_b), ctypes.byref(r))
                return r.top
            return max(matches, key=_top)

        # Cambiar a Estándar desde la pantalla de Configuración
        btn_std_cfg = _t1_bottommost_btn("Estándar")
        assert btn_std_cfg is not None, "Opción Estándar en Configuración no encontrada"
        _f7_click_btn(btn_std_cfg)
        time.sleep(0.6)

        # CAPTURA 14: BackOffice > Configuración (Estándar)
        capture_window(hwnd_main, os.path.join(out_dir, "14_settings_standard.png"))

        # Dejar Táctil para capturar teclado numérico, Login y Primer Inicio en Táctil
        btn_touch_cfg = _t1_bottommost_btn("Táctil")
        assert btn_touch_cfg is not None, "Opción Táctil en Configuración no encontrada"
        _f7_click_btn(btn_touch_cfg)
        time.sleep(0.6)

        # CAPTURA 20: BackOffice > Configuración con opción Pantalla completa activada
        btn_fs_cfg = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Usar pantalla completa"), None)
        assert btn_fs_cfg is not None, "Botón 'Usar pantalla completa' no encontrado en Configuración"
        _f7_click_btn(btn_fs_cfg)
        time.sleep(0.55)
        capture_window(hwnd_main, os.path.join(out_dir, "20_settings_fullscreen_option.png"))
        btn_fs_off = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Salir de pantalla completa" in t), None)
        assert btn_fs_off is not None, "Botón 'Salir de pantalla completa' no encontrado para restaurar ventana"
        _f7_click_btn(btn_fs_off)
        time.sleep(0.45)

        # CAPTURA 21 y 22: BackOffice > Respaldos (Touch) y Diálogo de advertencia de restauración
        def _t6_wait_window(title_sub, exclude=(), timeout_s=8.0):
            deadline = time.time() + timeout_s
            while time.time() < deadline:
                wins = [
                    (h, t) for (h, t) in find_windows_by_pid(proc.pid)
                    if title_sub in t and not any(ex in t for ex in exclude)
                ]
                if wins:
                    return wins[0][0]
                time.sleep(0.15)
            return None

        def _t6_enum_children_all(parent_h):
            items = []
            cls_buf = ctypes.create_unicode_buffer(128)
            txt_buf = ctypes.create_unicode_buffer(512)
            def _cb(ch, _lp):
                user32.GetClassNameW(ch, cls_buf, 128)
                user32.GetWindowTextW(ch, txt_buf, 512)
                items.append((ch, cls_buf.value, txt_buf.value))
                return True
            cb_ref = WNDENUMPROC(_cb)
            user32.EnumChildWindows(wintypes.HWND(parent_h), cb_ref, wintypes.LPARAM(0))
            return items

        def _t6_accept_file_dialog(dlg_title_sub, target_path):
            hwnd_fd = _t6_wait_window(dlg_title_sub, timeout_s=8.0)
            assert hwnd_fd is not None, f"Diálogo de archivos '{dlg_title_sub}' no apareció"
            time.sleep(0.45)
            children_fd = _t6_enum_children_all(hwnd_fd)
            edits_fd = [
                h for (h, c, _t) in children_fd
                if c.lower() == "edit" and user32.IsWindowVisible(h)
            ]
            assert edits_fd, "Campo Edit en diálogo de archivos no encontrado"
            def _parent_cls(hw):
                ph = user32.GetParent(hw)
                if not ph:
                    return ""
                pb = ctypes.create_unicode_buffer(128)
                user32.GetClassNameW(ph, pb, 128)
                return pb.value.lower()
            def _rect_top(hw):
                rr = wintypes.RECT()
                user32.GetWindowRect(hw, ctypes.byref(rr))
                return rr.top
            edit_name = next((h for h in edits_fd if "combobox" in _parent_cls(h)), None)
            if edit_name is None:
                edit_name = max(edits_fd, key=_rect_top)
            set_text(edit_name, target_path)
            time.sleep(0.15)
            btn_ok_fd = user32.GetDlgItem(hwnd_fd, 1)
            if not btn_ok_fd:
                btn_ok_fd = next(
                    (h for (h, c, t) in children_fd if c.lower() == "button" and user32.IsWindowVisible(h)
                     and ("Guardar" in t or "Abrir" in t or "Seleccionar" in t or "Aceptar" in t)),
                    None,
                )
            assert btn_ok_fd, "Botón Aceptar/Guardar/Abrir en diálogo de archivos no encontrado"
            user32.PostMessageW(btn_ok_fd, BM_CLICK, 0, 0)
            time.sleep(0.5)

        btn_respaldos = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Respaldos"), None)
        assert btn_respaldos is not None, "Subsección Respaldos no encontrada"
        _f7_click_btn(btn_respaldos)
        time.sleep(0.45)

        qa_backup_path = os.path.join(temp_dir, "cafeteria-qa-respaldo.db")
        btn_make_bkp = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Hacer un respaldo ahora" in t), None)
        assert btn_make_bkp is not None, "Botón 'Hacer un respaldo ahora' no encontrado"
        user32.PostMessageW(btn_make_bkp, BM_CLICK, 0, 0)
        _t6_accept_file_dialog("Guardar respaldo", qa_backup_path)
        hwnd_bkp_ok = _t6_wait_window("Respaldo guardado", timeout_s=8.0)
        assert hwnd_bkp_ok is not None, "Confirmación 'Respaldo guardado' no apareció"
        btn_ok_msg = next((h for (h, t) in _f7_list_buttons(hwnd_bkp_ok) if "Aceptar" in t), None)
        if not btn_ok_msg:
            btn_ok_msg = next((h for (h, _t) in _f7_list_buttons(hwnd_bkp_ok)), None)
        assert btn_ok_msg is not None, "Botón Aceptar en 'Respaldo guardado' no encontrado"
        _f7_click_btn(btn_ok_msg)
        time.sleep(0.4)
        capture_window(hwnd_main, os.path.join(out_dir, "21_backups_panel.png"))

        btn_rest_bkp = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Restaurar desde un respaldo" in t), None)
        assert btn_rest_bkp is not None, "Botón 'Restaurar desde un respaldo' no encontrado"
        user32.PostMessageW(btn_rest_bkp, BM_CLICK, 0, 0)
        _t6_accept_file_dialog("Elegir archivo de respaldo", qa_backup_path)
        hwnd_warn_r = _t6_wait_window("Revisar respaldo antes de restaurar", timeout_s=8.0)
        assert hwnd_warn_r is not None, "Diálogo de advertencia de restauración no apareció"
        time.sleep(0.35)
        capture_window(hwnd_warn_r, os.path.join(out_dir, "22_backups_restore_warning.png"))
        btn_cancel_r = next((h for (h, _c, t) in _t6_enum_children_all(hwnd_warn_r) if _c.lower() == "button" and "Cancelar" in t), None)
        assert btn_cancel_r is not None, "Botón Cancelar en advertencia de restauración no encontrado"
        _f7_click_btn(btn_cancel_r)
        time.sleep(0.35)

        # Configurar segunda carpeta temporal, desplazar vista y capturar Lista de respaldos + Segunda carpeta (captura 23)
        qa_usb_dir = os.path.join(temp_dir, "USB_Respaldo_QA")
        os.makedirs(qa_usb_dir, exist_ok=True)
        btn_choose_sec = next((h for (h, t) in _f7_list_buttons(hwnd_main) if t == "Elegir carpeta"), None)
        assert btn_choose_sec is not None, "Botón 'Elegir carpeta' no encontrado"
        user32.PostMessageW(btn_choose_sec, BM_CLICK, 0, 0)
        _t6_accept_file_dialog("Elegir segunda carpeta o memoria USB", qa_usb_dir)
        time.sleep(0.35)
        hwnd_sc_bkp = user32.GetParent(user32.GetParent(btn_choose_sec))
        if hwnd_sc_bkp:
            for _ in range(20):
                user32.SendMessageW(hwnd_sc_bkp, 0x0115, 1, 0)  # WM_VSCROLL, SB_LINEDOWN
            time.sleep(0.25)
        capture_window(hwnd_main, os.path.join(out_dir, "23_backups_list_and_secondary.png"))

        # CAPTURA 18 y 19: Teclado numérico integrado en POS (Touch) y Cobro (Touch)
        btn_pos_k = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Punto de Venta" in t), None)
        assert btn_pos_k is not None, "Botón Punto de Venta no encontrado para capturas 18-19"
        _f7_click_btn(btn_pos_k)
        time.sleep(0.5)
        btn_add_k = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Agregar al Carrito" in t), None)
        if btn_add_k is not None:
            _f7_click_btn(btn_add_k)
            time.sleep(0.35)
        pos_edits_k = []
        buf_cls_k = ctypes.create_unicode_buffer(128)
        enum_proc_k = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def _cb_edits_k(ch, _lp):
            if user32.IsWindowVisible(ch):
                user32.GetClassNameW(ch, buf_cls_k, 128)
                if buf_cls_k.value.lower() == "edit":
                    rk = wintypes.RECT()
                    user32.GetWindowRect(ch, ctypes.byref(rk))
                    pos_edits_k.append((ch, rk.right - rk.left))
            return True
        cb_edits_ref = enum_proc_k(_cb_edits_k)
        user32.EnumChildWindows(wintypes.HWND(hwnd_main), cb_edits_ref, wintypes.LPARAM(0))
        assert len(pos_edits_k) >= 2, "Campo Cantidad en POS no encontrado para captura 18"
        txt_qty_k = min(pos_edits_k, key=lambda it: it[1])[0]
        user32.PostMessageW(txt_qty_k, 0x0201, 1, 0x00080008)
        time.sleep(0.05)
        user32.PostMessageW(txt_qty_k, 0x0202, 0, 0x00080008)
        time.sleep(0.35)
        capture_window(hwnd_main, os.path.join(out_dir, "18_keypad_pos_touch.png"))

        btn_cobrar_k = next((h for (h, t) in _f7_list_buttons(hwnd_main) if "Cobrar" in t), None)
        assert btn_cobrar_k is not None and user32.IsWindowEnabled(btn_cobrar_k), "Botón Cobrar no disponible para captura 19"
        user32.PostMessageW(btn_cobrar_k, BM_CLICK, 0, 0)
        hwnd_chk_k = None
        for _ in range(30):
            wins_chk = find_windows_by_pid(proc.pid, "Cobro de Venta")
            if wins_chk:
                hwnd_chk_k = wins_chk[0][0]
                break
            time.sleep(0.2)
        assert hwnd_chk_k is not None, "Ventana Cobro de Venta no apareció para captura 19"
        time.sleep(0.4)
        capture_window(hwnd_chk_k, os.path.join(out_dir, "19_keypad_checkout_touch.png"))
        user32.PostMessageW(hwnd_chk_k, WM_CLOSE, 0, 0)
        time.sleep(0.35)

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=5)

        proc_extra = None
        temp_dir_owner_touch = None
        try:
            # CAPTURA 16: Login en perfil Táctil
            proc_extra = subprocess.Popen([exe_path], cwd=work_dir, env=env)
            hwnd_login_t = None
            for _ in range(30):
                wins_l = find_windows_by_pid(proc_extra.pid, "Iniciar Sesión")
                if wins_l:
                    hwnd_login_t = wins_l[0][0]
                    break
                time.sleep(0.2)
            assert hwnd_login_t is not None, "Ventana Iniciar Sesión (Táctil) no apareció"
            time.sleep(0.35)
            capture_window(hwnd_login_t, os.path.join(out_dir, "16_login_touch.png"))
            user32.PostMessageW(hwnd_login_t, WM_CLOSE, 0, 0)
            proc_extra.wait(timeout=5)
            proc_extra = None

            # CAPTURA 17: Alta del Dueño en perfil Táctil
            temp_dir_owner_touch = tempfile.mkdtemp(prefix="cafeteria_qa_owner_touch_")
            src_cfg = os.path.join(temp_dir, "station_profile.cfg")
            assert os.path.exists(src_cfg), "station_profile.cfg no encontrado tras activar Táctil"
            shutil.copy2(src_cfg, os.path.join(temp_dir_owner_touch, "station_profile.cfg"))

            env_owner_t = env.copy()
            env_owner_t["CAFETERIA_DATA_DIR"] = temp_dir_owner_touch
            proc_extra = subprocess.Popen([exe_path], cwd=work_dir, env=env_owner_t)
            hwnd_owner_t = None
            for _ in range(30):
                wins_o = find_windows_by_pid(proc_extra.pid, "Primer Inicio")
                if wins_o:
                    hwnd_owner_t = wins_o[0][0]
                    break
                time.sleep(0.2)
            assert hwnd_owner_t is not None, "Ventana Primer Inicio (Táctil) no apareció"
            time.sleep(0.35)
            capture_window(hwnd_owner_t, os.path.join(out_dir, "17_initial_owner_touch.png"))
            user32.PostMessageW(hwnd_owner_t, WM_CLOSE, 0, 0)
            proc_extra.wait(timeout=5)
            proc_extra = None
        finally:
            if proc_extra is not None and proc_extra.poll() is None:
                proc_extra.kill()
            if temp_dir_owner_touch:
                shutil.rmtree(temp_dir_owner_touch, ignore_errors=True)

        # CAPTURAS 24, 25 y 26: Actualizaciones (franja, Novedades con progreso y pestaña Actualizaciones)
        hooks_exe = os.path.join(repo_root, "build_hooks_release", "app_desktop.exe")
        if os.path.exists(hooks_exe):
            qa_upd_dir = tempfile.mkdtemp(prefix="cafeteria_qa_upd_")
            proc_upd = None
            try:
                upd_src = os.path.join(qa_upd_dir, "source")
                upd_data = os.path.join(qa_upd_dir, "data")
                os.makedirs(upd_data, exist_ok=True)
                inst_qa = os.path.join(qa_upd_dir, "CafeteriaITCH-0.2.0-instalador-win64.exe")
                with open(inst_qa, "wb") as f_inst:
                    f_inst.write(b"MZ_QA_INSTALLER_" * (32 * 1024))  # 512 KB -> 8 bloques de 64 KB
                notes_qa = os.path.join(qa_upd_dir, "notas.txt")
                with open(notes_qa, "w", encoding="utf-8") as f_n:
                    f_n.write(
                        "Ahora las actualizaciones se revisan al abrir sin molestar en caja.\n"
                        "Puedes descargar en segundo plano mientras sigues vendiendo.\n"
                        "La instalación se aplica al cerrar el programa y conserva tus datos.\n"
                    )
                pub_ps1 = os.path.join(repo_root, "tools", "publicar_version.ps1")
                priv_key = os.path.join(repo_root, "tests", "update_test_key.priv")
                subprocess.run(
                    [
                        "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", pub_ps1,
                        "-Version", "0.2.0", "-Instalador", inst_qa, "-Notas", notes_qa,
                        "-Clave", priv_key, "-Salida", upd_src,
                    ],
                    check=True,
                    capture_output=True,
                    text=True,
                )

                env_upd = os.environ.copy()
                env_upd["PATH"] = os.path.dirname(hooks_exe) + ";" + env_upd.get("PATH", "")
                env_upd["CAFETERIA_DATA_DIR"] = upd_data
                env_upd["CAFETERIA_UPDATE_TEST_SOURCE"] = upd_src
                env_upd["CAFETERIA_UPDATE_TEST_DELAY_MS"] = "350"

                proc_upd = subprocess.Popen([hooks_exe], cwd=os.path.dirname(hooks_exe), env=env_upd)
                hwnd_o_u = None
                for _ in range(40):
                    wins_o = find_windows_by_pid(proc_upd.pid, "Primer Inicio")
                    if wins_o:
                        hwnd_o_u = wins_o[0][0]
                        break
                    time.sleep(0.2)
                assert hwnd_o_u is not None, "Primer Inicio no apareció para capturas 24-26"
                o_ctrls_u = get_child_controls(hwnd_o_u)
                o_ed_u = [h for (h, cls, _t) in o_ctrls_u if cls.lower() == "edit"]
                o_btn_u = next(h for (h, cls, _t) in o_ctrls_u if cls.lower() == "button")
                set_text(o_ed_u[0], "Dueño QA")
                set_text(o_ed_u[1], "dueno_qa_u")
                set_text(o_ed_u[2], "dueno@qa.local")
                set_text(o_ed_u[3], "AdminSecure2026!")
                set_text(o_ed_u[4], "AdminSecure2026!")
                time.sleep(0.2)
                post_click_button(o_btn_u)

                hwnd_m_u = None
                for _ in range(50):
                    wins_m = [
                        (h, t) for (h, t) in find_windows_by_pid(proc_upd.pid, "Cafetería ITCH II")
                        if "Primer Inicio" not in t and "Iniciar Sesión" not in t
                    ]
                    if wins_m:
                        hwnd_m_u = wins_m[0][0]
                        break
                    time.sleep(0.2)
                assert hwnd_m_u is not None, "Ventana principal no abrió para capturas 24-26"

                # Esperar franja con "Ver novedades"
                btn_ver_nov = None
                for _ in range(45):
                    btn_ver_nov = next((h for (h, t) in _f7_list_buttons(hwnd_m_u) if t == "Ver novedades"), None)
                    if btn_ver_nov is not None:
                        break
                    time.sleep(0.2)
                assert btn_ver_nov is not None, "Franja de actualización no apareció para captura 24"
                time.sleep(0.35)
                capture_window(hwnd_m_u, os.path.join(out_dir, "24_update_banner.png"))

                # Captura 26: Pestaña Actualizaciones (con versión disponible visible)
                btn_bo_u = next((h for (h, t) in _f7_list_buttons(hwnd_m_u) if "BackOffice" in t), None)
                assert btn_bo_u is not None
                _f7_click_btn(btn_bo_u)
                time.sleep(0.35)
                btn_upd_tab = next((h for (h, t) in _f7_list_buttons(hwnd_m_u) if t == "Actualizaciones"), None)
                assert btn_upd_tab is not None, "Pestaña Actualizaciones no encontrada para captura 26"
                _f7_click_btn(btn_upd_tab)
                time.sleep(0.45)
                capture_window(hwnd_m_u, os.path.join(out_dir, "26_backoffice_updates_panel.png"))

                # Captura 25: Ventana Novedades con descarga en progreso
                btn_ver_nov2 = next((h for (h, t) in _f7_list_buttons(hwnd_m_u) if t == "Ver novedades"), None)
                assert btn_ver_nov2 is not None
                _f7_click_btn(btn_ver_nov2)
                hwnd_nov = None
                for _ in range(40):
                    wins_nov = find_windows_by_pid(proc_upd.pid, "Novedades de la versión")
                    if wins_nov:
                        hwnd_nov = wins_nov[0][0]
                        break
                    time.sleep(0.2)
                assert hwnd_nov is not None, "Ventana Novedades no apareció para captura 25"
                btn_dl_start = next((h for (h, t) in _f7_list_buttons(hwnd_nov) if t == "Descargar e instalar al cerrar"), None)
                assert btn_dl_start is not None, "Botón 'Descargar e instalar al cerrar' no encontrado"
                _f7_click_btn(btn_dl_start)
                time.sleep(1.3)
                capture_window(hwnd_nov, os.path.join(out_dir, "25_update_notes_progress.png"))
                user32.PostMessageW(hwnd_nov, WM_CLOSE, 0, 0)
                time.sleep(0.25)
                user32.PostMessageW(hwnd_m_u, WM_CLOSE, 0, 0)
                proc_upd.wait(timeout=8)
                proc_upd = None
            finally:
                if proc_upd is not None and proc_upd.poll() is None:
                    proc_upd.kill()
                shutil.rmtree(qa_upd_dir, ignore_errors=True)
        else:
            print(f"  [OMITIDO] No existe {hooks_exe}; capturas 24-26 omitidas.")

        print("=== TODAS LAS CAPTURAS DE QA VISUAL COMPLETADAS EXITOSAMENTE ===")

    finally:
        if proc.poll() is None:
            proc.kill()
        shutil.rmtree(temp_dir, ignore_errors=True)

if __name__ == "__main__":
    main()
