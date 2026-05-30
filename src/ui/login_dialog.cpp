#include "ui/login_dialog.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QInputDialog>

namespace sara {

LoginDialog::LoginDialog(UserManager* userMgr, QWidget* parent)
    : QDialog(parent), userMgr_(userMgr)
{
    setWindowTitle(tr("SARA Libre — Iniciar sesión"));
    setFixedSize(380, 280);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(24, 20, 24, 20);

    // Título
    auto* titleLabel = new QLabel(tr("Iniciar sesión"));
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 700;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(tr("Ingrese sus datos para operar SARA Libre"));
    subtitleLabel->setStyleSheet("font-size: 11px;");
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel);

    layout->addSpacing(8);

    // Formulario
    auto* form = new QFormLayout();
    form->setSpacing(8);

    usernameEdit_ = new QLineEdit();
    usernameEdit_->setPlaceholderText(tr("nombre de usuario"));
    usernameEdit_->setFixedHeight(32);
    form->addRow(tr("Usuario/a:"), usernameEdit_);

    pinEdit_ = new QLineEdit();
    pinEdit_->setPlaceholderText(tr("PIN numérico"));
    pinEdit_->setEchoMode(QLineEdit::Password);
    pinEdit_->setFixedHeight(32);
    form->addRow(tr("PIN:"), pinEdit_);

    layout->addLayout(form);

    // Error label
    errorLabel_ = new QLabel();
    errorLabel_->setStyleSheet("color: #ef4444; font-size: 11px;");
    errorLabel_->setAlignment(Qt::AlignCenter);
    errorLabel_->setVisible(false);
    layout->addWidget(errorLabel_);

    layout->addStretch();

    // Botones
    auto* btnRow = new QHBoxLayout();

    recoveryBtn_ = new QPushButton(tr("¿Olvidó su PIN?"));
    recoveryBtn_->setStyleSheet("background: transparent; color: #667eea; border: none; font-size: 11px;");
    recoveryBtn_->setCursor(Qt::PointingHandCursor);
    recoveryBtn_->setVisible(true);
    connect(recoveryBtn_, &QPushButton::clicked, this, &LoginDialog::showRecovery);
    btnRow->addWidget(recoveryBtn_);

    btnRow->addStretch();

    auto* skipBtn = new QPushButton(tr("Entrar sin sesión"));
    skipBtn->setToolTip(tr("SARA seguirá sonando pero los controles estarán deshabilitados"));
    connect(skipBtn, &QPushButton::clicked, this, [this]() {
        // Entrar sin autenticación — controles deshabilitados
        authenticatedUser_ = {};
        authenticatedUser_.id = 0;
        reject();  // reject = sin sesión
    });
    btnRow->addWidget(skipBtn);

    loginBtn_ = new QPushButton(tr("Iniciar sesión"));
    loginBtn_->setDefault(true);
    connect(loginBtn_, &QPushButton::clicked, this, &LoginDialog::attemptLogin);
    btnRow->addWidget(loginBtn_);

    layout->addLayout(btnRow);

    // Enter en campos
    connect(usernameEdit_, &QLineEdit::returnPressed, pinEdit_, [this]() { pinEdit_->setFocus(); });
    connect(pinEdit_, &QLineEdit::returnPressed, this, &LoginDialog::attemptLogin);

    usernameEdit_->setFocus();
}

void LoginDialog::attemptLogin()
{
    QString username = usernameEdit_->text().trimmed();
    QString pin = pinEdit_->text();

    if (username.isEmpty() || pin.isEmpty()) {
        errorLabel_->setText(tr("Complete ambos campos"));
        errorLabel_->setVisible(true);
        return;
    }

    auto user = userMgr_->authenticate(username, pin);
    if (user) {
        authenticatedUser_ = *user;
        accept();
    } else {
        failCount_++;
        errorLabel_->setText(tr("Usuario/a o PIN incorrecto (intento %1)").arg(failCount_));
        errorLabel_->setVisible(true);
        pinEdit_->clear();
        pinEdit_->setFocus();
    }
}

void LoginDialog::showRecovery()
{
    QString username = usernameEdit_->text().trimmed();
    if (username.isEmpty()) {
        QMessageBox::information(this, tr("Recuperar PIN"),
            tr("Escriba su nombre de usuario y luego presione \"¿Olvidó su PIN?\""));
        usernameEdit_->setFocus();
        return;
    }

    auto user = userMgr_->findUser(username);
    if (!user) {
        QMessageBox::warning(this, tr("Recuperar PIN"),
            tr("No se encontró la cuenta: %1").arg(username));
        return;
    }

    // Pedir archivo de recuperación
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("Seleccionar archivo de recuperación"),
        QDir::homePath(),
        tr("Archivo de recuperación (*.key *.txt);;Todos (*)"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"),
            tr("No se pudo leer el archivo"));
        return;
    }

    QTextStream in(&file);
    QString token = in.readAll().trimmed();
    file.close();

    if (!userMgr_->verifyRecoveryToken(user->id, token)) {
        QMessageBox::warning(this, tr("Error"),
            tr("El archivo de recuperación no es válido para esta cuenta."));
        return;
    }

    // Pedir nuevo PIN
    bool ok;
    QString newPin = QInputDialog::getText(this, tr("Nuevo PIN"),
        tr("Ingrese un nuevo PIN:"), QLineEdit::Password, "", &ok);
    if (!ok || newPin.isEmpty()) return;

    if (userMgr_->resetPinWithToken(user->id, token, newPin)) {
        QMessageBox::information(this, tr("PIN actualizado"),
            tr("Su PIN fue actualizado correctamente. Inicie sesión con el nuevo PIN."));
        pinEdit_->clear();
        pinEdit_->setFocus();
    } else {
        QMessageBox::warning(this, tr("Error"),
            tr("No se pudo actualizar el PIN."));
    }
}

} // namespace sara
