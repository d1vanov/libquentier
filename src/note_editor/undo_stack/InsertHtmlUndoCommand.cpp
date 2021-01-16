/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#include <qevercloud/generated/types/Resource.h>

#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QMimeType>

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
    const Callback & callback, NoteEditorPrivate & noteEditor,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
    ResourceInfo & resourceInfo,
    const QList<qevercloud::Resource> & addedResources,
    const QStringList & resourceFileStoragePaths, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_addedResources(addedResources),
    m_resourceFileStoragePaths(resourceFileStoragePaths), m_callback(callback),
    m_resourceFileStoragePathsByResourceLocalId(
        resourceFileStoragePathsByResourceLocalId),
    m_resourceInfo(resourceInfo)
{
    setText(tr("Insert HTML"));
}

InsertHtmlUndoCommand::InsertHtmlUndoCommand(
    const Callback & callback, NoteEditorPrivate & noteEditor,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
    ResourceInfo & resourceInfo, const QString & text,
    const QList<qevercloud::Resource> & addedResources,
    const QStringList & resourceFileStoragePaths, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_addedResources(addedResources),
    m_resourceFileStoragePaths(resourceFileStoragePaths), m_callback(callback),
    m_resourceFileStoragePathsByResourceLocalId(
        resourceFileStoragePathsByResourceLocalId),
    m_resourceInfo(resourceInfo)
{}

InsertHtmlUndoCommand::~InsertHtmlUndoCommand() noexcept = default;

void InsertHtmlUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "InsertHtmlUndoCommand::undoImpl");

    const auto & addedResources = m_addedResources;
    const int numResources = addedResources.size();

    for (int i = 0; i < numResources; ++i) {
        const auto * pResource = &(addedResources.at(i));

        if (Q_UNLIKELY(!pResource->data() || !pResource->data()->bodyHash())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data hash: " << *pResource);

            if (!pResource->data() || !pResource->data()->body()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            const QByteArray hash = QCryptographicHash::hash(
                *pResource->data()->body(), QCryptographicHash::Md5);

            m_addedResources[i].mutableData()->setBodyHash(hash);

            // This might have caused detach, need to update the pointer to
            // the resource
            pResource = &(addedResources.at(i));
        }

        m_noteEditorPrivate.removeResourceFromNote(*pResource);

        const auto rit = m_resourceFileStoragePathsByResourceLocalId.find(
            pResource->localId());

        if (Q_LIKELY(rit != m_resourceFileStoragePathsByResourceLocalId.end()))
        {
            Q_UNUSED(m_resourceFileStoragePathsByResourceLocalId.erase(rit))
        }

        Q_UNUSED(
            m_resourceInfo.removeResourceInfo(*pResource->data()->bodyHash()))
    }

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("htmlInsertionManager.undo();"), m_callback);
}

void InsertHtmlUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "InsertHtmlUndoCommand::redoImpl");

    const auto & addedResources = m_addedResources;
    const int numResources = addedResources.size();

    QMimeDatabase mimeDatabase;

    for (int i = 0; i < numResources; ++i) {
        const auto * pResource = &(addedResources.at(i));

        QMimeType mimeType;
        if (pResource->mime()) {
            mimeType = mimeDatabase.mimeTypeForName(*pResource->mime());
        }

        if (Q_UNLIKELY(!mimeType.isValid())) {
            QNDEBUG(
                "note_editor:undo",
                "Could not deduce the resource data's "
                    << "mime type from the mime type name or resource has "
                    << "no declared mime type");

            if (pResource->data() && pResource->data()->body()) {
                QNDEBUG(
                    "note_editor:undo",
                    "Trying to deduce the mime type from the resource data");

                mimeType = mimeDatabase.mimeTypeForData(
                    *pResource->data()->body());
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

        if (Q_UNLIKELY(!pResource->mime())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no mime type: " << *pResource);

            if (!pResource->data() || !pResource->data()->body()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            m_addedResources[i].setMime(mimeType.name());
            // This might have caused resize, need to update the pointer
            // to the resource
            pResource = &(addedResources.at(i));
        }

        if (Q_UNLIKELY(!pResource->data() || !pResource->data()->bodyHash())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data hash: " << *pResource);

            if (!pResource->data() || !pResource->data()->body()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            const QByteArray hash = QCryptographicHash::hash(
                *pResource->data()->body(), QCryptographicHash::Md5);

            m_addedResources[i].mutableData()->setBodyHash(hash);

            // This might have caused resize, need to update the pointer
            // to the resource
            pResource = &(addedResources.at(i));
        }

        if (Q_UNLIKELY(!pResource->data() || !pResource->data()->size())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data size: " << *pResource);

            if (!pResource->data() || !pResource->data()->body()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body as well, skipping it");
                continue;
            }

            m_addedResources[i].mutableData()->setSize(
                m_addedResources[i].data()->body()->size());

            // This might have caused resize, need to update the pointer
            // to the resource
            pResource = &(addedResources.at(i));
        }

        m_noteEditorPrivate.addResourceToNote(*pResource);

        if (Q_LIKELY(m_resourceFileStoragePaths.size() > i)) {
            m_resourceFileStoragePathsByResourceLocalId[pResource->localId()] =
                m_resourceFileStoragePaths[i];

            QSize resourceImageSize;
            if (pResource->height() && pResource->width()) {
                resourceImageSize.setHeight(*pResource->height());
                resourceImageSize.setWidth(*pResource->width());
            }

            m_resourceInfo.cacheResourceInfo(
                *pResource->data()->bodyHash(), resourceDisplayName(*pResource),
                humanReadableSize(
                    static_cast<quint64>(*pResource->data()->size())),
                m_resourceFileStoragePaths[i], resourceImageSize);
        }
        else {
            QNWARNING(
                "note_editor:undo",
                "Can't restore the resource file storage path for one of "
                    << "resources: the number of resource file storage path is "
                    << "less than or equal to the index: paths = "
                    << m_resourceFileStoragePaths.join(QStringLiteral(", "))
                    << "; resource: " << pResource);
        }
    }

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("htmlInsertionManager.redo();"), m_callback);
}

} // namespace quentier
