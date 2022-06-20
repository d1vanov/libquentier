/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/ISyncChunksDataCounters.h>
#include <quentier/synchronization/types/AuthenticationInfo.h>
#include <quentier/synchronization/types/SyncOptions.h>
#include <quentier/synchronization/types/SyncState.h>
#include <quentier/synchronization/types/SyncStats.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <QDir>
#include <QException>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QNetworkCookie>

#include <memory>
#include <optional>

namespace quentier {

class Account;
class ErrorString;

} // namespace quentier

namespace quentier::synchronization {

class QUENTIER_EXPORT ISynchronizer
{
public:
    struct QUENTIER_EXPORT DownloadNotesStatus : public Printable
    {
        QTextStream & print(QTextStream & strm) const override;

        struct QUENTIER_EXPORT NoteWithException : public Printable
        {
            NoteWithException() = default;

            NoteWithException(
                qevercloud::Note n, std::shared_ptr<QException> e) :
                note{std::move(n)},
                exception{std::move(e)}
            {}

            QTextStream & print(QTextStream & strm) const override;

            qevercloud::Note note;
            std::shared_ptr<QException> exception;
        };

        struct QUENTIER_EXPORT GuidWithException : public Printable
        {
            GuidWithException() = default;

            GuidWithException(
                qevercloud::Guid g, std::shared_ptr<QException> e) :
                guid{std::move(g)},
                exception{std::move(e)}
            {}

            QTextStream & print(QTextStream & strm) const override;

            qevercloud::Guid guid;
            std::shared_ptr<QException> exception;
        };

        using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

        struct QUENTIER_EXPORT GuidWithUsn : public Printable
        {
            GuidWithUsn() = default;

            GuidWithUsn(qevercloud::Guid g, qint32 u) :
                guid{std::move(g)}, updateSequenceNumber{u}
            {}

            QTextStream & print(QTextStream & strm) const override;

            qevercloud::Guid guid;
            qint32 updateSequenceNumber = 0;
        };

        quint64 totalNewNotes = 0UL;
        quint64 totalUpdatedNotes = 0UL;
        quint64 totalExpungedNotes = 0UL;

        QList<NoteWithException> notesWhichFailedToDownload;
        QList<NoteWithException> notesWhichFailedToProcess;
        QList<GuidWithException> noteGuidsWhichFailedToExpunge;

        UpdateSequenceNumbersByGuid processedNoteGuidsAndUsns;
        UpdateSequenceNumbersByGuid cancelledNoteGuidsAndUsns;
        QList<qevercloud::Guid> expungedNoteGuids;
    };

    struct QUENTIER_EXPORT DownloadResourcesStatus : public Printable
    {
        QTextStream & print(QTextStream & strm) const override;

        struct QUENTIER_EXPORT ResourceWithException : public Printable
        {
            ResourceWithException() = default;

            ResourceWithException(
                qevercloud::Resource r, std::shared_ptr<QException> e) :
                resource{std::move(r)},
                exception{std::move(e)}
            {}

            QTextStream & print(QTextStream & strm) const override;

            qevercloud::Resource resource;
            std::shared_ptr<QException> exception;
        };

        using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

        quint64 totalNewResources = 0UL;
        quint64 totalUpdatedResources = 0UL;

        QList<ResourceWithException> resourcesWhichFailedToDownload;
        QList<ResourceWithException> resourcesWhichFailedToProcess;

        UpdateSequenceNumbersByGuid processedResourceGuidsAndUsns;
        UpdateSequenceNumbersByGuid cancelledResourceGuidsAndUsns;
    };

    struct QUENTIER_EXPORT SyncResult : public Printable
    {
        QTextStream & print(QTextStream & strm) const override;

        SyncState userAccountSyncState;
        QHash<qevercloud::Guid, SyncState> linkedNotebookSyncStates;

        DownloadNotesStatus userAccountDownloadNotesStatus;
        QHash<qevercloud::Guid, DownloadNotesStatus>
            linkedNotebookDownloadNotesStatuses;

        DownloadResourcesStatus userAccountDownloadResourcesStatus;
        QHash<qevercloud::Guid, DownloadResourcesStatus>
            linkedNotebookDownloadResourcesStatuses;

        SyncStats syncStats;
    };

public:
    virtual ~ISynchronizer() = default;

    /**
     * @return true if synchronization is being performed at the moment,
     *         false otherwise
     */
    [[nodiscard]] virtual bool isSyncRunning() const = 0;

    /**
     * @return options passed to ISynchronizer on the last sync
     */
    [[nodiscard]] virtual SyncOptions options() const = 0;

    [[nodiscard]] virtual QFuture<AuthenticationInfo>
        authenticateNewAccount() = 0;

    [[nodiscard]] virtual QFuture<AuthenticationInfo> authenticateAccount(
        Account account) = 0;

    [[nodiscard]] virtual QFuture<SyncResult> synchronizeAccount(
        Account account, ISyncConflictResolverPtr syncConflictResolver,
        local_storage::ILocalStoragePtr localStorage, SyncOptions options) = 0;

    [[nodiscard]] virtual QFuture<void> revokeAuthentication(
        qevercloud::UserID userId) = 0;

    [[nodiscard]] virtual ISyncEventsNotifier * notifier() const = 0;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ISynchronizer::DownloadNotesStatus & lhs,
    const ISynchronizer::DownloadNotesStatus & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ISynchronizer::DownloadNotesStatus & lhs,
    const ISynchronizer::DownloadNotesStatus & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ISynchronizer::DownloadNotesStatus::NoteWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::NoteWithException & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ISynchronizer::DownloadNotesStatus::NoteWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::NoteWithException & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ISynchronizer::DownloadNotesStatus::GuidWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithException & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ISynchronizer::DownloadNotesStatus::GuidWithException & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithException & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & lhs,
    const ISynchronizer::DownloadNotesStatus::GuidWithUsn & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ISynchronizer::DownloadResourcesStatus & lhs,
    const ISynchronizer::DownloadResourcesStatus & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ISynchronizer::DownloadResourcesStatus & lhs,
    const ISynchronizer::DownloadResourcesStatus & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException & lhs,
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException &
        rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException & lhs,
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException &
        rhs) noexcept;

} // namespace quentier::synchronization
