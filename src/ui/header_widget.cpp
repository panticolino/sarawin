#include "ui/header_widget.h"
#include "ui/vumeter_widget.h"
#include "audio/audio_engine.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTime>
#include <QDateTime>
#include <QLocale>
#include <QIcon>
#include <QFrame>
#include <QStyle>
#include <QPainter>
#include <QPixmap>

namespace sara {

// ── Helper: tintar ícono SVG a un color ─────────────────
static QIcon tintIcon(const QString& path, const QColor& color)
{
    QPixmap pix = QIcon(path).pixmap(64, 64);
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pix.rect(), color);
    p.end();
    return QIcon(pix);
}

// ── Helper: crear botón circular ────────────────────────
static QPushButton* makeCircleButton(const QString& iconPath, int size,
                                      const QColor& iconColor = Qt::white)
{
    auto* btn = new QPushButton(tintIcon(iconPath, iconColor), "");
    int iconSz = static_cast<int>(size * 0.5);
    btn->setIconSize(QSize(iconSz, iconSz));
    btn->setFixedSize(size, size);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QString(
        "QPushButton { background: #20202c; border: none; border-radius: %1px; }"
        "QPushButton:hover { background: #2a2a3c; }"
        "QPushButton:pressed { background: #333345; }"
        "QPushButton:disabled { opacity: 0.4; }"
    ).arg(size / 2));
    return btn;
}

HeaderWidget::HeaderWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("headerWidget");
    setFixedHeight(80);
    setupUI();
    clockTimer_ = new QTimer(this);
    clockTimer_->setInterval(1000);
    connect(clockTimer_, &QTimer::timeout, this, &HeaderWidget::updateClock);
    clockTimer_->start();
    updateClock();
}

