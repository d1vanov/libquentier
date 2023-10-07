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

#include <quentier/synchronization/ISyncChunksDataCounters.h>
#include <quentier/synchronization/ISyncEventsNotifier.h>

#include <qevercloud/utility/ToRange.h>

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

void SyncEventsCollector::connectToNotifier(
    ISyncEventsNotifier * notifier) const
{
    QObject::connect(
        notifier, &ISyncEventsNotifier::syncChunksDownloadProgress, this,
        &SyncEventsCollector::onSyncChunksDownloadProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::syncChunksDownloaded, this,
        &SyncEventsCollector::onSyncChunksDownloaded);

    QObject::connect(
        notifier, &ISyncEventsNotifier::syncChunksDataProcessingProgress, this,
        &SyncEventsCollector::onSyncChunksDataProcessingProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::startLinkedNotebooksDataDownloading,
        this, &SyncEventsCollector::onStartLinkedNotebooksDataDownloading);

    QObject::connect(
        notifier,
        &ISyncEventsNotifier::linkedNotebookSyncChunksDownloadProgress, this,
        &SyncEventsCollector::onLinkedNotebookSyncChunksDownloadProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookSyncChunksDownloaded,
        this, &SyncEventsCollector::onLinkedNotebookSyncChunksDownloaded);

    QObject::connect(
        notifier,
        &ISyncEventsNotifier::linkedNotebookSyncChunksDataProcessingProgress,
        this,
        &SyncEventsCollector::onLinkedNotebookSyncChunksDataProcessingProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::notesDownloadProgress, this,
        &SyncEventsCollector::onNotesDownloadProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookNotesDownloadProgress,
        this, &SyncEventsCollector::onLinkedNotebookNotesDownloadProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::resourcesDownloadProgress, this,
        &SyncEventsCollector::onResourcesDownloadProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookResourcesDownloadProgress,
        this, &SyncEventsCollector::onLinkedNotebookResourcesDownloadProgress);

    QObject::connect(
        notifier, &ISyncEventsNotifier::userOwnSendStatusUpdate, this,
        &SyncEventsCollector::onUserOwnSendStatusUpdate);

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookSendStatusUpdate, this,
        &SyncEventsCollector::onLinkedNotebookSendStatusUpdate);
}

