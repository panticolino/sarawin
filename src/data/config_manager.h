#ifndef SARA_DATA_CONFIG_MANAGER_H
#define SARA_DATA_CONFIG_MANAGER_H

#include "core/types.h"
#include <QString>

namespace sara {

/**
 * Gestiona la configuración persistente de la aplicación.
 *
 * Archivo: ~/.config/saralibre/config.toml
 *
 * Si el archivo no existe, se crea con valores por defecto.
 * Si existe pero le faltan claves, las agrega sin tocar las existentes.
 */
class ConfigManager
{
public:
    ConfigManager();

    /// Cargar configuración desde disco (o crear archivo por defecto)
    bool load();

    /// Guardar configuración actual a disco
    bool save() const;

    /// Acceso a la configuración
    const AppConfig& config() const { return config_; }
    AppConfig& config() { return config_; }

    /// Ruta del archivo TOML
    QString configFilePath() const { return configPath_; }

    /// Verificar si es primera ejecución (no existía el archivo)
    bool isFirstRun() const { return firstRun_; }

    /// Verificar si la configuración mínima está completa
    /// (nombre de radio, carpeta de respaldo, tarjeta de audio)
    bool isMinimalConfigValid() const;

private:
    void writeDefaults() const;

    AppConfig config_;
    QString   configPath_;
    bool      firstRun_ = false;
};

} // namespace sara

#endif // SARA_DATA_CONFIG_MANAGER_H