void HeaderWidget::setupUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(0);  // Control manual entre grupos

    // ════════════════════════════════════════════════════
    // ZONA 1: Branding
    // ════════════════════════════════════════════════════
    auto* brandLayout = new QVBoxLayout();
    brandLayout->setSpacing(1);

    saraLabel_ = new QLabel(tr("SARA LIBRE V1"));
    saraLabel_->setStyleSheet(
        "font-size: 12px; font-weight: 600; letter-spacing: 2px; "
        "color: rgba(103, 123, 230,1);");

    radioNameLabel_ = new QLabel(tr("Mi Radio"));
    radioNameLabel_->setStyleSheet(
        "font-size: 20px; font-weight: 700; color: #e0e0f0;");

    brandLayout->addWidget(saraLabel_);
    brandLayout->addWidget(radioNameLabel_);
    mainLayout->addLayout(brandLayout);

    mainLayout->addStretch(1);

    // ════════════════════════════════════════════════════
    // ZONA 2: Volumen + Talkover (agrupados)
    // ════════════════════════════════════════════════════
    auto* volGroup = new QHBoxLayout();
    volGroup->setSpacing(10);

    auto* volIcon = new QLabel();
    volIcon->setPixmap(tintIcon(":/icons/volume-2.svg", QColor("#677be6")).pixmap(26, 26));

    volumeSlider_ = new QSlider(Qt::Horizontal);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(95);
    volumeSlider_->setFixedWidth(160);
    volumeSlider_->setFixedHeight(22);
    volumeSlider_->setToolTip(tr("Volumen master"));

    volumeLabel_ = new QLabel("90%");
    volumeLabel_->setFixedWidth(52);
    volumeLabel_->setStyleSheet("font-size: 16px; font-weight: 600; color: #e0e0f0;");

    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int val) {
        volumeLabel_->setText(QString("%1%").arg(val));
        emit volumeChanged(val);
    });

    talkoverButton_ = makeCircleButton(":/icons/mic.svg", 44);
    talkoverButton_->setToolTip(tr("Hablar encima (bajar música mientras habla)"));
    talkoverButton_->setCheckable(true);
    connect(talkoverButton_, &QPushButton::toggled, this, [this](bool on) {
        talkoverActive_ = on;
        if (on) {
            talkoverButton_->setStyleSheet(
                "QPushButton { background: rgba(245,158,11,0.3); border: 2px solid #f59e0b; "
                "border-radius: 22px; }"
                "QPushButton:hover { background: rgba(245,158,11,0.4); }");
        } else {
            talkoverButton_->setStyleSheet(
                "QPushButton { background: #20202c; border: none; border-radius: 22px; }"
                "QPushButton:hover { background: #2a2a3c; }");
        }
        int from = volumeSlider_->value();
        int to = on ? talkoverLevel_ : presetVolume_;
        if (on) presetVolume_ = from;

        auto* fadeTimer = new QTimer(this);
        int step = 0;
        const int totalSteps = 12;
        connect(fadeTimer, &QTimer::timeout, this, [=]() mutable {
            step++;
            double t = static_cast<double>(step) / totalSteps;
            int vol = from + static_cast<int>((to - from) * t);
            volumeSlider_->setValue(vol);
            if (step >= totalSteps) {
                volumeSlider_->setValue(to);
                fadeTimer->stop();
                fadeTimer->deleteLater();
            }
        });
        fadeTimer->start(25);
        emit talkoverChanged(on);
    });

    volGroup->addWidget(volIcon);
    volGroup->addWidget(volumeSlider_);
    volGroup->addWidget(volumeLabel_);
    volGroup->addWidget(talkoverButton_);
    mainLayout->addLayout(volGroup);

    // VU Meter (movido a las columnas)
    vuMeter_ = new VuMeterWidget(this);
    vuMeter_->setVisible(false);

    mainLayout->addStretch(1);

    // ════════════════════════════════════════════════════
    // ZONA 3: AUTO (prominente)
    // ════════════════════════════════════════════════════
    autoModeButton_ = new QPushButton(
        tintIcon(":/icons/radio.svg", Qt::white), " AUTO");
    autoModeButton_->setObjectName("autoModeButton");
    autoModeButton_->setCheckable(true);
    autoModeButton_->setChecked(true);
    autoModeButton_->setToolTip(tr("Modo Automático / Manual"));
    autoModeButton_->setIconSize(QSize(20, 20));
    autoModeButton_->setFixedHeight(44);
    autoModeButton_->setMinimumWidth(180);
    autoModeButton_->setCursor(Qt::PointingHandCursor);
    autoModeButton_->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2); color: white; border: none; "
        "border-radius: 10px; font-weight: 700; font-size: 15px; "
        "padding: 0 24px; letter-spacing: 1px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #7c94f4, stop:1 #8b5fc4); }"
        "QPushButton:checked { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2); }"
        "QPushButton:!checked { background: rgba(255,255,255,0.08); "
        "color: #aaa; font-size: 15px; border: 1px solid #FFFFFF; }");

    connect(autoModeButton_, &QPushButton::toggled, this, [this](bool on) {
        autoModeButton_->setText(on ? " AUTO" : " MANUAL");
        autoModeButton_->style()->unpolish(autoModeButton_);
        autoModeButton_->style()->polish(autoModeButton_);
        emit autoModeToggled(on);
    });
    mainLayout->addWidget(autoModeButton_);

    mainLayout->addStretch(1);

    // ════════════════════════════════════════════════════
    // ZONA 4: Fecha + Reloj (agrupados)
    // ════════════════════════════════════════════════════
    auto* clockGroup = new QHBoxLayout();
    clockGroup->setSpacing(10);

    dateLabel_ = new QLabel();
    dateLabel_->setStyleSheet(
        "font-size: 16px; font-weight: 500; color: rgba(255,255,255,0.7);");

    clockLabel_ = new QPushButton("00:00:00");
    clockLabel_->setObjectName("clockLabel");
    clockLabel_->setCursor(Qt::PointingHandCursor);
    clockLabel_->setFlat(true);
    clockLabel_->setToolTip(tr("Clic para reproducir la hora actual"));
    clockLabel_->setStyleSheet(
        "QPushButton { font-size: 20px; font-weight: 700; color: #e0e0f0; "
        "background: rgba(102,126,234,0.15); border: 1px solid rgba(102,126,234,0.25); "
        "border-radius: 10px; padding: 10px 35px; margin-left:20px;"
        "font-family: 'Ubuntu Mono', 'Courier New', monospace; }"
        "QPushButton:hover { background: rgba(102,126,234,0.25); }");
    connect(clockLabel_, &QPushButton::clicked, this, &HeaderWidget::clockClicked);

    clockGroup->addWidget(dateLabel_);
    clockGroup->addWidget(clockLabel_);
    mainLayout->addLayout(clockGroup);

    mainLayout->addStretch(1);

    // ════════════════════════════════════════════════════
    // ZONA 5: Herramientas (agrupadas)
    // ════════════════════════════════════════════════════
    auto* toolsGroup = new QHBoxLayout();
    toolsGroup->setSpacing(6);
    int btnSz = 40;

    recordBtn_ = makeCircleButton(":/icons/circle.svg", btnSz, QColor("#ef4444"));
    recordBtn_->setToolTip(tr("Grabar programación al aire"));
    recordBtn_->setCheckable(true);
    connect(recordBtn_, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            recordBtn_->setStyleSheet(
                "QPushButton { background: #ef4444; border: none; border-radius: 20px; }"
                "QPushButton:hover { background: #f87171; }");
        } else {
            recordBtn_->setStyleSheet(
                "QPushButton { background: #20202c; border: none; border-radius: 20px; }"
                "QPushButton:hover { background: #2a2a3c; }");
        }
        emit recordToggled(on);
    });
    toolsGroup->addWidget(recordBtn_);

    auto* eqBtn = makeCircleButton(":/icons/sliders-horizontal.svg", btnSz);
    eqBtn->setToolTip(tr("Ecualizador / Compresor"));
    connect(eqBtn, &QPushButton::clicked, this, &HeaderWidget::eqClicked);
    toolsGroup->addWidget(eqBtn);

    viewToggleBtn_ = makeCircleButton(":/icons/layout-dashboard.svg", btnSz);
    viewToggleBtn_->setToolTip(tr("Alternar: Modo operación / Ver todo"));
    viewToggleBtn_->setCheckable(true);
    connect(viewToggleBtn_, &QPushButton::clicked, this, &HeaderWidget::viewToggled);
    toolsGroup->addWidget(viewToggleBtn_);

    settingsButton_ = makeCircleButton(":/icons/settings.svg", btnSz);
    settingsButton_->setToolTip(tr("Configuración"));
    connect(settingsButton_, &QPushButton::clicked, this, &HeaderWidget::settingsClicked);
    toolsGroup->addWidget(settingsButton_);

    auditButton_ = makeCircleButton(":/icons/scroll-text.svg", btnSz);
    auditButton_->setToolTip(tr("Auditoría y Reportes"));
    connect(auditButton_, &QPushButton::clicked, this, &HeaderWidget::auditClicked);
    toolsGroup->addWidget(auditButton_);

    userBtn_ = new QPushButton(tintIcon(":/icons/user.svg", Qt::white), tr("Iniciar sesión"));
    userBtn_->setFixedHeight(36);
    userBtn_->setMinimumWidth(120);
    userBtn_->setCursor(Qt::PointingHandCursor);
    userBtn_->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15); "
        "border-radius: 18px; padding: 0 14px; font-size: 12px; font-weight: 600; color: #e0e0f0; }"
        "QPushButton:hover { background: rgba(255,255,255,0.15); }");
    connect(userBtn_, &QPushButton::clicked, this, &HeaderWidget::userMenuClicked);
    toolsGroup->addWidget(userBtn_);

    mainLayout->addLayout(toolsGroup);
}

