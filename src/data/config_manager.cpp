#include "data/config_manager.h"
#include "util/logger.h"

#include <toml++/toml.hpp>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <fstream>

namespace sara {

ConfigManager::ConfigManager()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + "/saralibre";
    QDir().mkpath(configDir);
    configPath_ = configDir + "/config.toml";
}

bool ConfigManager::load()
{
    QFile file(configPath_);
    if (!file.exists()) {
        LOG_INFO("[Config] Primera ejecución, creando config.toml por defecto");
        firstRun_ = true;
        writeDefaults();
        return true;
    }

    try {
        auto tbl = toml::parse_file(configPath_.toStdString());

        // [radio]
        if (auto radio = tbl["radio"].as_table()) {
            auto getStr = [&](const char* key, const char* def = "") -> std::string {
                auto* n = radio->get(key); return n ? n->value_or(std::string(def)) : std::string(def);
            };
            config_.radioName      = QString::fromStdString(getStr("name", "Mi Radio"));
            config_.radioSlogan    = QString::fromStdString(getStr("slogan"));
            config_.radioFrequency = QString::fromStdString(getStr("frequency"));
            config_.radioCity      = QString::fromStdString(getStr("city"));
            config_.radioCountry   = QString::fromStdString(getStr("country"));
        }

        // [interface]
        if (auto ui = tbl["interface"].as_table()) {
            auto getStr = [&](const char* key, const char* def = "") -> std::string {
                auto* n = ui->get(key); return n ? n->value_or(std::string(def)) : std::string(def);
            };
            auto getInt = [&](const char* key, int def = 0) -> int {
                auto* n = ui->get(key); return n ? n->value_or(def) : def;
            };
            config_.language = QString::fromStdString(getStr("language", "auto"));
            config_.theme    = QString::fromStdString(getStr("theme", "auto"));
            config_.fontSize = getInt("font_size", 0);
        }

        // [audio]
        if (auto audio = tbl["audio"].as_table()) {
            // Helper: toml++ get() retorna nullptr si la clave no existe
            auto getString = [&](const char* key, const char* def = "") -> std::string {
                auto* node = audio->get(key);
                return node ? node->value_or(std::string(def)) : std::string(def);
            };
            auto getInt = [&](const char* key, int def = 0) -> int {
                auto* node = audio->get(key);
                return node ? node->value_or(def) : def;
            };
            auto getBool = [&](const char* key, bool def = true) -> bool {
                auto* node = audio->get(key);
                return node ? node->value_or(def) : def;
            };

            config_.mainAudioDevice    = QString::fromStdString(getString("main_device", "default"));
            config_.cueAudioDevice     = QString::fromStdString(getString("cue_device"));
            config_.instantAudioDevice = QString::fromStdString(getString("instant_device"));
            config_.radioFolder        = QString::fromStdString(getString("radio_folder"));
            config_.fallbackFolder     = QString::fromStdString(getString("fallback_folder"));
            config_.crossfadeMs        = getInt("crossfade_ms", 3000);
            config_.crossfadeEnabled   = getBool("crossfade_enabled", true);
            config_.fadeOutMs          = getInt("fadeout_ms", 500);
            config_.fadeOutEnabled     = getBool("fadeout_enabled", true);
            config_.noRepeatHours      = getInt("no_repeat_hours", 3);
            config_.noRepeatArtistTracks = getInt("no_repeat_artist_tracks", 0);
            config_.silenceDetectionEnabled = getBool("silence_detection", true);
            config_.silenceThresholdSecs    = getInt("silence_threshold_secs", 15);
            config_.silenceLevelDb          = getInt("silence_level_db", -50);
            config_.replayGainEnabled       = getBool("replay_gain", false);
            config_.vuMeterEnabled          = getBool("vu_meter", true);
            config_.eqEnabled               = getBool("eq_enabled", false);
            config_.compressorEnabled       = getBool("compressor_enabled", false);
            config_.compressorThresholdDb   = audio->get("compressor_threshold")
                ? audio->get("compressor_threshold")->value_or(-10.0) : -10.0;
            config_.compressorRatio         = audio->get("compressor_ratio")
                ? audio->get("compressor_ratio")->value_or(4.0) : 4.0;

            // Leer bandas del ecualizador
            config_.eqPresetName = QString::fromStdString(getString("eq_preset_name"));
            if (auto eqArr = audio->get("eq_bands")) {
                if (auto* arr = eqArr->as_array()) {
                    config_.eqBands.clear();
                    for (const auto& v : *arr) {
                        config_.eqBands.append(v.value_or(0.0));
                    }
                    while (config_.eqBands.size() < 10) config_.eqBands.append(0.0);
                }
            }
        }

        // [pisadores]
        if (auto pis = tbl["pisadores"].as_table()) {
            auto getStr = [&](const char* key, const char* def = "") -> std::string {
                auto* n = pis->get(key); return n ? n->value_or(std::string(def)) : std::string(def);
            };
            auto getInt = [&](const char* key, int def = 0) -> int {
                auto* n = pis->get(key); return n ? n->value_or(def) : def;
            };
            auto getBool = [&](const char* key, bool def = false) -> bool {
                auto* n = pis->get(key); return n ? n->value_or(def) : def;
            };
            auto getDbl = [&](const char* key, double def = 0.25) -> double {
                auto* n = pis->get(key); return n ? n->value_or(def) : def;
            };
            config_.pisadorEnabled  = getBool("enabled", false);
            config_.pisadorFolder   = QString::fromStdString(getStr("folder"));
            config_.pisadorFrequency = getInt("frequency", 1);
            config_.pisadorDuckLevel = getDbl("duck_level", 0.25);
            config_.pisadorDelaySecs = getInt("delay_secs", 3);

            if (auto* arr = pis->get("excluded_folders")) {
                if (auto* a = arr->as_array()) {
                    for (const auto& v : *a) {
                        if (auto* s = v.as_string()) {
                            config_.pisadorExcludedFolders << QString::fromStdString(s->get());
                        }
                    }
                }
            }
        }

        // [time_announcements]
        if (auto ta = tbl["time_announcements"].as_table()) {
            auto getStr = [&](const char* key) -> std::string {
                auto* n = ta->get(key); return n ? n->value_or(std::string("")) : std::string("");
            };
            config_.hoursFolder   = QString::fromStdString(getStr("hours_folder"));
            config_.minutesFolder = QString::fromStdString(getStr("minutes_folder"));
            config_.prefixFile    = QString::fromStdString(getStr("prefix_file"));
            config_.suffixFile    = QString::fromStdString(getStr("suffix_file"));
            auto* n24 = ta->get("use_24h");
            config_.use24h = n24 ? n24->value_or(true) : true;
        }

        // [ad_break]
        if (auto ab = tbl["ad_break"].as_table()) {
            auto getStr = [&](const char* key) -> std::string {
                auto* n = ab->get(key); return n ? n->value_or(std::string("")) : std::string("");
            };
            config_.adIntroFile = QString::fromStdString(getStr("intro_file"));
            config_.adOutroFile = QString::fromStdString(getStr("outro_file"));
        }

        // [recording]
        if (auto rec = tbl["recording"].as_table()) {
            auto getStr = [&](const char* key, const char* def = "") -> std::string {
                auto* n = rec->get(key); return n ? n->value_or(std::string(def)) : std::string(def);
            };
            auto getInt = [&](const char* key, int def) -> int {
                auto* n = rec->get(key); return n ? n->value_or(def) : def;
            };
            auto getBool = [&](const char* key, bool def) -> bool {
                auto* n = rec->get(key); return n ? n->value_or(def) : def;
            };
            config_.recordingFolder = QString::fromStdString(getStr("folder"));
            config_.recordingFormat = QString::fromStdString(getStr("format", "mp3"));
            config_.recordingBitrate = getInt("bitrate", 96);
            config_.recordingSegmentByHour = getBool("segment_by_hour", true);
            config_.recordingDevice = QString::fromStdString(getStr("device"));
        }

        // [startup]
        if (auto startup = tbl["startup"].as_table()) {
            auto* n = startup->get("startup_mode");
            config_.startupMode = n ? n->value_or(0) : 0;
            auto* dv = startup->get("default_volume");
            config_.defaultVolume = dv ? dv->value_or(95) : 95;
            auto* tl = startup->get("talkover_level");
            config_.talkoverLevel = tl ? tl->value_or(25) : 25;
        }

        // [backup]
        if (auto backup = tbl["backup"].as_table()) {
            auto getBool = [&](const char* key, bool def) -> bool {
                auto* n = backup->get(key); return n ? n->value_or(def) : def;
            };
            auto getInt = [&](const char* key, int def) -> int {
                auto* n = backup->get(key); return n ? n->value_or(def) : def;
            };
            config_.backupEnabled       = getBool("enabled", true);
            config_.backupIntervalHours = getInt("interval_hours", 24);
            config_.backupMaxCount      = getInt("max_count", 7);
        }

        // [streaming]
        if (auto streaming = tbl["streaming"].as_table()) {
            auto* npEnabled = streaming->get("now_playing_enabled");
            config_.nowPlayingEnabled = npEnabled ? npEnabled->value_or(false) : false;
            auto* npFile = streaming->get("now_playing_file");
            config_.nowPlayingFile = npFile
                ? QString::fromStdString(npFile->value_or(std::string(""))) : QString();
        }

        LOG_INFO("[Config] Configuración cargada desde {}", configPath_.toStdString());
        return true;

    } catch (const toml::parse_error& err) {
        LOG_ERROR("[Config] Error parseando TOML: {}", err.description());
        return false;
    }
}

