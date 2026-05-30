#ifndef SARA_UI_EVENT_SUMMARY_H
#define SARA_UI_EVENT_SUMMARY_H

#include "core/types.h"
#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>

namespace sara {

class EventRepository;

class EventSummaryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EventSummaryDialog(EventRepository* repo, QWidget* parent = nullptr);

signals:
    void editEventRequested(const QString& eventId);
    void deleteEventRequested(const QString& eventId);
    void duplicateEventRequested(const QString& eventId, const QString& eventName);

private slots:
    void onFilterChanged();

private:
    void setupUI();
    void loadData();
    void applyFilters();

    EventRepository* repo_;

    QLineEdit*    searchEdit_;
    QComboBox*    dayFilter_;
    QComboBox*    modeCombo_;
    QComboBox*    vigencyFilter_;
    QTableWidget* table_;
    QLabel*       statusLabel_;

    enum VigencyStatus {
        VigencyAll = 0,
        VigencyExpired,        // Caducado
        VigencyExpiringSoon,   // Caduca en 7 dias
        VigencyNoDate,         // Sin vigencia definida
        VigencyActive          // Vigente
    };

    struct SummaryRow {
        QString eventId;
        QString eventName;
        QString day;
        QTime   triggerTime;
        int     priority;
        int     elementCount;
        QStringList contentNames;
        // Vigencia del evento
        QDate   eventValidFrom;
        QDate   eventValidUntil;
        // Vigencia de cada elemento
        struct ElementVigency {
            int     id;
            QString name;
            QDate   validFrom;
            QDate   validUntil;
        };
        QVector<ElementVigency> elementVigencies;
    };
    QVector<SummaryRow> allRows_;

    void bulkEditVigency(const QVector<int>& elementIds, const QStringList& names);
};

} // namespace sara

#endif
