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

#include "ITagsHandler.h"
#include "utils/Common.h"

#include <quentier/utility/StringUtils.h>

#include <memory>

namespace quentier::local_storage::sql {

class TagsHandler final :
    public ITagsHandler,
    public std::enable_shared_from_this<TagsHandler>
{
public:
    explicit TagsHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread);

    [[nodiscard]] QFuture<quint32> tagCount() const override;
    [[nodiscard]] QFuture<void> putTag(qevercloud::Tag tag) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Tag>> findTagByLocalId(
        QString tagLocalId) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Tag>> findTagByGuid(
        qevercloud::Guid tagGuid) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Tag>> findTagByName(
        QString tagName,
        std::optional<qevercloud::Guid> linkedNotebookGuid =
            std::nullopt) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Tag>> listTags(
        ListTagsOptions options = {}) const override;

    [[nodiscard]] QFuture<QList<qevercloud::Tag>> listTagsPerNoteLocalId(
        QString noteLocalId,
        ListTagsOptions options = {}) const override;

    [[nodiscard]] QFuture<void> expungeTagByLocalId(
        QString tagLocalId) override;

    [[nodiscard]] QFuture<void> expungeTagByGuid(
        qevercloud::Guid tagGuid) override;

    [[nodiscard]] QFuture<void> expungeTagByName(
        QString name,
        std::optional<qevercloud::Guid> linkedNotebookGuid =
            std::nullopt) override;

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
        const QString & name,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
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
        const QString & name,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        QSqlDatabase & database, ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::Tag> listTagsImpl(
        const ListTagsOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] QList<qevercloud::Tag> listTagsPerNoteLocalIdImpl(
        const QString & noteLocalId, const ListTagsOptions & options,
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
