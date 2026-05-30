#!/bin/bash
set -e

# ══════════════════════════════════════════════════════════
#  SARA Libre — Script de compilación
# ══════════════════════════════════════════════════════════

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"

print_header() {
    echo -e "\n${CYAN}╔══════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║  SARA Libre — Build System           ║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════╝${NC}\n"
}

# ── Instalar dependencias del sistema ─────────────────────
install_deps() {
    echo -e "${YELLOW}→ Instalando dependencias del sistema...${NC}"

    if command -v apt-get &> /dev/null; then
        # ────────────────────────────────────────────────────
        # Debian 12+ / Devuan 5+ / Ubuntu 22.04+ / EterTICs / Debian 13+
        # ────────────────────────────────────────────────────
        sudo apt-get update

        # Compilación: lo que necesitamos para hacer build
        echo -e "${CYAN}  [1/4] Herramientas de compilación...${NC}"
        sudo apt-get install -y \
            build-essential \
            cmake \
            pkg-config \
            git

        # Compilación: headers de Qt6 y GStreamer contra los que enlazamos.
        # NOTA: qt6-base-dev es el mismo paquete en bookworm y trixie aunque
        # internamente apunta a libqt6core6 vs libqt6core6t64 según corresponda.
        #
        # libqt6svg6-dev se llama qt6-svg-dev en trixie. Probamos ambos: el
        # primero que apt encuentre es el correcto para esta release.
        echo -e "${CYAN}  [2/4] Librerías de desarrollo (Qt6 + GStreamer)...${NC}"
        sudo apt-get install -y \
            qt6-base-dev \
            libqt6sql6-sqlite \
            libgstreamer1.0-dev \
            libgstreamer-plugins-base1.0-dev

        # SVG dev: nombre cambió entre bookworm y trixie. Probamos los dos.
        if ! sudo apt-get install -y libqt6svg6-dev 2>/dev/null; then
            sudo apt-get install -y qt6-svg-dev 2>/dev/null || true
        fi

        # taglib dev: nombre cambió de libtag1-dev (1.x) a libtag-dev (2.x)
        # en trixie. libtag-dev en trixie provides libtag1-dev como
        # transitional, así que preferimos libtag-dev.
        if ! sudo apt-get install -y libtag-dev 2>/dev/null; then
            sudo apt-get install -y libtag1-dev 2>/dev/null || true
        fi

        # Runtime: codecs y sink de audio (necesarios para que suene).
        # El sink correcto depende del servidor que está corriendo:
        #   - Sistemas con PipeWire activo → gstreamer1.0-pipewire
        #   - Sistemas con PulseAudio puro → gstreamer1.0-pulseaudio
        # Detección por runtime.
        echo -e "${CYAN}  [3/4] Plugins de audio (codecs y salida)...${NC}"
        sudo apt-get install -y \
            gstreamer1.0-plugins-base \
            gstreamer1.0-plugins-good \
            gstreamer1.0-plugins-ugly \
            gstreamer1.0-libav

        # Detectar PipeWire en runtime y elegir sink
        if [ -e "/run/user/$(id -u)/pipewire-0" ] || \
           [ -n "${PIPEWIRE_RUNTIME_DIR:-}" ]; then
            echo -e "${CYAN}     PipeWire detectado, instalando gstreamer1.0-pipewire${NC}"
            sudo apt-get install -y gstreamer1.0-pipewire \
                || sudo apt-get install -y gstreamer1.0-pulseaudio
        else
            echo -e "${CYAN}     PulseAudio detectado (o ningún servidor activo), "
            echo -e "${CYAN}     instalando gstreamer1.0-pulseaudio${NC}"
            sudo apt-get install -y gstreamer1.0-pulseaudio \
                || sudo apt-get install -y gstreamer1.0-pipewire
        fi

        # Opcionales del sistema (si están disponibles)
        echo -e "${CYAN}  [4/4] Dependencias opcionales (spdlog, toml++)...${NC}"
        sudo apt-get install -y libspdlog-dev 2>/dev/null || true
        sudo apt-get install -y libtomlplusplus-dev 2>/dev/null || true

    elif command -v pacman &> /dev/null; then
        # ────────────────────────────────────────────────────
        # Arch Linux / Manjaro
        # ────────────────────────────────────────────────────
        sudo pacman -Syu --noconfirm \
            base-devel cmake pkgconf git \
            qt6-base \
            gstreamer gst-plugins-base gst-plugins-good gst-plugins-ugly \
            gst-libav \
            spdlog

    elif command -v dnf &> /dev/null; then
        # ────────────────────────────────────────────────────
        # Fedora
        # ────────────────────────────────────────────────────
        sudo dnf install -y \
            gcc-c++ cmake pkgconfig git \
            qt6-qtbase-devel \
            gstreamer1-devel gstreamer1-plugins-base-devel \
            gstreamer1-plugins-good gstreamer1-plugins-ugly-free \
            gstreamer1-plugins-bad-free \
            spdlog-devel

    else
        echo -e "${RED}Gestor de paquetes no reconocido.${NC}"
        echo ""
        echo "Instale manualmente:"
        echo "  Compilación:  cmake, g++, pkg-config"
        echo "  Qt6:          qt6-base-dev (incluye Core, Widgets, Sql)"
        echo "  GStreamer:    libgstreamer1.0-dev, libgstreamer-plugins-base1.0-dev"
        echo "  Codecs:       gstreamer1.0-plugins-good, gstreamer1.0-plugins-ugly"
        echo "  Audio sink:   gstreamer1.0-pulseaudio (o gstreamer1.0-pipewire)"
        echo "  Formatos:     gstreamer1.0-libav"
        echo ""
        exit 1
    fi

    echo -e "${GREEN}✓ Dependencias del sistema instaladas${NC}"
}

