#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_WEB_SOCKET_WAITER_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_WEB_SOCKET_WAITER_H

#include <quentier/utility/Macros.h>
#include <QObject>

namespace quentier {

class WebSocketWaiter: public QObject
{
    Q_OBJECT
public:
    explicit WebSocketWaiter(QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void ready();

public Q_SLOTS:
    void onReady();
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_WEB_SOCKET_WAITER_H
