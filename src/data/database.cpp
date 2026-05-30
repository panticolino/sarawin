#include "data/database.h"
#include "util/logger.h"

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QStandardPaths>
#include <QTextStream>

namespace sara {

Database::Database()
{
    // Ruta: ~/.config/saralibre/sara.db
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + "/saralibre";
    QDir().mkpath(configDir);
    dbPath_ = configDir + "/sara.db";
}

Database::~Database()
{
    close();
}

bool Database::open()
{
    if (db_.isOpen()) return true;

    db_ = QSqlDatabase::addDatabase("QSQLITE", "sara_main");
    db_.setDatabaseName(dbPath_);

    if (!db_.open()) {
        LOG_ERROR("[Database] No se pudo abrir: {}", db_.lastError().text().toStdString());
        return false;
    }

    // Activar WAL para escrituras seguras
    exec("PRAGMA journal_mode = WAL");
    exec("PRAGMA foreign_keys = ON");
    exec("PRAGMA synchronous = NORMAL");  // Buen balance rendimiento/seguridad

    LOG_INFO("[Database] Abierta en: {}", dbPath_.toStdString());

    // Verificar si necesita schema inicial
    if (schemaVersion() == 0) {
        LOG_INFO("[Database] Primera ejecución, aplicando schema...");
        if (!applySchema()) {
            LOG_ERROR("[Database] Error aplicando schema inicial");
            return false;
        }
    }

    // Ejecutar migraciones pendientes
    if (!runMigrations()) {
        LOG_WARN("[Database] Error en migraciones (la BD puede estar parcialmente actualizada)");
    }

    LOG_INFO("[Database] Schema versión: {}", schemaVersion());
    return true;
}

void Database::close()
{
    if (db_.isOpen()) {
        db_.close();
        LOG_INFO("[Database] Conexión cerrada");
    }
}

bool Database::isOpen() const
{
    return db_.isOpen();
}

QString Database::databasePath() const
{
    return dbPath_;
}

QSqlDatabase& Database::connection()
{
    return db_;
}

bool Database::exec(const QString& sql)
{
    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        LOG_ERROR("[Database] Error en exec: {} — SQL: {}",
                  query.lastError().text().toStdString(),
                  sql.left(200).toStdString());
        return false;
    }
    return true;
}

std::optional<QSqlQuery> Database::execPrepared(const QString& sql,
                                                  const QVariantList& bindings)
{
    QSqlQuery query(db_);
    query.prepare(sql);

    for (int i = 0; i < bindings.size(); ++i) {
        query.bindValue(i, bindings[i]);
    }

    if (!query.exec()) {
        LOG_ERROR("[Database] Error en query preparada: {} — SQL: {}",
                  query.lastError().text().toStdString(),
                  sql.left(200).toStdString());
        return std::nullopt;
    }

    return query;
}

bool Database::transaction(const std::function<bool()>& fn)
{
    if (!db_.transaction()) {
        LOG_ERROR("[Database] No se pudo iniciar transacción");
        return false;
    }

    if (fn()) {
        if (!db_.commit()) {
            LOG_ERROR("[Database] Error en commit: {}", db_.lastError().text().toStdString());
            db_.rollback();
            return false;
        }
        return true;
    } else {
        db_.rollback();
        return false;
    }
}

