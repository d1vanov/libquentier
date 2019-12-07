/*
 * Copyright 2016-2019 Dmitry Ivanov
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
#include <quentier/utility/Macros.h>
#include <QString>
#include <QSharedDataPointer>

#include <qt5qevercloud/QEverCloud.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(AccountData)

/**
 * @brief The Account class encapsulates some details about the account: its name,
 * whether it is local or synchronized to Evernote and for the latter case -
 * some additional details like upload limit etc.
 */
class QUENTIER_EXPORT Account: public Printable
{
public:
    struct Type
    {
        enum type
        {
            Local = 0,
            Evernote
        };
    };

    struct EvernoteAccountType
    {
        enum type
        {
            Free = 0,
            Plus,
            Premium,
            Business
        };
    };

public:
    explicit Account();
    explicit Account(const QString & name, const Type::type type,
                     const qevercloud::UserID userId = -1,
                     const EvernoteAccountType::type evernoteAccountType =
                     EvernoteAccountType::Free,
                     const QString & evernoteHost = QString(),
                     const QString & shardId = QString());
    Account(const Account & other);
    Account & operator=(const Account & other);
    virtual ~Account();

    bool operator==(const Account & other) const;
    bool operator!=(const Account & other) const;

    /**
     * @return      True if either the account is local but the name is empty
     *              or if the account is Evernote but user id is negative;
     *              in all other cases return false
     */
    bool isEmpty() const;

    /**
     * @return      Username for either local or Evernote account
     */
    QString name() const;

    /**
     * @brief setName sets the username to the account
     */
    void setName(const QString & name);

    /**
     * @return      Printable user's name that is not used to uniquely identify
     *              the account, so this name may repeat across different local
     *              and Evernote accounts
     */
    QString displayName() const;

    /**
     * Set the printable name of the account
     */
    void setDisplayName(const QString & displayName);

    /**
     * @return      The type of the account: either local of Evernote
     */
    Type::type type() const;

    /**
     * @return      User id for Evernote accounts, -1 for local accounts
     *              (as the concept of user id is not defined for local accounts)
     */
    qevercloud::UserID id() const;

    /**
     * @return      The type of the Evernote account; if applied to free account,
     *              returns "Free"
     */
    EvernoteAccountType::type evernoteAccountType() const;

    /**
     * @return      The Evernote server host with which the account is associated
     */
    QString evernoteHost() const;

    /**
     * @return      Shard id for Evernote accounts, empty string for local accounts
     *              (as the concept of shard id is not defined for local accounts)
     */
    QString shardId() const;

    void setEvernoteAccountType(const EvernoteAccountType::type evernoteAccountType);
    void setEvernoteHost(const QString & evernoteHost);
    void setShardId(const QString & shardId);

    qint32 mailLimitDaily() const;
    qint64 noteSizeMax() const;
    qint64 resourceSizeMax() const;
    qint32 linkedNotebookMax() const;
    qint32 noteCountMax() const;
    qint32 notebookCountMax() const;
    qint32 tagCountMax() const;
    qint32 noteTagCountMax() const;
    qint32 savedSearchCountMax() const;
    qint32 noteResourceCountMax() const;
    void setEvernoteAccountLimits(const qevercloud::AccountLimits & limits);

    virtual QTextStream & print(QTextStream & strm) const override;

private:
    QSharedDataPointer<AccountData> d;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_ACCOUNT_H