bool ConfigManager::save() const
{
    try {
        auto tbl = toml::table{
            {"radio", toml::table{
                {"name",      config_.radioName.toStdString()},
                {"slogan",    config_.radioSlogan.toStdString()},
                {"frequency", config_.radioFrequency.toStdString()},
                {"city",      config_.radioCity.toStdString()},
                {"country",   config_.radioCountry.toStdString()},
            }},
            {"interface", toml::table{
                {"language",  config_.language.toStdString()},
                {"theme",     config_.theme.toStdString()},
                {"font_size", config_.fontSize},
            }},
            {"audio", toml::table{
                {"main_device",    config_.mainAudioDevice.toStdString()},
                {"cue_device",     config_.cueAudioDevice.toStdString()},
                {"instant_device", config_.instantAudioDevice.toStdString()},
                {"radio_folder",   config_.radioFolder.toStdString()},
                {"fallback_folder", config_.fallbackFolder.toStdString()},
                {"crossfade_ms",   config_.crossfadeMs},
                {"crossfade_enabled", config_.crossfadeEnabled},
                {"fadeout_ms",     config_.fadeOutMs},
                {"fadeout_enabled", config_.fadeOutEnabled},
                {"no_repeat_hours", config_.noRepeatHours},
                {"no_repeat_artist_tracks", config_.noRepeatArtistTracks},
                {"silence_detection", config_.silenceDetectionEnabled},
                {"silence_threshold_secs", config_.silenceThresholdSecs},
                {"silence_level_db", config_.silenceLevelDb},
                {"replay_gain", config_.replayGainEnabled},
                {"vu_meter", config_.vuMeterEnabled},
                {"eq_enabled", config_.eqEnabled},
                {"compressor_enabled", config_.compressorEnabled},
                {"compressor_threshold", config_.compressorThresholdDb},
                {"compressor_ratio", config_.compressorRatio},
                {"eq_bands", toml::array{
                    config_.eqBands.value(0, 0.0), config_.eqBands.value(1, 0.0),
                    config_.eqBands.value(2, 0.0), config_.eqBands.value(3, 0.0),
                    config_.eqBands.value(4, 0.0), config_.eqBands.value(5, 0.0),
                    config_.eqBands.value(6, 0.0), config_.eqBands.value(7, 0.0),
                    config_.eqBands.value(8, 0.0), config_.eqBands.value(9, 0.0)
                }},
                {"eq_preset_name", config_.eqPresetName.toStdString()},
            }},
            {"pisadores", toml::table{
                {"enabled", config_.pisadorEnabled},
                {"folder", config_.pisadorFolder.toStdString()},
                {"frequency", config_.pisadorFrequency},
                {"duck_level", config_.pisadorDuckLevel},
                {"delay_secs", config_.pisadorDelaySecs},
                {"excluded_folders", [&]() {
                    toml::array arr;
                    for (const auto& f : config_.pisadorExcludedFolders) {
                        arr.push_back(f.toStdString());
                    }
                    return arr;
                }()},
            }},
            {"time_announcements", toml::table{
                {"hours_folder",   config_.hoursFolder.toStdString()},
                {"minutes_folder", config_.minutesFolder.toStdString()},
                {"prefix_file",    config_.prefixFile.toStdString()},
                {"suffix_file",    config_.suffixFile.toStdString()},
                {"use_24h",        config_.use24h},
            }},
            {"ad_break", toml::table{
                {"intro_file", config_.adIntroFile.toStdString()},
                {"outro_file", config_.adOutroFile.toStdString()},
            }},
            {"recording", toml::table{
                {"folder",           config_.recordingFolder.toStdString()},
                {"format",           config_.recordingFormat.toStdString()},
                {"bitrate",          config_.recordingBitrate},
                {"segment_by_hour",  config_.recordingSegmentByHour},
                {"device",           config_.recordingDevice.toStdString()},
            }},
            {"startup", toml::table{
                {"startup_mode", config_.startupMode},
                {"default_volume", config_.defaultVolume},
                {"talkover_level", config_.talkoverLevel},
            }},
            {"backup", toml::table{
                {"enabled", config_.backupEnabled},
                {"interval_hours", config_.backupIntervalHours},
                {"max_count", config_.backupMaxCount},
            }},
            {"streaming", toml::table{
                {"now_playing_enabled", config_.nowPlayingEnabled},
                {"now_playing_file", config_.nowPlayingFile.toStdString()},
            }},
        };

        std::ofstream file(configPath_.toStdString());
        if (!file.is_open()) {
            LOG_ERROR("[Config] No se pudo abrir para escritura: {}", configPath_.toStdString());
            return false;
        }

        file << "# SARA Libre — Configuración\n";
        file << "# Editado automáticamente. Puede modificarse a mano.\n\n";
        file << tbl;
        file.close();

        LOG_INFO("[Config] Guardada en {}", configPath_.toStdString());
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("[Config] Error guardando: {}", e.what());
        return false;
    }
}

bool ConfigManager::isMinimalConfigValid() const
{
    return !config_.radioName.isEmpty()
        && !config_.fallbackFolder.isEmpty()
        && !config_.mainAudioDevice.isEmpty();
}

void ConfigManager::writeDefaults() const
{
    save();  // Guarda la config_ con valores por defecto
}

} // namespace sara
