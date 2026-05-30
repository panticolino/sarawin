#include "ui/audit_dashboard.h"
#include "data/audit_manager.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QFrame>
#include <QScrollArea>
#include <QInputDialog>
#include <QDir>
#include <QListWidget>
#include <QShortcut>
#include <QKeySequence>
#include <QPdfWriter>
#include <QPageSize>
#include <QFile>
#include <QTextStream>
#include <QPageLayout>

namespace sara {

// ── Widget de gráfico de barras pintado con QPainter ─────

class BarChartWidget : public QWidget
{
public:
    struct Bar {
        QString label;
        int     value;
        QColor  color;
    };

    explicit BarChartWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(140);
    }

    void setData(const QVector<Bar>& bars, const QString& title) {
        bars_ = bars;
        title_ = title;
        maxVal_ = 0;
        for (const auto& b : bars_) {
            if (b.value > maxVal_) maxVal_ = b.value;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int w = width();
        int h = height();
        int margin = 8;
        int titleH = 22;
        int labelH = 16;
        int chartH = h - margin * 2 - titleH - labelH;
        int chartY = margin + titleH;

        // Título
        p.setPen(QColor("#c0c0e0"));
        p.setFont(QFont("sans-serif", 10, QFont::Bold));
        p.drawText(margin, margin + 14, title_);

        if (bars_.isEmpty() || maxVal_ == 0) {
            p.setPen(QColor("#505070"));
            p.drawText(rect(), Qt::AlignCenter, "Sin datos");
            return;
        }

        int barCount = bars_.size();
        qreal barW = qreal(w - margin * 2) / barCount;
        qreal gap = barW * 0.15;

        for (int i = 0; i < barCount; ++i) {
            qreal x = margin + i * barW + gap;
            qreal bw = barW - gap * 2;
            qreal bh = (qreal(bars_[i].value) / maxVal_) * chartH;
            qreal y = chartY + chartH - bh;

            // Barra
            p.setBrush(bars_[i].color);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(x, y, bw, bh), 3, 3);

            // Valor encima
            if (bars_[i].value > 0) {
                p.setPen(palette().color(QPalette::PlaceholderText));
                p.setFont(QFont("sans-serif", 8));
                p.drawText(QRectF(x, y - 14, bw, 14), Qt::AlignCenter,
                           QString::number(bars_[i].value));
            }

            // Label debajo
            p.setPen(palette().color(QPalette::PlaceholderText));
            p.setFont(QFont("sans-serif", 7));
            p.drawText(QRectF(x, chartY + chartH + 2, bw, labelH),
                       Qt::AlignCenter, bars_[i].label);
        }
    }

private:
    QVector<Bar> bars_;
    QString title_;
    int maxVal_ = 0;
};

// ══════════════════════════════════════════════════════════
// AuditDashboard
// ══════════════════════════════════════════════════════════

AuditDashboard::AuditDashboard(AuditManager* audit, QWidget* parent)
    : QDialog(parent), audit_(audit)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Auditoría — Historial de Reproducción"));
    setMinimumSize(860, 560);
    resize(960, 640);
    setupUI();
    onRefresh();
}

