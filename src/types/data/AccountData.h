/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TYPES_DATA_ACCOUNT_DATA_H
#define LIB_QUENTIER_TYPES_DATA_ACCOUNT_DATA_H

#include <quentier/types/Account.h>

#include <QSharedData>

#include <limits>

namespace quentier {

class Q_DECL_HIDDEN AccountData final : public QSharedData
{
public:
    explicit AccountData();

    AccountData(const AccountData & other) = default;
    AccountData(AccountData && other) = default;

    ~AccountData() = default;

    AccountData & operator=(const AccountData & other) = delete;
    AccountData & operator=(AccountData && other) = delete;

    void switchEvernoteAccountType(
        Account::EvernoteAccountType evernoteAccountType);

    void setEvernoteAccountLimits(const qevercloud::AccountLimits & limits);

    [[nodiscard]] qint32 mailLimitDaily() const;
    [[nodiscard]] qint64 noteSizeMax() const;
    [[nodiscard]] qint64 resourceSizeMax() const;
    [[nodiscard]] qint32 linkedNotebookMax() const;
    [[nodiscard]] qint32 noteCountMax() const;
    [[nodiscard]] qint32 notebookCountMax() const;
    [[nodiscard]] qint32 tagCountMax() const;
    [[nodiscard]] qint32 noteTagCountMax() const;
    [[nodiscard]] qint32 savedSearchCountMax() const;
    [[nodiscard]] qint32 noteResourceCountMax() const;

public:
    QString m_name;
    QString m_displayName;

    Account::Type m_accountType = Account::Type::Local;

    Account::EvernoteAccountType m_evernoteAccountType =
        Account::EvernoteAccountType::Free;

    qevercloud::UserID m_userId = -1;

    QString m_evernoteHost;
    QString m_shardId;

    qint32 m_mailLimitDaily = std::numeric_limits<qint32>::max();
    qint64 m_noteSizeMax = std::numeric_limits<qint64>::max();
    qint64 m_resourceSizeMax = std::numeric_limits<qint64>::max();
    qint32 m_linkedNotebookMax = std::numeric_limits<qint32>::max();
    qint32 m_noteCountMax = std::numeric_limits<qint32>::max();
    qint32 m_notebookCountMax = std::numeric_limits<qint32>::max();
    qint32 m_tagCountMax = std::numeric_limits<qint32>::max();
    qint32 m_noteTagCountMax = std::numeric_limits<qint32>::max();
    qint32 m_savedSearchCountMax = std::numeric_limits<qint32>::max();
    qint32 m_noteResourceCountMax = std::numeric_limits<qint32>::max();
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_ACCOUNT_DATA_H
