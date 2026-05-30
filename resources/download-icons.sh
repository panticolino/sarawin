#!/bin/bash
# ══════════════════════════════════════════════════════════
#  Descargar set completo de iconos Lucide
#  https://github.com/lucide-icons/lucide
#  Licencia: ISC (compatible GPL)
# ══════════════════════════════════════════════════════════

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ICONS_DIR="${SCRIPT_DIR}/icons"

echo "→ Descargando iconos Lucide..."

# Descargar el repositorio (solo la carpeta de iconos SVG)
TMP_DIR=$(mktemp -d)
cd "$TMP_DIR"

git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/lucide-icons/lucide.git
cd lucide
git sparse-checkout set icons

# Copiar los SVGs al directorio de recursos
echo "→ Copiando SVGs a ${ICONS_DIR}..."
mkdir -p "$ICONS_DIR"

for svg_dir in icons/*/; do
    icon_name=$(basename "$svg_dir")
    if [ -f "${svg_dir}/${icon_name}.svg" ]; then
        cp "${svg_dir}/${icon_name}.svg" "${ICONS_DIR}/${icon_name}.svg"
    fi
done

# Limpiar
rm -rf "$TMP_DIR"

ICON_COUNT=$(ls -1 "${ICONS_DIR}"/*.svg 2>/dev/null | wc -l)
echo "✓ ${ICON_COUNT} iconos descargados en ${ICONS_DIR}/"
