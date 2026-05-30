#ifndef SARA_UI_WEEKLY_GRID_H
#define SARA_UI_WEEKLY_GRID_H

#include "core/types.h"
#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QMap>

namespace sara {

class ScheduleRepository;

/**
 * Grilla semanal de programación.
 *
 * Visualiza la semana completa con franjas de 30 minutos:
 * - 7 columnas (Lunes a Domingo)
 * - 48 filas (00:00 a 23:30)
 * - Cada celda puede tener asignada una programación
 * - Clic para asignar, clic derecho para quitar
 * - Colores por programación para identificación visual rápida
 *
 * Las franjas vacías significan que SARA usará la carpeta de respaldo.
 */
class WeeklyGrid : public QDialog
{
    Q_OBJECT

public:
    explicit WeeklyGrid(ScheduleRepository* repo, QWidget* parent = nullptr);

private slots:
    void onCellClicked(int row, int col);
    void onCellRightClicked(const QPoint& pos);
    void onDayFilterChanged(int index);
    void onBulkAssign();
    void onClearDay();

private:
    void setupUI();
    void loadGrid();
    void updateCell(int row, int col, const QString& scheduleId, const QString& scheduleName);
    void clearCell(int row, int col);

    // Helpers
    QString dayName(int col) const;
    int dayColumn(const QString& day) const;
    QTime rowToTime(int row) const;
    int timeToRow(const QTime& time) const;
    QColor scheduleColor(const QString& scheduleId) const;
    QString timeLabel(int row) const;

    ScheduleRepository* repo_;
    QTableWidget* grid_;
    QComboBox* dayFilter_;
    QPushButton* bulkAssignBtn_;
    QPushButton* clearDayBtn_;
    QLabel* infoLabel_;

    // Colores asignados a cada programación (mutable para lazy init en const methods)
    mutable QMap<QString, QColor> colorMap_;
    QVector<QColor> palette_;
    mutable int nextColor_ = 0;

    static constexpr int ROWS = 48;  // 24h × 2 (cada 30 min)
    static constexpr int COLS = 7;   // Lunes a Domingo
};

} // namespace sara

#endif
