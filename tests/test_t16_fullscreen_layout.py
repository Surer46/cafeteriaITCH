#!/usr/bin/env python3
"""
test_t16_fullscreen_layout.py - Prueba permanente de aprovechamiento de espacio en pantalla completa y ventana normal (T16d / I7).

Ejecuta build_release/app_desktop.exe con SQLite aislado, datos deterministas de ejemplo y
CAFETERIA_UPDATE_CHECK=0, recorriendo las 17 subsecciones reales en:
  - Pantalla completa (Estándar y Táctil)
  - Ventana normal 1200x720 y 960x600 (Estándar y Táctil)

Umbrales (sección 3 de Prompts/plan-2026-11-21-T16-neon.md — NO RELAJAR):
  - Cobertura horizontal >= 93.0 % (union_w_pct)
  - Cobertura vertical   >= 88.0 % (union_h_pct)
  - Franja vacía horizontal <= 5.0 % (max_h_gap_pct)
  - Franja vacía vertical   <= 8.0 % (max_v_gap_pct)
  - Sin barra vertical en pantalla completa (vscroll=NO)
  - Sin barra horizontal en ventana normal (hscroll=NO)
  - Sin texto cortado en wxStaticText visibles (texto_cortado=-)

Los umbrales son porcentajes y las alturas de texto se miden con la fuente real del control
mediante tests/gui_dpi.py, por lo que la prueba no depende del escalado 125 %.

REGLA PARA BLOQUES SIGUIENTES (T16b, T16c y posteriores):
  Cuando T16b o T16c terminen una pantalla, la pasan a estado "terminada" en SCREEN_STATES
  y quitan todas sus entradas de KNOWN_EXCEPTIONS. Nunca se relaja un umbral ni se agrega
  una excepción para una pantalla terminada.
"""

import os
import shutil
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gui_dpi import enable_per_monitor_dpi, ensure_supported_system_dpi, format_dpi_info
from fullscreen_layout_lib import (
    MAX_H_GAP_PCT,
    MAX_V_GAP_PCT,
    MIN_UNION_H_PCT,
    MIN_UNION_W_PCT,
    bootstrap_isolated_db,
    run_profile_pass,
    run_windowed_scroll_pass,
)


# ---------------------------------------------------------------------------
# Tabla de estado por pantalla (17 subsecciones)
# Estados permitidos: "terminada", "excepcion", "pendiente T16b", "pendiente T16c"
# ---------------------------------------------------------------------------
SCREEN_STATES = {
    # Terminadas en T16a
    "configuracion": {
        "estado": "terminada",
        "bloque": "T16a",
    },
    "respaldos": {
        "estado": "terminada",
        "bloque": "T16a",
    },
    # Excepción aprobada por el usuario (2026-11-21): Actualizaciones se muestra en dos
    # columnas ocupando toda la altura sin inventar contenido nuevo para rellenar el interior
    # vacío de las tarjetas; por ello no se comprueba la franja vertical (max_v_gap_pct).
    "actualizaciones": {
        "estado": "excepcion",
        "bloque": "T16a",
        "motivo": "Interior vacío de las dos tarjetas por decisión del usuario (2026-11-21); no se inventa contenido.",
    },
    # Pendientes T16b (BackOffice Roles y Usuarios)
    "usuarios": {"estado": "pendiente T16b", "bloque": "T16b"},
    "roles": {"estado": "pendiente T16b", "bloque": "T16b"},
    # Pendientes T16c (Inventario, POS, Caja y Reportes)
    "venta": {"estado": "pendiente T16c", "bloque": "T16c"},
    "existencias": {"estado": "pendiente T16c", "bloque": "T16c"},
    "reabastecimiento": {"estado": "pendiente T16c", "bloque": "T16c"},
    "productos_y_recetas": {"estado": "pendiente T16c", "bloque": "T16c"},
    "ingredientes": {"estado": "pendiente T16c", "bloque": "T16c"},
    "promociones": {"estado": "pendiente T16c", "bloque": "T16c"},
    "combos": {"estado": "pendiente T16c", "bloque": "T16c"},
    "barra": {"estado": "pendiente T16c", "bloque": "T16c"},
    "caja_corte": {"estado": "pendiente T16c", "bloque": "T16c"},
    "caja_historial": {"estado": "pendiente T16c", "bloque": "T16c"},
    "bitacora": {"estado": "pendiente T16c", "bloque": "T16c"},
    "reportes": {"estado": "pendiente T16c", "bloque": "T16c"},
}


