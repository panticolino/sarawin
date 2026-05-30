#include "data/user_manager.h"
#include "data/database.h"
#include "util/logger.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUuid>

namespace sara {

UserManager::UserManager(Database* db, QObject* parent)
    : QObject(parent), db_(db)
{
}

// ── Helpers ─────────────────────────────────────────────

QString UserManager::generateSalt()
{
    QByteArray bytes(16, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32*>(bytes.data()), bytes.size() / 4);
    return bytes.toHex();
}

QString UserManager::hashPin(const QString& pin, const QString& salt)
{
    QByteArray data = (salt + pin).toUtf8();
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// ── Consultas ───────────────────────────────────────────

int UserManager::userCount() const
{
    auto q = db_->execPrepared("SELECT COUNT(*) FROM users", {});
    if (q && q->next()) return q->value(0).toInt();
    return 0;
}

bool UserManager::hasAdmin() const
{
    auto q = db_->execPrepared(
        "SELECT COUNT(*) FROM users WHERE role = ?",
        {static_cast<int>(UserRole::Admin)});
    if (q && q->next()) return q->value(0).toInt() > 0;
    return false;
}

QVector<UserInfo> UserManager::allUsers() const
{
    QVector<UserInfo> result;
    auto q = db_->execPrepared(
        "SELECT id, username, display_name, role, default_view, created_at "
        "FROM users ORDER BY role DESC, display_name ASC", {});
    if (!q) return result;

    while (q->next()) {
        UserInfo u;
        u.id = q->value(0).toInt();
        u.username = q->value(1).toString();
        u.displayName = q->value(2).toString();
        u.role = static_cast<UserRole>(q->value(3).toInt());
        u.defaultView = static_cast<ViewMode>(q->value(4).toInt());
        u.createdAt = q->value(5).toString();
        result.append(u);
    }
    return result;
}

std::optional<UserInfo> UserManager::findUser(const QString& username) const
{
    auto q = db_->execPrepared(
        "SELECT id, username, display_name, role, default_view, created_at "
        "FROM users WHERE username = ?", {username});
    if (!q || !q->next()) return std::nullopt;

    UserInfo u;
    u.id = q->value(0).toInt();
    u.username = q->value(1).toString();
    u.displayName = q->value(2).toString();
    u.role = static_cast<UserRole>(q->value(3).toInt());
    u.defaultView = static_cast<ViewMode>(q->value(4).toInt());
    u.createdAt = q->value(5).toString();
    return u;
}

std::optional<UserInfo> UserManager::findUserById(int id) const
{
    auto q = db_->execPrepared(
        "SELECT id, username, display_name, role, default_view, created_at "
        "FROM users WHERE id = ?", {id});
    if (!q || !q->next()) return std::nullopt;

    UserInfo u;
    u.id = q->value(0).toInt();
    u.username = q->value(1).toString();
    u.displayName = q->value(2).toString();
    u.role = static_cast<UserRole>(q->value(3).toInt());
    u.defaultView = static_cast<ViewMode>(q->value(4).toInt());
    u.createdAt = q->value(5).toString();
    return u;
}

// ── Autenticación ───────────────────────────────────────

std::optional<UserInfo> UserManager::authenticate(const QString& username, const QString& pin) const
{
    auto q = db_->execPrepared(
        "SELECT id, username, display_name, pin_hash, salt, role, default_view, created_at "
        "FROM users WHERE username = ?", {username});
    if (!q || !q->next()) return std::nullopt;

    QString storedHash = q->value(3).toString();
    QString salt = q->value(4).toString();
    QString computedHash = hashPin(pin, salt);

    if (storedHash != computedHash) return std::nullopt;

    UserInfo u;
    u.id = q->value(0).toInt();
    u.username = q->value(1).toString();
    u.displayName = q->value(2).toString();
    u.role = static_cast<UserRole>(q->value(5).toInt());
    u.defaultView = static_cast<ViewMode>(q->value(6).toInt());
    u.createdAt = q->value(7).toString();

    LOG_INFO("[UserManager] Sesión iniciada: {} ({})", 
             u.displayName.toStdString(),
             u.role == UserRole::Admin ? "Administración" :
             u.role == UserRole::Programming ? "Programación" : "Operación");

    return u;
}

// ── CRUD ────────────────────────────────────────────────

int UserManager::createUser(const QString& username, const QString& displayName,
                            const QString& pin, UserRole role)
{
    // Verificar duplicado
    auto existing = findUser(username);
    if (existing) {
        LOG_WARN("[UserManager] Ya existe una cuenta con usuario: {}", username.toStdString());
        return -1;
    }

    QString salt = generateSalt();
    QString pinHash = hashPin(pin, salt);

    auto q = db_->execPrepared(
        "INSERT INTO users (username, display_name, pin_hash, salt, role, default_view) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        {username, displayName, pinHash, salt,
         static_cast<int>(role),
         static_cast<int>(role == UserRole::Operation ? ViewMode::Operation : ViewMode::Full)});

    if (!q) return -1;

    // Obtener el ID del nuevo registro
    auto idq = db_->execPrepared("SELECT last_insert_rowid()", {});
    int newId = (idq && idq->next()) ? idq->value(0).toInt() : -1;

    LOG_INFO("[UserManager] Cuenta creada: {} ({}) — rol: {}", 
             displayName.toStdString(), username.toStdString(),
             role == UserRole::Admin ? "Administración" :
             role == UserRole::Programming ? "Programación" : "Operación");

    return newId;
}

bool UserManager::updateProfile(int userId, const QString& displayName, ViewMode defaultView)
{
    auto q = db_->execPrepared(
        "UPDATE users SET display_name = ?, default_view = ? WHERE id = ?",
        {displayName, static_cast<int>(defaultView), userId});
    return q.has_value();
}

bool UserManager::changePin(int userId, const QString& newPin)
{
    QString salt = generateSalt();
    QString pinHash = hashPin(newPin, salt);

    auto q = db_->execPrepared(
        "UPDATE users SET pin_hash = ?, salt = ? WHERE id = ?",
        {pinHash, salt, userId});
    return q.has_value();
}

bool UserManager::changeRole(int userId, UserRole newRole)
{
    auto q = db_->execPrepared(
        "UPDATE users SET role = ? WHERE id = ?",
        {static_cast<int>(newRole), userId});
    return q.has_value();
}

bool UserManager::deleteUser(int userId)
{
    auto q = db_->execPrepared("DELETE FROM users WHERE id = ?", {userId});
    return q.has_value();
}

// ── Recovery ────────────────────────────────────────────

QString UserManager::generateRecoveryToken(int userId)
{
    // Generar token legible: 4 grupos de 4 caracteres alfanuméricos
    QString token;
    const QString chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // Sin I, O, 0, 1
    for (int g = 0; g < 4; ++g) {
        if (g > 0) token += "-";
        for (int i = 0; i < 4; ++i) {
            token += chars[QRandomGenerator::global()->bounded(chars.size())];
        }
    }

    // Guardar hash del token en BD
    QString tokenHash = QCryptographicHash::hash(
        token.toUtf8(), QCryptographicHash::Sha256).toHex();

    db_->execPrepared(
        "UPDATE users SET recovery_token = ? WHERE id = ?",
        {tokenHash, userId});

    LOG_INFO("[UserManager] Token de recuperación generado para usuario ID {}", userId);
    return token;
}

bool UserManager::verifyRecoveryToken(int userId, const QString& token) const
{
    auto q = db_->execPrepared(
        "SELECT recovery_token FROM users WHERE id = ?", {userId});
    if (!q || !q->next()) return false;

    QString storedHash = q->value(0).toString();
    if (storedHash.isEmpty()) return false;

    QString tokenHash = QCryptographicHash::hash(
        token.toUpper().remove('-').toUtf8(), QCryptographicHash::Sha256).toHex();

    // Recomputar con el formato original (con guiones y mayúsculas)
    QString tokenNorm = token.toUpper().trimmed();
    QString hash2 = QCryptographicHash::hash(
        tokenNorm.toUtf8(), QCryptographicHash::Sha256).toHex();

    return storedHash == hash2;
}

bool UserManager::resetPinWithToken(int userId, const QString& token, const QString& newPin)
{
    if (!verifyRecoveryToken(userId, token)) return false;
    return changePin(userId, newPin);
}

} // namespace sara
