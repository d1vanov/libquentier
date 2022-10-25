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

#include "INotebooksHandler.h"
#include "Transaction.h"

#include <quentier/threading/Fwd.h>

#include <QDir>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class NotebooksHandler final :
    public INotebooksHandler,
    public std::enable_shared_from_this<NotebooksHandler>
{
public:
    explicit NotebooksHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, threading::QThreadPtr writerThread,
        const QString & localStorageDirPath,
        QReadWriteLockPtr resourceDataFilesLock);

    [[nodiscard]] QFuture<quint32> notebookCount() const override;
    [[nodiscard]] QFuture<void> putNotebook(
        qevercloud::Notebook notebook) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByLocalId(QString localId) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByGuid(qevercloud::Guid guid) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByName(
            QString name,
            std::optional<qevercloud::Guid> linkedNotebookGuid =
                std::nullopt) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findDefaultNotebook() const override;

    [[nodiscard]] QFuture<void> expungeNotebookByLocalId(
        QString localId) override;

    [[nodiscard]] QFuture<void> expungeNotebookByGuid(
        qevercloud::Guid guid) override;

    [[nodiscard]] QFuture<void> expungeNotebookByName(
        QString name,
        std::optional<qevercloud::Guid> linkedNotebookGuid =
            std::nullopt) override;

    [[nodiscard]] QFuture<QSet<qevercloud::Guid>> listNotebookGuids(
        ListGuidsFilters filters,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Notebook>> listNotebooks(
        ListNotebooksOptions options) const override;

    [[nodiscard]] QFuture<QList<qevercloud::SharedNotebook>>
        listSharedNotebooks(qevercloud::Guid notebookGuid = {}) const override;

private:
    [[nodiscard]] std::optional<quint32> notebookCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebookByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebookByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebookByNameImpl(
        const QString & name,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findDefaultNotebookImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> fillSharedNotebooks(
        qevercloud::Notebook & notebook, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeNotebookByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription,
        std::optional<Transaction> transaction = std::nullopt);

    [[nodiscard]] bool expungeNotebookByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool expungeNotebookByNameImpl(
        const QString & name,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] QStringList listNoteLocalIdsByNotebookLocalId(
        const QString & notebookLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Notebook> listNotebooksImpl(
        const ListNotebooksOptions & options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    threading::QThreadPtr m_writerThread;
    QDir m_localStorageDir;
    QReadWriteLockPtr m_resourceDataFilesLock;
};

} // namespace quentier::local_storage::sql
