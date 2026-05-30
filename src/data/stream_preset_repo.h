#ifndef SARA_DATA_STREAM_PRESET_REPO_H
#define SARA_DATA_STREAM_PRESET_REPO_H

#include <QObject>
#include <QString>
#include <QVector>

namespace sara {

class Database;

struct StreamPreset {
    int     id = 0;
    QString name;
    QString url;
};

class StreamPresetRepo : public QObject
{
    Q_OBJECT
public:
    explicit StreamPresetRepo(Database* db, QObject* parent = nullptr);

    QVector<StreamPreset> getAll();
    bool add(const QString& name, const QString& url);
    bool update(int id, const QString& name, const QString& url);
    bool remove(int id);
    StreamPreset getById(int id);

private:
    Database* db_;
};

} // namespace sara

#endif