void HeaderWidget::setRadioName(const QString& name)
{
    radioNameLabel_->setText(name);
}

void HeaderWidget::setAudioEngine(AudioEngine* engine)
{
    audioEngine_ = engine;
}

void HeaderWidget::setAutoMode(bool on)
{
    autoModeButton_->setChecked(on);
}

void HeaderWidget::updateClock()
{
    QDateTime now = QDateTime::currentDateTime();
    QLocale locale;
    QString dayName = locale.dayName(now.date().dayOfWeek(), QLocale::LongFormat);
    dayName[0] = dayName[0].toUpper();
    QString dateStr = dayName + ", " + locale.toString(now.date(), "d MMMM yyyy");
    dateLabel_->setText(dateStr);
    clockLabel_->setText(now.time().toString("HH:mm:ss"));
}

void HeaderWidget::setLoopActive(bool)
{
    // Transport controls moved to ScheduleColumn
}

void HeaderWidget::setStopAtEndActive(bool)
{
    // Transport controls moved to ScheduleColumn
}

void HeaderWidget::updateLoopBlink()
{
    // Transport controls moved to ScheduleColumn
}

void HeaderWidget::setRecordingActive(bool on)
{
    if (!recordBtn_) return;
    recordBtn_->blockSignals(true);
    recordBtn_->setChecked(on);
    recordBtn_->blockSignals(false);
    if (on) {
        recordBtn_->setStyleSheet(
            "QPushButton { background: #ef4444; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: #f87171; }");
    } else {
        recordBtn_->setStyleSheet(
            "QPushButton { background: #20202c; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: #2a2a3c; }");
    }
}

