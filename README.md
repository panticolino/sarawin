# SARA Libre

**Software de Automatización RAdial Libre**

Sistema de automatización de radio para GNU/Linux. Diseñado para funcionar 24/7 en emisoras reales.

## Estado actual: Fase 0 (Cimientos)

Infraestructura base funcional:
- Motor de audio con 3 pipelines GStreamer (Programación, Publicidad, Asistente en Vivo)
- Base de datos SQLite con modo WAL (tolerante a cortes de luz)
- Configuración TOML legible por humanos
- Sistema de logging rotativo con spdlog
- Escáner de archivos de audio recursivo

## Compilación rápida

```bash
chmod +x build.sh
./build.sh    # Elegir opción 1 para instalación completa
```

## Dependencias

**Sistema (Debian 12 / Devuan 5 / Ubuntu 22.04+):**
```bash
# Compilación (headers y herramientas)
sudo apt install build-essential cmake pkg-config \
    qt6-base-dev \
    libqt6sql6-sqlite \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev

# Runtime (codecs y salida de audio — necesarios para que suene)
sudo apt install \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-pulseaudio
```

**Third-party (descargadas automáticamente por `build.sh`):**
- spdlog (MIT) — logging
- toml++ (MIT) — configuración

## Uso (Fase 0)

```bash
# Verificar que todo funciona
./build/saralibre

# Listar dispositivos de audio
./build/saralibre --devices

# Reproducir un archivo de prueba
./build/saralibre --play /ruta/a/cancion.mp3

# Ejecutar tests
cd build && ctest --output-on-failure
```

## Estructura del proyecto

```
saralibre/
├── src/
│   ├── core/       # Lógica de negocio (sin dependencia de UI)
│   ├── audio/      # Motor GStreamer (pipelines, crossfader)
│   ├── data/       # SQLite + TOML (persistencia, configuración)
│   ├── util/       # Logger, escáner de archivos
│   └── ui/         # Qt6 Widgets (Fase 1+)
├── resources/      # SQL schema, iconos, QRC
├── tests/          # Tests unitarios
└── dist/           # Archivos de distribución (systemd, SysV, .desktop)
```

## Licencia

GPL v3 — Ver archivo LICENSE.
