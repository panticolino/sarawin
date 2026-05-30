#!/usr/bin/env bash
# package-windows.sh — empaqueta SARA Libre en una carpeta autocontenida para Windows.
# Uso:  bash package-windows.sh <build_dir> <out_dir>
# Pensado para correr DENTRO del shell MSYS2/MINGW64 (lo usa el workflow de CI),
# pero también sirve en una instalación local de MSYS2.
set -euo pipefail

BUILD_DIR="${1:-build}"
OUT_DIR="${2:-dist-win}"
MINGW="/mingw64"

EXE="$BUILD_DIR/saralibre.exe"
if [ ! -f "$EXE" ]; then
  echo "ERROR: no encuentro $EXE (¿se compiló?)." >&2
  exit 1
fi

echo ">> Limpiando $OUT_DIR"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR" "$OUT_DIR/gstreamer-1.0"

echo ">> Copiando el ejecutable"
cp "$EXE" "$OUT_DIR/"

# ── Función: copiar recursivamente las DLLs de /mingw64 de las que depende un binario ──
copy_deps() {
  local target="$1"
  ntldd -R "$target" 2>/dev/null \
    | grep -oiE '[a-z]:\\[^ ]*mingw64[^ ]*\.dll|/mingw64/[^ ]*\.dll' \
    | tr '\\' '/' \
    | sed -E 's#^[A-Za-z]:/#/#' \
    | sort -u \
    | while read -r dll; do
        # normalizar a ruta unix de msys
        local p="$dll"
        [ -f "$p" ] || p="$MINGW/bin/$(basename "$dll")"
        if [ -f "$p" ]; then
          cp -n "$p" "$OUT_DIR/" 2>/dev/null || true
        fi
      done
}

echo ">> Resolviendo DLLs del ejecutable"
copy_deps "$EXE"

# ── Plugins de Qt (platforms/qwindows.dll, sqldrivers/qsqlite.dll, styles, imageformats) ──
echo ">> windeployqt (plugins de Qt6)"
WINDEPLOY="$(command -v windeployqt6 || command -v windeployqt-qt6 || command -v windeployqt || true)"
if [ -z "$WINDEPLOY" ]; then
  echo "ERROR: no encuentro windeployqt6 (instalá mingw-w64-x86_64-qt6-tools)." >&2
  exit 1
fi
"$WINDEPLOY" --release --compiler-runtime --no-translations \
  --dir "$OUT_DIR" "$OUT_DIR/saralibre.exe"

# windeployqt agrega DLLs de Microsoft (d3dcompiler_47.dll, opengl32sw.dll)
# que están compiladas con el runtime de Visual Studio. En una compilación
# MinGW eso provoca el cartel "no se encuentra el punto de entrada
# __std_parallel_algorithms_hw_threads ... d3dcompiler_47.dll". Una app Qt
# Widgets con MinGW no las necesita, así que las quitamos.
echo ">> Quitando DLLs de Microsoft incompatibles (d3dcompiler/opengl32sw)"
rm -f "$OUT_DIR/d3dcompiler_47.dll" "$OUT_DIR/opengl32sw.dll"

# ── Plugins de GStreamer + su scanner ──
echo ">> Copiando plugins de GStreamer"
cp -r "$MINGW/lib/gstreamer-1.0/." "$OUT_DIR/gstreamer-1.0/"
# el scanner vive en distintos lugares según versión; lo buscamos
for cand in \
  "$MINGW/bin/gst-plugin-scanner.exe" \
  "$MINGW/lib/gstreamer-1.0/gst-plugin-scanner.exe"; do
  [ -f "$cand" ] && cp "$cand" "$OUT_DIR/gstreamer-1.0/"
done

# Los plugins de GStreamer arrastran sus propias DLLs (libav, codecs, etc.):
echo ">> Resolviendo DLLs de cada plugin de GStreamer"
shopt -s nullglob
for plugin in "$OUT_DIR"/gstreamer-1.0/*.dll; do
  copy_deps "$plugin"
done
shopt -u nullglob

# ── Launcher .bat que fija las rutas de GStreamer y arranca la app ──
echo ">> Generando saralibre.bat (launcher)"
cat > "$OUT_DIR/saralibre.bat" <<'BAT'
@echo off
rem Launcher de SARA Libre — fija las rutas de plugins de GStreamer y arranca la app.
setlocal
set "HERE=%~dp0"
set "GST_PLUGIN_SYSTEM_PATH=%HERE%gstreamer-1.0"
set "GST_PLUGIN_PATH=%HERE%gstreamer-1.0"
set "GST_PLUGIN_SCANNER=%HERE%gstreamer-1.0\gst-plugin-scanner.exe"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%HERE%platforms"
start "" "%HERE%saralibre.exe" %*
endlocal
BAT

echo ">> Listo. Contenido de $OUT_DIR:"
ls -1 "$OUT_DIR" | sed 's/^/   /'
echo ">> Tamaño total:"
du -sh "$OUT_DIR" | sed 's/^/   /'
echo
echo "Para correr en Windows: doble clic en saralibre.bat (recomendado),"
echo "o ejecutá saralibre.exe directamente."
