#include "ui/settings_dialog.h"
#include "audio/audio_engine.h"
#include "data/backup_manager.h"
#include "data/user_manager.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QIcon>
#include <QFrame>
#include <QScrollArea>
#include <QPainter>

namespace sara {

static QIcon tintSvg(const QString& path, const QColor& color)
{
    QPixmap pix = QIcon(path).pixmap(64, 64);
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pix.rect(), color);
    p.end();
    return QIcon(pix);
}

static const QString SIDEBAR_STYLE =
    "QListWidget { background: rgba(0,0,0,0.06); border: none; border-radius: 8px; "
    "font-size: 13px; padding: 4px; }"
    "QListWidget::item { padding: 10px 14px; border-radius: 6px; "
    "margin: 2px 4px; }"
    "QListWidget::item:selected { background: rgba(102,126,234,0.2); "
    "font-weight: 600; }"
    "QListWidget::item:hover { background: rgba(102,126,234,0.08); }";

SettingsDialog::SettingsDialog(AppConfig& config, bool wizardMode, QWidget* parent)
    : QDialog(parent), config_(config), wizardMode_(wizardMode)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    if (wizardMode_) {
        setWindowTitle(tr("SARA Libre — Configuración inicial"));
        setMinimumSize(700, 550);
        setupWizard();
    } else {
        setWindowTitle(tr("Configuración — SARA"));
        setMinimumSize(700, 450);
        resize(820, 560);
        setupNormal();
    }
}

// ══════════════════════════════════════════════════════════
// Setup: Modo Normal (pestañas laterales)
// ══════════════════════════════════════════════════════════

void SettingsDialog::setupNormal()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    // Header
    auto* header = new QWidget();
    header->setStyleSheet("background: rgba(0,0,0,0.05); padding: 12px 20px;");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 10);

    auto* headerIcon = new QLabel();
    headerIcon->setPixmap(tintSvg(":/icons/settings.svg", Qt::white).pixmap(24, 24));
    auto* headerTitle = new QLabel(tr("Configuración"));
    headerTitle->setStyleSheet("font-size: 16px; font-weight: 700;");
    headerLayout->addWidget(headerIcon);
    headerLayout->addSpacing(8);
    headerLayout->addWidget(headerTitle);
    headerLayout->addStretch();
    layout->addWidget(header);

    // Cuerpo: sidebar + stack
    auto* body = new QHBoxLayout();
    body->setSpacing(0);
    body->setContentsMargins(0, 0, 0, 0);

    sidebar_ = new QListWidget();
    sidebar_->setFixedWidth(180);
    sidebar_->setStyleSheet(SIDEBAR_STYLE);
    sidebar_->setIconSize(QSize(18, 18));

    auto addTab = [&](const QString& icon, const QString& label) {
        auto* item = new QListWidgetItem(tintSvg(icon, Qt::white), label);
        sidebar_->addItem(item);
    };

    addTab(":/icons/radio.svg",          tr("Perfil de la Radio"));
    addTab(":/icons/settings.svg",       tr("Interfaz"));
    addTab(":/icons/play.svg",           tr("Modo Automático"));
    addTab(":/icons/monitor-speaker.svg",tr("Tarjetas de Audio"));
    addTab(":/icons/sliders-horizontal.svg", tr("Configuración de Audio"));
    addTab(":/icons/clock.svg",          tr("Locuciones de Hora"));
    addTab(":/icons/megaphone.svg",      tr("Espacio Publicitario"));
    addTab(":/icons/music.svg",           tr("Pisadores"));
    addTab(":/icons/save.svg",            tr("Respaldo"));
    addTab(":/icons/user.svg",             tr("Gestión de personal"));

    stack_ = new QStackedWidget();
    auto wrapScroll = [](QWidget* page) -> QWidget* {
        auto* scroll = new QScrollArea();
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        return scroll;
    };
    stack_->addWidget(wrapScroll(createPageProfile()));
    stack_->addWidget(wrapScroll(createPageInterface()));
    stack_->addWidget(wrapScroll(createPageAuto()));
    stack_->addWidget(wrapScroll(createPageAudioDevices()));
    stack_->addWidget(wrapScroll(createPageAudioConfig()));
    stack_->addWidget(wrapScroll(createPageTimeAnnounce()));
    stack_->addWidget(wrapScroll(createPageAdBreak()));
    stack_->addWidget(wrapScroll(createPagePisadores()));
    stack_->addWidget(wrapScroll(createPageBackup()));
    stack_->addWidget(wrapScroll(createPageUsers()));

    connect(sidebar_, &QListWidget::currentRowChanged,
            stack_, &QStackedWidget::setCurrentIndex);
    sidebar_->setCurrentRow(0);

    body->addWidget(sidebar_);
    body->addWidget(stack_, 1);
    layout->addLayout(body, 1);

    // Footer con botones
    auto* footer = new QWidget();
    footer->setStyleSheet("background: rgba(0,0,0,0.15);");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->addStretch();

    auto* cancelBtn = new QPushButton(tr("Cancelar"));
    cancelBtn->setProperty("class", "secondaryButton");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* saveBtn = new QPushButton(tintSvg(":/icons/save.svg", Qt::white), " Guardar cambios");
    saveBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2); color: white; border: none; "
        "border-radius: 8px; font-weight: 600; padding: 8px 20px; font-size: 13px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #7c94f4, stop:1 #8b5fc4); }");
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);

    footerLayout->addWidget(cancelBtn);
    footerLayout->addSpacing(8);
    footerLayout->addWidget(saveBtn);
    layout->addWidget(footer);
}

// ══════════════════════════════════════════════════════════
// Setup: Modo Wizard (primera ejecución)
// ══════════════════════════════════════════════════════════

void SettingsDialog::setupWizard()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(16);
    layout->setContentsMargins(32, 24, 32, 16);

    auto* welcomeLabel = new QLabel(tr("Bienvenido a SARA Libre"));
    welcomeLabel->setStyleSheet("font-size: 18px; font-weight: 700;");
    layout->addWidget(welcomeLabel);

    auto* descLabel = new QLabel(tr("Configure los parámetros básicos para comenzar."));
    descLabel->setStyleSheet(";");
    layout->addWidget(descLabel);

    stepLabel_ = new QLabel();
    stepLabel_->setStyleSheet("color: #667eea; font-weight: 600; font-size: 12px;");
    layout->addWidget(stepLabel_);

    stack_ = new QStackedWidget();

    auto wrapScroll = [](QWidget* page) -> QWidget* {
        auto* scroll = new QScrollArea();
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        // Asegurar inputs legibles en el wizard
        scroll->setStyleSheet(
            "QLineEdit, QComboBox, QSpinBox { min-height: 30px; font-size: 13px; }"
            "QSlider { min-height: 22px; }"
            "QCheckBox { font-size: 13px; spacing: 6px; }");
        return scroll;
    };

    stack_->addWidget(wrapScroll(createPageProfile()));
    stack_->addWidget(wrapScroll(createPageAuto()));
    stack_->addWidget(wrapScroll(createPageAudioDevices()));
    stack_->addWidget(wrapScroll(createPageTimeAnnounce()));
    stack_->addWidget(wrapScroll(createPageAdBreak()));
    layout->addWidget(stack_, 1);

    // Botones wizard
    auto* btnRow = new QHBoxLayout();
    prevButton_ = new QPushButton(tintSvg(":/icons/chevron-left.svg", Qt::white), " Anterior");
    prevButton_->setProperty("class", "secondaryButton");
    connect(prevButton_, &QPushButton::clicked, this, &SettingsDialog::prevStep);

    nextButton_ = new QPushButton(" Siguiente");
    nextButton_->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #667eea, stop:1 #764ba2); color: white; border: none; "
        "border-radius: 8px; font-weight: 600; padding: 8px 20px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #7c94f4, stop:1 #8b5fc4); }");
    connect(nextButton_, &QPushButton::clicked, this, &SettingsDialog::nextStep);

    btnRow->addStretch();
    btnRow->addWidget(prevButton_);
    btnRow->addWidget(nextButton_);
    layout->addLayout(btnRow);

    updateStepIndicator();
}

// ══════════════════════════════════════════════════════════
// Páginas
// ══════════════════════════════════════════════════════════

