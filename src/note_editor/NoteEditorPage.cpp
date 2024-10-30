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

#include "NoteEditorPage.h"

#include "JavaScriptInOrderExecutor.h"
#include "NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

#include <QAction>
#include <QMessageBox>

namespace quentier {

NoteEditorPage::NoteEditorPage(NoteEditorPrivate & parent) :
    QWebEnginePage(&parent), m_parent(&parent),
    m_javaScriptCanceler{std::make_shared<std::atomic<bool>>(false)},
    m_pJavaScriptInOrderExecutor(
        new JavaScriptInOrderExecutor(parent, m_javaScriptCanceler, this))
{
    QObject::connect(
        this, &NoteEditorPage::noteLoadCancelled, &parent,
        &NoteEditorPrivate::onNoteLoadCancelled);

    QObject::connect(
        m_pJavaScriptInOrderExecutor, &JavaScriptInOrderExecutor::finished,
        this, &NoteEditorPage::onJavaScriptQueueEmpty);
}

NoteEditorPage::~NoteEditorPage() noexcept
{
    QNDEBUG("note_editor", "NoteEditorPage::~NoteEditorPage");
    m_javaScriptCanceler->store(true, std::memory_order_release);
}

bool NoteEditorPage::javaScriptQueueEmpty() const noexcept
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPage::javaScriptQueueEmpty: "
            << (m_pJavaScriptInOrderExecutor->empty() ? "true" : "false"));

    return m_pJavaScriptInOrderExecutor->empty();
}

void NoteEditorPage::stopJavaScriptAutoExecution()
{
    QNDEBUG("note_editor", "NoteEditorPage::stopJavaScriptAutoExecution");
    m_javaScriptAutoExecution = false;
}

void NoteEditorPage::startJavaScriptAutoExecution()
{
    QNDEBUG("note_editor", "NoteEditorPage::startJavaScriptAutoExecution");

    m_javaScriptAutoExecution = true;

    if (!m_pJavaScriptInOrderExecutor->inProgress()) {
        m_pJavaScriptInOrderExecutor->start();
    }
}

bool NoteEditorPage::shouldInterruptJavaScript()
{
    QNDEBUG("note_editor", "NoteEditorPage::shouldInterruptJavaScript");

    const QString title = tr("Note editor hanged");

    const QString question =
        tr("Note editor seems hanged when loading or editing "
           "the note. Would you like to cancel loading the note?");

    QMessageBox::StandardButton reply = QMessageBox::question(
        m_parent, title, question, QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QNINFO(
            "note_editor",
            "Note load was cancelled due to too long javascript evaluation");
        Q_EMIT noteLoadCancelled();
        return true;
    }

    QNINFO(
        "note_editor", "Note load seems to hang but user wished to wait more");
    return false;
}

void NoteEditorPage::executeJavaScript(
    const QString & script, Callback callback, const bool clearPreviousQueue)
{
    if (Q_UNLIKELY(clearPreviousQueue)) {
        m_pJavaScriptInOrderExecutor->clear();
    }

    m_pJavaScriptInOrderExecutor->append(script, std::move(callback));

    if (m_javaScriptAutoExecution &&
        !m_pJavaScriptInOrderExecutor->inProgress())
    {
        m_pJavaScriptInOrderExecutor->start();
    }
}

void NoteEditorPage::onJavaScriptQueueEmpty()
{
    QNDEBUG("note_editor", "NoteEditorPage::onJavaScriptQueueEmpty");
    Q_EMIT javaScriptLoaded();
}

void NoteEditorPage::javaScriptAlert(
    const QUrl & securityOrigin, const QString & msg)
{
    QNDEBUG("note_editor", "NoteEditorPage::javaScriptAlert, message: " << msg);
    QWebEnginePage::javaScriptAlert(securityOrigin, msg);
}

bool NoteEditorPage::javaScriptConfirm(
    const QUrl & securityOrigin, const QString & msg)
{
    QNDEBUG(
        "note_editor", "NoteEditorPage::javaScriptConfirm, message: " << msg);

    return QWebEnginePage::javaScriptConfirm(securityOrigin, msg);
}

void NoteEditorPage::javaScriptConsoleMessage(
    QWebEnginePage::JavaScriptConsoleMessageLevel level,
    const QString & message, int lineNumber, const QString & sourceID)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPage::javaScriptConsoleMessage, message: "
            << message << ", level = " << level
            << ", line number: " << lineNumber << ", sourceID = " << sourceID);

    QWebEnginePage::javaScriptConsoleMessage(
        level, message, lineNumber, sourceID);
}

void NoteEditorPage::triggerAction(
    QWebEnginePage::WebAction action, bool checked)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorPage::triggerAction: action = "
            << action << ", checked = " << (checked ? "true" : "false"));

    if (action == QWebEnginePage::Back) {
        QNDEBUG("note_editor", "Filtering back action away");
        return;
    }

    if (action == QWebEnginePage::Paste) {
        QNDEBUG("note_editor", "Filtering paste action");
        Q_EMIT pasteActionRequested();
        return;
    }

    if (action == QWebEnginePage::PasteAndMatchStyle) {
        QNDEBUG("note_editor", "Filtering paste and match style action");
        Q_EMIT pasteAndMatchStyleActionRequested();
        return;
    }

    if (action == QWebEnginePage::Cut) {
        QNDEBUG("note_editor", "Filtering cut action");
        Q_EMIT cutActionRequested();
        return;
    }

    if (action == QWebEnginePage::Undo) {
        QNDEBUG("note_editor", "Filtering undo action");
        Q_EMIT undoActionRequested();
        return;
    }

    if (action == QWebEnginePage::Redo) {
        QNDEBUG("note_editor", "Filtering redo action");
        Q_EMIT redoActionRequested();
        return;
    }

    QWebEnginePage::triggerAction(action, checked);
}

} // namespace quentier
