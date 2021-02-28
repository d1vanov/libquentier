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

#include <quentier/types/Account.h>

#include "data/AccountData.h"

namespace quentier {

Account::Account() : d(new AccountData) {}

Account::Account(
    QString name, const Type type, const qevercloud::UserID userId,
    const EvernoteAccountType evernoteAccountType, QString evernoteHost,
    QString shardId) :
    d(new AccountData)
{
    d->m_name = std::move(name);
    d->m_accountType = type;
    d->m_userId = userId;
    d->m_evernoteHost = std::move(evernoteHost);
    d->m_shardId = std::move(shardId);

    d->switchEvernoteAccountType(evernoteAccountType);
}

Account::Account(const Account & other) : Printable(), d(other.d) {}

Account & Account::operator=(const Account & other)
{
    if (this != &other) {
        d = other.d;
    }

    return *this;
}

Account::~Account() {}

bool Account::operator==(const Account & other) const
{
    if (d == other.d) {
        return true;
    }

    // NOTE: display name intentionally does not take part in the comparison
    if ((d->m_name == other.d->m_name) &&
        (d->m_accountType == other.d->m_accountType) &&
        (d->m_evernoteAccountType == other.d->m_evernoteAccountType) &&
        (d->m_userId == other.d->m_userId) &&
        (d->m_evernoteHost == other.d->m_evernoteHost) &&
        (d->m_shardId == other.d->m_shardId) &&
        (d->m_mailLimitDaily == other.d->m_mailLimitDaily) &&
        (d->m_noteSizeMax == other.d->m_noteSizeMax) &&
        (d->m_resourceSizeMax == other.d->m_resourceSizeMax) &&
        (d->m_linkedNotebookMax == other.d->m_linkedNotebookMax) &&
        (d->m_noteCountMax == other.d->m_noteCountMax) &&
        (d->m_notebookCountMax == other.d->m_notebookCountMax) &&
        (d->m_tagCountMax == other.d->m_tagCountMax) &&
        (d->m_noteTagCountMax == other.d->m_noteTagCountMax) &&
        (d->m_savedSearchCountMax == other.d->m_savedSearchCountMax) &&
        (d->m_noteResourceCountMax == other.d->m_noteResourceCountMax))
    {
        return true;
    }

    return false;
}

bool Account::operator!=(const Account & other) const
{
    return !(operator==(other));
}

bool Account::isEmpty() const
{
    if (d->m_accountType == Account::Type::Local) {
        return d->m_name.isEmpty();
    }
    else {
        return (d->m_userId < 0);
    }
}

void Account::setEvernoteAccountType(
    const EvernoteAccountType evernoteAccountType)
{
    d->switchEvernoteAccountType(evernoteAccountType);
}

void Account::setEvernoteHost(QString evernoteHost)
{
    d->m_evernoteHost = std::move(evernoteHost);
}

void Account::setShardId(QString shardId)
{
    d->m_shardId = std::move(shardId);
}

void Account::setEvernoteAccountLimits(const qevercloud::AccountLimits & limits)
{
    d->setEvernoteAccountLimits(limits);
}

QString Account::name() const
{
    return d->m_name;
}

void Account::setName(QString name)
{
    d->m_name = std::move(name);
}

QString Account::displayName() const
{
    return d->m_displayName;
}

void Account::setDisplayName(QString displayName)
{
    d->m_displayName = std::move(displayName);
}

qevercloud::UserID Account::id() const
{
    return d->m_userId;
}

Account::Type Account::type() const
{
    return d->m_accountType;
}

Account::EvernoteAccountType Account::evernoteAccountType() const
{
    return d->m_evernoteAccountType;
}

QString Account::evernoteHost() const
{
    return d->m_evernoteHost;
}

QString Account::shardId() const
{
    return d->m_shardId;
}

qint32 Account::mailLimitDaily() const
{
    return d->m_mailLimitDaily;
}

qint64 Account::noteSizeMax() const
{
    return d->m_noteSizeMax;
}

qint64 Account::resourceSizeMax() const
{
    return d->m_resourceSizeMax;
}

qint32 Account::linkedNotebookMax() const
{
    return d->m_linkedNotebookMax;
}

qint32 Account::noteCountMax() const
{
    return d->m_noteCountMax;
}

qint32 Account::notebookCountMax() const
{
    return d->m_notebookCountMax;
}

qint32 Account::tagCountMax() const
{
    return d->m_tagCountMax;
}

qint32 Account::noteTagCountMax() const
{
    return d->m_noteTagCountMax;
}

qint32 Account::savedSearchCountMax() const
{
    return d->m_savedSearchCountMax;
}

qint32 Account::noteResourceCountMax() const
{
    return d->m_noteResourceCountMax;
}

QTextStream & Account::print(QTextStream & strm) const
{
    strm << "Account: {\n";
    strm << "    name = " << d->m_name << ";\n";
    strm << "    display name = " << d->m_displayName << ";\n";
    strm << "    id = " << d->m_userId << ";\n";

    strm << "    type = ";
    switch (d->m_accountType) {
    case Account::Type::Local:
        strm << "Local";
        break;
    case Account::Type::Evernote:
        strm << "Evernote";
        break;
    default:
        strm << "Unknown";
        break;
    }
    strm << ";\n";

    strm << "    Evernote account type = ";
    switch (d->m_evernoteAccountType) {
    case Account::EvernoteAccountType::Free:
        strm << "Free";
        break;
    case Account::EvernoteAccountType::Plus:
        strm << "Plus";
        break;
    case Account::EvernoteAccountType::Premium:
        strm << "Premium";
        break;
    case Account::EvernoteAccountType::Business:
        strm << "Business";
        break;
    default:
        strm << "Unknown";
        break;
    }
    strm << ";\n";

    strm << "    Evernote host = " << d->m_evernoteHost << ";\n";
    strm << "    shard id = " << d->m_shardId << ";\n";
    strm << "    mail limit daily = " << d->m_mailLimitDaily << ";\n";
    strm << "    note size max = " << d->m_noteSizeMax << ";\n";
    strm << "    resource size max = " << d->m_resourceSizeMax << ";\n";
    strm << "    linked notebook max = " << d->m_linkedNotebookMax << ";\n";
    strm << "    note count max = " << d->m_noteCountMax << ";\n";
    strm << "    notebook count max = " << d->m_notebookCountMax << ";\n";
    strm << "    tag count max = " << d->m_tagCountMax << ";\n";
    strm << "    note tag count max = " << d->m_noteTagCountMax << ";\n";
    strm << "    saved search count max = " << d->m_savedSearchCountMax
         << ";\n";
    strm << "    note resource count max = " << d->m_noteResourceCountMax
         << ";\n";
    strm << "};\n";

    return strm;
}

template <typename T>
void printAccountType(T & t, const Account::Type type)
{
    switch (type) {
    case Account::Type::Local:
        t << "Local";
        break;
    case Account::Type::Evernote:
        t << "Evernote";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(type) << ")";
        break;
    }
}

QTextStream & operator<<(QTextStream & strm, const Account::Type type)
{
    printAccountType(strm, type);
    return strm;
}

QDebug & operator<<(QDebug & dbg, const Account::Type type)
{
    printAccountType(dbg, type);
    return dbg;
}

template <typename T>
void printEvernoteAccountType(T & t, const Account::EvernoteAccountType type)
{
    switch (type) {
    case Account::EvernoteAccountType::Free:
        t << "Free";
        break;
    case Account::EvernoteAccountType::Plus:
        t << "Plus";
        break;
    case Account::EvernoteAccountType::Premium:
        t << "Premium";
        break;
    case Account::EvernoteAccountType::Business:
        t << "Business";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(type) << ")";
        break;
    }
}

QTextStream & operator<<(
    QTextStream & strm, const Account::EvernoteAccountType type)
{
    printEvernoteAccountType(strm, type);
    return strm;
}

QDebug & operator<<(QDebug & dbg, const Account::EvernoteAccountType type)
{
    printEvernoteAccountType(dbg, type);
    return dbg;
}

} // namespace quentier
