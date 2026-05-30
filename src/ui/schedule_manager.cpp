#include "ui/schedule_manager.h"
#include "ui/schedule_editor.h"
#include "ui/weekly_grid.h"
#include "data/schedule_repository.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QIcon>

namespace sara {

ScheduleManager::ScheduleManager(ScheduleRepository* repo, QWidget* parent)
    : QDialog(parent)
    , repo_(repo)
{
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowTitle(tr("Gestión de Programaciones"));
    setMinimumSize(480, 400);
    setupUI();
    refreshList();
}

void ScheduleManager::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* titleLabel = new QLabel(tr("Programaciones Musicales"));
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 700;");

    auto* descLabel = new QLabel(
        tr("Cree y edite programaciones con diferentes combinaciones de carpetas, archivos y locuciones. Luego asígnelas a horarios en la grilla semanal.")
    );
    descLabel->setStyleSheet("font-size: 12px;");
    descLabel->setWordWrap(true);

    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);

    // Lista + botones
    auto* contentRow = new QHBoxLayout();

    scheduleList_ = new QListWidget();
    scheduleList_->setMinimumHeight(200);
    connect(scheduleList_, &QListWidget::itemDoubleClicked,
            this, &ScheduleManager::onEdit);
    contentRow->addWidget(scheduleList_, 1);

    auto* sideButtons = new QVBoxLayout();
    sideButtons->setSpacing(6);

    newBtn_ = new QPushButton(QIcon(":/icons/plus.svg"), " Nueva");
    newBtn_->setToolTip(tr("Crear nueva programación"));

    editBtn_ = new QPushButton(QIcon(":/icons/settings.svg"), " Editar");
    editBtn_->setToolTip(tr("Editar programación seleccionada"));

    duplicateBtn_ = new QPushButton(QIcon(":/icons/list.svg"), " Duplicar");
    duplicateBtn_->setToolTip(tr("Duplicar programación seleccionada"));
    duplicateBtn_->setProperty("class", "secondaryButton");

    deleteBtn_ = new QPushButton(QIcon(":/icons/x.svg"), " Eliminar");
    deleteBtn_->setToolTip(tr("Eliminar programación seleccionada"));
    deleteBtn_->setStyleSheet(
        "QPushButton { background: rgba(239,68,68,0.15); color: #ef4444; "
        "border: 1px solid rgba(239,68,68,0.3); border-radius: 6px; "
        "padding: 7px 14px; font-weight: 600; }"
        "QPushButton:hover { background: rgba(239,68,68,0.25); }"
    );

    sideButtons->addWidget(newBtn_);
    sideButtons->addWidget(editBtn_);
    sideButtons->addWidget(duplicateBtn_);
    sideButtons->addStretch();
    sideButtons->addWidget(deleteBtn_);
    contentRow->addLayout(sideButtons);

    layout->addLayout(contentRow, 1);

    // Contador
    countLabel_ = new QLabel();
    countLabel_->setStyleSheet("font-size: 11px;");
    layout->addWidget(countLabel_);

    // Botones inferiores
    auto* closeRow = new QHBoxLayout();

    auto* gridBtn = new QPushButton(QIcon(":/icons/list.svg"), " Grilla Semanal");
    gridBtn->setToolTip(tr("Abrir la grilla semanal para asignar programaciones a horarios"));
    connect(gridBtn, &QPushButton::clicked, this, &ScheduleManager::onWeeklyGrid);

    closeRow->addWidget(gridBtn);
    closeRow->addStretch();
    auto* closeBtn = new QPushButton(tr("Cerrar"));
    closeBtn->setProperty("class", "secondaryButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(closeBtn);
    layout->addLayout(closeRow);

    // Conexiones
    connect(newBtn_,       &QPushButton::clicked, this, &ScheduleManager::onNew);
    connect(editBtn_,      &QPushButton::clicked, this, &ScheduleManager::onEdit);
    connect(duplicateBtn_, &QPushButton::clicked, this, &ScheduleManager::onDuplicate);
    connect(deleteBtn_,    &QPushButton::clicked, this, &ScheduleManager::onDelete);
}

void ScheduleManager::onNew()
{
    ScheduleEditor editor(repo_, this);
    if (editor.exec() == QDialog::Accepted) {
        refreshList();
    }
}

void ScheduleManager::onEdit()
{
    auto* item = scheduleList_->currentItem();
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();
    ScheduleEditor editor(repo_, id, this);
    if (editor.exec() == QDialog::Accepted) {
        refreshList();
    }
}

void ScheduleManager::onDuplicate()
{
    auto* item = scheduleList_->currentItem();
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();
    QString originalName = item->text();

    bool ok = false;
    QString newName = QInputDialog::getText(
        this, tr("Duplicar programación"),
        tr("Nombre para la copia:"),
        QLineEdit::Normal,
        originalName + " (copia)", &ok
    );

    if (!ok || newName.isEmpty()) return;

    QString newId = repo_->duplicate(id, newName);
    if (!newId.isEmpty()) {
        refreshList();
    }
}

void ScheduleManager::onDelete()
{
    auto* item = scheduleList_->currentItem();
    if (!item) return;

    QString name = item->text();
    QString id = item->data(Qt::UserRole).toString();

    auto result = QMessageBox::question(
        this, tr("Eliminar programación"),
        QString(tr("¿Eliminar la programación \")%1\"?\n\nSe eliminarán también sus asignaciones horarias.\nEsta acción no se puede deshacer.")).arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        repo_->remove(id);
        refreshList();
    }
}

void ScheduleManager::onWeeklyGrid()
{
    WeeklyGrid grid(repo_, this);
    grid.exec();
    refreshList();  // Los horarios pudieron cambiar
}

void ScheduleManager::refreshList()
{
    scheduleList_->clear();

    auto schedules = repo_->getAll();
    for (const auto& s : schedules) {
        auto elements = repo_->getElements(s.id);
        auto schedSlots = repo_->getSlots(s.id);

        QString label = tr("%1  (%2 elementos, %3 horarios)")
            .arg(s.name)
            .arg(elements.size())
            .arg(schedSlots.size());

        auto* item = new QListWidgetItem(QIcon(":/icons/music.svg"), label);
        item->setData(Qt::UserRole, s.id);
        scheduleList_->addItem(item);
    }

    countLabel_->setText(
        QString("%1 programación(es) creada(s)").arg(schedules.size())
    );
}

} // namespace sara
