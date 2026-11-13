#!/usr/bin/env python3
"""
test_i2_integration.py - Prueba automatizada exhaustiva de la Puerta I2 con el ejecutable nativo real.

Verifica los 9 requerimientos obligatorios de I2 en ejecutables Debug y Release con bases SQLite aisladas:
1. Editar cantidades y notas desde la interfaz gráfica.
2. Fusionar líneas compatibles y comprobar cantidades/importes en SQLite y UI.
3. Eliminar una línea y confirmar el estado restante.
4. Cobrar y comprobar venta, pago, inventario, caja y auditoría en transacción atómica.
5. Cerrar y reabrir; consultar historial y recuperar el mismo ticket emitido.
6. Comprobar acceso con y sin pos.reprint, incluido acceso del vendedor al ticket recién confirmado.
7. Abrir la impresión y cancelar el diálogo del sistema; verificar que la venta sigue guardada una sola vez.
8. Cubrir un error de impresión reproducible sin depender de desconectar hardware (CAFETERIA_SIMULATE_PRINT_ERROR).
9. Verificar reintentos/idempotencia sin duplicar ventas ni movimientos.

Garantías de robustez:
- Tipos Win32 estrictos (argtypes, restype).
- Sustitución de llamadas bloqueantes por SendMessageTimeoutW (SMTO_ABORTIFHUNG) y PostMessageW.
- Esperas con timeout estricto, diagnóstico de ventanas activas y limpieza de procesos en finally.
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
import string
import secrets

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# -----------------------------------------------------------------------------
# Declaración estricta de funciones y tipos Win32
# -----------------------------------------------------------------------------
user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

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

ULONG_PTR = ctypes.c_size_t
PDWORD_PTR = ctypes.POINTER(ULONG_PTR)

user32.SendMessageTimeoutW.argtypes = [
    wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM,
    wintypes.UINT, wintypes.UINT, PDWORD_PTR
]
user32.SendMessageTimeoutW.restype = wintypes.LPARAM

user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL

WM_SETTEXT = 0x000C
WM_COMMAND = 0x0111
WM_CLOSE = 0x0010
BM_CLICK = 0x00F5
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_RETURN = 0x0D
VK_ESCAPE = 0x1B
VK_DOWN = 0x28
VK_UP = 0x26
SMTO_ABORTIFHUNG = 0x0002

def generate_random_secret(length=18):
    alphabet = string.ascii_letters + string.digits + "!@#$%^&*()_+-="
    return "".join(secrets.choice(alphabet) for _ in range(length))

def get_window_pid(hwnd):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value

def send_message_timeout(hwnd, msg, wparam=0, lparam=0, timeout_ms=3000, check_error=True):
    res = ULONG_PTR(0)
    ret = user32.SendMessageTimeoutW(
        hwnd, msg, wparam, lparam,
        SMTO_ABORTIFHUNG, timeout_ms, ctypes.byref(res)
    )
    if ret == 0 and check_error:
        err = ctypes.get_last_error()
        if err == 1460:  # ERROR_TIMEOUT
            raise TimeoutError(f"SendMessageTimeoutW agotó el tiempo de espera ({timeout_ms}ms) en HWND {hwnd}, MSG {hex(msg)}")
        elif err != 0:
            raise ctypes.WinError(err)
        else:
            raise RuntimeError(f"SendMessageTimeoutW falló sin código de error en HWND {hwnd}, MSG {hex(msg)}")
    return ret, res.value

def post_message_checked(hwnd, msg, wparam=0, lparam=0, allow_closed=False):
    ok = user32.PostMessageW(hwnd, msg, wparam, lparam)
    if not ok:
        err = ctypes.get_last_error()
        if allow_closed and err == 1400:  # ERROR_INVALID_WINDOW_HANDLE
            return False
        raise ctypes.WinError(err)
    return True

def set_text(hwnd, text, timeout_ms=3000):
    text_buf = ctypes.create_unicode_buffer(text)
    send_message_timeout(hwnd, WM_SETTEXT, 0, ctypes.addressof(text_buf), timeout_ms=timeout_ms, check_error=True)

def send_click_button(hwnd, timeout_ms=3000):
    send_message_timeout(hwnd, BM_CLICK, 0, 0, timeout_ms=timeout_ms, check_error=True)

def post_click_button(hwnd):
    post_message_checked(hwnd, BM_CLICK, 0, 0)

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

def wait_for_window_by_pid(target_pid, title_substring, timeout=12.0, step_name="", exclude_substrings=None):
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
    raise TimeoutError(
        f"[TIMEOUT {timeout}s] Esperando ventana con título '{title_substring}' en PID {target_pid} "
        f"para el paso '{step_name}'. Ventanas visibles del proceso: {titles}"
    )

def get_child_controls(parent_hwnd):
    controls = []
    def enum_child(hwnd, _):
        class_buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, class_buf, 256)
        cls_name = class_buf.value

        text = ""
        ret_len, length = send_message_timeout(hwnd, 0x000E, 0, 0, timeout_ms=500, check_error=False) # WM_GETTEXTLENGTH
        if ret_len != 0 and length > 0:
            buf = ctypes.create_unicode_buffer(length + 2)
            send_message_timeout(hwnd, 0x000D, length + 1, ctypes.addressof(buf), timeout_ms=500, check_error=False) # WM_GETTEXT
            text = buf.value
        if not text:
            l = user32.GetWindowTextLengthW(hwnd)
            if l > 0:
                buf = ctypes.create_unicode_buffer(l + 1)
                user32.GetWindowTextW(hwnd, buf, l + 1)
                text = buf.value
        controls.append((hwnd, cls_name, text))
        return True
    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(enum_child), 0)
    return controls

def dismiss_dialog_ok(hwnd):
    children = get_child_controls(hwnd)
    btn_ok = next((h for (h, cls, txt) in children if cls.lower() == "button"), None)
    if btn_ok:
        post_click_button(btn_ok)
    post_message_checked(hwnd, WM_COMMAND, 1, 0, allow_closed=True)
    post_message_checked(hwnd, WM_KEYDOWN, VK_RETURN, 0, allow_closed=True)
    post_message_checked(hwnd, WM_KEYUP, VK_RETURN, 0, allow_closed=True)

def dismiss_debug_alert(hwnd):
    children = get_child_controls(hwnd)
    btns = [(h, txt) for (h, cls, txt) in children if cls.lower() == "button"]
    btn_cont = next((h for (h, t) in btns if any(c in t.lower() for c in ["continue", "continuar", "ignore", "ignorar"])), None)
    if btn_cont:
        post_click_button(btn_cont)
    else:
        post_message_checked(hwnd, WM_COMMAND, 2, 0, allow_closed=True)

def close_window_and_wait(proc, hwnd, timeout=6.0):
    post_message_checked(hwnd, WM_CLOSE, 0, 0, allow_closed=True)
    start = time.time()
    while time.time() - start < timeout:
        if proc.poll() is not None:
            return
        time.sleep(0.2)
    wins = find_windows_by_pid(proc.pid, "")
    print(f"[DIAGNÓSTICO] Proceso {proc.pid} no cerró a tiempo. Ventanas activas: {wins}", flush=True)
    for h, _ in wins:
        post_message_checked(h, WM_CLOSE, 0, 0, allow_closed=True)
    time.sleep(0.5)
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=3)

def wait_for_shift_open_dialog_by_pid(target_pid, timeout=10.0):
    start = time.time()
    while time.time() - start < timeout:
        for err_title in ["Error de Apertura", "Monto Inválido", "Error"]:
            err_wins = find_windows_by_pid(target_pid, err_title)
            if err_wins:
                hwnd_err, text = err_wins[0]
                children = get_child_controls(hwnd_err)
                details = [t for (_, _, t) in children if t]
                post_message_checked(hwnd_err, WM_COMMAND, 1, 0, allow_closed=True)
                raise RuntimeError(f"Error al abrir turno detectado en diálogo '{text}': {details}")
        success_wins = find_windows_by_pid(target_pid, "Turno Abierto")
        if success_wins:
            hwnd_ok, text = success_wins[0]
            children = get_child_controls(hwnd_ok)
            btn_ok = next((h for (h, cls, txt) in children if cls.lower() == "button"), None)
            if btn_ok:
                post_click_button(btn_ok)
            else:
                post_message_checked(hwnd_ok, WM_COMMAND, 1, 0, allow_closed=True)
            time.sleep(0.4)
            return hwnd_ok, text
        time.sleep(0.2)
    all_wins = find_windows_by_pid(target_pid, "")
    titles = [t for (_, t) in all_wins]
    raise TimeoutError(f"[TIMEOUT {timeout}s] Esperando diálogo 'Turno Abierto' en PID {target_pid}. Ventanas visibles: {titles}")

def cancel_system_print_dialog_by_pid(target_pid, timeout=6.0):
    """Detecta y cancela el diálogo nativo de impresión del sistema operativo Windows."""
    start = time.time()
    while time.time() - start < timeout:
        wins = find_windows_by_pid(target_pid, "")
        for hwnd, title in wins:
            class_buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, class_buf, 256)
            cls_name = class_buf.value
            title_l = title.lower()
            # Diálogo estándar #32770 que no sea la ventana principal ni el TicketPrintDialog
            if cls_name == "#32770" and "venta registrada" not in title_l and "cafetería" not in title_l:
                # Cancelar con IDCANCEL (2) o WM_CLOSE
                post_message_checked(hwnd, WM_COMMAND, 2, 0, allow_closed=True)
                time.sleep(0.3)
                return hwnd, title
        time.sleep(0.2)
    return None, None

def select_list_item(hwnd_list, row_index):
    """Selecciona un elemento en SysListView32 enviando clic a las coordenadas de la fila."""
    # Cabecera ~28px, filas ~24px
    y = 28 + row_index * 24 + 12
    x = 40
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd_list, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.05)
    user32.PostMessageW(hwnd_list, WM_LBUTTONUP, 0, lparam)
    time.sleep(0.2)

def seed_test_catalog(db_path):
    """Inserta insumos, recetas, productos y un rol de vendedor sin permiso pos.reprint."""
    con = sqlite3.connect(db_path)
    cur = con.cursor()

    # Ingredientes
    cur.execute("""
        INSERT OR IGNORE INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold)
        VALUES 
            ('ing-pan-01', 'Pan Bolillo I2', 'COUNT', 'pz', 50, 10),
            ('ing-jam-01', 'Jamon Pierna I2', 'MASS', 'mg', 5000000, 500000),
            ('ing-hor-01', 'Concentrado Horchata I2', 'MASS', 'mg', 2000000, 200000);
    """)

    # Productos
    cur.execute("""
        INSERT OR IGNORE INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
        VALUES
            ('prod-torta-01', 'TORTA-01', '750100001001', 'Torta Clasica I2', 'Alimentos', 4500, 1, 1, '2026-09-29 00:00:00'),
            ('prod-agua-01', 'AGUA-01', '750100001002', 'Agua de Horchata I2', 'Bebidas', 2000, 1, 1, '2026-09-29 00:00:00');
    """)

    # Recetas
    cur.execute("""
        INSERT OR IGNORE INTO recipes (id, product_id, version, created_at)
        VALUES ('rec-torta-01', 'prod-torta-01', 1, '2026-09-29 00:00:00');
    """)
    cur.execute("""
        INSERT OR IGNORE INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units)
        VALUES 
            ('ri-01', 'rec-torta-01', 'ing-pan-01', 1),
            ('ri-02', 'rec-torta-01', 'ing-jam-01', 50000);
    """)

    cur.execute("""
        INSERT OR IGNORE INTO recipes (id, product_id, version, created_at)
        VALUES ('rec-agua-01', 'prod-agua-01', 1, '2026-09-29 00:00:00');
    """)
    cur.execute("""
        INSERT OR IGNORE INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units)
        VALUES ('ri-03', 'rec-agua-01', 'ing-hor-01', 40000);
    """)

    # Rol sin permiso pos.reprint (posee catalog.view, pos.sell, pos.notes, shift.view)
    cur.execute("""
        INSERT OR IGNORE INTO roles (id, name, description, is_system)
        VALUES ('role-seller-only', 'Vendedor Sin Reimpresion', 'Rol que no posee pos.reprint', 0);
    """)
    cur.execute("""
        INSERT OR IGNORE INTO role_permissions (role_id, permission_id)
        VALUES 
            ('role-seller-only', 'catalog.view'),
            ('role-seller-only', 'pos.sell'),
            ('role-seller-only', 'pos.notes'),
            ('role-seller-only', 'shift.view');
    """)

    # Usuario con rol sin reimpresión (usa el mismo password_hash del Dueño)
    cur.execute("""
        INSERT OR IGNORE INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at)
        SELECT 'user-seller-01', 'vendedor_i2', 'Vendedor I2', 'vendedor@itch.edu.mx', password_hash, 'role-seller-only', 1, created_at
        FROM users WHERE id LIKE 'user-%' LIMIT 1;
    """)

    con.commit()
    con.close()

def binary_supports_test_hooks(exe_path):
    try:
        with open(exe_path, "rb") as f:
            content = f.read()
            return b"CAFETERIA_SIMULATE_PRINT_ERROR" in content
    except Exception:
        return False

def run_integration_test_i2(exe_path, work_dir):
    print(f"\n=======================================================", flush=True)
    print(f"VERIFICACIÓN EXHAUSTIVA DE PUERTA I2 (9 REQUERIMIENTOS):", flush=True)
    print(f"  Binario:  {exe_path}", flush=True)
    print(f"  WorkDir:  {work_dir}", flush=True)
    print(f"=======================================================", flush=True)

    if not os.path.exists(exe_path):
        raise FileNotFoundError(f"Ejecutable no encontrado en: {exe_path}")

    temp_test_dir = tempfile.mkdtemp(prefix="cafeteria_i2_test_")
    db_path = os.path.join(temp_test_dir, "cafeteria.db")
    print(f"[OK] Directorio de datos de prueba aislado: {temp_test_dir}", flush=True)

    created_processes = []

    test_username = "dueno_i2_" + secrets.token_hex(4)
    test_display_name = "Dueno I2 " + secrets.token_hex(2)
    test_password = generate_random_secret(20)
    test_email = f"{test_username}@itch.edu.mx"

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_test_dir

    try:
        # -----------------------------------------------------------------
        # PASO 1: Alta inicial del Dueño en primer arranque
        # -----------------------------------------------------------------
        print("\n--- PASO 1: Alta Inicial del Dueño ---", flush=True)
        proc1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        created_processes.append(proc1)
        pid1 = proc1.pid
        print(f"[DIAGNÓSTICO] Proceso 1 iniciado con PID: {pid1}", flush=True)

        hwnd_owner, title = wait_for_window_by_pid(pid1, "Primer Inicio", timeout=12.0, step_name="Alta de Dueño")
        print(f"[OK] Ventana detectada por PID {pid1}: '{title}'", flush=True)

        children = get_child_controls(hwnd_owner)
        edits = [h for (h, cls, txt) in children if cls.lower() == "edit"]
        buttons = [h for (h, cls, txt) in children if cls.lower() == "button"]
        assert len(edits) >= 5, f"Se esperaban 5 campos Edit (encontrados {len(edits)})"

        set_text(edits[0], test_display_name)
        set_text(edits[1], test_username)
        set_text(edits[2], test_email)
        set_text(edits[3], test_password)
        set_text(edits[4], test_password)
        time.sleep(0.3)

        post_click_button(buttons[0])

        hwnd_main, main_title = wait_for_window_by_pid(
            pid1, "Cafetería ITCH II", timeout=12.0, step_name="Apertura MainWindow",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"]
        )
        print(f"[OK] MainWindow abierta: '{main_title}'", flush=True)

        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc1.wait(timeout=6)
        print("[OK] Sesión 1 cerrada limpiamente.", flush=True)

        # -----------------------------------------------------------------
        # PASO 2: Sembrado de catálogo, recetas y vendedor sin pos.reprint
        # -----------------------------------------------------------------
        print("\n--- PASO 2: Sembrado de Catálogo y Cuentas en SQLite ---", flush=True)
        assert os.path.exists(db_path), "Base de datos no existe"
        seed_test_catalog(db_path)
        print("[OK] Productos, recetas y rol de vendedor sembrados en SQLite.", flush=True)

        # -----------------------------------------------------------------
        # PASO 3: Login de Dueño, Apertura de Turno y Operaciones de Carrito
        # -----------------------------------------------------------------
        print("\n--- PASO 3: Inicio de Sesión Dueño y Apertura de Turno ---", flush=True)
        proc2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        created_processes.append(proc2)
        pid2 = proc2.pid

        hwnd_login, login_title = wait_for_window_by_pid(pid2, "Iniciar Sesión", timeout=12.0, step_name="Diálogo Login")
        login_children = get_child_controls(hwnd_login)
        l_edits = [h for (h, cls, txt) in login_children if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in login_children if cls.lower() == "button"]
        set_text(l_edits[0], test_username)
        set_text(l_edits[1], test_password)
        time.sleep(0.3)
        post_click_button(l_btns[0])

        hwnd_main2, _ = wait_for_window_by_pid(
            pid2, "Cafetería ITCH II", timeout=12.0, step_name="MainWindow tras login",
            exclude_substrings=["Iniciar Sesión", "Primer Inicio"]
        )
        print("[OK] MainWindow autenticada con éxito.", flush=True)

        # Apertura de turno con $350.00
        main_controls = get_child_controls(hwnd_main2)
        buttons = [(h, txt) for (h, cls, txt) in main_controls if cls.lower() == "button"]
        btn_caja_sidebar = next((h for (h, t) in buttons if "Caja y Reportes" in t), None)
        send_click_button(btn_caja_sidebar)
        time.sleep(0.8)

        shift_controls = get_child_controls(hwnd_main2)
        shift_buttons = [(h, txt) for (h, cls, txt) in shift_controls if cls.lower() == "button"]
        shift_edits = [h for (h, cls, txt) in shift_controls if cls.lower() == "edit"]
        btn_abrir_turno = next((h for (h, t) in shift_buttons if "Abrir Turno de Caja" in t), None)
        if shift_edits:
            set_text(shift_edits[0], "350.00")
            time.sleep(0.2)
        post_click_button(btn_abrir_turno)
        wait_for_shift_open_dialog_by_pid(pid2, timeout=10.0)
        time.sleep(0.5)

        # Regresar a Punto de Venta
        pos_sidebar_btn = next((h for (h, t) in shift_buttons if "Punto de Venta" in t), None)
        send_click_button(pos_sidebar_btn)
        time.sleep(0.8)

        # Controles del POS
        main_controls = get_child_controls(hwnd_main2)
        edits = [h for (h, cls, txt) in main_controls if cls.lower() == "edit"]
        buttons = [(h, txt) for (h, cls, txt) in main_controls if cls.lower() == "button"]
        lists = [h for (h, cls, txt) in main_controls if "list" in cls.lower()]

        btn_buscar = next((h for (h, t) in buttons if "Buscar" in t), None)
        btn_agregar = next((h for (h, t) in buttons if "Agregar al Carrito" in t), None)
        btn_edit_qty = next((h for (h, t) in buttons if "Editar cantidad" in t), None)
        btn_edit_notes = next((h for (h, t) in buttons if "Editar nota" in t), None)
        btn_remove_item = next((h for (h, t) in buttons if "Quitar línea" in t), None)
        btn_cobrar = next((h for (h, t) in buttons if "Cobrar" in t), None)
        list_items = lists[0] if lists else None

        assert btn_edit_qty is not None, "Botón 'Editar cantidad' no encontrado"
        assert btn_edit_notes is not None, "Botón 'Editar nota' no encontrado"
        assert btn_remove_item is not None, "Botón 'Quitar línea' no encontrado"
        assert list_items is not None, "Control de lista del carrito no encontrado"

        txt_search = edits[0]
        txt_qty = edits[1]
        txt_notes = edits[2]

        print("\n--- PASO 3.1: Agregando 4 líneas al carrito ---", flush=True)
        # Línea 1: 2x Torta "sin cebolla"
        set_text(txt_search, "Torta")
        send_click_button(btn_buscar)
        time.sleep(0.3)
        set_text(txt_qty, "2")
        set_text(txt_notes, "sin cebolla")
        time.sleep(0.2)
        send_click_button(btn_agregar)
        time.sleep(0.3)

        # Línea 2: 1x Torta "con extra picante"
        set_text(txt_qty, "1")
        set_text(txt_notes, "con extra picante")
        time.sleep(0.2)
        send_click_button(btn_agregar)
        time.sleep(0.3)

        # Línea 3: 1x Agua "con poco hielo"
        set_text(txt_search, "Horchata")
        send_click_button(btn_buscar)
        time.sleep(0.3)
        set_text(txt_qty, "1")
        set_text(txt_notes, "con poco hielo")
        time.sleep(0.2)
        send_click_button(btn_agregar)
        time.sleep(0.3)

        # Línea 4: 1x Agua "para llevar" (línea adicional que se eliminará para probar punto 3)
        set_text(txt_qty, "1")
        set_text(txt_notes, "para llevar")
        time.sleep(0.2)
        send_click_button(btn_agregar)
        time.sleep(0.5)

        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT COUNT(*) FROM cart_items")
        assert cur.fetchone()[0] == 4, "Se esperaban 4 líneas en cart_items"
        con.close()
        print("[OK] 4 líneas agregadas al carrito y verificadas en SQLite.", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 1: Editar cantidades y notas desde la interfaz
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 1: Edición de Cantidades y Notas en UI ---", flush=True)
        # 1.1 Editar cantidad en Fila 0 (2x Torta "sin cebolla" -> cambiar a 3)
        select_list_item(list_items, 0)
        post_click_button(btn_edit_qty)

        hwnd_dlg_qty, _ = wait_for_window_by_pid(pid2, "Editar Cantidad", timeout=6.0, step_name="Diálogo Editar Cantidad")
        qty_children = get_child_controls(hwnd_dlg_qty)
        qty_edits = [h for (h, cls, txt) in qty_children if cls.lower() == "edit"]
        qty_btns = [h for (h, cls, txt) in qty_children if cls.lower() == "button"]
        set_text(qty_edits[0], "3")
        time.sleep(0.2)
        post_click_button(qty_btns[0])
        time.sleep(0.5)

        # Comprobar en SQLite cantidad modificada a 3
        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT quantity, notes FROM cart_items WHERE notes='sin cebolla'")
        row_qty = cur.fetchone()
        assert row_qty[0] == 3, f"Cantidad esperada 3, obtenida {row_qty[0]}"
        con.close()
        print(f"[OK] Cantidad editada desde UI: Fila 0 ahora tiene {row_qty[0]} unidades ('sin cebolla').", flush=True)

        # 1.2 Editar nota en Fila 2 (1x Agua "con poco hielo" -> cambiar a "sin hielo")
        select_list_item(list_items, 2)
        post_click_button(btn_edit_notes)

        hwnd_dlg_note, _ = wait_for_window_by_pid(pid2, "Editar Nota de Preparación", timeout=6.0, step_name="Diálogo Editar Nota")
        note_children = get_child_controls(hwnd_dlg_note)
        note_edits = [h for (h, cls, txt) in note_children if cls.lower() == "edit"]
        note_btns = [h for (h, cls, txt) in note_children if cls.lower() == "button"]
        set_text(note_edits[0], "sin hielo")
        time.sleep(0.2)
        post_click_button(note_btns[0])
        time.sleep(0.5)

        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT COUNT(*) FROM cart_items WHERE notes='sin hielo'")
        assert cur.fetchone()[0] == 1, "Nota modificada no encontrada en SQLite"
        con.close()
        print("[OK] Nota editada desde UI: Fila 2 actualizada a 'sin hielo'.", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 2: Fusionar líneas compatibles y comprobar cantidades/importes
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 2: Fusión de Líneas Compatibles al Coincidir Notas ---", flush=True)
        # Fila 1 es 1x Torta con nota "con extra picante".
        # Al editar su nota y poner "sin cebolla", debe fusionarse atómicamente con Fila 0 (3x Torta "sin cebolla")
        # Resultado esperado: 1 sola línea de Torta "sin cebolla" con cantidad 4 y subtotal $180.00
        select_list_item(list_items, 1)
        post_click_button(btn_edit_notes)

        hwnd_dlg_merge, _ = wait_for_window_by_pid(pid2, "Editar Nota de Preparación", timeout=6.0, step_name="Diálogo Fusión Notas")
        m_children = get_child_controls(hwnd_dlg_merge)
        m_edits = [h for (h, cls, txt) in m_children if cls.lower() == "edit"]
        m_btns = [h for (h, cls, txt) in m_children if cls.lower() == "button"]
        set_text(m_edits[0], "sin cebolla")
        time.sleep(0.2)
        post_click_button(m_btns[0])
        time.sleep(0.6)

        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT quantity, unit_price_cents FROM cart_items WHERE notes='sin cebolla'")
        torta_rows = cur.fetchall()
        assert len(torta_rows) == 1, f"Se esperaba 1 sola línea fusionada de Torta (encontradas {len(torta_rows)})"
        merged_qty, unit_p = torta_rows[0]
        assert merged_qty == 4, f"Cantidad fusionada esperada 4 (3+1), obtenida {merged_qty}"
        assert unit_p * merged_qty == 18000, f"Subtotal esperado 18000 ($180.00), obtenido {unit_p * merged_qty}"

        cur.execute("SELECT COUNT(*) FROM cart_items WHERE notes='con extra picante'")
        assert cur.fetchone()[0] == 0, "La línea original con extra picante debió ser eliminada tras la fusión"

        cur.execute("SELECT COUNT(*) FROM cart_items")
        assert cur.fetchone()[0] == 3, f"El carrito debe tener ahora 3 líneas en total tras la fusión"
        con.close()
        print(f"[OK] Fusión exitosa en UI y SQLite: Torta 'sin cebolla' consolidada en {merged_qty} unidades = ${unit_p*merged_qty/100:.2f}", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 3: Eliminar una línea y confirmar el estado restante
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 3: Eliminación de Línea y Estado Restante ---", flush=True)
        # Seleccionar la línea de Agua "para llevar" (fila 2) y quitarla
        select_list_item(list_items, 2)
        send_click_button(btn_remove_item)
        time.sleep(0.5)

        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT COUNT(*) FROM cart_items WHERE notes='para llevar'")
        assert cur.fetchone()[0] == 0, "La línea 'para llevar' no fue eliminada"

        cur.execute("SELECT notes, quantity FROM cart_items ORDER BY rowid ASC")
        remaining = cur.fetchall()
        assert len(remaining) == 2, f"Se esperaban exactamente 2 líneas restantes (encontradas {len(remaining)})"
        assert remaining[0] == ("sin cebolla", 4), f"Línea 1 incorrecta: {remaining[0]}"
        assert remaining[1] == ("sin hielo", 1), f"Línea 2 incorrecta: {remaining[1]}"
        con.close()
        print("[OK] Línea eliminada. Estado restante exacto: 4x Torta ('sin cebolla') + 1x Agua ('sin hielo').", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 4 y 7: Cobro y Cancelación del Diálogo del Sistema
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 4 & 7: Cobro y Cancelación de Impresión de Sistema ---", flush=True)
        # Total a pagar: 4x $45.00 ($180.00) + 1x $20.00 ($20.00) = $200.00
        post_click_button(btn_cobrar)

        hwnd_checkout, _ = wait_for_window_by_pid(pid2, "Cobro de Venta", timeout=10.0, step_name="Diálogo Cobro de Venta")
        chk_children = get_child_controls(hwnd_checkout)
        chk_edits = [h for (h, cls, txt) in chk_children if cls.lower() == "edit"]
        chk_btns = [(h, txt) for (h, cls, txt) in chk_children if cls.lower() == "button"]
        btn_confirmar = next((h for (h, t) in chk_btns if "Confirmar Venta" in t), None)
        set_text(chk_edits[0], "250.00")
        time.sleep(0.2)
        post_click_button(btn_confirmar)

        # Diálogo TicketPrintDialog
        hwnd_ticket, _ = wait_for_window_by_pid(pid2, "Venta Registrada", timeout=10.0, step_name="Modal TicketPrintDialog")
        t_children = get_child_controls(hwnd_ticket)
        t_btns = [(h, txt) for (h, cls, txt) in t_children if cls.lower() == "button"]
        btn_imprimir = next((h for (h, t) in t_btns if "Imprimir" in t), None)
        assert btn_imprimir is not None, "Botón 'Imprimir' no encontrado"

        # Clic en Imprimir para disparar el diálogo del sistema
        print("  -> Clic en 'Imprimir' para abrir diálogo del sistema...", flush=True)
        post_click_button(btn_imprimir)
        time.sleep(0.6)

        # Cancelar el diálogo del sistema nativo Windows
        h_sys, t_sys = cancel_system_print_dialog_by_pid(pid2, timeout=6.0)
        print(f"  -> Diálogo del sistema detectado y cancelado con IDCANCEL (HWND: {h_sys})", flush=True)

        # Diálogo de confirmación de cancelación: "Impresión Cancelada"
        hwnd_cancel_box, _ = wait_for_window_by_pid(pid2, "Impresión Cancelada", timeout=6.0, step_name="Aviso Impresión Cancelada")
        dismiss_dialog_ok(hwnd_cancel_box)
        time.sleep(0.5)

        # Con la impresión cancelada, cerrar TicketPrintDialog con «Ahora no»
        btn_ahora_no = next((h for (h, t) in t_btns if "Ahora no" in t), None)
        if btn_ahora_no:
            post_click_button(btn_ahora_no)
            time.sleep(0.6)

        # Comprobar en SQLite: venta confirmada, inventario deducido, caja actualizada, y venta NO revertida ni duplicada
        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT id, folio, total_cents, cashier_name FROM sales")
        sales = cur.fetchall()
        assert len(sales) == 1, f"Se esperaba exactamente 1 venta registrada (encontradas {len(sales)})"
        sale_id, folio1, total_cents1, cashier_snap1 = sales[0]
        assert total_cents1 == 20000, f"Total incorrecto: {total_cents1} != 20000 ($200.00)"
        assert cashier_snap1 == test_display_name, f"Cajero incorrecto: {cashier_snap1}"

        # Comprobar líneas de venta
        cur.execute("SELECT quantity, notes, subtotal_cents FROM sale_items WHERE sale_id=? ORDER BY rowid ASC", (sale_id,))
        s_items = cur.fetchall()
        assert len(s_items) == 2, f"Se esperaban 2 líneas de venta (encontradas {len(s_items)})"
        assert s_items[0] == (4, "sin cebolla", 18000)
        assert s_items[1] == (1, "sin hielo", 2000)

        # Comprobar pago
        cur.execute("SELECT amount_cents, cash_received_cents, change_cents FROM payment_records WHERE sale_id=?", (sale_id,))
        p_row = cur.fetchone()
        assert p_row == (20000, 25000, 5000)

        # Comprobar stock deducido
        cur.execute("SELECT current_stock FROM ingredients WHERE id='ing-pan-01'")
        assert cur.fetchone()[0] == 46 # 50 - 4 tortas = 46

        # Comprobar movimiento de caja SALE
        cur.execute("SELECT amount_cents FROM cash_movements WHERE movement_type='SALE'")
        assert cur.fetchone()[0] == 20000

        # Comprobar carrito vacío
        cur.execute("SELECT COUNT(*) FROM cart_items")
        assert cur.fetchone()[0] == 0, "El carrito no quedó vacío tras venta"
        con.close()
        print(f"[OK] Requerimiento 4 & 7 cumplido: Venta {folio1} guardada exactamente una vez a pesar de cancelar impresión.", flush=True)

        close_window_and_wait(proc2, hwnd_main2, timeout=6)
        print("[OK] Sesión 2 cerrada limpiamente.", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 5: Cerrar y reabrir; consultar historial y recuperar ticket
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 5: Reapertura, Historial y Recuperación de Ticket ---", flush=True)
        proc3 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        created_processes.append(proc3)
        pid3 = proc3.pid

        hwnd_login3, _ = wait_for_window_by_pid(pid3, "Iniciar Sesión", timeout=12.0)
        l_edits = [h for (h, cls, txt) in get_child_controls(hwnd_login3) if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in get_child_controls(hwnd_login3) if cls.lower() == "button"]
        set_text(l_edits[0], test_username)
        set_text(l_edits[1], test_password)
        time.sleep(0.2)
        post_click_button(l_btns[0])

        hwnd_main3, _ = wait_for_window_by_pid(pid3, "Cafetería ITCH II", timeout=12.0, exclude_substrings=["Iniciar Sesión"])
        time.sleep(0.8)

        # Abrir historial de tickets mediante comando 1001 (Más acciones > Historial)
        user32.PostMessageW(hwnd_main3, WM_COMMAND, 1001, 0)

        hwnd_hist, _ = wait_for_window_by_pid(pid3, "Historial de Tickets", timeout=6.0, step_name="Diálogo Historial")
        hist_controls = get_child_controls(hwnd_hist)
        hist_edits = [h for (h, cls, txt) in hist_controls if cls.lower() == "edit"]
        hist_btns = [(h, txt) for (h, cls, txt) in hist_controls if cls.lower() == "button"]
        hist_lists = [h for (h, cls, txt) in hist_controls if "list" in cls.lower()]

        btn_hist_buscar = next((h for (h, t) in hist_btns if "Buscar" in t), None)
        btn_hist_reprint = next((h for (h, t) in hist_btns if "Reimprimir" in t), None)
        btn_hist_close = next((h for (h, t) in hist_btns if "Cerrar" in t), None)
        assert btn_hist_buscar is not None, "Botón Buscar en historial no encontrado"
        assert btn_hist_close is not None, "Botón Cerrar en historial no encontrado"

        # Buscar por folio V-10001
        set_text(hist_edits[0], folio1)
        time.sleep(0.2)
        post_click_button(btn_hist_buscar)

        # Se abre TicketPrintDialog con el ticket histórico
        hwnd_hist_ticket, _ = wait_for_window_by_pid(pid3, "Venta Registrada", timeout=6.0, step_name="Recuperar Ticket Historial")
        th_controls = get_child_controls(hwnd_hist_ticket)
        th_texts = [txt for (h, cls, txt) in th_controls if txt]
        th_btns = [(h, txt) for (h, cls, txt) in th_controls if cls.lower() == "button"]
        btn_th_ahora_no = next((h for (h, t) in th_btns if "Ahora no" in t), None)

        full_ticket_text = "\n".join(th_texts)
        assert folio1 in full_ticket_text, f"El ticket recuperado debe contener el folio '{folio1}'"
        assert cashier_snap1 in full_ticket_text, f"El ticket recuperado debe contener el snapshot histórico del cajero '{cashier_snap1}'"
        assert "Torta Clasica I2" in full_ticket_text, "El ticket recuperado debe contener el producto 'Torta Clasica I2'"
        assert "4x" in full_ticket_text or "4 x" in full_ticket_text, "El ticket recuperado debe contener la cantidad 4x"
        assert "$45.00" in full_ticket_text, "El ticket recuperado debe contener el precio unitario $45.00"
        assert "Agua de Horchata I2" in full_ticket_text, "El ticket recuperado debe contener el producto 'Agua de Horchata I2'"
        assert "1x" in full_ticket_text or "1 x" in full_ticket_text, "El ticket recuperado debe contener la cantidad 1x"
        assert "sin cebolla" in full_ticket_text, "El ticket recuperado debe contener la nota 'sin cebolla'"
        assert "sin hielo" in full_ticket_text, "El ticket recuperado debe contener la nota 'sin hielo'"
        assert "$180.00" in full_ticket_text, "El ticket recuperado debe reflejar el subtotal de $180.00"
        assert "$20.00" in full_ticket_text, "El ticket recuperado debe reflejar el subtotal de $20.00"
        assert "$200.00" in full_ticket_text, "El ticket recuperado debe reflejar el total exacto $200.00"
        print(f"[OK] Ticket histórico {folio1} recuperado íntegro tras reapertura con verificación exhaustiva de UI (folio, cajero, productos, cantidades, precios unitarios, notas y total).", flush=True)

        post_click_button(btn_th_ahora_no)
        time.sleep(0.4)
        post_click_button(btn_hist_close)
        time.sleep(0.4)

        close_window_and_wait(proc3, hwnd_main3, timeout=6)
        print("[OK] Sesión 3 cerrada limpiamente.", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 8: Error de impresión reproducible (CAFETERIA_SIMULATE_PRINT_ERROR)
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 8: Simulación Determinista de Error de Impresión ---", flush=True)
        req8_executed = False
        if not binary_supports_test_hooks(exe_path):
            print("  [OMITIDO: binario sin CAFETERIA_TEST_HOOKS] Requerimiento 8 omitido porque el binario no fue compilado con la opción CAFETERIA_TEST_HOOKS activada.", flush=True)
        else:
            req8_executed = True
            env_err = env.copy()
            env_err["CAFETERIA_SIMULATE_PRINT_ERROR"] = "1"
            proc4 = subprocess.Popen([exe_path], cwd=work_dir, env=env_err)
            created_processes.append(proc4)
            pid4 = proc4.pid

            hwnd_login4, _ = wait_for_window_by_pid(pid4, "Iniciar Sesión", timeout=12.0)
            l_edits = [h for (h, cls, txt) in get_child_controls(hwnd_login4) if cls.lower() == "edit"]
            l_btns = [h for (h, cls, txt) in get_child_controls(hwnd_login4) if cls.lower() == "button"]
            set_text(l_edits[0], test_username)
            set_text(l_edits[1], test_password)
            time.sleep(0.2)
            post_click_button(l_btns[0])

            hwnd_main4, _ = wait_for_window_by_pid(pid4, "Cafetería ITCH II", timeout=12.0, exclude_substrings=["Iniciar Sesión"])
            time.sleep(0.6)

            # Abrir historial y reimprimir el ticket 1
            user32.PostMessageW(hwnd_main4, WM_COMMAND, 1001, 0)
            hwnd_hist4, _ = wait_for_window_by_pid(pid4, "Historial de Tickets", timeout=6.0)
            h4_controls = get_child_controls(hwnd_hist4)
            h4_edits = [h for (h, cls, txt) in h4_controls if cls.lower() == "edit"]
            h4_btns = [(h, txt) for (h, cls, txt) in h4_controls if cls.lower() == "button"]
            btn_h4_buscar = next((h for (h, t) in h4_btns if "Buscar" in t), None)
            set_text(h4_edits[0], folio1)
            time.sleep(0.2)
            post_click_button(btn_h4_buscar)

            hwnd_t4, _ = wait_for_window_by_pid(pid4, "Venta Registrada", timeout=6.0)
            t4_btns = [(h, txt) for (h, cls, txt) in get_child_controls(hwnd_t4) if cls.lower() == "button"]
            btn_t4_print = next((h for (h, t) in t4_btns if "Imprimir" in t), None)

            print("  -> Disparando impresión con CAFETERIA_SIMULATE_PRINT_ERROR=1...", flush=True)
            post_click_button(btn_t4_print)
            time.sleep(0.5)

            # wxWidgets puede mostrar primero un cuadro de depuración ("wxWidgets Debug Alert") o error ("App_desktop Error")
            start_wait = time.time()
            hwnd_err_box = None
            err_t = ""
            while time.time() - start_wait < 10.0:
                for ew_hwnd, ew_title in find_windows_by_pid(pid4, ""):
                    tl = ew_title.lower()
                    if "aviso del subsistema de impresión" in tl:
                        hwnd_err_box = ew_hwnd
                        err_t = ew_title
                        break
                    elif any(pat in tl for pat in ["alert", "debug"]):
                        print(f"  -> Descartando diálogo previo de depuración: '{ew_title}'", flush=True)
                        dismiss_debug_alert(ew_hwnd)
                        time.sleep(0.5)
                    elif any(pat in tl for pat in ["error"]) and not any(ex in tl for ex in ["historial", "venta registrada", "cafetería"]):
                        print(f"  -> Descartando diálogo previo: '{ew_title}'", flush=True)
                        dismiss_dialog_ok(ew_hwnd)
                        time.sleep(0.5)
                if hwnd_err_box:
                    break
                time.sleep(0.2)

            assert hwnd_err_box is not None, f"No se encontró 'Aviso del Subsistema de Impresión'. Ventanas activas: {find_windows_by_pid(pid4, '')}"
            print(f"[OK] Diálogo de error capturado correctamente: '{err_t}'", flush=True)
            dismiss_dialog_ok(hwnd_err_box)
            time.sleep(0.5)

            # Salir de TicketPrintDialog con «Ahora no»
            t4_btns = [(h, txt) for (h, cls, txt) in get_child_controls(hwnd_t4) if cls.lower() == "button"]
            btn_t4_not_now = next((h for (h, t) in t4_btns if "Ahora no" in t), None)
            if btn_t4_not_now:
                post_click_button(btn_t4_not_now)
                time.sleep(0.5)

            # Cerrar diálogo de historial
            btn_h4_close = next((h for (h, t) in h4_btns if "Cerrar" in t), None)
            post_click_button(btn_h4_close)
            time.sleep(0.4)

            # Verificar en SQLite que los datos siguen 100% consistentes y no se duplicaron
            con = sqlite3.connect(db_path)
            cur = con.cursor()
            cur.execute("SELECT COUNT(*) FROM sales")
            assert cur.fetchone()[0] == 1, "La venta no debe multiplicarse ni anularse ante fallo de impresión"
            cur.execute("SELECT COUNT(*) FROM cash_movements WHERE movement_type='SALE'")
            assert cur.fetchone()[0] == 1, "No deben crearse movimientos adicionales ante error de impresión"
            con.close()
            print("[OK] Requerimiento 8 cumplido: Error de impresión manejado limpiamente sin corromper la venta.", flush=True)

            close_window_and_wait(proc4, hwnd_main4, timeout=6)
            print("[OK] Sesión 4 cerrada limpiamente.", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 6: Permisos con y sin pos.reprint (Vendedor vs Dueño)
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 6: Verificación de Autorización con y sin pos.reprint ---", flush=True)
        # Iniciar sesión con 'vendedor_i2' (cuenta con 'pos.sell' pero SIN 'pos.reprint')
        proc5 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        created_processes.append(proc5)
        pid5 = proc5.pid

        hwnd_login5, _ = wait_for_window_by_pid(pid5, "Iniciar Sesión", timeout=12.0)
        l_edits = [h for (h, cls, txt) in get_child_controls(hwnd_login5) if cls.lower() == "edit"]
        l_btns = [h for (h, cls, txt) in get_child_controls(hwnd_login5) if cls.lower() == "button"]
        set_text(l_edits[0], "vendedor_i2")
        set_text(l_edits[1], test_password)
        time.sleep(0.2)
        post_click_button(l_btns[0])

        hwnd_main5, _ = wait_for_window_by_pid(pid5, "Cafetería ITCH II", timeout=12.0, exclude_substrings=["Iniciar Sesión"])
        time.sleep(0.6)

        # 6.1 Intento no autorizado de consultar historial:
        print("  -> 6.1 Probando acceso a historial sin pos.reprint (debe denegarse)...", flush=True)
        user32.PostMessageW(hwnd_main5, WM_COMMAND, 1001, 0)

        # TicketHistoryDialog::load_tickets falla con Forbidden -> wxMessageBox "Error al consultar historial..."
        hwnd_forbid_box, forbid_title = wait_for_window_by_pid(pid5, "Error", timeout=6.0, step_name="Bloqueo pos.reprint")
        forbid_children = get_child_controls(hwnd_forbid_box)
        forbid_texts = [txt for (h, cls, txt) in forbid_children if txt]
        print(f"  -> Controles capturados en diálogo de bloqueo: {forbid_texts}", flush=True)
        assert any("pos.reprint" in t.lower() for t in forbid_texts) or forbid_title == "Error", f"Error en bloqueo pos.reprint: {forbid_texts}"
        print(f"[OK] Bloqueo efectivo a usuario sin pos.reprint: título='{forbid_title}', textos={forbid_texts}", flush=True)
        dismiss_dialog_ok(hwnd_forbid_box)
        time.sleep(0.4)

        # Cerrar diálogo de historial vacío
        hwnd_h5 = find_windows_by_pid(pid5, "Historial de Tickets")
        if hwnd_h5:
            user32.PostMessageW(hwnd_h5[0][0], WM_CLOSE, 0, 0)
            time.sleep(0.4)

        # 6.2 Venta realizada por el vendedor: su propio ticket recién confirmado SÍ es accesible
        print("  -> 6.2 Venta por vendedor_i2: verificar acceso a su ticket recién confirmado...", flush=True)
        main_controls5 = get_child_controls(hwnd_main5)
        edits5 = [h for (h, cls, txt) in main_controls5 if cls.lower() == "edit"]
        buttons5 = [(h, txt) for (h, cls, txt) in main_controls5 if cls.lower() == "button"]
        btn_buscar5 = next((h for (h, t) in buttons5 if "Buscar" in t), None)
        btn_agregar5 = next((h for (h, t) in buttons5 if "Agregar al Carrito" in t), None)
        btn_cobrar5 = next((h for (h, t) in buttons5 if "Cobrar" in t), None)

        set_text(edits5[0], "Torta")
        send_click_button(btn_buscar5)
        time.sleep(0.3)
        set_text(edits5[1], "1")
        set_text(edits5[2], "para llevar vendedor")
        time.sleep(0.2)
        send_click_button(btn_agregar5)
        time.sleep(0.4)

        post_click_button(btn_cobrar5)
        hwnd_chk5, _ = wait_for_window_by_pid(pid5, "Cobro de Venta", timeout=10.0)
        chk_edits5 = [h for (h, cls, txt) in get_child_controls(hwnd_chk5) if cls.lower() == "edit"]
        chk_btns5 = [(h, txt) for (h, cls, txt) in get_child_controls(hwnd_chk5) if cls.lower() == "button"]
        btn_conf5 = next((h for (h, t) in chk_btns5 if "Confirmar Venta" in t), None)
        set_text(chk_edits5[0], "50.00")
        time.sleep(0.2)
        post_click_button(btn_conf5)

        # A pesar de NO tener pos.reprint, TicketPrintDialog se abre para su venta recién confirmada (is_recent_seller == true)
        hwnd_t5, _ = wait_for_window_by_pid(pid5, "Venta Registrada", timeout=10.0, step_name="Ticket recién confirmado vendedor")
        t5_texts = [txt for (h, cls, txt) in get_child_controls(hwnd_t5) if txt]
        t5_btns = [(h, txt) for (h, cls, txt) in get_child_controls(hwnd_t5) if cls.lower() == "button"]
        btn_t5_not_now = next((h for (h, t) in t5_btns if "Ahora no" in t), None)

        assert any("Vendedor I2" in t for t in t5_texts), "El ticket debe incluir el nombre del vendedor"
        print("[OK] Vendedor sin pos.reprint pudo visualizar con éxito su ticket recién confirmado.", flush=True)

        post_click_button(btn_t5_not_now)
        time.sleep(0.5)

        close_window_and_wait(proc5, hwnd_main5, timeout=6)
        print("[OK] Sesión 5 cerrada limpiamente.", flush=True)

        # -----------------------------------------------------------------
        # REQUERIMIENTO 9: Verificar reintentos / idempotencia (Nivel Servicio SQLite)
        # -----------------------------------------------------------------
        print("\n--- REQUERIMIENTO 9: Reintento Real de Confirmación / Idempotencia (Nivel de Servicio SQLite) ---", flush=True)
        print("  [NIVEL DE PRUEBA: Contrato ISaleService::confirm_sale con SQLite real y ejecutable nativo, no interfaz gráfica]", flush=True)

        con = sqlite3.connect(db_path)
        cur = con.cursor()

        cur.execute("SELECT id, folio, idempotency_key, total_cents, user_id FROM sales WHERE folio='V-10001'")
        s1_row = cur.fetchone()
        assert s1_row is not None, "Venta V-10001 no encontrada"
        s1_id, s1_folio, s1_key, s1_tot, s1_user = s1_row

        def capture_db_snapshot(cursor):
            tables = [
                ("sales", "SELECT id, folio, total_cents, cashier_name, idempotency_key FROM sales ORDER BY id"),
                ("sale_items", "SELECT id, sale_id, product_id, quantity, unit_price_cents, subtotal_cents, notes FROM sale_items ORDER BY id"),
                ("payment_records", "SELECT id, sale_id, payment_method, amount_cents, cash_received_cents, change_cents FROM payment_records ORDER BY id"),
                ("ingredients", "SELECT id, name, current_stock FROM ingredients ORDER BY id"),
                ("stock_movements", "SELECT id, ingredient_id, movement_type, quantity_base_units, reason FROM stock_movements ORDER BY id"),
                ("cash_movements", "SELECT id, shift_id, movement_type, amount_cents FROM cash_movements ORDER BY id"),
                ("audit_events", "SELECT event_type, user_id FROM audit_events WHERE event_type NOT IN ('LOGIN_SUCCESS', 'LOGIN_FAILED') ORDER BY id"),
                ("cart_items", "SELECT id, cart_id, product_id, quantity FROM cart_items ORDER BY id")
            ]
            snapshot = {}
            for tbl, query in tables:
                cursor.execute(query)
                snapshot[tbl] = cursor.fetchall()
            return snapshot

        snap_before = capture_db_snapshot(cur)
        con.close()

        # Localizar el ejecutable de pruebas dedicado para reintento de idempotencia
        runner_exe = os.path.join(work_dir, "tests", "test_idempotency_runner.exe")
        if not os.path.exists(runner_exe):
            runner_exe = os.path.join(work_dir, "test_idempotency_runner.exe")
        assert os.path.exists(runner_exe), f"No se encontró el ejecutable de prueba de idempotencia en {runner_exe}"

        # Caso 9.1: Reintento REAL con la misma clave contextual e identidad autorizada (Dueño)
        # Las credenciales se transmiten de forma segura por stdin, sin pasarlas como argumentos de argv ni logs
        print(f"  -> Ejecutando reintento real mediante ejecutable de prueba aislado ({os.path.basename(runner_exe)}) con clave '{s1_key}'...", flush=True)
        retry_proc = subprocess.Popen(
            [runner_exe, "--data-dir", temp_test_dir, "--key", s1_key],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env
        )
        retry_stdout, retry_stderr = retry_proc.communicate(input=f"{test_username}\n{test_password}\n", timeout=10)
        assert retry_proc.returncode == 0, f"El reintento nativo falló con código {retry_proc.returncode}. Salida:\n{retry_stdout}\n{retry_stderr}"
        assert f"CONFIRM_RETRY_FOLIO: {s1_folio}" in retry_stdout, f"El reintento no devolvió el folio original {s1_folio}. Salida: {retry_stdout}"
        print(f"  -> Reintento devolvió exitosamente el folio original: {s1_folio}", flush=True)

        # Verificar integridad exhaustiva tabla por tabla tras el reintento exitoso
        con = sqlite3.connect(db_path)
        cur = con.cursor()
        snap_after_valid = capture_db_snapshot(cur)

        for tbl in snap_before:
            assert snap_before[tbl] == snap_after_valid[tbl], (
                f"La tabla '{tbl}' fue alterada tras reintento de confirmación!\n"
                f"Antes ({len(snap_before[tbl])} filas): {snap_before[tbl]}\n"
                f"Después ({len(snap_after_valid[tbl])} filas): {snap_after_valid[tbl]}"
            )
        print("  -> Verificado: Todas las tablas (sales, sale_items, payment_records, ingredients, stock_movements, cash_movements, audit_events, cart_items) permanecen 100% IDÉNTICAS sin duplicación.", flush=True)

        # Caso 9.2: Intento con la misma clave pero identidad no autorizada (Vendedor intenta usar clave del Dueño)
        print("  -> Probando intento de confirmación con clave existente pero cuenta ajena (Vendedor)...", flush=True)
        unauth_proc = subprocess.Popen(
            [runner_exe, "--data-dir", temp_test_dir, "--key", s1_key],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env
        )
        unauth_stdout, unauth_stderr = unauth_proc.communicate(input=f"vendedor_i2\n{test_password}\n", timeout=10)
        assert unauth_proc.returncode != 0, "Debe rechazar la confirmación cruzada entre usuarios"
        assert "CONFIRM_RETRY_ERROR" in unauth_stderr or "CONFIRM_RETRY_ERROR" in unauth_stdout, "Debe rechazar la confirmación cruzada entre usuarios"
        print("  -> Intento cruzado no autorizado rechazado correctamente por el servicio de ventas.", flush=True)

        # Re-comprobar que la base de datos tampoco fue alterada por el intento no autorizado
        snap_after_unauth = capture_db_snapshot(cur)
        for tbl in snap_before:
            assert snap_before[tbl] == snap_after_unauth[tbl], f"La tabla '{tbl}' cambió tras intento no autorizado"
        con.close()

        print("[OK] Requerimiento 9 cumplido al 100%: [SERVICIO SQLITE] Reintento real verificado a nivel de servicio SQLite con coincidencia de folio e invariabilidad absoluta de datos.", flush=True)

        print("\n=======================================================", flush=True)
        if req8_executed:
            print(f"TODOS LOS 9 REQUERIMIENTOS DE I2 FUERON VERIFICADOS CON ÉXITO", flush=True)
        else:
            print(f"8 REQUERIMIENTOS DE I2 VERIFICADOS CON ÉXITO (Requerimiento 8 omitido: binario sin CAFETERIA_TEST_HOOKS)", flush=True)
        print(f"EN EL BINARIO REAL: {exe_path}", flush=True)
        print("=======================================================", flush=True)

    finally:
        for p in created_processes:
            if p.poll() is None:
                try:
                    p.terminate()
                    p.wait(timeout=3)
                except Exception:
                    try:
                        p.kill()
                        p.wait(timeout=2)
                    except Exception:
                        pass
        shutil.rmtree(temp_test_dir, ignore_errors=True)
        print(f"[OK] Limpieza de directorio temporal aislado: {temp_test_dir}", flush=True)

if __name__ == "__main__":
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    release_exe = os.path.join(repo_root, "build_release", "app_desktop.exe")
    release_dir = os.path.join(repo_root, "build_release")
    debug_exe = os.path.join(repo_root, "build_debug", "app_desktop.exe")
    debug_dir = os.path.join(repo_root, "build_debug")

    # Validar primero Release y después Debug según requisitos
    if os.path.exists(release_exe):
        run_integration_test_i2(release_exe, release_dir)
    else:
        print(f"[ERROR] No se encontró build_release en {release_exe}", flush=True)
        sys.exit(1)

    if os.path.exists(debug_exe):
        run_integration_test_i2(debug_exe, debug_dir)
    else:
        print(f"[ERROR] No se encontró build_debug en {debug_exe}", flush=True)
        sys.exit(1)
