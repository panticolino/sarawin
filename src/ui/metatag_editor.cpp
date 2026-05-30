#include "ui/metatag_editor.h"
#include "util/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QFileInfo>
#include <QMessageBox>
#include <QLocale>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

namespace sara {

MetatagEditor::MetatagEditor(const QString& filePath, QWidget* parent)
    : QDialog(parent), filePath_(filePath)
{
    setWindowTitle(tr("Editar metatags — %1").arg(QFileInfo(filePath).fileName()));
    setMinimumWidth(450);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 12, 16, 12);

    // Info del archivo
    QFileInfo fi(filePath);
    infoLabel_ = new QLabel(QString("%1 · %2 · %3")
        .arg(fi.fileName())
        .arg(fi.suffix().toUpper())
        .arg(QLocale().formattedDataSize(fi.size())));
    infoLabel_->setStyleSheet("font-size: 11px; color: rgba(255,255,255,0.5);");
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);

    // Formulario de tags
    auto* tagsGroup = new QGroupBox(tr("Metatags"));
    auto* form = new QFormLayout(tagsGroup);
    form->setSpacing(8);

    titleEdit_ = new QLineEdit();
    titleEdit_->setPlaceholderText(tr("Título de la pista"));
    form->addRow(tr("Título:"), titleEdit_);

    artistEdit_ = new QLineEdit();
    artistEdit_->setPlaceholderText(tr("Artista o intérprete"));
    form->addRow(tr("Artista:"), artistEdit_);

    albumEdit_ = new QLineEdit();
    albumEdit_->setPlaceholderText(tr("Álbum o producción"));
    form->addRow(tr("Álbum:"), albumEdit_);

    yearSpin_ = new QSpinBox();
    yearSpin_->setRange(0, 2100);
    yearSpin_->setSpecialValueText(tr("Sin año"));
    yearSpin_->setValue(0);
    yearSpin_->setFixedWidth(100);
    form->addRow(tr("Año:"), yearSpin_);

    layout->addWidget(tagsGroup);

    // Botones
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(tr("Cancelar"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);

    auto* saveBtn = new QPushButton(tr("Guardar"));
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        if (saveTags()) {
            modified_ = true;
            accept();
        }
    });
    btnRow->addWidget(saveBtn);

    layout->addLayout(btnRow);

    // Cargar tags existentes
    loadTags();
}

void MetatagEditor::loadTags()
{
    TagLib::FileRef f(filePath_.toUtf8().constData());
    if (f.isNull() || !f.tag()) {
        LOG_WARN("[MetatagEditor] No se pudo leer tags de: {}",
                 filePath_.toStdString());
        return;
    }

    TagLib::Tag* tag = f.tag();

    QString title = QString::fromStdString(tag->title().to8Bit(true));
    QString artist = QString::fromStdString(tag->artist().to8Bit(true));
    QString album = QString::fromStdString(tag->album().to8Bit(true));
    unsigned int year = tag->year();

    titleEdit_->setText(title);
    artistEdit_->setText(artist);
    albumEdit_->setText(album);
    if (year > 0) yearSpin_->setValue(static_cast<int>(year));

    // Duración en info
    if (f.audioProperties()) {
        int secs = f.audioProperties()->lengthInSeconds();
        int bitrate = f.audioProperties()->bitrate();
        int channels = f.audioProperties()->channels();
        int sampleRate = f.audioProperties()->sampleRate();
        infoLabel_->setText(infoLabel_->text() +
            QString(" · %1:%2 · %3kbps · %4ch · %5Hz")
            .arg(secs / 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0'))
            .arg(bitrate).arg(channels).arg(sampleRate));
    }

    LOG_INFO("[MetatagEditor] Tags cargados: {} — {} — {} ({})",
             title.toStdString(), artist.toStdString(),
             album.toStdString(), year);
}

bool MetatagEditor::saveTags()
{
    TagLib::FileRef f(filePath_.toUtf8().constData());
    if (f.isNull() || !f.tag()) {
        QMessageBox::warning(this, tr("Error"),
            tr("No se pudo abrir el archivo para escritura:\n%1").arg(filePath_));
        return false;
    }

    TagLib::Tag* tag = f.tag();

    tag->setTitle(TagLib::String(titleEdit_->text().toUtf8().constData(), TagLib::String::UTF8));
    tag->setArtist(TagLib::String(artistEdit_->text().toUtf8().constData(), TagLib::String::UTF8));
    tag->setAlbum(TagLib::String(albumEdit_->text().toUtf8().constData(), TagLib::String::UTF8));
    tag->setYear(yearSpin_->value());

    if (!f.save()) {
        QMessageBox::warning(this, tr("Error"),
            tr("No se pudieron guardar los metatags.\n"
               "Verifique los permisos del archivo."));
        return false;
    }

    LOG_INFO("[MetatagEditor] Tags guardados: {} — {} — {} ({})",
             titleEdit_->text().toStdString(),
             artistEdit_->text().toStdString(),
             albumEdit_->text().toStdString(),
             yearSpin_->value());
    return true;
}

} // namespace sara
