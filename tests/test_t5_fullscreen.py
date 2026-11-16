import ctypes
from ctypes import wintypes
import os
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui_dpi import (
    ensure_supported_system_dpi,
    format_dpi_info,
    get_window_dpi,
    get_window_scale,
    measure_text_extent,
    min_px,
    px_to_logical,
    to_px,
)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE_EXE = os.path.join(REPO_ROOT, "build_release", "app_desktop.exe")
DEBUG_EXE = os.path.join(REPO_ROOT, "build_debug", "app_desktop.exe")

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

BM_CLICK = 0x00F5
WM_SETTEXT = 0x000C
WM_CLOSE = 0x0010
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
KEYEVENTF_KEYUP = 0x0002
VK_ESCAPE = 0x1B
VK_F11 = 0x7A
GWL_STYLE = -16
WS_CAPTION = 0x00C00000
SW_MAXIMIZE = 3
SW_RESTORE = 9
MONITOR_DEFAULTTONEAREST = 2
LVM_GETITEMCOUNT = 0x1004


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("hwndActive", wintypes.HWND),
        ("hwndFocus", wintypes.HWND),
        ("hwndCapture", wintypes.HWND),
        ("hwndMenuOwner", wintypes.HWND),
        ("hwndMoveSize", wintypes.HWND),
        ("hwndCaret", wintypes.HWND),
        ("rcCaret", wintypes.RECT),
    ]


class MONITORINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("rcMonitor", wintypes.RECT),
        ("rcWork", wintypes.RECT),
        ("dwFlags", wintypes.DWORD),
    ]


def ensure_no_app_running():
    res = subprocess.run(["tasklist", "/FI", "IMAGENAME eq app_desktop.exe"], capture_output=True, text=True)
    if "app_desktop.exe" in res.stdout:
        subprocess.run(["taskkill", "/F", "/IM", "app_desktop.exe"], capture_output=True)
        time.sleep(0.4)


def get_text(hwnd):
    ln = user32.GetWindowTextLengthW(hwnd)
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    user32.GetWindowTextW(hwnd, buf, ln + 2)
    return buf.value


def get_class(hwnd):
    buf = ctypes.create_unicode_buffer(128)
    user32.GetClassNameW(hwnd, buf, 128)
    return buf.value


def describe_window_owner(hwnd):
    if not hwnd:
        return "hwnd=0x0"
    hwnd_root = user32.GetAncestor(hwnd, 2) or hwnd
    wpid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd_root, ctypes.byref(wpid))
    exe_name = "<desconocido>"
    if wpid.value:
        h_proc = kernel32.OpenProcess(0x1000, False, wpid.value)
        if h_proc:
            try:
                sz = wintypes.DWORD(512)
                buf = ctypes.create_unicode_buffer(512)
                if kernel32.QueryFullProcessImageNameW(h_proc, 0, buf, ctypes.byref(sz)):
                    exe_name = os.path.basename(buf.value)
            finally:
                kernel32.CloseHandle(h_proc)
    return (
        f"hwnd={hwnd:#x} root={hwnd_root:#x} pid={wpid.value} exe='{exe_name}' "
        f"title='{get_text(hwnd_root)}' class='{get_class(hwnd_root)}' "
        f"child_title='{get_text(hwnd)}' child_class='{get_class(hwnd)}'"
    )


def get_rect(hwnd):
    rc = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rc))
    return (rc.left, rc.top, rc.right, rc.bottom)


def get_monitor_rect(hwnd):
    hmon = user32.MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
    assert hmon, "MonitorFromWindow devolvió nulo"
    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    ok = user32.GetMonitorInfoW(hmon, ctypes.byref(mi))
    assert ok, "GetMonitorInfoW falló"
    r = mi.rcMonitor
    return (r.left, r.top, r.right, r.bottom)


def has_caption(hwnd):
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    return (style & WS_CAPTION) == WS_CAPTION


