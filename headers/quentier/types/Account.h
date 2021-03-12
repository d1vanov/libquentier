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

#ifndef LIB_QUENTIER_TYPES_ACCOUNT_H
#define LIB_QUENTIER_TYPES_ACCOUNT_H

#include <quentier/utility/Printable.h>

#include <qevercloud/QEverCloud.h>

#include <QSharedDataPointer>
#include <QString>

namespace quentier {

class AccountData;

/**
 * @brief The Account class encapsulates some details about the account: its
 * name, whether it is local or synchronized to Evernote and for the latter
 * case - some additional details like upload limit etc.
 */
class QUENTIER_EXPORT Account : public Printable
{
public:
    enum class Type
    {
        Local,
        Evernote
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, Type type);

    friend QUENTIER_EXPORT QDebug & operator<<(QDebug & dbg, Type type);

    enum class EvernoteAccountType
    {
        Free,
        Plus,
        Premium,
        Business
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, EvernoteAccountType type);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, EvernoteAccountType type);

public:
    explicit Account();

    explicit Account(
        QString name, Type type, qevercloud::UserID userId = -1,
        EvernoteAccountType evernoteAccountType =
            EvernoteAccountType::Free,
        QString evernoteHost = {}, QString shardId = {});

    Account(const Account & other);
    Account(Account && other) noexcept;

    Account & operator=(const Account & other);
    Account & operator=(Account && other) noexcept;

    ~Account() noexcept override;

    [[nodiscard]] bool operator==(const Account & other) const noexcept;
    [[nodiscard]] bool operator!=(const Account & other) const noexcept;

    /**
     * @return      True if either the account is local but the name is empty
     *              or if the account is Evernote but user id is negative;
     *              in all other cases return false
     */
    [[nodiscard]] bool isEmpty() const;

    /**
     * @return      Username for either local or Evernote account
     */
    [[nodiscard]] QString name() const;

    /**
     * @brief setName sets the username to the account
     */
    void setName(QString name);

    /**
     * @return      Printable user's name that is not used to uniquely identify
     *              the account, so this name may repeat across different local
     *              and Evernote accounts
     */
    [[nodiscard]] QString displayName() const;

    /**
     * Set the printable name of the account
     */
    void setDisplayName(QString displayName);

    /**
     * @return      The type of the account: either local of Evernote
     */
    [[nodiscard]] Type type() const;

    /**
     * @return      User id for Evernote accounts, -1 for local accounts
     *              (as the concept of user id is not defined for local
     *              accounts)
     */
    [[nodiscard]] qevercloud::UserID id() const;

    /**
     * @return      The type of the Evernote account; if applied to free
     *              account, returns "Free"
     */
    [[nodiscard]] EvernoteAccountType evernoteAccountType() const;

    /**
     * @return      The Evernote server host with which the account is
     *              associated
     */
    [[nodiscard]] QString evernoteHost() const;

    /**
     * @return      Shard id for Evernote accounts, empty string for local
     *              accounts (as the concept of shard id is not defined for
     *              local accounts)
     */
    [[nodiscard]] QString shardId() const;

    void setEvernoteAccountType(EvernoteAccountType evernoteAccountType);
    void setEvernoteHost(QString evernoteHost);
    void setShardId(QString shardId);

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

    void setEvernoteAccountLimits(const qevercloud::AccountLimits & limits);

    QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<AccountData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_ACCOUNT_H
