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

#include "utils/Common.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/Tag.h>
#include <quentier/utility/StringUtils.h>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class TagsHandler final :
    public std::enable_shared_from_this<TagsHandler>
{
public:
    explicit TagsHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread);

    [[nodiscard]] QFuture<quint32> tagCount() const;
    [[nodiscard]] QFuture<void> putTag(qevercloud::Tag tag);

    [[nodiscard]] QFuture<qevercloud::Tag> findTagByLocalId(
        QString tagLocalId) const;

    [[nodiscard]] QFuture<qevercloud::Tag> findTagByGuid(
        qevercloud::Guid tagGuid) const;

    [[nodiscard]] QFuture<qevercloud::Tag> findTagByName(
        QString tagName, std::optional<QString> linkedNotebookGuid = {}) const;

    template <class T>
    using ListOptions = ILocalStorage::ListOptions<T>;

    using ListTagsOrder = ILocalStorage::ListTagsOrder;
    using TagNotesRelation = ILocalStorage::TagNotesRelation;

    [[nodiscard]] QFuture<QList<qevercloud::Tag>> listTags(
        ListOptions<ListTagsOrder> options = {}) const;

    [[nodiscard]] QFuture<QList<qevercloud::Tag>> listTagsPerNoteLocalId(
        QString noteLocalId, ListOptions<ListTagsOrder> options = {}) const;

    [[nodiscard]] QFuture<void> expungeTagByLocalId(QString tagLocalId);
    [[nodiscard]] QFuture<void> expungeTagByGuid(qevercloud::Guid tagGuid);

    [[nodiscard]] QFuture<void> expungeTagByName(
        QString name, std::optional<QString> linkedNotebookGuid = {});

private:
    [[nodiscard]] std::optional<quint32> tagCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Tag> findTagByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Tag> findTagByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::Tag> findTagByNameImpl(
        const QString & name, const std::optional<QString> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] QStringList listChildTagLocalIds(
        const QString & tagLocalId, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    struct ExpungeTagResult
    {
        bool status = false;
        QString expungedTagLocalId;
        QStringList expungedChildTagLocalIds;
    };

    [[nodiscard]] ExpungeTagResult expungeTagByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription,
        std::optional<Transaction> transaction = std::nullopt,
        utils::TransactionOption transactionOption =
            utils::TransactionOption::UseSeparateTransaction);

    [[nodiscard]] ExpungeTagResult expungeTagByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] ExpungeTagResult expungeTagByNameImpl(
        const QString & name, const std::optional<QString> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::Tag> listTagsImpl(
        const ListOptions<ListTagsOrder> & options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Tag> listTagsPerNoteLocalIdImpl(
        const QString & noteLocalId, const ListOptions<ListTagsOrder> & options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    QThreadPtr m_writerThread;

    StringUtils m_stringUtils;
};

} // namespace quentier::local_storage::sql
