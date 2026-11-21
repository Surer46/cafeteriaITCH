#!/usr/bin/env python3
"""
test_t15_printer_missing.py - Prueba automatizada del aviso «Impresora No Encontrada» (T15).

Verifica en Release y Debug con ejecutable real, SQLite aislado y CAFETERIA_UPDATE_CHECK=0:
1. Configura una impresora inexistente ('Impresora T15 inexistente') en station_printer.cfg
   dentro del directorio aislado CAFETERIA_DATA_DIR.
2. Realiza una venta y solicita imprimir su ticket.
3. Al aparecer «Impresora No Encontrada», lee los textos reales de sus botones y valida
   que son 'Continuar' y 'Cancelar' (y no 'Aceptar').
4. Al pulsar 'Cancelar': no aparece el diálogo de impresión del sistema y la venta sigue
   registrada exactamente una vez en SQLite.
5. Al volver a pedir imprimir y pulsar 'Continuar': aparece el diálogo de impresión del
   sistema; se cancela con el mismo mecanismo de test_i2_integration.py y la venta sigue
   registrada exactamente una vez en SQLite.
"""

import ctypes
import os
import secrets
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from gui_dpi import (
    enable_per_monitor_dpi,
    ensure_supported_system_dpi,
    format_dpi_info,
)
import test_i2_integration as i2

PRINTER_NAME_MISSING = "Impresora T15 inexistente"


def write_missing_printer_config(data_dir):
    cfg_path = os.path.join(data_dir, "station_printer.cfg")
    with open(cfg_path, "w", encoding="utf-8") as f:
        f.write(
            "profile=80mm\n"
            "nominal_width_mm=80\n"
            "characters_per_line=42\n"
            f"printer_name={PRINTER_NAME_MISSING}\n"
        )
    return cfg_path


def seed_t15_product(db_path):
    con = sqlite3.connect(db_path)
    cur = con.cursor()
    cur.execute(
        """
        INSERT OR IGNORE INTO products
            (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
        VALUES
            ('prod-t15-01', 'T15-CAFE', '750100001501', 'Cafe Americano T15', 'Bebidas', 3500, 0, 1, '2026-11-20 00:00:00');
        """
    )
    con.commit()
    con.close()


def read_dialog_buttons(hwnd_dlg):
    buttons = []
    for h, cls, txt in i2.get_child_controls(hwnd_dlg):
        if cls.lower() == "button" and i2.user32.IsWindowVisible(h):
            clean = txt.replace("&", "").strip()
            if clean:
                buttons.append((h, clean, txt))
    return buttons


def assert_missing_printer_buttons(hwnd_dlg):
    buttons = read_dialog_buttons(hwnd_dlg)
    labels = [clean for (_h, clean, _raw) in buttons]
    assert "Aceptar" not in labels, f"El diálogo 'Impresora No Encontrada' mostró 'Aceptar' en lugar de 'Continuar': {labels}"
    assert "Continuar" in labels, f"No se encontró botón 'Continuar' en 'Impresora No Encontrada'. Botones reales: {labels}"
    assert "Cancelar" in labels, f"No se encontró botón 'Cancelar' en 'Impresora No Encontrada'. Botones reales: {labels}"
    btn_continue = next(h for (h, clean, _raw) in buttons if clean == "Continuar")
    btn_cancel = next(h for (h, clean, _raw) in buttons if clean == "Cancelar")
    return labels, btn_continue, btn_cancel


def click_sidebar_and_wait_button(hwnd_main, sidebar_label, target_btn_text, timeout=8.0):
    ctrls = i2.get_child_controls(hwnd_main)
    btn_side = next(
        (h for (h, cls, txt) in ctrls if cls.lower() == "button" and sidebar_label in txt),
        None,
    )
    assert btn_side is not None, f"Botón lateral '{sidebar_label}' no encontrado"
    i2.send_click_button(btn_side)
    deadline = time.time() + timeout
    while time.time() < deadline:
        now_ctrls = i2.get_child_controls(hwnd_main)
        found = next(
            (h for (h, cls, txt) in now_ctrls if cls.lower() == "button" and i2.user32.IsWindowVisible(h) and target_btn_text in txt),
            None,
        )
        if found is not None:
            return now_ctrls
        time.sleep(0.1)
    raise TimeoutError(f"Timeout ({timeout}s) esperando botón '{target_btn_text}' tras abrir '{sidebar_label}'")


