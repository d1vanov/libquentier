/*
 * Copyright 2022 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_RESOURCES_BINARY_DATA_FETCHER_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_RESOURCES_BINARY_DATA_FETCHER_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>

#include <QObject>
#include <QSet>
#include <QUuid>

namespace quentier {

class NoteResourcesBinaryDataFetcher : public QObject
{
    Q_OBJECT
public:
    explicit NoteResourcesBinaryDataFetcher(
        LocalStorageManagerAsync & localStorageManagerAsync,
        QObject * parent = nullptr);

Q_SIGNALS:
    void finished(Note note, QUuid requestId);
    void error(QUuid requestId, ErrorString errorDescription);

    // private signals
    void findResource(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

public Q_SLOTS:
    void onFetchResourceBinaryData(Note note, QUuid requestId);

private Q_SLOTS:
    void onFindResourceComplete(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onFindResourceFailed(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

private:
    void createConnections(LocalStorageManagerAsync & localStorageManagerAsync);

private:
    struct NoteData
    {
        Note m_note;
        QUuid m_requestId;
        QSet<QUuid> m_findResourceRequestIds;
    };

    QHash<QString, NoteData> m_noteDataByLocalUid;
    QHash<QUuid, QString> m_findResourceRequestIdToNoteLocalUid;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_RESOURCES_BINARY_DATA_FETCHER_H