int Database::schemaVersion() const
{
    QSqlQuery query(db_);

    // Verificar si la tabla existe
    query.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'");
    if (!query.next()) {
        return 0;  // No existe: base de datos nueva
    }

    query.exec("SELECT MAX(version) FROM schema_version");
    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool Database::applySchema()
{
    // Leer schema desde recurso Qt embebido
    QFile schemaFile(":/sql/schema.sql");
    if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR("[Database] No se pudo leer schema.sql desde recursos");
        return false;
    }

    QString schemaSql = QTextStream(&schemaFile).readAll();
    schemaFile.close();

    // Paso 1: Eliminar líneas de comentario (-- ...) antes de split por ';'
    // Esto evita que un bloque "-- comentario\nCREATE TABLE..." sea
    // descartado por empezar con "--"
    QStringList lines = schemaSql.split('\n');
    QString cleanSql;
    for (const QString& line : lines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith("--")) continue;  // Omitir comentarios
        cleanSql += line + '\n';
    }

    // Paso 2: Separar por ';' y ejecutar cada sentencia
    QStringList statements = cleanSql.split(';', Qt::SkipEmptyParts);

    return transaction([&]() {
        for (const QString& stmt : statements) {
            QString trimmed = stmt.trimmed();
            if (trimmed.isEmpty()) continue;

            // Los PRAGMA ya se configuran en open() y no pueden
            // ejecutarse dentro de una transacción
            if (trimmed.startsWith("PRAGMA", Qt::CaseInsensitive)) continue;

            if (!exec(trimmed)) {
                LOG_ERROR("[Database] Error en sentencia schema: {}",
                          trimmed.left(200).toStdString());
                return false;
            }
        }
        LOG_INFO("[Database] Schema aplicado correctamente");
        return true;
    });
}