def enum_children(hwnd_parent):
    out = []
    enum_proc_t = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def _cb(ch, _lp):
        out.append((ch, get_class(ch), get_text(ch)))
        return True

    cb_ref = enum_proc_t(_cb)
    user32.EnumChildWindows(hwnd_parent, cb_ref, 0)
    return out


def find_windows_by_pid(pid, title_sub=None, exclude=()):
    matches = []
    enum_proc_t = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def _cb(hwnd, _lp):
        if user32.IsWindowVisible(hwnd):
            wpid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
            if wpid.value == pid:
                t = get_text(hwnd)
                if (title_sub is None or title_sub in t) and not any(ex in t for ex in exclude):
                    matches.append((hwnd, t))
        return True

    cb_ref = enum_proc_t(_cb)
    user32.EnumWindows(cb_ref, 0)
    return matches


def wait_for_window(pid, title_sub, timeout=12.0, exclude=()):
    t0 = time.time()
    while time.time() - t0 < timeout:
        wins = find_windows_by_pid(pid, title_sub, exclude=exclude)
        if wins:
            return wins[0]
        time.sleep(0.08)
    raise AssertionError(f"Tiempo agotado esperando ventana '{title_sub}'")


def wait_until(pred, timeout=4.0, desc="condición"):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if pred():
            return
        time.sleep(0.06)
    raise AssertionError(f"Tiempo agotado esperando: {desc}")


def bring_foreground(hwnd):
    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, SW_RESTORE)
    fg = user32.GetForegroundWindow()
    if fg != hwnd:
        cur_tid = kernel32.GetCurrentThreadId()
        fg_tid = user32.GetWindowThreadProcessId(fg, None) if fg else 0
        if fg_tid and fg_tid != cur_tid:
            user32.AttachThreadInput(cur_tid, fg_tid, True)
            user32.SetForegroundWindow(hwnd)
            user32.AttachThreadInput(cur_tid, fg_tid, False)
        else:
            user32.SetForegroundWindow(hwnd)
    time.sleep(0.05)


def get_gui_focus(hwnd_top):
    tid = user32.GetWindowThreadProcessId(hwnd_top, None)
    gti = GUITHREADINFO()
    gti.cbSize = ctypes.sizeof(GUITHREADINFO)
    if user32.GetGUIThreadInfo(tid, ctypes.byref(gti)):
        return gti.hwndFocus
    return None


def set_edit_text(hwnd_edit, text):
    user32.SendMessageTimeoutW(hwnd_edit, WM_SETTEXT, 0, str(text), 0x0002, 2000, None)


def post_click_button(hwnd_btn):
    user32.PostMessageW(hwnd_btn, BM_CLICK, 0, 0)


def click_button(hwnd_btn, settle=0.25):
    user32.SendMessageTimeoutW(hwnd_btn, BM_CLICK, 0, 0, 0x0002, 2000, None)
    time.sleep(settle)


