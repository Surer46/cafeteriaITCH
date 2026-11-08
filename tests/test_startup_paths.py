"""Windows regression: launch the real app with Unicode environment/CLI paths.

Only inspects and closes windows belonging to the process started by this test.
Uses isolated data, creates no account and does not access development databases.
"""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tempfile
import time


def run(executable):
    executable = executable.resolve(strict=True)
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

    temp_parent = Path(tempfile.gettempdir()).resolve()
    root = Path(tempfile.mkdtemp(prefix="cafeteria_unicode_paths_", dir=temp_parent)).resolve()
    if root.parent != temp_parent:
        raise RuntimeError("Unexpected temporary directory; refusing cleanup")
    try:
        migrations = root / "Migraciones á ñ"
        shutil.copytree(executable.parent / "migrations", migrations)
        for mode in ("environment", "cli-equals", "cli-separate"):
            data = root / ("Cafetería QA ñ " + mode) / "datos"
            env = os.environ.copy()
            env["CAFETERIA_MIGRATIONS_DIR"] = str(migrations)
            env["CAFETERIA_DATA_DIR"] = str(data if mode == "environment" else root / "unused")
            args = [str(executable)]
            if mode == "cli-equals":
                args.append("--data-dir=" + str(data))
            elif mode == "cli-separate":
                args.extend(["--data-dir", str(data)])
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            process = subprocess.Popen(args, cwd=executable.parent, env=env, startupinfo=startup)
            windows = []

            def visit(hwnd, unused):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == process.pid:
                    title = ctypes.create_unicode_buffer(512)
                    user32.GetWindowTextW(hwnd, title, len(title))
                    if "Primer Inicio" in title.value:
                        windows.append(hwnd)
                return True

            callback = callback_type(visit)
            try:
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    user32.EnumWindows(callback, 0)
                    if windows or process.poll() is not None:
                        break
                    time.sleep(0.1)
                if not windows:
                    raise RuntimeError(f"{mode}: initial native dialog missing; exit={process.poll()}")
                user32.PostMessageW(windows[0], 0x0010, 0, 0)  # Cancel this process's dialog.
                process.wait(timeout=5)
                db = data / "cafeteria.db"
                if not db.is_file():
                    raise RuntimeError(f"{mode}: SQLite did not use the requested Unicode path")
                connection = sqlite3.connect(db)
                try:
                    if connection.execute("SELECT COUNT(*) FROM users").fetchone()[0] != 0:
                        raise RuntimeError(f"{mode}: cancellation unexpectedly created an account")
                    if connection.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                        raise RuntimeError(f"{mode}: invalid SQLite database")
                finally:
                    connection.close()
                if (root / "unused").exists():
                    raise RuntimeError(f"{mode}: command line did not override the environment")
                print(f"PASS {executable.parent.name}: {mode}, Unicode data and migration paths")
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
    finally:
        # The only recursive cleanup target is the verified, test-owned temp root.
        if root.parent == temp_parent and not root.is_symlink():
            shutil.rmtree(root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    run(parser.parse_args().executable)
