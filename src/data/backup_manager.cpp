#include "data/backup_manager.h"
#include "util/logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QTextStream>
#include <algorithm>

namespace sara {

BackupManager::BackupManager(QObject* parent)
    : QObject(parent)
{
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() {
        if (enabled_) {
            if (createBackup()) {
                LOG_INFO("[Backup] Backup automático completado");
            }
        }
    });
}

QString BackupManager::backupDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                  + "/saralibre/backups";
    QDir().mkpath(dir);
    return dir;
}

void BackupManager::setEnabled(bool on)
{
    enabled_ = on;
    if (!on && timer_->isActive()) {
        timer_->stop();
    }
}

void BackupManager::setIntervalHours(int hours)
{
    intervalHours_ = hours;
    if (timer_->isActive()) {
        timer_->setInterval(intervalHours_ * 3600 * 1000);
    }
}

void BackupManager::start()
{
    if (!enabled_) return;

    // Verificar si ya pasó el intervalo desde el último backup
    auto backups = availableBackups();
    bool needsBackup = true;
    if (!backups.isEmpty()) {
        int hoursSince = backups.first().created.secsTo(QDateTime::currentDateTime()) / 3600;
        needsBackup = (hoursSince >= intervalHours_);
    }

    if (needsBackup) {
        createBackup();
    }

    timer_->start(intervalHours_ * 3600 * 1000);
    LOG_INFO("[Backup] Backup automático activado (cada {}h, máx {} copias)",
             intervalHours_, maxBackups_);
}

bool BackupManager::createBackup()
{
    if (dbPath_.isEmpty()) {
        emit backupFailed("Ruta de base de datos no configurada");
        return false;
    }

    QString dir = backupDir();
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm");
    QString backupFile = dir + "/backup_" + timestamp + ".tar.gz";

    // Verificar que no exista ya (evitar duplicados si se llama dos veces rápido)
    if (QFile::exists(backupFile)) {
        LOG_DEBUG("[Backup] Ya existe backup para este minuto, omitiendo");
        return true;
    }

    // Crear directorio temporal con los archivos a respaldar
    QString tmpDir = dir + "/tmp_backup";
    QDir().mkpath(tmpDir);

    bool ok = true;

    // Copiar DB
    if (QFile::exists(dbPath_)) {
        ok &= safeCopy(dbPath_, tmpDir + "/saralibre.db");
        // También copiar WAL y SHM si existen (SQLite WAL mode)
        if (QFile::exists(dbPath_ + "-wal"))
            safeCopy(dbPath_ + "-wal", tmpDir + "/saralibre.db-wal");
        if (QFile::exists(dbPath_ + "-shm"))
            safeCopy(dbPath_ + "-shm", tmpDir + "/saralibre.db-shm");
    }

    // Copiar config
    if (!configPath_.isEmpty() && QFile::exists(configPath_)) {
        ok &= safeCopy(configPath_, tmpDir + "/config.toml");
    }

    if (!ok) {
        // Limpiar
        QDir(tmpDir).removeRecursively();
        emit backupFailed("Error copiando archivos para backup");
        return false;
    }

    // Empaquetar con tar
    QProcess tar;
    tar.setWorkingDirectory(dir);
    tar.start("tar", {"czf", backupFile, "-C", dir, "tmp_backup"});
    tar.waitForFinished(30000);

    // Limpiar directorio temporal
    QDir(tmpDir).removeRecursively();

    if (tar.exitCode() != 0) {
        QString err = tar.readAllStandardError();
        LOG_ERROR("[Backup] Error creando tar: {}", err.toStdString());
        emit backupFailed("Error empaquetando backup: " + err);
        return false;
    }

    LOG_INFO("[Backup] Backup creado: {}", backupFile.toStdString());
    emit backupCreated(backupFile);

    // Rotar backups viejos
    rotateBackups();

    return true;
}

