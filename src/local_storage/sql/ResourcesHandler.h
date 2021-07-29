/*
 * Copyright 2021 Dmitry Ivanov
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

#include "Fwd.h"
#include "Transaction.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/Resource.h>

#include <QDir>
#include <QReadWriteLock>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class ResourcesHandler final :
    public std::enable_shared_from_this<ResourcesHandler>
{
public:
    using FetchResourceOption = ILocalStorage::FetchResourceOption;
    using FetchResourceOptions = ILocalStorage::FetchResourceOptions;

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

public:
    explicit ResourcesHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread,
        const QString & localStorageDirPath);

    [[nodiscard]] QFuture<quint32> resourceCount(
        NoteCountOptions options = NoteCountOptions{
            NoteCountOption::IncludeNonDeletedNotes}) const;

    [[nodiscard]] QFuture<quint32> resourceCountPerNoteLocalId(
        QString noteLocalId) const;

    [[nodiscard]] QFuture<void> putResource(qevercloud::Resource resource);

    [[nodiscard]] QFuture<qevercloud::Resource> findResourceByLocalId(
        QString resourceLocalId, FetchResourceOptions options = {}) const;

    [[nodiscard]] QFuture<qevercloud::Resource> findResourceByGuid(
        qevercloud::Guid resourceGuid, FetchResourceOptions options = {}) const;

    [[nodiscard]] QFuture<void> expungeResourceByLocalId(
        QString resourceLocalId);

    [[nodiscard]] QFuture<void> expungeResourceByGuid(QString resourceGuid);

private:
    [[nodiscard]] std::optional<quint32> resourceCountImpl(
        NoteCountOptions noteCountOptions, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<quint32> resourceCountPerNoteLocalIdImpl(
        const QString & noteLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Resource> findResourceByLocalIdImpl(
        const QString & localId, FetchResourceOptions options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Resource> findResourceByGuidImpl(
        const qevercloud::Guid & guid, FetchResourceOptions options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] bool fillResourceData(
        qevercloud::Resource & resource, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool findResourceAttributesApplicationDataKeysOnlyByLocalId(
        const QString & localId, qevercloud::ResourceAttributes & attributes,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] bool findResourceAttributesApplicationDataFullMapByLocalId(
        const QString & localId, qevercloud::ResourceAttributes & attributes,
        QSqlDatabase & database, ErrorString & errorDescription) const;

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
    mutable QReadWriteLock m_resourceDataFilesLock;
};

} // namespace quentier::local_storage::sql
