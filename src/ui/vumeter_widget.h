#ifndef SARA_UI_VUMETER_WIDGET_H
#define SARA_UI_VUMETER_WIDGET_H

#include <QWidget>
#include <QTimer>
#include <QPair>

namespace sara {

/**
 * Widget de VU Meter estéreo con pico.
 * Muestra barras L/R con niveles RMS y peak hold.
 * Colores: verde → amarillo → rojo según nivel.
 */
class VuMeterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VuMeterWidget(QWidget* parent = nullptr);

    /// Actualizar niveles (en dB, típicamente -60 a 0)
    void setLevels(double leftDb, double rightDb);

    QSize sizeHint() const override { return QSize(120, 32); }
    QSize minimumSizeHint() const override { return QSize(80, 24); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Convertir dB a fracción 0.0-1.0 para dibujo
    double dbToFraction(double db) const;

    double leftLevel_  = 0.0;   // Fracción 0-1
    double rightLevel_ = 0.0;
    double leftPeak_   = 0.0;   // Peak hold
    double rightPeak_  = 0.0;

    QTimer* decayTimer_;
};

} // namespace sara

#endif