QWidget* SettingsDialog::createPageProfile()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Perfil de la Radio"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Esta información identifica tu emisora dentro de SARA y se usará en futuras funciones como zonas horarias y metadata."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Identidad
    auto* identGroup = new QGroupBox(tr("Identidad"));
    auto* identForm = new QFormLayout(identGroup);
    identForm->setSpacing(8);

    radioNameEdit_ = new QLineEdit(config_.radioName);
    radioNameEdit_->setPlaceholderText(tr("Mi Radio"));
    identForm->addRow(tr("Nombre de la radio *"), radioNameEdit_);

    radioSloganEdit_ = new QLineEdit(config_.radioSlogan);
    radioSloganEdit_->setPlaceholderText(tr("Ej: La radio que te acompaña"));
    identForm->addRow(tr("Slogan / tagline"), radioSloganEdit_);

    radioFrequencyEdit_ = new QLineEdit(config_.radioFrequency);
    radioFrequencyEdit_->setPlaceholderText(tr("Ej: 92.5 FM  /  1010 AM  /  Online"));
    identForm->addRow(tr("Frecuencia / medio"), radioFrequencyEdit_);

    layout->addWidget(identGroup);

    // Ubicación
    auto* locGroup = new QGroupBox(tr("Ubicación"));
    auto* locForm = new QFormLayout(locGroup);
    locForm->setSpacing(8);

    radioCountryEdit_ = new QLineEdit(config_.radioCountry);
    radioCountryEdit_->setPlaceholderText(tr("Selecciona o escribe tu país..."));
    locForm->addRow(tr("País"), radioCountryEdit_);

    radioCityEdit_ = new QLineEdit(config_.radioCity);
    radioCityEdit_->setPlaceholderText(tr("Ej: Buenos Aires, Ciudad de México..."));
    locForm->addRow(tr("Ciudad"), radioCityEdit_);

    layout->addWidget(locGroup);

    auto* hint = new QLabel("* El nombre de la radio aparecerá en la barra superior de SARA.");
    hint->setStyleSheet("color: #667eea; font-size: 11px;");
    layout->addWidget(hint);

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createPageInterface()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Interfaz"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    // Idioma — solo mostrar los que tengan traducción disponible
    auto* langGroup = new QGroupBox(tr("Idioma"));
    auto* langLayout = new QHBoxLayout(langGroup);
    langLayout->addWidget(new QLabel(tr("Idioma de la interfaz:")));
    languageCombo_ = new QComboBox();
    languageCombo_->addItem(tr("Automático (sistema)"), "auto");
    languageCombo_->addItem("Español", "es");  // Siempre disponible (idioma base)

    // Buscar traducciones .qm disponibles
    QStringList searchPaths = {
        QApplication::applicationDirPath() + "/translations",
        "/usr/share/saralibre/translations",
        "/usr/local/share/saralibre/translations"
    };
    struct LangEntry { QString code; QString label; };
    QVector<LangEntry> optionalLangs = {
        {"pt_BR", "Português (Brasil)"},
        {"en", "English"},
    };
    for (const auto& lang : optionalLangs) {
        for (const auto& path : searchPaths) {
            if (QFile::exists(path + "/saralibre_" + lang.code + ".qm")) {
                languageCombo_->addItem(lang.label, lang.code);
                break;
            }
        }
    }

    int langIdx = languageCombo_->findData(config_.language);
    if (langIdx >= 0) languageCombo_->setCurrentIndex(langIdx);
    languageCombo_->setMinimumWidth(200);
    langLayout->addWidget(languageCombo_);
    langLayout->addStretch();
    layout->addWidget(langGroup);

    auto* langHint = new QLabel(tr("El cambio de idioma se aplica al reiniciar SARA Libre."));
    langHint->setStyleSheet("font-size: 11px;");
    layout->addWidget(langHint);

    // Tema
    auto* themeGroup = new QGroupBox(tr("Tema visual"));
    auto* themeLayout = new QHBoxLayout(themeGroup);
    themeLayout->addWidget(new QLabel(tr("Tema:")));
    themeCombo_ = new QComboBox();
    themeCombo_->addItem(tr("Automático (sistema)"), "auto");
    themeCombo_->addItem(tr("Oscuro"), "dark");
    themeCombo_->addItem(tr("Claro"), "light");
    int themeIdx = themeCombo_->findData(config_.theme);
    if (themeIdx >= 0) themeCombo_->setCurrentIndex(themeIdx);
    themeLayout->addWidget(themeCombo_);
    themeLayout->addStretch();
    layout->addWidget(themeGroup);

    // Tamaño de fuente
    auto* fontGroup = new QGroupBox(tr("Tamaño de fuente"));
    auto* fontLayout = new QHBoxLayout(fontGroup);
    fontLayout->addWidget(new QLabel(tr("Tamaño:")));
    fontSizeCombo_ = new QComboBox();
    fontSizeCombo_->addItem(tr("Pequeño"), -1);
    fontSizeCombo_->addItem(tr("Normal"), 0);
    fontSizeCombo_->addItem(tr("Grande"), 1);
    int fontIdx = fontSizeCombo_->findData(config_.fontSize);
    if (fontIdx >= 0) fontSizeCombo_->setCurrentIndex(fontIdx);
    fontLayout->addWidget(fontSizeCombo_);
    fontLayout->addStretch();
    layout->addWidget(fontGroup);

    // VU Meter
    auto* vuGroup = new QGroupBox(tr("Indicadores"));
    auto* vuLayout = new QVBoxLayout(vuGroup);
    vuMeterCheck_ = new QCheckBox(tr("Mostrar VU Meter en la barra superior"));
    vuMeterCheck_->setChecked(config_.vuMeterEnabled);
    vuLayout->addWidget(vuMeterCheck_);
    layout->addWidget(vuGroup);

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createPageAuto()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Modo Automático"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Configura el comportamiento de SARA cuando funciona en automático."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Modo de inicio
    auto* startupGroup = new QGroupBox(tr("Modo de inicio"));
    auto* startupLayout = new QHBoxLayout(startupGroup);
    startupLayout->addWidget(new QLabel(tr("Al abrir SARA Libre:")));
    startupModeCombo_ = new QComboBox();
    startupModeCombo_->addItem(tr("Inicia tal como se cerró"), 0);
    startupModeCombo_->addItem(tr("Siempre inicia en AUTO"), 1);
    startupModeCombo_->addItem(tr("Siempre inicia en Manual"), 2);
    startupModeCombo_->setCurrentIndex(config_.startupMode);
    startupModeCombo_->setToolTip(
        tr("\"Tal como se cerró\" recuerda si estaba en AUTO o Manual al cerrar.\n"
           "Útil también para recuperación tras corte de luz."));
    startupLayout->addWidget(startupModeCombo_);
    startupLayout->addStretch();
    layout->addWidget(startupGroup);

    // Carpeta de la Radio
    auto* radioFolderGroup = new QGroupBox(tr("Carpeta de la Radio"));
    auto* radioFolderLayout = new QVBoxLayout(radioFolderGroup);

    auto* radioFolderDesc = new QLabel(
        tr("Carpeta raíz donde están los contenidos de audio de la radio "
           "(música, programas, cuñas, etc.). Aparecerá en el Explorador."));
    radioFolderDesc->setStyleSheet("font-size: 11px;");
    radioFolderDesc->setWordWrap(true);
    radioFolderLayout->addWidget(radioFolderDesc);

    auto* radioBrowseRow = new QHBoxLayout();
    radioFolderEdit_ = new QLineEdit(config_.radioFolder);
    radioFolderEdit_->setPlaceholderText(tr("/media/disco/radio  o  /home/radio/contenidos"));
    auto* radioBrowseBtn = new QPushButton(tintSvg(":/icons/folder.svg", Qt::white), tr(" Examinar"));
    connect(radioBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Carpeta de contenidos de la radio"), radioFolderEdit_->text());
        if (!dir.isEmpty()) radioFolderEdit_->setText(dir);
    });
    radioBrowseRow->addWidget(radioFolderEdit_, 1);
    radioBrowseRow->addWidget(radioBrowseBtn);
    radioFolderLayout->addLayout(radioBrowseRow);
    layout->addWidget(radioFolderGroup);

    // Carpeta alternativa (fallback)
    auto* folderGroup = new QGroupBox(tr("Carpeta Alternativa"));
    auto* folderLayout = new QVBoxLayout(folderGroup);

    auto* folderDesc = new QLabel(
        tr("SARA nunca se queda en silencio. Cuando no hay programación, reproducirá música aleatoria de esta carpeta (incluyendo subcarpetas)."));
    folderDesc->setStyleSheet("font-size: 11px;");
    folderDesc->setWordWrap(true);
    folderLayout->addWidget(folderDesc);

    auto* browseRow = new QHBoxLayout();
    fallbackFolderEdit_ = new QLineEdit(config_.fallbackFolder);
    fallbackFolderEdit_->setPlaceholderText("/home/radio/musica");
    auto* browseBtn = new QPushButton(tintSvg(":/icons/folder.svg", Qt::white), " Examinar");
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseFallback);
    browseRow->addWidget(fallbackFolderEdit_, 1);
    browseRow->addWidget(browseBtn);
    folderLayout->addLayout(browseRow);
    layout->addWidget(folderGroup);


    // Anti-repetición
    auto* repeatGroup = new QGroupBox(tr("Anti-repetición"));
    auto* repeatVLayout = new QVBoxLayout(repeatGroup);

    auto* repeatRow1 = new QHBoxLayout();
    repeatRow1->addWidget(new QLabel(tr("No repetir una canción en:")));
    noRepeatSpin_ = new QSpinBox();
    noRepeatSpin_->setRange(1, 24);
    noRepeatSpin_->setSuffix(" horas");
    noRepeatSpin_->setValue(config_.noRepeatHours);
    noRepeatSpin_->setFixedWidth(100);
    repeatRow1->addWidget(noRepeatSpin_);
    repeatRow1->addStretch();
    repeatVLayout->addLayout(repeatRow1);

    auto* repeatRow2 = new QHBoxLayout();
    repeatRow2->addWidget(new QLabel(tr("No repetir artista en:")));
    noRepeatArtistSpin_ = new QSpinBox();
    noRepeatArtistSpin_->setRange(0, 30);
    noRepeatArtistSpin_->setSuffix(" pistas");
    noRepeatArtistSpin_->setSpecialValueText(tr("Deshabilitado"));
    noRepeatArtistSpin_->setValue(config_.noRepeatArtistTracks);
    noRepeatArtistSpin_->setFixedWidth(120);
    noRepeatArtistSpin_->setToolTip(
        tr("Evita que el mismo artista suene en las últimas N pistas.\n"
           "Requiere que los archivos tengan el tag de artista.\n"
           "0 = deshabilitado."));
    repeatRow2->addWidget(noRepeatArtistSpin_);
    repeatRow2->addStretch();
    repeatVLayout->addLayout(repeatRow2);

    layout->addWidget(repeatGroup);


    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createPageAudioDevices()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Tarjetas de Audio"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Configure qué tarjeta de audio se usa para cada función. "
           "Si solo tiene una tarjeta, deje todo en \"default\"."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto devices = AudioEngine::availableAudioDevicesDetailed();

    auto addDeviceRow = [&](const QString& label, const QString& tooltip,
                            QComboBox*& combo, const QString& currentDevice) {
        auto* group = new QGroupBox(label);
        auto* gl = new QVBoxLayout(group);
        auto* tipLabel = new QLabel(tooltip);
        tipLabel->setProperty("class", "dimLabel");
        tipLabel->setWordWrap(true);
        gl->addWidget(tipLabel);

        combo = new QComboBox();
        combo->addItem(tr("default (automático)"), QString("default"));
        for (const auto& dev : devices) {
            combo->addItem(dev.displayName, dev.deviceId);
        }
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(30);
        if (!currentDevice.isEmpty() && currentDevice != "default") {
            for (int i = 1; i < combo->count(); ++i) {
                QString itemId = combo->itemData(i).toString();
                if (itemId == currentDevice || combo->itemText(i).contains(currentDevice)) {
                    combo->setCurrentIndex(i);
                    break;
                }
            }
        }
        gl->addWidget(combo);
        layout->addWidget(group);
    };

    addDeviceRow(tr("Main (Aire)"),
        tr("Tarjeta principal — la señal que sale al aire. Publicidad y eventos también salen por aquí."),
        audioDeviceCombo_, config_.mainAudioDevice);

    addDeviceRow(tr("CUE (Preescucha)"),
        tr("Tarjeta para preescucha por auriculares del operador."),
        cueDeviceCombo_, config_.cueAudioDevice);

    addDeviceRow(tr("InstantPlay (Asistente en Vivo)"),
        tr("Tarjeta para los botones F1-F12. Si la deja en default, sale por la misma que Main."),
        instantDeviceCombo_, config_.instantAudioDevice);

    // Grabación Testigo: listar todas las fuentes de PulseAudio (monitores + entradas)
    {
        auto* group = new QGroupBox(tr("Grabación Testigo (Fuente)"));
        auto* gl = new QVBoxLayout(group);
        auto* tipLabel = new QLabel(
            tr("Fuente de audio para la grabación testigo. Puede elegir un Monitor "
               "(captura la mezcla de la tarjeta), una entrada de micrófono o line-in."));
        tipLabel->setProperty("class", "dimLabel");
        tipLabel->setWordWrap(true);
        gl->addWidget(tipLabel);

        recordDeviceCombo_ = new QComboBox();
        recordDeviceCombo_->addItem(tr("default (monitor del sistema)"), QString("default"));

        // Listar todas las fuentes de PulseAudio incluyendo monitores
        auto sources = AudioEngine::availableInputSources();
        for (const auto& src : sources) {
            // Prefijo: [Monitor] para monitores, [Entrada] para entradas
            bool isMonitor = src.deviceId.contains(".monitor") ||
                             src.displayName.contains("Monitor", Qt::CaseInsensitive);
            QString prefix = isMonitor ? "[Monitor] " : "[Entrada] ";
            recordDeviceCombo_->addItem(
                QString("%1%2").arg(prefix, src.displayName),
                QString("source:%1").arg(src.deviceId));
        }

        recordDeviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        recordDeviceCombo_->setMinimumContentsLength(30);

        // Seleccionar el dispositivo actual
        if (!config_.recordingDevice.isEmpty() && config_.recordingDevice != "default") {
            for (int i = 1; i < recordDeviceCombo_->count(); ++i) {
                QString itemId = recordDeviceCombo_->itemData(i).toString();
                if (itemId == config_.recordingDevice) {
                    recordDeviceCombo_->setCurrentIndex(i);
                    break;
                }
            }
        }

        gl->addWidget(recordDeviceCombo_);
        layout->addWidget(group);
    }

    layout->addStretch();
    return page;
}


