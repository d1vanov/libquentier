/*
 * Copyright 2016-2024 Dmitry Ivanov
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

namespace quentier {

JavaScriptInOrderExecutor::JavaScriptInOrderExecutor(
    QWebEngineView & view, QObject * parent) :
    QObject(parent),
    m_view(view)
{}

void JavaScriptInOrderExecutor::append(
    const QString & script, JavaScriptInOrderExecutor::Callback callback)
{
    m_javaScriptsQueue.enqueue(std::make_pair(script, std::move(callback)));

    QNTRACE(
        "note_editor",
        "JavaScriptInOrderExecutor: appended new script, there are "
            << m_javaScriptsQueue.size() << " to execute now");
}

void JavaScriptInOrderExecutor::start()
{
    const auto scriptCallbackPair = m_javaScriptsQueue.dequeue();

    m_inProgress = true;

    const QString & script = scriptCallbackPair.first;
    const Callback & callback = scriptCallbackPair.second;

    m_currentPendingCallback = callback;
    m_view.page()->runJavaScript(script, JavaScriptCallback(*this));
}

void JavaScriptInOrderExecutor::next(const QVariant & data)
{
    QNTRACE("note_editor", "JavaScriptInOrderExecutor::next");

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

void JavaScriptInOrderExecutor::JavaScriptCallback::operator()(
    const QVariant & result)
{
    if (!m_executor.isNull()) {
        m_executor->next(result);
    }
}

} // namespace quentier
