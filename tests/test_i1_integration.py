#!/usr/bin/env python3
"""
test_i1_integration.py - Prueba automatizada reproducible de la Puerta I1.
Ejecuta app_desktop.exe real en modo nativo con base de datos SQLite real y aislada:
1. Tipos Win32 declarados explícitamente (argtypes, restype).
2. Búsqueda de ventanas EXCLUSIVAMENTE por el PID del ejecutable lanzado.
3. Uso de PostMessageW para interacción asíncrona evitando bloqueos en bucles modales.
4. Esperas con timeout y diagnóstico paso a paso.
5. Bloque finally que termina ÚNICAMENTE los procesos creados por la prueba antes de limpiar el directorio temporal.
6. Validación completa de alta inicial de Dueño, persistencia en SQLite, login y aislamiento de seguridad.
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

user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = wintypes.LPARAM

user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL

WM_SETTEXT = 0x000C
WM_COMMAND = 0x0111
WM_CLOSE = 0x0010
BM_CLICK = 0x00F5

def generate_random_secret(length=18):
    alphabet = string.ascii_letters + string.digits + "!@#$%^&*()_+-="
    return "".join(secrets.choice(alphabet) for _ in range(length))

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

def wait_for_window_by_pid(target_pid, title_substring, timeout=12.0, step_name="", exclude_substrings=None):
    start = time.time()
    while time.time() - start < timeout:
        wins = find_windows_by_pid(target_pid, title_substring)
        if exclude_substrings:
            wins = [w for w in wins if not any(ex.lower() in w[1].lower() for ex in exclude_substrings)]
        if wins:
            return wins[0]
        time.sleep(0.2)
    # Diagnóstico detallado en fallo
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

def post_click_button(hwnd):
    """Envía BM_CLICK de forma asíncrona mediante PostMessageW para evitar bloqueos en bucles modales."""
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)

def send_click_button(hwnd):
    """Envía BM_CLICK síncrono para botones estándar."""
    user32.SendMessageW(hwnd, BM_CLICK, 0, 0)

def run_integration_test(exe_path, work_dir):
    print(f"\n=======================================================")
    print(f"VERIFICACIÓN DE PUERTA I1 CON EJECUTABLE REAL:")
    print(f"  Binario:  {exe_path}")
    print(f"  WorkDir:  {work_dir}")
    print(f"=======================================================")

    if not os.path.exists(exe_path):
        raise FileNotFoundError(f"Ejecutable no encontrado en: {exe_path}")

    # 1. Crear un directorio temporal aislado para esta ejecución de prueba.
    temp_test_dir = tempfile.mkdtemp(prefix="cafeteria_i1_test_")
    db_path = os.path.join(temp_test_dir, "cafeteria.db")
    print(f"[OK] Directorio de datos de prueba aislado: {temp_test_dir}")

    # Generar credenciales dinámicas en memoria (sin registrar ni hardcodear)
    test_username = "dueno_test_" + secrets.token_hex(4)
    test_display_name = "Dueño de Prueba " + secrets.token_hex(2)
    test_password = generate_random_secret(20)
    test_wrong_password = generate_random_secret(22)
    while test_wrong_password == test_password:
        test_wrong_password = generate_random_secret(22)

    test_email = f"{test_username}@itch.edu.mx"

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    # Pasar directorio aislado a la aplicación
    env["CAFETERIA_DATA_DIR"] = temp_test_dir

    spawned_processes = []

    try:
        # -----------------------------------------------------------------
        # STEP 1: Primer arranque: Verificar diálogo de alta inicial y cancelación
        # -----------------------------------------------------------------
        print("\n--- PASO 1: Primer Inicio y Cancelación del Diálogo Inicial ---")
        proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        spawned_processes.append(proc)

        hwnd_owner, title = wait_for_window_by_pid(proc.pid, "Primer Inicio", timeout=12.0, step_name="Paso 1: Diálogo Alta Inicial")
        print(f"[OK] Ventana detectada en PID {proc.pid}: '{title}' (HWND: {hex(hwnd_owner)})")

        # Cancelar cerrando el diálogo
        user32.PostMessageW(hwnd_owner, WM_CLOSE, 0, 0)
        proc.wait(timeout=5)
        print(f"[OK] La aplicación cerró limpiamente al cancelar alta inicial (exit code: {proc.returncode})")

        # Comprobar que no se crearon usuarios en la base aislada
        if os.path.exists(db_path):
            con = sqlite3.connect(db_path)
            cur = con.cursor()
            cur.execute("SELECT COUNT(*) FROM users")
            cnt = cur.fetchone()[0]
            con.close()
            assert cnt == 0, f"No debe haber usuarios registrados tras cancelar (encontrados: {cnt})"
            print("[OK] Verificado en base aislada: 0 usuarios registrados tras cancelación.")

        # -----------------------------------------------------------------
        # STEP 2: Primer arranque real: Registro del Dueño y apertura de ventana principal
        # -----------------------------------------------------------------
        print("\n--- PASO 2: Primer Inicio y Alta Exitosa del Dueño ---")
        proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        spawned_processes.append(proc)

        hwnd_owner, title = wait_for_window_by_pid(proc.pid, "Primer Inicio", timeout=12.0, step_name="Paso 2: Diálogo Alta Inicial")
        print(f"[OK] Ventana detectada en PID {proc.pid}: '{title}' (HWND: {hex(hwnd_owner)})")

        children = get_child_controls(hwnd_owner)
        edits = [h for (h, cls, txt) in children if cls.lower() == "edit"]
        buttons = [h for (h, cls, txt) in children if cls.lower() == "button"]

        print(f"Controles detectados: {len(edits)} campos Edit, {len(buttons)} botones")
        assert len(edits) >= 5, f"Se esperaban 5 campos Edit (encontrados {len(edits)})"

        # Formulario: display_name, username, email, password, confirm_password
        set_text(edits[0], test_display_name)
        set_text(edits[1], test_username)
        set_text(edits[2], test_email)
        set_text(edits[3], test_password)
        set_text(edits[4], test_password)
        time.sleep(0.5)

        # Clic en botón Crear Cuenta
        submit_btn = buttons[0]
        post_click_button(submit_btn)

        # Ahora la Ventana Principal debe estar abierta
        hwnd_main, main_title = wait_for_window_by_pid(
            proc.pid, "Cafetería ITCH II", timeout=15.0,
            step_name="Paso 2: MainWindow tras Alta Dueño",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"]
        )
        print(f"[OK] Ventana Principal nativa abierta con éxito: '{main_title}' (HWND: {hex(hwnd_main)})")

        # Cerrar MainWindow
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=5)
        print(f"[OK] MainWindow cerrada limpiamente.")

        # -----------------------------------------------------------------
        # STEP 3: Verificar persistencia en base de datos SQLite aislada
        # -----------------------------------------------------------------
        print("\n--- PASO 3: Verificación de Persistencia en SQLite ---")
        assert os.path.exists(db_path), f"El archivo de base de datos {db_path} no existe"
        con = sqlite3.connect(db_path)
        cur = con.cursor()
        cur.execute("SELECT id, username, display_name, is_active, email FROM users WHERE username=?", (test_username,))
        row = cur.fetchone()
        assert row is not None, f"El usuario {test_username} no se encontró en la BD aislada"
        assert row[4] == test_email, f"Email incorrecto en BD: {row[4]} != {test_email}"
        print(f"[OK] Usuario persistido en BD: ID={row[0]}, User={row[1]}, Nombre='{row[2]}', Email='{row[4]}', Activo={row[3]}")

        cur.execute("SELECT r.name, r.id FROM roles r JOIN users u ON u.role_id = r.id WHERE u.username=?", (row[1],))
        role_row = cur.fetchone()
        assert role_row is not None and role_row[0] == "Dueño", f"Rol incorrecto: {role_row}"
        print(f"[OK] Rol persistido: '{role_row[0]}' (role_id={role_row[1]})")
        con.close()

        # -----------------------------------------------------------------
        # STEP 4: Arranque posterior: Diálogo de Login y Autenticación
        # -----------------------------------------------------------------
        print("\n--- PASO 4: Arranque Posterior y Autenticación en Login ---")
        proc = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        spawned_processes.append(proc)

        hwnd_login, login_title = wait_for_window_by_pid(proc.pid, "Iniciar Sesión", timeout=12.0, step_name="Paso 4: Diálogo Login")
        print(f"[OK] Diálogo de login activo en PID {proc.pid}: '{login_title}' (HWND: {hex(hwnd_login)})")

        owner_wins = find_windows_by_pid(proc.pid, "Primer Inicio")
        assert len(owner_wins) == 0, "No debe aparecer 'Primer Inicio' cuando ya existe un Dueño activo"

        children = get_child_controls(hwnd_login)
        edits = [h for (h, cls, txt) in children if cls.lower() == "edit"]
        buttons = [h for (h, cls, txt) in children if cls.lower() == "button"]
        assert len(edits) >= 2, f"Se esperaban 2 campos Edit en login (encontrados {len(edits)})"

        # 4a: Probar contraseña incorrecta
        print("  -> Probando contraseña incorrecta generada...")
        set_text(edits[0], test_username)
        set_text(edits[1], test_wrong_password)
        time.sleep(0.3)
        send_click_button(buttons[0])
        time.sleep(1)

        # Diálogo debe seguir abierto
        login_wins_still = find_windows_by_pid(proc.pid, "Iniciar Sesión")
        assert len(login_wins_still) > 0, "El diálogo de login se cerró con contraseña errónea!"
        print("  [OK] Contraseña incorrecta rechazada exitosamente.")

        # 4b: Probar contraseña correcta
        print("  -> Ingresando contraseña correcta generada...")
        set_text(edits[1], test_password)
        time.sleep(0.3)
        post_click_button(buttons[0])

        hwnd_main, main_title = wait_for_window_by_pid(
            proc.pid, "Cafetería ITCH II", timeout=15.0,
            step_name="Paso 4: MainWindow tras Login",
            exclude_substrings=["Iniciar Sesión"]
        )
        print(f"[OK] Sesión autenticada y MainWindow visible en PID {proc.pid}: '{main_title}' (HWND: {hex(hwnd_main)})")

        # Comprobar que 'Servidor de respaldos' no es visible para el Dueño
        main_children = get_child_controls(hwnd_main)
        texts = [txt for (h, cls, txt) in main_children if txt]
        has_dev_server = any("Servidor de respaldos" in t for t in texts)
        print(f"[OK] Verificación de seguridad: 'Servidor de respaldos' visible={has_dev_server} (esperado: False)")
        assert not has_dev_server, "El Dueño NO debe ver 'Servidor de respaldos'"

        # Cerrar MainWindow
        user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
        proc.wait(timeout=5)
        print(f"[OK] MainWindow cerrada con éxito tras prueba de sesión.")
        print(f"\n>>> PUERTA I1 COMPLETAMENTE VALIDADA CON EL EJECUTABLE REAL EN {exe_path} <<<")

    finally:
        # Terminar de forma segura exclusivamente los procesos iniciados por esta prueba
        for p in spawned_processes:
            if p.poll() is None:
                try:
                    p.terminate()
                    p.wait(timeout=2)
                except Exception:
                    try:
                        p.kill()
                    except Exception:
                        pass
        # Limpiar base de datos temporal aislada sin tocar las bases de desarrollo
        shutil.rmtree(temp_test_dir, ignore_errors=True)
        print(f"[OK] Limpieza de directorio temporal aislado: {temp_test_dir}")

if __name__ == "__main__":
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    release_exe = os.path.join(repo_root, "build_release", "app_desktop.exe")
    release_dir = os.path.join(repo_root, "build_release")
    debug_exe = os.path.join(repo_root, "build_debug", "app_desktop.exe")
    debug_dir = os.path.join(repo_root, "build_debug")

    if os.path.exists(release_exe):
        run_integration_test(release_exe, release_dir)
    else:
        print(f"[AVISO] No se encontró build_release en {release_exe}")

    if os.path.exists(debug_exe):
        run_integration_test(debug_exe, debug_dir)
    else:
        print(f"[AVISO] No se encontró build_debug en {debug_exe}")
