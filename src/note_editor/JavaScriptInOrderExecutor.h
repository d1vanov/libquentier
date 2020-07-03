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

#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVA_SCRIPT_IN_ORDER_EXECUTOR_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVA_SCRIPT_IN_ORDER_EXECUTOR_H

#include <quentier/utility/Macros.h>
#include <quentier/utility/SuppressWarnings.h>

#include <QObject>
#include <QQueue>

#ifdef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebEngineView>
#else
#include <QWebView>
#endif

SAVE_WARNINGS
GCC_SUPPRESS_WARNING(-Wdeprecated-declarations)

#include <boost/function.hpp>

RESTORE_WARNINGS

namespace quentier {

class Q_DECL_HIDDEN JavaScriptInOrderExecutor final: public QObject
{
    Q_OBJECT
private:
    using WebView =
#ifdef QUENTIER_USE_QT_WEB_ENGINE
    QWebEngineView;
#else
    QWebView;
#endif

public:
    using Callback = boost::function<void (const QVariant&)>;

    explicit JavaScriptInOrderExecutor(
        WebView & view, QObject * parent = nullptr);

    void append(const QString & script, Callback callback = 0);

    int size() const { return m_javaScriptsQueue.size(); }

    bool empty() const { return m_javaScriptsQueue.empty(); }

    void clear() { m_javaScriptsQueue.clear(); }

    void start();

    bool inProgress() const { return m_inProgress; }

Q_SIGNALS:
    void finished();

private:
    class Q_DECL_HIDDEN JavaScriptCallback
    {
    public:
        JavaScriptCallback(JavaScriptInOrderExecutor & executor) :
            m_executor(executor)
        {}

        void operator()(const QVariant & result);

    private:
        JavaScriptInOrderExecutor & m_executor;
    };

    friend class JavaScriptCallback;

    void next(const QVariant & data);

private:
    WebView &                           m_view;
    QQueue<QPair<QString, Callback>>    m_javaScriptsQueue;
    Callback                            m_currentPendingCallback;
    bool                                m_inProgress;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVA_SCRIPT_IN_ORDER_EXECUTOR_H