def click_control_at(hwnd_ctrl):
    rc = get_rect(hwnd_ctrl)
    lx = max(2, (rc[2] - rc[0]) // 2)
    ly = max(2, (rc[3] - rc[1]) // 2)
    lparam = (ly << 16) | (lx & 0xFFFF)
    user32.PostMessageW(hwnd_ctrl, 0x0201, 1, lparam)
    time.sleep(0.04)
    user32.PostMessageW(hwnd_ctrl, 0x0202, 0, lparam)
    time.sleep(0.18)


def send_vk(hwnd_top, vk_code, settle=0.35):
    bring_foreground(hwnd_top)
    if user32.GetForegroundWindow() == hwnd_top:
        user32.keybd_event(vk_code, 0, 0, 0)
        time.sleep(0.03)
        user32.keybd_event(vk_code, 0, KEYEVENTF_KEYUP, 0)
        time.sleep(settle)
        return
    target = get_gui_focus(hwnd_top) or hwnd_top
    user32.PostMessageW(target, WM_KEYDOWN, vk_code, 1)
    time.sleep(0.04)
    user32.PostMessageW(target, WM_KEYUP, vk_code, 0xC0000001)
    time.sleep(settle)


def click_sidebar_section(hwnd_main, label_sub):
    btns = [
        (h, t) for (h, c, t) in enum_children(hwnd_main)
        if c.lower() == "button" and user32.IsWindowVisible(h) and label_sub in t
    ]
    assert btns, f"Botón de sidebar '{label_sub}' no encontrado"
    btn = min(btns, key=lambda it: get_rect(it[0])[0])[0]
    click_button(btn, settle=0.3)


def click_subnav(hwnd_main, label_exact):
    btns = [
        (h, t) for (h, c, t) in enum_children(hwnd_main)
        if c.lower() == "button" and user32.IsWindowVisible(h) and t.strip() == label_exact
    ]
    assert btns, f"Botón subnav '{label_exact}' no encontrado"
    btn = min(btns, key=lambda it: get_rect(it[0])[1])[0]
    click_button(btn, settle=0.3)


def read_cfg_text(path):
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        return f.read().strip()


def assert_nav_bars_fit(hwnd_main, dpi, scale, is_touch, context):
    win_rc = get_rect(hwnd_main)
    btns = [
        (h, t) for (h, c, t) in enum_children(hwnd_main)
        if c.lower() == "button" and user32.IsWindowVisible(h) and t.strip()
    ]
    sidebar_labels = {"Punto de Venta", "Inventario", "Caja y Reportes", "BackOffice"}
    subnav_pos_labels = {"Venta", "Catálogo visual"}
    subnav_bo_labels = {"Usuarios y cuentas", "Roles y permisos", "Configuración"}
    header_profile_labels = {"Touch", "Estándar"}

    checked = 0
    for h, t in btns:
        clean_t = t.strip()
        is_nav = (
            any(s in clean_t for s in sidebar_labels)
            or clean_t in subnav_pos_labels
            or clean_t in subnav_bo_labels
            or clean_t in header_profile_labels
        )
        if not is_nav:
            continue

        rc = get_rect(h)
        w, h_px = rc[2] - rc[0], rc[3] - rc[1]
        assert (
            rc[0] >= win_rc[0] and rc[1] >= win_rc[1] and rc[2] <= win_rc[2] and rc[3] <= win_rc[3]
        ), f"[{context}] Botón '{clean_t}' fuera de hwnd_main: btn={rc}, win={win_rc}"

        checked += 1
        tw, th = measure_text_extent(h, clean_t)
        assert w >= tw + to_px(8, scale), f"[{context}] Texto recortado en '{clean_t}': ancho={w}px, texto={tw}px"
        assert h_px >= th + to_px(4, scale), f"[{context}] Altura insuficiente en '{clean_t}': alto={h_px}px, texto={th}px"

    assert checked >= 5, f"[{context}] Muy pocos controles de navegación auditados: {checked}"


def find_header_profile_btn(hwnd_main):
    btns = [
        (h, t) for (h, c, t) in enum_children(hwnd_main)
        if c.lower() == "button" and user32.IsWindowVisible(h) and t in ("Touch", "Estándar")
    ]
    assert btns, "Botón de perfil en encabezado no encontrado"
    return min(btns, key=lambda it: get_rect(it[0])[1])


def assert_fullscreen_state(hwnd_main, cfg_path, expect_full, context):
    mon_rc = get_monitor_rect(hwnd_main)
    win_rc = get_rect(hwnd_main)
    cap = has_caption(hwnd_main)
    cfg_val = read_cfg_text(cfg_path)
    if expect_full:
        assert win_rc == mon_rc, f"[{context}] Ventana no cubre monitor completo: win={win_rc}, monitor={mon_rc}"
        assert not cap, f"[{context}] Ventana en pantalla completa no debe tener WS_CAPTION"
        assert cfg_val == "fullscreen", f"[{context}] station_fullscreen.cfg esperado 'fullscreen', obtenido {cfg_val!r}"
    else:
        assert cap, f"[{context}] Ventana normal debe tener WS_CAPTION"
        assert cfg_val in (None, "windowed"), f"[{context}] station_fullscreen.cfg esperado 'windowed'/ausente, obtenido {cfg_val!r}"
    return mon_rc, win_rc


def run_t5_suite(exe_path, build_label):
    assert os.path.exists(exe_path), f"No existe ejecutable {build_label}: {exe_path}"
    print(f"\n=== T5 Modo Pantalla Completa ({build_label}) ===", flush=True)
    ensure_no_app_running()

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t5_{build_label.lower()}_") as tmp_dir:
        db_path = os.path.join(tmp_dir, "cafeteria.db")
        cfg_fs_path = os.path.join(tmp_dir, "station_fullscreen.cfg")
        cfg_prof_path = os.path.join(tmp_dir, "station_profile.cfg")
        env = os.environ.copy()
        env["CAFETERIA_DATA_DIR"] = tmp_dir
        procs = []
        try:
            # 1. Sin station_fullscreen.cfg: abre en ventana normal (con WS_CAPTION)
            assert not os.path.exists(cfg_fs_path)
            proc = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
            procs.append(proc)

            hwnd_first, _ = wait_for_window(proc.pid, "Primer Inicio", timeout=10.0)
            edits_f = [h for (h, c, _t) in enum_children(hwnd_first) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            btns_f = [h for (h, c, _t) in enum_children(hwnd_first) if c.lower() == "button" and user32.IsWindowVisible(h)]
            set_edit_text(edits_f[0], "Dueño T5 Prueba")
            set_edit_text(edits_f[1], "dueno_t5")
            set_edit_text(edits_f[2], "dueno_t5@itch.edu.mx")
            set_edit_text(edits_f[3], "CafeT5!2026")
            set_edit_text(edits_f[4], "CafeT5!2026")
            time.sleep(0.15)
            post_click_button(btns_f[0])

            hwnd_main, _ = wait_for_window(proc.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
            dpi = get_window_dpi(hwnd_main)
            scale = get_window_scale(hwnd_main)
            time.sleep(0.3)

            assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=False, context=f"{build_label} Inicio normal")
            rect_before = get_rect(hwnd_main)

            # Sembrar producto y abrir turno en Caja para permitir Cobrar
            con = sqlite3.connect(db_path)
            con.execute(
                """
                INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
                VALUES ('prod-t5-01', 'T5-01', '750100009955', 'Capuchino T5', 'Bebidas', 4500, 0, 1, '2026-11-14 10:00:00')
                """
            )
            con.commit()
            con.close()

            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)
            btn_open_sh = next(
                h for (h, c, t) in enum_children(hwnd_main)
                if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno" in t
            )
            post_click_button(btn_open_sh)
            hwnd_sh_msg, _ = wait_for_window(proc.pid, "Turno Abierto", timeout=6.0)
            sh_ok = next(h for (h, c, _t) in enum_children(hwnd_sh_msg) if c.lower() == "button" and user32.IsWindowVisible(h))
            post_click_button(sh_ok)
            time.sleep(0.3)

            click_sidebar_section(hwnd_main, "Punto de Venta")
            time.sleep(0.3)

            vis_pos = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            edits_pos = sorted([(h, get_rect(h)) for (h, c, _t) in vis_pos if c.lower() == "edit"], key=lambda x: (x[1][1], x[1][0]))
            txt_search = edits_pos[0][0]
            btn_add = next(h for (h, c, t) in vis_pos if c.lower() == "button" and "Agregar al Carrito" in t)
            btn_checkout = next(h for (h, c, t) in vis_pos if c.lower() == "button" and "Cobrar" in t)

            click_button(btn_add, settle=0.35)
            con = sqlite3.connect(db_path)
            qty0 = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items").fetchone()[0]
            con.close()
            assert qty0 == 1, f"Carrito esperado 1 antes de F11, obtenido {qty0}"

            # 2. Foco en campo de texto + F11 -> entra en pantalla completa
            click_control_at(txt_search)
            send_vk(hwnd_main, VK_F11)
            wait_until(lambda: not has_caption(hwnd_main), desc="F11 entrar en pantalla completa")
            mon_rc, rect_fs = assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=True, context=f"{build_label} F11 desde Edit")
            assert_nav_bars_fit(hwnd_main, dpi, scale, is_touch=False, context=f"{build_label} Fullscreen Estándar")
            assert read_cfg_text(cfg_prof_path) in (None, "standard"), "Perfil no debió cambiar al entrar en pantalla completa"

            # 3. Esc NO sale de pantalla completa
            send_vk(hwnd_main, VK_ESCAPE)
            assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=True, context=f"{build_label} Esc no sale")

            # 4. Diálogo modal encima y dentro del monitor + F11 ignorado con modal abierto
            post_click_button(btn_checkout)
            hwnd_chk, _ = wait_for_window(proc.pid, "Cobro de Venta", timeout=6.0)
            time.sleep(0.25)
            chk_rc = get_rect(hwnd_chk)
            assert (
                chk_rc[0] >= mon_rc[0] and chk_rc[1] >= mon_rc[1]
                and chk_rc[2] <= mon_rc[2] and chk_rc[3] <= mon_rc[3]
            ), f"Diálogo Cobro fuera de monitor: chk={chk_rc}, mon={mon_rc}"
            # Verificar por encima: WindowFromPoint en el centro del diálogo devuelve el diálogo o hijo suyo
            cx_dlg, cy_dlg = (chk_rc[0] + chk_rc[2]) // 2, (chk_rc[1] + chk_rc[3]) // 2
            top_at_center = user32.WindowFromPoint(wintypes.POINT(cx_dlg, cy_dlg))
            assert top_at_center == hwnd_chk or user32.IsChild(hwnd_chk, top_at_center), (
                f"Diálogo Cobro no está encima en ({cx_dlg},{cy_dlg}): {top_at_center:#x} vs {hwnd_chk:#x}; "
                f"ventana encima: {describe_window_owner(top_at_center)}"
            )

            send_vk(hwnd_chk, VK_F11)
            user32.PostMessageW(hwnd_main, WM_KEYDOWN, VK_F11, 1)
            time.sleep(0.04)
            user32.PostMessageW(hwnd_main, WM_KEYUP, VK_F11, 0xC0000001)
            time.sleep(0.25)
            assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=True, context=f"{build_label} F11 con modal abierto")

            user32.PostMessageW(hwnd_chk, WM_CLOSE, 0, 0)
            wait_until(lambda: not find_windows_by_pid(proc.pid, "Cobro de Venta"), desc="cerrar Cobro")
            time.sleep(0.2)

            # 5. F11 otra vez -> vuelve al rectángulo exacto anterior y conserva carrito/sección/subsección/perfil
            send_vk(hwnd_main, VK_F11)
            wait_until(lambda: has_caption(hwnd_main), desc="F11 salir de pantalla completa")
            _, rect_after = assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=False, context=f"{build_label} F11 salir")
            assert rect_after == rect_before, f"Rectángulo restaurado no coincide: antes={rect_before}, después={rect_after}"

            con = sqlite3.connect(db_path)
            qty1 = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items").fetchone()[0]
            con.close()
            assert qty1 == 1, f"Carrito perdió partidas tras F11 ida y vuelta: {qty1}"
            pos_lists = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
            assert pos_lists and user32.SendMessageW(pos_lists[0], LVM_GETITEMCOUNT, 0, 0) == 1, "Lista visual del carrito no conservó la partida"
            print(
                f"[OK] {build_label} F11 ida/vuelta: antes={rect_before}, fs={rect_fs}, después={rect_after}, cfg={read_cfg_text(cfg_fs_path)!r}",
                flush=True,
            )

            # Restaurar maximizada al salir de pantalla completa
            user32.ShowWindow(hwnd_main, SW_MAXIMIZE)
            time.sleep(0.25)
            assert user32.IsZoomed(hwnd_main) != 0, "Ventana debió quedar maximizada antes de probar F11"
            send_vk(hwnd_main, VK_F11)
            wait_until(lambda: not has_caption(hwnd_main), desc="F11 desde maximizada")
            send_vk(hwnd_main, VK_F11)
            wait_until(lambda: has_caption(hwnd_main), desc="F11 volver a maximizada")
            assert user32.IsZoomed(hwnd_main) != 0, "No restauró estado maximizado tras F11"
            user32.ShowWindow(hwnd_main, SW_RESTORE)
            time.sleep(0.25)

            # 6. BackOffice > Configuración: opción "Pantalla completa", estado textual y sincronización F11 <-> botón
            click_sidebar_section(hwnd_main, "BackOffice")
            click_subnav(hwnd_main, "Configuración")
            time.sleep(0.3)

            vis_cfg = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            texts_cfg = [t for (_h, _c, t) in vis_cfg if t]
            assert any("Ocupa toda la pantalla y oculta la barra de Windows" in t for t in texts_cfg), (
                f"Falta texto explicativo de pantalla completa en Configuración: {texts_cfg}"
            )
            assert any("Desactivada (ventana normal)" in t for t in texts_cfg), "Falta estado inicial no dependiente de color"
            btn_fs = next((h for (h, c, t) in vis_cfg if c.lower() == "button" and t == "Usar pantalla completa"), None)
            assert btn_fs is not None, "Botón 'Usar pantalla completa' no encontrado en Configuración"

            # Activar desde Configuración
            click_button(btn_fs, settle=0.4)
            wait_until(lambda: not has_caption(hwnd_main), desc="Config activar pantalla completa")
            assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=True, context=f"{build_label} Botón Configuración")
            vis_cfg2 = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            assert any(c.lower() == "button" and t == "Salir de pantalla completa" for (_h, c, t) in vis_cfg2), (
                "Botón no actualizó texto a 'Salir de pantalla completa'"
            )
            assert any("Opción activa ahora" in t for (_h, c, t) in vis_cfg2 if t), "Falta estado textual activo"

            # Cambiar a Táctil mientras sigue en pantalla completa: verificar altura >= 48 y sin recortes
            btn_touch_opt = max(
                (h for (h, c, t) in vis_cfg2 if c.lower() == "button" and t == "Táctil"),
                key=lambda h: get_rect(h)[1],
            )
            click_button(btn_touch_opt, settle=0.5)
            assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=True, context=f"{build_label} Fullscreen Táctil")
            assert_nav_bars_fit(hwnd_main, dpi, scale, is_touch=True, context=f"{build_label} Fullscreen Táctil")

            vis_cfg_t = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            btn_fs_t = next((h for (h, c, t) in vis_cfg_t if c.lower() == "button" and "Salir de pantalla completa" in t), None)
            assert btn_fs_t is not None, "Botón 'Salir de pantalla completa' perdido en Táctil"
            fs_h = get_rect(btn_fs_t)[3] - get_rect(btn_fs_t)[1]
            assert fs_h >= min_px(48, scale), f"Botón pantalla completa en Táctil < 48 log: {fs_h}px ({px_to_logical(fs_h, scale):.1f} log)"

            # F11 en Configuración sale de pantalla completa y sincroniza el botón/estado
            send_vk(hwnd_main, VK_F11)
            wait_until(lambda: has_caption(hwnd_main), desc="F11 sincroniza botón Configuración")
            vis_cfg3 = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            assert any(c.lower() == "button" and t == "Usar pantalla completa" for (_h, c, t) in vis_cfg3), (
                f"F11 no sincronizó botón en Configuración: {[t for _, c, t in vis_cfg3 if c.lower() == 'button']}"
            )
            assert any("Desactivada (ventana normal)" in t for (_h, c, t) in vis_cfg3 if t), "F11 no sincronizó etiqueta de estado"

            # Volver a activar pantalla completa antes de cerrar para probar persistencia al reabrir
            btn_fs_on2 = next(h for (h, c, t) in vis_cfg3 if c.lower() == "button" and t == "Usar pantalla completa")
            click_button(btn_fs_on2, settle=0.4)
            assert_fullscreen_state(hwnd_main, cfg_fs_path, expect_full=True, context=f"{build_label} Antes de cerrar")

            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            proc.wait(timeout=8)

            # 7. Reabrir con station_fullscreen.cfg = fullscreen -> Login normal, MainWindow arranca en pantalla completa
            proc2 = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
            procs.append(proc2)
            hwnd_login2, _ = wait_for_window(proc2.pid, "Iniciar Sesión", timeout=10.0)
            assert has_caption(hwnd_login2), "Diálogo Iniciar Sesión debe abrir normal (no pantalla completa)"
            l2_edits = [h for (h, c, _t) in enum_children(hwnd_login2) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            l2_btn = next(h for (h, c, t) in enum_children(hwnd_login2) if c.lower() == "button" and user32.IsWindowVisible(h) and "Iniciar Sesión" in t)
            set_edit_text(l2_edits[0], "dueno_t5")
            set_edit_text(l2_edits[1], "CafeT5!2026")
            time.sleep(0.15)
            post_click_button(l2_btn)

            hwnd_main2, _ = wait_for_window(proc2.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
            time.sleep(0.35)
            assert_fullscreen_state(hwnd_main2, cfg_fs_path, expect_full=True, context=f"{build_label} Reapertura en fullscreen")

            # Quitar pantalla completa, cerrar y reabrir -> debe abrir en ventana normal
            send_vk(hwnd_main2, VK_F11)
            wait_until(lambda: has_caption(hwnd_main2), desc="Salir antes de segunda reapertura")
            assert_fullscreen_state(hwnd_main2, cfg_fs_path, expect_full=False, context=f"{build_label} Guardado windowed")
            user32.PostMessageW(hwnd_main2, WM_CLOSE, 0, 0)
            proc2.wait(timeout=8)

            proc3 = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
            procs.append(proc3)
            hwnd_login3, _ = wait_for_window(proc3.pid, "Iniciar Sesión", timeout=10.0)
            l3_edits = [h for (h, c, _t) in enum_children(hwnd_login3) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            l3_btn = next(h for (h, c, t) in enum_children(hwnd_login3) if c.lower() == "button" and user32.IsWindowVisible(h) and "Iniciar Sesión" in t)
            set_edit_text(l3_edits[0], "dueno_t5")
            set_edit_text(l3_edits[1], "CafeT5!2026")
            time.sleep(0.15)
            post_click_button(l3_btn)

            hwnd_main3, _ = wait_for_window(proc3.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
            time.sleep(0.3)
            assert_fullscreen_state(hwnd_main3, cfg_fs_path, expect_full=False, context=f"{build_label} Reapertura windowed")
            user32.PostMessageW(hwnd_main3, WM_CLOSE, 0, 0)
            proc3.wait(timeout=8)
            print(f"[OK] {build_label} Reaperturas fullscreen/windowed verificadas.", flush=True)

        finally:
            for p in procs:
                if p.poll() is None:
                    p.kill()
                    p.wait(timeout=3)


def main():
    dpi, _ = ensure_supported_system_dpi()
    print(f"[INFO] test_t5_fullscreen ejecutándose en {format_dpi_info(dpi)}")
    run_t5_suite(RELEASE_EXE, "Release")
    run_t5_suite(DEBUG_EXE, "Debug")
    print("\n[OK] TODAS LAS VERIFICACIONES DE T5 PASARON EN RELEASE Y DEBUG.")


if __name__ == "__main__":
    main()
