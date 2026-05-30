#include "audio/audio_engine.h"
#include "audio/audio_pipeline.h"
#include "util/logger.h"

#include <QCoreApplication>
#include <QTimer>
#include <cassert>
#include <iostream>

/**
 * Test del motor de audio.
 * Requiere GStreamer runtime instalado.
 *
 * Ejecución:
 *   ./test_audio                     (solo verifica inicialización)
 *   ./test_audio /ruta/archivo.mp3   (reproduce 3 segundos y verifica señales)
 */

void testInitialization()
{
    sara::AudioEngine engine;
    bool ok = engine.initialize("default", "");
    assert(ok && "AudioEngine should initialize");
    assert(engine.isInitialized());
    assert(engine.mainPipeline() != nullptr);
    assert(engine.eventsPipeline() != nullptr);
    assert(engine.instantPipeline() != nullptr);

    std::cout << "  ✓ engine initialization (3 pipelines)\n";
}

void testVolumeControl()
{
    sara::AudioEngine engine;
    engine.initialize();

    engine.setMasterVolume(0.5);
    assert(engine.masterVolume() == 0.5);
    assert(engine.mainPipeline()->volume() == 0.5);
    assert(engine.eventsPipeline()->volume() == 0.5);

    // InstantPlay tiene volumen independiente
    engine.instantPipeline()->setVolume(0.8);
    assert(engine.instantPipeline()->volume() == 0.8);

    // Clamp
    engine.setMasterVolume(1.5);
    assert(engine.masterVolume() == 1.0);
    engine.setMasterVolume(-0.5);
    assert(engine.masterVolume() == 0.0);

    std::cout << "  ✓ volume control\n";
}

void testDeviceList()
{
    // Esto puede no encontrar dispositivos en entornos headless
    auto devices = sara::AudioEngine::availableAudioDevices();
    std::cout << "  ✓ device enumeration (" << devices.size() << " dispositivos)\n";
}

void testPlayback(const QString& filePath, QCoreApplication& app)
{
    sara::AudioEngine engine;
    engine.initialize();

    auto* pipeline = engine.mainPipeline();
    bool gotPosition = false;
    bool gotMetadata = false;

    QObject::connect(pipeline, &sara::AudioPipeline::positionUpdated,
        [&gotPosition](int64_t pos, int64_t dur) {
            if (pos > 0 && dur > 0) gotPosition = true;
        });

    QObject::connect(pipeline, &sara::AudioPipeline::metadataReceived,
        [&gotMetadata](const sara::TrackMetadata&) {
            gotMetadata = true;
        });

    bool playOk = pipeline->play(filePath);
    assert(playOk && "Play should succeed");

    // Reproducir 3 segundos y luego verificar
    QTimer::singleShot(3000, [&]() {
        assert(pipeline->isPlaying() && "Should still be playing after 3s");
        assert(pipeline->positionMs() > 0 && "Position should advance");
        assert(pipeline->durationMs() > 0 && "Duration should be known");
        assert(gotPosition && "Should have received position updates");

        pipeline->pause();
        assert(pipeline->state() == sara::PlaybackState::Paused);

        pipeline->resume();
        assert(pipeline->state() == sara::PlaybackState::Playing);

        pipeline->stop();
        assert(pipeline->state() == sara::PlaybackState::Stopped);

        std::cout << "  ✓ playback (play/pause/resume/stop)\n";
        std::cout << "  ✓ position updates\n";
        if (gotMetadata) {
            std::cout << "  ✓ metadata received\n";
        } else {
            std::cout << "  ⚠ metadata not received (file may not have tags)\n";
        }

        app.quit();
    });
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    sara::initLogger("warn");

    std::cout << "\n── Test AudioEngine ───────────────────\n";

    testInitialization();
    testVolumeControl();
    testDeviceList();

    if (argc > 1) {
        QString filePath = QString::fromUtf8(argv[1]);
        std::cout << "  → Probando reproducción con: " << filePath.toStdString() << "\n";
        testPlayback(filePath, app);
        return app.exec();
    } else {
        std::cout << "  ⚠ Sin archivo de test (pase una ruta como argumento para probar reproducción)\n";
    }

    std::cout << "\n  Todos los tests pasaron ✓\n\n";
    return 0;
}
