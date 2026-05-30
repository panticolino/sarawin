#ifndef SARA_DATA_USER_MANAGER_H
#define SARA_DATA_USER_MANAGER_H

#include "core/types.h"
#include <QObject>
#include <QString>
#include <QVector>
#include <optional>

namespace sara {

class Database;

struct UserInfo {
    int       id = 0;
    QString   username;
    QString   displayName;
    UserRole  role = UserRole::Operation;
    ViewMode  defaultView = ViewMode::Full;
    QString   createdAt;
};

/**
 * Gestión de cuentas de usuario: creación, autenticación,
 * edición de perfil, y recuperación de PIN.
 *
 * PINs guardados como SHA-256(salt + pin).
 * Recovery token: token aleatorio guardado hasheado, el texto plano
 * se exporta a un archivo .key.
 */
class UserManager : public QObject
{
    Q_OBJECT

public:
    explicit UserManager(Database* db, QObject* parent = nullptr);

    // ── Consultas ──────────────────────────────────
    int userCount() const;
    bool hasAdmin() const;
    QVector<UserInfo> allUsers() const;
    std::optional<UserInfo> findUser(const QString& username) const;
    std::optional<UserInfo> findUserById(int id) const;

    // ── Autenticación ──────────────────────────────
    /// Verifica PIN. Retorna el UserInfo si es correcto.
    std::optional<UserInfo> authenticate(const QString& username, const QString& pin) const;

    // ── CRUD ───────────────────────────────────────
    /// Crear cuenta. Retorna el ID del nuevo registro, o -1 si falla.
    int createUser(const QString& username, const QString& displayName,
                   const QString& pin, UserRole role);

    /// Editar perfil (nombre visible y vista por defecto)
    bool updateProfile(int userId, const QString& displayName, ViewMode defaultView);

    /// Cambiar PIN
    bool changePin(int userId, const QString& newPin);

    /// Cambiar rol (solo admin)
    bool changeRole(int userId, UserRole newRole);

    /// Eliminar cuenta
    bool deleteUser(int userId);

    // ── Recovery ───────────────────────────────────
    /// Generar token de recuperación. Retorna el token en texto plano
    /// (para guardar en archivo). El hash se guarda en la BD.
    QString generateRecoveryToken(int userId);

    /// Verificar un token de recuperación
    bool verifyRecoveryToken(int userId, const QString& token) const;

    /// Resetear PIN usando token de recuperación
    bool resetPinWithToken(int userId, const QString& token, const QString& newPin);

private:
    static QString generateSalt();
    static QString hashPin(const QString& pin, const QString& salt);

    Database* db_;
};

} // namespace sara

#endif