# ── Descargar librerías third-party ───────────────────────
fetch_third_party() {
    echo -e "${YELLOW}→ Descargando dependencias third-party...${NC}"

    cd "$PROJECT_DIR"

    # spdlog (si no está instalado en el sistema ni en third_party)
    if [ ! -d "third_party/spdlog" ] && ! pkg-config --exists spdlog 2>/dev/null; then
        echo "  Descargando spdlog..."
        git clone --depth 1 --branch v1.13.0 \
            https://github.com/gabime/spdlog.git third_party/spdlog
    fi

    # toml++
    if [ ! -d "third_party/tomlplusplus" ] && ! dpkg -s libtomlplusplus-dev &>/dev/null 2>&1; then
        echo "  Descargando toml++..."
        git clone --depth 1 --branch v3.4.0 \
            https://github.com/marzer/tomlplusplus.git third_party/tomlplusplus
    fi

    echo -e "${GREEN}✓ Third-party listo${NC}"
}

# ── Verificar dependencias antes de compilar ─────────────
verify_deps() {
    echo -e "${YELLOW}→ Verificando dependencias...${NC}"
    local missing=0

    # cmake
    if ! command -v cmake &> /dev/null; then
        echo -e "  ${RED}✗ cmake no encontrado${NC}"
        missing=1
    else
        echo -e "  ${GREEN}✓ cmake $(cmake --version | head -1 | awk '{print $3}')${NC}"
    fi

    # g++
    if ! command -v g++ &> /dev/null; then
        echo -e "  ${RED}✗ g++ no encontrado${NC}"
        missing=1
    else
        echo -e "  ${GREEN}✓ g++ $(g++ -dumpversion)${NC}"
    fi

    # pkg-config
    if ! command -v pkg-config &> /dev/null; then
        echo -e "  ${RED}✗ pkg-config no encontrado${NC}"
        missing=1
    else
        echo -e "  ${GREEN}✓ pkg-config${NC}"
    fi

    # Qt6 Core
    if pkg-config --exists Qt6Core 2>/dev/null; then
        echo -e "  ${GREEN}✓ Qt6 $(pkg-config --modversion Qt6Core)${NC}"
    elif [ -f /usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake ] || \
         [ -f /usr/lib/cmake/Qt6/Qt6Config.cmake ]; then
        echo -e "  ${GREEN}✓ Qt6 (encontrado via cmake)${NC}"
    else
        echo -e "  ${RED}✗ Qt6 no encontrado (instalar: qt6-base-dev)${NC}"
        missing=1
    fi

    # GStreamer core
    if pkg-config --exists gstreamer-1.0 2>/dev/null; then
        echo -e "  ${GREEN}✓ GStreamer $(pkg-config --modversion gstreamer-1.0)${NC}"
    else
        echo -e "  ${RED}✗ GStreamer dev no encontrado (instalar: libgstreamer1.0-dev)${NC}"
        missing=1
    fi

    # GStreamer plugins-base (pbutils, audio, app)
    if pkg-config --exists gstreamer-pbutils-1.0 2>/dev/null; then
        echo -e "  ${GREEN}✓ GStreamer plugins-base (pbutils, audio, app)${NC}"
    else
        echo -e "  ${RED}✗ GStreamer plugins-base dev no encontrado (instalar: libgstreamer-plugins-base1.0-dev)${NC}"
        missing=1
    fi

    # Qt6 SQLite driver plugin
    local qt6_plugin_dir="/usr/lib/x86_64-linux-gnu/qt6/plugins/sqldrivers"
    [ ! -d "$qt6_plugin_dir" ] && qt6_plugin_dir="/usr/lib/qt6/plugins/sqldrivers"
    if [ -f "$qt6_plugin_dir/libqsqlite.so" ]; then
        echo -e "  ${GREEN}✓ Qt6 SQLite driver${NC}"
    else
        echo -e "  ${RED}✗ Qt6 SQLite driver no encontrado (instalar: libqt6sql6-sqlite)${NC}"
        missing=1
    fi

    # GStreamer runtime plugins (no verificables con pkg-config, chequeamos binarios)
    local gst_plugin_dir="/usr/lib/x86_64-linux-gnu/gstreamer-1.0"
    [ ! -d "$gst_plugin_dir" ] && gst_plugin_dir="/usr/lib/gstreamer-1.0"

    if [ -f "$gst_plugin_dir/libgstplayback.so" ]; then
        echo -e "  ${GREEN}✓ GStreamer playbin (plugins-base runtime)${NC}"
    else
        echo -e "  ${RED}✗ GStreamer playbin no encontrado (instalar: gstreamer1.0-plugins-base)${NC}"
        missing=1
    fi

    if [ -f "$gst_plugin_dir/libgstlibav.so" ] || [ -f "$gst_plugin_dir/libgstavdec_mp3float.so" ]; then
        echo -e "  ${GREEN}✓ Codecs de audio (MP3, AAC, etc.)${NC}"
    else
        echo -e "  ${YELLOW}⚠ gstreamer1.0-libav no detectado (necesario para MP3, AAC)${NC}"
        echo -e "    Instalar: ${CYAN}sudo apt install gstreamer1.0-libav gstreamer1.0-plugins-ugly${NC}"
    fi

    if [ -f "$gst_plugin_dir/libgstpulseaudio.so" ] || [ -f "$gst_plugin_dir/libgstpipewire.so" ]; then
        echo -e "  ${GREEN}✓ Sink de audio (PulseAudio/PipeWire)${NC}"
    else
        echo -e "  ${YELLOW}⚠ Sink de audio no detectado${NC}"
        echo -e "    Instalar: ${CYAN}sudo apt install gstreamer1.0-pulseaudio${NC}"
    fi

    # spdlog (puede venir del sistema o de third_party)
    if pkg-config --exists spdlog 2>/dev/null; then
        echo -e "  ${GREEN}✓ spdlog $(pkg-config --modversion spdlog) (sistema)${NC}"
    elif [ -d "${PROJECT_DIR}/third_party/spdlog/include" ]; then
        echo -e "  ${GREEN}✓ spdlog (third_party local)${NC}"
    else
        echo -e "  ${YELLOW}⚠ spdlog no encontrado — se descargará con la opción 3${NC}"
    fi

    # toml++
    if [ -d "${PROJECT_DIR}/third_party/tomlplusplus/include" ]; then
        echo -e "  ${GREEN}✓ toml++ (third_party local)${NC}"
    elif [ -f /usr/include/toml++/toml.hpp ]; then
        echo -e "  ${GREEN}✓ toml++ (sistema)${NC}"
    else
        echo -e "  ${YELLOW}⚠ toml++ no encontrado — se descargará con la opción 3${NC}"
    fi

    echo ""
    if [ $missing -eq 1 ]; then
        echo -e "${RED}Faltan dependencias de compilación.${NC}"
        echo -e "Ejecute primero la opción 2 (instalar dependencias) y luego la 3 (third-party)."
        return 1
    fi
    echo -e "${GREEN}✓ Todas las dependencias de compilación presentes${NC}"
    return 0
}

