/*
 * Copyright 2023 Dmitry Ivanov
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

#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QList>
#include <QObject>

#include <utility>

namespace quentier::synchronization {

class ISyncEventsNotifier;

} // namespace quentier::synchronization

namespace quentier::synchronization::tests {

class SyncEventsCollector : public QObject
{
    Q_OBJECT
public:
    struct SyncChunksDownloadProgressMessage
    {
        qint32 m_highestDownloadedUsn = 0;
        qint32 m_highestServerUsn = 0;
        qint32 m_lastPreviousUsn = 0;
    };

    struct NoteDownloadProgressMessage
    {
        quint32 m_notesDownloaded = 0;
        quint32 m_totalNotesToDownload = 0;
    };

    struct ResourceDownloadProgressMessage
    {
        quint32 m_resourcesDownloaded = 0;
        quint32 m_totalResourcesToDownload = 0;
    };

    using LinkedNotebookSyncChunksDownloadProgressMessages = QHash<
        qevercloud::Guid,
        std::pair<
            qevercloud::LinkedNotebook,
            QList<SyncChunksDownloadProgressMessage>>>;

    using LinkedNotebookSyncChunksDataCounters = QHash<
        qevercloud::Guid,
        std::pair<
            qevercloud::LinkedNotebook, QList<ISyncChunksDataCountersPtr>>>;

    using LinkedNotebookNoteDownloadProgressMessages = QHash<
        qevercloud::Guid,
        std::pair<
            qevercloud::LinkedNotebook, QList<NoteDownloadProgressMessage>>>;

    using LinkedNotebookResourceDownloadProgressMessages = QHash<
        qevercloud::Guid,
        std::pair<
            qevercloud::LinkedNotebook,
            QList<ResourceDownloadProgressMessage>>>;

    using LinkedNotebookSendStatusMessages =
        QHash<qevercloud::Guid, QList<ISendStatusPtr>>;

public:
    explicit SyncEventsCollector(QObject * parent = nullptr);

    void clear();

    [[nodiscard]] QList<SyncChunksDownloadProgressMessage>
        userOwnSyncChunksDownloadProgressMessages() const;

    [[nodiscard]] bool userOwnSyncChunksDownloaded() const noexcept;

    [[nodiscard]] QList<ISyncChunksDataCountersPtr>
        userOwnSyncChunksDataCounters() const;

    [[nodiscard]] bool startedLinkedNotebooksDataDownloading() const noexcept;

    [[nodiscard]] LinkedNotebookSyncChunksDownloadProgressMessages
        linkedNotebookSyncChunksDownloadProgressMessages() const;

    [[nodiscard]] QList<qevercloud::LinkedNotebook>
        syncChunksDownloadedLinkedNotebooks() const;

    [[nodiscard]] LinkedNotebookSyncChunksDataCounters
        linkedNotebookSyncChunksDataCounters() const;

    [[nodiscard]] QList<NoteDownloadProgressMessage>
        userOwnNoteDownloadProgressMessages() const;

    [[nodiscard]] LinkedNotebookNoteDownloadProgressMessages
        linkedNotebookNoteDownloadProgressMessages() const;

    [[nodiscard]] QList<ResourceDownloadProgressMessage>
        userOwnResourceDownloadProgressMessages() const;

    [[nodiscard]] LinkedNotebookResourceDownloadProgressMessages
        linkedNotebookResourceDownloadProgressMessages() const;

    [[nodiscard]] QList<ISendStatusPtr> userOwnSendStatusMessages() const;

    [[nodiscard]] LinkedNotebookSendStatusMessages
        linkedNotebookSendStatusMessages() const;

    void connectToNotifier(ISyncEventsNotifier * notifier) const;

    [[nodiscard]] bool checkProgressNotificationsOrder(
        const char *& errorMessage) const;

private Q_SLOTS:
    void onSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn);

    void onSyncChunksDownloaded();

    void onSyncChunksDataProcessingProgress(
        const ISyncChunksDataCountersPtr & counters);

    void onStartLinkedNotebooksDataDownloading(
        const QList<qevercloud::LinkedNotebook> & linkedNotebooks);

    void onLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void onLinkedNotebookSyncChunksDownloaded(
        const qevercloud::LinkedNotebook & linkedNotebook);

    void onLinkedNotebookSyncChunksDataProcessingProgress(
        const ISyncChunksDataCountersPtr & counters,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void onNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void onLinkedNotebookNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void onResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void onLinkedNotebookResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void onUserOwnSendStatusUpdate(const ISendStatusPtr & sendStatus);

    void onLinkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid,
        const ISendStatusPtr & sendStatus);

private:
    [[nodiscard]] bool checkUserOwnSyncChunksDownloadProgressOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkLinkedNotebookSyncChunksDownloadProgressOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkSyncChunksDownloadProgressOrderImpl(
        const QList<SyncChunksDownloadProgressMessage> & messages,
        const char *& errorMessage) const;

    [[nodiscard]] bool checkSingleSyncChunkDownloadProgressMessage(
        const SyncChunksDownloadProgressMessage & message,
        const char *& errorMessage) const;

    [[nodiscard]] bool checkUserOwnNotesDownloadProgressOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkLinkedNotebookNotesDownloadProgressOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkNotesDownloadProgressOrderImpl(
        const QList<NoteDownloadProgressMessage> & messages,
        const char *& errorMessage) const;

    [[nodiscard]] bool checkSingleNoteDownloadProgressMessage(
        const NoteDownloadProgressMessage & message,
        const char *& errorMessage) const;

    [[nodiscard]] bool checkUserOwnResourcesDownloadProgressOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkLinkedNotebookResourcesDownloadProgressOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkResourcesDownloadProgressOrderImpl(
        const QList<ResourceDownloadProgressMessage> & messages,
        const char *& errorMessage) const;

    [[nodiscard]] bool checkSingleResourceDownloadProgressMessage(
        const ResourceDownloadProgressMessage & message,
        const char *& errorMessage) const;

    [[nodiscard]] bool checkUserOwnSyncChunksDataCountersOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkLinkedNotebookSyncChunkDataCountersOrder(
        const char *& errorMessage) const;

    [[nodiscard]] bool checkSyncChunksDataCountersOrderImpl(
        const QList<ISyncChunksDataCountersPtr> & messages,
        const char *& errorMessage) const;

private:
    QList<SyncChunksDownloadProgressMessage>
        m_userOwnSyncChunksDownloadProgressMessages;

    bool m_userOwnSyncChunksDownloaded = false;
    QList<ISyncChunksDataCountersPtr> m_userOwnSyncChunksDataCounters;
    bool m_startedLinkedNotebooksDataDownloading = false;

    LinkedNotebookSyncChunksDownloadProgressMessages
        m_linkedNotebookSyncChunksDownloadProgressMessages;

    QList<qevercloud::LinkedNotebook> m_syncChunksDownloadedLinkedNotebooks;
    LinkedNotebookSyncChunksDataCounters m_linkedNotebookSyncChunksDataCounters;
    QList<NoteDownloadProgressMessage> m_userOwnNoteDownloadProgressMessages;

    LinkedNotebookNoteDownloadProgressMessages
        m_linkedNotebookNoteDownloadProgressMessages;

    QList<ResourceDownloadProgressMessage>
        m_userOwnResourceDownloadProgressMessages;

    LinkedNotebookResourceDownloadProgressMessages
        m_linkedNotebookResourceDownloadProgressMessages;

    QList<ISendStatusPtr> m_userOwnSendStatusMessages;
    LinkedNotebookSendStatusMessages m_linkedNotebookSendStatusMessages;
};

} // namespace quentier::synchronization::tests