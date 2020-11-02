/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedDataPointer>
#include <QString>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(AccountData)

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
        Local = 0,
        Evernote
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const Type type);

    friend QUENTIER_EXPORT QDebug & operator<<(QDebug & dbg, const Type type);

    enum class EvernoteAccountType
    {
        Free = 0,
        Plus,
        Premium,
        Business
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const EvernoteAccountType type);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const EvernoteAccountType type);

public:
    explicit Account();

    explicit Account(
        QString name, const Type type, const qevercloud::UserID userId = -1,
        const EvernoteAccountType evernoteAccountType =
            EvernoteAccountType::Free,
        QString evernoteHost = {}, QString shardId = {});

    Account(const Account & other);
    Account & operator=(const Account & other);
    virtual ~Account() override;

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
    void setName(QString name);

    /**
     * @return      Printable user's name that is not used to uniquely identify
     *              the account, so this name may repeat across different local
     *              and Evernote accounts
     */
    QString displayName() const;

    /**
     * Set the printable name of the account
     */
    void setDisplayName(QString displayName);

    /**
     * @return      The type of the account: either local of Evernote
     */
    Type type() const;

    /**
     * @return      User id for Evernote accounts, -1 for local accounts
     *              (as the concept of user id is not defined for local
     *              accounts)
     */
    qevercloud::UserID id() const;

    /**
     * @return      The type of the Evernote account; if applied to free
     *              account, returns "Free"
     */
    EvernoteAccountType evernoteAccountType() const;

    /**
     * @return      The Evernote server host with which the account is
     *              associated
     */
    QString evernoteHost() const;

    /**
     * @return      Shard id for Evernote accounts, empty string for local
     *              accounts (as the concept of shard id is not defined for
     *              local accounts)
     */
    QString shardId() const;

    void setEvernoteAccountType(const EvernoteAccountType evernoteAccountType);
    void setEvernoteHost(QString evernoteHost);
    void setShardId(QString shardId);

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
