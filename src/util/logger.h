#ifndef SARA_UTIL_LOGGER_H
#define SARA_UTIL_LOGGER_H

#include <string>
#include <memory>
#include <spdlog/spdlog.h>

namespace sara {

/**
 * Inicializa el sistema de logging.
 * Debe llamarse una sola vez al arrancar, antes de cualquier uso de LOG_*.
 *
 * Crea dos sinks:
 *  - Consola (stdout) con color
 *  - Archivo rotativo en ~/.local/share/saralibre/logs/sara.log
 */
void initLogger(const std::string& level = "info");

/**
 * Obtiene el logger principal. Usar las macros LOG_* en su lugar.
 */
std::shared_ptr<spdlog::logger> getLogger();

// ── Macros de conveniencia ───────────────────────────────
// Uso: LOG_INFO("[AudioEngine] Pipeline {} started", id);

#define LOG_TRACE(...)  SPDLOG_LOGGER_TRACE(sara::getLogger(), __VA_ARGS__)
#define LOG_DEBUG(...)  SPDLOG_LOGGER_DEBUG(sara::getLogger(), __VA_ARGS__)
#define LOG_INFO(...)   SPDLOG_LOGGER_INFO(sara::getLogger(), __VA_ARGS__)
#define LOG_WARN(...)   SPDLOG_LOGGER_WARN(sara::getLogger(), __VA_ARGS__)
#define LOG_ERROR(...)  SPDLOG_LOGGER_ERROR(sara::getLogger(), __VA_ARGS__)

} // namespace sara

#endif // SARA_UTIL_LOGGER_H
