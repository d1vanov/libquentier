#include "WebSocketWaiter.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

WebSocketWaiter::WebSocketWaiter(QObject * parent) :
    QObject(parent)
{}

void WebSocketWaiter::onReady()
{
    QNDEBUG(QStringLiteral("WebSocketWaiter::onReady"));
    Q_EMIT ready();
}

} // namespace quentier
