# Guía de Traducciones — SARA Libre

## ¿Cómo contribuir traducciones?

SARA Libre está diseñada para que cualquier radio comunitaria pueda traducir
la interfaz a su idioma, incluyendo lenguas indígenas.

### Opción 1: Con Qt Linguist (recomendado)

1. Instalar Qt Linguist:
   - Debian/Ubuntu: `sudo apt install qttools5-dev-tools`
   - Manjaro: `sudo pacman -S qt6-tools`

2. Copiar la plantilla de traducción:
   ```bash
   cp translations/saralibre_es.ts translations/saralibre_XX.ts
   ```
   (donde XX es el código de su idioma, ej: `gn` para guaraní, `qu` para quechua)

3. Abrir con Qt Linguist:
   ```bash
   linguist translations/saralibre_XX.ts
   ```

4. Traducir las cadenas de texto. En cada entrada verá:
   - **Source text**: el texto original en castellano
   - **Translation**: escriba su traducción aquí
   - Marcar como "✓" (aceptada) cuando esté lista

5. Compilar la traducción:
   ```bash
   lrelease translations/saralibre_XX.ts
   ```
   Esto genera `saralibre_XX.qm` que es el archivo que SARA Libre carga.

6. Copiar a la carpeta de traducciones:
   ```bash
   cp translations/saralibre_XX.qm /usr/local/share/saralibre/translations/
   ```

### Opción 2: Con editor de texto (sin Qt Linguist)

El archivo `.ts` es XML simple. Puede editarlo con cualquier editor de texto:

```xml
<message>
    <source>Reproducir</source>
    <translation>Play</translation>  <!-- Escriba su traducción aquí -->
</message>
```

Después de editar, compile con:
```bash
lrelease translations/saralibre_XX.ts
```

### Opción 3: Enviar para inclusión

Si su radio traduce SARA Libre a un idioma nuevo, envíe el archivo `.ts`
al proyecto para que se incluya en futuras versiones. Todas las traducciones
son bienvenidas.

## Códigos de idioma comunes

| Código | Idioma |
|--------|--------|
| es | Español/Castellano |
| pt_BR | Português (Brasil) |
| en | English |
| gn | Guaraní |
| qu | Quechua |
| ay | Aymara |
| nah | Náhuatl |
| map | Mapudungun |

## Notas técnicas

- El idioma base del código fuente es **castellano**. Sin traducción cargada,
  SARA Libre se muestra en castellano.
- Los archivos `.ts` son XML con pares original→traducción.
- Los archivos `.qm` son la versión compilada (binaria) que SARA carga.
- SARA busca traducciones en:
  - `./translations/` (directorio del ejecutable)
  - `/usr/share/saralibre/translations/`
  - `/usr/local/share/saralibre/translations/`