QWidget* SettingsDialog::createPageAudioConfig()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Configuración de Audio"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    // ── Volumen ──────────────────────────────────────
    auto* volGroup = new QGroupBox(tr("Volumen"));
    auto* volForm = new QFormLayout(volGroup);
    volForm->setSpacing(8);

    defaultVolumeSpin_ = new QSpinBox();
    defaultVolumeSpin_->setRange(50, 100);
    defaultVolumeSpin_->setSuffix("%");
    defaultVolumeSpin_->setValue(config_.defaultVolume);
    defaultVolumeSpin_->setToolTip(tr("Volumen con el que SARA arranca."));
    volForm->addRow(tr("Volumen inicial"), defaultVolumeSpin_);

    talkoverLevelSpin_ = new QSpinBox();
    talkoverLevelSpin_->setRange(5, 50);
    talkoverLevelSpin_->setSuffix("%");
    talkoverLevelSpin_->setValue(config_.talkoverLevel);
    talkoverLevelSpin_->setToolTip(
        tr("Nivel al que baja la música cuando se pulsa el botón de micrófono "
           "(hablar encima)."));
    volForm->addRow(tr("Nivel al hablar encima"), talkoverLevelSpin_);

    layout->addWidget(volGroup);

    // Crossfade
    auto* fadeGroup = new QGroupBox(tr("Transiciones de Audio"));
    auto* fadeLayout = new QVBoxLayout(fadeGroup);

    crossfadeCheck_ = new QCheckBox(tr("Activar crossfade solapado entre canciones"));
    crossfadeCheck_->setChecked(config_.crossfadeEnabled);
    crossfadeCheck_->setToolTip(tr("Transición suave: ambas pistas suenan al mismo tiempo durante la mezcla."));
    fadeLayout->addWidget(crossfadeCheck_);

    auto* fadeRow = new QHBoxLayout();
    fadeRow->addWidget(new QLabel(tr("Duración:")));
    crossfadeSpin_ = new QSpinBox();
    crossfadeSpin_->setRange(500, 8000);
    crossfadeSpin_->setSingleStep(500);
    crossfadeSpin_->setSuffix(" ms");
    crossfadeSpin_->setValue(config_.crossfadeMs);
    crossfadeSpin_->setFixedWidth(110);
    crossfadeSpin_->setEnabled(config_.crossfadeEnabled);
    connect(crossfadeCheck_, &QCheckBox::toggled, crossfadeSpin_, &QSpinBox::setEnabled);
    auto* fadeHelp = new QLabel("(1000 = 1s, 3000 = 3s)");
    fadeHelp->setStyleSheet("font-size: 11px;");
    fadeRow->addWidget(crossfadeSpin_);
    fadeRow->addWidget(fadeHelp);
    fadeRow->addStretch();
    fadeLayout->addLayout(fadeRow);

    // Separador visual
    auto* fadeSep = new QFrame();
    fadeSep->setFrameShape(QFrame::HLine);
    fadeSep->setStyleSheet("color: rgba(255,255,255,0.06);");
    fadeLayout->addWidget(fadeSep);

    // Fade-Out para transiciones
    fadeOutCheck_ = new QCheckBox(tr("Activar transición con Fade-Out"));
    fadeOutCheck_->setChecked(config_.fadeOutEnabled);
    fadeOutCheck_->setToolTip(tr("Fade-out suave al saltar pista, detener, o cambiar a eventos.\n"
                                 "Si está desactivado, los cambios son inmediatos (corte directo)."));
    fadeLayout->addWidget(fadeOutCheck_);

    auto* fadeOutRow = new QHBoxLayout();
    fadeOutRow->addWidget(new QLabel(tr("Duración del Fade-Out:")));
    fadeOutSpin_ = new QSpinBox();
    fadeOutSpin_->setRange(200, 3000);
    fadeOutSpin_->setSingleStep(100);
    fadeOutSpin_->setSuffix(" ms");
    fadeOutSpin_->setValue(config_.fadeOutMs);
    fadeOutSpin_->setFixedWidth(110);
    fadeOutSpin_->setEnabled(config_.fadeOutEnabled);
    connect(fadeOutCheck_, &QCheckBox::toggled, fadeOutSpin_, &QSpinBox::setEnabled);
    auto* fadeOutHelp = new QLabel(tr("(200 = rápido, 1000 = suave)"));
    fadeOutHelp->setStyleSheet("font-size: 11px;");
    fadeOutRow->addWidget(fadeOutSpin_);
    fadeOutRow->addWidget(fadeOutHelp);
    fadeOutRow->addStretch();
    fadeLayout->addLayout(fadeOutRow);

    layout->addWidget(fadeGroup);

    // Detección de silencio
    auto* silenceGroup = new QGroupBox(tr("Detección de Silencio"));
    auto* silenceLayout = new QVBoxLayout(silenceGroup);

    silenceCheck_ = new QCheckBox(tr("Activar detección de silencio (skip automático)"));
    silenceCheck_->setChecked(config_.silenceDetectionEnabled);
    silenceCheck_->setToolTip(tr("Si una pista tiene silencio prolongado, SARA salta automáticamente a la siguiente."));
    silenceLayout->addWidget(silenceCheck_);

    auto* silenceRow1 = new QHBoxLayout();
    silenceRow1->addWidget(new QLabel(tr("Tiempo de silencio:")));
    silenceThresholdSpin_ = new QSpinBox();
    silenceThresholdSpin_->setRange(5, 60);
    silenceThresholdSpin_->setSuffix(" segundos");
    silenceThresholdSpin_->setValue(config_.silenceThresholdSecs);
    silenceThresholdSpin_->setFixedWidth(130);
    silenceThresholdSpin_->setEnabled(config_.silenceDetectionEnabled);
    silenceRow1->addWidget(silenceThresholdSpin_);
    silenceRow1->addStretch();
    silenceLayout->addLayout(silenceRow1);

    auto* silenceRow2 = new QHBoxLayout();
    silenceRow2->addWidget(new QLabel(tr("Nivel mínimo:")));
    silenceLevelSpin_ = new QSpinBox();
    silenceLevelSpin_->setRange(-80, -20);
    silenceLevelSpin_->setSuffix(" dB");
    silenceLevelSpin_->setValue(config_.silenceLevelDb);
    silenceLevelSpin_->setFixedWidth(100);
    silenceLevelSpin_->setEnabled(config_.silenceDetectionEnabled);
    silenceLevelSpin_->setToolTip(tr("Audio por debajo de este nivel se considera silencio.\nRecomendado: -50 dB. Más alto = más sensible."));
    silenceRow2->addWidget(silenceLevelSpin_);
    auto* silenceHelp = new QLabel("(recomendado: -50 dB)");
    silenceHelp->setStyleSheet("font-size: 11px;");
    silenceRow2->addWidget(silenceHelp);
    silenceRow2->addStretch();
    silenceLayout->addLayout(silenceRow2);

    connect(silenceCheck_, &QCheckBox::toggled, this, [this](bool on) {
        silenceThresholdSpin_->setEnabled(on);
        silenceLevelSpin_->setEnabled(on);
    });

    layout->addWidget(silenceGroup);

    // Normalización de audio (ReplayGain)
    auto* rgGroup = new QGroupBox(tr("Normalización de Audio"));
    auto* rgLayout = new QVBoxLayout(rgGroup);

    replayGainCheck_ = new QCheckBox(tr("Activar ReplayGain (normaliza volumen entre pistas)"));
    replayGainCheck_->setChecked(config_.replayGainEnabled);
    replayGainCheck_->setToolTip(
        tr("ReplayGain lee las etiquetas de ganancia de cada archivo y ajusta el volumen "
           "automáticamente para que todas las canciones suenen al mismo nivel. "
           "Requiere reiniciar SARA Libre para aplicarse."));
    rgLayout->addWidget(replayGainCheck_);

    auto* rgHint = new QLabel(
        tr("Si los archivos no tienen etiquetas ReplayGain, se aplica una ganancia "
           "de respaldo de -6 dB. El cambio se aplica al reiniciar."));
    rgHint->setProperty("class", "dimLabel");
    rgHint->setWordWrap(true);
    rgLayout->addWidget(rgHint);

    layout->addWidget(rgGroup);

    // Grabación testigo
    auto* recGroup = new QGroupBox(tr("Grabación Testigo"));
    auto* recLayout = new QVBoxLayout(recGroup);

    auto* recDesc = new QLabel(
        tr("Graba todo el audio que sale al aire en archivos locales. "
           "Útil para registro legal o revisión de la programación emitida."));
    recDesc->setProperty("class", "dimLabel");
    recDesc->setWordWrap(true);
    recLayout->addWidget(recDesc);

    auto* recFolderRow = new QHBoxLayout();
    recFolderRow->addWidget(new QLabel(tr("Carpeta:")));
    recFolderEdit_ = new QLineEdit(config_.recordingFolder);
    recFolderEdit_->setPlaceholderText(tr("/home/radio/grabaciones"));
    auto* recBrowseBtn = new QPushButton(tintSvg(":/icons/folder.svg", Qt::white), "");
    connect(recBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this,
            tr("Carpeta para grabaciones testigo"),
            recFolderEdit_->text().isEmpty() ? QDir::homePath() : recFolderEdit_->text());
        if (!dir.isEmpty()) recFolderEdit_->setText(dir);
    });
    recFolderRow->addWidget(recFolderEdit_, 1);
    recFolderRow->addWidget(recBrowseBtn);
    recLayout->addLayout(recFolderRow);

    auto* recOptionsRow = new QHBoxLayout();

    recOptionsRow->addWidget(new QLabel(tr("Formato:")));
    recFormatCombo_ = new QComboBox();
    recFormatCombo_->addItem("MP3", "mp3");
    recFormatCombo_->addItem("OGG Vorbis", "ogg");
    recFormatCombo_->setCurrentIndex(config_.recordingFormat == "ogg" ? 1 : 0);
    recFormatCombo_->setFixedWidth(120);
    recOptionsRow->addWidget(recFormatCombo_);

    recOptionsRow->addSpacing(16);
    recOptionsRow->addWidget(new QLabel(tr("Calidad:")));
    recBitrateSpin_ = new QSpinBox();
    recBitrateSpin_->setRange(48, 192);
    recBitrateSpin_->setSingleStep(16);
    recBitrateSpin_->setSuffix(" kbps");
    recBitrateSpin_->setValue(config_.recordingBitrate);
    recBitrateSpin_->setFixedWidth(110);
    recBitrateSpin_->setToolTip(tr("64-96 kbps para testigo legal, 128+ para mayor calidad"));
    recOptionsRow->addWidget(recBitrateSpin_);

    recOptionsRow->addStretch();
    recLayout->addLayout(recOptionsRow);

    recSegmentCheck_ = new QCheckBox(tr("Segmentar archivos por hora (recomendado)"));
    recSegmentCheck_->setChecked(config_.recordingSegmentByHour);
    recSegmentCheck_->setToolTip(
        tr("Crea un archivo nuevo cada hora.\n"
           "Ejemplo: sara_rec_2026-04-17_14-00.mp3, sara_rec_2026-04-17_15-00.mp3"));
    recLayout->addWidget(recSegmentCheck_);

    layout->addWidget(recGroup);

    // ── Streaming / Butt ──────────────────────────────
    auto* streamGroup = new QGroupBox(tr("Streaming (Butt / Sonando ahora)"));
    auto* streamLayout = new QVBoxLayout(streamGroup);

    auto* streamDesc = new QLabel(
        tr("Genera un archivo de texto con la canción en reproducción. "
           "Butt y otros programas de streaming pueden leerlo para enviar metadata."));
    streamDesc->setProperty("class", "dimLabel");
    streamDesc->setWordWrap(true);
    streamLayout->addWidget(streamDesc);

    nowPlayingCheck_ = new QCheckBox(tr("Activar archivo \"sonando ahora\""));
    nowPlayingCheck_->setChecked(config_.nowPlayingEnabled);
    streamLayout->addWidget(nowPlayingCheck_);

    auto* fileRow = new QHBoxLayout();
    nowPlayingFileEdit_ = new QLineEdit(config_.nowPlayingFile);
    nowPlayingFileEdit_->setPlaceholderText(tr("Ruta al archivo .txt (ej: /home/radio/sonando-ahora.txt)"));
    fileRow->addWidget(nowPlayingFileEdit_);

    auto* npBrowseBtn = new QPushButton(tr("Elegir..."));
    npBrowseBtn->setFixedWidth(80);
    connect(npBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this,
            tr("Ubicación del archivo \"sonando ahora\""),
            nowPlayingFileEdit_->text().isEmpty()
                ? QDir::homePath() + "/sonando-ahora.txt"
                : nowPlayingFileEdit_->text(),
            tr("Archivo de texto (*.txt)"));
        if (!path.isEmpty()) {
            nowPlayingFileEdit_->setText(path);
        }
    });
    fileRow->addWidget(npBrowseBtn);
    streamLayout->addLayout(fileRow);

    layout->addWidget(streamGroup);

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createPageTimeAnnounce()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Locuciones de Hora"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Configure las carpetas con los archivos de audio para las horas y minutos. SARA armará la locución automáticamente concatenando los archivos."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto addFolderRow = [&](const QString& label, QLineEdit*& edit,
                            const QString& value, const QString& placeholder,
                            void (SettingsDialog::*browseSlot)()) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(label);
        lbl->setFixedWidth(130);
        edit = new QLineEdit(value);
        edit->setPlaceholderText(placeholder);
        auto* btn = new QPushButton("...");
        btn->setFixedWidth(32);
        connect(btn, &QPushButton::clicked, this, browseSlot);
        row->addWidget(lbl);
        row->addWidget(edit, 1);
        row->addWidget(btn);
        layout->addLayout(row);
    };

    auto addFileRow = [&](const QString& label, QLineEdit*& edit,
                          const QString& value, const QString& placeholder) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(label);
        lbl->setFixedWidth(130);
        edit = new QLineEdit(value);
        edit->setPlaceholderText(placeholder);
        auto* btn = new QPushButton("...");
        btn->setFixedWidth(32);
        connect(btn, &QPushButton::clicked, this, [this, edit]() {
            QString file = QFileDialog::getOpenFileName(this, tr("Seleccionar archivo"),
                QString(), "Audio (*.mp3 *.ogg *.wav *.flac)");
            if (!file.isEmpty()) edit->setText(file);
        });
        row->addWidget(lbl);
        row->addWidget(edit, 1);
        row->addWidget(btn);
        layout->addLayout(row);
    };

    addFolderRow(tr("Carpeta horas:"), hoursFolderEdit_,
        config_.hoursFolder, "Carpeta con 00.mp3 a 23.mp3",
        &SettingsDialog::browseHoursFolder);

    addFolderRow(tr("Carpeta minutos:"), minutesFolderEdit_,
        config_.minutesFolder, "Carpeta con 00.mp3 a 59.mp3",
        &SettingsDialog::browseMinutesFolder);

    addFileRow(tr("Prefijo:"), prefixFileEdit_,
        config_.prefixFile, "Ej: 'Son las...' (opcional)");

    addFileRow(tr("Sufijo:"), suffixFileEdit_,
        config_.suffixFile, "Ej: 'horas con...' (opcional)");

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createPageAdBreak()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Espacio Publicitario"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Configure los audios opcionales de apertura y cierre de bloques publicitarios. Si un evento tiene marcado 'Incluir locución inicio/fin', estos audios se reproducirán automáticamente."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto addFileRow = [&](const QString& label, QLineEdit*& edit,
                          const QString& value, const QString& placeholder) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(label);
        lbl->setFixedWidth(130);
        edit = new QLineEdit(value);
        edit->setPlaceholderText(placeholder);
        auto* btn = new QPushButton("...");
        btn->setFixedWidth(32);
        connect(btn, &QPushButton::clicked, this, [this, edit]() {
            QString file = QFileDialog::getOpenFileName(this, tr("Seleccionar archivo"),
                QString(), "Audio (*.mp3 *.ogg *.wav *.flac)");
            if (!file.isEmpty()) edit->setText(file);
        });
        row->addWidget(lbl);
        row->addWidget(edit, 1);
        row->addWidget(btn);
        layout->addLayout(row);
    };

    addFileRow(tr("Audio inicio:"), adIntroFileEdit_,
        config_.adIntroFile, "\"Inicio de espacio publicitario\"");

    addFileRow(tr("Audio fin:"), adOutroFileEdit_,
        config_.adOutroFile, "\"Fin de espacio publicitario\"");

    layout->addStretch();
    return page;
}

