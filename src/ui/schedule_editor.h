#ifndef SARA_UI_SCHEDULE_EDITOR_H
#define SARA_UI_SCHEDULE_EDITOR_H

#include "core/types.h"
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>

namespace sara {

class Database;
class ScheduleRepository;

/**
 * Editor de programaciones musicales.
 *
 * Permite crear/editar una programación definiendo:
 * - Nombre (ej: "Mañanas de salsa")
 * - Lista ordenada de elementos:
 *   • Carpeta (selección aleatoria)
 *   • Archivo específico
 *   • Locución de hora
 *   • URL de streaming
 *
 * El orden de los elementos define el ciclo de reproducción.
 */
class ScheduleEditor : public QDialog
{
    Q_OBJECT

public:
    /// Crear nueva programación
    explicit ScheduleEditor(ScheduleRepository* repo, QWidget* parent = nullptr);

    /// Editar programación existente
    ScheduleEditor(ScheduleRepository* repo, const QString& scheduleId,
                   QWidget* parent = nullptr);

    QString scheduleId() const { return scheduleId_; }

private slots:
    void addFolder();
    void addFile();
    void addTimeAnnounce();
    void removeElement();
    void moveUp();
    void moveDown();
    void onSave();

private:
    void setupUI();
    void loadSchedule();
    void refreshList();
    void syncElementsFromList();
    QString elementIcon(ElementType type) const;
    QString elementTypeLabel(ElementType type) const;

    ScheduleRepository* repo_;
    QString scheduleId_;
    bool isNew_;

    QLineEdit*   nameEdit_;
    QListWidget* elementList_;
    QPushButton* addFolderBtn_;
    QPushButton* addFileBtn_;
    QPushButton* addTimeBtn_;
    QPushButton* removeBtn_;
    QPushButton* moveUpBtn_;
    QPushButton* moveDownBtn_;
    QPushButton* saveBtn_;
    QPushButton* cancelBtn_;

    QVector<ScheduleElement> elements_;
};

} // namespace sara

#endif
