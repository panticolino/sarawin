#ifndef SARA_DATA_BACKUP_MANAGER_H
#define SARA_DATA_BACKUP_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QDateTime>

namespace sara {

/**
 * Gestor de backups automáticos y exportación/importación.
 *
 * Backup automático: copia DB + config cada N horas con rotación.
 * Exportación: empaqueta DB + config en .sara (tar.gz) para portabilidad.
 * Importación: restaura desde .sara o backup local.
 */
class BackupManager : public QObject
{
    Q_OBJECT

public:
    explicit BackupManager(QObject* parent = nullptr);

    /// Configurar rutas de los archivos a respaldar
    void setDatabasePath(const QString& dbPath) { dbPath_ = dbPath; }
    void setConfigPath(const QString& configPath) { configPath_ = configPath; }

    /// Configurar backup automático
    void setEnabled(bool on);
    void setIntervalHours(int hours);
    void setMaxBackups(int count) { maxBackups_ = count; }

    /// Iniciar el timer de backup automático
    void start();

    /// Crear backup manualmente (desde menú o al cerrar)
    bool createBackup();

    /// Exportar a archivo .sara (DB + config empaquetados)
    bool exportTo(const QString& outputPath);

    /// Importar desde archivo .sara o backup
    /// Retorna true si la importación fue exitosa
    bool importFrom(const QString& inputPath);

    /// Listar backups disponibles (más reciente primero)
    struct BackupInfo {
        QString filePath;
        QString fileName;
        QDateTime created;
        qint64  sizeBytes;
    };
    QList<BackupInfo> availableBackups() const;

    /// Restaurar un backup específico
    bool restoreBackup(const QString& backupPath);

    /// Directorio de backups
    QString backupDir() const;

signals:
    /// Señal para notificar a la UI
    void backupCreated(const QString& path);
    void backupFailed(const QString& error);
    void restoreCompleted();

private:
    /// Rotar backups viejos (eliminar los que exceden maxBackups_)
    void rotateBackups();

    /// Copiar archivo de forma segura
    bool safeCopy(const QString& src, const QString& dst);

    QString dbPath_;
    QString configPath_;
    bool    enabled_ = true;
    int     intervalHours_ = 24;
    int     maxBackups_ = 7;
    QTimer* timer_ = nullptr;
};

} // namespace sara

#endif
