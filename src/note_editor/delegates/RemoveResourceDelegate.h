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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_REMOVE_RESOURCE_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_REMOVE_RESOURCE_DELEGATE_H

#include "JsResultCallbackFunctor.hpp"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Resource.h>

#include <QObject>

namespace quentier {

class LocalStorageManagerAsync;
class NoteEditorPrivate;

class Q_DECL_HIDDEN RemoveResourceDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit RemoveResourceDelegate(
        qevercloud::Resource resourceToRemove, NoteEditorPrivate & noteEditor,
        LocalStorageManagerAsync & localStorageManager);

    void start();

Q_SIGNALS:
    void finished(qevercloud::Resource removedResource, bool reversible);
    void cancelled(QString resourceLocalId);
    void notifyError(ErrorString error);

    // private signals
    void findResource(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options, QUuid requestId);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);

    void onResourceReferenceRemovedFromNoteContent(const QVariant & data);

private Q_SLOTS:
    void onFindResourceComplete(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onFindResourceFailed(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

private:
    void doStart();
    void removeResourceFromNoteEditorPage();
    void connectToLocalStorage();

private:
    using JsCallback = JsResultCallbackFunctor<RemoveResourceDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;
    LocalStorageManagerAsync & m_localStorageManager;
    qevercloud::Resource m_resource;
    bool m_reversible = true;

    QUuid m_findResourceRequestId;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_REMOVE_RESOURCE_DELEGATE_H
