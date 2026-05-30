#ifndef SARA_UI_LOGIN_DIALOG_H
#define SARA_UI_LOGIN_DIALOG_H

#include "core/types.h"
#include "data/user_manager.h"
#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <optional>

namespace sara {

/**
 * Diálogo de inicio de sesión.
 * Se muestra al iniciar si hay más de 1 usuario registrado.
 * Si solo hay 1, se autentica automáticamente (sin PIN, login implícito).
 */
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(UserManager* userMgr, QWidget* parent = nullptr);

    /// El usuario autenticado (válido después de accept())
    UserInfo authenticatedUser() const { return authenticatedUser_; }

private:
    void attemptLogin();
    void showRecovery();

    UserManager* userMgr_;
    UserInfo     authenticatedUser_;

    QLineEdit*   usernameEdit_;
    QLineEdit*   pinEdit_;
    QLabel*      errorLabel_;
    QPushButton* loginBtn_;
    QPushButton* recoveryBtn_ = nullptr;
    int          failCount_ = 0;
};

} // namespace sara

#endif
