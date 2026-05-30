#include "data/config_manager.h"
#include "util/logger.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>

void testFirstRun()
{
    QTemporaryDir tmpDir;
    qputenv("XDG_CONFIG_HOME", tmpDir.path().toUtf8());

    sara::ConfigManager cm;
    assert(cm.load() && "First load should succeed");
    assert(cm.isFirstRun() && "Should detect first run");

    // Verificar valores por defecto
    assert(cm.config().radioName == "Mi Radio");
    assert(cm.config().crossfadeMs == 3000);
    assert(cm.config().noRepeatHours == 3);
    assert(cm.config().startupMode == 0);
    assert(cm.config().use24h == true);

    std::cout << "  ✓ first run defaults\n";
}

void testSaveAndReload()
{
    QTemporaryDir tmpDir;
    qputenv("XDG_CONFIG_HOME", tmpDir.path().toUtf8());

    // Guardar
    {
        sara::ConfigManager cm;
        cm.load();
        cm.config().radioName = "Radio Libre FM";
        cm.config().fallbackFolder = "/home/radio/musica";
        cm.config().mainAudioDevice = "pulse_sink_1";
        cm.config().crossfadeMs = 5000;
        cm.config().radioCity = "Mendoza";
        assert(cm.save() && "Save should succeed");
    }

    // Recargar
    {
        sara::ConfigManager cm;
        assert(cm.load() && "Reload should succeed");
        assert(!cm.isFirstRun() && "Should NOT be first run");
        assert(cm.config().radioName == "Radio Libre FM");
        assert(cm.config().fallbackFolder == "/home/radio/musica");
        assert(cm.config().mainAudioDevice == "pulse_sink_1");
        assert(cm.config().crossfadeMs == 5000);
        assert(cm.config().radioCity == "Mendoza");
    }

    std::cout << "  ✓ save & reload\n";
}

void testMinimalConfig()
{
    QTemporaryDir tmpDir;
    qputenv("XDG_CONFIG_HOME", tmpDir.path().toUtf8());

    sara::ConfigManager cm;
    cm.load();

    // Defaults: nombre por defecto, sin fallback folder → no válido
    assert(!cm.isMinimalConfigValid() && "Default config should be incomplete");

    cm.config().fallbackFolder = "/some/folder";
    assert(cm.isMinimalConfigValid() && "With fallback folder should be valid");

    std::cout << "  ✓ minimal config validation\n";
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    sara::initLogger("warn");

    std::cout << "\n── Test ConfigManager ─────────────────\n";

    testFirstRun();
    testSaveAndReload();
    testMinimalConfig();

    std::cout << "\n  Todos los tests pasaron ✓\n\n";
    return 0;
}