// ══════════════════════════════════════════════════════════
// Wizard navigation
// ══════════════════════════════════════════════════════════

void SettingsDialog::nextStep()
{
    if (currentStep_ < totalSteps_ - 1) {
        currentStep_++;
        stack_->setCurrentIndex(currentStep_);
        updateStepIndicator();
    } else {
        onSave();
    }
}

void SettingsDialog::prevStep()
{
    if (currentStep_ > 0) {
        currentStep_--;
        stack_->setCurrentIndex(currentStep_);
        updateStepIndicator();
    }
}

void SettingsDialog::updateStepIndicator()
{
    if (!stepLabel_) return;

    QStringList names = {
        tr("Perfil de la Radio"),
        tr("Modo Automático"),
        tr("Tarjetas de Audio"),
        tr("Locuciones de Hora"),
        tr("Espacio Publicitario")
    };

    stepLabel_->setText(tr("Paso %1 de %2: %3")
        .arg(currentStep_ + 1).arg(totalSteps_).arg(names[currentStep_]));

    if (prevButton_) prevButton_->setEnabled(currentStep_ > 0);
    if (nextButton_) {
        nextButton_->setText(currentStep_ < totalSteps_ - 1 ? " Siguiente" : " Finalizar");
    }
}

// ══════════════════════════════════════════════════════════
// Guardar
// ══════════════════════════════════════════════════════════

