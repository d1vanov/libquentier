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

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/LinkedNotebook.h>

#include <QDir>
#include <QFuture>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class LinkedNotebooksHandler final :
    public std::enable_shared_from_this<LinkedNotebooksHandler>
{
public:
    explicit LinkedNotebooksHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread,
        const QString & localStorageDirPath);

    [[nodiscard]] QFuture<quint32> linkedNotebookCount() const;

    [[nodiscard]] QFuture<void> putLinkedNotebook(
        qevercloud::LinkedNotebook linkedNotebook);

    [[nodiscard]] QFuture<qevercloud::LinkedNotebook>
        findLinkedNotebookByGuid(qevercloud::Guid guid) const;

    [[nodiscard]] QFuture<void> expungeLinkedNotebookByGuid(
        qevercloud::Guid guid);

    template <class T>
    using ListOptions = ILocalStorage::ListOptions<T>;

    using ListLinkedNotebooksOrder = ILocalStorage::ListLinkedNotebooksOrder;

    [[nodiscard]] QFuture<QList<qevercloud::LinkedNotebook>>
        listLinkedNotebooks(
            ListOptions<ListLinkedNotebooksOrder> options = {}) const;

private:
    [[nodiscard]] std::optional<quint32> linkedNotebookCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::LinkedNotebook>
        findLinkedNotebookByGuid(
            const qevercloud::Guid & guid, QSqlDatabase & database,
            ErrorString & errorDescription) const;

    [[nodiscard]] QStringList listNoteLocalIdsByLinkedNotebookGuid(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeLinkedNotebookByGuidImpl(
        const qevercloud::Guid & linkedNotebookGuid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::LinkedNotebook> listLinkedNotebooksImpl(
        const ListOptions<ListLinkedNotebooksOrder> & options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    QThreadPtr m_writerThread;
    QDir m_localStorageDir;
};

} // namespace quentier::local_storage::sql