bool Database::runMigrations()
{
    int currentVer = schemaVersion();

    // v1 → v2: Agregar campos de vigencia a eventos, cambiar prioridad a integer
    if (currentVer < 2) {
        LOG_INFO("[Database] Migrando schema v1 → v2...");
        bool ok = transaction([&]() {
            // Agregar columnas a events (SQLite soporta ADD COLUMN)
            exec("ALTER TABLE events ADD COLUMN valid_from TEXT");
            exec("ALTER TABLE events ADD COLUMN valid_until TEXT");

            // Agregar columnas a event_elements
            exec("ALTER TABLE event_elements ADD COLUMN valid_from TEXT");
            exec("ALTER TABLE event_elements ADD COLUMN valid_until TEXT");

            // Cambiar prioridad de TEXT a INTEGER:
            // SQLite no soporta ALTER COLUMN, pero sí permite insertar
            // integers en columnas TEXT sin problema. Las nuevas filas
            // usarán el nuevo CHECK constraint del schema.sql.
            // Para filas existentes: convertir ALTA=8, MEDIA=5, BAJA=2
            exec("UPDATE events SET priority = 8 WHERE priority = 'ALTA'");
            exec("UPDATE events SET priority = 5 WHERE priority = 'MEDIA'");
            exec("UPDATE events SET priority = 2 WHERE priority = 'BAJA'");

            // Registrar versión
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (2)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v2 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v2");
            return false;
        }
    }

    // v2 → v3: Agregar campo use_ad_announce a eventos
    if (currentVer < 3) {
        LOG_INFO("[Database] Migrando schema v2 → v3...");
        bool ok = transaction([&]() {
            exec("ALTER TABLE events ADD COLUMN use_ad_announce INTEGER NOT NULL DEFAULT 0");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (3)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v3 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v3");
            return false;
        }
    }

    // v3 → v4: Expandir play_history para auditoría
    if (currentVer < 4) {
        LOG_INFO("[Database] Migrando schema v3 → v4...");
        bool ok = transaction([&]() {
            exec("ALTER TABLE play_history ADD COLUMN display_name TEXT");
            exec("ALTER TABLE play_history ADD COLUMN finished_at TEXT");
            exec("ALTER TABLE play_history ADD COLUMN pipeline TEXT DEFAULT 'main'");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (4)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v4 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v4");
            return false;
        }
    }

    // v4 → v5: Agregar duration_ms a event_elements (para streams)
    if (currentVer < 5) {
        LOG_INFO("[Database] Migrando schema v4 → v5...");
        bool ok = transaction([&]() {
            exec("ALTER TABLE event_elements ADD COLUMN duration_ms INTEGER DEFAULT 0");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (5)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v5 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v5");
            return false;
        }
    }

    // v5 → v6: Monitores de auditoría (Música Nacional, etc.)
    if (currentVer < 6) {
        LOG_INFO("[Database] Migrando schema v5 → v6...");
        bool ok = transaction([&]() {
            exec("CREATE TABLE IF NOT EXISTS audit_monitors ("
                 "id TEXT PRIMARY KEY, name TEXT NOT NULL, "
                 "created_at TEXT DEFAULT (datetime('now', 'localtime')))");
            exec("CREATE TABLE IF NOT EXISTS audit_monitor_folders ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "monitor_id TEXT NOT NULL REFERENCES audit_monitors(id) ON DELETE CASCADE, "
                 "folder_path TEXT NOT NULL, UNIQUE(monitor_id, folder_path))");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (6)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v6 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v6");
            return false;
        }
    }

    // v6 → v7: Pisadores individuales
    if (currentVer < 7) {
        LOG_INFO("[Database] Migrando schema v6 → v7...");
        bool ok = transaction([&]() {
            exec("CREATE TABLE IF NOT EXISTS pisador_assignments ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "file_path TEXT NOT NULL UNIQUE, "
                 "pisador_type TEXT NOT NULL DEFAULT 'specific', "
                 "pisador_path TEXT, "
                 "created_at TEXT DEFAULT (datetime('now', 'localtime')))");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (7)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v7 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v7");
            return false;
        }
    }

    // v7 → v8: Tiempo de espera de eventos + eventos vencidos
    if (currentVer < 8) {
        LOG_INFO("[Database] Migrando schema v7 → v8...");
        bool ok = transaction([&]() {
            exec("ALTER TABLE events ADD COLUMN max_wait_minutes INTEGER NOT NULL DEFAULT 0");
            exec("CREATE TABLE IF NOT EXISTS expired_events ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "event_id TEXT NOT NULL, "
                 "event_name TEXT NOT NULL, "
                 "scheduled_time TEXT NOT NULL, "
                 "expired_at TEXT DEFAULT (datetime('now', 'localtime')), "
                 "reason TEXT DEFAULT 'timeout')");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (8)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v8 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v8");
            return false;
        }
    }

    // v8 → v9: Stream presets
    if (currentVer < 9) {
        LOG_INFO("[Database] Migrando schema v8 → v9...");
        bool ok = transaction([&]() {
            exec("CREATE TABLE IF NOT EXISTS stream_presets ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "name TEXT NOT NULL, "
                 "url TEXT NOT NULL UNIQUE, "
                 "created_at TEXT DEFAULT (datetime('now', 'localtime')))");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (9)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v9 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v9");
            return false;
        }
    }

    // v9 → v10: Played events tracking
    if (currentVer < 10) {
        LOG_INFO("[Database] Migrando schema v9 → v10...");
        bool ok = transaction([&]() {
            exec("CREATE TABLE IF NOT EXISTS played_events ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "event_id TEXT NOT NULL, "
                 "slot_time TEXT NOT NULL, "
                 "play_date TEXT NOT NULL DEFAULT (date('now', 'localtime')), "
                 "played_at TEXT DEFAULT (datetime('now', 'localtime')), "
                 "UNIQUE(event_id, slot_time, play_date))");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (10)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v10 completada");
        } else {
            LOG_ERROR("[Database] Error en migración v10");
            return false;
        }
    }

    if (currentVer < 11) {
        LOG_INFO("[Database] Migrando schema v10 → v11...");
        bool ok = transaction([&]() {
            exec("CREATE TABLE IF NOT EXISTS users ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "username TEXT NOT NULL UNIQUE, "
                 "display_name TEXT NOT NULL, "
                 "pin_hash TEXT NOT NULL, "
                 "salt TEXT NOT NULL, "
                 "role INTEGER NOT NULL DEFAULT 0, "
                 "recovery_token TEXT, "
                 "default_view INTEGER NOT NULL DEFAULT 1, "
                 "created_at TEXT DEFAULT (datetime('now', 'localtime')))");
            exec("INSERT OR REPLACE INTO schema_version (version) VALUES (11)");
            return true;
        });
        if (ok) {
            LOG_INFO("[Database] Migración v11 completada (tabla users)");
        } else {
            LOG_ERROR("[Database] Error en migración v11");
            return false;
        }
    }

    return true;
}

} // namespace sara
