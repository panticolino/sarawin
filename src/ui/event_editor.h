#ifndef SARA_UI_EVENT_EDITOR_H
#define SARA_UI_EVENT_EDITOR_H

#include "core/types.h"
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QDateEdit>
#include <QTimeEdit>
#include <QGroupBox>

namespace sara {

class EventRepository;
class StreamPresetRepo;

/**
 * Editor de eventos (Publicidad/Eventos).
 *
 * Permite crear/editar un evento con:
 * - Nombre, prioridad (1-10), persistente, inmediato/retardado
 * - Vigencia general del bloque (fecha desde/hasta)
 * - Lista de elementos con vigencia individual por archivo
 * - Programación horaria (día + hora) con múltiples slots
 * - Tipos: archivo, carpeta aleatoria, locución de hora, streaming
 */
class EventEditor : public QDialog
{
    Q_OBJECT

public:
    explicit EventEditor(EventRepository* repo, QWidget* parent = nullptr);
    EventEditor(EventRepository* repo, const QString& eventId, QWidget* parent = nullptr);

    QString eventId() const { return eventId_; }
    void setStreamPresetRepo(StreamPresetRepo* r) { streamRepo_ = r; }

signals:
    void cuePreviewRequested(const QString& filePath);
    void cueStopRequested();

private slots:
    void addFile();
    void addFolder();
    void addTimeAnnounce();
    void addStream();
    void editStreamElement(int index);
    void removeElement();
    void removeFromAllEvents();
    void editElementDates();
    void addSlot();
    void openHourSelector();
    void editSlot(int row);
    void removeSlotAction();
    void onSave();

private:
    void setupUI();
    void loadEvent();
    void refreshElementList();
    void refreshSlotList();
    void syncElementsFromList();

    EventRepository* repo_;
    StreamPresetRepo* streamRepo_ = nullptr;
    QString eventId_;
    bool isNew_;

    // Info
    QLineEdit*   nameEdit_;
    QSpinBox*    prioritySpin_;
    QCheckBox*   persistentCheck_;
    QCheckBox*   immediateCheck_;
    QCheckBox*   adAnnounceCheck_;
    QCheckBox*   maxWaitCheck_ = nullptr;
    QSpinBox*    maxWaitSpin_ = nullptr;

    // Vigencia del bloque
    QCheckBox*   blockDatesCheck_;
    QDateEdit*   blockFromDate_;
    QDateEdit*   blockUntilDate_;

    // Elementos
    QListWidget* elementList_;

    // Slots horarios
    QListWidget* slotList_;
    QComboBox*   slotDayCombo_;

    QPushButton* saveBtn_;
    QPushButton* cancelBtn_;
    QPushButton* cueBtn_;
    bool         cuePlaying_ = false;

    QVector<EventElement> elements_;
    QVector<EventSlot>    eventSlots_;
};

} // namespace sara

#endif
