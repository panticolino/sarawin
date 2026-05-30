#include "ui/inverse_assign.h"
#include "data/event_repository.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QIcon>
#include <QGroupBox>

namespace sara {

InverseAssignDialog::InverseAssignDialog(EventRepository* repo, QWidget* parent)
    : QDialog(parent), repo_(repo)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Asignación Inversa — Agregar audio a eventos"));
    setMinimumSize(620, 500);
    setupUI();
}

void InverseAssignDialog::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    // Título
    auto* titleLabel = new QLabel(tr("Asignación Inversa"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 700;");
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(
        tr("Seleccione un archivo de audio y elija en qué eventos desea incluirlo. Puede asignar vigencia (fechas) al audio y agregarlo a múltiples eventos con un solo clic."));
    descLabel->setStyleSheet("font-size: 12px;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // ── Archivo ──────────────────────────────────────
    auto* fileGroup = new QGroupBox(tr("Archivo de Audio"));
    auto* fileLayout = new QVBoxLayout(fileGroup);

    auto* fileRow = new QHBoxLayout();
    filePathEdit_ = new QLineEdit();
    filePathEdit_->setPlaceholderText(tr("Seleccione un archivo de audio..."));
    filePathEdit_->setReadOnly(true);

    browseBtn_ = new QPushButton(QIcon(":/icons/folder.svg"), " Seleccionar");
    connect(browseBtn_, &QPushButton::clicked, this, &InverseAssignDialog::browseFile);

    fileRow->addWidget(filePathEdit_, 1);
    fileRow->addWidget(browseBtn_);
    fileLayout->addLayout(fileRow);

    fileInfoLabel_ = new QLabel();
    fileInfoLabel_->setStyleSheet("font-size: 11px;");
    fileLayout->addWidget(fileInfoLabel_);

    // Vigencia
    auto* dateRow = new QHBoxLayout();
    datesCheck_ = new QCheckBox(tr("Vigencia:"));
    datesCheck_->setToolTip(tr("Fechas durante las cuales este audio se reproducirá"));

    fromDate_ = new QDateEdit(QDate::currentDate());
    fromDate_->setCalendarPopup(true);
    fromDate_->setDisplayFormat("dd/MM/yyyy");
    fromDate_->setEnabled(false);
    fromDate_->setFixedWidth(110);

    untilDate_ = new QDateEdit(QDate::currentDate().addMonths(1));
    untilDate_->setCalendarPopup(true);
    untilDate_->setDisplayFormat("dd/MM/yyyy");
    untilDate_->setEnabled(false);
    untilDate_->setFixedWidth(110);

    connect(datesCheck_, &QCheckBox::toggled, fromDate_, &QDateEdit::setEnabled);
    connect(datesCheck_, &QCheckBox::toggled, untilDate_, &QDateEdit::setEnabled);

    dateRow->addWidget(datesCheck_);
    dateRow->addWidget(fromDate_);
    dateRow->addWidget(new QLabel("→"));
    dateRow->addWidget(untilDate_);
    dateRow->addStretch();
    fileLayout->addLayout(dateRow);

    layout->addWidget(fileGroup);

    // ── Lista de eventos ─────────────────────────────
    auto* eventsGroup = new QGroupBox(tr("Eventos disponibles"));
    auto* eventsLayout = new QVBoxLayout(eventsGroup);

    auto* helpLabel = new QLabel(
        tr("Marque los eventos donde desea incluir este audio. Los ya marcados indican que el audio ya está en ese evento."));
    helpLabel->setStyleSheet("font-size: 11px;");
    helpLabel->setWordWrap(true);
    eventsLayout->addWidget(helpLabel);

    eventList_ = new QListWidget();
    eventList_->setMinimumHeight(180);
    eventsLayout->addWidget(eventList_, 1);

    layout->addWidget(eventsGroup, 1);

    // Status
    statusLabel_ = new QLabel();
    statusLabel_->setStyleSheet("font-size: 11px;");
    layout->addWidget(statusLabel_);

    // Botones
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(QIcon(":/icons/x.svg"), " Cancelar");
    cancelBtn->setProperty("class", "secondaryButton");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* applyBtn = new QPushButton(QIcon(":/icons/check.svg"), " Aplicar");
    connect(applyBtn, &QPushButton::clicked, this, &InverseAssignDialog::onApply);

    btnRow->addWidget(applyBtn);
    btnRow->addWidget(cancelBtn);
    layout->addLayout(btnRow);

    // Cargar lista de eventos
    refreshEventList();
}

void InverseAssignDialog::setFilePath(const QString& path)
{
    filePathEdit_->setText(path);
    onFileChanged();
}

void InverseAssignDialog::browseFile()
{
    QString file = QFileDialog::getOpenFileName(
        this, tr("Seleccionar archivo de audio"), QString(),
        "Audio (*.mp3 *.ogg *.flac *.wav *.aac *.m4a *.opus);;Todos (*)");
    if (file.isEmpty()) return;

    filePathEdit_->setText(file);
    onFileChanged();
}

void InverseAssignDialog::onFileChanged()
{
    QString path = filePathEdit_->text();
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    fileInfoLabel_->setText(
        QString("%1  (%2)")
            .arg(fi.completeBaseName())
            .arg(QLocale().formattedDataSize(fi.size()))
    );

    // Marcar los eventos que ya contienen este archivo
    auto eventsWithFile = repo_->findEventsContaining(path);
    QSet<QString> containingIds;
    for (const auto& e : eventsWithFile) {
        containingIds.insert(e.id);
    }

    for (int i = 0; i < eventList_->count(); ++i) {
        auto* item = eventList_->item(i);
        QString eventId = item->data(Qt::UserRole).toString();
        bool contains = containingIds.contains(eventId);
        item->setCheckState(contains ? Qt::Checked : Qt::Unchecked);
    }

    int count = eventsWithFile.size();
    statusLabel_->setText(
        count > 0
            ? QString("Este audio ya está en %1 evento(s)").arg(count)
            : tr("Este audio no está en ningún evento")
    );
}

void InverseAssignDialog::refreshEventList()
{
    eventList_->clear();

    auto allEvents = repo_->getAll();
    for (const auto& event : allEvents) {
        auto eventSlotList = repo_->getSlots(event.id);
        QString slotsInfo;
        if (!eventSlotList.isEmpty()) {
            QStringList slotTexts;
            for (const auto& s : eventSlotList) {
                slotTexts << QString("%1 %2").arg(s.day.left(3), s.triggerTime.toString("HH:mm"));
            }
            // Mostrar hasta 3 horarios + "..."
            if (slotTexts.size() > 3) {
                slotsInfo = slotTexts.mid(0, 3).join(", ") +
                    QString(" (+%1 más)").arg(slotTexts.size() - 3);
            } else {
                slotsInfo = slotTexts.join(", ");
            }
        } else {
            slotsInfo = tr("sin horarios");
        }

        QString label = tr("%1  [pri %2] — %3")
            .arg(event.name)
            .arg(event.priority)
            .arg(slotsInfo);

        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, event.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        eventList_->addItem(item);
    }
}

void InverseAssignDialog::onApply()
{
    QString path = filePathEdit_->text();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Sin archivo"),
            tr("Seleccione un archivo de audio primero."));
        return;
    }

    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("Archivo no encontrado"),
            tr("El archivo seleccionado no existe."));
        return;
    }

    // Determinar qué eventos ya tenían el archivo y cuáles ahora están marcados
    auto eventsWithFile = repo_->findEventsContaining(path);
    QSet<QString> hadFile;
    for (const auto& e : eventsWithFile) hadFile.insert(e.id);

    QSet<QString> nowChecked;
    for (int i = 0; i < eventList_->count(); ++i) {
        auto* item = eventList_->item(i);
        if (item->checkState() == Qt::Checked) {
            nowChecked.insert(item->data(Qt::UserRole).toString());
        }
    }

    // Agregar a los nuevos
    QStringList toAdd;
    for (const auto& id : nowChecked) {
        if (!hadFile.contains(id)) toAdd << id;
    }

    // Quitar de los desmarcados
    QStringList toRemove;
    for (const auto& id : hadFile) {
        if (!nowChecked.contains(id)) toRemove << id;
    }

    // Preparar el elemento
    EventElement elem;
    elem.type = ElementType::File;
    elem.path = path;
    elem.displayName = QFileInfo(path).completeBaseName();

    if (datesCheck_->isChecked()) {
        elem.validFrom = fromDate_->date();
        elem.validUntil = untilDate_->date();
    }

    int added = 0, removed = 0;

    if (!toAdd.isEmpty()) {
        repo_->addElementToEvents(toAdd, elem);
        added = toAdd.size();
    }

    if (!toRemove.isEmpty()) {
        repo_->removeElementFromEvents(toRemove, path);
        removed = toRemove.size();
    }

    if (added > 0 || removed > 0) {
        LOG_INFO("[InverseAssign] '{}': agregado a {} evento(s), quitado de {}",
                 elem.displayName.toStdString(), added, removed);

        QMessageBox::information(this, tr("Asignación completada"),
            tr("Resultados:\n• Agregado a %1 evento(s)\n• Quitado de %2 evento(s)")
                .arg(added).arg(removed));
    } else {
        QMessageBox::information(this, tr("Sin cambios"),
            tr("No se realizaron cambios."));
    }

    accept();
}

} // namespace sara
