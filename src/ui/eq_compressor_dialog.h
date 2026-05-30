#ifndef SARA_UI_EQ_COMPRESSOR_DIALOG_H
#define SARA_UI_EQ_COMPRESSOR_DIALOG_H

#include "core/types.h"
#include <QDialog>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QVector>
#include <QMap>

namespace sara {

class EqCompressorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EqCompressorDialog(const AppConfig& config, QWidget* parent = nullptr);

    bool eqEnabled() const;
    QVector<double> eqBands() const;
    QString currentPresetName() const;
    bool compressorEnabled() const;
    double compressorThresholdDb() const;
    double compressorRatio() const;

signals:
    void parametersChanged();

private:
    void loadPreset(int index);
    void applyBands(const QVector<double>& bands);
    void saveCustomPreset();
    void deleteCustomPreset();
    void loadCustomPresets();
    void saveCustomPresets();
    void rebuildPresetCombo(const QString& selectName = QString());
    QString presetsFilePath() const;

    static QMap<QString, QVector<double>> builtinPresets();

    QCheckBox*         eqCheck_;
    QVector<QSlider*>  bandSliders_;
    QVector<QLabel*>   bandLabels_;
    QComboBox*         presetCombo_;

    QCheckBox*         compCheck_;
    QSlider*           thresholdSlider_;
    QSlider*           ratioSlider_;
    QLabel*            thresholdLabel_;
    QLabel*            ratioLabel_;

    QMap<QString, QVector<double>> customPresets_;
    bool suppressPresetChange_ = false;
};

} // namespace sara
#endif
