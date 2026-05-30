#ifndef SARA_UI_INVERSE_ASSIGN_H
#define SARA_UI_INVERSE_ASSIGN_H

#include "core/types.h"
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QDateEdit>
#include <QCheckBox>

namespace sara {

class EventRepository;

/**
 * Diálogo de asignación inversa.
 *
 * Flujo: selecciono un audio → veo en qué eventos está →
 * marco/desmarco eventos donde quiero que aparezca.
 *
 * Caso de uso: llega un cliente nuevo, grabo su comercial,
 * y con un solo diálogo lo agrego a los 10 bloques publicitarios
 * de la semana.
 */
class InverseAssignDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InverseAssignDialog(EventRepository* repo, QWidget* parent = nullptr);

    /// Abrir con un archivo pre-seleccionado
    void setFilePath(const QString& path);

private slots:
    void browseFile();
    void onFileChanged();
    void onApply();

private:
    void setupUI();
    void refreshEventList();

    EventRepository* repo_;

    QLineEdit*   filePathEdit_;
    QPushButton* browseBtn_;
    QLabel*      fileInfoLabel_;

    // Vigencia del audio (opcional)
    QCheckBox*   datesCheck_;
    QDateEdit*   fromDate_;
    QDateEdit*   untilDate_;

    // Lista de todos los eventos con checkboxes
    QListWidget* eventList_;

    QLabel*      statusLabel_;
};

} // namespace sara

#endif