void HeaderWidget::setDefaultVolume(int percent)
{
    presetVolume_ = percent;
    if (!talkoverActive_) {
        volumeSlider_->setValue(percent);
    }
}

void HeaderWidget::setCurrentUser(const QString& displayName, UserRole role)
{
    if (!userBtn_) return;
    if (displayName.isEmpty()) {
        userBtn_->setText(tr("Iniciar sesión"));
        userBtn_->setIcon(tintIcon(":/icons/user.svg", Qt::white));
        userBtn_->setStyleSheet(
            "QPushButton { background: rgba(239,68,68,0.2); border: 1px solid rgba(239,68,68,0.4); "
            "border-radius: 18px; padding: 0 14px; font-size: 12px; font-weight: 600; color: #ef4444; }"
            "QPushButton:hover { background: rgba(239,68,68,0.3); }");
    } else {
        QString roleStr;
        switch (role) {
        case UserRole::Admin: roleStr = tr("Admin"); break;
        case UserRole::Programming: roleStr = tr("Prog"); break;
        default: roleStr = tr("Op"); break;
        }
        userBtn_->setText(QString(" %1 (%2)").arg(displayName, roleStr));
        userBtn_->setIcon(tintIcon(":/icons/user.svg", QColor("#22c55e")));
        userBtn_->setStyleSheet(
            "QPushButton { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15); "
            "border-radius: 18px; padding: 0 14px; font-size: 12px; font-weight: 600; color: #e0e0f0; }"
            "QPushButton:hover { background: rgba(255,255,255,0.15); }");
    }
}

void HeaderWidget::setControlsEnabled(bool enabled)
{
    // En modo sin sesión, deshabilitar controles interactivos
    // pero dejar visible la interfaz
    if (settingsButton_) settingsButton_->setEnabled(enabled);
    if (auditButton_) auditButton_->setEnabled(enabled);
    if (recordBtn_) recordBtn_->setEnabled(enabled);
    if (viewToggleBtn_) viewToggleBtn_->setEnabled(enabled);
}

void HeaderWidget::setViewToggleEnabled(bool enabled)
{
    if (viewToggleBtn_) viewToggleBtn_->setVisible(enabled);
}

void HeaderWidget::setViewMode(ViewMode mode)
{
    if (!viewToggleBtn_) return;
    viewToggleBtn_->blockSignals(true);
    viewToggleBtn_->setChecked(mode == ViewMode::Full);
    viewToggleBtn_->blockSignals(false);
    viewToggleBtn_->setToolTip(
        mode == ViewMode::Full
            ? tr("Vista completa (clic para modo operación)")
            : tr("Modo operación (clic para ver todo)"));
}

} // namespace sara
