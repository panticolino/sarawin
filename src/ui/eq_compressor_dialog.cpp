#include "ui/eq_compressor_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

namespace sara {

static const char* BAND_LABELS[] = {
    "29", "59", "119", "237", "474", "947", "1.9k", "3.8k", "7.5k", "15k"
};

QMap<QString, QVector<double>> EqCompressorDialog::builtinPresets()
{
    QMap<QString, QVector<double>> p;
    p["Plano"]              = {0,0,0,0,0,0,0,0,0,0};
    p["Voz (claridad)"]     = {-2,-1,0,2,4,4,3,2,1,0};
    p["Música (cálido)"]    = {4,3,1,0,-1,0,1,2,3,2};
    p["Boost Graves"]       = {6,5,3,1,0,0,0,0,0,0};
    p["Broadcast Radio"]    = {3,2,1,2,3,3,2,3,4,3};
    return p;
}

EqCompressorDialog::EqCompressorDialog(const AppConfig& config, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Ecualizador y Compresor"));
    setMinimumSize(640, 440);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    loadCustomPresets();

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 12, 16, 12);

    // ── Ecualizador ──────────────────────────────────
    auto* eqGroup = new QGroupBox(tr("Ecualizador de 10 bandas"));
    auto* eqLayout = new QVBoxLayout(eqGroup);

    eqCheck_ = new QCheckBox(tr("Activar ecualizador"));
    eqCheck_->setChecked(config.eqEnabled);
    connect(eqCheck_, &QCheckBox::toggled, this, &EqCompressorDialog::parametersChanged);
    eqLayout->addWidget(eqCheck_);

    // Preset row
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Preset:")));
    presetCombo_ = new QComboBox();
    presetCombo_->setFixedWidth(200);
    presetRow->addWidget(presetCombo_);

    auto* savePresetBtn = new QPushButton(tr("Guardar"));
    savePresetBtn->setFixedHeight(26);
    savePresetBtn->setToolTip(tr("Guardar preset personalizado con los valores actuales"));
    connect(savePresetBtn, &QPushButton::clicked, this, &EqCompressorDialog::saveCustomPreset);
    presetRow->addWidget(savePresetBtn);

    auto* deletePresetBtn = new QPushButton(tr("Eliminar"));
    deletePresetBtn->setFixedHeight(26);
    deletePresetBtn->setToolTip(tr("Eliminar el preset personalizado seleccionado"));
    connect(deletePresetBtn, &QPushButton::clicked, this, &EqCompressorDialog::deleteCustomPreset);
    presetRow->addWidget(deletePresetBtn);

    presetRow->addStretch();

    auto* flatBtn = new QPushButton(tr("Resetear"));
    flatBtn->setFixedHeight(26);
    connect(flatBtn, &QPushButton::clicked, this, [this]() {
        applyBands({0,0,0,0,0,0,0,0,0,0});
        rebuildPresetCombo("Plano");
        emit parametersChanged();
    });
    presetRow->addWidget(flatBtn);
    eqLayout->addLayout(presetRow);

    // Sliders
    auto* slidersRow = new QHBoxLayout();
    slidersRow->setSpacing(4);

    auto* dbCol = new QVBoxLayout();
    auto* dbPlus = new QLabel("+12");
    dbPlus->setStyleSheet("font-size: 10px;");
    dbCol->addWidget(dbPlus);
    dbCol->addStretch();
    auto* dbZero = new QLabel("  0");
    dbZero->setStyleSheet("font-size: 10px;");
    dbCol->addWidget(dbZero);
    dbCol->addStretch();
    auto* dbMinus = new QLabel("-24");
    dbMinus->setStyleSheet("font-size: 10px;");
    dbCol->addWidget(dbMinus);
    auto* dbHz = new QLabel("Hz");
    dbHz->setAlignment(Qt::AlignCenter);
    dbHz->setStyleSheet("font-size: 10px;");
    dbCol->addWidget(dbHz);
    slidersRow->addLayout(dbCol);

