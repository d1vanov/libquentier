/*
 * Copyright 2017-2024 Dmitry Ivanov
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

#include "InsertHtmlUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ResourceUtils.h>
#include <quentier/utility/Size.h>

#include <qevercloud/types/Resource.h>

#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QMimeType>

#include <utility>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "InsertHtmlUndoCommand",                                           \
            "Can't undo/redo the html insertion: no note editor page"));       \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

InsertHtmlUndoCommand::InsertHtmlUndoCommand(
    Callback callback, NoteEditorPrivate & noteEditor,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
    ResourceInfo & resourceInfo, QList<qevercloud::Resource> addedResources,
    QStringList resourceFileStoragePaths, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_addedResources(std::move(addedResources)),
    m_resourceFileStoragePaths(std::move(resourceFileStoragePaths)),
    m_callback(std::move(callback)),
    m_resourceFileStoragePathsByResourceLocalId(
        resourceFileStoragePathsByResourceLocalId),
    m_resourceInfo(resourceInfo)
{
    setText(tr("Insert HTML"));
}

InsertHtmlUndoCommand::InsertHtmlUndoCommand(
    Callback callback, NoteEditorPrivate & noteEditor,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
    ResourceInfo & resourceInfo, const QString & text,
    QList<qevercloud::Resource> addedResources,
    QStringList resourceFileStoragePaths, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_addedResources(std::move(addedResources)),
    m_resourceFileStoragePaths(std::move(resourceFileStoragePaths)),
    m_callback(std::move(callback)),
    m_resourceFileStoragePathsByResourceLocalId(
        resourceFileStoragePathsByResourceLocalId),
    m_resourceInfo(resourceInfo)
{}

InsertHtmlUndoCommand::~InsertHtmlUndoCommand() noexcept = default;

void InsertHtmlUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "InsertHtmlUndoCommand::undoImpl");

    for (auto & resource: m_addedResources) {
        if (Q_UNLIKELY(!(resource.data() && resource.data()->bodyHash()))) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data hash: " << resource);

            if (!(resource.data() && resource.data()->body())) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            const QByteArray hash = QCryptographicHash::hash(
                *resource.data()->body(), QCryptographicHash::Md5);

            resource.mutableData()->setBodyHash(hash);
        }

        m_noteEditorPrivate.removeResourceFromNote(resource);

        const auto rit = m_resourceFileStoragePathsByResourceLocalId.find(
            resource.localId());

        if (Q_LIKELY(rit != m_resourceFileStoragePathsByResourceLocalId.end()))
        {
            Q_UNUSED(m_resourceFileStoragePathsByResourceLocalId.erase(rit))
        }

        Q_UNUSED(
            m_resourceInfo.removeResourceInfo(*resource.data()->bodyHash()))
    }

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("htmlInsertionManager.undo();"), m_callback);
}

void InsertHtmlUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "InsertHtmlUndoCommand::redoImpl");

    QMimeDatabase mimeDatabase;

    for (auto it = m_addedResources.begin(), end = m_addedResources.end();
         it != end; ++it)
    {
        auto & resource = *it;
        auto index = std::distance(m_addedResources.begin(), it);

        QMimeType mimeType;
        if (resource.mime()) {
            mimeType = mimeDatabase.mimeTypeForName(*resource.mime());
        }

        if (Q_UNLIKELY(!mimeType.isValid())) {
            QNDEBUG(
                "note_editor:undo",
                "Could not deduce the resource data's "
                    << "mime type from the mime type name or resource has "
                    << "no declared mime type");

            if (resource.data() && resource.data()->body()) {
                QNDEBUG(
                    "note_editor:undo",
                    "Trying to deduce the mime type from the resource data");

                mimeType =
                    mimeDatabase.mimeTypeForData(*resource.data()->body());
            }
        }

        if (Q_UNLIKELY(!mimeType.isValid())) {
            QNDEBUG(
                "note_editor:undo",
                "All attempts to deduce the correct mime type have failed, "
                    << "fallback to mime type of image/png");

            mimeType =
                mimeDatabase.mimeTypeForName(QStringLiteral("image/png"));
        }

        if (Q_UNLIKELY(!resource.mime())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no mime type: " << resource);

            if (!(resource.data() && resource.data()->body())) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            resource.setMime(mimeType.name());
        }

        if (Q_UNLIKELY(!(resource.data() && resource.data()->bodyHash()))) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data hash: " << resource);

            if (!(resource.data() && resource.data()->body())) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            const QByteArray hash = QCryptographicHash::hash(
                *resource.data()->body(), QCryptographicHash::Md5);

            resource.mutableData()->setBodyHash(hash);
        }

        if (Q_UNLIKELY(!(resource.data() && resource.data()->size()))) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data size: " << resource);

            if (!(resource.data() && resource.data()->body())) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            resource.mutableData()->setSize(
                static_cast<qint32>(resource.data()->body()->size()));
        }

        m_noteEditorPrivate.addResourceToNote(resource);

        if (Q_LIKELY(m_resourceFileStoragePaths.size() > index)) {
            m_resourceFileStoragePathsByResourceLocalId[resource.localId()] =
                m_resourceFileStoragePaths[static_cast<int>(index)];

            QSize resourceImageSize;
            if (resource.height() && resource.width()) {
                resourceImageSize.setHeight(*resource.height());
                resourceImageSize.setWidth(*resource.width());
            }

            m_resourceInfo.cacheResourceInfo(
                *resource.data()->bodyHash(), resourceDisplayName(resource),
                humanReadableSize(
                    static_cast<quint64>(*resource.data()->size())),
                m_resourceFileStoragePaths[static_cast<int>(index)],
                resourceImageSize);
        }
        else {
            QNWARNING(
                "note_editor:undo",
                "Can't restore the resource file storage path for one of "
                    << "resources: the number of resource file storage path is "
                    << "less than or equal to the index: paths = "
                    << m_resourceFileStoragePaths.join(QStringLiteral(", "))
                    << "; resource: " << resource);
        }
    }

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("htmlInsertionManager.redo();"), m_callback);
}

} // namespace quentier
