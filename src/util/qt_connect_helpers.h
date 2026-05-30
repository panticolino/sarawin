#ifndef SARA_UTIL_QT_CONNECT_HELPERS_H
#define SARA_UTIL_QT_CONNECT_HELPERS_H

#include <QObject>
#include <memory>

namespace sara {

/**
 * Helper para conectar una señal a un slot/lambda y desconectar
 * automáticamente tras la primera invocación.
 *
 * Equivalente funcional a Qt::SingleShotConnection, que solo está
 * disponible desde Qt 6.6. SARA Libre necesita compilar contra Qt 6.4
 * (Debian 12 / Devuan 5), donde ese flag aún no existe.
 *
 * El patrón usa shared_ptr para que el lambda capture el handle de
 * conexión por valor; al primer disparo, el lambda desconecta usando
 * ese mismo handle. Es seguro frente a cancelaciones (la conexión
 * regular se desconecta también si el receptor se destruye).
 *
 * Uso típico:
 *
 *   connectOnce(crossfader_, &Crossfader::fadeOutFinished, this,
 *               [this]() {
 *       // ... código original del slot ...
 *   });
 *
 * Si necesitás un tipo de conexión específico (Qt::QueuedConnection,
 * Qt::DirectConnection), pasalo como último argumento.
 */
template <typename Sender, typename Signal, typename Receiver, typename Slot>
inline QMetaObject::Connection connectOnce(
    Sender* sender, Signal signal,
    Receiver* receiver, Slot&& slot,
    Qt::ConnectionType type = Qt::AutoConnection)
{
    auto handle = std::make_shared<QMetaObject::Connection>();
    *handle = QObject::connect(
        sender, signal,
        receiver,
        [handle, slot = std::forward<Slot>(slot)](auto&&... args) mutable {
            QObject::disconnect(*handle);
            slot(std::forward<decltype(args)>(args)...);
        },
        type
    );
    return *handle;
}

} // namespace sara

#endif // SARA_UTIL_QT_CONNECT_HELPERS_H
