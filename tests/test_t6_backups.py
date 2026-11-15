#!/usr/bin/env python3
"""
test_t6_backups.py - Prueba E2E automatizada para BackOffice > Respaldos (Bloque T6).
Valida con ejecutables reales (Release y Debug) y base SQLite real:
1. Estado inicial ("Nunca", "Sin respaldos aun"), interruptor de respaldo automatico y persistencia.
2. Respaldo manual con dialogo de guardar archivo y mensaje con ruta; estado cambia a "Correcto".
3. Regla de conservacion (validacion de rangos, teclado numerico Tactil, guardar N y D).
4. Vista previa de limpieza y borrado real de respaldos antiguos en backup_files; boton Borrar deshabilitado sin vista previa vigente.
5. Rechazo de archivo invalido/danado y de respaldo de version futura sin modificar datos actuales.
6. Restauracion valida con doble confirmacion explicita, advertencia en lenguaje llano, pre_restore_backup_path y reapertura con datos restaurados.
7. Control de permisos: sin backup.manage muestra tarjeta de acceso denegado; con backup.manage sin backup.restore oculta restaurar.
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

from gui_dpi import (
    user32,
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    get_window_scale,
    min_px,
    px_to_logical,
)

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RELEASE_EXE = os.path.join(REPO_ROOT, "build_release", "app_desktop.exe")
DEBUG_EXE = os.path.join(REPO_ROOT, "build_debug", "app_desktop.exe")

WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_SETTEXT = 0x000C
WM_CLOSE = 0x0010
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
BM_CLICK = 0x00F5
BM_GETCHECK = 0x00F0
LVM_GETITEMCOUNT = 0x1004
BST_CHECKED = 1

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


def wait_for_window(pid, title_contains, exclude=(), timeout=12.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        wins = find_windows_by_pid(pid, title_contains, exclude=exclude)
        if wins:
            return wins[0]
        time.sleep(0.1)
    all_wins = find_windows_by_pid(pid)
    extra = []
    for wh, wt in all_wins:
        if "Debug Alert" in wt:
            try:
                ps_cmd = (
                    "Add-Type -AssemblyName UIAutomationClient; "
                    f"$el=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]{wh}); "
                    "$el.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
                    "[System.Windows.Automation.Condition]::TrueCondition)"
                    "|ForEach-Object{$_.Current.Name}|Where-Object{$_}"
                )
                uia = subprocess.check_output(["powershell", "-NoProfile", "-Command", ps_cmd], text=True, timeout=5)
                extra.append((wt, uia.splitlines()))
            except Exception as exc:
                extra.append((wt, repr(exc)))
    raise AssertionError(
        f"Timeout ({timeout}s) esperando ventana '{title_contains}'. Visibles: {[(h, t) for h, t in all_wins]} DebugAlerts={extra}"
    )


def enum_children(hwnd_parent):
    items = []

    def _cb(ch, _lp):
        items.append((ch, get_class_name(ch), get_window_text(ch)))
        return True

    user32.EnumChildWindows(wintypes.HWND(hwnd_parent), WNDENUMPROC(_cb), 0)
    return items


def visible_texts(hwnd):
    out = []
    for h, _c, t in enum_children(hwnd):
        if user32.IsWindowVisible(h) and t:
            out.append(t)
    return out


def visible_buttons(hwnd):
    return [
        (h, t)
        for (h, c, t) in enum_children(hwnd)
        if user32.IsWindowVisible(h) and c.lower() == "button"
    ]


def find_button(hwnd, text_contains=None, exact=None):
    for h, t in visible_buttons(hwnd):
        if exact is not None and t == exact:
            return h
        if text_contains is not None and text_contains in t:
            return h
    return None


def click_hwnd(hwnd, settle=0.25):
    user32.PostMessageW(hwnd, BM_CLICK, 0, 0)
    time.sleep(settle)


def click_client_center(hwnd, settle=0.25):
    l, t, r, b = get_rect(hwnd)
    cx = max(2, (r - l) // 2)
    cy = max(2, (b - t) // 2)
    lparam = (cy << 16) | (cx & 0xFFFF)
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lparam)
    time.sleep(0.04)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)
    time.sleep(settle)


def set_edit_text(hwnd_edit, text):
    buf = ctypes.create_unicode_buffer(str(text))
    user32.SendMessageW(hwnd_edit, WM_SETTEXT, 0, ctypes.addressof(buf))


def wait_until(pred, desc="", timeout=8.0, interval=0.1):
    deadline = time.time() + timeout
    while time.time() < deadline:
        val = pred()
        if val:
            return val
        time.sleep(interval)
    raise AssertionError(f"Timeout ({timeout}s) esperando: {desc}")


def create_owner_and_login(exe_path, env, procs, db_user="dueno_t6", db_pwd="CafeT6!2026"):
    p1 = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
    procs.append(p1)
    hwnd_owner, _ = wait_for_window(p1.pid, "Primer Inicio", timeout=15.0)
    edits = sorted(
        [h for (h, c, _t) in enum_children(hwnd_owner) if c.lower() == "edit" and user32.IsWindowVisible(h)],
        key=lambda h: get_rect(h)[1],
    )
    assert len(edits) >= 5, f"Se esperaban >= 5 edits en Primer Inicio, encontrados {len(edits)}"
    set_edit_text(edits[0], "Dueño T6")
    set_edit_text(edits[1], db_user)
    set_edit_text(edits[2], "dueno@t6.local")
    set_edit_text(edits[3], db_pwd)
    set_edit_text(edits[4], db_pwd)
    btns = visible_buttons(hwnd_owner)
    assert btns, "Botón crear cuenta en Primer Inicio no encontrado"
    click_hwnd(btns[0][0], settle=0.35)
    hwnd_main, _ = wait_for_window(p1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
    return p1, hwnd_main


def open_section_and_subnav(hwnd_main, section_label, subnav_label):
    btn_sec = wait_until(lambda: find_button(hwnd_main, text_contains=section_label), desc=f"Sidebar {section_label}")
    click_hwnd(btn_sec, settle=0.3)
    btn_sub = wait_until(lambda: find_button(hwnd_main, exact=subnav_label), desc=f"Subnav {subnav_label}")
    click_hwnd(btn_sub, settle=0.35)


def get_edit_text_msg(hwnd):
    length = user32.SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 2)
    user32.SendMessageW(hwnd, WM_GETTEXT, length + 2, ctypes.addressof(buf))
    return buf.value


def accept_file_dialog(pid, title_sub, target_path, expected_default_prefix=None):
    hwnd_fd, _ = wait_for_window(pid, title_sub, timeout=10.0)
    deadline = time.time() + 5.0
    edit_name = None
    while time.time() < deadline:
        children = enum_children(hwnd_fd)
        edits = [h for (h, c, _t) in children if c.lower() == "edit" and user32.IsWindowVisible(h)]
        if edits:
            if expected_default_prefix:
                match = next((h for h in edits if expected_default_prefix in get_edit_text_msg(h)), None)
                if match is not None:
                    edit_name = match
                    break
            else:
                cb_match = next(
                    (h for h in edits if "combobox" in get_class_name(user32.GetParent(h)).lower()),
                    None,
                )
                edit_name = cb_match if cb_match is not None else max(edits, key=lambda h: get_rect(h)[1])
                break
        time.sleep(0.15)
    assert edit_name is not None, (
        f"Campo nombre en '{title_sub}' no listo. Edits={[get_edit_text_msg(h) for h in edits]}"
    )
    if expected_default_prefix:
        cur_val = get_edit_text_msg(edit_name)
        assert expected_default_prefix in cur_val, (
            f"Nombre propuesto inesperado en Guardar respaldo: '{cur_val}'"
        )
    set_edit_text(edit_name, target_path)
    time.sleep(0.2)
    children = enum_children(hwnd_fd)
    btn_ok = user32.GetDlgItem(hwnd_fd, 1)
    if not btn_ok or not user32.IsWindowVisible(btn_ok):
        btn_ok = next(
            (h for (h, c, t) in children if user32.IsWindowVisible(h) and c.lower() == "button"
             and ("Guardar" in t or "Abrir" in t)),
            None,
        )
    assert btn_ok, f"Botón OK no encontrado en diálogo '{title_sub}'"
    click_hwnd(btn_ok, settle=0.5)


def dismiss_messagebox(pid, title_sub):
    hwnd_msg, _ = wait_for_window(pid, title_sub, timeout=10.0)
    time.sleep(0.15)
    texts = [t for (h, _c, t) in enum_children(hwnd_msg) if user32.IsWindowVisible(h) and t]
    joined = "\n".join(texts)
    btn_ok = find_button(hwnd_msg, text_contains="Aceptar")
    if btn_ok is None:
        btn_ok = user32.GetDlgItem(hwnd_msg, 2) or user32.GetDlgItem(hwnd_msg, 1)
    if not btn_ok:
        btns = visible_buttons(hwnd_msg)
        btn_ok = btns[0][0] if btns else None
    assert btn_ok is not None, f"Botón Aceptar no encontrado en '{title_sub}'"
    click_hwnd(btn_ok, settle=0.3)
    return joined


def run_suite(build_label, exe_path, sys_scale):
    if not os.path.exists(exe_path):
        raise FileNotFoundError(f"Ejecutable no encontrado: {exe_path}")

    print(f"\n=== T6 Respaldos ({build_label}, escala={sys_scale:.2f}) ===", flush=True)
    temp_dir = tempfile.mkdtemp(prefix=f"cafeteria_t6_{build_label.lower()}_")
    backup_out_dir = os.path.join(temp_dir, "manual_backups")
    os.makedirs(backup_out_dir, exist_ok=True)
    procs = []

    try:
        env = os.environ.copy()
        env["CAFETERIA_DATA_DIR"] = temp_dir
        cfg_profile = os.path.join(temp_dir, "station_profile.cfg")
        with open(cfg_profile, "w", encoding="utf-8") as f:
            f.write("touch\n")

        p, hwnd_main = create_owner_and_login(exe_path, env, procs)
        open_section_and_subnav(hwnd_main, "BackOffice", "Respaldos")

        # 1. Estado inicial (is_enabled inicia en 0 segun migracion 008)
        texts_init = visible_texts(hwnd_main)
        assert any("Nunca" in t for t in texts_init), f"Falta 'Nunca' en estado inicial: {texts_init}"
        assert any("Sin respaldos aún" in t for t in texts_init), f"Falta 'Sin respaldos aún': {texts_init}"
        assert any("Desactivado" in t for t in texts_init), f"Falta estado textual 'Desactivado': {texts_init}"
        assert not any("backup_state" in t or "sqlite" in t.lower() or "err_" in t.lower() for t in texts_init), (
            f"Jerga tecnica visible: {texts_init}"
        )

        # Verificar medidas Tactil >= 48 log
        btn_make = find_button(hwnd_main, text_contains="Hacer un respaldo ahora")
        btn_rest = find_button(hwnd_main, text_contains="Restaurar desde un respaldo")
        btn_save_pol = find_button(hwnd_main, exact="Guardar")
        btn_prev_cl = find_button(hwnd_main, text_contains="Revisar qué se borraría")
        btn_del_cl = find_button(hwnd_main, text_contains="Borrar respaldos antiguos")
        assert all([btn_make, btn_rest, btn_save_pol, btn_prev_cl, btn_del_cl]), "Faltan botones de Respaldos"
        assert user32.IsWindowEnabled(btn_del_cl) == 0, "Borrar respaldos antiguos debe iniciar deshabilitado"

        for lbl, bh in [("Hacer respaldo", btn_make), ("Restaurar", btn_rest), ("Guardar", btn_save_pol),
                        ("Revisar limpieza", btn_prev_cl), ("Borrar limpieza", btn_del_cl)]:
            l, t, r, b = get_rect(bh)
            assert (b - t) >= min_px(48, sys_scale), f"{lbl} menor a 48 log: {px_to_logical(b - t, sys_scale):.1f}"

        # Toggle automatico: inicia Desactivado (0) -> activar (1) -> desactivar (0) -> activar (1)
        toggle_hwnd = next(
            (h for (h, _c, t) in enum_children(hwnd_main)
             if user32.IsWindowVisible(h) and "Respaldo automático al cerrar turno" in t and _c.lower() != "static"),
            None,
        )
        assert toggle_hwnd is not None, "Interruptor ToggleSwitch no encontrado"
        assert user32.SendMessageW(toggle_hwnd, BM_GETCHECK, 0, 0) == 0, "Toggle debe iniciar Desactivado"
        click_hwnd(toggle_hwnd, settle=0.3)
        assert wait_until(lambda: any(t == "Activado" for t in visible_texts(hwnd_main)), desc="Toggle Activado")

        db_path = os.path.join(temp_dir, "cafeteria.db")
        con = sqlite3.connect(db_path)
        assert con.execute("SELECT is_enabled FROM backup_state WHERE id=1").fetchone()[0] == 1
        con.close()

        click_hwnd(toggle_hwnd, settle=0.3)
        assert wait_until(lambda: any(t == "Desactivado" for t in visible_texts(hwnd_main)), desc="Toggle Desactivado")
        con = sqlite3.connect(db_path)
        assert con.execute("SELECT is_enabled FROM backup_state WHERE id=1").fetchone()[0] == 0
        con.close()

        click_hwnd(toggle_hwnd, settle=0.3)
        assert wait_until(lambda: any(t == "Activado" for t in visible_texts(hwnd_main)), desc="Toggle Reactivado")

        # 2. Respaldo manual #1 -> bkp_before.db
        bkp_before = os.path.join(backup_out_dir, "bkp_before.db")
        click_hwnd(btn_make, settle=0.2)
        accept_file_dialog(p.pid, "Guardar respaldo", bkp_before, expected_default_prefix="cafeteria-respaldo-")
        msg_b1 = dismiss_messagebox(p.pid, "Respaldo guardado")
        assert bkp_before in msg_b1, f"Confirmación no muestra ruta guardada: {msg_b1}"
        assert os.path.exists(bkp_before) and os.path.getsize(bkp_before) > 0, "Respaldo manual #1 no existe"

        bkp_restore_copy = os.path.join(temp_dir, "copia_para_restaurar.db")
        shutil.copy2(bkp_before, bkp_restore_copy)

        assert wait_until(lambda: any("Correcto" in t for t in visible_texts(hwnd_main)), desc="Estado Correcto")
        assert not any(t == "Nunca" for t in visible_texts(hwnd_main)), "Fecha sigue diciendo Nunca"

        # 3. Crear usuario temporal para comprobar rollback real tras restauracion
        open_section_and_subnav(hwnd_main, "BackOffice", "Usuarios y cuentas")
        btn_new_u = wait_until(lambda: find_button(hwnd_main, text_contains="Nuevo Usuario"), desc="Nuevo Usuario")
        click_hwnd(btn_new_u, settle=0.2)
        hwnd_nu, _ = wait_for_window(p.pid, "Nuevo Usuario de Sistema", timeout=8.0)
        nu_edits = sorted(
            [h for (h, c, _t) in enum_children(hwnd_nu) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        assert len(nu_edits) >= 4
        set_edit_text(nu_edits[0], "Cajero Temporal")
        set_edit_text(nu_edits[1], "cajero_temp")
        set_edit_text(nu_edits[2], "temp@t6.local")
        set_edit_text(nu_edits[3], "Temp!2026")
        btn_save_u = find_button(hwnd_nu, text_contains="Guardar Usuario")
        click_hwnd(btn_save_u, settle=0.4)
        wait_until(lambda: not find_windows_by_pid(p.pid, "Nuevo Usuario de Sistema"), desc="Cerrar Nuevo Usuario")

        con = sqlite3.connect(db_path)
        cnt_tmp = con.execute("SELECT COUNT(*) FROM users WHERE username='cajero_temp'").fetchone()[0]
        con.close()
        assert cnt_tmp == 1, "Usuario cajero_temp no se guardo antes de backup #2"

        # 4. Volver a Respaldos y hacer respaldo #2 -> bkp_after.db
        open_section_and_subnav(hwnd_main, "BackOffice", "Respaldos")
        bkp_after = os.path.join(backup_out_dir, "bkp_after.db")
        btn_make = find_button(hwnd_main, text_contains="Hacer un respaldo ahora")
        click_hwnd(btn_make, settle=0.2)
        accept_file_dialog(p.pid, "Guardar respaldo", bkp_after)
        dismiss_messagebox(p.pid, "Respaldo guardado")
        assert os.path.exists(bkp_after)

        # 5. Politica de retencion: validar error (0), teclado numerico Tactil (N=1, D=0), preview y cleanup real
        res_edits = sorted(
            [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[0],
        )
        assert len(res_edits) >= 2, "Faltan campos N y D"
        set_edit_text(res_edits[0], "0")
        btn_save_pol = find_button(hwnd_main, exact="Guardar")
        click_hwnd(btn_save_pol, settle=0.2)
        err_pol = dismiss_messagebox(p.pid, "No se pudo guardar la regla")
        assert "1" in err_pol and "1000" in err_pol, f"Mensaje inesperado para rango N=0: {err_pol}"

        # Probar teclado numerico Tactil en keep_last y keep_days
        set_edit_text(res_edits[0], "")
        click_client_center(res_edits[0], settle=0.25)
        btn_k1 = wait_until(lambda: find_button(hwnd_main, exact="1"), desc="Tecla 1 de NumericKeypad")
        click_hwnd(btn_k1, settle=0.1)
        btn_listo = find_button(hwnd_main, exact="Listo")
        assert btn_listo is not None
        click_hwnd(btn_listo, settle=0.2)
        assert get_edit_text_msg(res_edits[0]) == "1", f"Teclado numerico no escribio '1': {get_edit_text_msg(res_edits[0])}"

        set_edit_text(res_edits[1], "")
        click_client_center(res_edits[1], settle=0.25)
        btn_k0 = wait_until(lambda: find_button(hwnd_main, exact="0"), desc="Tecla 0 de NumericKeypad")
        click_hwnd(btn_k0, settle=0.1)
        btn_listo = find_button(hwnd_main, exact="Listo")
        click_hwnd(btn_listo, settle=0.2)
        assert get_edit_text_msg(res_edits[1]) == "0", f"Teclado numerico no escribio '0': {get_edit_text_msg(res_edits[1])}"

        click_hwnd(btn_save_pol, settle=0.3)
        assert wait_until(lambda: any("Regla guardada" in t for t in visible_texts(hwnd_main)), desc="Regla guardada")

        btn_prev_cl = find_button(hwnd_main, text_contains="Revisar qué se borraría")
        btn_del_cl = find_button(hwnd_main, text_contains="Borrar respaldos antiguos")
        click_hwnd(btn_prev_cl, settle=0.35)

        lv = next((h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)), None)
        assert lv is not None
        assert wait_until(lambda: user32.SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0) == 1, desc="Vista previa 1 archivo a borrar")
        assert user32.IsWindowEnabled(btn_del_cl) != 0, "Borrar respaldos antiguos debe habilitarse tras vista previa con >= 1"

        # Cambiar N a 2 invalida vista previa y deshabilita Borrar
        set_edit_text(res_edits[0], "2")
        wait_until(lambda: user32.IsWindowEnabled(btn_del_cl) == 0, desc="Editar N deshabilita Borrar")

        set_edit_text(res_edits[0], "1")
        click_hwnd(btn_save_pol, settle=0.25)
        click_hwnd(btn_prev_cl, settle=0.3)
        wait_until(lambda: user32.IsWindowEnabled(btn_del_cl) != 0, desc="Vista previa rehabilita Borrar")

        click_hwnd(btn_del_cl, settle=0.25)
        hwnd_cl_dlg, _ = wait_for_window(p.pid, "Confirmar limpieza de respaldos", timeout=8.0)
        btn_cl_yes = find_button(hwnd_cl_dlg, text_contains="Sí, borrar respaldos")
        assert btn_cl_yes is not None
        click_hwnd(btn_cl_yes, settle=0.3)
        msg_cl = dismiss_messagebox(p.pid, "Limpieza de respaldos")
        assert "1 respaldo" in msg_cl, f"Resumen de limpieza inesperado: {msg_cl}"
        assert not os.path.exists(bkp_before), "bkp_before.db debió ser borrado por limpieza"
        assert os.path.exists(bkp_after), "bkp_after.db más reciente debió conservarse"

        # 6. Restaurar archivo danado -> rechazado sin tocar BD
        corrupt_path = os.path.join(temp_dir, "corrupto.db")
        with open(corrupt_path, "w", encoding="utf-8") as f:
            f.write("esto no es sqlite\n")
        btn_rest = find_button(hwnd_main, text_contains="Restaurar desde un respaldo")
        click_hwnd(btn_rest, settle=0.2)
        accept_file_dialog(p.pid, "Elegir archivo de respaldo", corrupt_path)
        err_corr = dismiss_messagebox(p.pid, "Respaldo inválido o dañado")
        assert "no es una base" in err_corr.lower() or "inválido" in err_corr.lower() or "integridad" in err_corr.lower()

        # 7. Restaurar version futura -> verifica advertencia + 2a confirmacion y falla sin alterar datos
        future_path = os.path.join(temp_dir, "futuro.db")
        shutil.copy2(bkp_restore_copy, future_path)
        con_f = sqlite3.connect(future_path)
        con_f.execute("INSERT INTO schema_migrations (version, description) VALUES (9999, 'future_test');")
        con_f.commit()
        con_f.close()

        click_hwnd(btn_rest, settle=0.2)
        accept_file_dialog(p.pid, "Elegir archivo de respaldo", future_path)
        hwnd_w1, _ = wait_for_window(p.pid, "Revisar respaldo antes de restaurar", timeout=8.0)
        w1_texts = "\n".join(visible_texts(hwnd_w1))
        assert "reemplazarán todos los datos actuales" in w1_texts
        assert "cierre" in w1_texts.lower() or "cerrará" in w1_texts.lower()
        btn_cont = find_button(hwnd_w1, text_contains="Continuar con restauración")
        assert btn_cont is not None
        click_hwnd(btn_cont, settle=0.25)

        hwnd_w2, _ = wait_for_window(p.pid, "Confirmación final", timeout=8.0)
        btn_red = find_button(hwnd_w2, text_contains="Sí, reemplazar y restaurar")
        assert btn_red is not None
        click_hwnd(btn_red, settle=0.3)

        err_fut = dismiss_messagebox(p.pid, "Restauración no aplicada")
        assert "más nueva" in err_fut and "intactos" in err_fut, f"Error de versión futura inesperado: {err_fut}"

        # 8. Restauracion valida -> copia previa, cierre de sesion y rollback real
        click_hwnd(btn_rest, settle=0.2)
        accept_file_dialog(p.pid, "Elegir archivo de respaldo", bkp_restore_copy)
        hwnd_rw1, _ = wait_for_window(p.pid, "Revisar respaldo antes de restaurar", timeout=8.0)
        click_hwnd(find_button(hwnd_rw1, text_contains="Continuar con restauración"), settle=0.25)
        hwnd_rw2, _ = wait_for_window(p.pid, "Confirmación final", timeout=8.0)
        click_hwnd(find_button(hwnd_rw2, text_contains="Sí, reemplazar y restaurar"), settle=0.35)

        done_msg = dismiss_messagebox(p.pid, "Restauración completada")
        assert "pre_restore_" in done_msg, f"Mensaje de éxito no muestra ruta pre_restore: {done_msg}"
        pre_restore_lines = [ln.strip() for ln in done_msg.splitlines() if "pre_restore_" in ln]
        assert pre_restore_lines and os.path.exists(pre_restore_lines[0]), (
            f"Archivo pre_restore reportado no existe: {done_msg}"
        )

        p.wait(timeout=10)

        # Reabrir y verificar que cajero_temp desaparecio por el rollback
        con_after = sqlite3.connect(db_path)
        cnt_after = con_after.execute("SELECT COUNT(*) FROM users WHERE username='cajero_temp'").fetchone()[0]
        con_after.close()
        assert cnt_after == 0, "La restauracion no revirtio cajero_temp"

        # 9. Probar permisos: usuario Cajero sin backup.manage vs con backup.manage (sin backup.restore)
        p_perm = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
        procs.append(p_perm)
        hwnd_l_p, _ = wait_for_window(p_perm.pid, "Iniciar Sesión", timeout=12.0)
        le = sorted(
            [h for (h, c, _t) in enum_children(hwnd_l_p) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        set_edit_text(le[0], "dueno_t6")
        set_edit_text(le[1], "CafeT6!2026")
        click_hwnd(find_button(hwnd_l_p, text_contains="Iniciar Sesión"), settle=0.25)
        hwnd_m_p, _ = wait_for_window(p_perm.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)

        open_section_and_subnav(hwnd_m_p, "BackOffice", "Usuarios y cuentas")
        click_hwnd(wait_until(lambda: find_button(hwnd_m_p, text_contains="Nuevo Usuario"), desc="Nuevo Usuario perm"), settle=0.2)
        hwnd_nu2, _ = wait_for_window(p_perm.pid, "Nuevo Usuario de Sistema", timeout=8.0)
        nu2e = sorted(
            [h for (h, c, _t) in enum_children(hwnd_nu2) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        set_edit_text(nu2e[0], "Cajero Sin Respaldo")
        set_edit_text(nu2e[1], "cajero_norst")
        set_edit_text(nu2e[2], "norst@t6.local")
        set_edit_text(nu2e[3], "Caj!2026")
        click_hwnd(find_button(hwnd_nu2, text_contains="Guardar Usuario"), settle=0.4)
        wait_until(lambda: not find_windows_by_pid(p_perm.pid, "Nuevo Usuario de Sistema"), desc="Cerrar Nuevo Usuario perm")
        user32.PostMessageW(hwnd_m_p, WM_CLOSE, 0, 0)
        p_perm.wait(timeout=8)

        # Login como cajero_norst (sin backup.manage)
        p_c1 = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
        procs.append(p_c1)
        hwnd_lc1, _ = wait_for_window(p_c1.pid, "Iniciar Sesión", timeout=12.0)
        le1 = sorted(
            [h for (h, c, _t) in enum_children(hwnd_lc1) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        set_edit_text(le1[0], "cajero_norst")
        set_edit_text(le1[1], "Caj!2026")
        click_hwnd(find_button(hwnd_lc1, text_contains="Iniciar Sesión"), settle=0.25)
        hwnd_mc1, _ = wait_for_window(p_c1.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
        open_section_and_subnav(hwnd_mc1, "BackOffice", "Respaldos")
        assert wait_until(lambda: any("Sin permiso para respaldos" in t for t in visible_texts(hwnd_mc1)), desc="Acceso denegado backup.manage")
        assert find_button(hwnd_mc1, text_contains="Hacer un respaldo ahora") is None
        assert find_button(hwnd_mc1, text_contains="Restaurar desde un respaldo") is None
        user32.PostMessageW(hwnd_mc1, WM_CLOSE, 0, 0)
        p_c1.wait(timeout=8)

        # Otorgar backup.manage al rol role-cashier (sin backup.restore) y comprobar que Restaurar queda oculto
        con_p = sqlite3.connect(db_path)
        con_p.execute("INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES ('role-cashier', 'backup.manage');")
        con_p.commit()
        con_p.close()

        p_c2 = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
        procs.append(p_c2)
        hwnd_lc2, _ = wait_for_window(p_c2.pid, "Iniciar Sesión", timeout=12.0)
        le2 = sorted(
            [h for (h, c, _t) in enum_children(hwnd_lc2) if c.lower() == "edit" and user32.IsWindowVisible(h)],
            key=lambda h: get_rect(h)[1],
        )
        set_edit_text(le2[0], "cajero_norst")
        set_edit_text(le2[1], "Caj!2026")
        click_hwnd(find_button(hwnd_lc2, text_contains="Iniciar Sesión"), settle=0.25)
        hwnd_mc2, _ = wait_for_window(p_c2.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
        open_section_and_subnav(hwnd_mc2, "BackOffice", "Respaldos")
        assert wait_until(lambda: find_button(hwnd_mc2, text_contains="Hacer un respaldo ahora") is not None, desc="Hacer respaldo visible con backup.manage")
        assert find_button(hwnd_mc2, text_contains="Restaurar desde un respaldo") is None, (
            "Restaurar debe ocultarse sin permiso backup.restore"
        )
        assert any("Solo el Dueño puede restaurar respaldos" in t for t in visible_texts(hwnd_mc2)), (
            "Falta explicacion de restauracion solo Dueno"
        )
        user32.PostMessageW(hwnd_mc2, WM_CLOSE, 0, 0)
        p_c2.wait(timeout=8)

        print(f"  [OK] {build_label}: backed up -> cleaned old -> rejected invalid/future -> restored ({pre_restore_lines[0]}) -> permissions OK")
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
        shutil.rmtree(temp_dir, ignore_errors=True)


def main():
    enable_per_monitor_dpi()
    sys_dpi, sys_scale = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {sys_dpi} (escala={sys_scale:.2f})")

    run_suite("Release", RELEASE_EXE, sys_scale)
    run_suite("Debug", DEBUG_EXE, sys_scale)
    print("\n=== PRUEBA T6 RESPALDOS EXITOSA (Release y Debug) ===")


if __name__ == "__main__":
    main()