void AuditDashboard::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 12, 16, 12);

    auto* titleLabel = new QLabel(tr("Auditoría de Reproducción"));
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 700;");
    layout->addWidget(titleLabel);

    // ── Filtros ──────────────────────────────────────
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    filterRow->addWidget(new QLabel(tr("Desde:")));
    fromDate_ = new QDateEdit(QDate::currentDate().addDays(-7));
    fromDate_->setCalendarPopup(true);
    fromDate_->setDisplayFormat("dd/MM/yyyy");
    filterRow->addWidget(fromDate_);

    filterRow->addWidget(new QLabel(tr("Hasta:")));
    toDate_ = new QDateEdit(QDate::currentDate());
    toDate_->setCalendarPopup(true);
    toDate_->setDisplayFormat("dd/MM/yyyy");
    filterRow->addWidget(toDate_);

    sourceFilter_ = new QComboBox();
    sourceFilter_->addItem(tr("Todas las fuentes"));
    sourceFilter_->addItem(tr("Alternativa"), "fallback");
    sourceFilter_->addItem(tr("Programación"), "schedule:");
    sourceFilter_->addItem(tr("Evento"), "event:");
    sourceFilter_->setEditable(false);
    sourceFilter_->setMinimumWidth(130);

    // Agregar monitores existentes al filtro
    auto monitors = audit_->getMonitors();
    if (!monitors.isEmpty()) {
        sourceFilter_->insertSeparator(sourceFilter_->count());
        for (const auto& m : monitors) {
            sourceFilter_->addItem("📊 " + m.name, "monitor:" + m.id);
        }
    }
    filterRow->addWidget(sourceFilter_);

    monitorBtn_ = new QPushButton(QIcon(":/icons/settings.svg"), " Monitores");
    monitorBtn_->setProperty("class", "secondaryButton");
    monitorBtn_->setToolTip(tr("Gestionar monitores de auditoría (Música Nacional, etc.)"));
    connect(monitorBtn_, &QPushButton::clicked, this, &AuditDashboard::onManageMonitors);
    filterRow->addWidget(monitorBtn_);

    auto* refreshBtn = new QPushButton(QIcon(":/icons/search.svg"), " Consultar");
    connect(refreshBtn, &QPushButton::clicked, this, &AuditDashboard::onRefresh);
    filterRow->addWidget(refreshBtn);

    layout->addLayout(filterRow);

    // ── Pestañas ─────────────────────────────────────
    tabs_ = new QTabWidget();

    // ── Pestaña: Historial ───────────────────────────
    auto* histWidget = new QWidget();
    auto* histLayout = new QVBoxLayout(histWidget);
    histLayout->setContentsMargins(0, 6, 0, 0);

    historyTable_ = new QTableWidget(0, 5);
    historyTable_->setHorizontalHeaderLabels(
        {tr("Fecha/Hora"), "Título", "Fuente", "Duración", "✓"});
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable_->setShowGrid(false);
    historyTable_->verticalHeader()->setVisible(false);
    historyTable_->verticalHeader()->setDefaultSectionSize(24);

    historyTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    historyTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    historyTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    historyTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    historyTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    historyTable_->setColumnWidth(0, 130);
    historyTable_->setColumnWidth(2, 140);
    historyTable_->setColumnWidth(3, 65);
    historyTable_->setColumnWidth(4, 35);

    histLayout->addWidget(historyTable_, 1);

    auto* histBottomRow = new QHBoxLayout();
    historyCountLabel_ = new QLabel();
    historyCountLabel_->setStyleSheet("font-size: 11px;");

    auto* exportBtn = new QPushButton(QIcon(":/icons/save.svg"), " Exportar CSV");
    exportBtn->setProperty("class", "secondaryButton");
    connect(exportBtn, &QPushButton::clicked, this, &AuditDashboard::onExportCSV);

    histBottomRow->addWidget(historyCountLabel_);
    histBottomRow->addStretch();
    histBottomRow->addWidget(exportBtn);
    histLayout->addLayout(histBottomRow);

    tabs_->addTab(histWidget, tr("Historial"));

    // ── Pestaña: Estadísticas ────────────────────────
    auto* statsScroll = new QScrollArea();
    statsScroll->setWidgetResizable(true);
    statsScroll->setFrameShape(QFrame::NoFrame);

    auto* statsWidget = new QWidget();
    auto* statsLayout = new QVBoxLayout(statsWidget);
    statsLayout->setSpacing(12);

    // Resumen numérico
    auto* summaryRow = new QHBoxLayout();
    totalPlaysLabel_ = new QLabel();
    totalPlaysLabel_->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #667eea;");
    totalDurationLabel_ = new QLabel();
    totalDurationLabel_->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #22c55e;");
    summaryRow->addWidget(totalPlaysLabel_);
    summaryRow->addSpacing(30);
    summaryRow->addWidget(totalDurationLabel_);
    summaryRow->addStretch();
    statsLayout->addLayout(summaryRow);

    // Gráfico: distribución por hora
    hourChartWidget_ = new BarChartWidget();
    statsLayout->addWidget(hourChartWidget_);

    // Gráfico: distribución por fuente
    sourceChartWidget_ = new BarChartWidget();
    sourceChartWidget_->setMinimumHeight(100);
    statsLayout->addWidget(sourceChartWidget_);

    // Monitores de auditoría (Música Nacional, etc.)
    monitorStatsWidget_ = new QWidget();
    monitorStatsWidget_->setVisible(false);  // Se muestra si hay monitores
    statsLayout->addWidget(monitorStatsWidget_);

    // Top tracks
    auto* topGroup = new QGroupBox(tr("Top Pistas Más Reproducidas"));
    auto* topLayout = new QVBoxLayout(topGroup);

    topTracksTable_ = new QTableWidget(0, 4);
    topTracksTable_->setHorizontalHeaderLabels({"#", tr("Título"), "Reproducciones", "Tiempo total"});
    topTracksTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    topTracksTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    topTracksTable_->setShowGrid(false);
    topTracksTable_->verticalHeader()->setVisible(false);
    topTracksTable_->verticalHeader()->setDefaultSectionSize(24);
    topTracksTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    topTracksTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    topTracksTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    topTracksTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    topTracksTable_->setColumnWidth(0, 30);
    topTracksTable_->setColumnWidth(2, 100);
    topTracksTable_->setColumnWidth(3, 100);
    topTracksTable_->setMaximumHeight(280);

    topLayout->addWidget(topTracksTable_);
    statsLayout->addWidget(topGroup);
    statsLayout->addStretch();

    statsScroll->setWidget(statsWidget);
    tabs_->addTab(statsScroll, tr("Estadísticas"));

    layout->addWidget(tabs_, 1);

    // ── Botón cerrar + exportaciones ────────────────────
    auto* bottomRow = new QHBoxLayout();

    auto* exportPdfBtn = new QPushButton(QIcon(":/icons/save.svg"), " Exportar PDF");
    exportPdfBtn->setProperty("class", "secondaryButton");
    exportPdfBtn->setToolTip(tr("Genera un informe PDF con historial, estadísticas y gráficos"));
    connect(exportPdfBtn, &QPushButton::clicked, this, &AuditDashboard::onExportPDF);
    bottomRow->addWidget(exportPdfBtn);

    auto* exportImgBtn = new QPushButton(QIcon(":/icons/save.svg"), " Exportar gráficos");
    exportImgBtn->setProperty("class", "secondaryButton");
    exportImgBtn->setToolTip(tr("Exporta los gráficos de estadísticas como imágenes PNG"));
    connect(exportImgBtn, &QPushButton::clicked, this, &AuditDashboard::onExportImages);
    bottomRow->addWidget(exportImgBtn);

    bottomRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Cerrar"));
    closeBtn->setProperty("class", "secondaryButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    layout->addLayout(bottomRow);
}