void SettingsDialog::collectConfig()
{
    // Perfil
    config_.radioName      = radioNameEdit_->text().trimmed();
    config_.radioSlogan    = radioSloganEdit_->text().trimmed();
    config_.radioFrequency = radioFrequencyEdit_->text().trimmed();
    config_.radioCity      = radioCityEdit_->text().trimmed();
    config_.radioCountry   = radioCountryEdit_->text().trimmed();
    if (defaultVolumeSpin_) {
        config_.defaultVolume  = defaultVolumeSpin_->value();
        config_.talkoverLevel  = talkoverLevelSpin_->value();
    }

    // Interfaz (solo en modo normal, no en wizard)
    if (languageCombo_) {
        config_.language = languageCombo_->currentData().toString();
        config_.theme    = themeCombo_->currentData().toString();
        config_.fontSize = fontSizeCombo_->currentData().toInt();
    }

    // Modo Automático
    config_.startupMode     = startupModeCombo_->currentIndex();
    if (radioFolderEdit_)
        config_.radioFolder      = radioFolderEdit_->text().trimmed();
    if (fallbackFolderEdit_)
        config_.fallbackFolder   = fallbackFolderEdit_->text().trimmed();
    if (crossfadeCheck_) {
        config_.crossfadeEnabled = crossfadeCheck_->isChecked();
        config_.crossfadeMs      = crossfadeSpin_->value();
    }
    if (fadeOutCheck_) {
        config_.fadeOutEnabled   = fadeOutCheck_->isChecked();
        config_.fadeOutMs        = fadeOutSpin_->value();
    }
    if (noRepeatSpin_) {
        config_.noRepeatHours    = noRepeatSpin_->value();
    }
    if (noRepeatArtistSpin_) {
        config_.noRepeatArtistTracks = noRepeatArtistSpin_->value();
    }
    if (silenceCheck_) {
        config_.silenceDetectionEnabled = silenceCheck_->isChecked();
        config_.silenceThresholdSecs    = silenceThresholdSpin_->value();
        config_.silenceLevelDb          = silenceLevelSpin_->value();
    }
    if (replayGainCheck_) {
        config_.replayGainEnabled = replayGainCheck_->isChecked();
    }
    if (recFolderEdit_)   config_.recordingFolder         = recFolderEdit_->text().trimmed();
    if (recFormatCombo_)  config_.recordingFormat          = recFormatCombo_->currentData().toString();
    if (recBitrateSpin_)  config_.recordingBitrate         = recBitrateSpin_->value();
    if (recSegmentCheck_) config_.recordingSegmentByHour   = recSegmentCheck_->isChecked();

    // Dispositivos
    auto extractDevice = [](QComboBox* combo) -> QString {
        if (!combo) return QString();
        return combo->currentData().toString();
    };
    if (audioDeviceCombo_)  config_.mainAudioDevice    = extractDevice(audioDeviceCombo_);
    if (cueDeviceCombo_)    config_.cueAudioDevice     = extractDevice(cueDeviceCombo_);
    if (instantDeviceCombo_) config_.instantAudioDevice = extractDevice(instantDeviceCombo_);
    if (recordDeviceCombo_) {
        config_.recordingDevice = extractDevice(recordDeviceCombo_);
        if (config_.recordingDevice == "default") config_.recordingDevice.clear();
    }
    if (config_.cueAudioDevice == "default") config_.cueAudioDevice.clear();
    if (config_.instantAudioDevice == "default") config_.instantAudioDevice.clear();

    // Locuciones
    if (hoursFolderEdit_) {
        config_.hoursFolder   = hoursFolderEdit_->text().trimmed();
        config_.minutesFolder = minutesFolderEdit_->text().trimmed();
        config_.prefixFile    = prefixFileEdit_->text().trimmed();
        config_.suffixFile    = suffixFileEdit_->text().trimmed();
    }

    // Espacio publicitario
    if (adIntroFileEdit_) {
        config_.adIntroFile = adIntroFileEdit_->text().trimmed();
        config_.adOutroFile = adOutroFileEdit_->text().trimmed();
    }

    // VU Meter
    if (vuMeterCheck_) {
        config_.vuMeterEnabled = vuMeterCheck_->isChecked();
    }

    // Pisadores
    if (pisadorEnabledCheck_) {
        config_.pisadorEnabled   = pisadorEnabledCheck_->isChecked();
        config_.pisadorFolder    = pisadorFolderEdit_->text().trimmed();
        config_.pisadorFrequency = pisadorFreqCombo_->currentData().toInt();
        config_.pisadorDuckLevel = pisadorDuckSpin_->value() / 100.0;
        config_.pisadorDelaySecs = pisadorDelaySpin_->value();

        config_.pisadorExcludedFolders.clear();
        for (int i = 0; i < pisadorExcludedList_->count(); ++i) {
            config_.pisadorExcludedFolders << pisadorExcludedList_->item(i)->text();
        }
    }

    // Backup
    if (backupEnabledCheck_) {
        config_.backupEnabled       = backupEnabledCheck_->isChecked();
        config_.backupIntervalHours = backupIntervalCombo_->currentData().toInt();
        config_.backupMaxCount      = backupMaxCombo_->currentData().toInt();
    }

    // Streaming / Butt
    if (nowPlayingCheck_) config_.nowPlayingEnabled = nowPlayingCheck_->isChecked();
    if (nowPlayingFileEdit_) config_.nowPlayingFile = nowPlayingFileEdit_->text().trimmed();

    LOG_INFO("[Settings] Dispositivos: main='{}', cue='{}', instant='{}'",
             config_.mainAudioDevice.toStdString(),
             config_.cueAudioDevice.toStdString(),
             config_.instantAudioDevice.toStdString());
}

