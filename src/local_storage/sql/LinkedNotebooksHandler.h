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

#include "ILinkedNotebooksHandler.h"

#include <quentier/threading/Fwd.h>

#include <QDir>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class LinkedNotebooksHandler final :
    public ILinkedNotebooksHandler,
    public std::enable_shared_from_this<LinkedNotebooksHandler>
{
public:
    explicit LinkedNotebooksHandler(
        ConnectionPoolPtr connectionPool, threading::QThreadPoolPtr threadPool,
        Notifier * notifier, threading::QThreadPtr writerThread,
        const QString & localStorageDirPath);

    [[nodiscard]] QFuture<quint32> linkedNotebookCount() const override;

    [[nodiscard]] QFuture<void> putLinkedNotebook(
        qevercloud::LinkedNotebook linkedNotebook) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByGuid(qevercloud::Guid guid) const override;

    [[nodiscard]] QFuture<void> expungeLinkedNotebookByGuid(
        qevercloud::Guid guid) override;

    [[nodiscard]] QFuture<QList<qevercloud::LinkedNotebook>>
        listLinkedNotebooks(
            ListLinkedNotebooksOptions options = {}) const override;

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
        const ListLinkedNotebooksOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    const ConnectionPoolPtr m_connectionPool;
    const threading::QThreadPoolPtr m_threadPool;
    const threading::QThreadPtr m_writerThread;
    const QDir m_localStorageDir;
    Notifier * m_notifier;
};

} // namespace quentier::local_storage::sql