// ══════════════════════════════════════════════════════════
// Refresh
// ══════════════════════════════════════════════════════════

void AuditDashboard::onRefresh()
{
    refreshHistory();
    refreshStats();
}

void AuditDashboard::refreshHistory()
{
    QDate from = fromDate_->date();
    QDate to = toDate_->date();

    QString srcFilter;
    QString monitorId;
    if (sourceFilter_->currentIndex() > 0) {
        QString filterData = sourceFilter_->currentData().toString();
        if (filterData.startsWith("monitor:")) {
            monitorId = filterData.mid(8);
            // No pasar filtro de fuente a la query, filtraremos en memoria
        } else {
            srcFilter = filterData;
        }
    }

    auto history = audit_->getHistory(from, to, srcFilter);

    // Si es un filtro de monitor, obtener carpetas y filtrar
    QStringList monitorFolders;
    if (!monitorId.isEmpty()) {
        auto monitors = audit_->getMonitors();
        for (const auto& m : monitors) {
            if (m.id == monitorId) { monitorFolders = m.folders; break; }
        }
    }

    historyTable_->setRowCount(0);
    for (const auto& e : history) {
        // Filtrar por monitor si aplica
        if (!monitorFolders.isEmpty()) {
            if (!audit_->fileMatchesMonitor(e.filePath, monitorFolders)) continue;
        }
        int row = historyTable_->rowCount();
        historyTable_->insertRow(row);

        auto* dateItem = new QTableWidgetItem(
            e.playedAt.toString(tr("dd/MM/yy HH:mm:ss")));
        dateItem->setForeground(palette().color(QPalette::PlaceholderText));
        historyTable_->setItem(row, 0, dateItem);

        QString name = e.displayName.isEmpty() ? QFileInfo(e.filePath).completeBaseName()
                                                : e.displayName;
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setToolTip(e.filePath);
        historyTable_->setItem(row, 1, nameItem);

        // Traducir fuente para mostrar en español
        QString sourceDisplay = e.source;
        if (sourceDisplay == "fallback") {
            sourceDisplay = tr("Alternativa");
        } else if (sourceDisplay.startsWith("schedule:")) {
            sourceDisplay = sourceDisplay.mid(9);
        } else if (sourceDisplay.startsWith("event:")) {
            sourceDisplay = tr("Evento: ") + sourceDisplay.mid(6);
        }
        auto* sourceItem = new QTableWidgetItem(sourceDisplay);
        sourceItem->setForeground(palette().color(QPalette::PlaceholderText));
        historyTable_->setItem(row, 2, sourceItem);

        auto* durItem = new QTableWidgetItem(formatDuration(e.durationMs));
        durItem->setTextAlignment(Qt::AlignCenter);
        historyTable_->setItem(row, 3, durItem);

        auto* compItem = new QTableWidgetItem(e.completed ? "✓" : "—");
        compItem->setTextAlignment(Qt::AlignCenter);
        compItem->setForeground(e.completed ? QColor("#22c55e") : QColor("#ef4444"));
        historyTable_->setItem(row, 4, compItem);
    }

    historyCountLabel_->setText(
        tr("%1 registro(s) — %2 a %3")
            .arg(history.size())
            .arg(from.toString("dd/MM/yyyy"), to.toString("dd/MM/yyyy")));
}

void AuditDashboard::refreshStats()
{
    QDate from = fromDate_->date();
    QDate to = toDate_->date();

    // Resumen
    int totalPlays = audit_->getTotalPlays(from, to);
    int64_t totalDur = audit_->getTotalDurationMs(from, to);
    totalPlaysLabel_->setText(QString("Total: %1 reproducciones").arg(totalPlays));
    totalDurationLabel_->setText(QString("Tiempo al aire: %1").arg(formatDuration(totalDur)));

    // Gráfico de horas
    drawHourChart();

    // Gráfico de fuentes
    drawSourceChart();

    // Top tracks
    auto topTracks = audit_->getTopTracks(from, to, 15);
    topTracksTable_->setRowCount(0);
    for (int i = 0; i < topTracks.size(); ++i) {
        int row = topTracksTable_->rowCount();
        topTracksTable_->insertRow(row);

        auto* numItem = new QTableWidgetItem(QString::number(i + 1));
        numItem->setTextAlignment(Qt::AlignCenter);
        numItem->setForeground(QColor("#667eea"));
        topTracksTable_->setItem(row, 0, numItem);

        auto* nameItem = new QTableWidgetItem(topTracks[i].displayName);
        nameItem->setToolTip(topTracks[i].filePath);
        topTracksTable_->setItem(row, 1, nameItem);

        auto* countItem = new QTableWidgetItem(QString::number(topTracks[i].playCount));
        countItem->setTextAlignment(Qt::AlignCenter);
        topTracksTable_->setItem(row, 2, countItem);

        auto* durItem = new QTableWidgetItem(formatDuration(topTracks[i].totalDurationMs));
        durItem->setTextAlignment(Qt::AlignCenter);
        topTracksTable_->setItem(row, 3, durItem);
    }

    // Monitores
    refreshMonitorStats();
}