void SettingsDialog::onSave()
{
    collectConfig();
    accept();
}

void SettingsDialog::browseFallback()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Carpeta de música alternativa"), fallbackFolderEdit_->text());
    if (!dir.isEmpty()) fallbackFolderEdit_->setText(dir);
}

void SettingsDialog::browseHoursFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Carpeta de horas"), hoursFolderEdit_->text());
    if (!dir.isEmpty()) hoursFolderEdit_->setText(dir);
}

void SettingsDialog::browseMinutesFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Carpeta de minutos"), minutesFolderEdit_->text());
    if (!dir.isEmpty()) minutesFolderEdit_->setText(dir);
}

QWidget* SettingsDialog::createPageUsers()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Gestión de personal"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Administre las cuentas de acceso al sistema. "
           "Solo las cuentas de Administración pueden crear, editar o eliminar cuentas."));
    desc->setStyleSheet("font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Lista de usuarios
    auto* listGroup = new QGroupBox(tr("Cuentas registradas"));
    auto* listLayout = new QVBoxLayout(listGroup);

    userListWidget_ = new QListWidget();
    userListWidget_->setMaximumHeight(200);
    listLayout->addWidget(userListWidget_);

    auto* listBtnRow = new QHBoxLayout();

    auto* addUserBtn = new QPushButton(tintSvg(":/icons/plus.svg", Qt::white), tr(" Nueva cuenta"));
    addUserBtn->setFixedHeight(28);
    connect(addUserBtn, &QPushButton::clicked, this, [this]() {
        if (!userManager_) return;

        bool ok;
        QString name = QInputDialog::getText(this, tr("Nueva cuenta"),
            tr("Nombre visible:"), QLineEdit::Normal, "", &ok);
        if (!ok || name.trimmed().isEmpty()) return;

        QString username = QInputDialog::getText(this, tr("Nueva cuenta"),
            tr("Usuario/a (para iniciar sesión):"), QLineEdit::Normal, "", &ok);
        if (!ok || username.trimmed().isEmpty()) return;

        QString pin = QInputDialog::getText(this, tr("Nueva cuenta"),
            tr("PIN numérico (4-8 dígitos):"), QLineEdit::Password, "", &ok);
        if (!ok || pin.isEmpty()) return;
        if (pin.length() < 4 || pin.length() > 8) {
            QMessageBox::warning(this, tr("PIN inválido"),
                tr("El PIN debe tener entre 4 y 8 dígitos."));
            return;
        }

        QStringList roles;
        roles << tr("Operación") << tr("Programación") << tr("Administración");
        QString roleStr = QInputDialog::getItem(this, tr("Nueva cuenta"),
            tr("Rol:"), roles, 0, false, &ok);
        if (!ok) return;

        UserRole role = UserRole::Operation;
        if (roleStr == tr("Programación")) role = UserRole::Programming;
        else if (roleStr == tr("Administración")) role = UserRole::Admin;

        int id = userManager_->createUser(username.trimmed(), name.trimmed(), pin, role);
        if (id > 0) {
            // Ofrecer generar archivo de recuperación
            auto answer = QMessageBox::question(this, tr("Archivo de recuperación"),
                tr("¿Desea generar un archivo de recuperación para %1?\n\n"
                   "Este archivo permite recuperar el PIN si se olvida.")
                .arg(name.trimmed()));
            if (answer == QMessageBox::Yes) {
                QString token = userManager_->generateRecoveryToken(id);
                QString savePath = QFileDialog::getSaveFileName(this,
                    tr("Guardar archivo de recuperación"),
                    QDir::homePath() + QString("/sara_recovery_%1.key").arg(username.trimmed()),
                    tr("Archivo de recuperación (*.key)"));
                if (!savePath.isEmpty()) {
                    QFile file(savePath);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream out(&file);
                        out << token;
                        file.close();
                    }
                }
            }
            QMessageBox::information(this, tr("Cuenta creada"),
                tr("Cuenta de %1 creada correctamente.").arg(name.trimmed()));
            refreshUserList();
        } else {
            QMessageBox::warning(this, tr("Error"),
                tr("No se pudo crear la cuenta. El usuario/a ya existe."));
        }
    });
    listBtnRow->addWidget(addUserBtn);

    listBtnRow->addStretch();

    auto* changePinBtn = new QPushButton(tintSvg(":/icons/settings.svg", Qt::white), tr(" Cambiar PIN"));
    changePinBtn->setFixedHeight(28);
    connect(changePinBtn, &QPushButton::clicked, this, [this]() {
        if (!userManager_ || !userListWidget_->currentItem()) return;
        int userId = userListWidget_->currentItem()->data(Qt::UserRole).toInt();

        bool ok;
        QString newPin = QInputDialog::getText(this, tr("Cambiar PIN"),
            tr("Nuevo PIN (4-8 dígitos):"), QLineEdit::Password, "", &ok);
        if (!ok || newPin.isEmpty()) return;
        if (newPin.length() < 4 || newPin.length() > 8) {
            QMessageBox::warning(this, tr("PIN inválido"),
                tr("El PIN debe tener entre 4 y 8 dígitos."));
            return;
        }
        userManager_->changePin(userId, newPin);
        QMessageBox::information(this, tr("PIN actualizado"),
            tr("El PIN fue actualizado correctamente."));
    });
    listBtnRow->addWidget(changePinBtn);

    auto* deleteUserBtn = new QPushButton(tintSvg(":/icons/x.svg", Qt::white), tr(" Eliminar"));
    deleteUserBtn->setFixedHeight(28);
    connect(deleteUserBtn, &QPushButton::clicked, this, [this]() {
        if (!userManager_ || !userListWidget_->currentItem()) return;
        int userId = userListWidget_->currentItem()->data(Qt::UserRole).toInt();
        QString name = userListWidget_->currentItem()->text();

        auto result = QMessageBox::question(this, tr("Eliminar cuenta"),
            tr("¿Eliminar la cuenta de %1?\n\nEsta acción no se puede deshacer.").arg(name));
        if (result != QMessageBox::Yes) return;

        userManager_->deleteUser(userId);
        refreshUserList();
    });
    listBtnRow->addWidget(deleteUserBtn);

    listLayout->addLayout(listBtnRow);
    layout->addWidget(listGroup);

    // Info de PIN
    auto* pinHelp = new QLabel(
        tr("El PIN debe tener entre 4 y 8 dígitos numéricos.\n"
           "Si una persona olvida su PIN, desde aquí puede cambiarlo."));
    pinHelp->setProperty("class", "dimLabel");
    pinHelp->setWordWrap(true);
    layout->addWidget(pinHelp);

    layout->addStretch();

    // Refresh on show
    refreshUserList();

    return page;
}

void SettingsDialog::refreshUserList()
{
    if (!userListWidget_ || !userManager_) return;
    userListWidget_->clear();

    auto users = userManager_->allUsers();
    for (const auto& u : users) {
        QString roleStr;
        switch (u.role) {
        case UserRole::Admin: roleStr = tr("Administración"); break;
        case UserRole::Programming: roleStr = tr("Programación"); break;
        default: roleStr = tr("Operación"); break;
        }
        auto* item = new QListWidgetItem(
            QString("%1  (%2) — %3").arg(u.displayName, u.username, roleStr));
        item->setData(Qt::UserRole, u.id);
        userListWidget_->addItem(item);
    }
}

