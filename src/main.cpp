#include "core/types.h"
#include "util/logger.h"
#include "util/codec_checker.h"
#include "data/user_manager.h"
#include "ui/login_dialog.h"
#include "data/database.h"
#include "data/config_manager.h"
#include "audio/audio_engine.h"
#include "ui/main_window.h"
#include "ui/settings_dialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTimer>
#include <QInputDialog>
#include <QFileDialog>
#include <QTextStream>
#include <QDir>
#include <QFile>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>
#include <csignal>
#include <iostream>

static QApplication* g_app = nullptr;

void initResources() {
    Q_INIT_RESOURCE(saralibre);
}

void signalHandler(int sig)
{
    LOG_INFO("Señal {} recibida, cerrando...", sig);
    if (g_app) g_app->quit();
}

void printBanner()
{
    std::cout << R"(
╔══════════════════════════════════════════════════════════╗
║                                                          ║
║   ███████╗ █████╗ ██████╗  █████╗                       ║
║   ██╔════╝██╔══██╗██╔══██╗██╔══██╗                      ║
║   ███████╗███████║██████╔╝███████║                       ║
║   ╚════██║██╔══██║██╔══██╗██╔══██║                       ║
║   ███████║██║  ██║██║  ██║██║  ██║  L I B R E           ║
║   ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝  v)" << sara::VERSION << R"(            ║
║                                                          ║
║   Software de Automatización RAdial Libre                ║
║   Licencia: GPL v3                                       ║
╚══════════════════════════════════════════════════════════╝
)" << std::endl;
}

