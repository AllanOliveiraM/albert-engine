#!/usr/bin/env python3
"""Register an Ubuntu/Linux launcher for this executable, without installing it."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--executable", type=Path, default=Path(__file__).resolve().parents[1] / "build/linux/bin/AlbertEngine")
parser.add_argument("--data-home", type=Path, help="Override XDG_DATA_HOME (also useful for isolated validation)")
args = parser.parse_args()
if sys.platform != "linux":
    parser.error("Linux launcher registration only")
executable = args.executable.resolve()
icon = executable.parent / "assets/branding/albert.png"
if not executable.is_file() or not icon.is_file():
    parser.error("Compile primeiro: make render-build")
data = args.data_home or Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
if not data.is_absolute():
    parser.error("O diretorio de dados precisa ser absoluto")
if any(c in str(executable) for c in "\n\r\0"):
    parser.error("Invalid executable path")
# Desktop Entry Exec quoting differs from both JSON and shell escaping.
quoted = '"' + str(executable).replace('\\', '\\\\\\\\').replace('"', '\\\\"').replace('`', '\\\\`').replace('$', '\\\\$').replace('%', '%%') + '"'
destination = data / "icons/hicolor/512x512/apps/albert-engine.png"
destination.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(icon, destination)
applications = data / "applications"
applications.mkdir(parents=True, exist_ok=True)
entry = applications / "albert-engine.desktop"
entry.write_text(f"""[Desktop Entry]
Version=1.0
Type=Application
Name=Albert Engine
Comment=Explore o Sistema Solar
# env also avoids GIO checking a literal %% executable name before expanding it.
Exec=/usr/bin/env {quoted}
Icon=albert-engine
Terminal=false
StartupNotify=true
StartupWMClass=Albert Engine
Categories=Education;Astronomy;
""", encoding="utf-8")
entry.chmod(0o644)
for command in (("update-desktop-database", str(applications)), ("gtk-update-icon-cache", "-f", "-t", str(data / "icons/hicolor"))):
    if shutil.which(command[0]):
        subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
print(f"Atalho registrado: {entry}\nExecutavel mantido em: {executable}\nReabra o Albert; se estava fixado na barra, fixe novamente.")
