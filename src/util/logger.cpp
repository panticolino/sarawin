#include "util/logger.h"
#include "core/types.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <QStandardPaths>
#include <QDir>
#include <filesystem>

namespace sara {

static std::shared_ptr<spdlog::logger> s_logger;

void initLogger(const std::string& level)
{
    // Directorio de logs
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                     + "/saralibre/logs";
    QDir().mkpath(logDir);

    std::string logPath = (logDir + "/sara.log").toStdString();

    // Crear sinks
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Archivo rotativo: 5 MB máximo, 3 archivos de respaldo
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logPath, 5 * 1024 * 1024, 3
    );
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    // Crear logger con ambos sinks
    s_logger = std::make_shared<spdlog::logger>(
        "sara",
        spdlog::sinks_init_list{consoleSink, fileSink}
    );

    // Nivel
    s_logger->set_level(spdlog::level::from_str(level));
    s_logger->flush_on(spdlog::level::warn);  // Flush automático en warn+

    spdlog::set_default_logger(s_logger);

    LOG_INFO("SARA Libre v{} — Logger inicializado (nivel: {})", VERSION, level);
    LOG_INFO("Archivo de log: {}", logPath);
}

std::shared_ptr<spdlog::logger> getLogger()
{
    if (!s_logger) {
        // Fallback: si no se inicializó, crear uno básico a consola
        s_logger = spdlog::stdout_color_mt("sara");
    }
    return s_logger;
}

} // namespace sara
