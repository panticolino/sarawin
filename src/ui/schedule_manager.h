#ifndef SARA_UI_SCHEDULE_MANAGER_H
#define SARA_UI_SCHEDULE_MANAGER_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

namespace sara {

class Database;
class ScheduleRepository;

/**
 * Ventana de gestión de programaciones.
 * Lista todas las programaciones y permite crear, editar, duplicar y eliminar.
 * Accesible desde un botón en la columna 1 o desde el menú.
 */
class ScheduleManager : public QDialog
{
    Q_OBJECT

public:
    explicit ScheduleManager(ScheduleRepository* repo, QWidget* parent = nullptr);

private slots:
    void onNew();
    void onEdit();
    void onDuplicate();
    void onDelete();
    void onWeeklyGrid();
    void refreshList();

private:
    void setupUI();

    ScheduleRepository* repo_;
    QListWidget*  scheduleList_;
    QPushButton*  newBtn_;
    QPushButton*  editBtn_;
    QPushButton*  duplicateBtn_;
    QPushButton*  deleteBtn_;
    QLabel*       countLabel_;
};

} // namespace sara

#endif
