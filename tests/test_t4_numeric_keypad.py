import os
import sys
import time
import ctypes
import sqlite3
import tempfile
import subprocess
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui_dpi import (
    REPO_ROOT,
    RELEASE_EXE,
    DEBUG_EXE,
    user32,
    WM_CLOSE,
    WM_KEYDOWN,
    WM_KEYUP,
    LVM_GETITEMCOUNT,
    min_px,
    to_px,
    px_to_logical,
    ensure_no_app_running,
    ensure_supported_system_dpi,
    format_dpi_info,
    get_window_scale,
    get_rect,
    get_client_screen_rect,
    set_window_logical_size,
    find_windows_by_pid,
    wait_for_window,
    enum_children,
    set_edit_text,
    click_button,
    post_click_button,
    click_control_at,
    rects_overlap,
    is_descendant,
    click_sidebar_section,
    click_subnav,
    get_text,
    get_class,
    send_msg,
    select_list_row,
)

KEYPAD_DIGITS = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"}
KEYPAD_ACTIONS = {".", "Borrar", "Limpiar", "Listo"}
KEYPAD_ALL = KEYPAD_DIGITS | KEYPAD_ACTIONS

MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
KEYEVENTF_KEYUP = 0x0002
VK_BACK = 0x08
VK_DOWN = 0x28
VK_UP = 0x26
VK_RETURN = 0x0D
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E


def get_edit_text(hwnd_edit):
    ln = int(send_msg(hwnd_edit, WM_GETTEXTLENGTH, 0, 0))
    if ln <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(ln + 2)
    send_msg(hwnd_edit, WM_GETTEXT, ln + 2, ctypes.addressof(buf))
    return buf.value


class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


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


user32.WindowFromPoint.argtypes = [POINT]
user32.WindowFromPoint.restype = wintypes.HWND
user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
user32.SetCursorPos.restype = wintypes.BOOL
user32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_ulonglong]
user32.keybd_event.argtypes = [wintypes.BYTE, wintypes.BYTE, wintypes.DWORD, ctypes.c_ulonglong]
user32.GetGUIThreadInfo.argtypes = [wintypes.DWORD, ctypes.POINTER(GUITHREADINFO)]
user32.GetGUIThreadInfo.restype = wintypes.BOOL


user32.GetForegroundWindow.argtypes = []
user32.GetForegroundWindow.restype = wintypes.HWND
user32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
user32.AttachThreadInput.restype = wintypes.BOOL
user32.BringWindowToTop.argtypes = [wintypes.HWND]
user32.BringWindowToTop.restype = wintypes.BOOL
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.SetWindowPos.restype = wintypes.BOOL
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.GetCurrentThreadId.argtypes = []
kernel32.GetCurrentThreadId.restype = wintypes.DWORD


def get_gui_focus(hwnd_top):
    tid = user32.GetWindowThreadProcessId(hwnd_top, None)
    gti = GUITHREADINFO()
    gti.cbSize = ctypes.sizeof(GUITHREADINFO)
    if not user32.GetGUIThreadInfo(tid, ctypes.byref(gti)):
        return 0
    return int(gti.hwndFocus or 0)


