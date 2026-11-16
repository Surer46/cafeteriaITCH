#!/usr/bin/env python3
"""
test_t7_updates.py - Prueba E2E GUI para Bloque T7 (Actualizaciones del programa).

Usa build_hooks_release (Release con CAFETERIA_TEST_HOOKS=ON) y ganchos locales
(CAFETERIA_UPDATE_TEST_SOURCE, CAFETERIA_UPDATE_TEST_DELAY_MS,
CAFETERIA_UPDATE_TEST_LAUNCH), sin tocar GitHub ni la red real.
"""

import ctypes
from ctypes import wintypes
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from gui_dpi import (
    REPO_ROOT,
    RELEASE_EXE,
    user32,
    WM_SETTEXT,
    WM_CLOSE,
    BM_CLICK,
    ensure_no_app_running,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_scale,
    measure_text_extent,
    min_px,
)

HOOKS_EXE = os.path.join(REPO_ROOT, "build_hooks_release", "app_desktop.exe")
PUBLICAR_PS1 = os.path.join(REPO_ROOT, "tools", "publicar_version.ps1")
TEST_PRIV_KEY = os.path.join(REPO_ROOT, "tests", "update_test_key.priv")

WM_NULL = 0x0000
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
SWP_NOMOVE = 0x0002
SWP_NOZORDER = 0x0004
SWP_FRAMECHANGED = 0x0020

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def get_window_text(hwnd):
    length = user32.GetWindowTextLengthW(hwnd)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 2)
    user32.GetWindowTextW(hwnd, buf, length + 2)
    return buf.value


def get_class_name(hwnd):
    buf = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, buf, 128)
    return buf.value


def get_rect(hwnd):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def get_client_rect(hwnd):
    r = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def sync_ui(hwnd):
    if hwnd and user32.IsWindow(hwnd):
        user32.SendMessageW(hwnd, WM_NULL, 0, 0)


def _read_uia_texts(hwnd, timeout=6):
    try:
        ps_cmd = (
            "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
            "Add-Type -AssemblyName UIAutomationClient; "
            f"$el=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]{int(hwnd)}); "
            "$el.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
            "[System.Windows.Automation.Condition]::TrueCondition)"
            "|ForEach-Object{$_.Current.Name}|Where-Object{$_}"
        )
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", ps_cmd],
            text=True,
            encoding="utf-8",
            timeout=timeout,
        )
        return [ln.strip() for ln in out.splitlines() if ln.strip()]
    except Exception:
        return []


def enum_children(hwnd_parent):
    items = []

    def _cb(ch, _lp):
        items.append((ch, get_class_name(ch), get_window_text(ch)))
        return True

    user32.EnumChildWindows(wintypes.HWND(hwnd_parent), WNDENUMPROC(_cb), 0)
    return items


def find_windows_by_pid(pid, title_contains=None, exclude=()):
    result = []

    def _cb(hwnd, _lp):
        if not user32.IsWindowVisible(hwnd):
            return True
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            title = get_window_text(hwnd)
            if any(ex in title for ex in exclude):
                return True
            if title_contains is None or title_contains in title:
                result.append((hwnd, title))
        return True

    user32.EnumWindows(WNDENUMPROC(_cb), 0)
    return result


def wait_for_window(pid, title_contains, exclude=(), timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        wins = find_windows_by_pid(pid, title_contains, exclude=exclude)
        if wins:
            return wins[0]
        time.sleep(0.1)
    all_wins = find_windows_by_pid(pid)
    extra = [(wt, _read_uia_texts(wh, timeout=5)) for wh, wt in all_wins if "Debug Alert" in wt]
    raise AssertionError(
        f"Timeout ({timeout}s) esperando ventana '{title_contains}'. Visibles={all_wins} DebugAlerts={extra}"
    )


def wait_until(pred, desc="", timeout=12.0, interval=0.12):
    deadline = time.time() + timeout
    while time.time() < deadline:
        val = pred()
        if val:
            return val
        time.sleep(interval)
    raise AssertionError(f"Timeout ({timeout}s) esperando: {desc}")


def visible_texts(hwnd, sync=True):
    if sync:
        sync_ui(hwnd)
    return [t for (h, _c, t) in enum_children(hwnd) if user32.IsWindowVisible(h) and t]


def visible_buttons(hwnd, sync=False):
    if sync:
        sync_ui(hwnd)
    return [(h, t) for (h, c, t) in enum_children(hwnd) if user32.IsWindowVisible(h) and c.lower() == "button"]


def find_button(hwnd, text_contains=None, exact=None, sync=False):
    for h, t in visible_buttons(hwnd, sync=sync):
        if exact is not None and t == exact:
            return h
        if text_contains is not None and text_contains in t:
            return h
    return None


def click_hwnd(hwnd, settle=0.25):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)
    time.sleep(settle)


def set_edit_text(hwnd_edit, text):
    buf = ctypes.create_unicode_buffer(str(text))
    user32.SendMessageW(hwnd_edit, WM_SETTEXT, 0, ctypes.addressof(buf))


def get_edit_text_msg(hwnd):
    length = user32.SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 2)
    user32.SendMessageW(hwnd, WM_GETTEXT, length + 2, ctypes.addressof(buf))
    return buf.value


def open_section_and_subnav(hwnd_main, section_label, subnav_label):
    btn_sec = wait_until(
        lambda: user32.IsWindowEnabled(hwnd_main)
        and (h := find_button(hwnd_main, text_contains=section_label))
        and user32.IsWindowEnabled(h)
        and h,
        desc=f"Sidebar {section_label}",
    )
    click_hwnd(btn_sec, settle=0.3)
    sync_ui(hwnd_main)
    btn_sub = wait_until(
        lambda: (h := find_button(hwnd_main, exact=subnav_label)) and user32.IsWindowEnabled(h) and h,
        desc=f"Subnav {subnav_label}",
    )
    click_hwnd(btn_sub, settle=0.35)
    sync_ui(hwnd_main)


