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

#include <qevercloud/types/Notebook.h>

#include <QDir>
#include <QFuture>
#include <QtGlobal>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class NotebooksHandler final :
    public std::enable_shared_from_this<NotebooksHandler>
{
public:
    explicit NotebooksHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        QThreadPtr writerThread, const QString & localStorageDirPath);

    [[nodiscard]] QFuture<quint32> notebookCount() const;
    [[nodiscard]] QFuture<void> putNotebook(qevercloud::Notebook notebook);

    [[nodiscard]] QFuture<qevercloud::Notebook> findNotebookByLocalId(
        QString localId) const;

    [[nodiscard]] QFuture<qevercloud::Notebook> findNotebookByGuid(
        qevercloud::Guid guid) const;

    [[nodiscard]] QFuture<qevercloud::Notebook> findNotebookByName(
        QString name, std::optional<QString> linkedNotebookGuid = {}) const;

    [[nodiscard]] QFuture<qevercloud::Notebook> findDefaultNotebook() const;

    [[nodiscard]] QFuture<void> expungeNotebookByLocalId(QString localId);
    [[nodiscard]] QFuture<void> expungeNotebookByGuid(qevercloud::Guid guid);

    [[nodiscard]] QFuture<void> expungeNotebookByName(
        QString name, std::optional<QString> linkedNotebookGuid = {});

    template <class T>
    using ListOptions = ILocalStorage::ListOptions<T>;

    using ListNotebooksOrder = ILocalStorage::ListNotebooksOrder;

    [[nodiscard]] QFuture<QList<qevercloud::Notebook>> listNotebooks(
        ListOptions<ListNotebooksOrder> options,
        std::optional<QString> linkedNotebookGuid) const;

    [[nodiscard]] QFuture<QList<qevercloud::SharedNotebook>>
        listSharedNotebooks(qevercloud::Guid notebookGuid = {}) const;

private:
    [[nodiscard]] std::optional<quint32> notebookCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] bool putNotebookImpl(
        qevercloud::Notebook notebook, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool putCommonNotebookData(
        const qevercloud::Notebook & notebook, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool putNotebookRestrictions(
        const QString & localId,
        const qevercloud::NotebookRestrictions & notebookRestrictions,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] bool putSharedNotebook(
        const qevercloud::SharedNotebook & sharedNotebook,
        int indexInNotebook, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool removeNotebookRestrictions(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool removeSharedNotebooks(
        const QString & notebookGuid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QString notebookLocalId(
        const qevercloud::Notebook & notebook, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QString notebookLocalIdByGuid(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QString notebookLocalIdByName(
        const QString & name, const std::optional<QString> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebookByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebookByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findNotebookByNameImpl(
        const QString & name, const std::optional<QString> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> findDefaultNotebookImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Notebook> fillSharedNotebooks(
        qevercloud::Notebook & notebook, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeNotebookByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool expungeNotebookByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] bool expungeNotebookByNameImpl(
        const QString & name, const std::optional<QString> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] QStringList listNoteLocalIdsByNotebookLocalId(
        const QString & notebookLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Notebook> listNotebooksImpl(
        const ListOptions<ListNotebooksOrder> & options,
        const std::optional<QString> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::SharedNotebook> listSharedNotebooksImpl(
        const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    QThreadPtr m_writerThread;
    QDir m_localStorageDir;
};

} // namespace quentier::local_storage::sql