void AuditDashboard::drawHourChart()
{
    QDate from = fromDate_->date();
    QDate to = toDate_->date();
    auto hourData = audit_->getHourDistribution(from, to);

    QVector<BarChartWidget::Bar> bars;
    for (const auto& h : hourData) {
        bars.append({
            QString("%1h").arg(h.hour, 2, 10, QChar('0')),
            h.playCount,
            QColor(102, 126, 234)
        });
    }

    static_cast<BarChartWidget*>(hourChartWidget_)->setData(
        bars, tr("Reproducciones por hora del día"));
}

void AuditDashboard::drawSourceChart()
{
    QDate from = fromDate_->date();
    QDate to = toDate_->date();
    auto sourceData = audit_->getSourceDistribution(from, to);

    QVector<QColor> colors = {
        QColor(102, 126, 234), QColor(34, 197, 94), QColor(245, 158, 11),
        QColor(239, 68, 68), QColor(139, 92, 246), QColor(6, 182, 212)
    };

    // Normalizar y agrupar fuentes (fallback = Respaldo, schedule:X = X, event:X = Evento: X)
    QMap<QString, int> grouped;
    for (const auto& s : sourceData) {
        QString label = s.source;
        if (label == "fallback") label = tr("Alternativa");
        else if (label.startsWith("schedule:")) label = label.mid(9);
        else if (label.startsWith("event:")) label = tr("Evento: ") + label.mid(6);
        grouped[label] += s.playCount;
    }

    QVector<BarChartWidget::Bar> bars;
    int i = 0;
    for (auto it = grouped.begin(); it != grouped.end(); ++it, ++i) {
        QString label = it.key();
        if (label.length() > 15) label = label.left(14) + "…";
        bars.append({label, it.value(), colors[i % colors.size()]});
    }

    static_cast<BarChartWidget*>(sourceChartWidget_)->setData(
        bars, tr("Reproducciones por fuente"));
}

void AuditDashboard::onExportCSV()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar historial a CSV"),
        QString("sara_audit_%1.csv").arg(QDate::currentDate().toString("yyyyMMdd")),
        "CSV (*.csv)");
    if (path.isEmpty()) return;

    auto history = getFilteredHistory();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Error"), tr("No se pudo crear el archivo CSV."));
        return;
    }

    QTextStream out(&file);
    out << "\xEF\xBB\xBF";
    out << "Fecha,Hora,Título,Archivo,Fuente,Duración(s),Completado\n";

    for (const auto& e : history) {
        QString durSecs = QString::number(e.durationMs / 1000.0, 'f', 1);
        QString name = e.displayName;
        name.replace('"', "\"\"");
        QString fp = e.filePath;
        fp.replace('"', "\"\"");

        QString src = e.source;
        if (src == "fallback") src = tr("Alternativa");
        else if (src.startsWith("schedule:")) src = src.mid(9);
        else if (src.startsWith("event:")) src = tr("Evento: ") + src.mid(6);

        out << e.playedAt.date().toString("dd/MM/yyyy") << ","
            << e.playedAt.time().toString("HH:mm:ss") << ","
            << "\"" << name << "\","
            << "\"" << fp << "\","
            << "\"" << src << "\","
            << durSecs << ","
            << (e.completed ? "Sí" : "No") << "\n";
    }

    file.close();
    QMessageBox::information(this, tr("Exportación completada"),
        tr("Historial exportado a:\n%1\n(%2 registros)").arg(path).arg(history.size()));
}

