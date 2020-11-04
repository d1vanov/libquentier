/*
 * Copyright 2017-2020 Dmitry Ivanov
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
#include <quentier/utility/Size.h>

#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QMimeType>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "InsertHtmlUndoCommand",                                           \
            "Can't undo/redo the html insertion: "                             \
            "no note editor page"));                                           \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

InsertHtmlUndoCommand::InsertHtmlUndoCommand(
    const Callback & callback, NoteEditorPrivate & noteEditor,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
    ResourceInfo & resourceInfo, const QList<Resource> & addedResources,
    const QStringList & resourceFileStoragePaths, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_addedResources(addedResources),
    m_resourceFileStoragePaths(resourceFileStoragePaths), m_callback(callback),
    m_resourceFileStoragePathsByResourceLocalUid(
        resourceFileStoragePathsByResourceLocalUid),
    m_resourceInfo(resourceInfo)
{
    setText(tr("Insert HTML"));
}

InsertHtmlUndoCommand::InsertHtmlUndoCommand(
    const Callback & callback, NoteEditorPrivate & noteEditor,
    QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
    ResourceInfo & resourceInfo, const QString & text,
    const QList<Resource> & addedResources,
    const QStringList & resourceFileStoragePaths, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_addedResources(addedResources),
    m_resourceFileStoragePaths(resourceFileStoragePaths), m_callback(callback),
    m_resourceFileStoragePathsByResourceLocalUid(
        resourceFileStoragePathsByResourceLocalUid),
    m_resourceInfo(resourceInfo)
{}

InsertHtmlUndoCommand::~InsertHtmlUndoCommand() {}

void InsertHtmlUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "InsertHtmlUndoCommand::undoImpl");

    const QList<Resource> & addedResources = m_addedResources;
    int numResources = addedResources.size();

    for (int i = 0; i < numResources; ++i) {
        const Resource * pResource = &(addedResources.at(i));

        if (Q_UNLIKELY(!pResource->hasDataHash())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data "
                    << "hash: " << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body "
                        << "as well, skipping it");
                continue;
            }

            QByteArray hash = QCryptographicHash::hash(
                pResource->dataBody(), QCryptographicHash::Md5);

            m_addedResources[i].setDataHash(hash);
            // This might have caused detach, need to update the pointer to
            // the resource
            pResource = &(addedResources.at(i));
        }

        m_noteEditorPrivate.removeResourceFromNote(*pResource);

        auto rit = m_resourceFileStoragePathsByResourceLocalUid.find(
            pResource->localUid());

        if (Q_LIKELY(rit != m_resourceFileStoragePathsByResourceLocalUid.end()))
        {
            Q_UNUSED(m_resourceFileStoragePathsByResourceLocalUid.erase(rit))
        }

        m_resourceInfo.removeResourceInfo(pResource->dataHash());
    }

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("htmlInsertionManager.undo();"), m_callback);
}

void InsertHtmlUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "InsertHtmlUndoCommand::redoImpl");

    const QList<Resource> & addedResources = m_addedResources;
    int numResources = addedResources.size();

    QMimeDatabase mimeDatabase;

    for (int i = 0; i < numResources; ++i) {
        const Resource * pResource = &(addedResources.at(i));

        QMimeType mimeType;
        if (pResource->hasMime()) {
            mimeType = mimeDatabase.mimeTypeForName(pResource->mime());
        }

        if (Q_UNLIKELY(!mimeType.isValid())) {
            QNDEBUG(
                "note_editor:undo",
                "Could not deduce the resource data's "
                    << "mime type from the mime type name or resource has "
                    << "no declared mime type");

            if (pResource->hasDataBody()) {
                QNDEBUG(
                    "note_editor:undo",
                    "Trying to deduce the mime type "
                        << "from the resource data");
                mimeType = mimeDatabase.mimeTypeForData(pResource->dataBody());
            }
        }

        if (Q_UNLIKELY(!mimeType.isValid())) {
            QNDEBUG(
                "note_editor:undo",
                "All attempts to deduce the correct "
                    << "mime type have failed, fallback to mime type of "
                       "image/png");
            mimeType =
                mimeDatabase.mimeTypeForName(QStringLiteral("image/png"));
        }

        if (Q_UNLIKELY(!pResource->hasMime())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no mime type: " << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body "
                        << "as well, skipping it");
                continue;
            }

            m_addedResources[i].setMime(mimeType.name());
            // This might have caused resize, need to update the pointer
            // to the resource
            pResource = &(addedResources.at(i));
        }

        if (Q_UNLIKELY(!pResource->hasDataHash())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data "
                    << "hash: " << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body "
                        << "as well, skipping it");
                continue;
            }

            QByteArray hash = QCryptographicHash::hash(
                pResource->dataBody(), QCryptographicHash::Md5);
            m_addedResources[i].setDataHash(hash);
            // This might have caused resize, need to update the pointer
            // to the resource
            pResource = &(addedResources.at(i));
        }

        if (Q_UNLIKELY(!pResource->hasDataSize())) {
            QNDEBUG(
                "note_editor:undo",
                "One of added resources has no data "
                    << "size: " << *pResource);

            if (!pResource->hasDataBody()) {
                QNDEBUG(
                    "note_editor:undo",
                    "This resource has no data body "
                        << "as well, skipping it");
                continue;
            }

            m_addedResources[i].setDataSize(
                m_addedResources[i].dataBody().size());

            // This might have caused resize, need to update the pointer
            // to the resource
            pResource = &(addedResources.at(i));
        }

        m_noteEditorPrivate.addResourceToNote(*pResource);

        if (Q_LIKELY(m_resourceFileStoragePaths.size() > i)) {
            m_resourceFileStoragePathsByResourceLocalUid[pResource
                                                             ->localUid()] =
                m_resourceFileStoragePaths[i];

            QSize resourceImageSize;
            if (pResource->hasHeight() && pResource->hasWidth()) {
                resourceImageSize.setHeight(pResource->height());
                resourceImageSize.setWidth(pResource->width());
            }

            m_resourceInfo.cacheResourceInfo(
                pResource->dataHash(), pResource->displayName(),
                humanReadableSize(static_cast<quint64>(pResource->dataSize())),
                m_resourceFileStoragePaths[i], resourceImageSize);
        }
        else {
            QNWARNING(
                "note_editor:undo",
                "Can't restore the resource file "
                    << "storage path for one of resources: the number of "
                    << "resource file storage path is less than "
                    << "or equal to the index: paths = "
                    << m_resourceFileStoragePaths.join(QStringLiteral(", "))
                    << "; resource: " << pResource);
        }
    }

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("htmlInsertionManager.redo();"), m_callback);
}

} // namespace quentier