bool BackupManager::exportTo(const QString& outputPath)
{
    if (dbPath_.isEmpty()) return false;

    // Crear directorio temporal
    QString tmpDir = backupDir() + "/tmp_export";
    QDir().mkpath(tmpDir);

    bool ok = true;

    // Copiar DB
    if (QFile::exists(dbPath_)) {
        ok &= safeCopy(dbPath_, tmpDir + "/saralibre.db");
    }

    // Copiar config
    if (!configPath_.isEmpty() && QFile::exists(configPath_)) {
        ok &= safeCopy(configPath_, tmpDir + "/config.toml");
    }

    // Crear archivo de metadatos
    QFile meta(tmpDir + "/export_info.txt");
    if (meta.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&meta);
        ts << "SARA Libre Export\n";
        ts << "Version: 0.1.0\n";
        ts << "Date: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        ts << "Schema: 7\n";
        meta.close();
    }

    if (!ok) {
        QDir(tmpDir).removeRecursively();
        return false;
    }

    // Empaquetar
    QProcess tar;
    tar.setWorkingDirectory(backupDir());
    tar.start("tar", {"czf", outputPath, "-C", backupDir(), "tmp_export"});
    tar.waitForFinished(30000);

    QDir(tmpDir).removeRecursively();

    if (tar.exitCode() != 0) {
        LOG_ERROR("[Backup] Error exportando: {}", tar.readAllStandardError().toStdString());
        return false;
    }

    LOG_INFO("[Backup] Exportado a: {}", outputPath.toStdString());
    return true;
}

bool BackupManager::importFrom(const QString& inputPath)
{
    if (!QFile::exists(inputPath)) return false;

    // Primero: crear backup de seguridad de lo actual
    createBackup();

    // Extraer a directorio temporal
    QString tmpDir = backupDir() + "/tmp_import";
    QDir().mkpath(tmpDir);

    QProcess tar;
    tar.start("tar", {"xzf", inputPath, "-C", tmpDir, "--strip-components=1"});
    tar.waitForFinished(30000);

    if (tar.exitCode() != 0) {
        LOG_ERROR("[Backup] Error extrayendo import: {}",
                  tar.readAllStandardError().toStdString());
        QDir(tmpDir).removeRecursively();
        return false;
    }

    bool ok = true;

    // Restaurar DB
    QString importedDb = tmpDir + "/saralibre.db";
    if (QFile::exists(importedDb) && !dbPath_.isEmpty()) {
        // Eliminar WAL/SHM actuales para evitar conflictos
        QFile::remove(dbPath_ + "-wal");
        QFile::remove(dbPath_ + "-shm");
        ok &= safeCopy(importedDb, dbPath_);
    }

    // Restaurar config
    QString importedConfig = tmpDir + "/config.toml";
    if (QFile::exists(importedConfig) && !configPath_.isEmpty()) {
        ok &= safeCopy(importedConfig, configPath_);
    }

    QDir(tmpDir).removeRecursively();

    if (ok) {
        LOG_INFO("[Backup] Importación completada desde: {}", inputPath.toStdString());
        emit restoreCompleted();
    }
    return ok;
}

bool BackupManager::restoreBackup(const QString& backupPath)
{
    return importFrom(backupPath);
}

QList<BackupManager::BackupInfo> BackupManager::availableBackups() const
{
    QList<BackupInfo> result;
    QDir dir(backupDir());

    for (const auto& fi : dir.entryInfoList({"backup_*.tar.gz"}, QDir::Files, QDir::Time)) {
        BackupInfo info;
        info.filePath = fi.absoluteFilePath();
        info.fileName = fi.fileName();
        info.created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
        info.sizeBytes = fi.size();
        result.append(info);
    }

    return result;  // Ya ordenado por tiempo (más reciente primero)
}

void BackupManager::rotateBackups()
{
    auto backups = availableBackups();
    while (backups.size() > maxBackups_) {
        // Eliminar el más viejo (último de la lista, ordenada por tiempo desc)
        QString oldest = backups.last().filePath;
        if (QFile::remove(oldest)) {
            LOG_INFO("[Backup] Rotación: eliminado {}", QFileInfo(oldest).fileName().toStdString());
        }
        backups.removeLast();
    }
}

bool BackupManager::safeCopy(const QString& src, const QString& dst)
{
    // Eliminar destino si existe
    if (QFile::exists(dst)) {
        QFile::remove(dst);
    }
    if (!QFile::copy(src, dst)) {
        LOG_ERROR("[Backup] Error copiando {} → {}", src.toStdString(), dst.toStdString());
        return false;
    }
    return true;
}

} // namespace sara
