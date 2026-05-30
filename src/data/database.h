#ifndef SARA_DATA_DATABASE_H
#define SARA_DATA_DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <functional>
#include <optional>

namespace sara {

/**
 * Gestión de la base de datos SQLite.
 *
 * - Crea el archivo en ~/.config/saralibre/sara.db
 * - Ejecuta el schema inicial en el primer arranque
 * - Soporte de migraciones por versión
 * - Modo WAL para escrituras seguras (tolerante a cortes de luz)
 */
class Database
{
public:
    Database();
    ~Database();

    // No copiable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Abrir/crear la base de datos. Retorna false si hay error.
    bool open();

    /// Cerrar la conexión
    void close();

    /// ¿Está abierta?
    bool isOpen() const;

    /// Ruta del archivo .db
    QString databasePath() const;

    /// Acceso directo a la conexión (para los repositorios)
    QSqlDatabase& connection();

    /// Ejecutar una query simple sin resultado
    bool exec(const QString& sql);

    /// Ejecutar una query preparada con bindings, retorna el QSqlQuery
    std::optional<QSqlQuery> execPrepared(const QString& sql,
                                           const QVariantList& bindings = {});

    /// Transacción: ejecuta fn dentro de BEGIN/COMMIT. Rollback si fn retorna false.
    bool transaction(const std::function<bool()>& fn);

    /// Versión actual del esquema
    int schemaVersion() const;

private:
    bool applySchema();
    bool runMigrations();

    QSqlDatabase db_;
    QString      dbPath_;
};

} // namespace sara

#endif // SARA_DATA_DATABASE_H