def bring_foreground(hwnd_top):
    if user32.GetForegroundWindow() == hwnd_top:
        return
    fg = user32.GetForegroundWindow()
    fg_tid = user32.GetWindowThreadProcessId(fg, None) if fg else 0
    cur_tid = kernel32.GetCurrentThreadId()
    tgt_tid = user32.GetWindowThreadProcessId(hwnd_top, None)
    attached_fg = bool(fg_tid and fg_tid != cur_tid and user32.AttachThreadInput(cur_tid, fg_tid, True))
    attached_tgt = bool(tgt_tid and tgt_tid != cur_tid and user32.AttachThreadInput(cur_tid, tgt_tid, True))
    try:
        user32.ShowWindow(hwnd_top, 9)
        user32.SetWindowPos(hwnd_top, wintypes.HWND(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
        user32.SetWindowPos(hwnd_top, wintypes.HWND(-2), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
        user32.BringWindowToTop(hwnd_top)
        user32.SetForegroundWindow(hwnd_top)
    finally:
        if attached_tgt:
            user32.AttachThreadInput(cur_tid, tgt_tid, False)
        if attached_fg:
            user32.AttachThreadInput(cur_tid, fg_tid, False)
    time.sleep(0.06)


def real_mouse_click_point(x, y, hwnd_top, settle=0.14):
    bring_foreground(hwnd_top)
    if user32.GetForegroundWindow() == hwnd_top:
        user32.SetCursorPos(int(x), int(y))
        time.sleep(0.03)
        user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
        time.sleep(0.03)
        user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)
        time.sleep(settle)
        return
    # Si la sesion de escritorio no permite robo de foco (fg=0), enrutar al HWND real bajo (x,y)
    target = user32.WindowFromPoint(POINT(int(x), int(y)))
    assert target, f"WindowFromPoint({x},{y}) devolvio nulo"
    rc = get_rect(target)
    lx = max(1, int(x) - rc[0])
    ly = max(1, int(y) - rc[1])
    lparam = (ly << 16) | (lx & 0xFFFF)
    user32.PostMessageW(target, 0x0201, 1, lparam)  # WM_LBUTTONDOWN
    time.sleep(0.04)
    user32.PostMessageW(target, 0x0202, 0, lparam)  # WM_LBUTTONUP
    time.sleep(settle)


def real_mouse_click_ctrl(hwnd_ctrl, hwnd_top, settle=0.14):
    l, t, r, b = get_rect(hwnd_ctrl)
    cx, cy = (l + r) // 2, (t + b) // 2
    bring_foreground(hwnd_top)
    if user32.GetForegroundWindow() == hwnd_top:
        real_mouse_click_point(cx, cy, hwnd_top, settle=settle)
        return
    hit = user32.WindowFromPoint(POINT(cx, cy))
    assert hit == hwnd_ctrl or is_descendant(hit, hwnd_ctrl), (
        f"Control {hwnd_ctrl:#x} bloqueado en ({cx},{cy}) por {hit or 0:#x}"
    )
    if get_class(hwnd_ctrl).lower() == "button":
        # Primero WM_LBUTTONDOWN para ejercitar OutsideClickFilter; sin captura activa en fg=0, BM_CLICK ejecuta el clic del botón una sola vez.
        lparam = ((max(1, (b - t) // 2)) << 16) | (max(1, (r - l) // 2) & 0xFFFF)
        user32.PostMessageW(hwnd_ctrl, 0x0201, 1, lparam)
        time.sleep(0.03)
        post_click_button(hwnd_ctrl)
    else:
        click_control_at(hwnd_ctrl)
    time.sleep(settle)


def post_key_char(hwnd_top, vk_code, settle=0.12):
    bring_foreground(hwnd_top)
    if user32.GetForegroundWindow() == hwnd_top:
        user32.keybd_event(vk_code, 0, 0, 0)
        time.sleep(0.03)
        user32.keybd_event(vk_code, 0, KEYEVENTF_KEYUP, 0)
        time.sleep(settle)
        return
    target = get_gui_focus(hwnd_top) or hwnd_top
    if 0x30 <= vk_code <= 0x39 or vk_code == VK_BACK:
        # wxMSW procesa TranslateMessage sobre WM_KEYDOWN y genera un unico WM_CHAR
        ch = 0x08 if vk_code == VK_BACK else vk_code
        user32.PostMessageW(target, 0x0102, ch, 1)  # WM_CHAR unico
    else:
        user32.PostMessageW(target, WM_KEYDOWN, vk_code, 1)
        time.sleep(0.03)
        user32.PostMessageW(target, WM_KEYUP, vk_code, 0xC0000001)
    time.sleep(settle)


def get_visible_keypad_buttons(hwnd_root):
    out = {}
    for h, c, t in enum_children(hwnd_root):
        if c.lower() == "button" and user32.IsWindowVisible(h) and t in KEYPAD_ALL:
            out[t] = h
    return out


def is_keypad_visible(hwnd_root):
    btns = get_visible_keypad_buttons(hwnd_root)
    return len(btns) > 0


def assert_no_keypad_visible(hwnd_root, context):
    btns = get_visible_keypad_buttons(hwnd_root)
    assert not btns, f"[{context}] No debía mostrarse teclado numérico, botones visibles: {list(btns.keys())}"


def assert_keypad_geometry(hwnd_root, hwnd_top, hwnd_field, forbidden_hwnds, scale, allow_decimal, context):
    btns = get_visible_keypad_buttons(hwnd_root)
    expected_keys = KEYPAD_DIGITS | {"Borrar", "Limpiar", "Listo"}
    if allow_decimal:
        expected_keys = expected_keys | {"."}
    if set(btns.keys()) != expected_keys:
        all_kp = [(h, c, t, user32.IsWindowVisible(h), get_rect(h)) for (h, c, t) in enum_children(hwnd_root) if t in KEYPAD_ALL]
        fg = user32.GetForegroundWindow() or 0
        fr = get_rect(hwnd_field)
        wfp = user32.WindowFromPoint(POINT((fr[0] + fr[2]) // 2, (fr[1] + fr[3]) // 2)) or 0
        print(f"[DEBUG][{context}] fg={fg:#x} top={hwnd_top:#x} focus={get_gui_focus(hwnd_top):#x} field={hwnd_field:#x} rect={fr} wfp={wfp:#x} all_kp={all_kp}", flush=True)
    assert set(btns.keys()) == expected_keys, (
        f"[{context}] Teclas visibles={sorted(btns.keys())}, esperadas={sorted(expected_keys)}"
    )

    parents = {user32.GetParent(h) for h in btns.values()}
    assert len(parents) == 1, f"[{context}] Teclas en distintos padres: {parents}"
    kp_hwnd = next(iter(parents))
    kp_rect = get_rect(kp_hwnd)

    client_rc = get_client_screen_rect(hwnd_root)
    margin_tol = to_px(1, scale)
    assert (
        kp_rect[0] >= client_rc[0] - margin_tol
        and kp_rect[1] >= client_rc[1] - margin_tol
        and kp_rect[2] <= client_rc[2] + margin_tol
        and kp_rect[3] <= client_rc[3] + margin_tol
    ), f"[{context}] Teclado fuera del cliente: kp={kp_rect}, client={client_rc}"

    min_k = min_px(64, scale)
    min_gap = to_px(8, scale) - 1  # tolerancia de 1 px por redondeo DPI
    key_rects = {}
    min_w_log, min_h_log = 999.0, 999.0

    bring_foreground(hwnd_top)
    for label, bh in btns.items():
        r = get_rect(bh)
        w, h = r[2] - r[0], r[3] - r[1]
        assert w >= min_k and h >= min_k, (
            f"[{context}] Tecla '{label}' menor a 64x64 lógicos: {w}x{h}px ({px_to_logical(w, scale):.1f}x{px_to_logical(h, scale):.1f} log)"
        )
        min_w_log = min(min_w_log, px_to_logical(w, scale))
        min_h_log = min(min_h_log, px_to_logical(h, scale))
        key_rects[label] = r

        # Ajuste b: WindowFromPoint en el centro exacto de cada tecla debe devolver la propia tecla (o un descendiente)
        cx, cy = (r[0] + r[2]) // 2, (r[1] + r[3]) // 2
        hit = user32.WindowFromPoint(POINT(cx, cy))
        assert hit == bh or is_descendant(hit, bh), (
            f"[{context}] WindowFromPoint({cx},{cy}) para tecla '{label}' ({bh:#x}) devolvió {hit:#x} clase='{get_text(hit)}'"
        )

    # Verificar separación >= 8 lógicos entre teclas en misma fila / columna (sin solapes)
    items = list(key_rects.items())
    for i in range(len(items)):
        la, ra = items[i]
        for j in range(i + 1, len(items)):
            lb, rb = items[j]
            assert not rects_overlap(ra, rb), f"[{context}] Teclas '{la}' y '{lb}' se solapan: {ra} vs {rb}"
            v_overlap = min(ra[3], rb[3]) - max(ra[1], rb[1])
            h_overlap = min(ra[2], rb[2]) - max(ra[0], rb[0])
            if v_overlap > 4:
                dx = max(rb[0] - ra[2], ra[0] - rb[2])
                assert dx >= min_gap, f"[{context}] Separación horizontal < 8 log entre '{la}' y '{lb}': {dx}px"
            if h_overlap > 4:
                dy = max(rb[1] - ra[3], ra[1] - rb[3])
                assert dy >= min_gap, f"[{context}] Separación vertical < 8 log entre '{la}' y '{lb}': {dy}px"

    # Verificar que no tapa el campo activo ni los controles críticos indicados
    f_rect = get_rect(hwnd_field)
    assert not rects_overlap(kp_rect, f_rect), f"[{context}] Teclado tapa campo activo: kp={kp_rect}, field={f_rect}"
    for desc, fhwnd in forbidden_hwnds:
        if fhwnd and user32.IsWindowVisible(fhwnd):
            fr = get_rect(fhwnd)
            if rects_overlap(kp_rect, fr):
                print(f"[DEBUG-OVERLAP][{context}] client={client_rc} field={f_rect} kp={kp_rect} {desc}={fr}", flush=True)
            assert not rects_overlap(kp_rect, fr), (
                f"[{context}] Teclado tapa '{desc}': kp={kp_rect}, '{desc}'={fr}"
            )

    rel_x = px_to_logical(kp_rect[0] - client_rc[0], scale)
    rel_y = px_to_logical(kp_rect[1] - client_rc[1], scale)
    kp_w = px_to_logical(kp_rect[2] - kp_rect[0], scale)
    kp_h = px_to_logical(kp_rect[3] - kp_rect[1], scale)
    layout_tag = "compacta 5x3" if kp_h < 270 else "4x4"
    print(
        f"  [METRICS][{context}] modo={layout_tag} kp=({rel_x:.0f},{rel_y:.0f},{kp_w:.0f}x{kp_h:.0f}) log, "
        f"tecla_min=({min_w_log:.1f}x{min_h_log:.1f}) log, client={px_to_logical(client_rc[2]-client_rc[0], scale):.0f}x{px_to_logical(client_rc[3]-client_rc[1], scale):.0f} log",
        flush=True,
    )
    return kp_hwnd, btns


def enter_number_via_keypad(hwnd_root, hwnd_top, seq):
    for ch in seq:
        btns = get_visible_keypad_buttons(hwnd_root)
        assert ch in btns, f"Tecla '{ch}' no visible en teclado: {list(btns.keys())}"
        real_mouse_click_ctrl(btns[ch], hwnd_top, settle=0.09)


def dismiss_messagebox(pid, title_sub, btn_text_pref="Aceptar"):
    hwnd_msg, _ = wait_for_window(pid, title_sub, timeout=6.0)
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_msg) if c.lower() == "button" and user32.IsWindowVisible(h)]
    chosen = next((h for (h, t) in btns if btn_text_pref.lower() in t.lower()), None)
    if chosen is None and btns:
        chosen = btns[0][0]
    assert chosen is not None, f"Sin botón en diálogo '{title_sub}': {btns}"
    post_click_button(chosen)
    time.sleep(0.35)


def locate_pos_controls(hwnd_main):
    vis = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
    edits = sorted([(h, get_rect(h)) for (h, c, _t) in vis if c.lower() == "edit"], key=lambda x: (x[1][1], x[1][0]))
    # PosPanel crea txt_search_, txt_qty_, txt_notes_ en orden
    btns = [(h, t) for (h, c, t) in vis if c.lower() == "button"]
    statics = [(h, t) for (h, c, t) in vis if c.lower() == "static"]
    btn_add = next((h for (h, t) in btns if "Agregar al Carrito" in t), None)
    btn_edit_qty = next((h for (h, t) in btns if "editar cantidad" in t.lower()), None)
    btn_checkout = next((h for (h, t) in btns if "Cobrar" in t), None)
    btn_cancel = next((h for (h, t) in btns if "Cancelar" in t and "Orden" in t), None)
    btn_remove = next((h for (h, t) in btns if "Quitar" in t), None)
    assert btn_add and btn_edit_qty and btn_checkout and btn_remove, (
        f"Faltan botones POS: add={btn_add}, edit_qty={btn_edit_qty}, checkout={btn_checkout}, remove={btn_remove}, visibles={[t for _, t in btns]}"
    )
    lbl_total = next((h for (h, t) in statics if "TOTAL:" in t or t.startswith("$")), None)
    # txt_qty_ es siempre el campo más angosto en POS (~60-90 log, vs búsqueda y notas anchos).
    assert len(edits) >= 2, f"Se esperaban campos Edit en POS, hallados: {edits}"
    txt_qty = min(edits, key=lambda x: x[1][2] - x[1][0])[0]
    return {
        "txt_search": edits[0][0],
        "txt_qty": txt_qty,
        "btn_add": btn_add,
        "btn_edit_qty": btn_edit_qty,
        "btn_checkout": btn_checkout,
        "btn_cancel": btn_cancel,
        "btn_remove": btn_remove,
        "lbl_total": lbl_total,
    }


def switch_profile_via_settings(hwnd_main, want_touch):
    click_sidebar_section(hwnd_main, "BackOffice")
    click_subnav(hwnd_main, "Configuración")
    time.sleep(0.3)
    target = "Táctil" if want_touch else "Estándar"
    btns = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    hdr_btns = [h for (h, t) in btns if t in ("Touch", "Estándar")]
    hdr_bottom = get_rect(min(hdr_btns, key=lambda h: get_rect(h)[1]))[3] if hdr_btns else 0
    matches = sorted(
        [(h, t) for (h, t) in btns if t == target and get_rect(h)[1] >= hdr_bottom],
        key=lambda x: get_rect(x[0])[1],
    )
    if not matches:
        matches = sorted([(h, t) for (h, t) in btns if target in t and get_rect(h)[1] >= hdr_bottom], key=lambda x: get_rect(x[0])[1])
    assert matches, f"Botón de perfil '{target}' no encontrado en Configuración: {btns}"
    post_click_button(matches[-1][0])
    time.sleep(0.6)
    expected_hdr = "Estándar" if want_touch else "Touch"
    btns_after = [(h, t) for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h)]
    assert any(t == expected_hdr for (_h, t) in btns_after), (
        f"No cambió al perfil want_touch={want_touch} (esperado header '{expected_hdr}'), botones={[t for _, t in btns_after]}"
    )


def run_t4_suite(exe_path, build_label):
    assert os.path.exists(exe_path), f"No existe ejecutable {build_label}: {exe_path}"
    print(f"\n=== T4 Teclado Numérico Integrado ({build_label}) ===", flush=True)
    ensure_no_app_running()

    with tempfile.TemporaryDirectory(prefix=f"cafeteria_t4_{build_label.lower()}_") as tmp_dir:
        db_path = os.path.join(tmp_dir, "cafeteria.db")
        env = os.environ.copy()
        env["CAFETERIA_DATA_DIR"] = tmp_dir
        env["CAFETERIA_UPDATE_CHECK"] = "0"
        procs = []
        try:
            proc = subprocess.Popen([exe_path], cwd=REPO_ROOT, env=env)
            procs.append(proc)

            # 1. Primer inicio: crear Dueño (Nombre, Usuario, Correo, Contraseña, Confirmar)
            hwnd_first, _ = wait_for_window(proc.pid, "Primer Inicio", timeout=10.0)
            edits_f = [h for (h, c, _t) in enum_children(hwnd_first) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            btns_f = [(h, t) for (h, c, t) in enum_children(hwnd_first) if c.lower() == "button" and user32.IsWindowVisible(h)]
            assert len(edits_f) >= 5 and btns_f, f"Controles faltantes en Primer Inicio: edits={len(edits_f)}"
            set_edit_text(edits_f[0], "Dueño T4 Prueba")
            set_edit_text(edits_f[1], "dueno_t4")
            set_edit_text(edits_f[2], "dueno_t4@itch.edu.mx")
            set_edit_text(edits_f[3], "CafeT4!2026")
            set_edit_text(edits_f[4], "CafeT4!2026")
            time.sleep(0.15)
            post_click_button(btns_f[0][0])

            hwnd_main, _ = wait_for_window(proc.pid, "Cafetería ITCH II", exclude=("Primer Inicio", "Iniciar Sesión"), timeout=15.0)
            scale = get_window_scale(hwnd_main)
            set_window_logical_size(hwnd_main, 1200, 720)
            time.sleep(0.25)

            # Sembrar producto (< $45.50 para probar cambio exacto con $45.50)
            con = sqlite3.connect(db_path)
            con.execute(
                """
                INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
                VALUES ('prod-t4-01', 'T4-01', '750100009944', 'Cafe Americano T4', 'Bebidas', 1000, 0, 1, '2026-11-14 10:00:00')
                """
            )
            con.execute(
                """
                INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
                VALUES ('prod-t4-65', 'ZZ-65', '750100009965', 'ZZ Combo 65 T4', 'Alimentos', 6500, 0, 1, '2026-11-14 10:00:00')
                """
            )
            con.commit()
            con.close()

            # Recargar Venta navegando fuera y volviendo (inicia en Perfil Estándar)
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)

            # 2. En Perfil Estándar: ningún campo debe abrir teclado numérico y Editar Cantidad sigue nativo
            cash_edits_std = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            assert cash_edits_std, "txt_initial_cash_ no visible en Caja y turnos"
            real_mouse_click_ctrl(cash_edits_std[0], hwnd_main)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Estándar Caja inicial")

            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Ingredientes")
            time.sleep(0.25)
            btn_new_std = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Nuevo ingrediente")
            click_button(btn_new_std)
            time.sleep(0.2)
            ing_edits_std = sorted(
                [(h, get_rect(h)) for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)],
                key=lambda x: x[1][1]
            )
            real_mouse_click_ctrl(ing_edits_std[1][0], hwnd_main)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Estándar Ingredientes umbral")

            click_sidebar_section(hwnd_main, "Punto de Venta")
            time.sleep(0.3)
            pos_std = locate_pos_controls(hwnd_main)
            real_mouse_click_ctrl(pos_std["txt_qty"], hwnd_main)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Estándar POS cantidad")
            click_button(pos_std["btn_add"])
            time.sleep(0.3)

            # Verificar Editar Cantidad nativo (sin teclado) en Estándar
            pos_lists_std = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
            assert pos_lists_std and send_msg(pos_lists_std[0], LVM_GETITEMCOUNT, 0, 0) >= 1, "Carrito vacío en Estándar"
            select_list_row(pos_lists_std[0], 0, settle=0.25)
            post_click_button(pos_std["btn_edit_qty"])
            hwnd_eq_std, _ = wait_for_window(proc.pid, "Editar Cantidad", timeout=6.0)
            time.sleep(0.2)
            assert_no_keypad_visible(hwnd_eq_std, f"{build_label} Estándar Editar Cantidad")
            user32.PostMessageW(hwnd_eq_std, WM_CLOSE, 0, 0)
            time.sleep(0.25)

            # Quitar la línea de prueba del carrito para iniciar limpio en Táctil
            pos_lists_std = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
            if pos_lists_std and send_msg(pos_lists_std[0], LVM_GETITEMCOUNT, 0, 0) > 0:
                click_button(pos_std["btn_remove"])
                time.sleep(0.25)
            print(f"[OK] {build_label} Estándar: cero teclados en Caja inicial, Ingredientes, POS cantidad y Editar Cantidad nativo.")

            # 3. Cambiar a perfil Táctil desde BackOffice > Configuración
            switch_profile_via_settings(hwnd_main, True)

            # 4. Caja y turnos (Táctil): abrir turno con teclado ($250.00 -> 25000 centavos)
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)
            cash_edits = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            btn_open_shift = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno" in t)
            real_mouse_click_ctrl(cash_edits[0], hwnd_main)
            assert_keypad_geometry(
                hwnd_main, hwnd_main, cash_edits[0],
                [("Abrir Turno", btn_open_shift)],
                scale, allow_decimal=True, context=f"{build_label} Caja Abrir 1200x720"
            )
            enter_number_via_keypad(hwnd_main, hwnd_main, ["Limpiar", "2", "5", "0", ".", "0", "0"])
            assert get_edit_text(cash_edits[0]) == "250.00", f"Texto caja inicial='{get_edit_text(cash_edits[0])}'"
            assert get_gui_focus(hwnd_main) == cash_edits[0], "Foco se perdió tras pulsar teclas en Caja"

            # Probar en 960x600 también en Caja inicial
            set_window_logical_size(hwnd_main, 960, 600)
            time.sleep(0.25)
            assert_keypad_geometry(
                hwnd_main, hwnd_main, cash_edits[0],
                [("Abrir Turno", btn_open_shift)],
                scale, allow_decimal=True, context=f"{build_label} Caja Abrir 960x600"
            )
            set_window_logical_size(hwnd_main, 1200, 720)
            time.sleep(0.25)

            # Ajuste e: cambio de subsección y sección con teclado abierto -> cero huérfanos
            click_subnav(hwnd_main, "Bitácora")
            time.sleep(0.2)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Tras cambio de subsección")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.2)
            cash_edits = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            real_mouse_click_ctrl(cash_edits[0], hwnd_main)
            assert is_keypad_visible(hwnd_main), "Teclado debió abrirse en Caja antes de cambiar sección"
            click_sidebar_section(hwnd_main, "BackOffice")
            time.sleep(0.2)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Tras cambio de sección")

            # Volver a Caja y abrir el turno escribiendo 250.00 vía teclado numérico
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)
            cash_edits = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            btn_open_shift = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno" in t)
            assert get_edit_text(cash_edits[0]) == "300.00", f"Fondo inicial esperado '300.00', obtenido '{get_edit_text(cash_edits[0])}'"
            real_mouse_click_ctrl(cash_edits[0], hwnd_main)
            # Requisito T5-0 (calculadora): sin Limpiar, 300.00 + tocar 5, 0, 0 = 500
            enter_number_via_keypad(hwnd_main, hwnd_main, ["5", "0", "0"])
            assert get_edit_text(cash_edits[0]) == "500", f"Calculadora fondo inicial: esperado '500', obtenido '{get_edit_text(cash_edits[0])}'"
            enter_number_via_keypad(hwnd_main, hwnd_main, ["Limpiar", "2", "5", "0", ".", "0", "0"])
            # Pulsar Abrir Turno directamente (1 solo clic con teclado abierto - Ajuste c)
            real_mouse_click_ctrl(btn_open_shift, hwnd_main, settle=0.2)
            dismiss_messagebox(proc.pid, "Turno Abierto")
            con = sqlite3.connect(db_path)
            shift_row = con.execute("SELECT initial_cash_cents, is_open FROM shifts ORDER BY opened_at DESC LIMIT 1").fetchone()
            con.close()
            assert shift_row == (25000, 1), f"Turno no persistió 25000 abierto en SQLite: {shift_row}"
            print(f"[OK] {build_label} Caja inicial con teclado -> SQLite {shift_row}.")

            # 5. Punto de Venta (Táctil): cantidad, teclado físico paralelo, foco, clic fuera, 1 clic Cobrar
            click_sidebar_section(hwnd_main, "Punto de Venta")
            time.sleep(0.35)
            pos = locate_pos_controls(hwnd_main)
            real_mouse_click_ctrl(pos["txt_qty"], hwnd_main)
            forbidden_pos = [
                ("TOTAL", pos["lbl_total"]),
                ("Cobrar", pos["btn_checkout"]),
                ("Cancelar Orden", pos["btn_cancel"]),
            ]
            assert_keypad_geometry(
                hwnd_main, hwnd_main, pos["txt_qty"], forbidden_pos,
                scale, allow_decimal=False, context=f"{build_label} POS 1200x720"
            )
            # Requisito T5-0 (calculadora): sin Limpiar, cantidad 1 + tocar 3 = 3; teclado fisico igual
            enter_number_via_keypad(hwnd_main, hwnd_main, ["3"])
            assert get_edit_text(pos["txt_qty"]) == "3", f"Calculadora POS sin Limpiar: esperado '3', obtenido '{get_edit_text(pos['txt_qty'])}'"
            real_mouse_click_ctrl(pos["txt_qty"], hwnd_main)
            post_key_char(hwnd_main, ord("7"))
            post_key_char(hwnd_main, ord("8"))
            assert get_edit_text(pos["txt_qty"]) == "78", f"Calculadora POS teclado fisico: esperado '78', obtenido '{get_edit_text(pos['txt_qty'])}'"
            enter_number_via_keypad(hwnd_main, hwnd_main, ["Limpiar", "3", "5", "Borrar"])
            assert get_edit_text(pos["txt_qty"]) == "3", f"Cantidad esperada '3', obtenida '{get_edit_text(pos['txt_qty'])}'"
            assert get_gui_focus(hwnd_main) == pos["txt_qty"], "Foco debe seguir en txt_qty_ tras pulsar botones"

            # Teclado físico sigue funcionando con keypad abierto
            post_key_char(hwnd_main, ord("4"))
            assert get_edit_text(pos["txt_qty"]) == "34", f"Teclado físico esperado '34', obtenido '{get_edit_text(pos['txt_qty'])}'"
            post_key_char(hwnd_main, VK_BACK)
            assert get_edit_text(pos["txt_qty"]) == "3", f"Backspace físico esperado '3', obtenido '{get_edit_text(pos['txt_qty'])}'"

            # Listo oculta teclado y mantiene foco en el campo
            btns_kp = get_visible_keypad_buttons(hwnd_main)
            real_mouse_click_ctrl(btns_kp["Listo"], hwnd_main)
            assert_no_keypad_visible(hwnd_main, f"{build_label} POS tras Listo")
            assert get_gui_focus(hwnd_main) == pos["txt_qty"], "Foco debe volver a txt_qty_ tras pulsar Listo"

            # Agregar al carrito -> cantidad 3 en SQLite ($30.00)
            click_button(pos["btn_add"])
            time.sleep(0.35)
            con = sqlite3.connect(db_path)
            qty_db = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items").fetchone()[0]
            con.close()
            assert qty_db == 3, f"SQLite cart_items esperado 3, obtenido {qty_db}"

            # Probar Editar Cantidad en Táctil: título, prompt, tamaños (>=48 campo, >=56 botones, >=64 teclado)
            pos_lists_t = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
            assert pos_lists_t and send_msg(pos_lists_t[0], LVM_GETITEMCOUNT, 0, 0) >= 1, "Carrito vacío en Táctil"
            select_list_row(pos_lists_t[0], 0, settle=0.25)
            post_click_button(pos["btn_edit_qty"])
            hwnd_eq, _ = wait_for_window(proc.pid, "Editar Cantidad", timeout=6.0)
            time.sleep(0.25)
            eq_children = enum_children(hwnd_eq)
            eq_statics = [t for (h, c, t) in eq_children if c.lower() == "static" and user32.IsWindowVisible(h)]
            assert any("Ingrese la nueva cantidad para" in t for t in eq_statics), f"Mensaje esperado no hallado: {eq_statics}"
            eq_edits = [h for (h, c, _t) in eq_children if c.lower() == "edit" and user32.IsWindowVisible(h)]
            assert len(eq_edits) == 1, f"Se esperaba 1 Edit en Editar Cantidad: {eq_edits}"
            eq_er = get_rect(eq_edits[0])
            assert (eq_er[3] - eq_er[1]) >= min_px(18, scale), f"Edit interno de Editar Cantidad demasiado bajo: {eq_er}"
            # Contenedor externo del edit (make_text_field) debe medir >= 48 log
            eq_wrap_r = get_rect(user32.GetParent(eq_edits[0]))
            assert (eq_wrap_r[3] - eq_wrap_r[1]) >= min_px(48, scale), f"Campo Editar Cantidad < 48 log: {eq_wrap_r}"
            btn_eq_cancel = next(h for (h, c, t) in eq_children if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Cancelar")
            btn_eq_ok = next(h for (h, c, t) in eq_children if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Aceptar")
            for bname, bh in (("Cancelar", btn_eq_cancel), ("Aceptar", btn_eq_ok)):
                br = get_rect(bh)
                assert (br[3] - br[1]) >= min_px(56, scale), f"Botón '{bname}' en Editar Cantidad < 56 log: {br}"
            assert_keypad_geometry(
                hwnd_eq, hwnd_eq, eq_edits[0],
                [("Cancelar", btn_eq_cancel), ("Aceptar", btn_eq_ok)],
                scale, allow_decimal=False, context=f"{build_label} Editar Cantidad Touch"
            )
            enter_number_via_keypad(hwnd_eq, hwnd_eq, ["Limpiar", "2"])
            assert get_edit_text(eq_edits[0]) == "2", f"Edit cantidad esperado '2', obtenido '{get_edit_text(eq_edits[0])}'"
            real_mouse_click_ctrl(btn_eq_ok, hwnd_eq, settle=0.35)
            con = sqlite3.connect(db_path)
            qty_db2 = con.execute("SELECT COALESCE(SUM(quantity), 0) FROM cart_items").fetchone()[0]
            con.close()
            assert qty_db2 == 2, f"SQLite cart_items tras Editar Cantidad esperado 2, obtenido {qty_db2}"
            print(f"[OK] {build_label} POS Editar Cantidad Táctil actualizado a 2.")

            # Probar POS en 960x600 Táctil + clic fuera cierra + 1 solo clic en Cobrar
            set_window_logical_size(hwnd_main, 960, 600)
            time.sleep(0.3)
            pos = locate_pos_controls(hwnd_main)
            real_mouse_click_ctrl(pos["txt_qty"], hwnd_main)
            assert_keypad_geometry(
                hwnd_main, hwnd_main, pos["txt_qty"],
                [("TOTAL", pos["lbl_total"]), ("Cobrar", pos["btn_checkout"]), ("Cancelar Orden", pos["btn_cancel"])],
                scale, allow_decimal=False, context=f"{build_label} POS 960x600"
            )
            # Clic en área neutra (zona superior vacía de ventana, x=client_left+20, y=client_top+20 en fondo/encabezado)
            cli_rc = get_client_screen_rect(hwnd_main)
            real_mouse_click_point(cli_rc[0] + to_px(180, scale), cli_rc[1] + to_px(12, scale), hwnd_main)
            assert_no_keypad_visible(hwnd_main, f"{build_label} POS tras clic fuera")

            # Reabrir teclado y hacer UN SOLO clic real sobre Cobrar -> debe abrir Cobro de Venta inmediatamente
            real_mouse_click_ctrl(pos["txt_qty"], hwnd_main)
            assert is_keypad_visible(hwnd_main), "Teclado POS debió reabrirse"
            real_mouse_click_ctrl(pos["btn_checkout"], hwnd_main, settle=0.25)

            # 6. Diálogo Cobro de Venta (Ajuste a: teclado visible desde apertura, cabe en 960x600, no cambia tamaño)
            hwnd_chk, _ = wait_for_window(proc.pid, "Cobro de Venta", timeout=6.0)
            time.sleep(0.25)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Teclado POS debió cerrarse al abrir Cobro")
            chk_rc0 = get_rect(hwnd_chk)
            chk_w_log = px_to_logical(chk_rc0[2] - chk_rc0[0], scale)
            chk_h_log = px_to_logical(chk_rc0[3] - chk_rc0[1], scale)
            assert chk_w_log <= 960.5 and chk_h_log <= 600.5, (
                f"[{build_label}] Diálogo Cobro excede 960x600: {chk_w_log:.1f}x{chk_h_log:.1f} log"
            )
            chk_children = enum_children(hwnd_chk)
            chk_edits = [h for (h, c, _t) in chk_children if c.lower() == "edit" and user32.IsWindowVisible(h)]
            btn_chk_confirm = next(h for (h, c, t) in chk_children if c.lower() == "button" and user32.IsWindowVisible(h) and "Confirmar Venta" in t)
            btn_chk_cancel = next(h for (h, c, t) in chk_children if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Cancelar")
            chk_statics = [(h, t) for (h, c, t) in chk_children if c.lower() == "static" and user32.IsWindowVisible(h)]
            lbl_change = next(h for (h, t) in chk_statics if t.startswith("$") and get_rect(h)[1] > get_rect(chk_edits[0])[1])
            assert is_keypad_visible(hwnd_chk), f"[{build_label}] Teclado en Cobro debía verse desde apertura en Efectivo"
            assert_keypad_geometry(
                hwnd_chk, hwnd_chk, chk_edits[0],
                [("Confirmar Venta", btn_chk_confirm), ("Cancelar", btn_chk_cancel), ("Cambio", lbl_change)],
                scale, allow_decimal=True, context=f"{build_label} Cobro 960x600_fit"
            )

            # Escribir 45.50 con botones (y segundo '.' ignorado); total es $20.00 (2x$10) -> cambio $25.50
            enter_number_via_keypad(hwnd_chk, hwnd_chk, ["Limpiar", "4", "5", ".", "5", ".", "0"])
            assert get_edit_text(chk_edits[0]) == "45.50", f"Monto recibido esperado '45.50', obtenido '{get_edit_text(chk_edits[0])}'"
            assert get_text(lbl_change) == "$25.50", f"Cambio esperado '$25.50', obtenido '{get_text(lbl_change)}'"

            # Cambiar método a Tarjeta y Transferencia -> teclado desaparece sin cambiar tamaño ni mover diálogo
            # TouchComboBox está encima del campo efectivo, con altura ~48 log
            edit_r = get_rect(chk_edits[0])
            combo_candidates = []
            for (h, c, _t) in chk_children:
                if not user32.IsWindowVisible(h) or c.lower() in ("button", "static", "edit"):
                    continue
                rc = get_rect(h)
                if rc[3] <= edit_r[1] and (edit_r[0] - to_px(16, scale)) <= rc[0] and rc[2] <= (edit_r[2] + to_px(16, scale)):
                    if min_px(32, scale) <= (rc[3] - rc[1]) <= to_px(68, scale) and (rc[2] - rc[0]) >= min_px(100, scale):
                        combo_candidates.append(h)
            assert combo_candidates, "No se encontró TouchComboBox de método de pago en Cobro"
            combo_h = sorted(combo_candidates, key=lambda h: get_rect(h)[1])[-1]

            def pick_payment_method(idx):
                known = {w for (w, _t) in find_windows_by_pid(proc.pid)}
                crc = get_rect(combo_h)
                cw = crc[2] - crc[0]
                ch = crc[3] - crc[1]
                lp_open = ((ch // 2) << 16) | (max(to_px(4, scale), cw - to_px(12, scale)) & 0xFFFF)
                user32.PostMessageW(combo_h, 0x0201, 1, lp_open)
                time.sleep(0.05)
                user32.PostMessageW(combo_h, 0x0202, 0, lp_open)
                popup_h = None
                t0 = time.time()
                while time.time() - t0 < 3.0 and popup_h is None:
                    for (w, _t) in find_windows_by_pid(proc.pid):
                        if w not in known:
                            popup_h = w
                            break
                    if popup_h is None:
                        time.sleep(0.05)
                assert popup_h is not None, "No se abrió desplegable de método de pago"
                time.sleep(0.22)
                p_children = enum_children(popup_h)
                target = next((h for (h, _c, _t) in p_children if user32.IsWindowVisible(h)), popup_h)
                trc = get_client_screen_rect(target)
                tw = max(10, trc[2] - trc[0])
                th = max(30, trc[3] - trc[1])
                px = tw // 2
                py = int((idx + 0.5) * th / 3.0)
                lparam = (py << 16) | (px & 0xFFFF)
                user32.PostMessageW(target, 0x0200, 0, lparam)  # WM_MOUSEMOVE para actualizar hover en wxVListBox
                time.sleep(0.06)
                user32.PostMessageW(target, 0x0201, 1, lparam)  # WM_LBUTTONDOWN
                time.sleep(0.06)
                user32.PostMessageW(target, 0x0202, 0, lparam)  # WM_LBUTTONUP
                t_close = time.time()
                while time.time() - t_close < 2.0 and user32.IsWindowVisible(popup_h):
                    time.sleep(0.05)
                time.sleep(0.18)

            pick_payment_method(1)  # Tarjeta
            assert not is_keypad_visible(hwnd_chk), "Teclado debió ocultarse al cambiar a Tarjeta"
            assert get_rect(hwnd_chk) == chk_rc0, f"Cobro cambió tamaño/posición en Tarjeta: {chk_rc0} -> {get_rect(hwnd_chk)}"

            pick_payment_method(2)  # Transferencia
            assert not is_keypad_visible(hwnd_chk), "Teclado debió ocultarse al cambiar a Transferencia"
            assert get_rect(hwnd_chk) == chk_rc0, f"Cobro cambió tamaño/posición en Transferencia: {chk_rc0} -> {get_rect(hwnd_chk)}"

            pick_payment_method(0)  # Efectivo
            assert is_keypad_visible(hwnd_chk), "Teclado debió reaparecer al volver a Efectivo"
            assert get_rect(hwnd_chk) == chk_rc0, f"Cobro cambió tamaño/posición al volver a Efectivo: {chk_rc0} -> {get_rect(hwnd_chk)}"

            # Confirmar venta
            real_mouse_click_ctrl(btn_chk_confirm, hwnd_chk, settle=0.25)
            dismiss_messagebox(proc.pid, "Venta Registrada", btn_text_pref="Cerrar")
            print(f"[OK] {build_label} Cobro verificado: tamaño estable {chk_w_log:.0f}x{chk_h_log:.0f}, cambio $25.50, Tarjeta/Transferencia sin cambio de rect.")

            # Requisito T5-0 (calculadora en cobro): total $65.00 + tocar 7, 0 = 70 SIN pulsar Limpiar
            pos_65 = locate_pos_controls(hwnd_main)
            set_edit_text(pos_65["txt_search"], "ZZ-65")
            btn_search_65 = next(
                h for (h, c, t) in enum_children(hwnd_main)
                if c.lower() == "button" and user32.IsWindowVisible(h) and t.strip() == "Buscar"
            )
            click_button(btn_search_65)
            time.sleep(0.25)
            click_button(pos_65["btn_add"])
            time.sleep(0.3)
            post_click_button(pos_65["btn_checkout"])
            hwnd_chk65, _ = wait_for_window(proc.pid, "Cobro de Venta", timeout=6.0)
            time.sleep(0.25)
            chk65_edits = [h for (h, c, _t) in enum_children(hwnd_chk65) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            assert get_edit_text(chk65_edits[0]) == "$65.00", f"Cobro inicial esperado '$65.00', obtenido '{get_edit_text(chk65_edits[0])}'"
            enter_number_via_keypad(hwnd_chk65, hwnd_chk65, ["7", "0"])
            assert get_edit_text(chk65_edits[0]) == "70", f"Calculadora cobro $65.00 sin Limpiar: esperado '70', obtenido '{get_edit_text(chk65_edits[0])}'"
            # Re-tocar campo -> selecciona todo -> teclado fisico reemplaza "70" por "85"
            real_mouse_click_ctrl(chk65_edits[0], hwnd_chk65)
            post_key_char(hwnd_chk65, ord("8"))
            post_key_char(hwnd_chk65, ord("5"))
            assert get_edit_text(chk65_edits[0]) == "85", f"Calculadora cobro teclado fisico: esperado '85', obtenido '{get_edit_text(chk65_edits[0])}'"
            user32.PostMessageW(hwnd_chk65, WM_CLOSE, 0, 0)
            time.sleep(0.25)
            if pos_65["btn_cancel"]:
                post_click_button(pos_65["btn_cancel"])
                dismiss_messagebox(proc.pid, "Cancelar", btn_text_pref="Sí")
            else:
                pos_lists_65 = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "syslistview32" and user32.IsWindowVisible(h)]
                if pos_lists_65:
                    select_list_row(pos_lists_65[0], 0)
                post_click_button(pos_65["btn_remove"])
                time.sleep(0.25)
            # Restaurar catalogo completo antes del resto del flujo
            set_edit_text(pos_65["txt_search"], "")
            click_button(btn_search_65)
            time.sleep(0.2)

            # Restaurar 1200x720
            set_window_logical_size(hwnd_main, 1200, 720)
            time.sleep(0.25)

            # 7. Caja y turnos (abierto -> cerrar turno con txt_final_cash_)
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)
            btn_close_shift = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Cerrar Turno" in t)
            co_scroll = user32.GetParent(btn_close_shift)
            cash_edits_open = sorted(
                [h for (h, c, _t) in enum_children(co_scroll) if c.lower() == "edit" and user32.IsWindowVisible(h)],
                key=lambda h: get_rect(h)[1],
            )
            real_mouse_click_ctrl(cash_edits_open[0], hwnd_main)
            assert_keypad_geometry(
                hwnd_main, hwnd_main, cash_edits_open[0],
                [("Cerrar Turno", btn_close_shift)],
                scale, allow_decimal=True, context=f"{build_label} Caja Final 1200x720"
            )
            enter_number_via_keypad(hwnd_main, hwnd_main, ["Limpiar", "2", "7", "0", ".", "0", "0"])
            assert get_edit_text(cash_edits_open[0]) == "270.00"
            if len(cash_edits_open) >= 2:
                set_edit_text(cash_edits_open[1], "Diferencia de prueba T4")
            real_mouse_click_ctrl(btn_close_shift, hwnd_main, settle=0.2)
            dismiss_messagebox(proc.pid, "Confirmar Cierre de Turno", btn_text_pref="Sí")
            dismiss_messagebox(proc.pid, "Turno Cerrado")
            con = sqlite3.connect(db_path)
            closed_row = con.execute("SELECT final_cash_cents, is_open FROM shifts ORDER BY opened_at DESC LIMIT 1").fetchone()
            con.close()
            assert closed_row == (27000, 0), f"Turno no cerró con 27000 en SQLite: {closed_row}"
            print(f"[OK] {build_label} Caja cierre con teclado -> SQLite {closed_row}.")

            # 8. Inventario > Ingredientes (Ajuste d: evitar nombre, existencia actual, botón Guardar y tarjeta derecha)
            click_sidebar_section(hwnd_main, "Inventario")
            click_subnav(hwnd_main, "Ingredientes")
            time.sleep(0.3)
            vis_ing = enum_children(hwnd_main)
            btn_new_ing = next(h for (h, c, t) in vis_ing if c.lower() == "button" and user32.IsWindowVisible(h) and t == "Nuevo ingrediente")
            click_button(btn_new_ing)
            time.sleep(0.25)

            vis_ing = [(h, c, t) for (h, c, t) in enum_children(hwnd_main) if user32.IsWindowVisible(h)]
            ing_edits = sorted([(h, get_rect(h)) for (h, c, _t) in vis_ing if c.lower() == "edit"], key=lambda x: x[1][1])
            txt_ing_name, txt_ing_thresh = ing_edits[0][0], ing_edits[1][0]
            btn_ing_save = next(h for (h, c, t) in vis_ing if c.lower() == "button" and t == "Guardar")
            lbl_ing_stock = next((h for (h, c, t) in vis_ing if c.lower() == "static" and "Inicia en 0" in t), None)
            set_edit_text(txt_ing_name, "Azucar T4")

            real_mouse_click_ctrl(txt_ing_thresh, hwnd_main)
            forbidden_ing = [
                ("Nombre ingrediente", txt_ing_name),
                ("Existencia actual", lbl_ing_stock),
                ("Guardar", btn_ing_save),
            ]
            assert_keypad_geometry(
                hwnd_main, hwnd_main, txt_ing_thresh, forbidden_ing,
                scale, allow_decimal=False, context=f"{build_label} Ingredientes 1200x720"
            )
            set_window_logical_size(hwnd_main, 960, 600)
            time.sleep(0.25)
            assert_keypad_geometry(
                hwnd_main, hwnd_main, txt_ing_thresh, forbidden_ing,
                scale, allow_decimal=False, context=f"{build_label} Ingredientes 960x600"
            )
            enter_number_via_keypad(hwnd_main, hwnd_main, ["Limpiar", "5", "0", "0"])
            assert get_edit_text(txt_ing_thresh) == "500"
            real_mouse_click_ctrl(btn_ing_save, hwnd_main, settle=0.3)
            con = sqlite3.connect(db_path)
            ing_row = con.execute(
                "SELECT name, unit_dimension, base_unit, low_stock_threshold FROM ingredients WHERE name='Azucar T4'"
            ).fetchone()
            con.close()
            assert ing_row == ("Azucar T4", "MASS", "mg", 500000), f"Ingrediente inesperado en SQLite: {ing_row}"
            print(f"[OK] {build_label} Ingredientes umbral 500 con teclado -> SQLite {ing_row}.")

            # 9. Ajuste e: cambiar perfil con teclado abierto -> cero huérfanos y sin teclado en Estándar
            real_mouse_click_ctrl(txt_ing_thresh, hwnd_main)
            assert is_keypad_visible(hwnd_main), "Teclado debió abrirse antes del cambio de perfil"
            toggle_btn = next(
                (h for (h, c, t) in enum_children(hwnd_main)
                 if c.lower() == "button" and user32.IsWindowVisible(h) and ("Estándar" in t or "Touch" in t or "Táctil" in t)),
                None
            )
            assert toggle_btn is not None, "Botón de cambio rápido de perfil no encontrado"
            real_mouse_click_ctrl(toggle_btn, hwnd_main, settle=0.45)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Tras cambiar perfil Táctil->Estándar")
            print(f"[OK] {build_label} Cambio de perfil con teclado abierto sin huérfanos.")

            # 10. Verificar en Estándar que arqueo final y Cobro de Venta tampoco muestran teclado
            click_sidebar_section(hwnd_main, "Caja y Reportes")
            click_subnav(hwnd_main, "Caja y turnos")
            time.sleep(0.25)
            btn_open_std = next(h for (h, c, t) in enum_children(hwnd_main) if c.lower() == "button" and user32.IsWindowVisible(h) and "Abrir Turno" in t)
            post_click_button(btn_open_std)
            dismiss_messagebox(proc.pid, "Turno Abierto")
            cash_edits_open_std = [h for (h, c, _t) in enum_children(hwnd_main) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            real_mouse_click_ctrl(cash_edits_open_std[0], hwnd_main)
            assert_no_keypad_visible(hwnd_main, f"{build_label} Estándar Caja final")

            click_sidebar_section(hwnd_main, "Punto de Venta")
            time.sleep(0.3)
            pos_std2 = locate_pos_controls(hwnd_main)
            click_button(pos_std2["btn_add"])
            time.sleep(0.25)
            post_click_button(pos_std2["btn_checkout"])
            hwnd_chk_std, _ = wait_for_window(proc.pid, "Cobro de Venta", timeout=6.0)
            time.sleep(0.2)
            chk_edits_std = [h for (h, c, _t) in enum_children(hwnd_chk_std) if c.lower() == "edit" and user32.IsWindowVisible(h)]
            real_mouse_click_ctrl(chk_edits_std[0], hwnd_chk_std)
            assert_no_keypad_visible(hwnd_chk_std, f"{build_label} Estándar Cobro")
            user32.PostMessageW(hwnd_chk_std, WM_CLOSE, 0, 0)
            time.sleep(0.2)
            print(f"[OK] {build_label} Estándar: cero teclados en Caja final y Cobro de Venta.")

            user32.PostMessageW(hwnd_main, WM_CLOSE, 0, 0)
            proc.wait(timeout=6)
        finally:
            for p in procs:
                if p.poll() is None:
                    p.kill()
                    p.wait(timeout=3)


def main():
    dpi, _ = ensure_supported_system_dpi()
    print(f"[INFO] test_t4_numeric_keypad ejecutándose en {format_dpi_info(dpi)}")
    run_t4_suite(RELEASE_EXE, "Release")
    run_t4_suite(DEBUG_EXE, "Debug")
    print("\n[OK] TODAS LAS VERIFICACIONES DE T4 PASARON EN RELEASE Y DEBUG.")


if __name__ == "__main__":
    main()