# ---------------------------------------------------------------------------
# Excepciones conocidas SOLO para pantallas pendientes (T16b / T16c).
# Clave: (pantalla, perfil, tamano, metrica)
#   - tamano: "fullscreen", "1200x720" o "960x600"
#   - metrica: "union_w", "union_h", "gap_h", "gap_v", "vscroll", "hscroll", "texto_cortado"
# Prohibido agregar una pantalla con estado "terminada" a esta lista.
# ---------------------------------------------------------------------------
KNOWN_EXCEPTIONS = {
    # --- Pantalla completa ---
    ("productos_y_recetas", "Estandar", "fullscreen", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Falta registrar una compra..." recortada (48>25px, 2L)',
    },
    ("productos_y_recetas", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Detalle de receta en Táctil requiere desplazamiento vertical (costo/margen debajo)",
    },
    ("promociones", "Estandar", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Panel de Promociones muestra barra vertical en pantalla completa Estándar",
    },
    ("promociones", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Panel de Promociones muestra barra vertical en pantalla completa Táctil",
    },
    ("caja_historial", "Estandar", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Historial de cortes muestra barra vertical en pantalla completa Estándar",
    },
    ("caja_historial", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16c",
        "detalle": "Historial de cortes muestra barra vertical en pantalla completa Táctil",
    },
    ("roles", "Estandar", "fullscreen", "vscroll"): {
        "bloque": "T16b",
        "detalle": "Lista de permisos de Roles en 1 columna muestra barra vertical en Estándar",
    },
    ("roles", "Tactil", "fullscreen", "vscroll"): {
        "bloque": "T16b",
        "detalle": "Lista de permisos de Roles en 1 columna muestra barra vertical en Táctil",
    },
    # --- Ventana normal (1200x720 y 960x600) ---
    ("promociones", "Estandar", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Toca o haz clic en los prod..." recortada en 960x600 Estándar',
    },
    ("roles", "Estandar", "960x600", "texto_cortado"): {
        "bloque": "T16b",
        "detalle": 'Etiqueta "Rol de sistema: solo se pue..." recortada en 960x600 Estándar',
    },
    ("venta", "Tactil", "1200x720", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta de producto largo "Agua Embotellada Natural 60..." recortada en 1200x720 Táctil',
    },
    ("productos_y_recetas", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Categoría (elige una o escr..." recortada en 960x600 Táctil',
    },
    ("combos", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Categoría (elige una o escr..." recortada en 960x600 Táctil',
    },
    ("caja_corte", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Motivo de diferencia (oblig..." recortada en 960x600 Táctil',
    },
    ("bitacora", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16c",
        "detalle": 'Etiqueta "Registro cronológico inmuta..." recortada en 960x600 Táctil',
    },
    ("usuarios", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16b",
        "detalle": 'Etiqueta "Total: 3 cuentas" recortada en 960x600 Táctil',
    },
    ("roles", "Tactil", "960x600", "texto_cortado"): {
        "bloque": "T16b",
        "detalle": 'Etiqueta "Rol de sistema: solo se pue..." recortada en 960x600 Táctil',
    },
}


def validate_config_integrity():
    """Garantiza la regla permanente: ninguna pantalla terminada puede estar en KNOWN_EXCEPTIONS."""
    for key, info in KNOWN_EXCEPTIONS.items():
        screen = key[0]
        assert screen in SCREEN_STATES, f"Excepción conocida referencia pantalla inexistente: {screen}"
        st = SCREEN_STATES[screen]["estado"]
        assert st != "terminada", (
            f"Regla violada: la pantalla terminada '{screen}' no puede tener excepciones en KNOWN_EXCEPTIONS ({key}, {info})."
        )


def evaluate_checks(fs_rows, win_rows):
    """Evalúa todas las aserciones sobre las filas medidas y devuelve (failures, warnings, matched_known)."""
    failures = []
    warnings = []
    matched_known = set()

    def check_metric(screen, profile, size_label, metric, ok, fail_msg):
        key = (screen, profile, size_label, metric)
        known = KNOWN_EXCEPTIONS.get(key)
        if ok:
            if known is not None:
                matched_known.add(key)
                warnings.append(
                    f"[AVISO] Excepción conocida ({screen}, {profile}, {size_label}, {metric}, bloque={known['bloque']}) "
                    f"ya cumple, quitar de la lista."
                )
        else:
            if known is not None:
                matched_known.add(key)
            else:
                failures.append(fail_msg)

    # 1. Verificación en pantalla completa
    seen_fs = set()
    for m in fs_rows:
        screen = m["screen"]
        profile = m["profile"]
        seen_fs.add((screen, profile))
        assert screen in SCREEN_STATES, f"Pantalla no declarada en SCREEN_STATES: {screen}"
        estado = SCREEN_STATES[screen]["estado"]
        size_label = "fullscreen"

        check_metric(
            screen, profile, size_label, "union_w",
            m["union_w_pct"] >= MIN_UNION_W_PCT,
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=cobertura_ancho "
            f"medida={m['union_w_pct']:.1f}% umbral>={MIN_UNION_W_PCT:.1f}%",
        )
        check_metric(
            screen, profile, size_label, "union_h",
            m["union_h_pct"] >= MIN_UNION_H_PCT,
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=cobertura_alto "
            f"medida={m['union_h_pct']:.1f}% umbral>={MIN_UNION_H_PCT:.1f}%",
        )
        check_metric(
            screen, profile, size_label, "gap_h",
            m["max_h_gap_pct"] <= MAX_H_GAP_PCT,
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=franja_horizontal "
            f"medida={m['max_h_gap_pct']:.1f}% ({m['max_h_gap']}px) umbral<={MAX_H_GAP_PCT:.1f}%",
        )
        if estado != "excepcion":
            check_metric(
                screen, profile, size_label, "gap_v",
                m["max_v_gap_pct"] <= MAX_V_GAP_PCT,
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=franja_vertical "
                f"medida={m['max_v_gap_pct']:.1f}% ({m['max_v_gap']}px) umbral<={MAX_V_GAP_PCT:.1f}%",
            )
        check_metric(
            screen, profile, size_label, "vscroll",
            m["vscroll"] == "NO",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_vertical "
            f"medida=SI umbral=NO",
        )
        check_metric(
            screen, profile, size_label, "texto_cortado",
            m["texto_cortado"] == "-",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=texto_cortado "
            f"medida={m['texto_cortado']} umbral=-",
        )

    # 2. Verificación en ventana normal (1200x720 y 960x600)
    seen_win = set()
    for w in win_rows:
        screen = w["screen"]
        profile = w["profile"]
        size_label = w["size"]
        seen_win.add((screen, profile, size_label))
        assert screen in SCREEN_STATES, f"Pantalla no declarada en SCREEN_STATES: {screen}"

        check_metric(
            screen, profile, size_label, "hscroll",
            w["hscroll"] == "NO",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_horizontal "
            f"medida=SI umbral=NO",
        )
        if size_label == "toggle_fullscreen":
            check_metric(
                screen, profile, size_label, "vscroll",
                w["vscroll"] == "NO",
                f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=barra_vertical "
                f"medida=SI umbral=NO",
            )
        check_metric(
            screen, profile, size_label, "texto_cortado",
            w["texto_cortado"] == "-",
            f"[FALLO] pantalla={screen} perfil={profile} tamano={size_label} metrica=texto_cortado "
            f"medida={w['texto_cortado']} umbral=-",
        )

    # Verificar que se cubrieron las 17 pantallas en todos los modos
    expected_screens = set(SCREEN_STATES.keys())
    for prof in ("Estandar", "Tactil"):
        missing_fs = expected_screens - {s for (s, p) in seen_fs if p == prof}
        if missing_fs:
            failures.append(f"[FALLO] Faltaron pantallas en fullscreen [{prof}]: {sorted(missing_fs)}")
        for sz in ("1200x720", "960x600"):
            missing_w = expected_screens - {s for (s, p, z) in seen_win if p == prof and z == sz}
            if missing_w:
                failures.append(f"[FALLO] Faltaron pantallas en ventana {sz} [{prof}]: {sorted(missing_w)}")

    return failures, warnings, matched_known


def main():
    t0 = time.time()
    validate_config_integrity()
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    work_dir = os.path.join(repo_root, "build_release")
    exe_path = os.path.join(work_dir, "app_desktop.exe")
    assert os.path.exists(exe_path), f"No se encontró el ejecutable Release: {exe_path}"

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_t16_layout_")
    username = "admin_t16"
    password = "AdminT16Layout2026!"

    try:
        bootstrap_isolated_db(exe_path, work_dir, temp_dir, username, password)

        rows_std, mon_std = run_profile_pass(
            exe_path, work_dir, temp_dir, None, "standard", "Estandar", username, password, verbose=False
        )
        rows_tch, _mon_tch = run_profile_pass(
            exe_path, work_dir, temp_dir, None, "touch", "Tactil", username, password, verbose=False
        )
        win_std = run_windowed_scroll_pass(
            exe_path, work_dir, temp_dir, "standard", "Estandar", username, password,
            verbose=False, check_dynamic_fullscreen_toggle=True,
        )
        win_tch = run_windowed_scroll_pass(
            exe_path, work_dir, temp_dir, "touch", "Tactil", username, password,
            verbose=False, check_dynamic_fullscreen_toggle=True,
        )

        mon_w, mon_h, dpi, _scale = mon_std
        print(
            f"=== T16d PRUEBA PERMANENTE DE PANTALLA COMPLETA Y VENTANA NORMAL "
            f"(Monitor: {mon_w}x{mon_h} px | {format_dpi_info(dpi)}) ===",
            flush=True,
        )
        print(
            f"Pantallas medidas: {len(rows_std) + len(rows_tch)} fullscreen (17x2) + "
            f"{len(win_std) + len(win_tch)} ventana (17x2x2).",
            flush=True,
        )

        failures, warnings, matched_known = evaluate_checks(rows_std + rows_tch, win_std + win_tch)

        for w_msg in warnings:
            print(w_msg, flush=True)

        elapsed = time.time() - t0
        if failures:
            print(f"\n[ERROR] Se detectaron {len(failures)} fallo(s) de layout:", flush=True)
            for f_msg in failures:
                print(f"  {f_msg}", flush=True)
            print(f"\n=== PRUEBA T16d FALLIDA en {elapsed:.1f}s ===", flush=True)
            sys.exit(1)

        print(
            f"[OK] Todas las pantallas terminadas ({sum(1 for v in SCREEN_STATES.values() if v['estado'] == 'terminada')}), "
            f"excepción aprobada (1) e invariantes vigentes de pendientes pasaron "
            f"(excepciones conocidas activas={len(matched_known) - len(warnings)}, avisos={len(warnings)}).",
            flush=True,
        )
        print(f"=== PRUEBA T16d EXITOSA en {elapsed:.1f}s ===", flush=True)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
