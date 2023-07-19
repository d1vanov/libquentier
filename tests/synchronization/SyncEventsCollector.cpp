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

#include "SyncEventsCollector.h"

namespace quentier::synchronization::tests {

SyncEventsCollector::SyncEventsCollector(QObject * parent) : QObject(parent) {}

QList<SyncEventsCollector::SyncChunksDownloadProgressMessage>
    SyncEventsCollector::userOwnSyncChunksDownloadProgressMessages() const
{
    return m_userOwnSyncChunksDownloadProgressMessages;
}

bool SyncEventsCollector::userOwnSyncChunksDownloaded() const noexcept
{
    return m_userOwnSyncChunksDownloaded;
}

QList<ISyncChunksDataCountersPtr>
    SyncEventsCollector::userOwnSyncChunksDataCounters() const
{
    return m_userOwnSyncChunksDataCounters;
}

bool SyncEventsCollector::startedLinkedNotebooksDataDownloading() const noexcept
{
    return m_startedLinkedNotebooksDataDownloading;
}

SyncEventsCollector::LinkedNotebookSyncChunksDownloadProgressMessages
    SyncEventsCollector::linkedNotebookSyncChunksDownloadProgressMessages()
        const
{
    return m_linkedNotebookSyncChunksDownloadProgressMessages;
}

QList<qevercloud::LinkedNotebook>
    SyncEventsCollector::syncChunksDownloadedLinkedNotebooks() const
{
    return m_syncChunksDownloadedLinkedNotebooks;
}

SyncEventsCollector::LinkedNotebookSyncChunksDataCounters
    SyncEventsCollector::linkedNotebookSyncChunksDataCounters() const
{
    return m_linkedNotebookSyncChunksDataCounters;
}

QList<SyncEventsCollector::NoteDownloadProgressMessage>
    SyncEventsCollector::userOwnNoteDownloadProgressMessages() const
{
    return m_userOwnNoteDownloadProgressMessages;
}

SyncEventsCollector::LinkedNotebookNoteDownloadProgressMessages
    SyncEventsCollector::linkedNotebookNoteDownloadProgressMessages() const
{
    return m_linkedNotebookNoteDownloadProgressMessages;
}

QList<SyncEventsCollector::ResourceDownloadProgressMessage>
    SyncEventsCollector::userOwnResourceDownloadProgressMessages() const
{
    return m_userOwnResourceDownloadProgressMessages;
}

SyncEventsCollector::LinkedNotebookResourceDownloadProgressMessages
    SyncEventsCollector::linkedNotebookResourceDownloadProgressMessages() const
{
    return m_linkedNotebookResourceDownloadProgressMessages;
}

QList<ISendStatusPtr> SyncEventsCollector::userOwnSendStatusMessages() const
{
    return m_userOwnSendStatusMessages;
}

SyncEventsCollector::LinkedNotebookSendStatusMessages
    SyncEventsCollector::linkedNotebookSendStatusMessages() const
{
    return m_linkedNotebookSendStatusMessages;
}

void SyncEventsCollector::onSyncChunksDownloadProgress(
    const qint32 highestDownloadedUsn, const qint32 highestServerUsn,
    const qint32 lastPreviousUsn)
{
    m_userOwnSyncChunksDownloadProgressMessages.push_back(
        SyncChunksDownloadProgressMessage{
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn});
}

void SyncEventsCollector::onSyncChunksDownloaded()
{
    m_userOwnSyncChunksDownloaded = true;
}

void SyncEventsCollector::onSyncChunksDataProcessingProgress(
    const ISyncChunksDataCountersPtr & counters)
{
    m_userOwnSyncChunksDataCounters.push_back(counters);
}

void SyncEventsCollector::onStartLinkedNotebooksDataDownloading(
    [[maybe_unused]] const QList<qevercloud::LinkedNotebook> & linkedNotebooks)
{
    m_startedLinkedNotebooksDataDownloading = true;
}

void SyncEventsCollector::onLinkedNotebookSyncChunksDownloadProgress(
    const qint32 highestDownloadedUsn, const qint32 highestServerUsn,
    const qint32 lastPreviousUsn,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_ASSERT(linkedNotebook.guid());

    const bool contains =
        m_linkedNotebookSyncChunksDownloadProgressMessages.contains(
            *linkedNotebook.guid());

    auto & entry =
        m_linkedNotebookSyncChunksDownloadProgressMessages[*linkedNotebook
                                                                .guid()];

    if (contains) {
        Q_ASSERT(entry.first == linkedNotebook);
    }
    else {
        entry.first = linkedNotebook;
    }

    entry.second.push_back(SyncChunksDownloadProgressMessage{
        highestDownloadedUsn, highestServerUsn, lastPreviousUsn});
}

void SyncEventsCollector::onLinkedNotebookSyncChunksDownloaded(
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    m_syncChunksDownloadedLinkedNotebooks.push_back(linkedNotebook);
}

void SyncEventsCollector::onLinkedNotebookSyncChunksDataProcessingProgress(
    const ISyncChunksDataCountersPtr & counters,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_ASSERT(linkedNotebook.guid());

    const bool contains =
        m_linkedNotebookSyncChunksDataCounters.contains(*linkedNotebook.guid());

    auto & entry =
        m_linkedNotebookSyncChunksDataCounters[*linkedNotebook.guid()];

    if (contains) {
        Q_ASSERT(entry.first == linkedNotebook);
    }
    else {
        entry.first = linkedNotebook;
    }

    entry.second.push_back(counters);
}

void SyncEventsCollector::onNotesDownloadProgress(
    const quint32 notesDownloaded, const quint32 totalNotesToDownload)
{
    m_userOwnNoteDownloadProgressMessages.push_back(
        NoteDownloadProgressMessage{notesDownloaded, totalNotesToDownload});
}

void SyncEventsCollector::onLinkedNotebookNotesDownloadProgress(
    const quint32 notesDownloaded, const quint32 totalNotesToDownload,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_ASSERT(linkedNotebook.guid());

    const bool contains = m_linkedNotebookNoteDownloadProgressMessages.contains(
        *linkedNotebook.guid());

    auto & entry =
        m_linkedNotebookNoteDownloadProgressMessages[*linkedNotebook.guid()];

    if (contains) {
        Q_ASSERT(entry.first == linkedNotebook);
    }
    else {
        entry.first = linkedNotebook;
    }

    entry.second.push_back(
        NoteDownloadProgressMessage{notesDownloaded, totalNotesToDownload});
}

void SyncEventsCollector::onResourcesDownloadProgress(
    const quint32 resourcesDownloaded, const quint32 totalResourcesToDownload)
{
    m_userOwnResourceDownloadProgressMessages.push_back(
        ResourceDownloadProgressMessage{
            resourcesDownloaded, totalResourcesToDownload});
}

void SyncEventsCollector::onLinkedNotebookResourcesDownloadProgress(
    const quint32 resourcesDownloaded, const quint32 totalResourcesToDownload,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_ASSERT(linkedNotebook.guid());

    const bool contains =
        m_linkedNotebookResourceDownloadProgressMessages.contains(
            *linkedNotebook.guid());

    auto & entry =
        m_linkedNotebookResourceDownloadProgressMessages[*linkedNotebook
                                                              .guid()];

    if (contains) {
        Q_ASSERT(entry.first == linkedNotebook);
    }
    else {
        entry.first = linkedNotebook;
    }

    entry.second.push_back(ResourceDownloadProgressMessage{
        resourcesDownloaded, totalResourcesToDownload});
}

void SyncEventsCollector::onUserOwnSendStatusUpdate(
    const ISendStatusPtr & sendStatus)
{
    m_userOwnSendStatusMessages.push_back(sendStatus);
}

void SyncEventsCollector::onLinkedNotebookSendStatusUpdate(
    const qevercloud::Guid & linkedNotebookGuid,
    const ISendStatusPtr & sendStatus)
{
    m_linkedNotebookSendStatusMessages[linkedNotebookGuid].push_back(
        sendStatus);
}

} // namespace quentier::synchronization::tests
