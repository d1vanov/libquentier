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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_PAGE_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_PAGE_H

#include "JavaScriptInOrderExecutor.h"

#ifndef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebPage>
#else
#include <QWebEnginePage>
#endif

#include <atomic>
#include <memory>

namespace quentier {

using WebPage =
#ifndef QUENTIER_USE_QT_WEB_ENGINE
    QWebPage;
#else
    QWebEnginePage;
#endif

QT_FORWARD_DECLARE_CLASS(NoteEditor)
QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)

class Q_DECL_HIDDEN NoteEditorPage final : public WebPage
{
    Q_OBJECT
public:
    using Callback = JavaScriptInOrderExecutor::Callback;

public:
    explicit NoteEditorPage(NoteEditorPrivate & parent);

    virtual ~NoteEditorPage();

    bool javaScriptQueueEmpty() const;

    void setInactive();
    void setActive();

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

    virtual void triggerAction(
        WebPage::WebAction action, bool checked = false) override;

Q_SIGNALS:
    void javaScriptLoaded();
    void noteLoadCancelled();

    void undoActionRequested();
    void redoActionRequested();

    void pasteActionRequested();
    void pasteAndMatchStyleActionRequested();
    void cutActionRequested();

public Q_SLOTS:
#ifndef QUENTIER_USE_QT_WEB_ENGINE
    virtual bool shouldInterruptJavaScript() override;
#else
    bool shouldInterruptJavaScript();
#endif

    void executeJavaScript(
        const QString & script, Callback callback = 0,
        const bool clearPreviousQueue = false);

private Q_SLOTS:
    void onJavaScriptQueueEmpty();

private:
#ifndef QUENTIER_USE_QT_WEB_ENGINE
    virtual void javaScriptAlert(
        QWebFrame * pFrame, const QString & message) override;

    virtual bool javaScriptConfirm(
        QWebFrame * pFrame, const QString & message) override;

    virtual void javaScriptConsoleMessage(
        const QString & message, int lineNumber,
        const QString & sourceID) override;
#else
    virtual void javaScriptAlert(
        const QUrl & securityOrigin, const QString & msg) override;

    virtual bool javaScriptConfirm(
        const QUrl & securityOrigin, const QString & msg) override;

    virtual void javaScriptConsoleMessage(
        JavaScriptConsoleMessageLevel level, const QString & message,
        int lineNumber, const QString & sourceID) override;
#endif

private:
    NoteEditorPrivate * m_parent;
    std::shared_ptr<std::atomic<bool>> m_javaScriptCanceler;
    JavaScriptInOrderExecutor * m_pJavaScriptInOrderExecutor;
    bool m_javaScriptAutoExecution = true;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_PAGE_H