def count_sales(db_path):
    con = sqlite3.connect(db_path)
    try:
        return con.execute("SELECT COUNT(*) FROM sales").fetchone()[0]
    finally:
        con.close()


def run_t15_for_binary(exe_path, work_dir, build_label):
    print(f"\n=======================================================", flush=True)
    print(f"PRUEBA T15 AVISO IMPRESORA NO ENCONTRADA [{build_label}]", flush=True)
    print(f"  Binario: {exe_path}", flush=True)
    print(f"=======================================================", flush=True)

    if not os.path.exists(exe_path):
        raise FileNotFoundError(f"Ejecutable no encontrado: {exe_path}")

    temp_data_dir = tempfile.mkdtemp(prefix=f"cafeteria_t15_{build_label.lower()}_")
    db_path = os.path.join(temp_data_dir, "cafeteria.db")
    cfg_path = write_missing_printer_config(temp_data_dir)
    print(f"[OK] {build_label} station_printer.cfg escrito en {cfg_path} (printer_name='{PRINTER_NAME_MISSING}').", flush=True)

    env = os.environ.copy()
    env["PATH"] = work_dir + ";" + env.get("PATH", "")
    env["CAFETERIA_DATA_DIR"] = temp_data_dir
    env["CAFETERIA_UPDATE_CHECK"] = "0"

    procs = []
    had_exception = False
    username = "dueno_t15_" + secrets.token_hex(3)
    password = i2.generate_random_secret(18)
    email = f"{username}@itch.edu.mx"

    try:
        # 1. Alta inicial del Dueño
        proc1 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(proc1)
        hwnd_owner, _ = i2.wait_for_window_by_pid(proc1.pid, "Primer Inicio", timeout=12.0, step_name="Alta Dueño T15")
        o_ctrls = i2.get_child_controls(hwnd_owner)
        o_edits = [h for (h, cls, _t) in o_ctrls if cls.lower() == "edit"]
        o_btns = [h for (h, cls, _t) in o_ctrls if cls.lower() == "button"]
        assert len(o_edits) >= 5, f"Se esperaban 5 edits en Primer Inicio, encontrados {len(o_edits)}"
        i2.set_text(o_edits[0], "Dueno T15")
        i2.set_text(o_edits[1], username)
        i2.set_text(o_edits[2], email)
        i2.set_text(o_edits[3], password)
        i2.set_text(o_edits[4], password)
        time.sleep(0.2)
        i2.post_click_button(o_btns[0])

        hwnd_main1, _ = i2.wait_for_window_by_pid(
            proc1.pid, "Cafetería ITCH II", timeout=12.0, step_name="MainWindow Primer Inicio",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
        )
        i2.close_window_and_wait(proc1, hwnd_main1, timeout=6.0)

        # 2. Sembrar producto sin receta para venta rápida
        assert os.path.exists(db_path), f"No se creó la base SQLite en {db_path}"
        seed_t15_product(db_path)

        # 3. Sesión principal: abrir turno, vender y probar impresión
        proc2 = subprocess.Popen([exe_path], cwd=work_dir, env=env)
        procs.append(proc2)
        pid2 = proc2.pid

        hwnd_login, _ = i2.wait_for_window_by_pid(pid2, "Iniciar Sesión", timeout=12.0, step_name="Login T15")
        l_ctrls = i2.get_child_controls(hwnd_login)
        l_edits = [h for (h, cls, _t) in l_ctrls if cls.lower() == "edit"]
        l_btns = [h for (h, cls, _t) in l_ctrls if cls.lower() == "button"]
        i2.set_text(l_edits[0], username)
        i2.set_text(l_edits[1], password)
        time.sleep(0.2)
        i2.post_click_button(l_btns[0])

        hwnd_main2, _ = i2.wait_for_window_by_pid(
            pid2, "Cafetería ITCH II", timeout=12.0, step_name="MainWindow Login T15",
            exclude_substrings=["Primer Inicio", "Iniciar Sesión"],
        )

        # Abrir turno de caja
        shift_ctrls = click_sidebar_and_wait_button(hwnd_main2, "Caja y Reportes", "Abrir Turno de Caja")
        shift_edits = [h for (h, cls, _t) in shift_ctrls if cls.lower() == "edit" and i2.user32.IsWindowVisible(h)]
        btn_open_shift = next(h for (h, cls, t) in shift_ctrls if cls.lower() == "button" and "Abrir Turno de Caja" in t)
        if shift_edits:
            i2.set_text(shift_edits[0], "200.00")
            time.sleep(0.15)
        i2.post_click_button(btn_open_shift)
        i2.wait_for_shift_open_dialog_by_pid(pid2, timeout=10.0)

        # Ir a Punto de Venta y agregar producto
        pos_ctrls = click_sidebar_and_wait_button(hwnd_main2, "Punto de Venta", "Agregar al Carrito")
        btn_add = next(h for (h, cls, t) in pos_ctrls if cls.lower() == "button" and "Agregar al Carrito" in t)
        i2.send_click_button(btn_add)

        deadline = time.time() + 6.0
        btn_cobrar = None
        while time.time() < deadline:
            cur_ctrls = i2.get_child_controls(hwnd_main2)
            cand = next(
                (h for (h, cls, t) in cur_ctrls if cls.lower() == "button" and i2.user32.IsWindowVisible(h) and "Cobrar" in t),
                None,
            )
            if cand is not None and i2.user32.IsWindowEnabled(cand):
                btn_cobrar = cand
                break
            time.sleep(0.1)
        assert btn_cobrar is not None, "Botón 'Cobrar' no se habilitó tras agregar producto"
        i2.post_click_button(btn_cobrar)

        # Cobrar venta ($35.00, recibido $50.00)
        hwnd_chk, _ = i2.wait_for_window_by_pid(pid2, "Cobro de Venta", timeout=10.0, step_name="Cobro T15")
        chk_ctrls = i2.get_child_controls(hwnd_chk)
        chk_edits = [h for (h, cls, _t) in chk_ctrls if cls.lower() == "edit"]
        btn_confirm = next(
            (h for (h, cls, t) in chk_ctrls if cls.lower() == "button" and "Confirmar Venta" in t),
            None,
        )
        assert btn_confirm is not None, "Botón 'Confirmar Venta' no encontrado"
        i2.set_text(chk_edits[0], "50.00")
        time.sleep(0.15)
        i2.post_click_button(btn_confirm)

        # TicketPrintDialog debe mostrar la impresora configurada inexistente
        hwnd_ticket, _ = i2.wait_for_window_by_pid(pid2, "Venta Registrada", timeout=10.0, step_name="TicketPrintDialog T15")
        assert count_sales(db_path) == 1, f"Se esperaba 1 venta tras confirmar, hay {count_sales(db_path)}"

        t_ctrls = i2.get_child_controls(hwnd_ticket)
        t_texts = [t for (_h, _cls, t) in t_ctrls if t]
        assert any(PRINTER_NAME_MISSING in t for t in t_texts), (
            f"TicketPrintDialog no muestra la impresora configurada '{PRINTER_NAME_MISSING}': {t_texts}"
        )
        btn_print = next((h for (h, cls, t) in t_ctrls if cls.lower() == "button" and "Imprimir" in t), None)
        btn_not_now = next((h for (h, cls, t) in t_ctrls if cls.lower() == "button" and "Ahora no" in t), None)
        assert btn_print is not None, "Botón 'Imprimir' no encontrado en TicketPrintDialog"
        assert btn_not_now is not None, "Botón 'Ahora no' no encontrado en TicketPrintDialog"

        # 4. Primer intento de impresión -> Cancelar en «Impresora No Encontrada»
        i2.post_click_button(btn_print)
        hwnd_warn1, _ = i2.wait_for_window_by_pid(pid2, "Impresora No Encontrada", timeout=8.0, step_name="Aviso Impresora No Encontrada (Cancelar)")
        labels1, _btn_cont1, btn_cancel1 = assert_missing_printer_buttons(hwnd_warn1)
        print(f"[OK] {build_label} Botones leídos en 'Impresora No Encontrada' (1er intento): {labels1}", flush=True)

        i2.post_click_button(btn_cancel1)
        assert i2.wait_for_window_close(hwnd_warn1, timeout=5.0), "'Impresora No Encontrada' no cerró tras pulsar 'Cancelar'"

        # Verificar que NO apareció diálogo de impresión del sistema
        sys_deadline = time.time() + 1.0
        while time.time() < sys_deadline:
            h_sys_unexpected, _, desc_unexpected = i2._find_system_print_dialog(pid2, owner_hwnd=hwnd_ticket)
            assert h_sys_unexpected is None, (
                f"Apareció diálogo de impresión del sistema ({desc_unexpected}) tras pulsar 'Cancelar'"
            )
            time.sleep(0.15)
        assert i2.user32.IsWindow(hwnd_ticket) and i2.user32.IsWindowVisible(hwnd_ticket), (
            "TicketPrintDialog debió permanecer visible tras cancelar 'Impresora No Encontrada'"
        )
        assert count_sales(db_path) == 1, f"La venta debió seguir 1 sola vez tras Cancelar, encontradas {count_sales(db_path)}"
        print(f"[OK] {build_label} Cancelar volvió al diálogo sin impresión del sistema y con 1 venta en SQLite.", flush=True)

        # 5. Segundo intento -> Continuar en «Impresora No Encontrada» -> cancelar diálogo del sistema
        i2.post_click_button(btn_print)
        hwnd_warn2, _ = i2.wait_for_window_by_pid(pid2, "Impresora No Encontrada", timeout=8.0, step_name="Aviso Impresora No Encontrada (Continuar)")
        labels2, btn_cont2, _btn_cancel2 = assert_missing_printer_buttons(hwnd_warn2)
        print(f"[OK] {build_label} Botones leídos en 'Impresora No Encontrada' (2o intento): {labels2}", flush=True)

        i2.post_click_button(btn_cont2)
        assert i2.wait_for_window_close(hwnd_warn2, timeout=5.0), "'Impresora No Encontrada' no cerró tras pulsar 'Continuar'"

        h_sys, desc_sys = i2.cancel_system_print_dialog_by_pid(pid2, timeout=8.0, owner_hwnd=hwnd_ticket)
        assert h_sys is not None, (
            f"No apareció el diálogo de impresión del sistema tras elegir 'Continuar'. "
            f"Ventanas visibles: {i2.dump_visible_windows()}"
        )
        print(f"[OK] {build_label} Diálogo de impresión del sistema detectado y cancelado ({desc_sys}, HWND={h_sys}).", flush=True)

        hwnd_cancel_box, _ = i2.wait_for_window_by_pid(pid2, "Impresión Cancelada", timeout=6.0, step_name="Aviso Impresión Cancelada")
        i2.dismiss_dialog_ok(hwnd_cancel_box)
        assert i2.wait_for_window_close(hwnd_cancel_box, timeout=5.0), "'Impresión Cancelada' no cerró"

        i2.post_click_button(btn_not_now)
        assert i2.wait_for_window_close(hwnd_ticket, timeout=5.0), "TicketPrintDialog no cerró tras pulsar 'Ahora no'"

        # Esperar por sondeo (hasta 10 s) a que Windows retire por completo la interfaz de impresión
        # (p. ej. CoreWindow de PrintDialog.exe) antes de cerrar app_desktop.exe.
        print_clear_deadline = time.time() + 10.0
        leftover_before_close = i2._find_leftover_print_windows()
        while leftover_before_close and time.time() < print_clear_deadline:
            time.sleep(0.15)
            leftover_before_close = i2._find_leftover_print_windows()
        assert not leftover_before_close, (
            f"El subsistema de impresión conservó ventanas visibles tras cancelar (>10 s): {leftover_before_close}"
        )

        sales_final = count_sales(db_path)
        assert sales_final == 1, f"La venta debió seguir 1 sola vez en SQLite tras Continuar/Cancelar impresión, encontradas {sales_final}"
        print(f"[OK] {build_label} Venta preservada 1 sola vez en SQLite tras flujo completo.", flush=True)

        i2.close_window_and_wait(proc2, hwnd_main2, timeout=6.0)

    except Exception:
        had_exception = True
        raise
    finally:
        for p in procs:
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
        leftover = i2._find_leftover_print_windows()
        if leftover:
            time.sleep(0.4)
            leftover = i2._find_leftover_print_windows()
        shutil.rmtree(temp_data_dir, ignore_errors=True)
        if leftover and not had_exception:
            raise RuntimeError(f"Quedaron ventanas visibles de impresión al terminar T15 [{build_label}]: {leftover}")


def main():
    enable_per_monitor_dpi()
    dpi, _ = ensure_supported_system_dpi()
    print(f"DPI sistema detectado: {format_dpi_info(dpi)}", flush=True)

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    rel_dir = os.path.join(repo_root, "build_release")
    rel_exe = os.path.join(rel_dir, "app_desktop.exe")
    dbg_dir = os.path.join(repo_root, "build_debug")
    dbg_exe = os.path.join(dbg_dir, "app_desktop.exe")

    run_t15_for_binary(rel_exe, rel_dir, "Release")
    run_t15_for_binary(dbg_exe, dbg_dir, "Debug")
    print("\n[EXITO TOTAL] Prueba T15 superada en Release y Debug.", flush=True)


if __name__ == "__main__":
    main()
