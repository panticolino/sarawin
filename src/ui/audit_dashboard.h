#ifndef SARA_UI_AUDIT_DASHBOARD_H
#define SARA_UI_AUDIT_DASHBOARD_H

#include <QDialog>
#include <QDateEdit>
#include <QComboBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include "data/audit_manager.h"

namespace sara {

/**
 * Dashboard de auditoría.
 *
 * Pestañas:
 * 1. Historial: tabla completa con filtros, exportable a CSV
 * 2. Estadísticas: top tracks, distribución por hora, por fuente
 */
class AuditDashboard : public QDialog
{
    Q_OBJECT

public:
    explicit AuditDashboard(AuditManager* audit, QWidget* parent = nullptr);

private slots:
    void onRefresh();
    void onExportCSV();
    void onExportPDF();
    void onExportImages();
    void onManageMonitors();

private:
    void setupUI();
    void refreshHistory();
    void refreshStats();
    void refreshMonitorStats();
    void drawHourChart();
    void drawSourceChart();
    QString formatDuration(int64_t ms) const;
    QVector<AuditManager::HistoryEntry> getFilteredHistory(int limit = 100000);

    AuditManager* audit_;

    // Filtros
    QDateEdit*  fromDate_;
    QDateEdit*  toDate_;
    QComboBox*  sourceFilter_;

    QTabWidget* tabs_;

    // Pestaña historial
    QTableWidget* historyTable_;
    QLabel*       historyCountLabel_;

    // Pestaña estadísticas
    QLabel*       totalPlaysLabel_;
    QLabel*       totalDurationLabel_;
    QTableWidget* topTracksTable_;
    QWidget*      hourChartWidget_;
    QWidget*      sourceChartWidget_;
    QWidget*      monitorStatsWidget_;  // Porcentajes de monitores
    QPushButton*  monitorBtn_ = nullptr;

public:
    void setOperationMode(bool on) { if (monitorBtn_) monitorBtn_->setVisible(!on); }
};

} // namespace sara

#endif
