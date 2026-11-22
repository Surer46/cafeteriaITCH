#!/usr/bin/env python3
"""
measure_fullscreen_space.py - Medición objetiva de aprovechamiento de espacio en pantalla completa y ventana normal (T16 / I7).

Ejecuta build_release/app_desktop.exe con SQLite aislado y datos deterministas, en perfil Estándar
y perfil Táctil, recorriendo las 17 subsecciones reales. Guarda capturas en qa_visual/fullscreen/
y escribe qa_visual/fullscreen/medidas.txt con una fila por pantalla y perfil.
"""

import os
import shutil
import sys
import tempfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gui_dpi import enable_per_monitor_dpi, ensure_supported_system_dpi, format_dpi_info
from fullscreen_layout_lib import (
    bootstrap_isolated_db,
    run_profile_pass,
    run_windowed_scroll_pass,
)


def main():
    enable_per_monitor_dpi()
    sys_dpi, _ = ensure_supported_system_dpi()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    work_dir = os.path.join(repo_root, "build_release")
    exe_path = os.path.join(work_dir, "app_desktop.exe")
    out_dir = os.path.join(repo_root, "qa_visual", "fullscreen")
    os.makedirs(out_dir, exist_ok=True)
    assert os.path.exists(exe_path), f"No se encontró {exe_path}"

    temp_dir = tempfile.mkdtemp(prefix="cafeteria_fs_measure_")
    username = "admin_fs"
    password = "AdminFsMeasure2026!"

    print(f"=== MEDICIÓN DE PANTALLA COMPLETA ({format_dpi_info(sys_dpi)}) ===", flush=True)
    try:
        bootstrap_isolated_db(exe_path, work_dir, temp_dir, username, password)

        rows_std, mon_std = run_profile_pass(exe_path, work_dir, temp_dir, out_dir, "standard", "Estandar", username, password)
        rows_tch, _mon_tch = run_profile_pass(exe_path, work_dir, temp_dir, out_dir, "touch", "Tactil", username, password)

        print("\n=== PASADA INFORMATIVA EN VENTANA NORMAL (1200x720 y 960x600 — 17 subsecciones) ===", flush=True)
        win_std = run_windowed_scroll_pass(exe_path, work_dir, temp_dir, "standard", "Estandar", username, password)
        win_tch = run_windowed_scroll_pass(exe_path, work_dir, temp_dir, "touch", "Tactil", username, password)

        mon_w, mon_h, dpi, _scale = mon_std
        medidas_path = os.path.join(out_dir, "medidas.txt")
        header = (
            f"# Medidas pantalla completa — Monitor: {mon_w}x{mon_h} px | {format_dpi_info(dpi)}\n"
            f"# Columnas: perfil | pantalla | cumple | vscroll | content_px | union_px | union_w% | union_h% | union_area% | "
            f"max_gap_h_px(pct) | max_gap_v_px(pct) | pil_fg% | pil_bg_h_px | pil_bg_v_px | listas_tablas | texto_cortado\n"
        )
        lines = [header]
        for m in rows_std + rows_tch:
            lines.append(
                f"{m['profile']:8s} | {m['screen']:20s} | {m['cumple']:2s} | {m['vscroll']:2s} | "
                f"{m['content_w']:4d}x{m['content_h']:<4d} | "
                f"{m['union_w']:4d}x{m['union_h']:<4d} | "
                f"{m['union_w_pct']:5.1f}% | {m['union_h_pct']:5.1f}% | {m['union_area_pct']:5.1f}% | "
                f"{m['max_h_gap']:4d}px({m['max_h_gap_pct']:4.1f}%) | "
                f"{m['max_v_gap']:4d}px({m['max_v_gap_pct']:4.1f}%) | "
                f"{m['pil_non_bg_pct']:5.1f}% | {m['pil_h_bg']:4d}px | {m['pil_v_bg']:4d}px | "
                f"{m['lists']} | {m['texto_cortado']}\n"
            )
        lines.append("\n# Ventana normal (1200x720 y 960x600) — perfil | tamano | pantalla | hscroll | vscroll | texto_cortado\n")
        for w in win_std + win_tch:
            lines.append(
                f"{w['profile']:8s} | {w['size']:8s} | {w['screen']:20s} | "
                f"hscroll={w['hscroll']:2s} | vscroll={w['vscroll']:2s} | texto_cortado={w['texto_cortado']}\n"
            )
        with open(medidas_path, "w", encoding="utf-8") as f:
            f.writelines(lines)
        print(f"\n[OK] Medidas escritas en {medidas_path} ({len(rows_std) + len(rows_tch)} fullscreen + {len(win_std) + len(win_tch)} ventana).", flush=True)
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
