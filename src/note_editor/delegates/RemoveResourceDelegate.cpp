/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "RemoveResourceDelegate.h"

#include "../NoteEditorSettingsNames.h"
#include "../NoteEditor_p.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/MessageBox.h>
#include <quentier/utility/Size.h>

#include <algorithm>
#include <cmath>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor.page());         \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "RemoveResourceDelegate",                                          \
            "Can't remove the attachment: "                                    \
            "no note editor page"));                                           \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

RemoveResourceDelegate::RemoveResourceDelegate(
    qevercloud::Resource resourceToRemove, NoteEditorPrivate & noteEditor,
    LocalStorageManagerAsync & localStorageManager) :
    m_noteEditor(noteEditor),
    m_localStorageManager(localStorageManager),
    m_resource(std::move(resourceToRemove))
{}

void RemoveResourceDelegate::start()
{
    QNDEBUG("note_editor:delegate", "RemoveResourceDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &RemoveResourceDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        doStart();
    }
}

void RemoveResourceDelegate::onOriginalPageConvertedToNote(
    qevercloud::Note note) // NOLINT
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveResourceDelegate::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &RemoveResourceDelegate::onOriginalPageConvertedToNote);

    doStart();
}

void RemoveResourceDelegate::onFindResourceComplete(
    qevercloud::Resource resource,
    LocalStorageManager::GetResourceOptions options, QUuid requestId)
{
    if (m_findResourceRequestId != requestId) {
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "RemoveResourceDelegate::onFindResourceComplete: request id = "
            << requestId);

    Q_UNUSED(options)
    m_findResourceRequestId = QUuid{};

    m_resource = std::move(resource);
    removeResourceFromNoteEditorPage();
}

void RemoveResourceDelegate::onFindResourceFailed(
    qevercloud::Resource resource, // NOLINT
    LocalStorageManager::GetResourceOptions options,
    ErrorString errorDescription, QUuid requestId) // NOLINT
{
    if (m_findResourceRequestId != requestId) {
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "RemoveResourceDelegate::onFindResourceFailed: request id = "
            << requestId << ", error description: " << errorDescription);

    Q_UNUSED(resource)
    Q_UNUSED(options)
    m_findResourceRequestId = QUuid{};

    Q_EMIT notifyError(errorDescription);
}

void RemoveResourceDelegate::doStart()
{
    QNDEBUG("note_editor:delegate", "RemoveResourceDelegate::doStart");

    if (Q_UNLIKELY(!m_resource.data() || !m_resource.data()->bodyHash())) {
        ErrorString error(
            QT_TR_NOOP("Can't remove the attachment: data hash is missing"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    const Account * pAccount = m_noteEditor.accountPtr();
    if (Q_UNLIKELY(!pAccount)) {
        ErrorString error(
            QT_TR_NOOP("Can't remove the attachment: no account "
                       "is set to the note editor"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    ApplicationSettings appSettings(*pAccount, NOTE_EDITOR_SETTINGS_NAME);
    int resourceDataSizeThreshold = -1;
    if (appSettings.contains(
            NOTE_EDITOR_REMOVE_RESOURCE_UNDO_DATA_SIZE_THRESHOLD)) {
        const QVariant threshold = appSettings.value(
            NOTE_EDITOR_REMOVE_RESOURCE_UNDO_DATA_SIZE_THRESHOLD);

        bool conversionResult = false;
        resourceDataSizeThreshold = threshold.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(
                "note_editor:delegate",
                "Failed to convert resource "
                    << "undo data size threshold from persistent settings to "
                       "int: "
                    << threshold);
            resourceDataSizeThreshold = -1;
        }
    }

    if (resourceDataSizeThreshold < 0) {
        resourceDataSizeThreshold =
            NOTE_EDITOR_REMOVE_RESOURCE_UNDO_DATA_SIZE_DEFAULT_THRESHOLD;
    }

    if ((!m_resource.data()->body() && m_resource.data()->size() &&
         (*m_resource.data()->size() > resourceDataSizeThreshold)) ||
        (!m_resource.data()->body() && m_resource.alternateData() &&
         !m_resource.alternateData()->body() &&
         m_resource.alternateData()->size() &&
         (*m_resource.alternateData()->size() > resourceDataSizeThreshold)))
    {
        const int resourceDataSize =
            (m_resource.data()->size() ? *m_resource.data()->size()
                                       : *m_resource.alternateData()->size());

        const int result = questionMessageBox(
            &m_noteEditor, tr("Confirm attachment removal"),
            tr("The attachment removal would be irreversible"),
            tr("Are you sure you want to remove this "
               "attachment? Due to its large size") +
                QStringLiteral(" (") +
                humanReadableSize(
                    static_cast<quint64>(std::max(resourceDataSize, 0))) +
                QStringLiteral(") ") + tr("its removal would be irreversible"));

        if (result != QMessageBox::Ok) {
            Q_EMIT cancelled(m_resource.localId());
            return;
        }

        m_reversible = false;
    }

    if (m_reversible && (!m_resource.data() || m_resource.data()->body()) &&
        (!m_resource.alternateData() || !m_resource.alternateData()->body()))
    {
        connectToLocalStorage();
        m_findResourceRequestId = QUuid::createUuid();

        QNDEBUG(
            "note_editor:delegate",
            "Emitting the request to find resource "
                << "within the local storage: request id = "
                << m_findResourceRequestId
                << ", resource local id = " << m_resource.localId());

        const LocalStorageManager::GetResourceOptions options(
            LocalStorageManager::GetResourceOption::WithBinaryData);

        Q_EMIT findResource(m_resource, options, m_findResourceRequestId);
        return;
    }

    removeResourceFromNoteEditorPage();
}

void RemoveResourceDelegate::removeResourceFromNoteEditorPage()
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveResourceDelegate::removeResourceFromNoteEditorPage");

    const QString javascript =
        QStringLiteral("resourceManager.removeResource('") +
        QString::fromLocal8Bit(m_resource.data()->bodyHash()->toHex()) +
        QStringLiteral("');");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(
            *this,
            &RemoveResourceDelegate::
                onResourceReferenceRemovedFromNoteContent));
}

void RemoveResourceDelegate::connectToLocalStorage()
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveResourceDelegate::connectToLocalStorage");

    QObject::connect(
        this, &RemoveResourceDelegate::findResource, &m_localStorageManager,
        &LocalStorageManagerAsync::onFindResourceRequest);

    QObject::connect(
        &m_localStorageManager, &LocalStorageManagerAsync::findResourceComplete,
        this, &RemoveResourceDelegate::onFindResourceComplete);

    QObject::connect(
        &m_localStorageManager, &LocalStorageManagerAsync::findResourceFailed,
        this, &RemoveResourceDelegate::onFindResourceFailed);
}

void RemoveResourceDelegate::onResourceReferenceRemovedFromNoteContent(
    const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "RemoveResourceDelegate::onResourceReferenceRemovedFromNoteContent");

    const auto resultMap = data.toMap();

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of attachment "
                       "reference removal from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of attachment "
                           "reference removal from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't remove the attachment reference "
                           "from the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_noteEditor.removeResourceFromNote(m_resource);

    Q_EMIT finished(m_resource, m_reversible);
}

} // namespace quentier