def accept_file_dialog(pid, title_sub, target_path):
    hwnd_fd, _ = wait_for_window(pid, title_sub, timeout=10.0)
    edit_name = wait_until(
        lambda: next(
            (
                h for (h, c, _t) in enum_children(hwnd_fd)
                if c.lower() == "edit" and user32.IsWindowVisible(h)
                and "combobox" in get_class_name(user32.GetParent(h)).lower()
            ),
            None,
        ) or next(
            (
                max(
                    [h for (h, c, _t) in enum_children(hwnd_fd) if c.lower() == "edit" and user32.IsWindowVisible(h)],
                    key=lambda h: get_rect(h)[1],
                    default=None,
                )
            ),
            None,
        ),
        desc=f"Edit en diálogo {title_sub}",
    )
    set_edit_text(edit_name, target_path)
    time.sleep(0.2)
    children = enum_children(hwnd_fd)
    btn_ok = user32.GetDlgItem(hwnd_fd, 1)
    if not btn_ok or not user32.IsWindowVisible(btn_ok):
        btn_ok = next(
            (h for (h, c, t) in children if user32.IsWindowVisible(h) and c.lower() == "button"
             and ("Abrir" in t or "Guardar" in t or "Aceptar" in t or "Seleccionar" in t)),
            None,
        )
    assert btn_ok, f"Botón OK no encontrado en '{title_sub}'"
    click_hwnd(btn_ok, settle=0.45)


def dismiss_plain_dialog(pid, title_sub):
    hwnd_d, _ = wait_for_window(pid, title_sub, timeout=10.0)
    sync_ui(hwnd_d)
    texts = visible_texts(hwnd_d)
    btn = find_button(hwnd_d, exact="Aceptar") or find_button(hwnd_d, exact="Entendido")
    if btn is None:
        btns = visible_buttons(hwnd_d)
        btn = btns[0][0] if btns else None
    assert btn is not None, f"Botón en diálogo '{title_sub}' no encontrado"
    click_hwnd(btn, settle=0.25)
    wait_until(lambda: not find_windows_by_pid(pid, title_sub), desc=f"Cerrar {title_sub}")
    return "\n".join(texts)


