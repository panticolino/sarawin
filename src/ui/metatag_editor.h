#ifndef SARA_UI_METATAG_EDITOR_H
#define SARA_UI_METATAG_EDITOR_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>

namespace sara {

/**
 * Diálogo para editar metatags/ID3 de archivos de audio.
 * Soporta MP3 (ID3v2), OGG (Vorbis), FLAC, M4A (MP4).
 */
class MetatagEditor : public QDialog
{
    Q_OBJECT

public:
    explicit MetatagEditor(const QString& filePath, QWidget* parent = nullptr);

    /// Retorna true si los tags fueron modificados y guardados
    bool wasModified() const { return modified_; }

private:
    void loadTags();
    bool saveTags();

    QString filePath_;
    bool    modified_ = false;

    QLineEdit* titleEdit_;
    QLineEdit* artistEdit_;
    QLineEdit* albumEdit_;
    QSpinBox*  yearSpin_;
    QLabel*    infoLabel_;
};

} // namespace sara

#endif