QString AuditDashboard::formatDuration(int64_t ms) const
{
    if (ms <= 0) return "—";
    int64_t totalSecs = ms / 1000;
    int hours = static_cast<int>(totalSecs / 3600);
    int mins = static_cast<int>((totalSecs % 3600) / 60);
    int secs = static_cast<int>(totalSecs % 60);

    if (hours > 0) {
        return QString("%1h %2m").arg(hours).arg(mins, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}

void AuditDashboard::onExportImages()
{
    // Asegurar que las estadísticas estén actualizadas
    refreshStats();

    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Carpeta donde guardar los gráficos"), QDir::homePath());
    if (dir.isEmpty()) return;

    QString prefix = QString("sara_audit_%1").arg(QDate::currentDate().toString("yyyyMMdd"));

    // Capturar gráfico de horas
    QPixmap hourPix = hourChartWidget_->grab();
    QString hourPath = dir + "/" + prefix + "_horas.png";
    hourPix.save(hourPath, "PNG");

    // Capturar gráfico de fuentes
    QPixmap sourcePix = sourceChartWidget_->grab();
    QString sourcePath = dir + "/" + prefix + "_fuentes.png";
    sourcePix.save(sourcePath, "PNG");

    // Capturar monitores si existen
    if (monitorStatsWidget_->isVisible()) {
        QPixmap monPix = monitorStatsWidget_->grab();
        QString monPath = dir + "/" + prefix + "_monitores.png";
        monPix.save(monPath, "PNG");
    }

    QMessageBox::information(this, tr("Exportación completada"),
        QString("Gráficos exportados en:\n%1").arg(dir));
}

void AuditDashboard::onExportPDF()
{
    // Asegurar que las estadísticas estén actualizadas
    refreshStats();

    QDate from = fromDate_->date();
    QDate to = toDate_->date();

    QString defaultName = tr("sara_informe_%1_%2.pdf")
        .arg(from.toString("yyyyMMdd"), to.toString("yyyyMMdd"));

    QString path = QFileDialog::getSaveFileName(this,
        "Exportar informe PDF", defaultName, "PDF (*.pdf)");
    if (path.isEmpty()) return;

    // ── Configurar PDF ──────────────────────────────
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
    pdf.setTitle(tr("SARA Libre — Informe de Auditoría"));
    pdf.setCreator(tr("SARA Libre"));

    QPainter p(&pdf);
    int w = pdf.width();
    int h = pdf.height();
    int y = 0;
    int lineH = pdf.resolution() / 5;  // ~5mm por línea

    // Fuentes
    QFont titleFont("Sans", 18, QFont::Bold);
    QFont subtitleFont("Sans", 12, QFont::Bold);
    QFont normalFont("Sans", 9);
    QFont smallFont("Sans", 7);
    QFont headerFont("Sans", 8, QFont::Bold);

    // ── Página 1: Encabezado + Estadísticas ─────────
    // Título
    p.setFont(titleFont);
    p.setPen(QColor("#222222"));
    p.drawText(0, y, w, lineH * 3, Qt::AlignCenter,
               tr("SARA Libre — Informe de Auditoría"));
    y += lineH * 3;

    // Período
    p.setFont(subtitleFont);
    p.drawText(0, y, w, lineH * 2, Qt::AlignCenter,
               tr("Período: %1 a %2")
                   .arg(from.toString("dd/MM/yyyy"), to.toString("dd/MM/yyyy")));
    y += lineH * 3;

    // Resumen
    p.setFont(normalFont);
    p.setPen(QColor("#333333"));
    p.drawText(0, y, w, lineH, 0, totalPlaysLabel_->text());
    y += lineH;
    p.drawText(0, y, w, lineH, 0, totalDurationLabel_->text());
    y += lineH * 2;

    // Gráfico de horas
    p.setFont(subtitleFont);
    p.drawText(0, y, w, lineH, 0, tr("Distribución por hora del día"));
    y += lineH + lineH / 2;

    QPixmap hourPix = hourChartWidget_->grab();
    int imgW = w;
    int imgH = imgW * hourPix.height() / hourPix.width();
    if (imgH > h / 4) imgH = h / 4;
    p.drawPixmap(0, y, imgW, imgH, hourPix);
    y += imgH + lineH;

    // Gráfico de fuentes
    p.setFont(subtitleFont);
    p.drawText(0, y, w, lineH, 0, tr("Distribución por fuente"));
    y += lineH + lineH / 2;

    QPixmap srcPix = sourceChartWidget_->grab();
    imgH = imgW * srcPix.height() / srcPix.width();
    if (imgH > h / 4) imgH = h / 4;
    p.drawPixmap(0, y, imgW, imgH, srcPix);
    y += imgH + lineH;

    // Monitores si existen
    auto monitors = audit_->getMonitors();
    if (!monitors.isEmpty()) {
        p.setFont(subtitleFont);
        p.drawText(0, y, w, lineH, 0, tr("Monitores de Auditoría"));
        y += lineH + lineH / 2;

        // Capturar gráfico solo si el widget está visible
        if (monitorStatsWidget_->isVisible()) {
            QPixmap monPix = monitorStatsWidget_->grab();
            imgH = imgW * monPix.height() / monPix.width();
            if (imgH > h / 5) imgH = h / 5;
            p.drawPixmap(0, y, imgW, imgH, monPix);
            y += imgH + lineH / 2;
        }

        // Porcentajes de cada monitor
        p.setFont(normalFont);
        for (const auto& m : monitors) {
            auto stats = audit_->getMonitorStats(m.id, from, to);
            double pct = stats.totalPlays > 0
                ? (stats.monitorPlays * 100.0 / stats.totalPlays) : 0.0;
            QString line = tr("%1: %2 de %3 pistas (%4%)")
                .arg(m.name)
                .arg(stats.monitorPlays)
                .arg(stats.totalPlays)
                .arg(pct, 0, 'f', 1);
            p.drawText(0, y, w, lineH, 0, line);
            y += lineH;
        }
        y += lineH / 2;
    }

    // ── Página 2+: Historial ─────────────────────────
    pdf.newPage();
    y = 0;

    p.setFont(subtitleFont);
    p.drawText(0, y, w, lineH * 2, 0, tr("Historial de Reproducción"));
    y += lineH * 2;

    // Obtener datos filtrados (mismos que la tabla visible, incluyendo monitores)
    auto history = getFilteredHistory(5000);

    // Encabezado de tabla
    int colDate = w * 15 / 100;
    int colTitle = w * 45 / 100;
    int colSource = w * 20 / 100;
    int colDur = w * 10 / 100;
    int colOk = w * 10 / 100;

    auto drawTableHeader = [&]() {
        p.setFont(headerFont);
        p.setPen(QColor("#333333"));
        p.fillRect(0, y, w, lineH, QColor("#e0e0e0"));
        int x = 0;
        p.drawText(x, y, colDate, lineH, Qt::AlignLeft | Qt::AlignVCenter, " Fecha/Hora");
        x += colDate;
        p.drawText(x, y, colTitle, lineH, Qt::AlignLeft | Qt::AlignVCenter, " Título");
        x += colTitle;
        p.drawText(x, y, colSource, lineH, Qt::AlignLeft | Qt::AlignVCenter, " Fuente");
        x += colSource;
        p.drawText(x, y, colDur, lineH, Qt::AlignCenter, "Duración");
        x += colDur;
        p.drawText(x, y, colOk, lineH, Qt::AlignCenter, "OK");
        y += lineH;
    };

    drawTableHeader();

    p.setFont(smallFont);
    for (const auto& e : history) {
        if (y + lineH > h - lineH) {
            // Nueva página
            pdf.newPage();
            y = 0;
            drawTableHeader();
            p.setFont(smallFont);
        }

        // Fila alternada
        if ((&e - &history[0]) % 2 == 0) {
            p.fillRect(0, y, w, lineH, QColor("#f5f5f5"));
        }

        p.setPen(QColor("#333333"));
        int x = 0;

        p.drawText(x, y, colDate, lineH, Qt::AlignLeft | Qt::AlignVCenter,
                   " " + e.playedAt.toString(tr("dd/MM/yy HH:mm:ss")));
        x += colDate;

        QString name = e.displayName.isEmpty()
            ? QFileInfo(e.filePath).completeBaseName() : e.displayName;
        if (name.length() > 50) name = name.left(48) + "…";
        p.drawText(x, y, colTitle, lineH, Qt::AlignLeft | Qt::AlignVCenter, " " + name);
        x += colTitle;

        // Traducir fuente
        QString src = e.source;
        if (src == "fallback") src = tr("Alternativa");
        else if (src.startsWith("schedule:")) src = src.mid(9);
        else if (src.startsWith("event:")) src = tr("Evento: ") + src.mid(6);
        p.drawText(x, y, colSource, lineH, Qt::AlignLeft | Qt::AlignVCenter, " " + src);
        x += colSource;

        p.drawText(x, y, colDur, lineH, Qt::AlignCenter, formatDuration(e.durationMs));
        x += colDur;

        p.setPen(e.completed ? QColor("#22a555") : QColor("#cc3333"));
        p.drawText(x, y, colOk, lineH, Qt::AlignCenter, e.completed ? "✓" : "—");

        y += lineH;
    }

    // Pie
    y += lineH;
    p.setPen(QColor("#999999"));
    p.setFont(smallFont);
    p.drawText(0, y, w, lineH, Qt::AlignRight,
               tr("Generado por SARA Libre — %1 — %2 registro(s)")
                   .arg(QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm"))
                   .arg(history.size()));

    p.end();

    QMessageBox::information(this, tr("Exportación completada"),
        QString("Informe PDF exportado a:\n%1").arg(path));
}

void AuditDashboard::refreshMonitorStats()
{
    auto monitors = audit_->getMonitors();

    // Limpiar widget anterior
    if (monitorStatsWidget_->layout()) {
        QLayoutItem* item;
        while ((item = monitorStatsWidget_->layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete monitorStatsWidget_->layout();
    }

    if (monitors.isEmpty()) {
        monitorStatsWidget_->setVisible(false);
        return;
    }

    monitorStatsWidget_->setVisible(true);
    auto* layout = new QVBoxLayout(monitorStatsWidget_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* title = new QLabel(tr("Monitores de Auditoría"));
    title->setStyleSheet("font-size: 13px; font-weight: 700;");
    layout->addWidget(title);

    QDate from = fromDate_->date();
    QDate to = toDate_->date();

    for (const auto& monitor : monitors) {
        auto stats = audit_->getMonitorStats(monitor.id, from, to);

        auto* row = new QWidget();
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 8, 4);
        row->setStyleSheet("background: rgba(0,0,0,0.04); border-radius: 6px;");

        auto* nameLabel = new QLabel(monitor.name);
        nameLabel->setStyleSheet("font-weight: 600;");
        nameLabel->setToolTip(tr("Carpetas: ") + monitor.folders.join(", "));

        // Barra de porcentaje visual
        auto* barBg = new QWidget();
        barBg->setFixedHeight(18);
        barBg->setMinimumWidth(200);
        barBg->setStyleSheet("background: rgba(0,0,0,0.08); border-radius: 4px;");

        auto* barFill = new QWidget(barBg);
        int fillWidth = qBound(0, static_cast<int>(stats.percentage * 2), 200);
        barFill->setGeometry(0, 0, fillWidth, 18);
        QColor barColor = stats.percentage >= 50 ? QColor(34, 197, 94) :
                          stats.percentage >= 25 ? QColor(245, 158, 11) :
                                                    QColor(239, 68, 68);
        barFill->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(barColor.name()));

        auto* pctLabel = new QLabel(tr("%1%  (%2 de %3)")
            .arg(stats.percentage, 0, 'f', 1)
            .arg(stats.monitorPlays)
            .arg(stats.totalPlays));
        pctLabel->setStyleSheet("font-size: 12px;");

        rowLayout->addWidget(nameLabel);
        rowLayout->addWidget(barBg);
        rowLayout->addWidget(pctLabel);

        layout->addWidget(row);
    }
}

void AuditDashboard::onManageMonitors()
{
    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    dlg.setWindowTitle(tr("Monitores de Auditoría"));
    dlg.setMinimumSize(720, 440);
    dlg.resize(780, 480);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 12, 16, 12);

    auto* title = new QLabel(tr("Monitores de Auditoría"));
    title->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(title);

    auto* desc = new QLabel(
        tr("Cree monitores para rastrear el porcentaje de reproducción de música específica "
           "(ej: Música Nacional, Música Intercultural). Seleccione las carpetas que contienen "
           "esa música y SARA calculará automáticamente qué porcentaje se reproduce."));
    desc->setStyleSheet("font-size: 11px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // ── Dos paneles ──────────────────────────────────
    auto* columns = new QHBoxLayout();
    columns->setSpacing(8);

    // Panel izquierdo: lista de monitores
    auto* leftGroup = new QGroupBox(tr("Monitores"));
    auto* leftLayout = new QVBoxLayout(leftGroup);
    leftLayout->setSpacing(4);

    auto* monitorList = new QListWidget();
    leftLayout->addWidget(monitorList, 1);

    auto* leftBtnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(QIcon(":/icons/plus.svg"), " Nuevo");
    addBtn->setFixedHeight(26);
    auto* renameBtn = new QPushButton(QIcon(":/icons/settings.svg"), " Renombrar");
    renameBtn->setFixedHeight(26);
    renameBtn->setProperty("class", "secondaryButton");
    auto* deleteBtn = new QPushButton(QIcon(":/icons/x.svg"), "");
    deleteBtn->setFixedSize(26, 26);
    deleteBtn->setToolTip(tr("Eliminar monitor"));
    deleteBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }");

    leftBtnRow->addWidget(addBtn);
    leftBtnRow->addWidget(renameBtn);
    leftBtnRow->addStretch();
    leftBtnRow->addWidget(deleteBtn);
    leftLayout->addLayout(leftBtnRow);

    auto* monitorCountLabel = new QLabel();
    monitorCountLabel->setStyleSheet("font-size: 10px;");
    leftLayout->addWidget(monitorCountLabel);

    columns->addWidget(leftGroup, 4);

    // Panel derecho: carpetas del monitor seleccionado
    auto* rightGroup = new QGroupBox(tr("Carpetas Monitoreadas"));
    auto* rightLayout = new QVBoxLayout(rightGroup);
    rightLayout->setSpacing(4);

    auto* folderList = new QListWidget();
    rightLayout->addWidget(folderList, 1);

    auto* rightBtnRow = new QHBoxLayout();
    auto* addFolderBtn = new QPushButton(QIcon(":/icons/plus.svg"), " Agregar carpeta");
    addFolderBtn->setFixedHeight(26);
    auto* removeFolderBtn = new QPushButton(QIcon(":/icons/x.svg"), "");
    removeFolderBtn->setFixedSize(26, 26);
    removeFolderBtn->setToolTip(tr("Quitar carpeta seleccionada"));
    removeFolderBtn->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }");

    rightBtnRow->addWidget(addFolderBtn);
    rightBtnRow->addStretch();
    rightBtnRow->addWidget(removeFolderBtn);
    rightLayout->addLayout(rightBtnRow);

    auto* folderCountLabel = new QLabel();
    folderCountLabel->setStyleSheet("font-size: 10px;");
    rightLayout->addWidget(folderCountLabel);

    columns->addWidget(rightGroup, 5);
    layout->addLayout(columns, 1);

    // ── Funciones de refresco ────────────────────────
    auto refreshMonitors = [this, monitorList, monitorCountLabel]() {
        QString prevId;
        if (monitorList->currentItem())
            prevId = monitorList->currentItem()->data(Qt::UserRole).toString();

        monitorList->clear();
        auto monitors = audit_->getMonitors();
        int selectRow = -1;
        for (int i = 0; i < monitors.size(); ++i) {
            const auto& m = monitors[i];
            auto* item = new QListWidgetItem(QIcon(":/icons/folder.svg"), m.name);
            item->setData(Qt::UserRole, m.id);
            monitorList->addItem(item);
            if (m.id == prevId) selectRow = i;
        }
        monitorCountLabel->setText(QString("%1 monitor(es)").arg(monitors.size()));
        if (selectRow >= 0) monitorList->setCurrentRow(selectRow);
        else if (monitors.size() > 0) monitorList->setCurrentRow(0);
    };

    auto refreshFolders = [this, monitorList, folderList, folderCountLabel]() {
        folderList->clear();
        folderCountLabel->clear();

        auto* item = monitorList->currentItem();
        if (!item) return;

        QString monitorId = item->data(Qt::UserRole).toString();
        auto monitors = audit_->getMonitors();
        for (const auto& m : monitors) {
            if (m.id == monitorId) {
                for (const auto& folder : m.folders) {
                    auto* fi = new QListWidgetItem(QIcon(":/icons/folder.svg"), folder);
                    fi->setData(Qt::UserRole, folder);
                    fi->setToolTip(folder);
                    folderList->addItem(fi);
                }
                folderCountLabel->setText(QString("%1 carpeta(s)").arg(m.folders.size()));
                break;
            }
        }
    };

    refreshMonitors();
    connect(monitorList, &QListWidget::currentRowChanged, &dlg, [refreshFolders]() {
        refreshFolders();
    });
    if (monitorList->count() > 0) refreshFolders();

    // ── Acciones ─────────────────────────────────────
    connect(addBtn, &QPushButton::clicked, &dlg, [this, refreshMonitors, &dlg]() {
        bool ok = false;
        QString name = QInputDialog::getText(&dlg, tr("Nuevo Monitor"),
            tr("Nombre (ej: Música Nacional):"), QLineEdit::Normal, "", &ok);
        if (!ok || name.trimmed().isEmpty()) return;

        audit_->addMonitor(name.trimmed(), {});
        refreshMonitors();
        // Seleccionar el último (recién creado)
    });

    connect(renameBtn, &QPushButton::clicked, &dlg, [this, monitorList, refreshMonitors, &dlg]() {
        auto* item = monitorList->currentItem();
        if (!item) return;

        bool ok = false;
        QString name = QInputDialog::getText(&dlg, tr("Renombrar Monitor"),
            tr("Nuevo nombre:"), QLineEdit::Normal, item->text(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;

        QString id = item->data(Qt::UserRole).toString();
        auto monitors = audit_->getMonitors();
        for (const auto& m : monitors) {
            if (m.id == id) {
                audit_->updateMonitor(id, name.trimmed(), m.folders);
                break;
            }
        }
        refreshMonitors();
    });

    connect(deleteBtn, &QPushButton::clicked, &dlg, [this, monitorList, refreshMonitors, refreshFolders, &dlg]() {
        auto* item = monitorList->currentItem();
        if (!item) return;
        auto confirm = QMessageBox::question(&dlg, tr("Eliminar monitor"),
            QString("¿Eliminar el monitor \"%1\" y todas sus carpetas?").arg(item->text()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm == QMessageBox::Yes) {
            audit_->removeMonitor(item->data(Qt::UserRole).toString());
            refreshMonitors();
            refreshFolders();
        }
    });

    connect(addFolderBtn, &QPushButton::clicked, &dlg, [this, monitorList, refreshFolders, &dlg]() {
        auto* item = monitorList->currentItem();
        if (!item) {
            QMessageBox::information(&dlg, tr("Sin monitor"),
                tr("Seleccione o cree un monitor primero."));
            return;
        }

        QString dir = QFileDialog::getExistingDirectory(&dlg,
            tr("Seleccionar carpeta a monitorear"), QDir::homePath());
        if (dir.isEmpty()) return;

        QString monitorId = item->data(Qt::UserRole).toString();
        auto monitors = audit_->getMonitors();
        for (const auto& m : monitors) {
            if (m.id == monitorId) {
                // Verificar duplicado
                if (m.folders.contains(dir)) {
                    QMessageBox::information(&dlg, tr("Carpeta duplicada"),
                        tr("La carpeta \"%1\" ya está siendo monitoreada "
                           "en este monitor.").arg(dir));
                    return;
                }
                QStringList updated = m.folders;
                updated << dir;
                audit_->updateMonitor(monitorId, m.name, updated);
                break;
            }
        }
        refreshFolders();
    });

    connect(removeFolderBtn, &QPushButton::clicked, &dlg, [this, monitorList, folderList, refreshFolders]() {
        auto* monItem = monitorList->currentItem();
        auto* folItem = folderList->currentItem();
        if (!monItem || !folItem) return;

        QString monitorId = monItem->data(Qt::UserRole).toString();
        QString folderToRemove = folItem->data(Qt::UserRole).toString();

        auto monitors = audit_->getMonitors();
        for (const auto& m : monitors) {
            if (m.id == monitorId) {
                QStringList updated = m.folders;
                updated.removeAll(folderToRemove);
                audit_->updateMonitor(monitorId, m.name, updated);
                break;
            }
        }
        refreshFolders();
    });

    // Tecla Suprimir en carpetas
    auto* delFolderShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), folderList);
    delFolderShortcut->setContext(Qt::WidgetShortcut);
    connect(delFolderShortcut, &QShortcut::activated, removeFolderBtn, &QPushButton::click);

    // ── Pie ──────────────────────────────────────────
    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Cerrar"));
    closeBtn->setProperty("class", "secondaryButton");
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    closeRow->addWidget(closeBtn);
    layout->addLayout(closeRow);

    dlg.exec();

    // Refrescar filtro de fuentes para incluir nuevos monitores
    int prevIdx = sourceFilter_->currentIndex();
    while (sourceFilter_->count() > 4) {
        sourceFilter_->removeItem(sourceFilter_->count() - 1);
    }
    auto updatedMonitors = audit_->getMonitors();
    if (!updatedMonitors.isEmpty()) {
        sourceFilter_->insertSeparator(sourceFilter_->count());
        for (const auto& m : updatedMonitors) {
            sourceFilter_->addItem("📊 " + m.name, "monitor:" + m.id);
        }
    }
    if (prevIdx < sourceFilter_->count()) {
        sourceFilter_->setCurrentIndex(prevIdx);
    }

    refreshStats();
}

QVector<AuditManager::HistoryEntry> AuditDashboard::getFilteredHistory(int limit)
{
    QDate from = fromDate_->date();
    QDate to = toDate_->date();

    QString srcFilter;
    QString monitorId;
    if (sourceFilter_->currentIndex() > 0) {
        QString filterData = sourceFilter_->currentData().toString();
        if (filterData.startsWith("monitor:")) {
            monitorId = filterData.mid(8);
        } else {
            srcFilter = filterData;
        }
    }

    auto history = audit_->getHistory(from, to, srcFilter, {}, limit);

    // Si es filtro de monitor, filtrar en memoria
    if (!monitorId.isEmpty()) {
        QStringList monitorFolders;
        auto monitors = audit_->getMonitors();
        for (const auto& m : monitors) {
            if (m.id == monitorId) { monitorFolders = m.folders; break; }
        }
        if (!monitorFolders.isEmpty()) {
            QVector<AuditManager::HistoryEntry> filtered;
            for (const auto& e : history) {
                if (audit_->fileMatchesMonitor(e.filePath, monitorFolders)) {
                    filtered.append(e);
                }
            }
            return filtered;
        }
    }

    return history;
}

} // namespace sara
