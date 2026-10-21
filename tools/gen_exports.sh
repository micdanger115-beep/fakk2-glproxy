#!/bin/sh
# Erzeugt src/gl_exports.inc aus der 32-Bit-MinGW-Importbibliothek fuer opengl32.
# Aufruf: tools/gen_exports.sh [/pfad/zu/libopengl32.a] > src/gl_exports.inc
set -eu
LIB="${1:-/usr/i686-w64-mingw32/lib/libopengl32.a}"
NM="${NM:-i686-w64-mingw32-nm}"
echo "/* Alle 368 Exporte der Windows-opengl32.dll (x86)."
echo " * Format: X(name, stdcall-Argumentbytes)"
echo " * Erzeugt mit tools/gen_exports.sh aus der MinGW-Importbibliothek libopengl32.a. */"
"$NM" "$LIB" | sed -n 's/^[0-9a-f]* T _\([A-Za-z0-9_]*\)@\([0-9]*\)$/X(\1, \2)/p' | LC_ALL=C sort -u
