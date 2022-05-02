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

#include "IResourcesHandler.h"
#include "Transaction.h"

#include <QDir>
#include <QReadWriteLock>

#include <memory>

namespace quentier::local_storage::sql {

class ResourcesHandler final :
    public IResourcesHandler,
    public std::enable_shared_from_this<ResourcesHandler>
{
public:
    explicit ResourcesHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread,
        const QString & localStorageDirPath,
        QReadWriteLockPtr resourceDataFilesLock);

    [[nodiscard]] QFuture<quint32> resourceCount(
        NoteCountOptions options = NoteCountOptions{
            NoteCountOption::IncludeNonDeletedNotes}) const override;

    [[nodiscard]] QFuture<quint32> resourceCountPerNoteLocalId(
        QString noteLocalId) const override;

    [[nodiscard]] QFuture<void> putResource(
        qevercloud::Resource resource) override;

    [[nodiscard]] QFuture<void> putResourceMetadata(
        qevercloud::Resource resource) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Resource>>
        findResourceByLocalId(
            QString resourceLocalId,
            FetchResourceOptions options = {}) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Resource>>
        findResourceByGuid(
            qevercloud::Guid resourceGuid,
            FetchResourceOptions options = {}) const override;

    [[nodiscard]] QFuture<void> expungeResourceByLocalId(
        QString resourceLocalId) override;

    [[nodiscard]] QFuture<void> expungeResourceByGuid(
        qevercloud::Guid resourceGuid) override;

private:
    [[nodiscard]] std::optional<quint32> resourceCountImpl(
        NoteCountOptions noteCountOptions, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<quint32> resourceCountPerNoteLocalIdImpl(
        const QString & noteLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeResourceByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription,
        std::optional<Transaction> transaction = std::nullopt);

    [[nodiscard]] bool expungeResourceByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    QThreadPtr m_writerThread;
    QDir m_localStorageDir;
    QReadWriteLockPtr m_resourceDataFilesLock;
};

} // namespace quentier::local_storage::sql
