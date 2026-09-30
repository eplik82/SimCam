# PlatformIO eel-skript: määrab SIMCAM_VERSION
#   1) keskkonnamuutuja SIMCAM_VERSION (GitHub Actions: sildi nimi, nt v1.2.3)
#   2) `git describe --tags` (kohalik ehitus)
#   3) "0.0.0-dev"
# FOTA võrdleb seda GitHubi viimase release'i versiooniga.
import os
import re
import subprocess

Import("env")  # noqa: F821  (PlatformIO SCons)

v = os.environ.get("SIMCAM_VERSION", "").strip()
if not v:
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--dirty"],
            cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL).decode().strip()  # noqa: F821
    except Exception:
        v = ""
v = v[1:] if v[:1] in ("v", "V") else v
if not re.match(r"^\d+\.\d+\.\d+", v):
    v = "0.0.0-dev"

env.Append(CPPDEFINES=[("SIMCAM_VERSION", '\\"%s\\"' % v)])  # noqa: F821
print("SimCam püsivara versioon:", v)