    for (int i = 0; i < 10; ++i) {
        auto* col = new QVBoxLayout();
        col->setSpacing(2);

        auto* valueLabel = new QLabel("0");
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setFixedWidth(36);
        valueLabel->setStyleSheet("font-size: 10px;");
        bandLabels_.append(valueLabel);
        col->addWidget(valueLabel);

        auto* slider = new QSlider(Qt::Vertical);
        slider->setRange(-240, 120);
        slider->setValue(static_cast<int>(config.eqBands.value(i, 0.0) * 10));
        slider->setFixedWidth(36);
        slider->setMinimumHeight(120);
        bandSliders_.append(slider);

        connect(slider, &QSlider::valueChanged, this, [this, i, valueLabel](int val) {
            double db = val / 10.0;
            valueLabel->setText(QString("%1").arg(db, 0, 'f', 1));
            if (!suppressPresetChange_) {
                // Marcar como personalizado si se movió un slider
                suppressPresetChange_ = true;
                presetCombo_->setCurrentIndex(0);
                suppressPresetChange_ = false;
            }
            emit parametersChanged();
        });
        valueLabel->setText(QString("%1").arg(config.eqBands.value(i, 0.0), 0, 'f', 1));

        col->addWidget(slider, 1, Qt::AlignHCenter);

        auto* freqLabel = new QLabel(BAND_LABELS[i]);
        freqLabel->setAlignment(Qt::AlignCenter);
        freqLabel->setStyleSheet("font-size: 10px;");
        col->addWidget(freqLabel);

        slidersRow->addLayout(col);
    }

    eqLayout->addLayout(slidersRow);
    layout->addWidget(eqGroup);

    // ── Compresor ────────────────────────────────────
    auto* compGroup = new QGroupBox(tr("Compresor / Limiter"));
    auto* compLayout = new QVBoxLayout(compGroup);

    compCheck_ = new QCheckBox(tr("Activar compresor"));
    compCheck_->setChecked(config.compressorEnabled);
    connect(compCheck_, &QCheckBox::toggled, this, &EqCompressorDialog::parametersChanged);
    compLayout->addWidget(compCheck_);

    auto* compParams = new QHBoxLayout();

    auto* threshCol = new QVBoxLayout();
    threshCol->addWidget(new QLabel(tr("Umbral (Threshold)")));
    thresholdSlider_ = new QSlider(Qt::Horizontal);
    thresholdSlider_->setRange(-400, 0);
    thresholdSlider_->setValue(static_cast<int>(config.compressorThresholdDb * 10));
    thresholdLabel_ = new QLabel(QString("%1 dB").arg(config.compressorThresholdDb, 0, 'f', 1));
    connect(thresholdSlider_, &QSlider::valueChanged, this, [this](int val) {
        thresholdLabel_->setText(QString("%1 dB").arg(val / 10.0, 0, 'f', 1));
        emit parametersChanged();
    });
    threshCol->addWidget(thresholdSlider_);
    threshCol->addWidget(thresholdLabel_);
    compParams->addLayout(threshCol);

    compParams->addSpacing(20);

    auto* ratioCol = new QVBoxLayout();
    ratioCol->addWidget(new QLabel(tr("Ratio")));
    ratioSlider_ = new QSlider(Qt::Horizontal);
    ratioSlider_->setRange(10, 200);
    ratioSlider_->setValue(static_cast<int>(config.compressorRatio * 10));
    ratioLabel_ = new QLabel(QString("%1:1").arg(config.compressorRatio, 0, 'f', 1));
    connect(ratioSlider_, &QSlider::valueChanged, this, [this](int val) {
        ratioLabel_->setText(QString("%1:1").arg(val / 10.0, 0, 'f', 1));
        emit parametersChanged();
    });
    ratioCol->addWidget(ratioSlider_);
    ratioCol->addWidget(ratioLabel_);
    compParams->addLayout(ratioCol);

    compLayout->addLayout(compParams);
    layout->addWidget(compGroup);

    // Botones
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Cerrar"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    // Construir combo de presets y seleccionar el último usado
    rebuildPresetCombo(config.eqPresetName);

    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EqCompressorDialog::loadPreset);
}

bool EqCompressorDialog::eqEnabled() const { return eqCheck_->isChecked(); }

QVector<double> EqCompressorDialog::eqBands() const
{
    QVector<double> bands;
    for (auto* s : bandSliders_) bands.append(s->value() / 10.0);
    return bands;
}

QString EqCompressorDialog::currentPresetName() const
{
    if (presetCombo_->currentIndex() == 0) return QString();
    return presetCombo_->currentText();
}

bool EqCompressorDialog::compressorEnabled() const { return compCheck_->isChecked(); }
double EqCompressorDialog::compressorThresholdDb() const { return thresholdSlider_->value() / 10.0; }
double EqCompressorDialog::compressorRatio() const { return ratioSlider_->value() / 10.0; }