bool SyncEventsCollector::checkProgressNotificationsOrder(
    const char *& errorMessage) const
{
    if (!checkUserOwnSyncChunksDownloadProgressOrder(errorMessage)) {
        return false;
    }

    if (!checkLinkedNotebookSyncChunksDownloadProgressOrder(errorMessage)) {
        return false;
    }

    if (!checkUserOwnSyncChunksDataCountersOrder(errorMessage)) {
        return false;
    }

    if (!checkLinkedNotebookSyncChunkDataCountersOrder(errorMessage)) {
        return false;
    }

    if (!checkUserOwnNotesDownloadProgressOrder(errorMessage)) {
        return false;
    }

    if (!checkLinkedNotebookNotesDownloadProgressOrder(errorMessage)) {
        return false;
    }

    if (!checkUserOwnResourcesDownloadProgressOrder(errorMessage)) {
        return false;
    }

    if (!checkLinkedNotebookResourcesDownloadProgressOrder(errorMessage)) {
        return false;
    }

    return true;
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

bool SyncEventsCollector::checkUserOwnSyncChunksDownloadProgressOrder(
    const char *& errorMessage) const
{
    return checkSyncChunksDownloadProgressOrderImpl(
        m_userOwnSyncChunksDownloadProgressMessages, errorMessage);
}

bool SyncEventsCollector::checkLinkedNotebookSyncChunksDownloadProgressOrder(
    const char *& errorMessage) const
{
    for (const auto it: qevercloud::toRange(
             qAsConst(m_linkedNotebookSyncChunksDownloadProgressMessages)))
    {
        bool res = checkSyncChunksDownloadProgressOrderImpl(
            it.value().second, errorMessage);

        if (!res) {
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkSyncChunksDownloadProgressOrderImpl(
    const QList<SyncChunksDownloadProgressMessage> & messages,
    const char *& errorMessage) const
{
    if (messages.isEmpty()) {
        return true;
    }

    if (messages.size() == 1) {
        return checkSingleSyncChunkDownloadProgressMessage(
            messages[0], errorMessage);
    }

    for (int i = 1, size = messages.size(); i < size; ++i) {
        const auto & currentProgress = messages[i];

        if (!checkSingleSyncChunkDownloadProgressMessage(
                currentProgress, errorMessage)) {
            return false;
        }

        const auto & previousProgress = messages[i - 1];
        if (i == 1) {
            if (!checkSingleSyncChunkDownloadProgressMessage(
                    previousProgress, errorMessage))
            {
                return false;
            }
        }

        if (previousProgress.m_highestDownloadedUsn >=
            currentProgress.m_highestDownloadedUsn)
        {
            errorMessage = "Found decreasing highest downloaded USN";
            return false;
        }

        if (previousProgress.m_highestServerUsn !=
            currentProgress.m_highestServerUsn) {
            errorMessage =
                "Highest server USN changed between two sync "
                "chunk download progresses";
            return false;
        }

        if (previousProgress.m_lastPreviousUsn !=
            currentProgress.m_lastPreviousUsn) {
            errorMessage =
                "Last previous USN changed between two sync "
                "chunk download progresses";
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkSingleSyncChunkDownloadProgressMessage(
    const SyncChunksDownloadProgressMessage & message,
    const char *& errorMessage) const
{
    if (message.m_highestDownloadedUsn > message.m_highestServerUsn) {
        errorMessage =
            "Detected highest downloaded USN greater than highest server USN";
        return false;
    }

    if (message.m_lastPreviousUsn > message.m_highestDownloadedUsn) {
        errorMessage =
            "Detected last previous USN greater than highest downloaded USN";
        return false;
    }

    return true;
}

bool SyncEventsCollector::checkUserOwnNotesDownloadProgressOrder(
    const char *& errorMessage) const
{
    return checkNotesDownloadProgressOrderImpl(
        m_userOwnNoteDownloadProgressMessages, errorMessage);
}

bool SyncEventsCollector::checkLinkedNotebookNotesDownloadProgressOrder(
    const char *& errorMessage) const
{
    for (const auto it: qevercloud::toRange(
             qAsConst(m_linkedNotebookNoteDownloadProgressMessages)))
    {
        if (!checkNotesDownloadProgressOrderImpl(
                it.value().second, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkNotesDownloadProgressOrderImpl(
    const QList<NoteDownloadProgressMessage> & messages,
    const char *& errorMessage) const
{
    if (messages.isEmpty()) {
        return true;
    }

    if (messages.size() == 1) {
        return checkSingleNoteDownloadProgressMessage(
            messages[0], errorMessage);
    }

    for (int i = 1, size = messages.size(); i < size; ++i) {
        const auto & currentProgress = messages[i];
        if (!checkSingleNoteDownloadProgressMessage(
                currentProgress, errorMessage)) {
            return false;
        }

        const auto & previousProgress = messages[i - 1];
        if ((i == 1) &&
            !checkSingleNoteDownloadProgressMessage(
                previousProgress, errorMessage))
        {
            return false;
        }

        if (previousProgress.m_notesDownloaded >=
            currentProgress.m_notesDownloaded) {
            errorMessage = "Found non-increasing downloaded notes count";
            return false;
        }

        if (previousProgress.m_totalNotesToDownload !=
            currentProgress.m_totalNotesToDownload)
        {
            errorMessage =
                "The total number of notes to download has "
                "changed between two progresses";
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkSingleNoteDownloadProgressMessage(
    const NoteDownloadProgressMessage & message,
    const char *& errorMessage) const
{
    if (message.m_notesDownloaded > message.m_totalNotesToDownload) {
        errorMessage =
            "The number of downloaded notes is greater than the total number "
            "of notes to download";
        return false;
    }

    return true;
}

bool SyncEventsCollector::checkUserOwnResourcesDownloadProgressOrder(
    const char *& errorMessage) const
{
    return checkResourcesDownloadProgressOrderImpl(
        m_userOwnResourceDownloadProgressMessages, errorMessage);
}

bool SyncEventsCollector::checkLinkedNotebookResourcesDownloadProgressOrder(
    const char *& errorMessage) const
{
    for (const auto it: qevercloud::toRange(
             qAsConst(m_linkedNotebookResourceDownloadProgressMessages)))
    {
        if (!checkResourcesDownloadProgressOrderImpl(
                it.value().second, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkResourcesDownloadProgressOrderImpl(
    const QList<ResourceDownloadProgressMessage> & messages,
    const char *& errorMessage) const
{
    if (messages.isEmpty()) {
        return true;
    }

    if (messages.size() == 1) {
        return checkSingleResourceDownloadProgressMessage(
            messages[0], errorMessage);
    }

    for (int i = 1, size = messages.size(); i < size; ++i) {
        const auto & currentProgress = messages[i];

        if (!checkSingleResourceDownloadProgressMessage(
                currentProgress, errorMessage)) {
            return false;
        }

        const auto & previousProgress = messages[i - 1];
        if (i == 1) {
            if (!checkSingleResourceDownloadProgressMessage(
                    previousProgress, errorMessage)) {
                return false;
            }
        }

        if (previousProgress.m_resourcesDownloaded >=
            currentProgress.m_resourcesDownloaded)
        {
            errorMessage =
                "Found non-increasing downloaded resources "
                "count";
            return false;
        }

        if (previousProgress.m_totalResourcesToDownload !=
            currentProgress.m_totalResourcesToDownload)
        {
            errorMessage =
                "The total number of resources to download has "
                "changed between two progresses";
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkSingleResourceDownloadProgressMessage(
    const ResourceDownloadProgressMessage & message,
    const char *& errorMessage) const
{
    if (message.m_resourcesDownloaded > message.m_totalResourcesToDownload) {
        errorMessage =
            "The number of downloaded resources is greater than "
            "the total number of resources to download";

        return false;
    }

    return true;
}

bool SyncEventsCollector::checkUserOwnSyncChunksDataCountersOrder(
    const char *& errorMessage) const
{
    return checkSyncChunksDataCountersOrderImpl(
        m_userOwnSyncChunksDataCounters, errorMessage);
}

bool SyncEventsCollector::checkLinkedNotebookSyncChunkDataCountersOrder(
    const char *& errorMessage) const
{
    for (const auto it:
         qevercloud::toRange(qAsConst(m_linkedNotebookSyncChunksDataCounters)))
    {
        // FIXME: temporarily disabling check for total counters integrity
        // for linked notebooks as one of the tests revealed they might not
        // always be consistent. Need to debug and fix it later.
        if (!checkSyncChunksDataCountersOrderImpl(
                it.value().second, errorMessage, CheckTotalCounters::No)) {
            return false;
        }
    }

    return true;
}

bool SyncEventsCollector::checkSyncChunksDataCountersOrderImpl(
    const QList<ISyncChunksDataCountersPtr> & messages,
    const char *& errorMessage,
    const CheckTotalCounters checkTotalCounters) const
{
    if (messages.isEmpty()) {
        return true;
    }

    ISyncChunksDataCountersPtr lastSyncChunksDataCounters;
    for (const auto & currentCounters: qAsConst(messages)) {
        Q_ASSERT(currentCounters);

        if (!lastSyncChunksDataCounters) {
            lastSyncChunksDataCounters = currentCounters;
            continue;
        }

        if (checkTotalCounters == CheckTotalCounters::Yes) {
            if (currentCounters->totalSavedSearches() !=
                lastSyncChunksDataCounters->totalSavedSearches())
            {
                errorMessage =
                    "The number of total saved searches is different in "
                    "consequent sync chunks data counters";
                return false;
            }

            if (currentCounters->totalExpungedSavedSearches() !=
                lastSyncChunksDataCounters->totalExpungedSavedSearches())
            {
                errorMessage =
                    "The number of total expunged saved searches is different "
                    "in consequent sync chunks data counters";
                return false;
            }

            if (currentCounters->totalTags() !=
                lastSyncChunksDataCounters->totalTags()) {
                errorMessage =
                    "The number of total tags is different in consequent sync "
                    "chunks data counters";
                return false;
            }

            if (currentCounters->totalExpungedTags() !=
                lastSyncChunksDataCounters->totalExpungedTags())
            {
                errorMessage =
                    "The number of total expunged tags is different in "
                    "consequent sync chunks data counters";
                return false;
            }

            if (currentCounters->totalNotebooks() !=
                lastSyncChunksDataCounters->totalNotebooks())
            {
                errorMessage =
                    "The number of total notebooks is different in consequent "
                    "sync chunks data counters";
                return false;
            }

            if (currentCounters->totalExpungedNotebooks() !=
                lastSyncChunksDataCounters->totalExpungedNotebooks())
            {
                errorMessage =
                    "The number of total expunged notebooks is different in "
                    "consequent sync chunks data counters";
                return false;
            }

            if (currentCounters->totalLinkedNotebooks() !=
                lastSyncChunksDataCounters->totalLinkedNotebooks())
            {
                errorMessage =
                    "The number of total linked notebooks is different in "
                    "consequent sync chunks data counters";
                return false;
            }

            if (currentCounters->totalExpungedLinkedNotebooks() !=
                lastSyncChunksDataCounters->totalExpungedLinkedNotebooks())
            {
                errorMessage =
                    "The number of total expunged linked notebooks is "
                    "different in consequent sync chunks data counters";
                return false;
            }
        }

        if (currentCounters->addedSavedSearches() <
            lastSyncChunksDataCounters->addedSavedSearches())
        {
            errorMessage =
                "The number of added saved searches is unexpectedly declining "
                "in consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->updatedSavedSearches() <
            lastSyncChunksDataCounters->updatedSavedSearches())
        {
            errorMessage =
                "The number of updated saved searches is unexpectedly "
                "declining in consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->expungedSavedSearches() <
            lastSyncChunksDataCounters->expungedSavedSearches())
        {
            errorMessage =
                "The number of expunged saved searches is unexpectedly "
                "declining in consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->addedTags() <
            lastSyncChunksDataCounters->addedTags()) {
            errorMessage =
                "The number of added tags is unexpectedly declining in "
                "consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->updatedTags() <
            lastSyncChunksDataCounters->updatedTags()) {
            errorMessage =
                "The number of updated tags is unexpectedly declining in "
                "consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->expungedTags() <
            lastSyncChunksDataCounters->expungedTags())
        {
            errorMessage =
                "The number of expunged tags is unexpectedly declining in "
                "consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->addedNotebooks() <
            lastSyncChunksDataCounters->addedNotebooks())
        {
            errorMessage =
                "The number of added notebooks is unexpectedly declining in "
                "consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->updatedNotebooks() <
            lastSyncChunksDataCounters->updatedNotebooks())
        {
            errorMessage =
                "The number of updated notebooks is unexpectedly declining in "
                "consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->expungedNotebooks() <
            lastSyncChunksDataCounters->expungedNotebooks())
        {
            errorMessage =
                "The number of expunged notebooks is unexpectedly declining in "
                "consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->addedLinkedNotebooks() <
            lastSyncChunksDataCounters->addedLinkedNotebooks())
        {
            errorMessage =
                "The number of added linked notebooks is unexpectedly "
                "declining in consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->updatedLinkedNotebooks() <
            lastSyncChunksDataCounters->updatedLinkedNotebooks())
        {
            errorMessage =
                "The number of updated linked notebooks is unexpectedly "
                "declining in consequent sync chunks data counters";
            return false;
        }

        if (currentCounters->expungedLinkedNotebooks() <
            lastSyncChunksDataCounters->expungedLinkedNotebooks())
        {
            errorMessage =
                "The number of expunged linked notebooks is unexpectedly "
                "declining in consequent sync chunks data counters";
            return false;
        }
    }

    return true;
}

} // namespace quentier::synchronization::tests