void SettingsDialog::setUserRole(UserRole role)
{
    userRole_ = role;
    if (!sidebar_) return;

    // Tabs: 0=Perfil, 1=Interfaz, 2=Auto, 3=Tarjetas, 4=Config Audio,
    //       5=Hora, 6=Publicidad, 7=Pisadores, 8=Respaldo, 9=Personal
    if (role == UserRole::Operation) {
        for (int i = 0; i < sidebar_->count(); ++i) {
            bool visible = (i == 1 || i == 3 || i == 4);  // Interfaz, Tarjetas, Config Audio
            sidebar_->item(i)->setHidden(!visible);
        }
        sidebar_->setCurrentRow(1);
    } else if (role == UserRole::Programming) {
        for (int i = 0; i < sidebar_->count(); ++i) {
            sidebar_->item(i)->setHidden(i == 9);  // Ocultar Gestión de personal
        }
    } else {
        // Admin: todo visible
        for (int i = 0; i < sidebar_->count(); ++i) {
            sidebar_->item(i)->setHidden(false);
        }
    }
}

} // namespace sara

// ══════════════════════════════════════════════════════════
// Página: Pisadores
// ══════════════════════════════════════════════════════════

QWidget* sara::SettingsDialog::createPagePisadores()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Pisadores"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Los pisadores son jingles o IDs que suenan encima de la música "
           "al inicio de cada canción, bajando automáticamente el volumen de la pista."));
    desc->setProperty("class", "dimLabel");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // Activar pisadores
    pisadorEnabledCheck_ = new QCheckBox(tr("Activar sistema de pisadores"));
    pisadorEnabledCheck_->setChecked(config_.pisadorEnabled);
    layout->addWidget(pisadorEnabledCheck_);

    // Carpeta de pisadores
    auto* folderGroup = new QGroupBox(tr("Carpeta de Pisadores"));
    auto* folderLayout = new QHBoxLayout(folderGroup);
    pisadorFolderEdit_ = new QLineEdit(config_.pisadorFolder);
    pisadorFolderEdit_->setPlaceholderText(tr("Seleccione la carpeta con los archivos de pisador..."));
    auto* browseBtn = new QPushButton(tintSvg(":/icons/folder.svg", Qt::white), "");
    browseBtn->setFixedSize(30, 30);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this,
            tr("Carpeta de Pisadores"), pisadorFolderEdit_->text());
        if (!dir.isEmpty()) pisadorFolderEdit_->setText(dir);
    });
    folderLayout->addWidget(pisadorFolderEdit_, 1);
    folderLayout->addWidget(browseBtn);
    layout->addWidget(folderGroup);

    // Frecuencia
    auto* freqGroup = new QGroupBox(tr("Frecuencia"));
    auto* freqLayout = new QHBoxLayout(freqGroup);
    freqLayout->addWidget(new QLabel(tr("Pisar canciones:")));
    pisadorFreqCombo_ = new QComboBox();
    pisadorFreqCombo_->addItem(tr("Solo manual/individual"), -1);
    pisadorFreqCombo_->addItem(tr("Todas las canciones"), 0);
    pisadorFreqCombo_->addItem(tr("Intercalado (1 no, 1 sí)"), 1);
    pisadorFreqCombo_->addItem(tr("Cada 3 canciones (2 no, 1 sí)"), 2);
    pisadorFreqCombo_->addItem(tr("Cada 4 canciones (3 no, 1 sí)"), 3);
    pisadorFreqCombo_->addItem(tr("Cada 5 canciones (4 no, 1 sí)"), 4);
    int freqIdx = pisadorFreqCombo_->findData(config_.pisadorFrequency);
    if (freqIdx >= 0) pisadorFreqCombo_->setCurrentIndex(freqIdx);
    freqLayout->addWidget(pisadorFreqCombo_);
    freqLayout->addStretch();
    layout->addWidget(freqGroup);

    // Nivel de duck
    auto* duckGroup = new QGroupBox(tr("Nivel de Ducking"));
    auto* duckLayout = new QHBoxLayout(duckGroup);
    duckLayout->addWidget(new QLabel(tr("Volumen de la música durante el pisador:")));
    pisadorDuckSpin_ = new QSpinBox();
    pisadorDuckSpin_->setRange(10, 50);
    pisadorDuckSpin_->setSuffix("%");
    pisadorDuckSpin_->setValue(static_cast<int>(config_.pisadorDuckLevel * 100));
    pisadorDuckSpin_->setToolTip(tr("Porcentaje al que se reduce el volumen de la música "
                                    "mientras suena el pisador. Recomendado: 25%."));
    duckLayout->addWidget(pisadorDuckSpin_);
    duckLayout->addStretch();
    layout->addWidget(duckGroup);

    // Tiempo de entrada del pisador
    auto* delayGroup = new QGroupBox(tr("Tiempo de Entrada"));
    auto* delayLayout = new QHBoxLayout(delayGroup);
    delayLayout->addWidget(new QLabel(tr("El pisador entra a los:")));
    pisadorDelaySpin_ = new QSpinBox();
    pisadorDelaySpin_->setRange(1, 15);
    pisadorDelaySpin_->setSuffix(tr(" segundos"));
    pisadorDelaySpin_->setValue(config_.pisadorDelaySecs);
    pisadorDelaySpin_->setToolTip(tr("Segundos después del inicio de la canción "
                                     "en los que entrará el pisador."));
    delayLayout->addWidget(pisadorDelaySpin_);
    delayLayout->addStretch();
    layout->addWidget(delayGroup);

    // Carpetas excluidas
    auto* exclGroup = new QGroupBox(tr("Carpetas Excluidas (no pisar)"));
    auto* exclLayout = new QVBoxLayout(exclGroup);
    auto* exclDesc = new QLabel(tr("Las canciones dentro de estas carpetas nunca serán pisadas."));
    exclDesc->setProperty("class", "dimLabel");
    exclDesc->setWordWrap(true);
    exclLayout->addWidget(exclDesc);

    pisadorExcludedList_ = new QListWidget();
    pisadorExcludedList_->setMinimumHeight(60);
    pisadorExcludedList_->setMaximumHeight(150);
    pisadorExcludedList_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pisadorExcludedList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (const auto& folder : config_.pisadorExcludedFolders) {
        pisadorExcludedList_->addItem(folder);
    }
    exclLayout->addWidget(pisadorExcludedList_);

    auto* exclBtnRow = new QHBoxLayout();
    auto* addExclBtn = new QPushButton(tintSvg(":/icons/plus.svg", Qt::white), tr(" Agregar"));
    addExclBtn->setFixedHeight(26);
    connect(addExclBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this,
            tr("Carpeta a excluir de pisadores"));
        if (!dir.isEmpty()) {
            // Verificar duplicado
            for (int i = 0; i < pisadorExcludedList_->count(); ++i) {
                if (pisadorExcludedList_->item(i)->text() == dir) return;
            }
            pisadorExcludedList_->addItem(dir);
        }
    });
    auto* removeExclBtn = new QPushButton(tintSvg(":/icons/x.svg", Qt::white), "");
    removeExclBtn->setFixedSize(26, 26);
    connect(removeExclBtn, &QPushButton::clicked, this, [this]() {
        auto* item = pisadorExcludedList_->currentItem();
        if (item) delete item;
    });
    exclBtnRow->addWidget(addExclBtn);
    exclBtnRow->addStretch();
    exclBtnRow->addWidget(removeExclBtn);
    exclLayout->addLayout(exclBtnRow);
    layout->addWidget(exclGroup);

    // Habilitar/deshabilitar controles según checkbox
    auto updateEnabled = [this](bool on) {
        pisadorFolderEdit_->setEnabled(on);
        pisadorFreqCombo_->setEnabled(on);
        pisadorDuckSpin_->setEnabled(on);
        pisadorDelaySpin_->setEnabled(on);
        pisadorExcludedList_->setEnabled(on);
    };
    connect(pisadorEnabledCheck_, &QCheckBox::toggled, this, updateEnabled);
    updateEnabled(config_.pisadorEnabled);

    layout->addStretch();
    return page;
}

// ══════════════════════════════════════════════════════════
// Página: Respaldo
// ══════════════════════════════════════════════════════════

