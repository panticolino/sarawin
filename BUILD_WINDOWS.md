# Compilar SARA Libre para Windows 10/11 (x64)

Este repo incluye una compilación automática para Windows. **No necesitás compilar nada a mano**: GitHub lo hace por vos y te deja un `.exe` listo para descargar.

## Opción A — Automático (recomendado)

1. Copiá estos archivos a la raíz de tu repositorio de SARA Libre:
   - `.github/workflows/windows-build.yml`
   - `package-windows.sh`

   > El workflow detecta solo si tu `CMakeLists.txt` está en la raíz del repo
   > o dentro de una subcarpeta `saralibre/`. No tenés que configurar nada.

2. Subí los cambios (`git add . && git commit -m "CI Windows" && git push`).

3. En GitHub, andá a la pestaña **Actions** → workflow **"Build Windows (x64)"**.
   Se ejecuta solo al hacer push; también podés dispararlo a mano con
   **"Run workflow"**.

4. Cuando termine (unos minutos), entrá al run y descargá el artifact
   **`saralibre-windows-x64`**. Es un `.zip` con `saralibre.exe` y todas sus
   dependencias.

5. Descomprimí en Windows y ejecutá **`saralibre.bat`** (fija las rutas de los
   plugins de GStreamer antes de arrancar). También funciona el `saralibre.exe`
   directo si las DLLs están al lado.

### ¿Querés que genere un `.zip` en cada versión publicada?

El workflow ya lo contempla: si creás un tag de versión, adjunta el zip a la
Release automáticamente.

```bash
git tag v0.9.4
git push origin v0.9.4
```

## Opción B — Local con MSYS2 (si querés compilar en tu propia PC Windows)

1. Instalá [MSYS2](https://www.msys2.org/) y abrí la terminal **MINGW64**.
2. Instalá las dependencias:

   ```bash
   pacman -S --needed \
     mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
     mingw-w64-x86_64-pkgconf mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-tools \
     mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base \
     mingw-w64-x86_64-gst-plugins-good mingw-w64-x86_64-gst-plugins-bad \
     mingw-w64-x86_64-gst-plugins-ugly mingw-w64-x86_64-gst-libav \
     mingw-w64-x86_64-taglib mingw-w64-x86_64-spdlog \
     mingw-w64-x86_64-tomlplusplus mingw-w64-x86_64-ntldd
   ```

3. Compilá y empaquetá:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSARA_BUILD_TESTS=OFF
   cmake --build build --parallel
   bash package-windows.sh build dist-win
   ```

   El resultado queda en `dist-win/`.

## Notas técnicas

- Tu `CMakeLists.txt` no necesita cambios para este flujo: usa
  `find_package(Qt6)` + `pkg_check_modules(GST/TAGLIB)`, que funcionan tal cual
  bajo MinGW/MSYS2.
- La línea `install(FILES dist/saralibre.desktop ...)` del CMake es de Linux y
  fallaría con `cmake --install`, pero **este flujo no usa `install`** (toma el
  exe directo de `build/` y lo empaqueta con `package-windows.sh`), así que no
  molesta. Si más adelante querés `cmake --install` multiplataforma, conviene
  envolver esa línea en `if(UNIX AND NOT APPLE)`.
- El audio en Windows usa el backend WASAPI de GStreamer (incluido en los
  paquetes `gst-plugins-bad`). Para reproducir MP3/AAC/M4A se incluyen
  `gst-plugins-good`, `-ugly` y `gst-libav`.
- El manejo de señales POSIX (`SIGINT`/`SIGTERM`) de `main.cpp` compila sin
  cambios en MinGW.
