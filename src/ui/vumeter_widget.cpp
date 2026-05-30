#include "ui/vumeter_widget.h"

#include <QPainter>
#include <cmath>

namespace sara {

static constexpr int NUM_SEGMENTS = 28;    // Segmentos LED por canal
static constexpr int GREEN_END    = 18;    // 0-17: verde
static constexpr int YELLOW_END   = 23;    // 18-22: amarillo
                                            // 23-27: rojo

VuMeterWidget::VuMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(32);
    setMinimumWidth(80);

    decayTimer_ = new QTimer(this);
    decayTimer_->setInterval(50);
    connect(decayTimer_, &QTimer::timeout, this, [this]() {
        leftLevel_  *= 0.85;
        rightLevel_ *= 0.85;
        leftPeak_   *= 0.97;
        rightPeak_  *= 0.97;

        if (leftLevel_ < 0.001 && rightLevel_ < 0.001 &&
            leftPeak_ < 0.001 && rightPeak_ < 0.001) {
            leftLevel_ = rightLevel_ = leftPeak_ = rightPeak_ = 0.0;
        }
        update();
    });
    decayTimer_->start();
}

double VuMeterWidget::dbToFraction(double db) const
{
    if (db <= -60.0) return 0.0;
    if (db >= 0.0) return 1.0;
    return (db + 60.0) / 60.0;
}

void VuMeterWidget::setLevels(double leftDb, double rightDb)
{
    double newLeft = dbToFraction(leftDb);
    double newRight = dbToFraction(rightDb);

    if (newLeft > leftLevel_) leftLevel_ = newLeft;
    if (newRight > rightLevel_) rightLevel_ = newRight;
    if (newLeft > leftPeak_) leftPeak_ = newLeft;
    if (newRight > rightPeak_) rightPeak_ = newRight;

    update();
}

void VuMeterWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();
    int labelW = 14;
    int barX = labelW;
    int barW = w - barX - 2;
    int gap = 3;
    int barH = (h - gap - 2) / 2;
    int barY1 = 1;
    int barY2 = barY1 + barH + gap;

    // Labels L/R
    p.setPen(QColor(120, 120, 160));
    QFont f = font();
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(0, barY1, labelW, barH, Qt::AlignCenter, "L");
    p.drawText(0, barY2, labelW, barH, Qt::AlignCenter, "R");

    // Color por segmento
    auto segColor = [](int seg, bool lit) -> QColor {
        if (seg < GREEN_END) {
            return lit ? QColor(34, 197, 94) : QColor(34, 197, 94, 25);
        } else if (seg < YELLOW_END) {
            return lit ? QColor(245, 158, 11) : QColor(245, 158, 11, 25);
        } else {
            return lit ? QColor(239, 68, 68) : QColor(239, 68, 68, 25);
        }
    };

    // Dibujar barra segmentada
    auto drawSegBar = [&](int y, int bh, double level, double peak) {
        int litSegs = static_cast<int>(level * NUM_SEGMENTS);
        int peakSeg = static_cast<int>(peak * NUM_SEGMENTS);
        peakSeg = qBound(0, peakSeg, NUM_SEGMENTS - 1);

        double segTotalW = static_cast<double>(barW) / NUM_SEGMENTS;
        int segGap = 1;

        for (int i = 0; i < NUM_SEGMENTS; ++i) {
            int sx = barX + static_cast<int>(i * segTotalW);
            int sw = static_cast<int>((i + 1) * segTotalW) - static_cast<int>(i * segTotalW) - segGap;
            if (sw < 1) sw = 1;

            bool lit = (i < litSegs);
            bool isPeak = (i == peakSeg && peak > 0.02);

            QColor color;
            if (lit) {
                color = segColor(i, true);
            } else if (isPeak) {
                color = segColor(i, true);
                color.setAlpha(200);
            } else {
                color = segColor(i, false);
            }

            p.fillRect(sx, y, sw, bh, color);
        }
    };

    drawSegBar(barY1, barH, leftLevel_, leftPeak_);
    drawSegBar(barY2, barH, rightLevel_, rightPeak_);
}

} // namespace sara