QWidget* sara::SettingsDialog::createPageBackup()
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(tr("Respaldo y Restauración"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("SARA Libre guarda copias automáticas de la base de datos y configuración. "
           "También puede exportar e importar la configuración completa para "
           "migrar a otro equipo."));
    desc->setProperty("class", "dimLabel");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // ── Backup automático ─────────────────────────────
    auto* autoGroup = new QGroupBox(tr("Backup Automático"));
    auto* autoLayout = new QVBoxLayout(autoGroup);

    backupEnabledCheck_ = new QCheckBox(tr("Activar backup automático"));
    backupEnabledCheck_->setChecked(config_.backupEnabled);
    autoLayout->addWidget(backupEnabledCheck_);

    auto* freqRow = new QHBoxLayout();
    freqRow->addWidget(new QLabel(tr("Frecuencia:")));
    backupIntervalCombo_ = new QComboBox();
    backupIntervalCombo_->addItem(tr("Cada 6 horas"), 6);
    backupIntervalCombo_->addItem(tr("Cada 12 horas"), 12);
    backupIntervalCombo_->addItem(tr("Cada 24 horas"), 24);
    backupIntervalCombo_->addItem(tr("Cada 48 horas"), 48);
    int intIdx = backupIntervalCombo_->findData(config_.backupIntervalHours);
    if (intIdx >= 0) backupIntervalCombo_->setCurrentIndex(intIdx);
    freqRow->addWidget(backupIntervalCombo_);
    freqRow->addStretch();
    autoLayout->addLayout(freqRow);

    auto* maxRow = new QHBoxLayout();
    maxRow->addWidget(new QLabel(tr("Copias a mantener:")));
    backupMaxCombo_ = new QComboBox();
    backupMaxCombo_->addItem("3", 3);
    backupMaxCombo_->addItem("5", 5);
    backupMaxCombo_->addItem("7", 7);
    backupMaxCombo_->addItem("10", 10);
    int maxIdx = backupMaxCombo_->findData(config_.backupMaxCount);
    if (maxIdx >= 0) backupMaxCombo_->setCurrentIndex(maxIdx);
    maxRow->addWidget(backupMaxCombo_);
    maxRow->addStretch();
    autoLayout->addLayout(maxRow);

    layout->addWidget(autoGroup);

    // ── Backups disponibles ───────────────────────────
    auto* listGroup = new QGroupBox(tr("Backups Disponibles"));
    auto* listLayout = new QVBoxLayout(listGroup);

    backupListWidget_ = new QListWidget();
    backupListWidget_->setMaximumHeight(140);
    listLayout->addWidget(backupListWidget_);

    auto* listBtnRow = new QHBoxLayout();

    auto* restoreBtn = new QPushButton(tintSvg(":/icons/rotate-ccw.svg", Qt::white), tr(" Restaurar seleccionado"));
    restoreBtn->setFixedHeight(28);
    connect(restoreBtn, &QPushButton::clicked, this, [this]() {
        if (!backupManager_) return;
        auto* item = backupListWidget_->currentItem();
        if (!item) {
            QMessageBox::information(this, tr("Restaurar"),
                tr("Seleccione un backup de la lista."));
            return;
        }
        QString path = item->data(Qt::UserRole).toString();
        auto answer = QMessageBox::question(this, tr("Restaurar backup"),
            tr("¿Restaurar este backup?\n\n"
               "Se creará un backup de seguridad de la configuración actual "
               "antes de restaurar.\n\n"
               "SARA Libre se cerrará y deberá reiniciarla manualmente."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        if (backupManager_->restoreBackup(path)) {
            QMessageBox::information(this, tr("Restauración completada"),
                tr("La configuración fue restaurada.\n"
                   "SARA Libre se cerrará ahora. Reiníciela para aplicar los cambios."));
            qApp->quit();
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo restaurar el backup."));
        }
    });

    auto* backupNowBtn = new QPushButton(tintSvg(":/icons/save.svg", Qt::white), tr(" Backup ahora"));
    backupNowBtn->setFixedHeight(28);
    connect(backupNowBtn, &QPushButton::clicked, this, [this]() {
        if (!backupManager_) return;
        if (backupManager_->createBackup()) {
            refreshBackupList();
            QMessageBox::information(this, tr("Backup"),
                tr("Backup creado correctamente."));
        } else {
            QMessageBox::warning(this, tr("Error"),
                tr("No se pudo crear el backup."));
        }
    });

    auto* deleteBackupBtn = new QPushButton(tintSvg(":/icons/x.svg", Qt::white), tr(" Eliminar"));
    deleteBackupBtn->setFixedHeight(28);
    connect(deleteBackupBtn, &QPushButton::clicked, this, [this]() {
        if (!backupManager_) return;
        auto* item = backupListWidget_->currentItem();
        if (!item) {
            QMessageBox::information(this, tr("Eliminar"),
                tr("Seleccione un backup de la lista."));
            return;
        }
        QString path = item->data(Qt::UserRole).toString();
        QString name = item->text().split(QString::fromUtf8(" — ")).last().trimmed();
        auto answer = QMessageBox::question(this, tr("Eliminar backup"),
            tr("¿Eliminar este backup?\n%1").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        QFile::remove(path);
        refreshBackupList();
    });

    auto* downloadBackupBtn = new QPushButton(tintSvg(":/icons/download.svg", Qt::white), tr(" Descargar"));
    downloadBackupBtn->setFixedHeight(28);
    connect(downloadBackupBtn, &QPushButton::clicked, this, [this]() {
        if (!backupListWidget_ || backupListWidget_->currentRow() < 0) {
            QMessageBox::information(this, tr("Descargar"), tr("Seleccione un backup de la lista."));
            return;
        }
        QString path = backupListWidget_->currentItem()->data(Qt::UserRole).toString();
        if (path.isEmpty() || !QFileInfo::exists(path)) return;

        QString dest = QFileDialog::getSaveFileName(this, tr("Guardar backup como..."),
            QDir::homePath() + "/" + QFileInfo(path).fileName(),
            tr("Archivos de base de datos (*.db);;Todos (*)"));
        if (dest.isEmpty()) return;

        if (QFile::copy(path, dest)) {
            QMessageBox::information(this, tr("Descargar"),
                tr("Backup guardado en:\n%1").arg(dest));
        } else {
            QMessageBox::warning(this, tr("Error"),
                tr("No se pudo copiar el archivo a:\n%1").arg(dest));
        }
    });

    listBtnRow->addWidget(backupNowBtn);
    listBtnRow->addStretch();
    listBtnRow->addWidget(downloadBackupBtn);
    listBtnRow->addWidget(deleteBackupBtn);
    listBtnRow->addWidget(restoreBtn);
    listLayout->addLayout(listBtnRow);

    layout->addWidget(listGroup);

    // ── Exportar / Importar ──────────────────────────
    auto* portGroup = new QGroupBox(tr("Exportar / Importar"));
    auto* portLayout = new QVBoxLayout(portGroup);

    auto* portDesc = new QLabel(
        tr("Exporte la configuración completa (programaciones, eventos, pisadores, "
           "auditoría) a un archivo .sara para llevar a otro equipo."));
    portDesc->setProperty("class", "dimLabel");
    portDesc->setWordWrap(true);
    portLayout->addWidget(portDesc);

    auto* portBtnRow = new QHBoxLayout();

    auto* exportBtn = new QPushButton(tintSvg(":/icons/download.svg", Qt::white), tr(" Exportar"));
    exportBtn->setFixedHeight(28);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        if (!backupManager_) return;
        QString defaultName = "saralibre_" +
            QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".sara";
        QString path = QFileDialog::getSaveFileName(this,
            tr("Exportar configuración"), QDir::homePath() + "/" + defaultName,
            tr("SARA Libre (*.sara)"));
        if (path.isEmpty()) return;

        if (backupManager_->exportTo(path)) {
            QMessageBox::information(this, tr("Exportación completada"),
                tr("Configuración exportada correctamente a:\n%1").arg(path));
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo exportar la configuración."));
        }
    });

    auto* importBtn = new QPushButton(tintSvg(":/icons/upload.svg", Qt::white), tr(" Importar"));
    importBtn->setFixedHeight(28);
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        if (!backupManager_) return;
        QString path = QFileDialog::getOpenFileName(this,
            tr("Importar configuración"), QDir::homePath(),
            tr("SARA Libre (*.sara);;Backup (*.tar.gz)"));
        if (path.isEmpty()) return;

        auto answer = QMessageBox::question(this, tr("Importar configuración"),
            tr("¿Importar esta configuración?\n\n"
               "Se reemplazará la base de datos y configuración actuales.\n"
               "Se creará un backup de seguridad antes de importar.\n\n"
               "SARA Libre se cerrará y deberá reiniciarla manualmente."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        if (backupManager_->importFrom(path)) {
            QMessageBox::information(this, tr("Importación completada"),
                tr("La configuración fue importada.\n"
                   "SARA Libre se cerrará ahora. Reiníciela para aplicar los cambios."));
            qApp->quit();
        } else {
            QMessageBox::critical(this, tr("Error"),
                tr("No se pudo importar la configuración."));
        }
    });

    portBtnRow->addWidget(exportBtn);
    portBtnRow->addWidget(importBtn);
    portBtnRow->addStretch();
    portLayout->addLayout(portBtnRow);

    layout->addWidget(portGroup);

    // Habilitar/deshabilitar según checkbox
    auto updateEnabled = [=](bool on) {
        backupIntervalCombo_->setEnabled(on);
        backupMaxCombo_->setEnabled(on);
    };
    connect(backupEnabledCheck_, &QCheckBox::toggled, this, updateEnabled);
    updateEnabled(config_.backupEnabled);

    layout->addStretch();
    return page;
}

void sara::SettingsDialog::refreshBackupList()
{
    if (!backupListWidget_) return;
    backupListWidget_->clear();

    if (!backupManager_) {
        backupListWidget_->addItem(tr("(BackupManager no disponible)"));
        return;
    }

    auto backups = backupManager_->availableBackups();
    if (backups.isEmpty()) {
        backupListWidget_->addItem(tr("No hay backups disponibles"));
        return;
    }

    for (const auto& b : backups) {
        double sizeMB = b.sizeBytes / (1024.0 * 1024.0);
        QString label = QString("%1  —  %2  (%3 MB)")
            .arg(b.created.toString("dd/MM/yyyy HH:mm"))
            .arg(b.fileName)
            .arg(sizeMB, 0, 'f', 2);
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, b.filePath);
        backupListWidget_->addItem(item);
    }
}
