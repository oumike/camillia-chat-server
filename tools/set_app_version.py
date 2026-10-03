"""Injects CS_VERSION into the build from the VERSION file.

Same job as camillia-mt's tools/set_app_version.py: scripts/release.sh writes
VERSION before it builds, so the version the OLED shows is the version the
release was tagged with. The leading "v" is dropped because the display
prints "v" CS_VERSION itself.
"""

Import("env")  # noqa: F821  - injected by PlatformIO/SCons

import os

version_path = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")  # noqa: F821
try:
    with open(version_path, "r", encoding="utf-8") as fh:
        version = fh.read().strip()
except OSError:
    version = ""

if version.startswith("v"):
    version = version[1:]
if version:
    env.Append(CPPDEFINES=[("CS_VERSION", env.StringifyMacro(version))])  # noqa: F821
    print("[set_app_version] CS_VERSION=%s" % version)
else:
    # Not fatal: status_display.h falls back to a placeholder.
    print("[set_app_version] VERSION file not readable; CS_VERSION left at its default")
