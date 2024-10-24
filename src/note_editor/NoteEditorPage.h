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

#include "JavaScriptInOrderExecutor.h"

#include <QWebEnginePage>

#include <atomic>
#include <memory>

namespace quentier {

class NoteEditor;
class NoteEditorPrivate;

class NoteEditorPage final : public QWebEnginePage
{
    Q_OBJECT
public:
    using Callback = JavaScriptInOrderExecutor::Callback;

public:
    explicit NoteEditorPage(NoteEditorPrivate & parent);

    ~NoteEditorPage() noexcept override;

    [[nodiscard]] bool javaScriptQueueEmpty() const noexcept;

    /**
     * @brief stopJavaScriptAutoExecution method can be used to prevent
     * the actual execution of JavaScript code immediately on calling
     * executeJavaScript; instead the code would be put on the queue for
     * subsequent execution and the signal javaScriptLoaded would only be
     * emitted when the whole queue is executed
     */
    void stopJavaScriptAutoExecution();

    /**
     * @brief startJavaScriptAutoExecution is the counterpart of
     * stopJavaScriptAutoExecution: when called on stopped JavaScript queue it
     * starts the execution of the code in the queue until it is empty;
     * if the auto execution was not stopped or the queue of JavaScript code is
     * empty, calling this method has no effect
     */
    void startJavaScriptAutoExecution();

    void triggerAction(
        QWebEnginePage::WebAction action, bool checked = false) override;

Q_SIGNALS:
    void javaScriptLoaded();
    void noteLoadCancelled();

    void undoActionRequested();
    void redoActionRequested();

    void pasteActionRequested();
    void pasteAndMatchStyleActionRequested();
    void cutActionRequested();

public Q_SLOTS:
    bool shouldInterruptJavaScript();

    void executeJavaScript(
        const QString & script, Callback callback = {},
        bool clearPreviousQueue = false);

private Q_SLOTS:
    void onJavaScriptQueueEmpty();

private:
    void javaScriptAlert(
        const QUrl & securityOrigin, const QString & msg) override;

    [[nodiscard]] bool javaScriptConfirm(
        const QUrl & securityOrigin, const QString & msg) override;

    void javaScriptConsoleMessage(
        JavaScriptConsoleMessageLevel level, const QString & message,
        int lineNumber, const QString & sourceID) override;

private:
    NoteEditorPrivate * m_parent;
    std::shared_ptr<std::atomic<bool>> m_javaScriptCanceler;
    JavaScriptInOrderExecutor * m_pJavaScriptInOrderExecutor;
    bool m_javaScriptAutoExecution = true;
};

} // namespace quentier