int main(int argc, char* argv[])
{
    printBanner();

    // Registrar recursos Qt de la biblioteca estática
    initResources();

    QApplication app(argc, argv);
    app.setApplicationName("SARA Libre");
    app.setApplicationVersion(sara::VERSION);
    app.setOrganizationName("SaraLibre");
    g_app = &app;

    // Manejo de señales POSIX
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Argumentos de línea de comandos
    QCommandLineParser parser;
    parser.setApplicationDescription("SARA Libre — Automatización de Radio");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption logLevelOpt(
        {"l", "log-level"}, "Nivel de log (trace, debug, info, warn, error)",
        "level", "info"
    );
    parser.addOption(logLevelOpt);

    QCommandLineOption listDevicesOpt(
        {"d", "devices"}, "Listar dispositivos de audio"
    );
    parser.addOption(listDevicesOpt);

    parser.process(app);

    // ── 1. Logger ────────────────────────────────────
    sara::initLogger(parser.value(logLevelOpt).toStdString());

    // ── 2. Configuración ─────────────────────────────
    sara::ConfigManager configManager;
    if (!configManager.load()) {
        LOG_ERROR("Error cargando configuración");
        return 1;
    }

    // ── 2b. Traducciones ──────────────────────────────
    QTranslator qtTranslator;    // Traducción de Qt base (diálogos nativos)
    QTranslator appTranslator;   // Traducción de SARA Libre

    {
        QString lang = configManager.config().language;
        QLocale locale;
        if (lang == "auto" || lang.isEmpty()) {
            locale = QLocale::system();
        } else {
            locale = QLocale(lang);
        }

        // Traducción de Qt base
        if (qtTranslator.load(locale, "qt", "_",
                QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            app.installTranslator(&qtTranslator);
        }

        // Traducción de SARA Libre
        // Buscar en: directorio del ejecutable, /usr/share/saralibre/translations/,
        //            y el directorio de recursos
        QStringList searchPaths = {
            QApplication::applicationDirPath() + "/translations",
            "/usr/share/saralibre/translations",
            "/usr/local/share/saralibre/translations",
            ":/translations"
        };

        bool loaded = false;
        for (const auto& path : searchPaths) {
            if (appTranslator.load(locale, "saralibre", "_", path)) {
                app.installTranslator(&appTranslator);
                LOG_INFO("[i18n] Traducción cargada: {} (desde {})",
                         locale.name().toStdString(), path.toStdString());
                loaded = true;
                break;
            }
        }
        if (!loaded) {
            LOG_INFO("[i18n] Usando idioma por defecto (castellano)");
        }
    }

    // ── 3. Base de datos ─────────────────────────────
    sara::Database database;
    if (!database.open()) {
        LOG_ERROR("Error abriendo base de datos");
        return 1;
    }

    // ── 4. Listar dispositivos (si se pidió) ─────────
    if (parser.isSet(listDevicesOpt)) {
        gst_init(nullptr, nullptr);
        auto devices = sara::AudioEngine::availableAudioDevices();
        std::cout << "\nDispositivos de audio disponibles:\n";
        for (int i = 0; i < devices.size(); ++i) {
            std::cout << "  [" << i << "] " << devices[i].toStdString() << "\n";
        }
        return 0;
    }

    // ── 5. Wizard de primera ejecución ───────────────
    if (configManager.isFirstRun() || !configManager.isMinimalConfigValid()) {
        LOG_INFO("Mostrando wizard de configuración inicial...");

        sara::SettingsDialog wizard(configManager.config(), true);
        if (wizard.exec() != QDialog::Accepted) {
            LOG_INFO("Configuración cancelada, saliendo");
            return 0;
        }

        configManager.config() = wizard.result();
        configManager.save();
        LOG_INFO("Configuración inicial guardada");
    }

    // ── 6. Motor de audio ────────────────────────────
    sara::AudioEngine audioEngine;
    const auto& cfg = configManager.config();

    audioEngine.setReplayGainEnabled(cfg.replayGainEnabled);
    audioEngine.setProcessingEnabled(true);  // Siempre crear EQ/compresor (pass-through si no activos)

    if (!audioEngine.initialize(cfg.mainAudioDevice, cfg.cueAudioDevice,
                                cfg.instantAudioDevice)) {
        LOG_ERROR("Error inicializando motor de audio");
        return 1;
    }

    // Aplicar volumen
    audioEngine.setMasterVolume(0.75);

    // ── 6b. Verificar codecs de GStreamer ────────────
    auto codecResult = sara::checkCodecs();
    if (!codecResult.allCriticalFound) {
        QString report = sara::formatCodecReport(codecResult);
        QMessageBox::warning(nullptr, "SARA Libre — Codecs faltantes", report);
        LOG_WARN("[Main] Codecs críticos faltantes: {}", 
                 codecResult.missingCritical.join(", ").toStdString());
    } else if (!codecResult.missingOptional.isEmpty()) {
        LOG_INFO("[Main] Codecs opcionales faltantes: {}", 
                 codecResult.missingOptional.join(", ").toStdString());
    }

    // ── 7. Gestión de usuarios ────────────────────────
    sara::UserManager userManager(&database);
    sara::UserInfo currentUser;

    // Primera ejecución: crear cuenta de administración
    if (!userManager.hasAdmin()) {
        QMessageBox::information(nullptr, "SARA Libre",
            "Bienvenido/a a SARA Libre.\n\n"
            "A continuación, cree la cuenta de administración.\n"
            "Esta cuenta tendrá acceso completo al sistema.");

        bool created = false;
        while (!created) {
            bool ok;
            QString name = QInputDialog::getText(nullptr, "Crear cuenta de administración",
                "Nombre visible (ej: María García):", QLineEdit::Normal, "", &ok);
            if (!ok || name.trimmed().isEmpty()) { return 0; }

            QString username = QInputDialog::getText(nullptr, "Crear cuenta de administración",
                "Usuario/a (para iniciar sesión):", QLineEdit::Normal, "", &ok);
            if (!ok || username.trimmed().isEmpty()) { return 0; }

            QString pin = QInputDialog::getText(nullptr, "Crear cuenta de administración",
                "PIN numérico (4-8 dígitos):", QLineEdit::Password, "", &ok);
            if (!ok || pin.isEmpty()) { return 0; }
            if (pin.length() < 4 || pin.length() > 8) {
                QMessageBox::warning(nullptr, "PIN inválido",
                    "El PIN debe tener entre 4 y 8 dígitos.");
                continue;
            }

            int userId = userManager.createUser(username.trimmed(), name.trimmed(),
                                                 pin, sara::UserRole::Admin);
            if (userId > 0) {
                // Generar y ofrecer guardar token de recuperación
                QString token = userManager.generateRecoveryToken(userId);
                QString savePath = QFileDialog::getSaveFileName(nullptr,
                    "Guardar archivo de recuperación",
                    QDir::homePath() + "/sara_recovery.key",
                    "Archivo de recuperación (*.key)");
                if (!savePath.isEmpty()) {
                    QFile file(savePath);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream out(&file);
                        out << token;
                        file.close();
                        QMessageBox::information(nullptr, "Archivo de recuperación",
                            QString("Archivo guardado en:\n%1\n\n"
                                    "Guárdelo en un lugar seguro. Lo necesitará si olvida su PIN.")
                            .arg(savePath));
                    }
                }
                created = true;
                auto user = userManager.findUserById(userId);
                if (user) currentUser = *user;
            } else {
                QMessageBox::warning(nullptr, "Error",
                    "No se pudo crear la cuenta. El usuario/a ya existe o hubo un error.");
            }
        }
    }

    // Sesión de inicio:
    // - 1 solo usuario: login implícito (siempre conectado)
    // - >1 usuarios: arranca sin sesión (música suena, controles deshabilitados)
    //   La persona inicia sesión desde el botón del header cuando llegue.
    if (currentUser.id == 0 && userManager.userCount() == 1) {
        auto users = userManager.allUsers();
        if (!users.isEmpty()) currentUser = users.first();
    }
    // Si hay >1 usuarios y currentUser.id == 0, queda sin sesión → controles deshabilitados

    LOG_INFO("Sesión: {} (rol: {})",
             currentUser.id > 0 ? currentUser.displayName.toStdString() : "sin sesión",
             currentUser.id > 0 ? static_cast<int>(currentUser.role) : -1);

    // ── 8. Ventana principal ─────────────────────────
    sara::MainWindow mainWindow(&audioEngine, &database, &configManager, &userManager, currentUser);
    mainWindow.show();

    const char* modeNames[] = {"Último estado", "Automático", "Manual"};
    int modeIdx = (cfg.startupMode >= 0 && cfg.startupMode <= 2) ? cfg.startupMode : 0;
    LOG_INFO("SARA Libre iniciado — {} — Inicio: {}",
             cfg.radioName.toStdString(), modeNames[modeIdx]);

    return app.exec();
}