# ── Compilar ──────────────────────────────────────────────
build() {
    echo -e "${YELLOW}→ Compilando SARA Libre...${NC}"

    # Verificar antes de intentar compilar
    if ! verify_deps; then
        echo -e "${RED}Abortando compilación por dependencias faltantes.${NC}"
        exit 1
    fi

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. \
        -DCMAKE_BUILD_TYPE="${1:-Release}" \
        -DSARA_BUILD_TESTS=ON

    make -j"$(nproc)"

    echo -e "${GREEN}✓ Compilación exitosa${NC}"
    echo -e "  Ejecutable: ${BUILD_DIR}/saralibre"
}

# ── Ejecutar tests ────────────────────────────────────────
run_tests() {
    echo -e "${YELLOW}→ Ejecutando tests...${NC}"

    cd "$BUILD_DIR"
    ctest --output-on-failure

    echo -e "${GREEN}✓ Tests completados${NC}"
}

# ── Menú ──────────────────────────────────────────────────
print_header

echo "  1) Instalación completa (deps + third-party + build + tests)"
echo "  2) Solo instalar dependencias del sistema"
echo "  3) Solo descargar third-party"
echo "  4) Verificar que todo esté listo para compilar"
echo "  5) Solo compilar (Release)"
echo "  6) Compilar en modo Debug"
echo "  7) Ejecutar tests"
echo "  8) Limpiar build"
echo ""
read -rp "  Seleccione opción [1-8]: " option

case $option in
    1)
        install_deps
        fetch_third_party
        build "Release"
        run_tests
        ;;
    2)
        install_deps
        ;;
    3)
        fetch_third_party
        ;;
    4)
        verify_deps
        ;;
    5)
        build "Release"
        ;;
    6)
        build "Debug"
        ;;
    7)
        run_tests
        ;;
    8)
        echo -e "${YELLOW}→ Limpiando build...${NC}"
        rm -rf "$BUILD_DIR"
        echo -e "${GREEN}✓ Limpio${NC}"
        ;;
    *)
        echo -e "${RED}Opción no válida${NC}"
        exit 1
        ;;
esac

echo -e "\n${GREEN}╔══════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  ✓ Operación completada              ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════╝${NC}\n"
