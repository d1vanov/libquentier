/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <qevercloud/Fwd.h>
#include <qevercloud/types/AuthenticationResult.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/SyncState.h>
#include <qevercloud/types/Tag.h>

#include <QObject>
#include <QPointer>

class QTcpServer;
class QTcpSocket;

namespace quentier::synchronization::tests {

class FakeNoteStoreBackend;

class NoteStoreServer : public QObject
{
    Q_OBJECT
public:
    NoteStoreServer(FakeNoteStoreBackend * backend, QObject * parent = nullptr);
    ~NoteStoreServer() override;

    [[nodiscard]] quint16 port() const noexcept;

    // private signals
Q_SIGNALS:
    void createNotebookRequestReady(
        qevercloud::Notebook notebook, std::exception_ptr e, QUuid requestId);

    void updateNotebookRequestReady(
        qint32 updateSequenceNum, std::exception_ptr e, QUuid requestId);

    void createNoteRequestReady(
        qevercloud::Note note, std::exception_ptr e, QUuid requestId);

    void updateNoteRequestReady(
        qevercloud::Note note, std::exception_ptr e, QUuid requestId);

    void createTagRequestReady(
        qevercloud::Tag, std::exception_ptr e, QUuid requestId);

    void updateTagRequestReady(
        qint32 updateSequenceNum, std::exception_ptr e, QUuid requestId);

    void createSavedSearchRequestReady(
        qevercloud::SavedSearch search, std::exception_ptr e, QUuid requestId);

    void updateSavedSearchRequestReady(
        qint32 updateSequenceNum, std::exception_ptr e, QUuid requestId);

    void getSyncStateRequestReady(
        qevercloud::SyncState syncState, std::exception_ptr e, QUuid requestId);

    void getLinkedNotebookSyncStateRequestReady(
        qevercloud::SyncState syncState, std::exception_ptr e, QUuid requestId);

    void getFilteredSyncChunkRequestReady(
        qevercloud::SyncChunk syncChunk, std::exception_ptr e, QUuid requestId);

    void getLinkedNotebookSyncChunkRequestReady(
        qevercloud::SyncChunk syncChunk, std::exception_ptr e, QUuid requestId);

    void getNoteWithResultSpecRequestReady(
        qevercloud::Note note, std::exception_ptr e, QUuid requestId);

    void getResourceRequestReady(
        qevercloud::Resource resource, std::exception_ptr e, QUuid requestId);

    void authenticateToSharedNotebookRequestReady(
        qevercloud::AuthenticationResult result, std::exception_ptr e,
        QUuid requestId);

private Q_SLOTS:
    void onRequestReady(const QByteArray & responseData, QUuid requestId);

private:
    void connectToQEverCloudServer();

private:
    QPointer<FakeNoteStoreBackend> m_backend;

    QTcpServer * m_tcpServer = nullptr;
    qevercloud::NoteStoreServer * m_server = nullptr;
    QHash<QUuid, QTcpSocket *> m_sockets;
};

} // namespace quentier::synchronization::tests
