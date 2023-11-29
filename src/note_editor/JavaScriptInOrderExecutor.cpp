/*
 * Copyright 2016-2020 Dmitry Ivanov
 *
 * This file is part of libquentier
 *
 * libquentier is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * libquentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libquentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "JavaScriptInOrderExecutor.h"

#include <quentier/logging/QuentierLogger.h>

#ifndef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebFrame>
#endif

namespace quentier {

JavaScriptInOrderExecutor::JavaScriptInOrderExecutor(
    WebView & view, Canceler canceler, QObject * parent) :
    QObject{parent},
    m_view{view}, m_canceler{std::move(canceler)}
{
    Q_ASSERT(m_canceler);
}

void JavaScriptInOrderExecutor::append(
    const QString & script, JavaScriptInOrderExecutor::Callback callback)
{
    m_javaScriptsQueue.enqueue(std::make_pair(script, callback));

    QNTRACE(
        "note_editor",
        "JavaScriptInOrderExecutor: appended new script, there are "
            << m_javaScriptsQueue.size() << " to execute now");
}

void JavaScriptInOrderExecutor::start()
{
    auto scriptCallbackPair = m_javaScriptsQueue.dequeue();

    m_inProgress = true;

    const QString & script = scriptCallbackPair.first;
    const Callback & callback = scriptCallbackPair.second;

    m_currentPendingCallback = callback;

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    m_view.page()->runJavaScript(script, JavaScriptCallback(*this));
#else
    QVariant data = m_view.page()->mainFrame()->evaluateJavaScript(script);
    next(data);
#endif
}

void JavaScriptInOrderExecutor::next(const QVariant & data)
{
    QNTRACE("note_editor", "JavaScriptInOrderExecutor::next");

    if (canceled()) {
        QNDEBUG("note_editor", "JavaScriptInOrderExecutor: canceled");
        m_javaScriptsQueue.clear();
        m_inProgress = false;
        Q_EMIT finished();
        return;
    }

    if (m_currentPendingCallback) {
        m_currentPendingCallback(data);
        m_currentPendingCallback = {};
    }

    if (m_javaScriptsQueue.empty()) {
        QNTRACE("note_editor", "JavaScriptInOrderExecutor: done");
        m_inProgress = false;
        Q_EMIT finished();
        return;
    }

    QNTRACE(
        "note_editor",
        "JavaScriptInOrderExecutor: " << m_javaScriptsQueue.size()
                                      << " more scripts to execute");
    start();
}

bool JavaScriptInOrderExecutor::canceled() const
{
    return m_canceler->load(std::memory_order_acquire);
}

void JavaScriptInOrderExecutor::JavaScriptCallback::operator()(
    const QVariant & result)
{
    if (!m_executor.isNull()) {
        m_executor->next(result);
    }
}

} // namespace quentier
