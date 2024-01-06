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

#pragma once

#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QWebEngineView>

#include <functional>
#include <utility>

namespace quentier {

class JavaScriptInOrderExecutor final : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QVariant &)>;

    explicit JavaScriptInOrderExecutor(
        QWebEngineView & view, QObject * parent = nullptr);

    void append(const QString & script, Callback callback = {});

    [[nodiscard]] qsizetype size() const noexcept
    {
        return m_javaScriptsQueue.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_javaScriptsQueue.empty();
    }

    void clear()
    {
        m_javaScriptsQueue.clear();
    }

    void start();

    [[nodiscard]] bool inProgress() const noexcept
    {
        return m_inProgress;
    }

Q_SIGNALS:
    void finished();

private:
    class JavaScriptCallback
    {
    public:
        JavaScriptCallback(JavaScriptInOrderExecutor & executor) :
            m_executor(&executor)
        {}

        void operator()(const QVariant & result);

    private:
        QPointer<JavaScriptInOrderExecutor> m_executor;
    };

    friend class JavaScriptCallback;

    void next(const QVariant & data);

private:
    QWebEngineView & m_view;
    QQueue<std::pair<QString, Callback>> m_javaScriptsQueue;
    Callback m_currentPendingCallback;
    bool m_inProgress = false;
};

} // namespace quentier