void EqCompressorDialog::applyBands(const QVector<double>& bands)
{
    suppressPresetChange_ = true;
    for (int i = 0; i < qMin(bands.size(), bandSliders_.size()); ++i) {
        bandSliders_[i]->setValue(static_cast<int>(bands[i] * 10));
        bandLabels_[i]->setText(QString("%1").arg(bands[i], 0, 'f', 1));
    }
    suppressPresetChange_ = false;
}

void EqCompressorDialog::loadPreset(int index)
{
    if (suppressPresetChange_ || index < 0) return;
    if (index == 0) return;  // "Personalizado" — no tocar sliders

    QString name = presetCombo_->currentText();

    // Buscar en builtins
    auto builtin = builtinPresets();
    if (builtin.contains(name)) {
        applyBands(builtin[name]);
        emit parametersChanged();
        return;
    }

    // Buscar en custom
    if (customPresets_.contains(name)) {
        applyBands(customPresets_[name]);
        emit parametersChanged();
    }
}

void EqCompressorDialog::saveCustomPreset()
{
    bool ok;
    QString name = QInputDialog::getText(this, tr("Guardar preset"),
        tr("Nombre del preset:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    // No permitir sobreescribir builtins
    if (builtinPresets().contains(name)) {
        QMessageBox::warning(this, tr("Nombre reservado"),
            tr("No puede usar el nombre de un preset predefinido."));
        return;
    }

    customPresets_[name] = eqBands();
    saveCustomPresets();
    rebuildPresetCombo(name);

    QMessageBox::information(this, tr("Preset guardado"),
        tr("Preset \"%1\" guardado correctamente.").arg(name));
}

void EqCompressorDialog::deleteCustomPreset()
{
    QString name = presetCombo_->currentText();
    if (name.isEmpty() || presetCombo_->currentIndex() == 0) return;

    // No eliminar builtins
    if (builtinPresets().contains(name)) {
        QMessageBox::warning(this, tr("No se puede eliminar"),
            tr("Los presets predefinidos no se pueden eliminar."));
        return;
    }

    if (!customPresets_.contains(name)) return;

    auto result = QMessageBox::question(this, tr("Eliminar preset"),
        tr("¿Eliminar el preset \"%1\"?").arg(name));
    if (result != QMessageBox::Yes) return;

    customPresets_.remove(name);
    saveCustomPresets();
    rebuildPresetCombo();
}

void EqCompressorDialog::rebuildPresetCombo(const QString& selectName)
{
    suppressPresetChange_ = true;
    presetCombo_->clear();

    presetCombo_->addItem(tr("Personalizado"));

    // Builtins
    auto builtin = builtinPresets();
    QStringList builtinOrder = {"Plano", "Voz (claridad)", "Música (cálido)", "Boost Graves", "Broadcast Radio"};
    for (const auto& name : builtinOrder) {
        presetCombo_->addItem(name);
    }

    // Separador visual
    if (!customPresets_.isEmpty()) {
        presetCombo_->insertSeparator(presetCombo_->count());
        for (auto it = customPresets_.begin(); it != customPresets_.end(); ++it) {
            presetCombo_->addItem(it.key());
        }
    }

    // Seleccionar el preset indicado
    int idx = 0;
    if (!selectName.isEmpty()) {
        for (int i = 0; i < presetCombo_->count(); ++i) {
            if (presetCombo_->itemText(i) == selectName) {
                idx = i;
                break;
            }
        }
    }
    presetCombo_->setCurrentIndex(idx);
    suppressPresetChange_ = false;
}

QString EqCompressorDialog::presetsFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    return configDir + "/eq_presets.json";
}

void EqCompressorDialog::loadCustomPresets()
{
    QFile file(presetsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        QJsonArray arr = it.value().toArray();
        QVector<double> bands;
        for (const auto& v : arr) bands.append(v.toDouble());
        while (bands.size() < 10) bands.append(0.0);
        customPresets_[it.key()] = bands;
    }
}

void EqCompressorDialog::saveCustomPresets()
{
    QJsonObject obj;
    for (auto it = customPresets_.begin(); it != customPresets_.end(); ++it) {
        QJsonArray arr;
        for (double v : it.value()) arr.append(v);
        obj[it.key()] = arr;
    }
    QFile file(presetsFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        file.close();
    }
}

} // namespace sara