def create_signed_release(out_dir, version, installer_bytes, notes_lines):
    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    tmp_stage = tempfile.mkdtemp(prefix="cafeteria_t7_pub_")
    try:
        inst_name = f"CafeteriaITCH-{version}-instalador-win64.exe"
        inst_path = os.path.join(tmp_stage, inst_name)
        with open(inst_path, "wb") as f:
            f.write(installer_bytes)
        notes_path = os.path.join(tmp_stage, "notas.txt")
        with open(notes_path, "w", encoding="utf-8") as f:
            for ln in notes_lines:
                f.write(ln + "\n")
        subprocess.run(
            [
                "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", PUBLICAR_PS1,
                "-Version", version, "-Instalador", inst_path, "-Notas", notes_path,
                "-Clave", TEST_PRIV_KEY, "-Salida", out_dir,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    finally:
        shutil.rmtree(tmp_stage, ignore_errors=True)
    return (
        os.path.join(out_dir, f"CafeteriaITCH-{version}-instalador-win64.exe"),
        os.path.join(out_dir, "manifiesto.json"),
        os.path.join(out_dir, "manifiesto.json.sig"),
    )


def create_owner_first_run(exe_path, env, procs, user="dueno_t7", pwd="CafeT7!2026"):
    p = subprocess.Popen([exe_path], cwd=os.path.dirname(exe_path), env=env)
    procs.append(p)
    hwnd_o, _ = wait_for_window(p.pid, "Primer Inicio", timeout=15.0)
    edits = sorted(
        [h for (h, c, _t) in enum_children(hwnd_o) if c.lower() == "edit" and user32.IsWindowVisible(h)],
        key=lambda h: get_rect(h)[1],
    )
    assert len(edits) >= 5
    set_edit_text(edits[0], "Dueño T7")
    set_edit_text(edits[1], user)
    set_edit_text(edits[2], f"{user}@t7.local")
    set_edit_text(edits[3], pwd)
    set_edit_text(edits[4], pwd)
    click_hwnd(visible_buttons(hwnd_o)[0][0], settle=0.35)
    hwnd_m, _ = wait_for_window(p.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
    return p, hwnd_m


def login_as(exe_path, env, procs, user="dueno_t7", pwd="CafeT7!2026"):
    p = subprocess.Popen([exe_path], cwd=os.path.dirname(exe_path), env=env)
    procs.append(p)
    hwnd_l, _ = wait_for_window(p.pid, "Iniciar Sesión", timeout=15.0)
    edits = sorted(
        [h for (h, c, _t) in enum_children(hwnd_l) if c.lower() == "edit" and user32.IsWindowVisible(h)],
        key=lambda h: get_rect(h)[1],
    )
    set_edit_text(edits[0], user)
    set_edit_text(edits[1], pwd)
    click_hwnd(find_button(hwnd_l, text_contains="Iniciar Sesión"), settle=0.25)
    hwnd_m, _ = wait_for_window(p.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
    return p, hwnd_m


def set_client_logical_size(hwnd, w_log, h_log):
    scale = get_window_scale(hwnd)
    req_cw = int(round(w_log * scale))
    req_ch = int(round(h_log * scale))
    wl, wt, wr, wb = get_rect(hwnd)
    cl, ct, cr, cb = get_client_rect(hwnd)
    frame_w = (wr - wl) - (cr - cl)
    frame_h = (wb - wt) - (cb - ct)
    user32.SetWindowPos(hwnd, 0, 0, 0, req_cw + frame_w, req_ch + frame_h, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED)
    sync_ui(hwnd)


def assert_backoffice_subnav_fits(hwnd_main, label_ctx):
    expected_tabs = ["Usuarios y cuentas", "Roles y permisos", "Configuración", "Respaldos", "Actualizaciones"]
    btns = []
    for name in expected_tabs:
        h = wait_until(lambda n=name: find_button(hwnd_main, exact=n), desc=f"Tab {name} ({label_ctx})")
        btns.append((name, h))
    parents = {user32.GetParent(h) for _n, h in btns}
    assert len(parents) == 1, f"Tabs no comparten SubnavBar padre: {parents}"
    hwnd_sub = next(iter(parents))
    sl, st, sr, sb = get_rect(hwnd_sub)
    prev_r = None
    for name, h in btns:
        bl, bt, br, bb = get_rect(h)
        assert bl >= sl - 1 and br <= sr + 1, (
            f"[{label_ctx}] Tab '{name}' desborda SubnavBar: btn=({bl},{br}) vs subnav=({sl},{sr})"
        )
        assert bt >= st - 1 and bb <= sb + 1, (
            f"[{label_ctx}] Tab '{name}' desborda verticalmente SubnavBar: btn=({bt},{bb}) vs subnav=({st},{sb})"
        )
        if prev_r is not None:
            assert bl >= prev_r - 1, f"[{label_ctx}] Tab '{name}' solapa con el anterior: bl={bl} < prev_r={prev_r}"
        prev_r = br
        tw, _th = measure_text_extent(h, name)
        cw = get_client_rect(h)[2]
        assert cw >= tw + min_px(10, get_window_scale(hwnd_main)), (
            f"[{label_ctx}] Texto de '{name}' recortado: client_w={cw} < text_w={tw}+pad"
        )


def check_no_jargon(texts, ctx):
    joined = "\n".join(texts).lower()
    for bad in ("sha-256", "sha256", "manifiesto", "http", "json", "not_configured", "up_to_date", "will_install_on_close"):
        assert bad not in joined, f"Jerga técnica '{bad}' visible en {ctx}: {texts}"


def run_all_tests():
    assert os.path.exists(HOOKS_EXE), f"Falta compilación con ganchos: {HOOKS_EXE}"
    assert os.path.exists(PUBLICAR_PS1) and os.path.exists(TEST_PRIV_KEY), "Faltan herramientas de firma de prueba"

    ensure_no_app_running()
    temp_root = tempfile.mkdtemp(prefix="cafeteria_t7_")
    procs = []
    try:
        # ------------------------------------------------------------------
        # 1. Estado NOT_CONFIGURED (binario Release normal sin claves públicas)
        # ------------------------------------------------------------------
        if os.path.exists(RELEASE_EXE):
            print("\n[T7.1] Probando NOT_CONFIGURED en build_release...", flush=True)
            data_nc = os.path.join(temp_root, "data_nc")
            usb_nc_src = os.path.join(temp_root, "usb_nc_src")
            os.makedirs(data_nc, exist_ok=True)
            usb_exe, usb_json, usb_sig = create_signed_release(
                usb_nc_src, "0.2.0", b"MZ_NC_" * 2048, ["Nota NC"]
            )
            env_nc = os.environ.copy()
            env_nc["PATH"] = os.path.dirname(RELEASE_EXE) + ";" + env_nc.get("PATH", "")
            env_nc["CAFETERIA_DATA_DIR"] = data_nc
            p_nc, hwnd_nc = create_owner_first_run(RELEASE_EXE, env_nc, procs, user="dueno_nc")
            time.sleep(0.5)
            assert find_button(hwnd_nc, exact="Ver novedades", sync=True) is None, "En NOT_CONFIGURED no debe aparecer la franja"
            open_section_and_subnav(hwnd_nc, "BackOffice", "Actualizaciones")
            t_nc = visible_texts(hwnd_nc)
            assert any("no están activadas" in t for t in t_nc), f"Falta texto NOT_CONFIGURED: {t_nc}"
            assert find_button(hwnd_nc, exact="Buscar ahora") is None, "En NOT_CONFIGURED no debe existir 'Buscar ahora'"
            check_no_jargon(t_nc, "NOT_CONFIGURED")

            btn_usb_nc = wait_until(lambda: find_button(hwnd_nc, text_contains="Actualizar desde un archivo"), desc="Botón USB NOT_CONFIGURED")
            click_hwnd(btn_usb_nc, settle=0.25)
            accept_file_dialog(p_nc.pid, "Elegir instalador", usb_exe)
            accept_file_dialog(p_nc.pid, "Elegir archivo de datos", usb_json)
            accept_file_dialog(p_nc.pid, "Elegir archivo de verificación", usb_sig)
            msg_nc = dismiss_plain_dialog(p_nc.pid, "No se pudo usar la actualización")
            assert "no están activadas" in msg_nc.lower(), f"Motivo inesperado en USB NOT_CONFIGURED: {msg_nc}"

            user32.PostMessageW(hwnd_nc, WM_CLOSE, 0, 0)
            p_nc.wait(timeout=8)
            print("  [OK] NOT_CONFIGURED validado.", flush=True)

        # ------------------------------------------------------------------
        # 2. Subnav de 5 pestañas en 1200x720 y 960x600 (Estándar y Táctil) + sin versión nueva
        # ------------------------------------------------------------------
        print("\n[T7.2] Probando sin versión nueva + geometría de 5 pestañas en BackOffice...", flush=True)
        src_same = os.path.join(temp_root, "src_same")
        create_signed_release(src_same, "0.1.0", b"MZ_SAME_" * 2048, ["Misma versión"])
        data_main = os.path.join(temp_root, "data_main")
        launch_log = os.path.join(temp_root, "launched_installer.txt")
        os.makedirs(data_main, exist_ok=True)
        # Sembrar configuración previa sin touch (Estándar) para medir ambos perfiles
        env_m = os.environ.copy()
        env_m["PATH"] = os.path.dirname(HOOKS_EXE) + ";" + env_m.get("PATH", "")
        env_m["CAFETERIA_DATA_DIR"] = data_main
        env_m["CAFETERIA_UPDATE_TEST_SOURCE"] = src_same
        env_m["CAFETERIA_UPDATE_TEST_DELAY_MS"] = "250"
        env_m["CAFETERIA_UPDATE_TEST_LAUNCH"] = launch_log

        p1, hwnd1 = create_owner_first_run(HOOKS_EXE, env_m, procs)
        open_section_and_subnav(hwnd1, "BackOffice", "Actualizaciones")
        assert wait_until(
            lambda: any("Tienes la versión más reciente" in t for t in visible_texts(hwnd1)),
            desc="Texto 'Tienes la versión más reciente'",
            timeout=15.0,
        )
        assert find_button(hwnd1, exact="Ver novedades", sync=True) is None, "Sin versión nueva no debe aparecer la franja"

        # Medir 5 pestañas de BackOffice en Estándar (1200x720 y 960x600) y Táctil (1200x720 y 960x600)
        set_client_logical_size(hwnd1, 1200, 720)
        assert_backoffice_subnav_fits(hwnd1, "Estándar 1200x720")
        set_client_logical_size(hwnd1, 960, 600)
        assert_backoffice_subnav_fits(hwnd1, "Estándar 960x600")

        btn_prof = wait_until(lambda: find_button(hwnd1, exact="Estándar") or find_button(hwnd1, exact="Touch"), desc="Toggle perfil header")
        click_hwnd(btn_prof, settle=0.5)
        sync_ui(hwnd1)
        set_client_logical_size(hwnd1, 1200, 720)
        assert_backoffice_subnav_fits(hwnd1, "Táctil 1200x720")
        set_client_logical_size(hwnd1, 960, 600)
        assert_backoffice_subnav_fits(hwnd1, "Táctil 960x600")

        # Volver a Estándar 1200x720 para pruebas fluidas siguientes y cerrar
        btn_prof2 = wait_until(
            lambda: find_button(hwnd1, exact="Estándar"),
            desc="Volver a Estándar",
        )
        click_hwnd(btn_prof2, settle=0.45)
        sync_ui(hwnd1)
        user32.PostMessageW(hwnd1, WM_CLOSE, 0, 0)
        p1.wait(timeout=8)
        print("  [OK] Sin versión nueva y barra de 5 pestañas verificada.", flush=True)

        # ------------------------------------------------------------------
        # 3. Sin red (carpeta vacía): sin diálogos, POS responde y Actualizaciones dice sin conexión
        # ------------------------------------------------------------------
        print("\n[T7.3] Probando sin red (fuente vacía)...", flush=True)
        src_empty = os.path.join(temp_root, "src_empty")
        os.makedirs(src_empty, exist_ok=True)
        # Reiniciar last_check_ms para forzar comprobación en arranque sin red
        db_main = os.path.join(data_main, "cafeteria.db")
        con = sqlite3.connect(db_main)
        con.execute("UPDATE update_state SET last_check_ms=0, last_manual_check_ms=0, last_check_result='NONE', last_error='';")
        con.commit()
        con.close()

        env_offline = env_m.copy()
        env_offline["CAFETERIA_UPDATE_TEST_SOURCE"] = src_empty
        p_off, hwnd_off = login_as(HOOKS_EXE, env_offline, procs)
        # Verificar que POS responde de inmediato
        assert find_button(hwnd_off, text_contains="Buscar") is not None, "POS no respondió sin red"
        assert find_button(hwnd_off, exact="Ver novedades", sync=True) is None, "Sin red no debe mostrar franja"
        open_section_and_subnav(hwnd_off, "BackOffice", "Actualizaciones")
        assert wait_until(
            lambda: any("No se pudo comprobar: sin conexión" in t for t in visible_texts(hwnd_off)),
            desc="Estado 'No se pudo comprobar: sin conexión'",
            timeout=15.0,
        )
        user32.PostMessageW(hwnd_off, WM_CLOSE, 0, 0)
        p_off.wait(timeout=8)
        print("  [OK] Sin red validado.", flush=True)

        # ------------------------------------------------------------------
        # 4. Versión nueva 0.2.0: Dueño ve franja, "Ahora no", Cajero no ve nada,
        #    Novedades, cancelar descarga, venta mientras descarga, "No instalar todavía",
        #    "Cerrar sin instalar", "Cerrar e instalar" y resultado al reabrir
        # ------------------------------------------------------------------
        print("\n[T7.4] Probando flujo completo con versión 0.2.0...", flush=True)
        src_020 = os.path.join(temp_root, "src_020")
        exe_020, json_020, sig_020 = create_signed_release(
            src_020,
            "0.2.0",
            b"MZ_UPD_020_" * (64 * 1024 // 11 * 12),  # ~768 KB (12 bloques * 250ms = ~3s)
            ["Mejora de pruebas en caja.", "Respaldo automático al cerrar turno."],
        )

        # Resetear last_check_ms y sembrar producto + crear Cajero para probar permisos
        con = sqlite3.connect(db_main)
        con.execute("UPDATE update_state SET last_check_ms=0, last_manual_check_ms=0, last_check_result='NONE', last_error='';")
        con.execute(
            "INSERT OR IGNORE INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) "
            "VALUES ('prod-t7-1', 'T7-01', '750100009771', 'Cafe T7', 'Bebidas', 3500, 0, 1, '2026-10-01 08:00:00');"
        )
        con.commit()
        con.close()

        env_020 = env_m.copy()
        env_020["CAFETERIA_UPDATE_TEST_SOURCE"] = src_020

        # Abrir como Dueño -> franja aparece -> "Ahora no" la oculta -> crear Cajero
        p_d1, hwnd_d1 = login_as(HOOKS_EXE, env_020, procs)
        wait_until(
            lambda: any("Hay una versión nueva del programa (0.2.0)." in t for t in visible_texts(hwnd_d1)),
            desc="Franja versión nueva 0.2.0",
            timeout=15.0,
        )
        check_no_jargon(visible_texts(hwnd_d1), "Franja 0.2.0")
        click_hwnd(find_button(hwnd_d1, exact="Ahora no"), settle=0.25)
        sync_ui(hwnd_d1)
        assert find_button(hwnd_d1, exact="Ver novedades", sync=True) is None, "'Ahora no' debe ocultar la franja"

        # Crear usuario Cajero desde BackOffice > Usuarios y cuentas
        open_section_and_subnav(hwnd_d1, "BackOffice", "Usuarios y cuentas")
        click_hwnd(wait_until(lambda: find_button(hwnd_d1, text_contains="Nuevo Usuario"), desc="Nuevo Usuario T7"), settle=0.25)
        hwnd_nu, _ = wait_for_window(p_d1.pid, "Nuevo Usuario de Sistema", timeout=8.0)
        nu_edits = sorted(
            [h for (h, c, _t) in enum_children(hwnd_nu) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        set_edit_text(nu_edits[0], "Cajero T7")
        set_edit_text(nu_edits[1], "cajero_t7")
        set_edit_text(nu_edits[2], "cajero@t7.local")
        set_edit_text(nu_edits[3], "CajT7!2026")
        click_hwnd(find_button(hwnd_nu, text_contains="Guardar Usuario"), settle=0.35)
        wait_until(lambda: not find_windows_by_pid(p_d1.pid, "Nuevo Usuario de Sistema"), desc="Cerrar Nuevo Usuario")

        # Abrir turno de caja como Dueño para la prueba de venta durante descarga
        open_section_and_subnav(hwnd_d1, "Caja y Reportes", "Caja y turnos")
        sh_edits = wait_until(
            lambda: [h for (h, c, _t) in enum_children(hwnd_d1) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            desc="Edit fondo inicial Caja",
        )
        set_edit_text(sh_edits[0], "500.00")
        click_hwnd(find_button(hwnd_d1, text_contains="Abrir Turno de Caja"), settle=0.3)
        hwnd_ta, _ = wait_for_window(p_d1.pid, "Turno Abierto", timeout=8.0)
        click_hwnd(visible_buttons(hwnd_ta)[0][0], settle=0.25)

        user32.PostMessageW(hwnd_d1, WM_CLOSE, 0, 0)
        p_d1.wait(timeout=8)

        # Iniciar sesión como Cajero -> NO ve franja y en Actualizaciones dice Sin permiso
        p_caj, hwnd_caj = login_as(HOOKS_EXE, env_020, procs, user="cajero_t7", pwd="CajT7!2026")
        time.sleep(0.6)
        assert find_button(hwnd_caj, exact="Ver novedades", sync=True) is None, "Cajero no debe ver franja de actualización"
        assert not any("0.2.0" in t for t in visible_texts(hwnd_caj)), "Cajero no debe ver versión disponible en franja"
        open_section_and_subnav(hwnd_caj, "BackOffice", "Actualizaciones")
        assert wait_until(
            lambda: any("Sin permiso para actualizar el programa" in t for t in visible_texts(hwnd_caj)),
            desc="Sin permiso para actualizar el programa en Cajero",
        )
        assert find_button(hwnd_caj, exact="Buscar ahora") is None
        assert find_button(hwnd_caj, text_contains="Actualizar desde un archivo") is None
        user32.PostMessageW(hwnd_caj, WM_CLOSE, 0, 0)
        p_caj.wait(timeout=8)

        # Reabrir como Dueño -> franja vuelve tras haber elegido "Ahora no"
        p_d2, hwnd_d2 = login_as(HOOKS_EXE, env_020, procs)
        wait_until(
            lambda: find_button(hwnd_d2, exact="Ver novedades", sync=True) is not None,
            desc="Franja vuelve al reabrir",
            timeout=12.0,
        )
        click_hwnd(find_button(hwnd_d2, exact="Ver novedades"), settle=0.25)
        hwnd_nov, _ = wait_for_window(p_d2.pid, "Novedades de la versión 0.2.0", timeout=8.0)
        sync_ui(hwnd_nov)
        nov_texts = visible_texts(hwnd_nov)
        assert any("Mejora de pruebas en caja." in t for t in nov_texts), f"Faltan notas en Novedades: {nov_texts}"
        assert any("MB" in t for t in nov_texts), f"Falta tamaño en MB en Novedades: {nov_texts}"
        assert any("Tus datos se conservan." in t for t in nov_texts), f"Falta aviso de conservación de datos: {nov_texts}"
        check_no_jargon(nov_texts, "Ventana Novedades")

        # Descargar -> cancelar a mitad -> no queda nada aceptado
        click_hwnd(find_button(hwnd_nov, exact="Descargar e instalar al cerrar"), settle=0.2)
        wait_until(
            lambda: find_button(hwnd_nov, exact="Cancelar", sync=True) is not None
            and any("Descargando..." in t and "%" in t for t in visible_texts(hwnd_nov)),
            desc="Progreso con porcentaje y botón Cancelar",
            timeout=8.0,
        )
        click_hwnd(find_button(hwnd_nov, exact="Cancelar"), settle=0.5)
        wait_until(
            lambda: find_button(hwnd_nov, exact="Descargar e instalar al cerrar", sync=True) is not None,
            desc="Volver a botón descargar tras cancelar",
            timeout=10.0,
        )
        con = sqlite3.connect(db_main)
        assert con.execute("SELECT accepted FROM update_state WHERE id=1").fetchone()[0] == 0, "Cancelar dejó accepted=1"
        con.close()

        # Iniciar descarga otra vez y MIENTRAS descarga, confirmar una venta en POS
        click_hwnd(find_button(hwnd_nov, exact="Descargar e instalar al cerrar"), settle=0.2)
        wait_until(
            lambda: any("Descargando..." in t for t in visible_texts(hwnd_nov)),
            desc="Descarga iniciada antes de vender",
            timeout=6.0,
        )
        con_s0 = sqlite3.connect(db_main)
        sales_before = con_s0.execute("SELECT COUNT(*) FROM sales").fetchone()[0]
        con_s0.close()

        click_hwnd(wait_until(lambda: find_button(hwnd_d2, text_contains="Punto de Venta"), desc="POS"), settle=0.3)
        sync_ui(hwnd_d2)
        pos_edits = sorted(
            [h for (h, c, _t) in enum_children(hwnd_d2) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        set_edit_text(pos_edits[0], "Cafe T7")
        btn_b = find_button(hwnd_d2, exact="Buscar")
        if btn_b:
            click_hwnd(btn_b, settle=0.2)
        click_hwnd(wait_until(lambda: find_button(hwnd_d2, text_contains="Agregar al Carrito"), desc="Agregar POS"), settle=0.25)
        click_hwnd(wait_until(lambda: find_button(hwnd_d2, exact="Cobrar"), desc="Cobrar POS"), settle=0.25)
        hwnd_chk, _ = wait_for_window(p_d2.pid, "Cobro de Venta", timeout=8.0)
        chk_ed = [h for (h, c, _t) in enum_children(hwnd_chk) if c.lower() == "edit" and user32.IsWindowVisible(h)]
        if chk_ed:
            set_edit_text(chk_ed[0], "50.00")
        click_hwnd(wait_until(lambda: find_button(hwnd_chk, text_contains="Confirmar Venta"), desc="Confirmar Venta"), settle=0.35)
        hwnd_vr, _ = wait_for_window(p_d2.pid, "Venta Registrada", timeout=8.0)
        btn_an = find_button(hwnd_vr, text_contains="Ahora no") or visible_buttons(hwnd_vr)[0][0]
        click_hwnd(btn_an, settle=0.2)
        wait_until(lambda: not find_windows_by_pid(p_d2.pid, "Venta Registrada"), desc="Cerrar Ticket")

        con_s1 = sqlite3.connect(db_main)
        sales_after = con_s1.execute("SELECT COUNT(*) FROM sales").fetchone()[0]
        con_s1.close()
        assert sales_after == sales_before + 1, f"La venta durante la descarga no quedó en SQLite ({sales_before} -> {sales_after})"

        # Esperar en Novedades a que termine y diga "Lista." -> probar "No instalar todavía"
        wait_until(
            lambda: any("Lista. Se instalará cuando cierres el programa." in t for t in visible_texts(hwnd_nov)),
            desc="Mensaje 'Lista.' tras descarga",
            timeout=20.0,
        )
        click_hwnd(find_button(hwnd_nov, exact="No instalar todavía"), settle=0.3)
        wait_until(
            lambda: find_button(hwnd_nov, exact="Descargar e instalar al cerrar", sync=True) is not None,
            desc="'No instalar todavía' quita aceptación",
        )
        con = sqlite3.connect(db_main)
        assert con.execute("SELECT accepted FROM update_state WHERE id=1").fetchone()[0] == 0
        con.close()

        # Volver a aceptar (descarga ya verificada -> pasa a Lista de inmediato), cerrar Novedades
        click_hwnd(find_button(hwnd_nov, exact="Descargar e instalar al cerrar"), settle=0.3)
        wait_until(
            lambda: any("Lista. Se instalará cuando cierres el programa." in t for t in visible_texts(hwnd_nov)),
            desc="Reaceptar usa descarga existente",
            timeout=10.0,
        )
        click_hwnd(find_button(hwnd_nov, exact="Cerrar esta ventana"), settle=0.25)
        wait_until(lambda: not find_windows_by_pid(p_d2.pid, "Novedades de la versión"), desc="Cerrar Novedades")

        # Franja debe indicar que se instalará al cerrar
        wait_until(
            lambda: any("La versión 0.2.0 se instalará al cerrar el programa." in t for t in visible_texts(hwnd_d2)),
            desc="Franja lista para instalar al cerrar",
        )

        # Cerrar MainWindow -> diálogo de cierre -> "Cerrar sin instalar" NO escribe launch_log
        user32.PostMessageW(hwnd_d2, WM_CLOSE, 0, 0)
        hwnd_cl1, _ = wait_for_window(p_d2.pid, "Actualización lista para instalar", timeout=8.0)
        assert any("Se instalará la versión 0.2.0." in t for t in visible_texts(hwnd_cl1))
        click_hwnd(find_button(hwnd_cl1, exact="Cerrar sin instalar"), settle=0.25)
        p_d2.wait(timeout=10)
        assert not os.path.exists(launch_log), "'Cerrar sin instalar' no debió lanzar el instalador simulado"

        # Reabrir -> sigue aceptada -> cerrar y elegir "Cerrar e instalar" -> escribe launch_log
        p_d3, hwnd_d3 = login_as(HOOKS_EXE, env_020, procs)
        wait_until(
            lambda: any("La versión 0.2.0 se instalará al cerrar el programa." in t for t in visible_texts(hwnd_d3)),
            desc="Sigue aceptada al reabrir tras 'Cerrar sin instalar'",
            timeout=10.0,
        )
        user32.PostMessageW(hwnd_d3, WM_CLOSE, 0, 0)
        hwnd_cl2, _ = wait_for_window(p_d3.pid, "Actualización lista para instalar", timeout=8.0)
        click_hwnd(find_button(hwnd_cl2, exact="Cerrar e instalar"), settle=0.25)
        p_d3.wait(timeout=10)

        assert os.path.exists(launch_log), "'Cerrar e instalar' debió escribir el archivo de lanzamiento simulado"
        with open(launch_log, "r", encoding="utf-8", errors="replace") as f_l:
            launch_content = f_l.read()
        assert "0.2.0" in launch_content and "CafeteriaITCH-0.2.0-instalador-win64.exe" in launch_content, (
            f"Contenido inesperado en lanzamiento simulado: {launch_content}"
        )

        # Reabrir tras lanzamiento simulado (sigue en 0.1.0 -> UAC cancelado / no se completó -> mostrar UNA vez)
        p_d4, hwnd_d4 = login_as(HOOKS_EXE, env_020, procs)
        wait_until(
            lambda: any(
                "No se instaló porque no se dio permiso en Windows" in t or "no se completó" in t
                for t in visible_texts(hwnd_d4)
            ),
            desc="Franja resultado al reabrir tras no completarse instalación",
            timeout=12.0,
        )
        click_hwnd(find_button(hwnd_d4, exact="Entendido"), settle=0.25)
        user32.PostMessageW(hwnd_d4, WM_CLOSE, 0, 0)
        p_d4.wait(timeout=8)

        # Reabrir segunda vez: ese aviso ya NO se repite (solo se muestra una vez)
        p_d5, hwnd_d5 = login_as(HOOKS_EXE, env_020, procs)
        time.sleep(0.5)
        t_d5 = visible_texts(hwnd_d5)
        assert not any("No se instaló porque no se dio permiso" in t for t in t_d5), (
            f"Aviso de resultado de instalación se repitió en segundo arranque: {t_d5}"
        )
        user32.PostMessageW(hwnd_d5, WM_CLOSE, 0, 0)
        p_d5.wait(timeout=8)

        # Simular cierre tras actualización exitosa (versión objetivo 0.1.0 == instalada 0.1.0) -> muestra 'Se actualizó...' una vez
        con = sqlite3.connect(db_main)
        con.execute("UPDATE update_state SET install_target_version='0.1.0', last_install_result='NONE', last_install_shown=0;")
        con.commit()
        con.close()
        p_d6, hwnd_d6 = login_as(HOOKS_EXE, env_020, procs)
        wait_until(
            lambda: any("Se actualizó a la versión 0.1.0." in t for t in visible_texts(hwnd_d6)),
            desc="Franja 'Se actualizó a la versión 0.1.0.'",
            timeout=10.0,
        )
        click_hwnd(find_button(hwnd_d6, exact="Entendido"), settle=0.25)
        user32.PostMessageW(hwnd_d6, WM_CLOSE, 0, 0)
        p_d6.wait(timeout=8)
        print("  [OK] Flujo completo 0.2.0 (franja, permisos, descarga/cancelar, POS, cierre y resultado único) validado.", flush=True)

        # ------------------------------------------------------------------
        # 5. Descarga alterada (1 byte cambiado en el instalador del origen) -> rechazada
        # ------------------------------------------------------------------
        print("\n[T7.5] Probando descarga alterada (hash inválido)...", flush=True)
        src_bad_dl = os.path.join(temp_root, "src_bad_dl")
        bad_dl_exe, _, _ = create_signed_release(
            src_bad_dl, "0.2.1", b"MZ_ORIG_021_" * (64 * 1024 // 11 * 2), ["Parche 0.2.1"]
        )
        # Alterar 1 byte del instalador sin volver a firmar el manifiesto
        with open(bad_dl_exe, "r+b") as f_b:
            data_b = bytearray(f_b.read())
            data_b[100] ^= 0xFF
            f_b.seek(0)
            f_b.write(data_b)

        con = sqlite3.connect(db_main)
        con.execute("UPDATE update_state SET last_check_ms=0, last_manual_check_ms=0, available_version='', accepted=0, download_state='NONE', download_error='';")
        con.commit()
        con.close()

        env_bad_dl = env_m.copy()
        env_bad_dl["CAFETERIA_UPDATE_TEST_SOURCE"] = src_bad_dl
        env_bad_dl["CAFETERIA_UPDATE_TEST_DELAY_MS"] = "50"
        p_bdl, hwnd_bdl = login_as(HOOKS_EXE, env_bad_dl, procs)
        wait_until(lambda: find_button(hwnd_bdl, exact="Ver novedades", sync=True) is not None, desc="Franja 0.2.1")
        click_hwnd(find_button(hwnd_bdl, exact="Ver novedades"), settle=0.25)
        hwnd_n_bdl, _ = wait_for_window(p_bdl.pid, "Novedades de la versión 0.2.1", timeout=8.0)
        click_hwnd(find_button(hwnd_n_bdl, exact="Descargar e instalar al cerrar"), settle=0.25)
        wait_until(
            lambda: any("La descarga no es válida y se descartó. No se instaló nada." in t for t in visible_texts(hwnd_n_bdl)),
            desc="Rechazo de descarga alterada",
            timeout=15.0,
        )
        con = sqlite3.connect(db_main)
        assert con.execute("SELECT accepted FROM update_state WHERE id=1").fetchone()[0] == 0
        con.close()
        user32.PostMessageW(hwnd_n_bdl, WM_CLOSE, 0, 0)
        user32.PostMessageW(hwnd_bdl, WM_CLOSE, 0, 0)
        p_bdl.wait(timeout=8)
        print("  [OK] Descarga alterada rechazada.", flush=True)

        # ------------------------------------------------------------------
        # 6. Desde archivo (USB): igual/menor rechazada, alterada rechazada, válida 0.3.0 aceptada
        # ------------------------------------------------------------------
        print("\n[T7.6] Probando Actualizar desde un archivo (USB)...", flush=True)
        usb_old = os.path.join(temp_root, "usb_old")
        usb_corrupt = os.path.join(temp_root, "usb_corrupt")
        usb_030 = os.path.join(temp_root, "usb_030")
        old_exe, old_json, old_sig = create_signed_release(usb_old, "0.1.0", b"MZ_USB_OLD_" * 1024, ["Igual"])
        bad_exe, bad_json, bad_sig = create_signed_release(usb_corrupt, "0.3.0", b"MZ_USB_BAD_" * 1024, ["Alterada"])
        with open(bad_exe, "r+b") as f_ub:
            b_u = bytearray(f_ub.read())
            b_u[20] ^= 0x55
            f_ub.seek(0)
            f_ub.write(b_u)
        ok_exe, ok_json, ok_sig = create_signed_release(usb_030, "0.3.0", b"MZ_USB_030_" * 1024, ["USB 0.3.0 válida"])

        env_usb = env_m.copy()
        env_usb["CAFETERIA_UPDATE_TEST_SOURCE"] = src_empty  # sin internet funciona USB
        p_u, hwnd_u = login_as(HOOKS_EXE, env_usb, procs)
        open_section_and_subnav(hwnd_u, "BackOffice", "Actualizaciones")
        btn_usb = wait_until(lambda: find_button(hwnd_u, text_contains="Actualizar desde un archivo"), desc="Botón USB")

        # 6a. Igual versión (0.1.0) -> rechazo
        click_hwnd(btn_usb, settle=0.25)
        accept_file_dialog(p_u.pid, "Elegir instalador", old_exe)
        accept_file_dialog(p_u.pid, "Elegir archivo de datos", old_json)
        accept_file_dialog(p_u.pid, "Elegir archivo de verificación", old_sig)
        msg_old = dismiss_plain_dialog(p_u.pid, "No se pudo usar la actualización")
        assert "no es más nueva que la instalada" in msg_old, f"Rechazo versión igual inesperado: {msg_old}"

        # 6b. Archivo alterado -> rechazo
        click_hwnd(btn_usb, settle=0.25)
        accept_file_dialog(p_u.pid, "Elegir instalador", bad_exe)
        accept_file_dialog(p_u.pid, "Elegir archivo de datos", bad_json)
        accept_file_dialog(p_u.pid, "Elegir archivo de verificación", bad_sig)
        msg_bad_u = dismiss_plain_dialog(p_u.pid, "No se pudo usar la actualización")
        assert "dañado" in msg_bad_u.lower() or "no corresponde" in msg_bad_u.lower(), f"Rechazo USB alterado inesperado: {msg_bad_u}"

        # 6c. Válida 0.3.0 -> aceptada al cerrar
        click_hwnd(btn_usb, settle=0.25)
        accept_file_dialog(p_u.pid, "Elegir instalador", ok_exe)
        accept_file_dialog(p_u.pid, "Elegir archivo de datos", ok_json)
        accept_file_dialog(p_u.pid, "Elegir archivo de verificación", ok_sig)
        msg_ok_u = dismiss_plain_dialog(p_u.pid, "Actualización lista")
        assert "0.3.0" in msg_ok_u
        wait_until(
            lambda: any("La versión 0.3.0 se instalará al cerrar el programa." in t for t in visible_texts(hwnd_u)),
            desc="Franja 0.3.0 lista tras USB",
        )
        # Limpiar aceptación antes de cerrar para no escribir launch_log extra
        click_hwnd(find_button(hwnd_u, exact="Cancelar"), settle=0.25)
        user32.PostMessageW(hwnd_u, WM_CLOSE, 0, 0)
        p_u.wait(timeout=8)
        print("  [OK] Actualizar desde un archivo (USB) validado.", flush=True)

        # ------------------------------------------------------------------
        # 7. Copia portable: solo aviso, sin botones de descarga ni USB
        # ------------------------------------------------------------------
        print("\n[T7.7] Probando copia portable (portable.flag)...", flush=True)
        port_dir = os.path.join(temp_root, "portable_copy")
        os.makedirs(port_dir, exist_ok=True)
        hooks_dir = os.path.dirname(HOOKS_EXE)
        shutil.copy2(HOOKS_EXE, os.path.join(port_dir, "app_desktop.exe"))
        for fn in os.listdir(hooks_dir):
            if fn.lower().endswith(".dll"):
                shutil.copy2(os.path.join(hooks_dir, fn), os.path.join(port_dir, fn))
        shutil.copytree(os.path.join(hooks_dir, "assets"), os.path.join(port_dir, "assets"))
        shutil.copytree(os.path.join(hooks_dir, "migrations"), os.path.join(port_dir, "migrations"))
        with open(os.path.join(port_dir, "portable.flag"), "w", encoding="utf-8") as f_pf:
            f_pf.write("portable\n")

        port_data_dir = os.path.join(port_dir, "data")
        os.makedirs(port_data_dir, exist_ok=True)
        port_db = os.path.join(port_data_dir, "cafeteria.db")
        shutil.copy2(db_main, port_db)
        con_pd = sqlite3.connect(port_db)
        con_pd.execute(
            "UPDATE update_state SET last_check_ms=0, last_manual_check_ms=0, last_check_result='NONE', "
            "last_error='', available_version='', notes='', installer_name='', installer_size=0, "
            "installer_sha256='', download_state='NONE', downloaded_path='', accepted=0, "
            "install_target_version='', last_install_result='NONE', last_install_shown=1;"
        )
        con_pd.commit()
        con_pd.close()

        port_exe = os.path.join(port_dir, "app_desktop.exe")
        env_port = os.environ.copy()
        env_port.pop("CAFETERIA_DATA_DIR", None)
        env_port["PATH"] = port_dir + ";" + env_port.get("PATH", "")
        env_port["CAFETERIA_UPDATE_TEST_SOURCE"] = src_020

        p_p, hwnd_p = login_as(port_exe, env_port, procs)
        wait_until(lambda: find_button(hwnd_p, exact="Ver novedades", sync=True) is not None, desc="Franja aviso portable", timeout=15.0)
        click_hwnd(find_button(hwnd_p, exact="Ver novedades"), settle=0.25)
        hwnd_np, _ = wait_for_window(p_p.pid, "Novedades de la versión 0.2.0", timeout=8.0)
        assert find_button(hwnd_np, exact="Descargar e instalar al cerrar", sync=True) is None, "Portable en Novedades no debe ofrecer descarga"
        assert any("portable" in t.lower() for t in visible_texts(hwnd_np)), "Falta aviso portable en Novedades"
        click_hwnd(find_button(hwnd_np, exact="Cerrar esta ventana"), settle=0.2)

        open_section_and_subnav(hwnd_p, "BackOffice", "Actualizaciones")
        tp = visible_texts(hwnd_p)
        assert any("Esta copia es portable: no se actualiza sola" in t for t in tp), f"Falta aviso portable en Actualizaciones: {tp}"
        assert find_button(hwnd_p, exact="Buscar ahora") is None, "Portable no debe mostrar Buscar ahora"
        assert find_button(hwnd_p, exact="Descargar e instalar al cerrar") is None, "Portable no debe mostrar descargar"
        assert find_button(hwnd_p, text_contains="Actualizar desde un archivo") is None, "Portable no debe mostrar USB"

        user32.PostMessageW(hwnd_p, WM_CLOSE, 0, 0)
        p_p.wait(timeout=8)
        print("  [OK] Copia portable validada.", flush=True)

        print("\n=== PRUEBA T7 ACTUALIZACIONES EXITOSA ===")
    finally:
        for pr in procs:
            if pr.poll() is None:
                try:
                    pr.terminate()
                    pr.wait(timeout=3)
                except Exception:
                    try:
                        pr.kill()
                    except Exception:
                        pass
        shutil.rmtree(temp_root, ignore_errors=True)


def main():
    enable_per_monitor_dpi()
    sys_dpi, sys_scale = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {sys_dpi} (escala={sys_scale:.2f})")
    run_all_tests()


if __name__ == "__main__":
    main()
