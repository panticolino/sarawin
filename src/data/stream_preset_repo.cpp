#include "data/stream_preset_repo.h"
#include "data/database.h"
#include "util/logger.h"

namespace sara {

StreamPresetRepo::StreamPresetRepo(Database* db, QObject* parent)
    : QObject(parent), db_(db)
{
}

QVector<StreamPreset> StreamPresetRepo::getAll()
{
    QVector<StreamPreset> result;
    auto q = db_->execPrepared(
        "SELECT id, name, url FROM stream_presets ORDER BY name", {});
    if (!q) return result;

    while (q->next()) {
        StreamPreset p;
        p.id   = q->value(0).toInt();
        p.name = q->value(1).toString();
        p.url  = q->value(2).toString();
        result.append(p);
    }
    return result;
}

bool StreamPresetRepo::add(const QString& name, const QString& url)
{
    auto q = db_->execPrepared(
        "INSERT OR IGNORE INTO stream_presets (name, url) VALUES (?, ?)",
        {name, url});
    return q.has_value();
}

bool StreamPresetRepo::update(int id, const QString& name, const QString& url)
{
    auto q = db_->execPrepared(
        "UPDATE stream_presets SET name=?, url=? WHERE id=?",
        {name, url, id});
    return q.has_value();
}

bool StreamPresetRepo::remove(int id)
{
    auto q = db_->execPrepared("DELETE FROM stream_presets WHERE id=?", {id});
    return q.has_value();
}

StreamPreset StreamPresetRepo::getById(int id)
{
    StreamPreset p;
    auto q = db_->execPrepared("SELECT id, name, url FROM stream_presets WHERE id=?", {id});
    if (q && q->next()) {
        p.id   = q->value(0).toInt();
        p.name = q->value(1).toString();
        p.url  = q->value(2).toString();
    }
    return p;
}

} // namespace sara
