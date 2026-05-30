#include "util/file_scanner.h"
#include "util/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>

void testSupportedExtensions()
{
    auto exts = sara::FileScanner::supportedExtensions();
    assert(exts.contains("mp3"));
    assert(exts.contains("ogg"));
    assert(exts.contains("flac"));
    assert(exts.contains("wav"));
    assert(!exts.contains("txt"));
    assert(!exts.contains("jpg"));

    std::cout << "  ✓ supported extensions\n";
}

void testIsAudioFile()
{
    assert(sara::FileScanner::isAudioFile("/music/track.mp3"));
    assert(sara::FileScanner::isAudioFile("/music/track.OGG"));  // case insensitive
    assert(sara::FileScanner::isAudioFile("track.flac"));
    assert(!sara::FileScanner::isAudioFile("readme.txt"));
    assert(!sara::FileScanner::isAudioFile("photo.jpg"));
    assert(!sara::FileScanner::isAudioFile("no_extension"));

    std::cout << "  ✓ isAudioFile\n";
}

void testScanFolder()
{
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());

    // Crear estructura de test
    QString base = tmpDir.path();
    QDir().mkpath(base + "/subdir");

    // Crear archivos de prueba (vacíos, solo importa la extensión)
    QFile(base + "/track1.mp3").open(QIODevice::WriteOnly);
    QFile(base + "/track2.ogg").open(QIODevice::WriteOnly);
    QFile(base + "/track3.flac").open(QIODevice::WriteOnly);
    QFile(base + "/readme.txt").open(QIODevice::WriteOnly);
    QFile(base + "/subdir/track4.wav").open(QIODevice::WriteOnly);
    QFile(base + "/subdir/photo.jpg").open(QIODevice::WriteOnly);

    // Scan recursivo
    auto results = sara::FileScanner::scanFolder(base, true);
    assert(results.size() == 4 && "Should find 4 audio files recursively");

    // Scan no recursivo
    auto resultsFlat = sara::FileScanner::scanFolder(base, false);
    assert(resultsFlat.size() == 3 && "Should find 3 audio files non-recursively");

    // Carpeta inexistente
    auto resultsEmpty = sara::FileScanner::scanFolder("/nonexistent/path");
    assert(resultsEmpty.isEmpty() && "Nonexistent folder should return empty");

    std::cout << "  ✓ scanFolder (recursive + non-recursive)\n";
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    sara::initLogger("warn");

    std::cout << "\n── Test FileScanner ───────────────────\n";

    testSupportedExtensions();
    testIsAudioFile();
    testScanFolder();

    std::cout << "\n  Todos los tests pasaron ✓\n\n";
    return 0;
}
