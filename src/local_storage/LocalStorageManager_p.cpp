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

#include "LocalStorageManager_p.h"

#include "LocalStoragePatchManager.h"
#include "LocalStorageShared.h"
#include "Transaction.h"
#include "TypeChecks.h"

#include <quentier/exception/DatabaseLockFailedException.h>
#include <quentier/exception/DatabaseLockedException.h>
#include <quentier/exception/DatabaseOpeningException.h>
#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/NoteUtils.h>
#include <quentier/types/ResourceRecognitionIndices.h>
#include <quentier/types/ResourceUtils.h>
#include <quentier/utility/Checks.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/StringUtils.h>
#include <quentier/utility/SysInfo.h>
#include <quentier/utility/UidGenerator.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlRecord>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

void clearBinaryDataFromResource(qevercloud::Resource & resource)
{
    if (resource.data() && resource.data()->body()) {
        resource.mutableData()->setBody(std::nullopt);
    }

    if (resource.alternateData() && resource.alternateData()->body()) {
        resource.mutableAlternateData()->setBody(std::nullopt);
    }
}

[[nodiscard]] bool compareResourcesWithoutBinaryData(
    const qevercloud::Resource & lhs,
    const qevercloud::Resource & rhs)
{
    static const auto hasDataBody = [](const qevercloud::Resource & resource)
    {
        return resource.data() && resource.data()->body();
    };

    static const auto hasAlternateDataBody =
        [](const qevercloud::Resource & resource)
        {
            return resource.alternateData() && resource.alternateData()->body();
        };

    const bool lhsHasBinaryData = hasDataBody(lhs) || hasAlternateDataBody(lhs);
    const bool rhsHasBinaryData = hasDataBody(rhs) || hasAlternateDataBody(rhs);

    if (!lhsHasBinaryData && !rhsHasBinaryData) {
        return lhs == rhs;
    }

    if (lhsHasBinaryData && !rhsHasBinaryData) {
        qevercloud::Resource lhsCopy = lhs;
        clearBinaryDataFromResource(lhsCopy);
        return lhsCopy == rhs;
    }

    if (!lhsHasBinaryData && rhsHasBinaryData) {
        qevercloud::Resource rhsCopy = rhs;
        clearBinaryDataFromResource(rhsCopy);
        return lhs == rhsCopy;
    }

    qevercloud::Resource lhsCopy = lhs;
    clearBinaryDataFromResource(lhsCopy);

    qevercloud::Resource rhsCopy = rhs;
    clearBinaryDataFromResource(rhsCopy);

    return lhsCopy == rhsCopy;
}

[[nodiscard]] bool compareResourcesListsWithoutBinaryData(
    const QList<qevercloud::Resource> & lhs,
    const QList<qevercloud::Resource> & rhs)
{
    const auto lhsSize = lhs.size();
    if (lhsSize != rhs.size()) {
        return false;
    }

    for (int i = 0; i < lhsSize; ++i)
    {
        if (!compareResourcesWithoutBinaryData(lhs[i], rhs[i])) {
            return false;
        }
    }

    return true;
}

template <class T>
[[nodiscard]] bool checkDuplicatesByLocalId(const QList<T> & lhs)
{
    return !std::any_of(
        lhs.begin(), lhs.end(),
        [s = QSet<QString>{}] (const auto & item) mutable
        {
            const auto & localId = item.localId();
            if (s.contains(localId)) {
                return true;
            }

            Q_UNUSED(s.insert(localId))
            return false;
        });
}

[[nodiscard]] std::optional<int> sharedNotebookIndexInNotebook(
    const qevercloud::SharedNotebook & sharedNotebook) noexcept
{
    const auto it =
        sharedNotebook.localData().constFind(QStringLiteral("indexInNotebook"));
    if (it == sharedNotebook.localData().constEnd()) {
        return std::nullopt;
    }

    bool conversionResult = false;
    const int index = it.value().toInt(&conversionResult);
    if (!conversionResult) {
        return std::nullopt;
    }

    return index;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

#define QUENTIER_DATABASE_NAME "qn.storage.sqlite"

////////////////////////////////////////////////////////////////////////////////

using GetNoteOption = LocalStorageManager::GetNoteOption;
using GetNoteOptions = LocalStorageManager::GetNoteOptions;

using GetResourceOption = LocalStorageManager::GetResourceOption;
using GetResourceOptions = LocalStorageManager::GetResourceOptions;

using ListLinkedNotebooksOrder = LocalStorageManager::ListLinkedNotebooksOrder;
using ListNotebooksOrder = LocalStorageManager::ListNotebooksOrder;
using ListNotesOrder = LocalStorageManager::ListNotesOrder;
using ListSavedSearchesOrder = LocalStorageManager::ListSavedSearchesOrder;
using ListTagsOrder = LocalStorageManager::ListTagsOrder;

using ListObjectsOption = LocalStorageManager::ListObjectsOption;
using ListObjectsOptions = LocalStorageManager::ListObjectsOptions;

using NoteCountOption = LocalStorageManager::NoteCountOption;
using NoteCountOptions = LocalStorageManager::NoteCountOptions;

using OrderDirection = LocalStorageManager::OrderDirection;

using StartupOption = LocalStorageManager::StartupOption;
using StartupOptions = LocalStorageManager::StartupOptions;

using UpdateNoteOption = LocalStorageManager::UpdateNoteOption;
using UpdateNoteOptions = LocalStorageManager::UpdateNoteOptions;

////////////////////////////////////////////////////////////////////////////////

LocalStorageManagerPrivate::LocalStorageManagerPrivate(
    const Account & account, const StartupOptions options, QObject * parent) :
    QObject(parent),
    m_currentAccount(account)
{
    m_preservedAsterisk.reserve(1);
    m_preservedAsterisk.push_back(QChar::fromLatin1('*'));

    switchUser(account, options);
}

LocalStorageManagerPrivate::~LocalStorageManagerPrivate() noexcept
{
    if (m_sqlDatabase.isOpen()) {
        m_sqlDatabase.close();
    }

    unlockDatabaseFile();
}

bool LocalStorageManagerPrivate::addUser(
    const qevercloud::User & user, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add user to the local storage"));

    ErrorString error;
    if (!checkUser(user, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid user: " << user << "\nError: " << error);
        return false;
    }

    const QString userId = QString::number(*user.id());
    if (rowExists(QStringLiteral("Users"), QStringLiteral("id"), userId)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("user with the same id already exists"));
        errorDescription.details() = userId;
        QNWARNING("local_storage", errorDescription << ", id: " << userId);
        return false;
    }

    error.clear();
    if (!insertOrReplaceUser(user, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::updateUser(
    const qevercloud::User & user, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update user in the local storage"));

    ErrorString error;
    if (!checkUser(user, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid user: " << user << "\nError: " << error);
        return false;
    }

    const QString userId = QString::number(*user.id());
    if (!rowExists(QStringLiteral("Users"), QStringLiteral("id"), userId)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("user with the specified id was not found"));
        errorDescription.details() = userId;
        QNWARNING("local_storage", errorDescription << ", id: " << userId);
        return false;
    }

    error.clear();
    if (!insertOrReplaceUser(user, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::findUser(
    qevercloud::User & user, ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::findUser: user = " << user);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find user in the local storage"));

    if (!user.id()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("user id is not set"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString userId = QString::number(*user.id());
    QNDEBUG("local_storage", "Looking for user with id = " << userId);

    const QString queryString = QStringLiteral(
        "SELECT * FROM Users LEFT OUTER JOIN UserAttributes "
        "ON Users.id = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions "
        "ON Users.id = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses "
        "ON Users.id = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON Users.id = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON Users.id = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON Users.id = BusinessUserInfo.id "
        "WHERE Users.id = :id");

    QSqlQuery query(m_sqlDatabase);
    bool res = query.prepare(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    quint32 counter = 0;
    while (query.next()) {
        if (!fillUserFromSqlRecord(query.record(), user, errorDescription)) {
            return false;
        }

        ++counter;
    }

    if (!counter) {
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::deleteUser(
    const qevercloud::User & user, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't mark user as deleted in the local storage"));

    if (!user.deleted()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("deletion timestamp is not set"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (!user.id()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("user id is not set"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    bool res = checkAndPrepareDeleteUserQuery();
    QSqlQuery & query = m_deleteUserQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    query.bindValue(QStringLiteral(":userDeletionTimestamp"), *user.deleted());

    query.bindValue(
        QStringLiteral(":userIsLocal"), (user.isLocalOnly() ? 1 : 0));
    query.bindValue(QStringLiteral(":id"), *user.id());

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::expungeUser(
    const qevercloud::User & user, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge user from the local storage"));

    if (!user.id()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("user id is not set"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString queryString =
        QStringLiteral("DELETE FROM Users WHERE id=:id");
    QSqlQuery query(m_sqlDatabase);
    bool res = query.prepare(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    const auto id = *user.id();
    QString userId = QString::number(id);
    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

#define SET_ERROR()                                                            \
    errorDescription.base() = errorPrefix.base();                              \
    errorDescription.details() = query.lastError().text();                     \
    QNERROR(                                                                   \
        "local_storage",                                                       \
        errorDescription << ", last query: " << lastExecutedQuery(query))

#define SET_INT_CONVERSION_ERROR()                                             \
    errorDescription.base() = errorPrefix.base();                              \
    errorDescription.appendBase(QT_TRANSLATE_NOOP(                             \
        "LocalStorageManagerPrivate",                                          \
        "can't convert the fetched data to int"));                             \
    QNERROR("local_storage", errorDescription << ": " << query.value(0))

#define SET_NO_DATA_FOUND()                                                    \
    errorDescription.base() = errorPrefix.base();                              \
    errorDescription.appendBase(                                               \
        QT_TRANSLATE_NOOP("LocalStorageManagerPrivate", "no data found"));     \
    QNDEBUG("local_storage", errorDescription)

int LocalStorageManagerPrivate::notebookCount(
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of notebooks in the local storage"));

    bool res = checkAndPrepareNotebookCountQuery();
    QSqlQuery & query = m_getNotebookCountQuery;
    if (!res) {
        SET_ERROR();
        return -1;
    }

    res = query.exec();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG("local_storage", "Found no notebooks in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);

    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

void LocalStorageManagerPrivate::switchUser(
    const Account & account, const StartupOptions options)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::switchUser: "
            << account.name() << ", clear = "
            << ((options & StartupOption::ClearDatabase) ? "true" : "false")
            << ", override lock = "
            << ((options & StartupOption::OverrideLock) ? "true" : "false"));

    QNTRACE("local_storage", "Account: " << account);

    if (!m_databaseFilePath.isEmpty() &&
        (m_currentAccount.type() == account.type()) &&
        (m_currentAccount.name() == account.name()) &&
        (m_currentAccount.id() == account.id()))
    {
        QNDEBUG(
            "local_storage",
            "The same account and it has already been initialized once");
        return;
    }

    // Unlocking the previous database file, if any
    unlockDatabaseFile();

    if (m_pLocalStoragePatchManager) {
        m_pLocalStoragePatchManager->deleteLater();
        m_pLocalStoragePatchManager = nullptr;
    }

    m_currentAccount = account;

    const QString sqlDriverName = QStringLiteral("QSQLITE");
    const bool isSqlDriverAvailable =
        QSqlDatabase::isDriverAvailable(sqlDriverName);

    if (!isSqlDriverAvailable) {
        ErrorString error(QT_TR_NOOP("SQLite driver is not available"));
        error.details() = QStringLiteral("Available SQL drivers: ");

        const QStringList drivers = QSqlDatabase::drivers();
        for (const auto & driver: qAsConst(drivers)) {
            error.details() +=
                QStringLiteral("{") + driver + QStringLiteral("} ");
        }

        throw DatabaseRequestException(error);
    }

    m_sqlDatabase.close();

    QString sqlDatabaseConnectionName =
        QStringLiteral("quentier_sqlite_connection");

    if (!QSqlDatabase::contains(sqlDatabaseConnectionName)) {
        m_sqlDatabase =
            QSqlDatabase::addDatabase(sqlDriverName, sqlDatabaseConnectionName);
    }
    else {
        m_sqlDatabase = QSqlDatabase::database(sqlDatabaseConnectionName);
    }

    const QString accountName = account.name();
    if (Q_UNLIKELY(accountName.isEmpty())) {
        ErrorString error(QT_TR_NOOP(
            "Can't initialize local storage: account name is empty"));

        throw DatabaseOpeningException(error);
    }

    m_databaseFilePath = accountPersistentStoragePath(account);
    if (Q_UNLIKELY(m_databaseFilePath.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't initialize local storage: account persistent "
                       "storage path is empty"));
        throw DatabaseOpeningException(error);
    }

    m_databaseFilePath +=
        QStringLiteral("/") + QStringLiteral(QUENTIER_DATABASE_NAME);

    QNDEBUG(
        "local_storage",
        "Attempting to open or create the database file: "
            << m_databaseFilePath);

    const QFileInfo databaseFileInfo(m_databaseFilePath);

    const QDir databaseFileDir = databaseFileInfo.absoluteDir();
    if (Q_UNLIKELY(!databaseFileDir.exists())) {
        bool res = databaseFileDir.mkpath(databaseFileDir.absolutePath());
        if (!res) {
            ErrorString error(
                QT_TR_NOOP("Can't create folder for the local storage database "
                           "file"));
            throw DatabaseOpeningException(error);
        }
    }

    if (databaseFileInfo.exists()) {
        if (Q_UNLIKELY(!databaseFileInfo.isReadable())) {
            ErrorString error(
                QT_TR_NOOP("Local storage database file is not readable"));
            error.details() = m_databaseFilePath;
            throw DatabaseOpeningException(error);
        }

        if (Q_UNLIKELY(!databaseFileInfo.isWritable())) {
            ErrorString error(
                QT_TR_NOOP("Local storage database file is not writable"));
            error.details() = m_databaseFilePath;
            throw DatabaseOpeningException(error);
        }
    }
    else {
        // The file needs to exist in order to lock it
        clearDatabaseFile();
    }

    /**
     * NOTE: it appears boost::interprocess::file_lock applied to the database
     * file on Windows causes the inability to properly open the database.
     * The reason for this is not clear so for now just disable the use of
     * boost::interprocess::file_lock on Windows. It's not a major problem
     * because Windows won't let another process to open the file being worked
     * with by another process
     */
#ifndef Q_OS_WIN
    /**
     * WARNING: something strange is going on here: if no call is made to the
     * below method, boost::interprocess::file_lock occasionally and
     * sporadically thinks "there is no such file or directory"; that's what
     * its exception message says
     */
    const bool databaseFileExists = databaseFileInfo.exists();
    QNDEBUG(
        "local_storage",
        "Database file exists before locking: "
            << (databaseFileExists ? "true" : "false"));

    bool lockResult = false;

    try {
        boost::interprocess::file_lock databaseLock(
            databaseFileInfo.canonicalFilePath().toUtf8().constData());

        m_databaseFileLock.swap(databaseLock);
        lockResult = m_databaseFileLock.try_lock();
    }
    catch (boost::interprocess::interprocess_exception & exc) {
        ErrorString error(QT_TR_NOOP("Can't lock the database file"));
        error.details() = QStringLiteral("error code ");
        error.details() += QString::number(exc.get_error_code());
        error.details() += QStringLiteral("; ");
        error.details() += QString::fromUtf8(exc.what());
        throw DatabaseLockFailedException(error);
    }

    if (!lockResult) {
        if (!(options & StartupOption::OverrideLock)) {
            ErrorString error(
                QT_TR_NOOP("Local storage database file is locked"));
            error.details() = m_databaseFilePath;
            throw DatabaseLockedException(error);
        }
        else {
            QNINFO(
                "local_storage",
                "Local storage database file "
                    << m_databaseFilePath << " is locked but nobody cares");
        }
    }
#endif // Q_OS_WIN

    if (options & StartupOption::ClearDatabase) {
        QNDEBUG(
            "local_storage",
            "Cleaning up the whole database for account: " << m_currentAccount);
        clearDatabaseFile();
    }

    m_sqlDatabase.setHostName(QStringLiteral("localhost"));
    m_sqlDatabase.setUserName(accountName);
    m_sqlDatabase.setPassword(accountName);
    m_sqlDatabase.setDatabaseName(m_databaseFilePath);

    if (!m_sqlDatabase.open()) {
        QString lastErrorText = m_sqlDatabase.lastError().text();
        ErrorString error(
            QT_TR_NOOP("Can't connect to the local storage database"));
        error.details() = lastErrorText;
        throw DatabaseOpeningException(error);
    }

    QSqlQuery query(m_sqlDatabase);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        QString lastErrorText = m_sqlDatabase.lastError().text();
        ErrorString error(
            QT_TR_NOOP("Can't set foreign_keys = ON pragma for "
                       "the local storage database"));
        error.details() = lastErrorText;
        throw DatabaseRequestException(error);
    }

    SysInfo sysInfo;
    const qint64 pageSize = sysInfo.pageSize();

    const QString pageSizeQuery = QString::fromUtf8("PRAGMA page_size = %1")
                                      .arg(QString::number(pageSize));

    if (!query.exec(pageSizeQuery)) {
        const QString lastErrorText = m_sqlDatabase.lastError().text();
        ErrorString error(
            QT_TR_NOOP("Can't set page_size pragma for the local storage "
                       "database"));
        error.details() = lastErrorText;
        throw DatabaseRequestException(error);
    }

    const QString writeAheadLoggingQuery =
        QStringLiteral("PRAGMA journal_mode=WAL");

    if (!query.exec(writeAheadLoggingQuery)) {
        const QString lastErrorText = m_sqlDatabase.lastError().text();
        ErrorString error(
            QT_TR_NOOP("Can't set journal_mode pragma to WAL for the local "
                       "storage database"));
        error.details() = lastErrorText;
        throw DatabaseRequestException(error);
    }

    ErrorString errorDescription;
    if (!createTables(errorDescription)) {
        ErrorString error(
            QT_TR_NOOP("Can't init tables in the local storage database"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        throw DatabaseRequestException(error);
    }

    clearCachedQueries();
}

bool LocalStorageManagerPrivate::isLocalStorageVersionTooHigh(
    ErrorString & errorDescription)
{
    const qint32 currentVersion = localStorageVersion(errorDescription);
    if (currentVersion < 0) {
        return false;
    }

    const qint32 highestSupportedVersion =
        highestSupportedLocalStorageVersion();
    return currentVersion > highestSupportedVersion;
}

bool LocalStorageManagerPrivate::localStorageRequiresUpgrade(
    ErrorString & errorDescription)
{
    const qint32 currentVersion = localStorageVersion(errorDescription);
    if (currentVersion < 0) {
        return false;
    }

    const qint32 highestSupportedVersion =
        highestSupportedLocalStorageVersion();
    return currentVersion < highestSupportedVersion;
}

QList<ILocalStoragePatchPtr>
LocalStorageManagerPrivate::requiredLocalStoragePatches()
{
    if (!m_pLocalStoragePatchManager) {
        m_pLocalStoragePatchManager = new LocalStoragePatchManager(
            m_currentAccount, *this, m_sqlDatabase, this);
    }

    return m_pLocalStoragePatchManager->patchesForCurrentVersion();
}

qint32 LocalStorageManagerPrivate::localStorageVersion(
    ErrorString & errorDescription)
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::localStorageVersion");

    const QString queryString =
        QStringLiteral("SELECT version FROM Auxiliary LIMIT 1");

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(queryString);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to execute SQL query checking whether "
                       "the database requires an upgrade"));
        errorDescription.details() = query.lastError().text();
        QNWARNING("local_storage", errorDescription);
        return -1;
    }

    if (!query.next()) {
        QNDEBUG(
            "local_storage",
            "No version was found within the local "
                << "storage database, assuming version 1");
        return 1;
    }

    const QVariant value = query.record().value(QStringLiteral("version"));
    bool conversionResult = false;
    const int version = value.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to decode the current local storage database "
                       "version"));
        QNWARNING("local_storage", errorDescription << ", value = " << value);
        return -1;
    }

    QNDEBUG("local_storage", "Version = " << version);
    return version;
}

qint32 LocalStorageManagerPrivate::highestSupportedLocalStorageVersion() const
{
    return 2;
}

int LocalStorageManagerPrivate::userCount(ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of users within the local storage"));

    bool res = checkAndPrepareUserCountQuery();
    QSqlQuery & query = m_getUserCountQuery;
    if (!res) {
        SET_ERROR();
        return -1;
    }

    res = query.exec();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG("local_storage", "Found no users in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);

    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

bool LocalStorageManagerPrivate::addNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add notebook to the local storage"));

    ErrorString error;
    if (!checkNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid notebook: " << notebook << "\nError: " << error);
        return false;
    }

    QString localId = notebook.localId();

    QString column, id;
    bool shouldCheckRowExistence = true;

    if (notebook.guid()) {
        column = QStringLiteral("guid");
        id = *notebook.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("notebook guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            ErrorString error;
            bool res = getNotebookLocalIdForGuid(id, localId, error);
            if (res || !localId.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("found existing notebook corresponding to "
                               "the added notebook by guid"));
                errorDescription.details() = id;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            localId = UidGenerator::Generate();
            notebook.setLocalId(localId);
            shouldCheckRowExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = localId;
    }

    if (shouldCheckRowExistence &&
        rowExists(QStringLiteral("Notebooks"), column, QVariant(id)))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("notebook already exists"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    error.clear();
    if (!insertOrReplaceNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::updateNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update notebook in the local storage"));

    ErrorString error;
    if (!checkNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid notebook: " << notebook << "\nError: " << error);
        return false;
    }

    QString localId = notebook.localId();

    QString column, id;
    bool shouldCheckRowExistence = true;

    if (notebook.guid()) {
        column = QStringLiteral("guid");
        id = *notebook.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("notebook guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            ErrorString error;
            if (localId.isEmpty() ||
                !getNotebookLocalIdForGuid(id, localId, error)) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            notebook.setLocalId(localId);
            shouldCheckRowExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = localId;
    }

    if (shouldCheckRowExistence &&
        !rowExists(QStringLiteral("Notebooks"), column, id))
    {
        bool foundByOtherColumn = false;

        if (notebook.guid()) {
            QNDEBUG(
                "local_storage",
                "Failed to find the notebook by guid within the local storage, "
                    << "trying to find it by local id");

            column = QStringLiteral("localUid");
            id = localId;

            foundByOtherColumn =
                rowExists(QStringLiteral("Notebooks"), column, id);
        }

        if (!foundByOtherColumn) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("notebook to be updated was not found in the local "
                           "storage"));
            errorDescription.details() = column;
            errorDescription.details() += QStringLiteral(" = ");
            errorDescription.details() += id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    error.clear();
    if (!insertOrReplaceNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::findNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::findNotebook: notebook = " << notebook);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find notebook in the local storage"));

    bool searchingByName = false;

    QString column, value;
    if (notebook.guid()) {
        column = QStringLiteral("guid");
        value = *notebook.guid();

        if (!checkGuid(value)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("notebook guid is invalid"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }
    else if (!notebook.localId().isEmpty()) {
        column = QStringLiteral("localUid");
        value = notebook.localId();
    }
    else if (notebook.name()) {
        column = QStringLiteral("notebookNameUpper");
        value = notebook.name()->toUpper();
        searchingByName = true;
    }
    else if (const auto linkedNotebookGuid = notebook.linkedNotebookGuid();
             linkedNotebookGuid && !linkedNotebookGuid->isEmpty())
    {
        column = QStringLiteral("linkedNotebookGuid");
        value = *linkedNotebookGuid;
    }
    else {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("need either guid or local id or name or linked "
                       "notebook guid as search criteria"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    value = sqlEscapeString(value);

    QString queryString =
        QString::fromUtf8(
            "SELECT * FROM Notebooks "
            "LEFT OUTER JOIN NotebookRestrictions ON "
            "Notebooks.localUid = NotebookRestrictions.localUid "
            "LEFT OUTER JOIN Users ON "
            "Notebooks.contactId = Users.id "
            "LEFT OUTER JOIN UserAttributes ON "
            "Notebooks.contactId = UserAttributes.id "
            "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
            "Notebooks.contactId = UserAttributesViewedPromotions.id "
            "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
            "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
            "LEFT OUTER JOIN Accounting ON "
            "Notebooks.contactId = Accounting.id "
            "LEFT OUTER JOIN AccountLimits ON "
            "Notebooks.contactId = AccountLimits.id "
            "LEFT OUTER JOIN BusinessUserInfo ON "
            "Notebooks.contactId = BusinessUserInfo.id "
            "WHERE (Notebooks.%1 = '%2'")
            .arg(column, value);

    if (searchingByName) {
        QString linkedNotebookGuid =
            notebook.linkedNotebookGuid().value_or(QString{});
        if (!linkedNotebookGuid.isEmpty()) {
            linkedNotebookGuid = sqlEscapeString(linkedNotebookGuid);

            queryString +=
                QString::fromUtf8(" AND Notebooks.linkedNotebookGuid = '%1')")
                    .arg(linkedNotebookGuid);
        }
        else {
            queryString +=
                QStringLiteral(" AND Notebooks.linkedNotebookGuid IS NULL)");
        }
    }
    else {
        queryString += QStringLiteral(")");
    }

    qevercloud::Notebook result;

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        return false;
    }

    ErrorString error;
    if (!fillNotebookFromSqlRecord(query.record(), result, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (result.guid())
    {
        error.clear();
        auto sharedNotebooks = listSharedNotebooksPerNotebookGuid(
            *result.guid(), error);
        if (!error.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (!sharedNotebooks.isEmpty()) {
            result.setSharedNotebooks(std::move(sharedNotebooks));
        }
    }

    notebook = result;
    return true;
}

bool LocalStorageManagerPrivate::findDefaultNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find the default notebook in the local storage"));

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(QStringLiteral(
        "SELECT * FROM Notebooks "
        "LEFT OUTER JOIN NotebookRestrictions ON "
        "Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN Users ON "
        "Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id "
        "WHERE isDefault = 1 LIMIT 1"));
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no default notebook was found"));
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    qevercloud::Notebook result;
    ErrorString error;
    if (!fillNotebookFromSqlRecord(query.record(), result, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (result.guid())
    {
        error.clear();
        auto sharedNotebooks = listSharedNotebooksPerNotebookGuid(
            *result.guid(), error);
        if (!error.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (!sharedNotebooks.isEmpty()) {
            result.setSharedNotebooks(std::move(sharedNotebooks));
        }
    }

    notebook = result;
    return true;
}

bool LocalStorageManagerPrivate::findLastUsedNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find the last used notebook in the local storage"));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(QStringLiteral(
        "SELECT * FROM Notebooks "
        "LEFT OUTER JOIN NotebookRestrictions ON "
        "Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN Users ON "
        "Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id "
        "WHERE isLastUsed = 1 LIMIT 1"));
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no last used notebook exists in the local storage"));
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    qevercloud::Notebook result;
    ErrorString error;
    if (!fillNotebookFromSqlRecord(query.record(), result, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (result.guid())
    {
        error.clear();
        auto sharedNotebooks = listSharedNotebooksPerNotebookGuid(
            *result.guid(), error);
        if (!error.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (!sharedNotebooks.isEmpty()) {
            result.setSharedNotebooks(std::move(sharedNotebooks));
        }
    }

    notebook = result;
    return true;
}

bool LocalStorageManagerPrivate::findDefaultOrLastUsedNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription) const
{
    bool res = findDefaultNotebook(notebook, errorDescription);
    if (res) {
        return true;
    }

    return findLastUsedNotebook(notebook, errorDescription);
}

QList<qevercloud::Notebook> LocalStorageManagerPrivate::listAllNotebooks(
    ErrorString & errorDescription, const std::size_t limit,
    const std::size_t offset, const ListNotebooksOrder & order,
    const OrderDirection & orderDirection,
    std::optional<QString> linkedNotebookGuid) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::listAllNotebooks");

    return listNotebooks(
        ListObjectsOption::ListAll, errorDescription, limit, offset, order,
        orderDirection, std::move(linkedNotebookGuid));
}

QList<qevercloud::Notebook> LocalStorageManagerPrivate::listNotebooks(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const std::size_t limit, const std::size_t offset,
    const ListNotebooksOrder & order, const OrderDirection & orderDirection,
    std::optional<QString> linkedNotebookGuid) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listNotebooks: flag = " << flag);

    QString linkedNotebookGuidSqlQueryCondition;
    if (linkedNotebookGuid) {
        if (linkedNotebookGuid->isEmpty()) {
            linkedNotebookGuidSqlQueryCondition =
                QStringLiteral("linkedNotebookGuid IS NULL");
        }
        else {
            linkedNotebookGuidSqlQueryCondition =
                QString::fromUtf8("linkedNotebookGuid = '%1'")
                    .arg(sqlEscapeString(*linkedNotebookGuid));
        }
    }

    return listObjects<qevercloud::Notebook, ListNotebooksOrder>(
        flag, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuidSqlQueryCondition);
}

QList<qevercloud::SharedNotebook>
LocalStorageManagerPrivate::listAllSharedNotebooks(
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage", "LocalStorageManagerPrivate::listAllSharedNotebooks");

    QList<qevercloud::SharedNotebook> sharedNotebooks;

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list all shared notebooks"));

    QSqlQuery query(m_sqlDatabase);

    const bool res =
        query.exec(QStringLiteral("SELECT * FROM SharedNotebooks"));

    if (!res) {
        errorDescription.base() = errorPrefix.base();
        QNERROR(
            "local_storage",
            errorDescription << "last error = " << query.lastError()
                             << ", last query = " << query.lastQuery());
        errorDescription.details() += query.lastError().text();
        return sharedNotebooks;
    }

    sharedNotebooks.reserve(qMax(query.size(), 0));

    while (query.next()) {
        sharedNotebooks << qevercloud::SharedNotebook();
        auto & sharedNotebook = sharedNotebooks.back();

        ErrorString error;
        if (!fillSharedNotebookFromSqlRecord(
                query.record(), sharedNotebook, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            sharedNotebooks.clear();
            return sharedNotebooks;
        }
    }

    const int numSharedNotebooks = sharedNotebooks.size();

    QNDEBUG(
        "local_storage", "found " << numSharedNotebooks << " shared notebooks");

    if (numSharedNotebooks <= 0) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no shared notebooks were found in the local storage"));
        QNDEBUG("local_storage", errorDescription);
    }

    return sharedNotebooks;
}

QList<qevercloud::SharedNotebook>
LocalStorageManagerPrivate::listSharedNotebooksPerNotebookGuid(
    const QString & notebookGuid, ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listSharedNotebooksPerNotebookGuid: "
            << "guid = " << notebookGuid);

    QList<qevercloud::SharedNotebook> sharedNotebooks;

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list shared notebooks per notebook guid"));

    QSqlQuery query(m_sqlDatabase);

    query.prepare(QStringLiteral(
        "SELECT * FROM SharedNotebooks WHERE sharedNotebookNotebookGuid=?"));

    query.addBindValue(notebookGuid);
    const bool res = query.exec();
    if (!res) {
        SET_ERROR();
        return sharedNotebooks;
    }

    const int numSharedNotebooks = query.size();
    sharedNotebooks.reserve(qMax(numSharedNotebooks, 0));

    while (query.next()) {
        sharedNotebooks << qevercloud::SharedNotebook();
        auto & sharedNotebook = sharedNotebooks.back();

        ErrorString error;
        if (!fillSharedNotebookFromSqlRecord(
                query.record(), sharedNotebook, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            sharedNotebooks.clear();
            return sharedNotebooks;
        }
    }

    std::sort(
        sharedNotebooks.begin(), sharedNotebooks.end(),
        SharedNotebookCompareByIndex());

    QNDEBUG(
        "local_storage",
        "found " << sharedNotebooks.size() << " shared notebooks");

    return sharedNotebooks;
}

QList<qevercloud::SharedNote> LocalStorageManagerPrivate::listSharedNotesPerNoteGuid(
    const QString & noteGuid, ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listSharedNotesPerNoteGuid: guid = "
            << noteGuid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list shared notes per note guid"));

    QSqlQuery query(m_sqlDatabase);

    bool res = query.prepare(QStringLiteral(
        "SELECT * FROM SharedNotes WHERE sharedNoteNoteGuid=?"));
    if (!res) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase("can't prepare SQL query");
        errorDescription.details() = query.lastError().text();
        return {};
    }

    query.addBindValue(noteGuid);
    res = query.exec();
    if (!res) {
        SET_ERROR();
        return {};
    }

    QMap<int, qevercloud::SharedNote> sharedNotesByIndex;

    while (query.next()) {
        qevercloud::SharedNote sharedNote;
        int indexInNote = -1;
        ErrorString error;
        if (!fillSharedNoteFromSqlRecord(
                query.record(), sharedNote, indexInNote, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return {};
        }

        sharedNotesByIndex[indexInNote] = sharedNote;
    }

    QList<qevercloud::SharedNote> sharedNotes;
    sharedNotes.reserve(qMax(sharedNotesByIndex.size(), 0));
    for (const auto it: qevercloud::toRange(sharedNotesByIndex)) {
        sharedNotes << it.value();
    }

    return sharedNotes;
}

QList<qevercloud::Resource> LocalStorageManagerPrivate::listResourcesPerNoteLocalId(
    const QString & noteLocalId, const bool withBinaryData,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listResourcesPerNoteGuid: note local id = "
            << noteLocalId);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list resources per note local id"));

    QSqlQuery query(m_sqlDatabase);
    bool res = query.prepare(QStringLiteral(
        "SELECT Resources.resourceLocalUid, resourceGuid, "
        "noteGuid, resourceUpdateSequenceNumber, resourceIsDirty, "
        "dataSize, dataHash, mime, width, height, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, alternateDataHash, "
        "resourceIndexInNote, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, fileName, "
        "attachment, resourceKey, resourceMapKey, resourceValue, "
        "localNote, recognitionDataBody FROM Resources "
        "LEFT OUTER JOIN ResourceAttributes ON "
        "Resources.resourceLocalUid = "
        "ResourceAttributes.resourceLocalUid "
        "LEFT OUTER JOIN ResourceAttributesApplicationDataKeysOnly ON "
        "Resources.resourceLocalUid = "
        "ResourceAttributesApplicationDataKeysOnly.resourceLocalUid "
        "LEFT OUTER JOIN ResourceAttributesApplicationDataFullMap ON "
        "Resources.resourceLocalUid = "
        "ResourceAttributesApplicationDataFullMap.resourceLocalUid "
        "LEFT OUTER JOIN NoteResources ON "
        "Resources.resourceLocalUid = NoteResources.localResource "
        "WHERE Resources.noteLocalUid=?"));
    if (!res) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase("can't prepare SQL query");
        errorDescription.details() = query.lastError().text();
        return {};
    }

    query.addBindValue(noteLocalId);
    res = query.exec();
    if (!res) {
        SET_ERROR();
        return {};
    }

    QMap<int, qevercloud::Resource> resourcesByIndex;
    while (query.next()) {
        qevercloud::Resource resource;
        int indexInNote = -1;
        ErrorString error;
        if (!fillResourceFromSqlRecord(
                query.record(), resource, indexInNote, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return {};
        }

        resourcesByIndex[indexInNote] = resource;
    }

    QList<qevercloud::Resource> resources;
    resources.reserve(qMax(resourcesByIndex.size(), 0));
    for (const auto & it: qevercloud::toRange(resourcesByIndex)) {
        resources << it.value();
    }

    if (withBinaryData) {
        for (auto & resource: resources) {
            if (!readResourceDataFromFiles(resource, errorDescription)) {
                return {};
            }
        }
    }

    return resources;
}

bool LocalStorageManagerPrivate::expungeNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::expungeNotebook: notebook = " << notebook);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge notebook from the local storage"));

    QString localId = notebook.localId();

    QString column, id;
    bool shouldCheckRowExistence = true;

    if (notebook.guid()) {
        column = QStringLiteral("guid");
        id = *notebook.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("notebook's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            ErrorString error;
            if (!getNotebookLocalIdForGuid(id, localId, error)) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            notebook.setLocalId(localId);
            shouldCheckRowExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = notebook.localId();
    }

    if (shouldCheckRowExistence &&
        !rowExists(QStringLiteral("Notebooks"), column, id))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("notebook to be expunged was not found"));
        errorDescription.details() = column;
        errorDescription.details() = QStringLiteral(" = ");
        errorDescription.details() = id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    ErrorString error;
    if (!removeResourceDataFilesForNotebook(notebook, error)) {
        errorDescription = errorPrefix;
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        return false;
    }

    id = sqlEscapeString(id);

    const QString queryString =
        QString::fromUtf8("DELETE FROM Notebooks WHERE %1 = '%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

int LocalStorageManagerPrivate::linkedNotebookCount(
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of linked notebooks "
                   "in the local storage"));

    bool res = checkAndPrepareGetLinkedNotebookCountQuery();
    QSqlQuery & query = m_getLinkedNotebookCountQuery;
    if (!res) {
        SET_ERROR();
        return -1;
    }

    res = query.exec();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG(
            "local_storage", "Found no linked notebooks in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);

    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

bool LocalStorageManagerPrivate::addLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add linked notebook to the local storage"));

    ErrorString error;
    if (!checkLinkedNotebook(linkedNotebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid LinkedNotebook: " << linkedNotebook
                                             << "\nError: " << error);
        return false;
    }

    if (rowExists(
            QStringLiteral("LinkedNotebooks"), QStringLiteral("guid"),
            QVariant(*linkedNotebook.guid())))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("linked notebook with specified guid already exists"));
        errorDescription.details() = *linkedNotebook.guid();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    error.clear();
    if (!insertOrReplaceLinkedNotebook(linkedNotebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::updateLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update linked notebook in the local storage"));

    ErrorString error;
    if (!checkLinkedNotebook(linkedNotebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid LinkedNotebook: " << linkedNotebook
                                             << "\nError: " << error);
        return false;
    }

    const QString guid = *linkedNotebook.guid();

    if (!rowExists(
            QStringLiteral("LinkedNotebooks"), QStringLiteral("guid"),
            QVariant(guid)))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("linked notebook to be updated was not found"));
        errorDescription.details() = guid;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    error.clear();
    if (!insertOrReplaceLinkedNotebook(linkedNotebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::findLinkedNotebook(
    qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::findLinkedNotebook");

    ErrorString errorPrefix(
        QT_TR_NOOP("Can't find linked notebook in the local storage"));

    if (!linkedNotebook.guid()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("linked notebook's guid is not set"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString linkedNotebookGuid = *linkedNotebook.guid();
    QNDEBUG("local_storage", "guid = " << linkedNotebookGuid);

    if (!checkGuid(linkedNotebookGuid)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("linked notebook's guid is invalid"));
        errorDescription.details() = linkedNotebookGuid;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    QSqlQuery query(m_sqlDatabase);

    query.prepare(
        QStringLiteral("SELECT guid, updateSequenceNumber, isDirty, "
                       "shareName, username, shardId, "
                       "sharedNotebookGlobalId, uri, noteStoreUrl, "
                       "webApiUrlPrefix, stack, businessId "
                       "FROM LinkedNotebooks WHERE guid = ?"));

    query.addBindValue(linkedNotebookGuid);

    bool res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    qevercloud::LinkedNotebook result;
    ErrorString error;
    if (!fillLinkedNotebookFromSqlRecord(query.record(), result, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    linkedNotebook = result;
    return true;
}

QList<qevercloud::LinkedNotebook>
LocalStorageManagerPrivate::listAllLinkedNotebooks(
    ErrorString & errorDescription, const std::size_t limit,
    const std::size_t offset, const ListLinkedNotebooksOrder order,
    const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage", "LocalStorageManagerPrivate::listAllLinkedNotebooks");

    return listLinkedNotebooks(
        ListObjectsOption::ListAll, errorDescription, limit, offset, order,
        orderDirection);
}

QList<qevercloud::LinkedNotebook>
LocalStorageManagerPrivate::listLinkedNotebooks(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const std::size_t limit, const std::size_t offset,
    const ListLinkedNotebooksOrder & order,
    const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listLinkedNotebooks: flag = " << flag);

    return listObjects<qevercloud::LinkedNotebook, ListLinkedNotebooksOrder>(
        flag, errorDescription, limit, offset, order, orderDirection);
}

bool LocalStorageManagerPrivate::expungeLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::expungeLinkedNotebook: linked notebook = "
            << linkedNotebook);

    ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge linked notebook from the local storage"));

    if (!linkedNotebook.guid()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("linked notebook's guid is not set"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString linkedNotebookGuid = sqlEscapeString(*linkedNotebook.guid());

    if (!checkGuid(linkedNotebookGuid)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("linked notebook's guid is invalid"));
        errorDescription.details() = linkedNotebookGuid;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (!rowExists(
            QStringLiteral("LinkedNotebooks"), QStringLiteral("guid"),
            linkedNotebookGuid))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("can't find the linked notebook to be expunged"));
        errorDescription.details() = linkedNotebookGuid;
        return false;
    }

    ErrorString error;
    if (!removeResourceDataFilesForLinkedNotebook(linkedNotebook, error)) {
        errorDescription = errorPrefix;
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        return false;
    }

    const QString queryString =
        QString::fromUtf8("DELETE FROM LinkedNotebooks WHERE guid='%1'")
            .arg(linkedNotebookGuid);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

int LocalStorageManagerPrivate::noteCount(
    ErrorString & errorDescription, const NoteCountOptions options) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of notes in the local storage"));

    QString queryString = QStringLiteral("SELECT COUNT(*) FROM Notes");
    const QString condition = noteCountOptionsToSqlQueryPart(options);
    if (!condition.isEmpty()) {
        queryString += QStringLiteral(" WHERE ");
        queryString += condition;
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG("local_storage", "Found no notes in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);

    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

int LocalStorageManagerPrivate::noteCountPerNotebook(
    const qevercloud::Notebook & notebook, ErrorString & errorDescription,
    const NoteCountOptions options) const
{
    ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of notes per notebook in the local "
                   "storage"));

    ErrorString error;
    if (!checkNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid notebook: " << notebook << "\nError: " << error);
        return -1;
    }

    QString column, value;
    if (notebook.guid()) {
        column = QStringLiteral("notebookGuid");
        value = *notebook.guid();
    }
    else {
        column = QStringLiteral("notebookLocalUid");
        value = notebook.localId();
    }

    value = sqlEscapeString(value);

    QString queryString =
        QString::fromUtf8("SELECT COUNT(*) FROM Notes WHERE %1 = '%2'")
            .arg(column, value);

    const QString condition = noteCountOptionsToSqlQueryPart(options);
    if (!condition.isEmpty()) {
        queryString += QStringLiteral(" AND ");
        queryString += condition;
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);

    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG(
            "local_storage",
            "Found no notes per given notebook in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

int LocalStorageManagerPrivate::noteCountPerTag(
    const qevercloud::Tag & tag, ErrorString & errorDescription,
    const NoteCountOptions options) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of notes per tag from the local "
                   "storage"));

    ErrorString error;
    if (!checkTag(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid tag: " << tag << "\nError: " << error);
        return -1;
    }

    QString column, value;
    if (tag.guid()) {
        column = QStringLiteral("tag");
        value = *tag.guid();
    }
    else {
        column = QStringLiteral("localTag");
        value = tag.localId();
    }

    value = sqlEscapeString(value);

    QString queryString =
        QString::fromUtf8(
            "SELECT COUNT(*) FROM Notes WHERE (localUid IN (SELECT DISTINCT "
            "localNote FROM NoteTags WHERE %1 = '%2'))")
            .arg(column, value);

    QString condition = noteCountOptionsToSqlQueryPart(options);
    if (!condition.isEmpty()) {
        queryString += QStringLiteral(" AND ");
        queryString += condition;
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG(
            "local_storage",
            "Found no notes containing given tag in the local storage");
        return 0;
    }

    bool conversionResult = false;
    int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

bool LocalStorageManagerPrivate::noteCountsPerAllTags(
    QHash<QString, int> & noteCountsPerTagLocalId,
    ErrorString & errorDescription, const NoteCountOptions options) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get note counts for all tags from the local "
                   "storage"));

    noteCountsPerTagLocalId.clear();

    QString queryString = QStringLiteral(
        "SELECT localTag, COUNT(localTag) AS noteCount FROM "
        "NoteTags LEFT OUTER JOIN Notes "
        "ON NoteTags.localNote = Notes.localUid ");

    const QString condition = noteCountOptionsToSqlQueryPart(options);
    if (!condition.isEmpty()) {
        queryString += QStringLiteral("WHERE ");
        queryString += condition;
        queryString += QStringLiteral(" ");
    }
    queryString += QStringLiteral("GROUP BY localTag");

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        SET_ERROR();
        return false;
    }

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const int tagLocalIdIndex = rec.indexOf(QStringLiteral("localTag"));
        if (Q_UNLIKELY(tagLocalIdIndex < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't find local id of tag in the result of "
                           "SQL query"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        const QString tagLocalId = rec.value(tagLocalIdIndex).toString();
        if (Q_UNLIKELY(tagLocalId.isEmpty())) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("local id of a tag from the result of SQL query "
                           "is empty"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        const int noteCountIndex = rec.indexOf(QStringLiteral("noteCount"));
        if (Q_UNLIKELY(noteCountIndex < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't find note count for tag in the result of "
                           "SQL query"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        bool conversionResult = false;

        const int noteCount =
            rec.value(noteCountIndex).toInt(&conversionResult);

        if (Q_UNLIKELY(!conversionResult)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("failed to convert note count for tag from "
                           "the result of SQL query to int"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (Q_UNLIKELY(noteCount < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("note count for tag from the result of SQL "
                           "query is negative"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        noteCountsPerTagLocalId[tagLocalId] = noteCount;
    }

    return true;
}

int LocalStorageManagerPrivate::noteCountPerNotebooksAndTags(
    const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
    ErrorString & errorDescription, const NoteCountOptions options) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of notes per notebooks and tags from "
                   "the local storage"));

    QString queryString = QStringLiteral("SELECT COUNT(*) FROM Notes");
    if (!notebookLocalIds.isEmpty() || !tagLocalIds.isEmpty()) {
        queryString += QStringLiteral(" WHERE ");

        if (!notebookLocalIds.isEmpty()) {
            queryString += QStringLiteral("(notebookLocalUid IN (");

            for (const auto & notebookLocalId: notebookLocalIds) {
                queryString += QStringLiteral("'") +
                    sqlEscapeString(notebookLocalId) + QStringLiteral("', ");
            }

            queryString.chop(2);
            queryString += QStringLiteral(")) ");
        }

        if (!tagLocalIds.isEmpty()) {
            if (!notebookLocalIds.isEmpty()) {
                queryString += QStringLiteral(" AND ");
            }

            queryString += QStringLiteral(
                "(localUid IN (SELECT DISTINCT localNote "
                "FROM NoteTags WHERE localTag IN (");

            for (const auto & tagLocalId: tagLocalIds) {
                queryString += QStringLiteral("'") +
                    sqlEscapeString(tagLocalId) + QStringLiteral("', ");
            }

            queryString.chop(2);
            queryString += QStringLiteral(")))");
        }
    }

    const QString condition = noteCountOptionsToSqlQueryPart(options);
    if (!condition.isEmpty()) {
        if (!notebookLocalIds.isEmpty() || !tagLocalIds.isEmpty()) {
            queryString += QStringLiteral(" AND ");
        }
        else {
            queryString += QStringLiteral(" WHERE ");
        }

        queryString += condition;
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG(
            "local_storage",
            "Found no notes per given notebooks and tags in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

QString LocalStorageManagerPrivate::noteCountOptionsToSqlQueryPart(
    const NoteCountOptions options) const
{
    QString queryPart;
    if (!(options & NoteCountOption::IncludeNonDeletedNotes) ||
        !(options & NoteCountOption::IncludeDeletedNotes))
    {
        queryPart = QStringLiteral("deletionTimestamp IS ");
        if (options & NoteCountOption::IncludeNonDeletedNotes) {
            queryPart += QStringLiteral("NULL");
        }
        else {
            queryPart += QStringLiteral("NOT NULL");
        }
    }
    return queryPart;
}

bool LocalStorageManagerPrivate::addNote(
    qevercloud::Note & note, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add note to the local storage"));

    ErrorString error;
    QString notebookLocalId;
    if (!getNotebookLocalIdFromNote(note, notebookLocalId, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription << ", note: " << note);
        return false;
    }

    note.setNotebookLocalId(notebookLocalId);

    error.clear();
    QString notebookGuid;
    if (!getNotebookGuidForNote(note, notebookGuid, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription << ", note: " << note);
        return false;
    }

    note.setNotebookGuid(notebookGuid);

    error.clear();
    if (!checkNote(note, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid note: " << errorDescription << "; note: " << note);
        return false;
    }

    QString localId = note.localId();

    QString column, id;
    bool shouldCheckNoteExistence = true;

    if (note.guid()) {
        column = QStringLiteral("guid");
        id = *note.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("note's guid is invalid"));
            QNWARNING("local_storage", errorDescription << ", note: " << note);
            return false;
        }

        if (localId.isEmpty()) {
            error.clear();
            if (getNoteLocalIdForGuid(id, localId, error) && !localId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("found already existing note with the same "
                               "guid"));
                QNWARNING(
                    "local_storage", errorDescription << ", guid: " << id);
                return false;
            }

            localId = UidGenerator::Generate();
            note.setLocalId(localId);
            shouldCheckNoteExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = localId;
    }

    setNoteIdsToNoteResources(note);

    if (shouldCheckNoteExistence &&
        rowExists(QStringLiteral("Notes"), column, id)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("note already exists"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription << ", note: " << note);
        return false;
    }

    UpdateNoteOptions options(
        UpdateNoteOption::UpdateResourceMetadata |
        UpdateNoteOption::UpdateResourceBinaryData |
        UpdateNoteOption::UpdateTags);

    const bool res = insertOrReplaceNote(note, options, errorDescription);
    if (!res) {
        QNWARNING("local_storage", "Note which produced the error: " << note);
    }

    return res;
}

bool LocalStorageManagerPrivate::updateNote(
    qevercloud::Note & note, const UpdateNoteOptions options,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update note in the local storage"));

    ErrorString error;
    QString notebookLocalId;
    if (!getNotebookLocalIdFromNote(note, notebookLocalId, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    note.setNotebookLocalId(notebookLocalId);

    error.clear();
    QString notebookGuid;
    if (!getNotebookGuidForNote(note, notebookGuid, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    note.setNotebookGuid(notebookGuid);

    error.clear();
    if (!checkNote(note, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", "Found invalid note: " << note);
        return false;
    }

    QString localId = note.localId();

    QString column, id;
    bool shouldCheckNoteExistence = true;

    if (note.guid()) {
        column = QStringLiteral("guid");
        id = *note.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("note's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            error.clear();
            if (!getNoteLocalIdForGuid(id, localId, error) || localId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                return false;
            }

            note.setLocalId(localId);
            shouldCheckNoteExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = localId;
    }

    setNoteIdsToNoteResources(note);

    if (shouldCheckNoteExistence &&
        !rowExists(QStringLiteral("Notes"), column, id)) {
        bool foundByOtherColumn = false;

        if (note.guid()) {
            QNDEBUG(
                "local_storage",
                "Failed to find note by guid within the local storage, trying "
                    << "to find it by local id");

            column = QStringLiteral("localUid");
            id = localId;

            foundByOtherColumn = rowExists(QStringLiteral("Notes"), column, id);
        }

        if (!foundByOtherColumn) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("note was not found in the local storage"));
            errorDescription.details() = column;
            errorDescription.details() += QStringLiteral(" = ");
            errorDescription.details() += id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    const bool res = insertOrReplaceNote(note, options, errorDescription);
    if (!res) {
        QNWARNING("local_storage", "Note which produced the error: " << note);
    }

    return res;
}

bool LocalStorageManagerPrivate::findNote(
    qevercloud::Note & note, const GetNoteOptions options,
    ErrorString & errorDescription) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::findNote");

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find note in the local storage"));

    QString column, id;
    if (note.guid()) {
        column = QStringLiteral("guid");
        id = *note.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("note's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        QString localId;
        ErrorString error;
        if (!getNoteLocalIdForGuid(id, localId, error) || localId.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }

        note.setLocalId(localId);
    }
    else {
        column = QStringLiteral("localUid");
        id = note.localId();
    }

    bool withResourceMetadata = (options & GetNoteOption::WithResourceMetadata);

    bool withResourceBinaryData =
        (options & GetNoteOption::WithResourceBinaryData);

    QSqlQuery query(m_sqlDatabase);
    bool res = query.prepare(QString::fromUtf8(
        "SELECT localUid, guid, updateSequenceNumber, isDirty, "
        "isLocal, isFavorited, title, content, contentLength, "
        "contentHash, creationTimestamp, modificationTimestamp, "
        "deletionTimestamp, isActive, hasAttributes, thumbnail, "
        "notebookLocalUid, notebookGuid, subjectDate, latitude, "
        "longitude, altitude, author, source, sourceURL, "
        "sourceApplication, shareDate, reminderOrder, "
        "reminderDoneTime, reminderTime, placeName, contentClass, "
        "lastEditedBy, creatorId, lastEditorId, sharedWithBusiness, "
        "conflictSourceNoteGuid, noteTitleQuality, "
        "applicationDataKeysOnly, applicationDataKeysMap, "
        "applicationDataValues, classificationKeys, "
        "classificationValues, noUpdateNoteTitle, noUpdateNoteContent, "
        "noEmailNote, noShareNote, noShareNotePublicly, "
        "noteResourceCountMax, uploadLimit, resourceSizeMax, "
        "noteSizeMax, uploaded, localNote, note, localTag, tag, "
        "tagIndexInNote FROM Notes "
        "LEFT OUTER JOIN NoteRestrictions ON "
        "Notes.localUid = NoteRestrictions.noteLocalUid "
        "LEFT OUTER JOIN NoteLimits ON "
        "Notes.localUid = NoteLimits.noteLocalUid "
        "LEFT OUTER JOIN NoteTags ON "
        "Notes.localUid = NoteTags.localNote "
        "WHERE Notes.%1 = ?").arg(column));
    if (!res) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase("can't prepare SQL query");
        errorDescription.details() = query.lastError().text();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    query.addBindValue(id);

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    qevercloud::Note result;

    QList<std::pair<QString, int>> tagGuidsAndIndices;
    QHash<QString, int> tagGuidIndexPerGuid;

    QList<std::pair<QString, int>> tagLocalIdsAndIndices;
    QHash<QString, int> tagLocalIdIndexPerId;

    std::size_t counter = 0;
    while (query.next()) {
        const QSqlRecord rec = query.record();

        ErrorString error;
        if (!fillNoteFromSqlRecord(rec, result, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        ++counter;

        if (withResourceMetadata) {
            ErrorString error;
            auto resources = listResourcesPerNoteLocalId(
                result.localId(), withResourceBinaryData, error);
            if (!error.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }
            result.setResources(std::move(resources));
        }

        error.clear();
        if (!fillNoteTagIdFromSqlRecord(
                rec, QStringLiteral("tag"), tagGuidsAndIndices,
                tagGuidIndexPerGuid, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        error.clear();
        if (!fillNoteTagIdFromSqlRecord(
                rec, QStringLiteral("localTag"), tagLocalIdsAndIndices,
                tagLocalIdIndexPerId, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    if (!counter) {
        if (!errorDescription.isEmpty()) {
            QNDEBUG("local_storage", errorDescription);
        }

        return false;
    }

    const int numTagGuids = tagGuidsAndIndices.size();
    if (numTagGuids > 0) {
        std::sort(
            tagGuidsAndIndices.begin(), tagGuidsAndIndices.end(),
            QStringIntPairCompareByInt());

        QStringList tagGuids;
        tagGuids.reserve(numTagGuids);
        for (int i = 0; i < numTagGuids; ++i) {
            const QString & guid = tagGuidsAndIndices[i].first;
            if (guid.isEmpty()) {
                continue;
            }

            tagGuids << guid;
        }

        result.setTagGuids(tagGuids);
    }

    const int numTagLocalIds = tagLocalIdsAndIndices.size();
    if (numTagLocalIds > 0) {
        std::sort(
            tagLocalIdsAndIndices.begin(), tagLocalIdsAndIndices.end(),
            QStringIntPairCompareByInt());

        QStringList tagLocalIds;
        tagLocalIds.reserve(numTagLocalIds);
        for (int i = 0; i < numTagLocalIds; ++i) {
            tagLocalIds << tagLocalIdsAndIndices[i].first;
        }

        result.setTagLocalIds(tagLocalIds);
    }

    if (result.guid()) {
        ErrorString error;
        auto sharedNotes = listSharedNotesPerNoteGuid(*result.guid(), error);
        if (!error.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (!sharedNotes.isEmpty()) {
            result.setSharedNotes(std::move(sharedNotes));
        }
    }

    ErrorString error;
    if (!checkNote(result, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    note = result;
    return true;
}

QList<qevercloud::Note> LocalStorageManagerPrivate::listNotesPerNotebook(
    const qevercloud::Notebook & notebook, const GetNoteOptions options,
    ErrorString & errorDescription, const ListObjectsOptions & flag,
    const std::size_t limit, const std::size_t offset,
    const ListNotesOrder & order, const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listNotesPerNotebook: "
            << "notebook = " << notebook << "\nWith resource metadata = "
            << ((options & GetNoteOption::WithResourceMetadata) ? "true"
                                                                : "false")
            << ", with resource binary data = "
            << ((options & GetNoteOption::WithResourceBinaryData) ? "true"
                                                                  : "false")
            << ", flag = " << flag << ", limit = " << limit
            << ", offset = " << offset << ", order = " << order
            << ", order direction = " << orderDirection);

    const ErrorString errorPrefix(QT_TR_NOOP("Can't list notes per notebook"));

    QList<qevercloud::Note> notes;

    QString column, id;
    if (notebook.guid()) {
        column = QStringLiteral("notebookGuid");
        id = *notebook.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("notebook's guid is invalid"));
            QNWARNING("local_storage", errorDescription);
            return notes;
        }
    }
    else {
        column = QStringLiteral("notebookLocalUid");
        id = notebook.localId();
    }

    id = sqlEscapeString(id);

    const QString notebookIdSqlQueryCondition =
        QString::fromUtf8("%1 = '%2'").arg(column, id);

    return listNotesImpl(
        errorPrefix, notebookIdSqlQueryCondition, flag, options,
        errorDescription, limit, offset, order, orderDirection);
}

QList<qevercloud::Note> LocalStorageManagerPrivate::listNotesPerTag(
    const qevercloud::Tag & tag, const GetNoteOptions options,
    ErrorString & errorDescription, const ListObjectsOptions & flag,
    const std::size_t limit, const std::size_t offset,
    const ListNotesOrder & order, const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listNotesPerTag: "
            << "tag = " << tag << "\nWith resource metadata = "
            << ((options & GetNoteOption::WithResourceMetadata) ? "true"
                                                                : "false")
            << ", with resource binary data = "
            << ((options & GetNoteOption::WithResourceBinaryData) ? "true"
                                                                  : "false")
            << ", flag = " << flag << ", limit = " << limit
            << ", offset = " << offset << ", order = " << order
            << ", order direction = " << orderDirection);

    const ErrorString errorPrefix(QT_TR_NOOP("Can't list all notes with tag"));

    QList<qevercloud::Note> notes;

    QString column, id;
    if (tag.guid()) {
        column = QStringLiteral("tag");
        id = *tag.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("tag's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return notes;
        }
    }
    else {
        column = QStringLiteral("localTag");
        id = tag.localId();
    }

    id = sqlEscapeString(id);

    QString queryCondition =
        QString::fromUtf8(
            "localUid IN (SELECT DISTINCT localNote FROM NoteTags WHERE "
            "%1 = '%2')")
            .arg(column, id);

    return listNotesImpl(
        errorPrefix, queryCondition, flag, options, errorDescription, limit,
        offset, order, orderDirection);
}

QList<qevercloud::Note>
LocalStorageManagerPrivate::listNotesPerNotebooksAndTags(
    const QStringList & notebookLocalIds, const QStringList & tagLocalIds,
    const GetNoteOptions options, ErrorString & errorDescription,
    const ListObjectsOptions & flag, const std::size_t limit,
    const std::size_t offset, const ListNotesOrder & order,
    const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listNotesPerNotebooksAndTags: flag = "
            << flag
            << ((options & GetNoteOption::WithResourceMetadata) ? "true"
                                                                : "false")
            << ", with resource binary data = "
            << ((options & GetNoteOption::WithResourceBinaryData) ? "true"
                                                                  : "false")
            << ", notebook local ids: "
            << notebookLocalIds.join(QStringLiteral(", "))
            << ", tag local ids: " << tagLocalIds.join(QStringLiteral(", ")));

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list notes per notebooks and tags from the local "
                   "storage"));

    QString notebooksAndTagsSqlQueryCondition;

    if (!notebookLocalIds.isEmpty() && tagLocalIds.isEmpty()) {
        notebooksAndTagsSqlQueryCondition += QStringLiteral(
            "localUid IN (SELECT DISTINCT Notes.localUid FROM "
            "Notes WHERE Notes.notebookLocalUid IN (");

        for (const auto & notebookLocalId: notebookLocalIds) {
            notebooksAndTagsSqlQueryCondition += QStringLiteral("'") +
                sqlEscapeString(notebookLocalId) + QStringLiteral("', ");
        }

        notebooksAndTagsSqlQueryCondition.chop(2);
        notebooksAndTagsSqlQueryCondition += QStringLiteral("))");
    }
    else if (notebookLocalIds.isEmpty() && !tagLocalIds.isEmpty()) {
        notebooksAndTagsSqlQueryCondition += QStringLiteral(
            "localUid IN (SELECT DISTINCT NoteTags.localNote FROM "
            "NoteTags WHERE NoteTags.localTag IN (");

        for (const auto & tagLocalId: tagLocalIds) {
            notebooksAndTagsSqlQueryCondition += QStringLiteral("'") +
                sqlEscapeString(tagLocalId) + QStringLiteral("', ");
        }

        notebooksAndTagsSqlQueryCondition.chop(2);
        notebooksAndTagsSqlQueryCondition += QStringLiteral("))");
    }
    else {
        notebooksAndTagsSqlQueryCondition += QStringLiteral(
            "localUid IN (SELECT DISTINCT Notes.localUid FROM "
            "(Notes LEFT OUTER JOIN NoteTags ON "
            "Notes.localUid = NoteTags.localNote) "
            "WHERE Notes.notebookLocalUid IN (");

        for (const auto & notebookLocalId: notebookLocalIds) {
            notebooksAndTagsSqlQueryCondition += QStringLiteral("'") +
                sqlEscapeString(notebookLocalId) + QStringLiteral("', ");
        }

        notebooksAndTagsSqlQueryCondition.chop(2);
        notebooksAndTagsSqlQueryCondition +=
            QStringLiteral(") AND NoteTags.localTag IN(");

        for (const auto & tagLocalId: tagLocalIds) {
            notebooksAndTagsSqlQueryCondition += QStringLiteral("'") +
                sqlEscapeString(tagLocalId) + QStringLiteral("', ");
        }

        notebooksAndTagsSqlQueryCondition.chop(2);
        notebooksAndTagsSqlQueryCondition += QStringLiteral("))");
    }

    return listNotesImpl(
        errorPrefix, notebooksAndTagsSqlQueryCondition, flag, options,
        errorDescription, limit, offset, order, orderDirection);
}

QList<qevercloud::Note> LocalStorageManagerPrivate::listNotesByLocalIds(
    const QStringList & noteLocalIds, const GetNoteOptions options,
    ErrorString & errorDescription, const ListObjectsOptions & flag,
    const std::size_t limit, const std::size_t offset,
    const ListNotesOrder order, const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listNotesByLocalIds: "
            << "flag = " << flag << ", with resource metadata = "
            << ((options & GetNoteOption::WithResourceMetadata) ? "true"
                                                                : "false")
            << ", with resource binary data = "
            << ((options & GetNoteOption::WithResourceBinaryData) ? "true"
                                                                  : "false")
            << ", note local ids: " << noteLocalIds.join(QStringLiteral(",")));

    if (noteLocalIds.isEmpty()) {
        return QList<qevercloud::Note>();
    }

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list notes by local ids from the local storage"));

    QString noteLocalIdsSqlQueryCondition = QStringLiteral("localUid IN (");
    for (const auto & noteLocalId: noteLocalIds) {
        noteLocalIdsSqlQueryCondition += QStringLiteral("'") +
            sqlEscapeString(noteLocalId) + QStringLiteral("', ");
    }

    noteLocalIdsSqlQueryCondition.chop(2);
    noteLocalIdsSqlQueryCondition += QStringLiteral(")");

    return listNotesImpl(
        errorPrefix, noteLocalIdsSqlQueryCondition, flag, options,
        errorDescription, limit, offset, order, orderDirection);
}

QList<qevercloud::Note> LocalStorageManagerPrivate::listNotes(
    const ListObjectsOptions flag, const GetNoteOptions options,
    ErrorString & errorDescription, const std::size_t limit,
    const std::size_t offset, const ListNotesOrder & order,
    const OrderDirection & orderDirection,
    std::optional<QString> linkedNotebookGuid) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listNotes: flag = "
            << flag << ", with resource metadata = "
            << ((options & GetNoteOption::WithResourceMetadata) ? "true"
                                                                : "false")
            << ", with resource binary data = "
            << ((options & GetNoteOption::WithResourceBinaryData) ? "true"
                                                                  : "false")
            << ", linked notebook guid = "
            << linkedNotebookGuid.value_or(QStringLiteral("<not set>")));

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list notes from the local storage"));

    QString linkedNotebookGuidSqlQueryCondition;
    if (linkedNotebookGuid) {
        linkedNotebookGuidSqlQueryCondition = QStringLiteral(
            "localUid IN (SELECT DISTINCT Notes.localUid FROM "
            "(Notes LEFT OUTER JOIN Notebooks ON "
            "Notes.notebookLocalUid = Notebooks.localUid) "
            "WHERE Notebooks.linkedNotebookGuid");

        if (linkedNotebookGuid->isEmpty()) {
            linkedNotebookGuidSqlQueryCondition += QStringLiteral(" IS NULL)");
        }
        else {
            linkedNotebookGuidSqlQueryCondition +=
                QString::fromUtf8(" = '%1')")
                    .arg(sqlEscapeString(*linkedNotebookGuid));
        }
    }

    return listNotesImpl(
        errorPrefix, linkedNotebookGuidSqlQueryCondition, flag, options,
        errorDescription, limit, offset, order, orderDirection);
}

QList<qevercloud::Note> LocalStorageManagerPrivate::listNotesImpl(
    const ErrorString & errorPrefix, const QString & sqlQueryCondition,
    const ListObjectsOptions flag, const GetNoteOptions options,
    ErrorString & errorDescription, const std::size_t limit,
    const std::size_t offset, const ListNotesOrder & order,
    const OrderDirection & orderDirection) const
{
    bool withResourceMetadata = (options & GetNoteOption::WithResourceMetadata);

    bool withResourceBinaryData =
        (options & GetNoteOption::WithResourceBinaryData);

    // Will run all the queries from this method and its sub-methods within
    // a single transaction to prevent multiple drops and re-obtainings of
    // shared lock
    const Transaction transaction(
        m_sqlDatabase, *this, Transaction::Type::Selection);

    Q_UNUSED(transaction)

    ErrorString error;

    auto notes = listObjects<qevercloud::Note, ListNotesOrder>(
        flag, error, limit, offset, order, orderDirection, sqlQueryCondition);

    if (notes.isEmpty() && !error.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return notes;
    }

    const int numNotes = notes.size();
    for (int i = 0; i < numNotes; ++i) {
        auto & note = notes[i];

        error.clear();
        if (!findAndSetTagIdsPerNote(note, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return {};
        }

        if (withResourceMetadata) {
            error.clear();
            auto resources = listResourcesPerNoteLocalId(
                note.localId(), withResourceBinaryData, error);
            if (!error.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return {};
            }

            if (!resources.isEmpty()) {
                note.setResources(std::move(resources));
            }
        }

        if (note.guid()) {
            error.clear();
            auto sharedNotes = listSharedNotesPerNoteGuid(*note.guid(), error);
            if (!error.isEmpty()) {
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return {};
            }

            if (!sharedNotes.isEmpty()) {
                note.setSharedNotes(std::move(sharedNotes));
            }
        }

        error.clear();
        if (!checkNote(note, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            notes.clear();
            return notes;
        }
    }

    return notes;
}

bool LocalStorageManagerPrivate::expungeNote(
    qevercloud::Note & note, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::expungeNote: note = " << note);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge note from the local storage"));

    ErrorString error;
    QString notebookLocalId;
    if (!getNotebookLocalIdFromNote(note, notebookLocalId, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    note.setNotebookLocalId(notebookLocalId);

    error.clear();
    QString notebookGuid;
    if (!getNotebookGuidForNote(note, notebookGuid, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    note.setNotebookGuid(notebookGuid);

    QString localId = note.localId();

    QString column, id;
    bool shouldCheckNoteExistence = true;

    if (note.guid()) {
        column = QStringLiteral("guid");
        id = *note.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("note's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            error.clear();
            if (!getNoteLocalIdForGuid(id, localId, error) || localId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            note.setLocalId(localId);
            shouldCheckNoteExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = note.localId();
    }

    id = sqlEscapeString(id);

    if (shouldCheckNoteExistence &&
        !rowExists(QStringLiteral("Notes"), column, id)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("note to be expunged was not found"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    QString queryString =
        QString::fromUtf8("DELETE FROM Notes WHERE %1 = '%2'").arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    error.clear();
    if (!removeResourceDataFilesForNote(localId, error)) {
        errorDescription = errorPrefix;
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        return false;
    }

    return true;
}

QStringList LocalStorageManagerPrivate::findNoteLocalIdsWithSearchQuery(
    const NoteSearchQuery & noteSearchQuery,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::findNoteLocalIdsWithSearchQuery: "
            << noteSearchQuery);

    if (!noteSearchQuery.isMatcheable()) {
        return QStringList();
    }

    QString queryString;

    /**
     * Will run all the queries from this method and its sub-methods within
     * a single transaction to prevent multiple drops and re-obtainings of
     * the shared lock
     */
    const Transaction transaction(
        m_sqlDatabase, *this, Transaction::Type::Selection);

    Q_UNUSED(transaction)

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find notes with the note search query"));

    ErrorString error;
    if (!noteSearchQueryToSQL(noteSearchQuery, queryString, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return QStringList();
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        SET_ERROR();
        QNWARNING("local_storage", "Full executed SQL query: " << queryString);
        return QStringList();
    }

    QSet<QString> foundLocalIds;
    while (query.next()) {
        const QSqlRecord rec = query.record();
        const int index = rec.indexOf(QStringLiteral("localUid"));
        if (index < 0) {
            continue;
        }

        const QString value = rec.value(index).toString();
        if (value.isEmpty() || foundLocalIds.contains(value)) {
            continue;
        }

        foundLocalIds.insert(value);
    }

    QStringList result;
    result.reserve(foundLocalIds.size());
    for (const auto & localId: foundLocalIds) {
        result << localId;
    }

    return result;
}

NoteList LocalStorageManagerPrivate::findNotesWithSearchQuery(
    const NoteSearchQuery & noteSearchQuery, const GetNoteOptions options,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::findNotesWithSearchQuery: "
            << noteSearchQuery << "\nWith resource metadata = "
            << ((options & GetNoteOption::WithResourceMetadata) ? "true"
                                                                : "false")
            << ", with resource binary data = "
            << ((options & GetNoteOption::WithResourceBinaryData) ? "true"
                                                                  : "false"));

    const QStringList foundLocalIds =
        findNoteLocalIdsWithSearchQuery(noteSearchQuery, errorDescription);

    if (foundLocalIds.isEmpty()) {
        return {};
    }

    QString joinedLocalIds;
    for (const auto & item: qAsConst(foundLocalIds)) {
        if (!joinedLocalIds.isEmpty()) {
            joinedLocalIds += QStringLiteral(", ");
        }

        joinedLocalIds += QStringLiteral("'");
        joinedLocalIds += sqlEscapeString(item);
        joinedLocalIds += QStringLiteral("'");
    }

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find notes with the note search query"));

    QString queryString =
        QString::fromUtf8("SELECT * FROM Notes WHERE localUid IN (%1)")
            .arg(joinedLocalIds);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (Q_UNLIKELY(!res)) {
        SET_ERROR();
        return {};
    }

    const bool withResourceMetadata =
        (options & GetNoteOption::WithResourceMetadata);

    const bool withResourceBinaryData =
        (options & GetNoteOption::WithResourceBinaryData);

    NoteList notes;
    notes.reserve(qMax(query.size(), 0));
    ErrorString error;

    while (query.next()) {
        notes.push_back(qevercloud::Note());
        auto & note = notes.back();
        note.setLocalId({});

        const QSqlRecord rec = query.record();

        error.clear();
        if (!fillNoteFromSqlRecord(rec, note, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't fetch note's tag ids"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return {};
        }

        error.clear();
        if (!findAndSetTagIdsPerNote(note, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't fetch note's tag ids"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return {};
        }

        if (withResourceMetadata) {
            error.clear();
            auto resources = listResourcesPerNoteLocalId(
                note.localId(), withResourceBinaryData, error);
            if (!error.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return {};
            }

            if (!resources.isEmpty()) {
                note.setResources(std::move(resources));
            }
        }

        if (note.guid()) {
            error.clear();
            auto sharedNotes = listSharedNotesPerNoteGuid(*note.guid(), error);
            if (!error.isEmpty()) {
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return {};
            }

            if (!sharedNotes.isEmpty()) {
                note.setSharedNotes(std::move(sharedNotes));
            }
        }

        error.clear();
        if (!checkNote(note, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't fetch note's resources"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return {};
        }
    }

    errorDescription.clear();
    return notes;
}

int LocalStorageManagerPrivate::tagCount(ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of tags in the local storage"));

    bool res = checkAndPrepareTagCountQuery();
    QSqlQuery & query = m_getTagCountQuery;
    if (!res) {
        SET_ERROR();
        return -1;
    }

    res = query.exec();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG("local_storage", "Found no tags in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);

    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

bool LocalStorageManagerPrivate::addTag(
    qevercloud::Tag & tag, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add tag to the local storage"));

    ErrorString error;
    if (!checkTag(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid tag: " << errorDescription << ", tag: " << tag);
        return false;
    }

    QString localId = tag.localId();

    QString column, id;
    bool shouldCheckTagExistence = true;
    if (tag.guid()) {
        column = QStringLiteral("guid");
        id = *tag.guid();

        if (localId.isEmpty()) {
            error.clear();
            if (getTagLocalIdForGuid(id, localId, error) && !localId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("found already existing tag"));
                errorDescription.details() = QStringLiteral("guid = ");
                errorDescription.details() += id;
                QNWARNING(
                    "local_storage", errorDescription << ", tag: " << tag);
                return false;
            }

            localId = UidGenerator::Generate();
            tag.setLocalId(localId);
            shouldCheckTagExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = tag.localId();
    }

    if (shouldCheckTagExistence &&
        rowExists(QStringLiteral("Tags"), column, id)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("tag already exists"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription << ", tag: " << tag);
        return false;
    }

    error.clear();
    if (!complementTagParentInfo(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription << ", tag: " << tag);
        return false;
    }

    error.clear();
    if (!insertOrReplaceTag(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription << ", tag: " << tag);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::updateTag(
    qevercloud::Tag & tag, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update tag in the local storage"));

    ErrorString error;
    if (!checkTag(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid tag: " << errorDescription << ", tag: " << tag);
        return false;
    }

    QString localId = tag.localId();

    QString column, id;
    bool shouldCheckTagExistence = true;

    if (tag.guid()) {
        column = QStringLiteral("guid");
        id = *tag.guid();

        if (localId.isEmpty()) {
            error.clear();
            if (!getTagLocalIdForGuid(id, localId, error) || localId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING(
                    "local_storage", errorDescription << ", tag: " << tag);
                return false;
            }

            tag.setLocalId(localId);
            shouldCheckTagExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = tag.localId();
    }

    if (shouldCheckTagExistence &&
        !rowExists(QStringLiteral("Tags"), column, id)) {
        bool foundByOtherColumn = false;
        if (tag.guid()) {
            QNDEBUG(
                "local_storage",
                "Failed to find tag by guid within the local storage, trying "
                    << "to find it by local id");
            column = QStringLiteral("localUid");
            id = tag.localId();
            foundByOtherColumn = rowExists(QStringLiteral("Tags"), column, id);
        }

        if (!foundByOtherColumn) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("tag was not found in the local storage"));
            errorDescription.details() = column;
            errorDescription.details() += QStringLiteral(" = ");
            errorDescription.details() += id;
            QNWARNING("local_storage", errorDescription << ", tag: " << tag);
            return false;
        }
    }

    error.clear();
    if (!complementTagParentInfo(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription << ", tag: " << tag);
        return false;
    }

    error.clear();
    if (!insertOrReplaceTag(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription << ", tag: " << tag);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::findTag(
    qevercloud::Tag & tag, ErrorString & errorDescription) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::findTag");

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find tag in the local storage"));

    bool searchingByName = false;

    QString column, value;
    if (tag.guid()) {
        column = QStringLiteral("guid");
        value = *tag.guid();

        if (!checkGuid(value)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("tag's guid is invalid"));
            errorDescription.details() = value;
            QNWARNING("local_storage", errorDescription << ", tag: " << tag);
            return false;
        }
    }
    else if (tag.localId().isEmpty()) {
        if (!tag.name()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("need either guid or local id "
                           "or name as a search criteria"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        column = QStringLiteral("nameLower");
        value = tag.name()->toLower();
        m_stringUtils.removeDiacritics(value);
        searchingByName = true;
    }
    else {
        column = QStringLiteral("localUid");
        value = tag.localId();
    }

    value = sqlEscapeString(value);

    QString queryString =
        QString::fromUtf8(
            "SELECT localUid, guid, linkedNotebookGuid, "
            "updateSequenceNumber, name, parentGuid, "
            "parentLocalUid, isDirty, isLocal, isLocal, isFavorited "
            "FROM Tags WHERE (%1 = '%2'")
            .arg(column, value);

    if (searchingByName) {
        QString linkedNotebookGuid =
            tag.linkedNotebookGuid().value_or(QString{});
        if (!linkedNotebookGuid.isEmpty()) {
            linkedNotebookGuid = sqlEscapeString(linkedNotebookGuid);
            queryString += QString::fromUtf8(" AND linkedNotebookGuid = '%1')")
                               .arg(sqlEscapeString(linkedNotebookGuid));
        }
        else {
            queryString += QStringLiteral(" AND linkedNotebookGuid IS NULL)");
        }
    }
    else {
        queryString += QStringLiteral(")");
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    bool foundTag = false;

    while (query.next()) {
        const QSqlRecord record = query.record();

        qevercloud::Tag result;
        ErrorString error;
        if (!fillTagFromSqlRecord(record, result, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription << ", tag: " << tag);
            return false;
        }

        if (searchingByName && result.name() &&
            (result.name()->toLower() != tag.name()->toLower()))
        {
            continue;
        }

        tag = result;
        foundTag = true;
        break;
    }

    return foundTag;
}

QList<qevercloud::Tag> LocalStorageManagerPrivate::listAllTagsPerNote(
    const qevercloud::Note & note, ErrorString & errorDescription,
    const ListObjectsOptions & flag, const std::size_t limit,
    const std::size_t offset, const ListTagsOrder & order,
    const OrderDirection & orderDirection) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::listAllTagsPerNote");

    QList<qevercloud::Tag> tags;

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list all tags per note from the local storage"));

    QString column, id;
    if (note.guid()) {
        column = QStringLiteral("note");
        id = *note.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("note's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return tags;
        }
    }
    else {
        column = QStringLiteral("localNote");
        id = note.localId();
    }

    /**
     * Will run all the queries from this method and its sub-methods within
     * a single transaction to prevent multiple drops and re-obtainings of
     * the shared lock
     */
    const Transaction transaction(
        m_sqlDatabase, *this, Transaction::Type::Selection);

    Q_UNUSED(transaction)

    id = sqlEscapeString(id);

    QString queryString =
        QString::fromUtf8("SELECT localTag FROM NoteTags WHERE %1 = '%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        SET_ERROR();
        return tags;
    }

    if (query.size() == 0) {
        QNDEBUG("local_storage", "No tags for this note were found");
        return tags;
    }

    QStringList tagLocalIds;
    tagLocalIds.reserve(std::max(query.size(), 0));

    while (query.next()) {
        tagLocalIds << QString();
        QString & tagLocalId = tagLocalIds.back();
        tagLocalId = query.value(0).toString();

        if (tagLocalId.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("internal error: no tag's local "
                           "id in the result of SQL query"));
            tags.clear();
            return tags;
        }
    }

    QString noteGuidSqlQueryCondition = QStringLiteral("localUid IN (");
    const int numTagLocalIds = tagLocalIds.size();
    for (int i = 0; i < numTagLocalIds; ++i) {
        noteGuidSqlQueryCondition +=
            QString::fromUtf8("'%1'").arg(sqlEscapeString(tagLocalIds[i]));

        if (i != (numTagLocalIds - 1)) {
            noteGuidSqlQueryCondition += QStringLiteral(", ");
        }
    }
    noteGuidSqlQueryCondition += QStringLiteral(")");

    ErrorString error;
    tags = listObjects<qevercloud::Tag, ListTagsOrder>(
        flag, error, limit, offset, order, orderDirection,
        noteGuidSqlQueryCondition);

    if (tags.isEmpty() && !error.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
    }

    return tags;
}

QList<qevercloud::Tag> LocalStorageManagerPrivate::listAllTags(
    ErrorString & errorDescription, const std::size_t limit,
    const std::size_t offset, const ListTagsOrder & order,
    const OrderDirection & orderDirection,
    std::optional<QString> linkedNotebookGuid) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::listAllTags");
    return listTags(
        ListObjectsOption::ListAll, errorDescription, limit, offset, order,
        orderDirection, std::move(linkedNotebookGuid));
}

QList<qevercloud::Tag> LocalStorageManagerPrivate::listTags(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const std::size_t limit, const std::size_t offset,
    const ListTagsOrder & order, const OrderDirection & orderDirection,
    std::optional<QString> linkedNotebookGuid) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listTags: flag = " << flag);

    QString linkedNotebookGuidSqlQueryCondition;
    if (linkedNotebookGuid) {
        linkedNotebookGuidSqlQueryCondition =
            (linkedNotebookGuid->isEmpty()
                 ? QStringLiteral("linkedNotebookGuid IS NULL")
                 : QString::fromUtf8("linkedNotebookGuid = '%1'")
                       .arg(sqlEscapeString(*linkedNotebookGuid)));
    }

    return listObjects<qevercloud::Tag, ListTagsOrder>(
        flag, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuidSqlQueryCondition);
}

QList<std::pair<qevercloud::Tag, QStringList>>
LocalStorageManagerPrivate::listTagsWithNoteLocalIds(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const std::size_t limit, const std::size_t offset,
    const ListTagsOrder & order, const OrderDirection & orderDirection,
    std::optional<QString> linkedNotebookGuid) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listTagsWithNoteLocalIds: flag = "
            << flag);

    QString linkedNotebookGuidSqlQueryCondition;
    if (linkedNotebookGuid) {
        linkedNotebookGuidSqlQueryCondition =
            (linkedNotebookGuid->isEmpty()
                 ? QStringLiteral("linkedNotebookGuid IS NULL")
                 : QString::fromUtf8("linkedNotebookGuid = '%1'")
                       .arg(sqlEscapeString(*linkedNotebookGuid)));
    }

    using ListTagsOrder = ListTagsOrder;

    return listObjects<std::pair<qevercloud::Tag, QStringList>, ListTagsOrder>(
        flag, errorDescription, limit, offset, order, orderDirection,
        linkedNotebookGuidSqlQueryCondition);
}

bool LocalStorageManagerPrivate::expungeTag(
    qevercloud::Tag & tag, QStringList & expungedChildTagLocalIds,
    ErrorString & errorDescription)
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::expungeTag: " << tag);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge tag from the local storage"));

    expungedChildTagLocalIds.clear();

    QString localId = tag.localId();

    QString column, parentColumn, id;
    bool shouldCheckTagExistence = true;

    if (tag.guid()) {
        column = QStringLiteral("guid");
        parentColumn = QStringLiteral("parentGuid");
        id = *tag.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP("tag's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            ErrorString error;
            if (!getTagLocalIdForGuid(id, localId, error) || localId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("tag to be expunged was not found in the local "
                               "storage"));
                errorDescription.details() = QStringLiteral("local id = ");
                errorDescription.details() += localId;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            tag.setLocalId(localId);
            shouldCheckTagExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        parentColumn = QStringLiteral("parentLocalUid");
        id = tag.localId();
    }

    id = sqlEscapeString(id);

    if (shouldCheckTagExistence &&
        !rowExists(QStringLiteral("Tags"), column, id)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP(
            "tag to be expunged was not found in the local storage"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    QSqlQuery query(m_sqlDatabase);

    QString findChildTagsQueryString =
        QString::fromUtf8("SELECT localUid FROM Tags WHERE %1='%2'")
            .arg(parentColumn, id);

    bool res = query.exec(findChildTagsQueryString);
    DATABASE_CHECK_AND_SET_ERROR()

    while (query.next()) {
        const QSqlRecord record = query.record();

        const int index = record.indexOf(QStringLiteral("localUid"));
        if (Q_UNLIKELY(index < 0)) {
            QNDEBUG(
                "local_storage",
                "Index of localUid within the SQL record is negative");
            continue;
        }

        const QVariant value = record.value(index);
        if (Q_UNLIKELY(value.isNull())) {
            QNDEBUG("local_storage", "The value from the SQL record is null");
            continue;
        }

        QString childTagLocalId = value.toString();
        if (Q_UNLIKELY(childTagLocalId.isEmpty())) {
            QNDEBUG(
                "local_storage",
                "The string from the value from SQL record is empty");
            continue;
        }

        expungedChildTagLocalIds << childTagLocalId;
    }

    // Removing child tags
    QString queryString = QString::fromUtf8("DELETE FROM Tags WHERE %1='%2'")
                              .arg(parentColumn, id);

    res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    queryString =
        QString::fromUtf8("DELETE FROM Tags WHERE %1='%2'").arg(column, id);

    res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::expungeNotelessTagsFromLinkedNotebooks(
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge tags from linked notebooks "
                   "not connected to any notes"));

    const QString queryString = QStringLiteral(
        "DELETE FROM Tags WHERE ((linkedNotebookGuid IS NOT NULL) "
        "AND (localUid NOT IN (SELECT localTag FROM NoteTags)))");

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

int LocalStorageManagerPrivate::enResourceCount(
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of resources "
                   "in the local storage"));

    bool res = checkAndPrepareResourceCountQuery();
    QSqlQuery & query = m_getResourceCountQuery;
    if (!res) {
        SET_ERROR();
        return -1;
    }

    res = query.exec();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG("local_storage", "Found no resources in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

bool LocalStorageManagerPrivate::findEnResource(
    qevercloud::Resource & resource, const GetResourceOptions options,
    ErrorString & errorDescription) const
{
    QNTRACE(
        "local_storage",
        "LocalStorageManagerPrivate::findEnResource: " << resource);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find resource in the local storage"));

    QString column, id;
    if (resource.guid()) {
        column = QStringLiteral("resourceGuid");
        id = *resource.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("resource's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }
    else {
        column = QStringLiteral("resourceLocalUid");
        id = resource.localId();
    }

    id = sqlEscapeString(id);

    QString queryString = QStringLiteral(
        "SELECT Resources.resourceLocalUid, resourceGuid, "
        "noteGuid, resourceUpdateSequenceNumber, resourceIsDirty, "
        "dataSize, dataHash, mime, width, height, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, alternateDataHash, "
        "resourceIndexInNote, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, fileName, "
        "attachment, resourceKey, resourceMapKey, resourceValue, "
        "localNote, recognitionDataBody");

    queryString +=
        QString::fromUtf8(
            " FROM Resources "
            "LEFT OUTER JOIN ResourceAttributes ON "
            "Resources.resourceLocalUid = "
            "ResourceAttributes.resourceLocalUid "
            "LEFT OUTER JOIN ResourceAttributesApplicationDataKeysOnly ON "
            "Resources.resourceLocalUid = "
            "ResourceAttributesApplicationDataKeysOnly.resourceLocalUid "
            "LEFT OUTER JOIN ResourceAttributesApplicationDataFullMap ON "
            "Resources.resourceLocalUid = "
            "ResourceAttributesApplicationDataFullMap.resourceLocalUid "
            "LEFT OUTER JOIN NoteResources ON "
            "Resources.resourceLocalUid = NoteResources.localResource "
            "WHERE Resources.%1 = '%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    qevercloud::Resource foundResource;

    std::size_t counter = 0;
    while (query.next()) {
        ErrorString error;
        int indexInNote = -1;
        if (!fillResourceFromSqlRecord(
                query.record(), foundResource, indexInNote, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }
        ++counter;
    }

    if (!counter) {
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    if ((options & GetResourceOption::WithBinaryData) &&
        !readResourceDataFromFiles(foundResource, errorDescription))
    {
        return false;
    }

    resource = foundResource;
    return true;
}

bool LocalStorageManagerPrivate::expungeEnResource(
    qevercloud::Resource & resource, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge resource from the local storage"));

    ErrorString error;
    QString noteLocalId;
    if (!getNoteLocalIdFromResource(resource, noteLocalId, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("note's local id corresponding to the resource is "
                       "empty"));
        errorDescription.details() = QStringLiteral("local id = ");
        errorDescription.details() += noteLocalId;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    resource.setNoteLocalId(noteLocalId);

    QString localId = resource.localId();

    QString column, id;
    bool shouldCheckResourceExistence = true;

    if (resource.guid()) {
        column = QStringLiteral("resourceGuid");
        id = *resource.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("resource's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (localId.isEmpty()) {
            error.clear();
            if (!getResourceLocalIdForGuid(id, localId, error) ||
                localId.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("resource to be updated was not found in "
                               "the local storage"));
                errorDescription.details() = QStringLiteral("guid = ");
                errorDescription.details() += id;
                QNERROR("local_storage", errorDescription);
                return false;
            }

            resource.setLocalId(localId);
            shouldCheckResourceExistence = false;
        }
    }
    else {
        column = QStringLiteral("resourceLocalUid");
        id = resource.localId();
    }

    id = sqlEscapeString(id);

    if (shouldCheckResourceExistence &&
        !rowExists(QStringLiteral("Resources"), column, id))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("resource to be expunged was not found in the local "
                       "storage"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString queryString =
        QString::fromUtf8("DELETE FROM Resources WHERE %1 = '%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    error.clear();
    if (!removeResourceDataFiles(resource, error)) {
        errorDescription = errorPrefix;
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        return false;
    }

    return true;
}

int LocalStorageManagerPrivate::savedSearchCount(
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't get the number of saved searches in the local "
                   "storage"));

    QSqlQuery & query = m_getSavedSearchCountQuery;
    bool res = checkAndPrepareGetSavedSearchCountQuery();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    res = query.exec();
    if (!res) {
        SET_ERROR();
        return -1;
    }

    if (!query.next()) {
        QNDEBUG(
            "local_storage", "Found no saved searches in the local storage");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        SET_INT_CONVERSION_ERROR();
        return -1;
    }

    return count;
}

bool LocalStorageManagerPrivate::addSavedSearch(
    qevercloud::SavedSearch & search, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add saved search to the local storage"));

    ErrorString error;
    if (!checkSavedSearch(search, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid SavedSearch: " << search << "\nError: " << error);
        return false;
    }

    QString localId = search.localId();

    QString column, id;
    bool shouldCheckSearchExistence = true;

    if (search.guid()) {
        column = QStringLiteral("guid");
        id = *search.guid();

        if (localId.isEmpty()) {
            error.clear();
            if (getSavedSearchLocalIdForGuid(id, localId, error) &&
                !localId.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search already exists"));
                errorDescription.details() = column;
                errorDescription.details() += QStringLiteral(" = ");
                errorDescription.details() += id;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            localId = UidGenerator::Generate();
            search.setLocalId(localId);
            shouldCheckSearchExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = search.localId();
    }

    if (shouldCheckSearchExistence &&
        rowExists(QStringLiteral("SavedSearches"), column, id))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("saved search already exists"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    error.clear();
    if (!insertOrReplaceSavedSearch(search, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", error);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::updateSavedSearch(
    qevercloud::SavedSearch & search, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update saved search in the local storage"));

    ErrorString error;
    if (!checkSavedSearch(search, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid SavedSearch: " << search << "\nError: " << error);
        return false;
    }

    QString localId = search.localId();

    QString column, id;
    bool shouldCheckSearchExistence = true;

    if (search.guid()) {
        column = QStringLiteral("guid");
        id = *search.guid();

        if (localId.isEmpty()) {
            error.clear();
            if (!getSavedSearchLocalIdForGuid(id, localId, error) ||
                localId.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search to be updated was not found in "
                               "the local storage"));
                errorDescription.details() = column;
                errorDescription.details() += QStringLiteral(" = ");
                errorDescription.details() += id;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            search.setLocalId(localId);
            shouldCheckSearchExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = search.localId();
    }

    if (shouldCheckSearchExistence &&
        !rowExists(QStringLiteral("SavedSearches"), column, id))
    {
        bool foundByOtherColumn = false;

        if (search.guid()) {
            QNDEBUG(
                "local_storage",
                "Failed to find the saved search by guid within the local "
                    << "storage, trying to find it by local id");
            column = QStringLiteral("localUid");
            id = search.localId();

            foundByOtherColumn =
                rowExists(QStringLiteral("SavedSearches"), column, id);
        }

        if (!foundByOtherColumn) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("saved search to be updated was not found in "
                           "the local storage database"));
            errorDescription.details() = column;
            errorDescription.details() += QStringLiteral(" = ");
            errorDescription.details() += id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    error.clear();
    if (!insertOrReplaceSavedSearch(search, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", error);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::findSavedSearch(
    qevercloud::SavedSearch & search, ErrorString & errorDescription) const
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::findSavedSearch");

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't find saved search in the local storage"));

    QString column, value;
    if (search.guid()) {
        column = QStringLiteral("guid");
        value = *search.guid();

        if (!checkGuid(value)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("saved search's guid is invalid"));
            errorDescription.details() = value;
            return false;
        }
    }
    else if (search.localId().isEmpty()) {
        if (!search.name()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("need either guid or local id or name as search "
                           "criteria"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        column = QStringLiteral("nameLower");
        value = search.name()->toLower();
    }
    else {
        column = QStringLiteral("localUid");
        value = search.localId();
    }

    value = sqlEscapeString(value);

    const QString queryString =
        QString::fromUtf8(
            "SELECT localUid, guid, name, query, format, "
            "updateSequenceNumber, isDirty, isLocal, "
            "includeAccount, includePersonalLinkedNotebooks, "
            "includeBusinessLinkedNotebooks, isFavorited FROM "
            "SavedSearches WHERE %1 = '%2'")
            .arg(column, value);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    qevercloud::SavedSearch result;
    ErrorString error;
    if (!fillSavedSearchFromSqlRecord(query.record(), result, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    search = result;
    return true;
}

QList<qevercloud::SavedSearch> LocalStorageManagerPrivate::listAllSavedSearches(
    ErrorString & errorDescription, const std::size_t limit,
    const std::size_t offset, const ListSavedSearchesOrder & order,
    const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage", "LocalStorageManagerPrivate::listAllSavedSearches");

    return listSavedSearches(
        ListObjectsOption::ListAll, errorDescription, limit, offset, order,
        orderDirection);
}

QList<qevercloud::SavedSearch> LocalStorageManagerPrivate::listSavedSearches(
    const ListObjectsOptions flag, ErrorString & errorDescription,
    const std::size_t limit, const std::size_t offset,
    const ListSavedSearchesOrder & order,
    const OrderDirection & orderDirection) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::listSavedSearches: flag = " << flag);

    using ListSavedSearchesOrder = ListSavedSearchesOrder;

    return listObjects<qevercloud::SavedSearch, ListSavedSearchesOrder>(
        flag, errorDescription, limit, offset, order, orderDirection);
}

bool LocalStorageManagerPrivate::expungeSavedSearch(
    qevercloud::SavedSearch & search, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::expungeSavedSearch: saved search = "
            << search);

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't expunge saved search from the local storage"));

    ErrorString error;
    if (!checkSavedSearch(search, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage",
            "Found invalid SavedSearch: " << search << "\nError: " << error);
        return false;
    }

    QString localId = search.localId();

    QString column, id;
    bool shouldCheckSearchExistence = true;

    if (search.guid()) {
        column = QStringLiteral("guid");
        id = *search.guid();

        if (localId.isEmpty()) {
            error.clear();
            if (!getSavedSearchLocalIdForGuid(id, localId, error) ||
                localId.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search to be expunged was not found in "
                               "the local storage"));
                errorDescription.details() = column;
                errorDescription.details() += QStringLiteral(" = ");
                errorDescription.details() += id;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            search.setLocalId(localId);
            shouldCheckSearchExistence = false;
        }
    }
    else {
        column = QStringLiteral("localUid");
        id = search.localId();
    }

    id = sqlEscapeString(id);

    if (shouldCheckSearchExistence &&
        !rowExists(QStringLiteral("SavedSearches"), column, id))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("saved search to be expunged was not found in the local "
                       "storage"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString queryString =
        QString::fromUtf8("DELETE FROM SavedSearches WHERE %1='%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

qint32 LocalStorageManagerPrivate::accountHighUsn(
    const QString & linkedNotebookGuid, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::accountHighUsn: linked notebook guid = "
            << linkedNotebookGuid);

    qint32 updateSequenceNumber = 0;

    QList<HighUsnRequestData> tablesAndUsnColumns;
    if (linkedNotebookGuid.isEmpty()) {
        tablesAndUsnColumns.reserve(6);
    }
    else {
        tablesAndUsnColumns.reserve(4);
    }

    QString queryCondition = QStringLiteral("WHERE linkedNotebookGuid");
    if (linkedNotebookGuid.isEmpty()) {
        queryCondition += QStringLiteral(" IS NULL");
    }
    else {
        queryCondition +=
            QString::fromUtf8("='%1'").arg(sqlEscapeString(linkedNotebookGuid));
    }

#define ADD_TABLE_AND_USN_COLUMN(tableName, usnColumnName)                     \
    tablesAndUsnColumns << HighUsnRequestData(                                 \
        tableName, usnColumnName, queryCondition)

    ADD_TABLE_AND_USN_COLUMN(
        QStringLiteral("Notebooks"), QStringLiteral("updateSequenceNumber"));

    ADD_TABLE_AND_USN_COLUMN(
        QStringLiteral("Tags"), QStringLiteral("updateSequenceNumber"));

    // Separate query condition is required for notes table
    queryCondition = QStringLiteral(
        "WHERE notebookLocalUid IN (SELECT DISTINCT localUid "
        "FROM Notebooks WHERE linkedNotebookGuid");

    if (linkedNotebookGuid.isEmpty()) {
        queryCondition += QStringLiteral(" IS NULL)");
    }
    else {
        queryCondition += QString::fromUtf8("='%1')").arg(
            sqlEscapeString(linkedNotebookGuid));
    }

    ADD_TABLE_AND_USN_COLUMN(
        QStringLiteral("Notes"), QStringLiteral("updateSequenceNumber"));

    // Separate query condition is required for resources table
    queryCondition = QStringLiteral(
        "WHERE noteLocalUid IN (SELECT DISTINCT localUid FROM "
        "Notes WHERE notebookLocalUid IN ");

    queryCondition += QStringLiteral(
        "(SELECT DISTINCT localUid FROM Notebooks "
        "WHERE linkedNotebookGuid");

    if (linkedNotebookGuid.isEmpty()) {
        queryCondition += QStringLiteral(" IS NULL))");
    }
    else {
        queryCondition += QString::fromUtf8("='%1'))").arg(
            sqlEscapeString(linkedNotebookGuid));
    }

    ADD_TABLE_AND_USN_COLUMN(
        QStringLiteral("Resources"),
        QStringLiteral("resourceUpdateSequenceNumber"));

    /**
     * No query condition is required for linked notebooks and saved searches
     * tables + only need to consider them for highest USN from user's own
     * account, not from some linked notebook
     */

    if (linkedNotebookGuid.isEmpty()) {
        queryCondition.clear();

        ADD_TABLE_AND_USN_COLUMN(
            QStringLiteral("LinkedNotebooks"),
            QStringLiteral("updateSequenceNumber"));

        ADD_TABLE_AND_USN_COLUMN(
            QStringLiteral("SavedSearches"),
            QStringLiteral("updateSequenceNumber"));
    }

#undef ADD_TABLE_AND_USN_COLUMN

    for (const auto & requestData: tablesAndUsnColumns) {
        qint32 usn = 0;
        if (!updateSequenceNumberFromTable(
                requestData.m_tableName, requestData.m_usnColumnName,
                requestData.m_queryCondition, usn, errorDescription))
        {
            return -1;
        }

        updateSequenceNumber = std::max(updateSequenceNumber, usn);

        QNTRACE(
            "local_storage",
            "Max update sequence number from table "
                << requestData.m_tableName << ": " << usn
                << ", overall max USN so far: " << updateSequenceNumber);
    }

    QNDEBUG("local_storage", "Max USN = " << updateSequenceNumber);
    return updateSequenceNumber;
}

bool LocalStorageManagerPrivate::updateSequenceNumberFromTable(
    const QString & tableName, const QString & usnColumnName,
    const QString & queryCondition, qint32 & usn,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::updateSequenceNumberFromTable: "
            << tableName << ", usn column name = " << usnColumnName
            << ", query condition = " << queryCondition);

    const ErrorString errorPrefix(
        QT_TR_NOOP("failed to get the update sequence number "
                   "from one of local storage database tables"));

    QString queryString = QStringLiteral("SELECT MAX(") + usnColumnName +
        QStringLiteral(") FROM ") + tableName;

    if (!queryCondition.isEmpty()) {
        queryString += QStringLiteral(" ") + queryCondition;
    }

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        QNDEBUG("local_storage", "No query result for table " << tableName);
        // NOTE: consider this the acceptable result, the table might be empty
        usn = 0;
        return true;
    }

    bool conversionResult = false;
    const QVariant value = query.value(0);
    usn = value.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        QNDEBUG("local_storage", "Failed to convert the query result to int");
        /**
         * NOTE: surprisingly, this also seems to happen when the table on which
         * the query runs is empty, so need to handle it gently: don't return
         * error, return zero instead
         */
        usn = 0;
    }

    return true;
}

bool LocalStorageManagerPrivate::compactLocalStorage(
    ErrorString & errorDescription)
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::compactLocalStorage");

    clearCachedQueries();

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't compact local storage database"));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(QStringLiteral("VACUUM"));
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

void LocalStorageManagerPrivate::processPostTransactionException(
    ErrorString message, QSqlError error)
{
    QNERROR("local_storage", message << ": " << error);
    message.details() += error.text();
    throw DatabaseRequestException(message);
}

bool LocalStorageManagerPrivate::addEnResource(
    qevercloud::Resource & resource, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't add resource to the local storage database"));

    ErrorString error;
    if (!checkResource(resource, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", "Found invalid resource: " << resource);
        return false;
    }

    if (!resource.noteGuid() && resource.noteLocalId().isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("both resource's note local id and note guid are "
                       "empty"));
        QNWARNING(
            "local_storage", errorDescription << ", resource: " << resource);
        return false;
    }

    error.clear();
    if (!complementResourceNoteIds(resource, error)) {
        return false;
    }

    int resourceIndexInNote = -1;

    QString noteLocalId = resource.noteLocalId();
    noteLocalId = sqlEscapeString(noteLocalId);

    const QString queryString =
        QString::fromUtf8(
            "SELECT COUNT(*) FROM NoteResources WHERE localNote = '%1'")
            .arg(noteLocalId);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        bool conversionResult = false;
        resourceIndexInNote = query.record().value(0).toInt(&conversionResult);
        if (!conversionResult) {
            SET_INT_CONVERSION_ERROR();
            return false;
        }
    }
    else {
        resourceIndexInNote = 0;
    }

    QString resourceLocalId = resource.localId();

    QString column, id;
    bool shouldCheckResourceExistence = true;

    if (resource.guid()) {
        column = QStringLiteral("resourceGuid");
        id = *resource.guid();

        if (resourceLocalId.isEmpty()) {
            error.clear();
            if (getResourceLocalIdForGuid(id, resourceLocalId, error) &&
                !resourceLocalId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("resource already exists"));
                errorDescription.details() = column;
                errorDescription.details() += QStringLiteral(" = ");
                errorDescription.details() += id;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            resourceLocalId = UidGenerator::Generate();
            resource.setLocalId(resourceLocalId);
            shouldCheckResourceExistence = false;
        }
    }
    else {
        column = QStringLiteral("resourceLocalUid");
        id = resource.localId();
    }

    if (shouldCheckResourceExistence &&
        rowExists(QStringLiteral("Resources"), column, id))
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP("resource already exists"));
        errorDescription.details() = column;
        errorDescription.details() += QStringLiteral(" = ");
        errorDescription.details() += id;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    error.clear();
    if (!insertOrReplaceResource(resource, resourceIndexInNote, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::updateEnResource(
    qevercloud::Resource & resource, ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't update resource in the local storage database"));

    ErrorString error;
    if (!checkResource(resource, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", "Found invalid resource: " << resource);
        return false;
    }

    if (!resource.noteGuid() && resource.noteLocalId().isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(QT_TR_NOOP(
            "both resource's note local id and note guid are empty"));
        QNWARNING(
            "local_storage", errorDescription << ", resource: " << resource);
        return false;
    }

    error.clear();
    if (!complementResourceNoteIds(resource, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    int resourceIndexInNote = -1;

    QString noteLocalId = resource.noteLocalId();
    noteLocalId = sqlEscapeString(noteLocalId);

    const QString queryString =
        QString::fromUtf8(
            "SELECT COUNT(*) FROM NoteResources WHERE localNote = '%1'")
            .arg(noteLocalId);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        bool conversionResult = false;
        resourceIndexInNote = query.record().value(0).toInt(&conversionResult);
        if (!conversionResult) {
            SET_INT_CONVERSION_ERROR();
            return false;
        }
    }
    else {
        errorDescription.setBase(
            QT_TR_NOOP("Can't update resource: resource index in note was not "
                       "found"));
        errorDescription.details() += QStringLiteral("note local id = ");
        errorDescription.details() += noteLocalId;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    QString resourceLocalId = resource.localId();

    QString column, id;
    bool shouldCheckResourceExistence = true;

    if (resource.guid()) {
        column = QStringLiteral("resourceGuid");
        id = *resource.guid();

        if (resourceLocalId.isEmpty()) {
            error.clear();
            if (!getResourceLocalIdForGuid(id, resourceLocalId, error) ||
                resourceLocalId.isEmpty())
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(
                    QT_TR_NOOP("resource to be updated was not found in "
                               "the local storage database"));
                errorDescription.details() = column;
                errorDescription.details() += QStringLiteral(" = ");
                errorDescription.details() += id;
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            resource.setLocalId(resourceLocalId);
            shouldCheckResourceExistence = false;
        }
    }
    else {
        column = QStringLiteral("resourceLocalUid");
        id = resource.localId();
    }

    if (shouldCheckResourceExistence &&
        !rowExists(QStringLiteral("Resources"), column, id))
    {
        bool foundByOtherColumn = false;

        if (resource.guid()) {
            QNDEBUG(
                "local_storage",
                "Failed to find the resource by guid within the local storage, "
                    << "trying to find it by local id");
            column = QStringLiteral("resourceLocalUid");
            id = resource.localId();
            foundByOtherColumn =
                rowExists(QStringLiteral("Resources"), column, id);
        }

        if (!foundByOtherColumn) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("resource to be updated was not found in the local "
                           "storage"));
            errorDescription.details() = column;
            errorDescription.details() += QStringLiteral(" = ");
            errorDescription.details() += id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    error.clear();
    if (!insertOrReplaceResource(resource, resourceIndexInNote, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

void LocalStorageManagerPrivate::unlockDatabaseFile()
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::unlockDatabaseFile: "
            << m_databaseFilePath);

#ifndef Q_OS_WIN
    if (m_databaseFilePath.isEmpty()) {
        QNDEBUG("local_storage", "No database file, nothing to do");
        return;
    }

    try {
        m_databaseFileLock.unlock();
    }
    catch (boost::interprocess::interprocess_exception & exc) {
        QNWARNING(
            "local_storage",
            "Caught exception trying to unlock "
                << "the database file: error = " << exc.get_error_code()
                << ", error message = " << exc.what()
                << "; native error = " << exc.get_native_error());
    }
#endif
}

bool LocalStorageManagerPrivate::createTables(ErrorString & errorDescription)
{
    QSqlQuery query(m_sqlDatabase);

    // Checking whether auxiliary table exists
    bool res = query.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE name='Auxiliary'"));

    ErrorString errorPrefix(
        QT_TR_NOOP("Can't check whether Auxiliary table exists"));

    DATABASE_CHECK_AND_SET_ERROR()
    const bool auxiliaryTableExists = query.next();

    QNDEBUG(
        "local_storage",
        "Auxiliary table "
            << (auxiliaryTableExists ? "already exists" : "doesn't exist yet"));

    if (!auxiliaryTableExists) {
        res = query.exec(
            QStringLiteral("CREATE TABLE Auxiliary("
                           "  lock    CHAR(1) PRIMARY KEY  NOT NULL DEFAULT "
                           "'X' CHECK (lock='X'), "
                           "  version INTEGER              NOT NULL DEFAULT 2"
                           ")"));
        errorPrefix.setBase(QT_TR_NOOP("Can't create Auxiliary table"));
        DATABASE_CHECK_AND_SET_ERROR()

        res = query.exec(
            QStringLiteral("INSERT INTO Auxiliary (version) VALUES(2)"));
        errorPrefix.setBase(QT_TR_NOOP("Can't set version to Auxiliary table"));
        DATABASE_CHECK_AND_SET_ERROR()
    }

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Users("
        "  id                           INTEGER PRIMARY KEY NOT NULL UNIQUE, "
        "  username                     TEXT                DEFAULT NULL, "
        "  email                        TEXT                DEFAULT NULL, "
        "  name                         TEXT                DEFAULT NULL, "
        "  timezone                     TEXT                DEFAULT NULL, "
        "  privilege                    INTEGER             DEFAULT NULL, "
        "  serviceLevel                 INTEGER             DEFAULT NULL, "
        "  userCreationTimestamp        INTEGER             DEFAULT NULL, "
        "  userModificationTimestamp    INTEGER             DEFAULT NULL, "
        "  userIsDirty                  INTEGER             NOT NULL, "
        "  userIsLocal                  INTEGER             NOT NULL, "
        "  userDeletionTimestamp        INTEGER             DEFAULT NULL, "
        "  userIsActive                 INTEGER             DEFAULT NULL, "
        "  userShardId                  TEXT                DEFAULT NULL, "
        "  userPhotoUrl                 TEXT                DEFAULT NULL, "
        "  userPhotoLastUpdateTimestamp INTEGER             DEFAULT NULL"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create Users table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS UserAttributes("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  defaultLocationName        TEXT                  DEFAULT NULL, "
        "  defaultLatitude            REAL                  DEFAULT NULL, "
        "  defaultLongitude           REAL                  DEFAULT NULL, "
        "  preactivation              INTEGER               DEFAULT NULL, "
        "  incomingEmailAddress       TEXT                  DEFAULT NULL, "
        "  comments                   TEXT                  DEFAULT NULL, "
        "  dateAgreedToTermsOfService INTEGER               DEFAULT NULL, "
        "  maxReferrals               INTEGER               DEFAULT NULL, "
        "  referralCount              INTEGER               DEFAULT NULL, "
        "  refererCode                TEXT                  DEFAULT NULL, "
        "  sentEmailDate              INTEGER               DEFAULT NULL, "
        "  sentEmailCount             INTEGER               DEFAULT NULL, "
        "  dailyEmailLimit            INTEGER               DEFAULT NULL, "
        "  emailOptOutDate            INTEGER               DEFAULT NULL, "
        "  partnerEmailOptInDate      INTEGER               DEFAULT NULL, "
        "  preferredLanguage          TEXT                  DEFAULT NULL, "
        "  preferredCountry           TEXT                  DEFAULT NULL, "
        "  clipFullPage               INTEGER               DEFAULT NULL, "
        "  twitterUserName            TEXT                  DEFAULT NULL, "
        "  twitterId                  TEXT                  DEFAULT NULL, "
        "  groupName                  TEXT                  DEFAULT NULL, "
        "  recognitionLanguage        TEXT                  DEFAULT NULL, "
        "  referralProof              TEXT                  DEFAULT NULL, "
        "  educationalDiscount        INTEGER               DEFAULT NULL, "
        "  businessAddress            TEXT                  DEFAULT NULL, "
        "  hideSponsorBilling         INTEGER               DEFAULT NULL, "
        "  useEmailAutoFiling         INTEGER               DEFAULT NULL, "
        "  reminderEmailConfig        INTEGER               DEFAULT NULL, "
        "  emailAddressLastConfirmed  INTEGER               DEFAULT NULL, "
        "  passwordUpdated            INTEGER               DEFAULT NULL, "
        "  salesforcePushEnabled      INTEGER               DEFAULT NULL, "
        "  shouldLogClientEvent       INTEGER               DEFAULT NULL)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create UserAttributes table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS UserAttributesViewedPromotions("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  promotion               TEXT                    DEFAULT NULL)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create UserAttributesViewedPromotions table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS UserAttributesRecentMailedAddresses("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  address                 TEXT                    DEFAULT NULL)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create UserAttributesRecentMailedAddresses table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Accounting("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  uploadLimitEnd              INTEGER             DEFAULT NULL, "
        "  uploadLimitNextMonth        INTEGER             DEFAULT NULL, "
        "  premiumServiceStatus        INTEGER             DEFAULT NULL, "
        "  premiumOrderNumber          TEXT                DEFAULT NULL, "
        "  premiumCommerceService      TEXT                DEFAULT NULL, "
        "  premiumServiceStart         INTEGER             DEFAULT NULL, "
        "  premiumServiceSKU           TEXT                DEFAULT NULL, "
        "  lastSuccessfulCharge        INTEGER             DEFAULT NULL, "
        "  lastFailedCharge            INTEGER             DEFAULT NULL, "
        "  lastFailedChargeReason      TEXT                DEFAULT NULL, "
        "  nextPaymentDue              INTEGER             DEFAULT NULL, "
        "  premiumLockUntil            INTEGER             DEFAULT NULL, "
        "  updated                     INTEGER             DEFAULT NULL, "
        "  premiumSubscriptionNumber   TEXT                DEFAULT NULL, "
        "  lastRequestedCharge         INTEGER             DEFAULT NULL, "
        "  currency                    TEXT                DEFAULT NULL, "
        "  unitPrice                   INTEGER             DEFAULT NULL, "
        "  unitDiscount                INTEGER             DEFAULT NULL, "
        "  nextChargeDate              INTEGER             DEFAULT NULL, "
        "  availablePoints             INTEGER             DEFAULT NULL)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create Accounting table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS AccountLimits("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  userMailLimitDaily          INTEGER             DEFAULT NULL, "
        "  noteSizeMax                 INTEGER             DEFAULT NULL, "
        "  resourceSizeMax             INTEGER             DEFAULT NULL, "
        "  userLinkedNotebookMax       INTEGER             DEFAULT NULL, "
        "  uploadLimit                 INTEGER             DEFAULT NULL, "
        "  userNoteCountMax            INTEGER             DEFAULT NULL, "
        "  userNotebookCountMax        INTEGER             DEFAULT NULL, "
        "  userTagCountMax             INTEGER             DEFAULT NULL, "
        "  noteTagCountMax             INTEGER             DEFAULT NULL, "
        "  userSavedSearchesMax        INTEGER             DEFAULT NULL, "
        "  noteResourceCountMax        INTEGER             DEFAULT NULL)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create AccountLimits table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS BusinessUserInfo("
        "  id REFERENCES Users(id) ON UPDATE CASCADE, "
        "  businessId              INTEGER                 DEFAULT NULL, "
        "  businessName            TEXT                    DEFAULT NULL, "
        "  role                    INTEGER                 DEFAULT NULL, "
        "  businessInfoEmail       TEXT                    DEFAULT NULL)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create BusinessUserInfo table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS on_user_delete_trigger "
        "BEFORE DELETE ON Users "
        "BEGIN "
        "DELETE FROM UserAttributes WHERE id=OLD.id; "
        "DELETE FROM UserAttributesViewedPromotions WHERE id=OLD.id; "
        "DELETE FROM UserAttributesRecentMailedAddresses WHERE id=OLD.id; "
        "DELETE FROM Accounting WHERE id=OLD.id; "
        "DELETE FROM AccountLimits WHERE id=OLD.id; "
        "DELETE FROM BusinessUserInfo WHERE id=OLD.id; "
        "END"));
    errorPrefix.setBase(QT_TR_NOOP(
        "Can't create trigger to fire on deletion from users table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS LinkedNotebooks("
        "  guid                            TEXT PRIMARY KEY  NOT NULL UNIQUE, "
        "  updateSequenceNumber            INTEGER           DEFAULT NULL, "
        "  isDirty                         INTEGER           DEFAULT NULL, "
        "  shareName                       TEXT              DEFAULT NULL, "
        "  username                        TEXT              DEFAULT NULL, "
        "  shardId                         TEXT              DEFAULT NULL, "
        "  sharedNotebookGlobalId          TEXT              DEFAULT NULL, "
        "  uri                             TEXT              DEFAULT NULL, "
        "  noteStoreUrl                    TEXT              DEFAULT NULL, "
        "  webApiUrlPrefix                 TEXT              DEFAULT NULL, "
        "  stack                           TEXT              DEFAULT NULL, "
        "  businessId                      INTEGER           DEFAULT NULL"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create LinkedNotebooks table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Notebooks("
        "  localUid                        TEXT PRIMARY KEY  NOT NULL UNIQUE, "
        "  guid                            TEXT              DEFAULT NULL "
        "UNIQUE, "
        "  linkedNotebookGuid REFERENCES LinkedNotebooks(guid) ON UPDATE "
        "CASCADE, "
        "  updateSequenceNumber            INTEGER           DEFAULT NULL, "
        "  notebookName                    TEXT              DEFAULT NULL, "
        "  notebookNameUpper               TEXT              DEFAULT NULL, "
        "  creationTimestamp               INTEGER           DEFAULT NULL, "
        "  modificationTimestamp           INTEGER           DEFAULT NULL, "
        "  isDirty                         INTEGER           NOT NULL, "
        "  isLocal                         INTEGER           NOT NULL, "
        "  isDefault                       INTEGER           DEFAULT NULL "
        "UNIQUE, "
        "  isLastUsed                      INTEGER           DEFAULT NULL "
        "UNIQUE, "
        "  isFavorited                     INTEGER           DEFAULT NULL, "
        "  publishingUri                   TEXT              DEFAULT NULL, "
        "  publishingNoteSortOrder         INTEGER           DEFAULT NULL, "
        "  publishingAscendingSort         INTEGER           DEFAULT NULL, "
        "  publicDescription               TEXT              DEFAULT NULL, "
        "  isPublished                     INTEGER           DEFAULT NULL, "
        "  stack                           TEXT              DEFAULT NULL, "
        "  businessNotebookDescription     TEXT              DEFAULT NULL, "
        "  businessNotebookPrivilegeLevel  INTEGER           DEFAULT NULL, "
        "  businessNotebookIsRecommended   INTEGER           DEFAULT NULL, "
        "  contactId                       INTEGER           DEFAULT NULL, "
        "  recipientReminderNotifyEmail    INTEGER           DEFAULT NULL, "
        "  recipientReminderNotifyInApp    INTEGER           DEFAULT NULL, "
        "  recipientInMyList               INTEGER           DEFAULT NULL, "
        "  recipientStack                  TEXT              DEFAULT NULL, "
        "  UNIQUE(localUid, guid), "
        "  UNIQUE(notebookNameUpper, linkedNotebookGuid) "
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create Notebooks table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS NotebookFTS "
                       "USING FTS4(content=\"Notebooks\", "
                       "localUid, guid, notebookName)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create virtual FTS4 NotebookFTS table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "NotebookFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Notebooks "
                       "BEGIN "
                       "DELETE FROM NotebookFTS WHERE localUid=old.localUid; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create NotebookFTS_BeforeDeleteTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS "
        "NotebookFTS_AfterInsertTrigger "
        "AFTER INSERT ON Notebooks "
        "BEGIN "
        "INSERT INTO NotebookFTS(NotebookFTS) VALUES('rebuild'); "
        "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create NotebookFTS_AfterInsertTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NotebookRestrictions("
        "  localUid REFERENCES Notebooks(localUid) ON UPDATE CASCADE, "
        "  noReadNotes                 INTEGER      DEFAULT NULL, "
        "  noCreateNotes               INTEGER      DEFAULT NULL, "
        "  noUpdateNotes               INTEGER      DEFAULT NULL, "
        "  noExpungeNotes              INTEGER      DEFAULT NULL, "
        "  noShareNotes                INTEGER      DEFAULT NULL, "
        "  noEmailNotes                INTEGER      DEFAULT NULL, "
        "  noSendMessageToRecipients   INTEGER      DEFAULT NULL, "
        "  noUpdateNotebook            INTEGER      DEFAULT NULL, "
        "  noExpungeNotebook           INTEGER      DEFAULT NULL, "
        "  noSetDefaultNotebook        INTEGER      DEFAULT NULL, "
        "  noSetNotebookStack          INTEGER      DEFAULT NULL, "
        "  noPublishToPublic           INTEGER      DEFAULT NULL, "
        "  noPublishToBusinessLibrary  INTEGER      DEFAULT NULL, "
        "  noCreateTags                INTEGER      DEFAULT NULL, "
        "  noUpdateTags                INTEGER      DEFAULT NULL, "
        "  noExpungeTags               INTEGER      DEFAULT NULL, "
        "  noSetParentTag              INTEGER      DEFAULT NULL, "
        "  noCreateSharedNotebooks     INTEGER      DEFAULT NULL, "
        "  noShareNotesWithBusiness    INTEGER      DEFAULT NULL, "
        "  noRenameNotebook            INTEGER      DEFAULT NULL, "
        "  updateWhichSharedNotebookRestrictions    INTEGER     DEFAULT NULL, "
        "  expungeWhichSharedNotebookRestrictions   INTEGER     DEFAULT NULL "
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NotebookRestrictions table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SharedNotebooks("
        "  sharedNotebookShareId                      INTEGER PRIMARY KEY   "
        "NOT NULL UNIQUE, "
        "  sharedNotebookUserId                       INTEGER    DEFAULT NULL, "
        "  sharedNotebookNotebookGuid REFERENCES Notebooks(guid) ON UPDATE "
        "CASCADE, "
        "  sharedNotebookEmail                        TEXT       DEFAULT NULL, "
        "  sharedNotebookIdentityId                   INTEGER    DEFAULT NULL, "
        "  sharedNotebookCreationTimestamp            INTEGER    DEFAULT NULL, "
        "  sharedNotebookModificationTimestamp        INTEGER    DEFAULT NULL, "
        "  sharedNotebookGlobalId                     TEXT       DEFAULT NULL, "
        "  sharedNotebookUsername                     TEXT       DEFAULT NULL, "
        "  sharedNotebookPrivilegeLevel               INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientReminderNotifyEmail INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientReminderNotifyInApp INTEGER    DEFAULT NULL, "
        "  sharedNotebookSharerUserId                 INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientUsername            TEXT       DEFAULT NULL, "
        "  sharedNotebookRecipientUserId              INTEGER    DEFAULT NULL, "
        "  sharedNotebookRecipientIdentityId          INTEGER    DEFAULT NULL, "
        "  sharedNotebookAssignmentTimestamp          INTEGER    DEFAULT NULL, "
        "  indexInNotebook                            INTEGER    DEFAULT NULL, "
        "  UNIQUE(sharedNotebookShareId, sharedNotebookNotebookGuid) ON "
        "CONFLICT REPLACE)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create SharedNotebooks table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Notes("
        "  localUid                        TEXT PRIMARY KEY     NOT NULL "
        "UNIQUE, "
        "  guid                            TEXT                 DEFAULT NULL "
        "UNIQUE, "
        "  updateSequenceNumber            INTEGER              DEFAULT NULL, "
        "  isDirty                         INTEGER              NOT NULL, "
        "  isLocal                         INTEGER              NOT NULL, "
        "  isFavorited                     INTEGER              NOT NULL, "
        "  title                           TEXT                 DEFAULT NULL, "
        "  titleNormalized                 TEXT                 DEFAULT NULL, "
        "  content                         TEXT                 DEFAULT NULL, "
        "  contentLength                   INTEGER              DEFAULT NULL, "
        "  contentHash                     TEXT                 DEFAULT NULL, "
        "  contentPlainText                TEXT                 DEFAULT NULL, "
        "  contentListOfWords              TEXT                 DEFAULT NULL, "
        "  contentContainsFinishedToDo     INTEGER              DEFAULT NULL, "
        "  contentContainsUnfinishedToDo   INTEGER              DEFAULT NULL, "
        "  contentContainsEncryption       INTEGER              DEFAULT NULL, "
        "  creationTimestamp               INTEGER              DEFAULT NULL, "
        "  modificationTimestamp           INTEGER              DEFAULT NULL, "
        "  deletionTimestamp               INTEGER              DEFAULT NULL, "
        "  isActive                        INTEGER              DEFAULT NULL, "
        "  hasAttributes                   INTEGER              NOT NULL, "
        "  thumbnail                       BLOB                 DEFAULT NULL, "
        "  notebookLocalUid REFERENCES Notebooks(localUid) ON UPDATE CASCADE, "
        "  notebookGuid REFERENCES Notebooks(guid) ON UPDATE CASCADE, "
        "  subjectDate                     INTEGER              DEFAULT NULL, "
        "  latitude                        REAL                 DEFAULT NULL, "
        "  longitude                       REAL                 DEFAULT NULL, "
        "  altitude                        REAL                 DEFAULT NULL, "
        "  author                          TEXT                 DEFAULT NULL, "
        "  source                          TEXT                 DEFAULT NULL, "
        "  sourceURL                       TEXT                 DEFAULT NULL, "
        "  sourceApplication               TEXT                 DEFAULT NULL, "
        "  shareDate                       INTEGER              DEFAULT NULL, "
        "  reminderOrder                   INTEGER              DEFAULT NULL, "
        "  reminderDoneTime                INTEGER              DEFAULT NULL, "
        "  reminderTime                    INTEGER              DEFAULT NULL, "
        "  placeName                       TEXT                 DEFAULT NULL, "
        "  contentClass                    TEXT                 DEFAULT NULL, "
        "  lastEditedBy                    TEXT                 DEFAULT NULL, "
        "  creatorId                       INTEGER              DEFAULT NULL, "
        "  lastEditorId                    INTEGER              DEFAULT NULL, "
        "  sharedWithBusiness              INTEGER              DEFAULT NULL, "
        "  conflictSourceNoteGuid          TEXT                 DEFAULT NULL, "
        "  noteTitleQuality                INTEGER              DEFAULT NULL, "
        "  applicationDataKeysOnly         TEXT                 DEFAULT NULL, "
        "  applicationDataKeysMap          TEXT                 DEFAULT NULL, "
        "  applicationDataValues           TEXT                 DEFAULT NULL, "
        "  classificationKeys              TEXT                 DEFAULT NULL, "
        "  classificationValues            TEXT                 DEFAULT NULL, "
        "  UNIQUE(localUid, guid)"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create Notes table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SharedNotes("
        "  sharedNoteNoteGuid REFERENCES Notes(guid) ON UPDATE CASCADE, "
        "  sharedNoteSharerUserId                           INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientIdentityId                    INTEGER DEFAULT "
        "NULL UNIQUE, "
        "  sharedNoteRecipientContactName                   TEXT    DEFAULT "
        "NULL, "
        "  sharedNoteRecipientContactId                     TEXT    DEFAULT "
        "NULL, "
        "  sharedNoteRecipientContactType                   INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientContactPhotoUrl               TEXT    DEFAULT "
        "NULL, "
        "  sharedNoteRecipientContactPhotoLastUpdated       INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientContactMessagingPermit        BLOB    DEFAULT "
        "NULL, "
        "  sharedNoteRecipientContactMessagingPermitExpires INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientUserId                        INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientDeactivated                   INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientSameBusiness                  INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientBlocked                       INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientUserConnected                 INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteRecipientEventId                       INTEGER DEFAULT "
        "NULL, "
        "  sharedNotePrivilegeLevel                         INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteCreationTimestamp                      INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteModificationTimestamp                  INTEGER DEFAULT "
        "NULL, "
        "  sharedNoteAssignmentTimestamp                    INTEGER DEFAULT "
        "NULL, "
        "  indexInNote                                      INTEGER DEFAULT "
        "NULL, "
        "  UNIQUE(sharedNoteNoteGuid, sharedNoteRecipientIdentityId) ON "
        "CONFLICT REPLACE)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create SharedNotes table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteRestrictions("
        "  noteLocalUid REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  noUpdateNoteTitle                INTEGER             DEFAULT NULL, "
        "  noUpdateNoteContent              INTEGER             DEFAULT NULL, "
        "  noEmailNote                      INTEGER             DEFAULT NULL, "
        "  noShareNote                      INTEGER             DEFAULT NULL, "
        "  noShareNotePublicly              INTEGER             DEFAULT "
        "NULL)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NoteRestrictions table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res =
        query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS "
                                  "NoteRestrictionsByNoteLocalUid ON "
                                  "NoteRestrictions(noteLocalUid)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create index NoteRestrictionsByNoteLocalUid"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteLimits("
        "  noteLocalUid REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  noteResourceCountMax             INTEGER             DEFAULT NULL, "
        "  uploadLimit                      INTEGER             DEFAULT NULL, "
        "  resourceSizeMax                  INTEGER             DEFAULT NULL, "
        "  noteSizeMax                      INTEGER             DEFAULT NULL, "
        "  uploaded                         INTEGER             DEFAULT "
        "NULL)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NoteLimits table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res =
        query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS NotesNotebooks "
                                  "ON Notes(notebookLocalUid)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create index NotesNotebooks"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS NoteFTS "
                       "USING FTS4(content=\"Notes\", localUid, "
                       "titleNormalized, contentListOfWords, "
                       "contentContainsFinishedToDo, "
                       "contentContainsUnfinishedToDo, "
                       "contentContainsEncryption, creationTimestamp, "
                       "modificationTimestamp, isActive, "
                       "notebookLocalUid, notebookGuid, subjectDate, "
                       "latitude, longitude, altitude, author, source, "
                       "sourceApplication, reminderOrder, reminderDoneTime, "
                       "reminderTime, placeName, contentClass, "
                       "applicationDataKeysOnly, "
                       "applicationDataKeysMap, applicationDataValues)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create virtual FTS4 table NoteFTS"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "NoteFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Notes "
                       "BEGIN "
                       "DELETE FROM NoteFTS WHERE localUid=old.localUid; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger NoteFTS_BeforeDeleteTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "NoteFTS_AfterInsertTrigger "
                       "AFTER INSERT ON Notes "
                       "BEGIN "
                       "INSERT INTO NoteFTS(NoteFTS) VALUES('rebuild'); "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger NoteFTS_AfterInsertTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "on_notebook_delete_trigger "
                       "BEFORE DELETE ON Notebooks "
                       "BEGIN "
                       "DELETE FROM NotebookRestrictions WHERE "
                       "NotebookRestrictions.localUid=OLD.localUid; "
                       "DELETE FROM SharedNotebooks WHERE "
                       "SharedNotebooks.sharedNotebookNotebookGuid=OLD.guid; "
                       "DELETE FROM Notes WHERE "
                       "Notes.notebookLocalUid=OLD.localUid; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger to fire on notebook deletion"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Resources("
        "  resourceLocalUid                TEXT PRIMARY KEY     NOT NULL "
        "UNIQUE, "
        "  resourceGuid                    TEXT                 DEFAULT NULL "
        "UNIQUE, "
        "  noteLocalUid REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  noteGuid REFERENCES Notes(guid) ON UPDATE CASCADE, "
        "  resourceUpdateSequenceNumber    INTEGER              DEFAULT NULL, "
        "  resourceIsDirty                 INTEGER              NOT NULL, "
        "  dataSize                        INTEGER              DEFAULT NULL, "
        "  dataHash                        TEXT                 DEFAULT NULL, "
        "  mime                            TEXT                 DEFAULT NULL, "
        "  width                           INTEGER              DEFAULT NULL, "
        "  height                          INTEGER              DEFAULT NULL, "
        "  recognitionDataBody             TEXT                 DEFAULT NULL, "
        "  recognitionDataSize             INTEGER              DEFAULT NULL, "
        "  recognitionDataHash             TEXT                 DEFAULT NULL, "
        "  alternateDataSize               INTEGER              DEFAULT NULL, "
        "  alternateDataHash               TEXT                 DEFAULT NULL, "
        "  resourceIndexInNote             INTEGER              DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid, resourceGuid)"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create Resources table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS ResourceMimeIndex ON Resources(mime)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create ResourceMimeIndex index"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS ResourceRecognitionData("
                       "  resourceLocalUid REFERENCES "
                       "Resources(resourceLocalUid) ON UPDATE CASCADE, "
                       "  noteLocalUid REFERENCES Notes(localUid)              "
                       "   ON UPDATE CASCADE, "
                       "  recognitionData                 TEXT                 "
                       "   DEFAULT NULL)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create ResourceRecognitionData table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE INDEX IF NOT EXISTS "
                       "ResourceRecognitionDataIndex "
                       "ON ResourceRecognitionData(recognitionData)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create ResourceRecognitionDataIndex index"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS "
                       "ResourceRecognitionDataFTS USING FTS4"
                       "(content=\"ResourceRecognitionData\", "
                       "resourceLocalUid, noteLocalUid, recognitionData)"));
    errorPrefix.setBase(QT_TR_NOOP(
        "Can't create virtual FTS4 ResourceRecognitionDataFTS table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceRecognitionDataFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON ResourceRecognitionData "
                       "BEGIN "
                       "DELETE FROM ResourceRecognitionDataFTS "
                       "WHERE recognitionData=old.recognitionData; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger "
                   "ResourceRecognitionDataFTS_BeforeDeleteTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceRecognitionDataFTS_AfterInsertTrigger "
                       "AFTER INSERT ON ResourceRecognitionData "
                       "BEGIN "
                       "INSERT INTO ResourceRecognitionDataFTS("
                       "ResourceRecognitionDataFTS) VALUES('rebuild'); "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger "
                   "ResourceRecognitionDataFTS_AfterInsertTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS "
                       "ResourceMimeFTS USING FTS4(content=\"Resources\", "
                       "resourceLocalUid, mime)"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create virtual FTS4 ResourceMimeFTS table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceMimeFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Resources "
                       "BEGIN "
                       "DELETE FROM ResourceMimeFTS WHERE mime=old.mime; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger "
                   "ResourceMimeFTS_BeforeDeleteTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "ResourceMimeFTS_AfterInsertTrigger "
                       "AFTER INSERT ON Resources "
                       "BEGIN "
                       "INSERT INTO ResourceMimeFTS(ResourceMimeFTS) "
                       "VALUES('rebuild'); "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger ResourceMimeFTS_AfterInsertTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS ResourceNote ON Resources(noteLocalUid)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create ResourceNote index"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ResourceAttributes("
        "  resourceLocalUid REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resourceSourceURL       TEXT                DEFAULT NULL, "
        "  timestamp               INTEGER             DEFAULT NULL, "
        "  resourceLatitude        REAL                DEFAULT NULL, "
        "  resourceLongitude       REAL                DEFAULT NULL, "
        "  resourceAltitude        REAL                DEFAULT NULL, "
        "  cameraMake              TEXT                DEFAULT NULL, "
        "  cameraModel             TEXT                DEFAULT NULL, "
        "  clientWillIndex         INTEGER             DEFAULT NULL, "
        "  fileName                TEXT                DEFAULT NULL, "
        "  attachment              INTEGER             DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid) "
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create ResourceAttributes table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ResourceAttributesApplicationDataKeysOnly("
        "  resourceLocalUid REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resourceKey             TEXT                DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid, resourceKey)"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP(
        "Can't create ResourceAttributesApplicationDataKeysOnly table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS ResourceAttributesApplicationDataFullMap("
        "  resourceLocalUid REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resourceMapKey          TEXT                DEFAULT NULL, "
        "  resourceValue           TEXT                DEFAULT NULL, "
        "  UNIQUE(resourceLocalUid, resourceMapKey) ON CONFLICT REPLACE"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP(
        "Can't create ResourceAttributesApplicationDataFullMap table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS Tags("
        "  localUid              TEXT PRIMARY KEY     NOT NULL UNIQUE, "
        "  guid                  TEXT                 DEFAULT NULL UNIQUE, "
        "  linkedNotebookGuid REFERENCES LinkedNotebooks(guid) ON UPDATE "
        "CASCADE, "
        "  updateSequenceNumber  INTEGER              DEFAULT NULL, "
        "  name                  TEXT                 DEFAULT NULL, "
        "  nameLower             TEXT                 DEFAULT NULL, "
        "  parentGuid REFERENCES Tags(guid)           ON UPDATE CASCADE "
        "DEFAULT NULL, "
        "  parentLocalUid REFERENCES Tags(localUid)   ON UPDATE CASCADE "
        "DEFAULT NULL, "
        "  isDirty               INTEGER              NOT NULL, "
        "  isLocal               INTEGER              NOT NULL, "
        "  isFavorited           INTEGER              NOT NULL, "
        "  UNIQUE(localUid, guid), "
        "  UNIQUE(nameLower, linkedNotebookGuid) "
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create Tags table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS TagNameUpperIndex ON Tags(nameLower)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create TagNameUpperIndex index"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS TagFTS "
        "USING FTS4(content=\"Tags\", localUid, guid, nameLower)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create virtual FTS4 table TagFTS"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "TagFTS_BeforeDeleteTrigger "
                       "BEFORE DELETE ON Tags "
                       "BEGIN "
                       "DELETE FROM TagFTS WHERE localUid=old.localUid; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger TagFTS_BeforeDeleteTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "TagFTS_AfterInsertTrigger AFTER INSERT ON Tags "
                       "BEGIN "
                       "INSERT INTO TagFTS(TagFTS) VALUES('rebuild'); "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger TagFTS_AfterInsertTrigger"));
    DATABASE_CHECK_AND_SET_ERROR()

    res =
        query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS TagsSearchName "
                                  "ON Tags(nameLower)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create TagsSearchName index"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteTags("
        "  localNote REFERENCES Notes(localUid) ON UPDATE CASCADE, "
        "  note REFERENCES Notes(guid)          ON UPDATE CASCADE, "
        "  localTag REFERENCES Tags(localUid)   ON UPDATE CASCADE, "
        "  tag  REFERENCES Tags(guid)           ON UPDATE CASCADE, "
        "  tagIndexInNote        INTEGER        DEFAULT NULL, "
        "  UNIQUE(localNote, localTag) ON CONFLICT REPLACE"
        ")"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NoteTags table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res =
        query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS NoteTagsNote "
                                  "ON NoteTags(localNote)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NoteTagsNote index"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS NoteResources("
        "  localNote     REFERENCES Notes(localUid)             ON UPDATE "
        "CASCADE, "
        "  note          REFERENCES Notes(guid)                 ON UPDATE "
        "CASCADE, "
        "  localResource REFERENCES Resources(resourceLocalUid) ON UPDATE "
        "CASCADE, "
        "  resource      REFERENCES Resources(resourceGuid)     ON UPDATE "
        "CASCADE, "
        "  UNIQUE(localNote, localResource) ON CONFLICT REPLACE)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NoteResources table"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE INDEX IF NOT EXISTS NoteResourcesNote ON "
                       "NoteResources(localNote)"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create NoteResourcesNote index"));
    DATABASE_CHECK_AND_SET_ERROR()

    // NOTE: reasoning for existence and unique constraint for nameLower,
    // citing Evernote API reference: "The account may only contain one search
    // with a given name (case-insensitive compare)"

    res =
        query.exec(QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                                  "on_linked_notebook_delete_trigger "
                                  "BEFORE DELETE ON LinkedNotebooks "
                                  "BEGIN "
                                  "DELETE FROM Notebooks WHERE "
                                  "Notebooks.linkedNotebookGuid=OLD.guid; "
                                  "DELETE FROM Tags WHERE "
                                  "Tags.linkedNotebookGuid=OLD.guid; "
                                  "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger to fire on linked notebook deletion"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS "
                       "on_note_delete_trigger "
                       "BEFORE DELETE ON Notes "
                       "BEGIN "
                       "DELETE FROM Resources WHERE "
                       "Resources.noteLocalUid=OLD.localUid; "
                       "DELETE FROM ResourceRecognitionData WHERE "
                       "ResourceRecognitionData.noteLocalUid=OLD.localUid; "
                       "DELETE FROM NoteTags WHERE "
                       "NoteTags.localNote=OLD.localUid; "
                       "DELETE FROM NoteResources WHERE "
                       "NoteResources.localNote=OLD.localUid; "
                       "DELETE FROM SharedNotes WHERE "
                       "SharedNotes.sharedNoteNoteGuid=OLD.guid; "
                       "DELETE FROM NoteRestrictions WHERE "
                       "NoteRestrictions.noteLocalUid=OLD.localUid; "
                       "DELETE FROM NoteLimits WHERE "
                       "NoteLimits.noteLocalUid=OLD.localUid; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger to fire on note deletion"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS "
        "on_resource_delete_trigger "
        "BEFORE DELETE ON Resources "
        "BEGIN "
        "DELETE FROM ResourceRecognitionData "
        "WHERE ResourceRecognitionData.resourceLocalUid="
        "OLD.resourceLocalUid; "
        "DELETE FROM ResourceAttributes WHERE "
        "ResourceAttributes.resourceLocalUid=OLD.resourceLocalUid; "
        "DELETE FROM ResourceAttributesApplicationDataKeysOnly WHERE "
        "ResourceAttributesApplicationDataKeysOnly.resourceLocalUid="
        "OLD.resourceLocalUid; "
        "DELETE FROM ResourceAttributesApplicationDataFullMap WHERE "
        "ResourceAttributesApplicationDataFullMap.resourceLocalUid="
        "OLD.resourceLocalUid; "
        "DELETE FROM NoteResources WHERE "
        "NoteResources.localResource=OLD.resourceLocalUid; "
        "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger to fire on resource deletion"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(
        QStringLiteral("CREATE TRIGGER IF NOT EXISTS on_tag_delete_trigger "
                       "BEFORE DELETE ON Tags "
                       "BEGIN "
                       "DELETE FROM NoteTags WHERE "
                       "NoteTags.localTag=OLD.localUid; "
                       "END"));
    errorPrefix.setBase(
        QT_TR_NOOP("Can't create trigger to fire on tag deletion"));
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS SavedSearches("
        "  localUid                        TEXT PRIMARY KEY    NOT NULL "
        "UNIQUE, "
        "  guid                            TEXT                DEFAULT NULL "
        "UNIQUE, "
        "  name                            TEXT                DEFAULT NULL, "
        "  nameLower                       TEXT                DEFAULT NULL "
        "UNIQUE, "
        "  query                           TEXT                DEFAULT NULL, "
        "  format                          INTEGER             DEFAULT NULL, "
        "  updateSequenceNumber            INTEGER             DEFAULT NULL, "
        "  isDirty                         INTEGER             NOT NULL, "
        "  isLocal                         INTEGER             NOT NULL, "
        "  includeAccount                  INTEGER             DEFAULT NULL, "
        "  includePersonalLinkedNotebooks  INTEGER             DEFAULT NULL, "
        "  includeBusinessLinkedNotebooks  INTEGER             DEFAULT NULL, "
        "  isFavorited                     INTEGER             NOT NULL, "
        "  UNIQUE(localUid, guid))"));
    errorPrefix.setBase(QT_TR_NOOP("Can't create SavedSearches table"));
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceNotebookRestrictions(
    const QString & localId,
    const qevercloud::NotebookRestrictions & notebookRestrictions,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace notebook restrictions"));

    bool res = checkAndPrepareInsertOrReplaceNotebookRestrictionsQuery();
    QSqlQuery & query = m_insertOrReplaceNotebookRestrictionsQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(QStringLiteral(":localUid"), localId);

#define BIND_RESTRICTION(name)                                                 \
    query.bindValue(                                                           \
        QStringLiteral(":" #name),                                             \
        (notebookRestrictions.name() ? (*notebookRestrictions.name() ? 1 : 0)  \
                                     : nullValue))

    BIND_RESTRICTION(noReadNotes);
    BIND_RESTRICTION(noCreateNotes);
    BIND_RESTRICTION(noUpdateNotes);
    BIND_RESTRICTION(noExpungeNotes);
    BIND_RESTRICTION(noShareNotes);
    BIND_RESTRICTION(noEmailNotes);
    BIND_RESTRICTION(noSendMessageToRecipients);
    BIND_RESTRICTION(noUpdateNotebook);
    BIND_RESTRICTION(noExpungeNotebook);
    BIND_RESTRICTION(noSetDefaultNotebook);
    BIND_RESTRICTION(noSetNotebookStack);
    BIND_RESTRICTION(noPublishToPublic);
    BIND_RESTRICTION(noPublishToBusinessLibrary);
    BIND_RESTRICTION(noCreateTags);
    BIND_RESTRICTION(noUpdateTags);
    BIND_RESTRICTION(noExpungeTags);
    BIND_RESTRICTION(noSetParentTag);
    BIND_RESTRICTION(noCreateSharedNotebooks);
    BIND_RESTRICTION(noShareNotesWithBusiness);
    BIND_RESTRICTION(noRenameNotebook);

#undef BIND_RESTRICTION

    query.bindValue(
        QStringLiteral(":updateWhichSharedNotebookRestrictions"),
        notebookRestrictions.updateWhichSharedNotebookRestrictions()
            ? static_cast<int>(
                  *notebookRestrictions.updateWhichSharedNotebookRestrictions())
            : nullValue);

    query.bindValue(
        QStringLiteral(":expungeWhichSharedNotebookRestrictions"),
        notebookRestrictions.expungeWhichSharedNotebookRestrictions()
            ? static_cast<int>(*notebookRestrictions
                                    .expungeWhichSharedNotebookRestrictions())
            : nullValue);

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceSharedNotebook(
    const qevercloud::SharedNotebook & sharedNotebook,
    ErrorString & errorDescription)
{
    // NOTE: this method is expected to be called after the sanity check of
    // sharedNotebook object!

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace shared notebook"));

    bool res = checkAndPrepareInsertOrReplaceSharedNotebookQuery();
    QSqlQuery & query = m_insertOrReplaceSharedNotebookQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(
        QStringLiteral(":sharedNotebookShareId"), *sharedNotebook.id());

    query.bindValue(
        QStringLiteral(":sharedNotebookUserId"),
        (sharedNotebook.userId() ? *sharedNotebook.userId() : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookNotebookGuid"),
        (sharedNotebook.notebookGuid() ? *sharedNotebook.notebookGuid()
                                       : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookEmail"),
        (sharedNotebook.email() ? *sharedNotebook.email() : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookCreationTimestamp"),
        (sharedNotebook.serviceCreated() ? *sharedNotebook.serviceCreated()
                                         : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookModificationTimestamp"),
        (sharedNotebook.serviceUpdated() ? *sharedNotebook.serviceUpdated()
                                         : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookGlobalId"),
        (sharedNotebook.globalId() ? *sharedNotebook.globalId() : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookUsername"),
        (sharedNotebook.username() ? *sharedNotebook.username() : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookPrivilegeLevel"),
        (sharedNotebook.privilege()
             ? static_cast<int>(*sharedNotebook.privilege())
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientReminderNotifyEmail"),
        (sharedNotebook.recipientSettings()
             ? (sharedNotebook.recipientSettings()->reminderNotifyEmail()
                    ? (*sharedNotebook.recipientSettings()
                               ->reminderNotifyEmail()
                           ? 1
                           : 0)
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientReminderNotifyInApp"),
        (sharedNotebook.recipientSettings()
             ? (sharedNotebook.recipientSettings()->reminderNotifyInApp()
                    ? (*sharedNotebook.recipientSettings()
                               ->reminderNotifyInApp()
                           ? 1
                           : 0)
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookSharerUserId"),
        (sharedNotebook.sharerUserId() ? *sharedNotebook.sharerUserId()
                                       : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientUsername"),
        (sharedNotebook.recipientUsername()
             ? *sharedNotebook.recipientUsername()
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientUserId"),
        (sharedNotebook.recipientUserId() ? *sharedNotebook.recipientUserId()
                                          : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientIdentityId"),
        (sharedNotebook.recipientIdentityId()
             ? *sharedNotebook.recipientIdentityId()
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookAssignmentTimestamp"),
        (sharedNotebook.serviceAssigned() ? *sharedNotebook.serviceAssigned()
                                          : nullValue));

    const auto indexInNotebook = sharedNotebookIndexInNotebook(sharedNotebook);

    query.bindValue(
        QStringLiteral(":indexInNotebook"),
        (indexInNotebook ? *indexInNotebook : nullValue));

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::rowExists(
    const QString & tableName, const QString & uniqueKeyName,
    const QVariant & uniqueKeyValue) const
{
    const QString key = sqlEscapeString(uniqueKeyValue.toString());

    const QString queryString =
        QString::fromUtf8("SELECT count(*) FROM %1 WHERE %2='%3'")
            .arg(tableName, uniqueKeyName, key);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    if (!res) {
        QNWARNING(
            "local_storage",
            "Unable to check the existence of row with key name "
                << uniqueKeyName << ", value = " << key << " in table "
                << tableName << ": unable to execute SQL statement: "
                << query.lastError().text() << "; assuming no such row exists");
        return false;
    }

    if (query.next() && query.isValid()) {
        bool conversionResult = false;
        const int count = query.value(0).toInt(&conversionResult);
        if (!conversionResult) {
            return false;
        }

        return (count != 0);
    }

    return false;
}

bool LocalStorageManagerPrivate::insertOrReplaceUser(
    const qevercloud::User & user, ErrorString & errorDescription)
{
    // NOTE: this method is expected to be called after the check of user object
    // for sanity of its parameters!

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace User into the local storage "
                   "database"));

    Transaction transaction(m_sqlDatabase, *this, Transaction::Type::Exclusive);

    const QString userId = QString::number(*user.id());
    const QVariant nullValue;

    // Insert or replace common user data
    {
        bool res = checkAndPrepareInsertOrReplaceUserQuery();
        QSqlQuery & query = m_insertOrReplaceUserQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":id"), userId);

        query.bindValue(
            QStringLiteral(":username"),
            (user.username() ? *user.username() : nullValue));

        query.bindValue(
            QStringLiteral(":email"),
            (user.email() ? *user.email() : nullValue));

        query.bindValue(
            QStringLiteral(":name"), (user.name() ? *user.name() : nullValue));

        query.bindValue(
            QStringLiteral(":timezone"),
            (user.timezone() ? *user.timezone() : nullValue));

        query.bindValue(
            QStringLiteral(":privilege"),
            (user.privilege() ? static_cast<int>(*user.privilege())
                              : nullValue));

        query.bindValue(
            QStringLiteral(":serviceLevel"),
            (user.serviceLevel() ? static_cast<int>(*user.serviceLevel())
                                 : nullValue));

        query.bindValue(
            QStringLiteral(":userCreationTimestamp"),
            (user.created() ? *user.created() : nullValue));

        query.bindValue(
            QStringLiteral(":userModificationTimestamp"),
            (user.updated() ? *user.updated() : nullValue));

        query.bindValue(
            QStringLiteral(":userIsDirty"), (user.isLocallyModified() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":userIsLocal"), (user.isLocalOnly() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":userDeletionTimestamp"),
            (user.deleted() ? *user.deleted() : nullValue));

        query.bindValue(
            QStringLiteral(":userIsActive"),
            (user.active() ? (*user.active() ? 1 : 0) : nullValue));

        query.bindValue(
            QStringLiteral(":userShardId"),
            (user.shardId() ? *user.shardId() : nullValue));

        query.bindValue(
            QStringLiteral(":userPhotoUrl"),
            (user.photoUrl() ? *user.photoUrl() : nullValue));

        query.bindValue(
            QStringLiteral(":userPhotoLastUpdateTimestamp"),
            (user.photoLastUpdated() ? *user.photoLastUpdated() : nullValue));

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (user.attributes()) {
        ErrorString error;
        if (!insertOrReplaceUserAttributes(
                *user.id(), *user.attributes(), error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }
    else {
        // Clear entries from UserAttributesViewedPromotions table
        {
            const QString queryString =
                QString::fromUtf8(
                    "DELETE FROM UserAttributesViewedPromotions WHERE id=%1")
                    .arg(userId);

            QSqlQuery query(m_sqlDatabase);
            const bool res = query.exec(queryString);
            DATABASE_CHECK_AND_SET_ERROR()
        }

        // Clear entries from UserAttributesRecentMailedAddresses table
        {
            const QString queryString =
                QString::fromUtf8(
                    "DELETE FROM UserAttributesRecentMailedAddresses WHERE "
                    "id=%1")
                    .arg(userId);

            QSqlQuery query(m_sqlDatabase);
            const bool res = query.exec(queryString);
            DATABASE_CHECK_AND_SET_ERROR()
        }

        // Clear entries from UserAttributes table
        {
            const QString queryString =
                QString::fromUtf8("DELETE FROM UserAttributes WHERE id=%1")
                    .arg(userId);

            QSqlQuery query(m_sqlDatabase);
            const bool res = query.exec(queryString);
            DATABASE_CHECK_AND_SET_ERROR()
        }
    }

    if (user.accounting()) {
        ErrorString error;
        if (!insertOrReplaceAccounting(*user.id(), *user.accounting(), error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }
    else {
        const QString queryString =
            QString::fromUtf8("DELETE FROM Accounting WHERE id=%1").arg(userId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (user.accountLimits()) {
        ErrorString error;
        if (!insertOrReplaceAccountLimits(
                *user.id(), *user.accountLimits(), error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }
    else {
        const QString queryString =
            QString::fromUtf8("DELETE FROM AccountLimits WHERE id=%1")
                .arg(userId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (user.businessUserInfo()) {
        ErrorString error;
        if (!insertOrReplaceBusinessUserInfo(
                *user.id(), *user.businessUserInfo(), error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }
    else {
        const QString queryString =
            QString::fromUtf8("DELETE FROM BusinessUserInfo WHERE id=%1")
                .arg(userId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    return transaction.commit(errorDescription);
}

bool LocalStorageManagerPrivate::insertOrReplaceBusinessUserInfo(
    const qevercloud::UserID id, const qevercloud::BusinessUserInfo & info,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace business user info"));

    bool res = checkAndPrepareInsertOrReplaceBusinessUserInfoQuery();
    QSqlQuery & query = m_insertOrReplaceBusinessUserInfoQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(QStringLiteral(":id"), id);

    query.bindValue(
        QStringLiteral(":businessId"),
        (info.businessId() ? *info.businessId() : nullValue));

    query.bindValue(
        QStringLiteral(":businessName"),
        (info.businessName() ? *info.businessName() : nullValue));

    query.bindValue(
        QStringLiteral(":role"),
        (info.role() ? static_cast<int>(*info.role()) : nullValue));

    query.bindValue(
        QStringLiteral(":businessInfoEmail"),
        (info.email() ? *info.email() : nullValue));

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceAccounting(
    const qevercloud::UserID id, const qevercloud::Accounting & accounting,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace accounting"));

    bool res = checkAndPrepareInsertOrReplaceAccountingQuery();
    QSqlQuery & query = m_insertOrReplaceAccountingQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    query.bindValue(QStringLiteral(":id"), id);

    const QVariant nullValue;

#define CHECK_AND_BIND_VALUE(name, ...)                                        \
    query.bindValue(                                                           \
        QStringLiteral(":" #name),                                             \
        accounting.name() ? __VA_ARGS__(*accounting.name()) : nullValue)

    CHECK_AND_BIND_VALUE(uploadLimitEnd);
    CHECK_AND_BIND_VALUE(uploadLimitNextMonth);
    CHECK_AND_BIND_VALUE(premiumServiceStatus, static_cast<int>);
    CHECK_AND_BIND_VALUE(premiumOrderNumber);
    CHECK_AND_BIND_VALUE(premiumCommerceService);
    CHECK_AND_BIND_VALUE(premiumServiceStart);
    CHECK_AND_BIND_VALUE(premiumServiceSKU);
    CHECK_AND_BIND_VALUE(lastSuccessfulCharge);
    CHECK_AND_BIND_VALUE(lastFailedCharge);
    CHECK_AND_BIND_VALUE(lastFailedChargeReason);
    CHECK_AND_BIND_VALUE(nextPaymentDue);
    CHECK_AND_BIND_VALUE(premiumLockUntil);
    CHECK_AND_BIND_VALUE(updated);
    CHECK_AND_BIND_VALUE(premiumSubscriptionNumber);
    CHECK_AND_BIND_VALUE(lastRequestedCharge);
    CHECK_AND_BIND_VALUE(currency);
    CHECK_AND_BIND_VALUE(unitPrice);
    CHECK_AND_BIND_VALUE(unitDiscount);
    CHECK_AND_BIND_VALUE(nextChargeDate);
    CHECK_AND_BIND_VALUE(availablePoints);

#undef CHECK_AND_BIND_VALUE

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceAccountLimits(
    const qevercloud::UserID id,
    const qevercloud::AccountLimits & accountLimits,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace account limits"));

    bool res = checkAndPrepareInsertOrReplaceAccountLimitsQuery();
    QSqlQuery & query = m_insertOrReplaceAccountLimitsQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    query.bindValue(QStringLiteral(":id"), id);

    const QVariant nullValue;

#define CHECK_AND_BIND_VALUE(name)                                             \
    query.bindValue(                                                           \
        QStringLiteral(":" #name),                                             \
        accountLimits.name() ? *accountLimits.name() : nullValue)

    CHECK_AND_BIND_VALUE(userMailLimitDaily);
    CHECK_AND_BIND_VALUE(noteSizeMax);
    CHECK_AND_BIND_VALUE(resourceSizeMax);
    CHECK_AND_BIND_VALUE(userLinkedNotebookMax);
    CHECK_AND_BIND_VALUE(uploadLimit);
    CHECK_AND_BIND_VALUE(userNoteCountMax);
    CHECK_AND_BIND_VALUE(userNotebookCountMax);
    CHECK_AND_BIND_VALUE(userTagCountMax);
    CHECK_AND_BIND_VALUE(noteTagCountMax);
    CHECK_AND_BIND_VALUE(userSavedSearchesMax);
    CHECK_AND_BIND_VALUE(noteResourceCountMax);

#undef CHECK_AND_BIND_VALUE

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceUserAttributes(
    const qevercloud::UserID id, const qevercloud::UserAttributes & attributes,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace user attributes"));

    const QVariant nullValue;

    // Insert or replace common user attributes data
    {
        bool res = checkAndPrepareInsertOrReplaceUserAttributesQuery();
        QSqlQuery & query = m_insertOrReplaceUserAttributesQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":id"), id);

#define CHECK_AND_BIND_VALUE(name, ...)                                        \
    query.bindValue(                                                           \
        QStringLiteral(":" #name),                                             \
        (attributes.name() ? __VA_ARGS__(*attributes.name()) : nullValue))

        CHECK_AND_BIND_VALUE(defaultLocationName);
        CHECK_AND_BIND_VALUE(defaultLatitude);
        CHECK_AND_BIND_VALUE(defaultLongitude);
        CHECK_AND_BIND_VALUE(incomingEmailAddress);
        CHECK_AND_BIND_VALUE(comments);
        CHECK_AND_BIND_VALUE(dateAgreedToTermsOfService);
        CHECK_AND_BIND_VALUE(maxReferrals);
        CHECK_AND_BIND_VALUE(referralCount);
        CHECK_AND_BIND_VALUE(refererCode);
        CHECK_AND_BIND_VALUE(sentEmailDate);
        CHECK_AND_BIND_VALUE(sentEmailCount);
        CHECK_AND_BIND_VALUE(dailyEmailLimit);
        CHECK_AND_BIND_VALUE(emailOptOutDate);
        CHECK_AND_BIND_VALUE(partnerEmailOptInDate);
        CHECK_AND_BIND_VALUE(preferredLanguage);
        CHECK_AND_BIND_VALUE(preferredCountry);
        CHECK_AND_BIND_VALUE(twitterUserName);
        CHECK_AND_BIND_VALUE(twitterId);
        CHECK_AND_BIND_VALUE(groupName);
        CHECK_AND_BIND_VALUE(recognitionLanguage);
        CHECK_AND_BIND_VALUE(referralProof);
        CHECK_AND_BIND_VALUE(businessAddress);
        CHECK_AND_BIND_VALUE(reminderEmailConfig, static_cast<int>);
        CHECK_AND_BIND_VALUE(emailAddressLastConfirmed);
        CHECK_AND_BIND_VALUE(passwordUpdated);

#undef CHECK_AND_BIND_VALUE

#define CHECK_AND_BIND_BOOLEAN_VALUE(name)                                     \
    query.bindValue(                                                           \
        QStringLiteral(":" #name),                                             \
        (attributes.name() ? (*attributes.name() ? 1 : 0) : nullValue))

        CHECK_AND_BIND_BOOLEAN_VALUE(preactivation);
        CHECK_AND_BIND_BOOLEAN_VALUE(clipFullPage);
        CHECK_AND_BIND_BOOLEAN_VALUE(educationalDiscount);
        CHECK_AND_BIND_BOOLEAN_VALUE(hideSponsorBilling);
        CHECK_AND_BIND_BOOLEAN_VALUE(useEmailAutoFiling);
        CHECK_AND_BIND_BOOLEAN_VALUE(salesforcePushEnabled);
        CHECK_AND_BIND_BOOLEAN_VALUE(shouldLogClientEvent);

#undef CHECK_AND_BIND_BOOLEAN_VALUE

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    // Clear viewed promotions first, then re-insert
    {
        const QString queryString =
            QString::fromUtf8(
                "DELETE FROM UserAttributesViewedPromotions WHERE id=%1")
                .arg(id);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (attributes.viewedPromotions()) {
        bool res =
            checkAndPrepareInsertOrReplaceUserAttributesViewedPromotionsQuery();

        QSqlQuery & query =
            m_insertOrReplaceUserAttributesViewedPromotionsQuery;

        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":id"), id);

        const auto & viewedPromotions = *attributes.viewedPromotions();
        for (const auto & viewedPromotion: viewedPromotions) {
            query.bindValue(QStringLiteral(":promotion"), viewedPromotion);
            res = query.exec();
            DATABASE_CHECK_AND_SET_ERROR()
        }
    }

    // Clear recent mailed addresses first, then re-insert
    {
        const QString queryString =
            QString::fromUtf8(
                "DELETE FROM UserAttributesRecentMailedAddresses WHERE id=%1")
                .arg(id);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (attributes.recentMailedAddresses()) {
        bool res =
            checkAndPrepareInsertOrReplaceUserAttributesRecentMailedAddressesQuery();

        QSqlQuery & query =
            m_insertOrReplaceUserAttributesRecentMailedAddressesQuery;

        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":id"), id);

        const auto & recentMailedAddresses =
            *attributes.recentMailedAddresses();

        for (const auto & recentMailedAddress: recentMailedAddresses) {
            query.bindValue(QStringLiteral(":address"), recentMailedAddress);
            res = query.exec();
            DATABASE_CHECK_AND_SET_ERROR()
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::checkAndPrepareUserCountQuery() const
{
    if (Q_LIKELY(m_getUserCountQueryPrepared)) {
        return true;
    }

    QNDEBUG("local_storage", "Preparing SQL query to get the count of users");

    m_getUserCountQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "SELECT COUNT(*) FROM Users WHERE userDeletionTimestamp IS NULL");

    const bool res = m_getUserCountQuery.prepare(queryString);
    if (res) {
        m_getUserCountQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceUserQuery()
{
    if (Q_LIKELY(m_insertOrReplaceUserQueryPrepared)) {
        return true;
    }

    QNDEBUG("local_storage", "Preparing SQL query to insert or replace user");

    m_insertOrReplaceUserQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Users"
        "(id, username, email, name, timezone, privilege, "
        "serviceLevel, userCreationTimestamp, "
        "userModificationTimestamp, userIsDirty, "
        "userIsLocal, userDeletionTimestamp, userIsActive, "
        "userShardId, userPhotoUrl, userPhotoLastUpdateTimestamp) "
        "VALUES(:id, :username, :email, :name, :timezone, "
        ":privilege, :serviceLevel, :userCreationTimestamp, "
        ":userModificationTimestamp, :userIsDirty, :userIsLocal, "
        ":userDeletionTimestamp, :userIsActive, :userShardId, "
        ":userPhotoUrl, :userPhotoLastUpdateTimestamp)");

    const bool res = m_insertOrReplaceUserQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceUserQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceAccountingQuery()
{
    if (Q_LIKELY(m_insertOrReplaceAccountingQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "accounting");

    m_insertOrReplaceAccountingQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Accounting"
        "(id, uploadLimitEnd, uploadLimitNextMonth, "
        "premiumServiceStatus, premiumOrderNumber, "
        "premiumCommerceService, premiumServiceStart, "
        "premiumServiceSKU, lastSuccessfulCharge, "
        "lastFailedCharge, lastFailedChargeReason, nextPaymentDue, "
        "premiumLockUntil, updated, premiumSubscriptionNumber, "
        "lastRequestedCharge, currency, unitPrice, unitDiscount, "
        "nextChargeDate, availablePoints) "
        "VALUES(:id, :uploadLimitEnd, :uploadLimitNextMonth, "
        ":premiumServiceStatus, :premiumOrderNumber, "
        ":premiumCommerceService, :premiumServiceStart, "
        ":premiumServiceSKU, :lastSuccessfulCharge, "
        ":lastFailedCharge, :lastFailedChargeReason, "
        ":nextPaymentDue, :premiumLockUntil, :updated, "
        ":premiumSubscriptionNumber, :lastRequestedCharge, "
        ":currency, :unitPrice, :unitDiscount, :nextChargeDate, "
        ":availablePoints)");

    const bool res = m_insertOrReplaceAccountingQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceAccountingQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceAccountLimitsQuery()
{
    if (Q_LIKELY(m_insertOrReplaceAccountLimitsQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace account "
            << "limits");

    m_insertOrReplaceAccountLimitsQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO AccountLimits"
        "(id, userMailLimitDaily, noteSizeMax, resourceSizeMax, "
        "userLinkedNotebookMax, uploadLimit, userNoteCountMax, "
        "userNotebookCountMax, userTagCountMax, noteTagCountMax, "
        "userSavedSearchesMax, noteResourceCountMax) "
        "VALUES(:id, :userMailLimitDaily, :noteSizeMax, "
        ":resourceSizeMax, :userLinkedNotebookMax, :uploadLimit, "
        ":userNoteCountMax, :userNotebookCountMax, "
        ":userTagCountMax, :noteTagCountMax, "
        ":userSavedSearchesMax, :noteResourceCountMax)");

    const bool res = m_insertOrReplaceAccountLimitsQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceAccountLimitsQueryPrepared = true;
    }

    return true;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceBusinessUserInfoQuery()
{
    if (Q_LIKELY(m_insertOrReplaceBusinessUserInfoQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQl query to insert or replace "
            << "business user info");

    m_insertOrReplaceBusinessUserInfoQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO BusinessUserInfo"
        "(id, businessId, businessName, role, businessInfoEmail) "
        "VALUES(:id, :businessId, :businessName, :role, :businessInfoEmail)");

    const bool res =
        m_insertOrReplaceBusinessUserInfoQuery.prepare(queryString);

    if (res) {
        m_insertOrReplaceBusinessUserInfoQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceUserAttributesQuery()
{
    if (Q_LIKELY(m_insertOrReplaceUserAttributesQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace user "
            << "attributes");

    m_insertOrReplaceUserAttributesQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO UserAttributes"
        "(id, defaultLocationName, defaultLatitude, "
        "defaultLongitude, preactivation, "
        "incomingEmailAddress, comments, "
        "dateAgreedToTermsOfService, maxReferrals, "
        "referralCount, refererCode, sentEmailDate, "
        "sentEmailCount, dailyEmailLimit, "
        "emailOptOutDate, partnerEmailOptInDate, "
        "preferredLanguage, preferredCountry, "
        "clipFullPage, twitterUserName, twitterId, "
        "groupName, recognitionLanguage, "
        "referralProof, educationalDiscount, "
        "businessAddress, hideSponsorBilling, "
        "useEmailAutoFiling, reminderEmailConfig, "
        "emailAddressLastConfirmed, passwordUpdated, "
        "salesforcePushEnabled, shouldLogClientEvent) "
        "VALUES(:id, :defaultLocationName, :defaultLatitude, "
        ":defaultLongitude, :preactivation, "
        ":incomingEmailAddress, :comments, "
        ":dateAgreedToTermsOfService, :maxReferrals, "
        ":referralCount, :refererCode, :sentEmailDate, "
        ":sentEmailCount, :dailyEmailLimit, "
        ":emailOptOutDate, :partnerEmailOptInDate, "
        ":preferredLanguage, :preferredCountry, "
        ":clipFullPage, :twitterUserName, :twitterId, "
        ":groupName, :recognitionLanguage, "
        ":referralProof, :educationalDiscount, "
        ":businessAddress, :hideSponsorBilling, "
        ":useEmailAutoFiling, :reminderEmailConfig, "
        ":emailAddressLastConfirmed, :passwordUpdated, "
        ":salesforcePushEnabled, :shouldLogClientEvent)");

    const bool res = m_insertOrReplaceUserAttributesQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceUserAttributesQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceUserAttributesViewedPromotionsQuery()
{
    if (Q_LIKELY(m_insertOrReplaceUserAttributesViewedPromotionsQueryPrepared))
    {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace user "
            << "attributes viewed promotions");

    m_insertOrReplaceUserAttributesViewedPromotionsQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO UserAttributesViewedPromotions"
        "(id, promotion) VALUES(:id, :promotion)");

    const bool res =
        m_insertOrReplaceUserAttributesViewedPromotionsQuery.prepare(
            queryString);

    if (res) {
        m_insertOrReplaceUserAttributesViewedPromotionsQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceUserAttributesRecentMailedAddressesQuery()
{
    if (Q_LIKELY(
            m_insertOrReplaceUserAttributesRecentMailedAddressesQueryPrepared))
    {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace user "
            << "attributes recent mailed addresses");

    m_insertOrReplaceUserAttributesRecentMailedAddressesQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO UserAttributesRecentMailedAddresses"
        "(id, address) VALUES(:id, :address)");

    const bool res =
        m_insertOrReplaceUserAttributesRecentMailedAddressesQuery.prepare(
            queryString);

    if (res) {
        m_insertOrReplaceUserAttributesRecentMailedAddressesQueryPrepared =
            true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareDeleteUserQuery()
{
    if (Q_LIKELY(m_deleteUserQueryPrepared)) {
        return true;
    }

    QNDEBUG("local_storage", "Preparing SQL query to mark user deleted");

    m_deleteUserQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "UPDATE Users SET userDeletionTimestamp = :userDeletionTimestamp, "
        "userIsLocal = :userIsLocal WHERE id = :id");

    const bool res = m_deleteUserQuery.prepare(queryString);
    if (res) {
        m_deleteUserQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::insertOrReplaceNotebook(
    const qevercloud::Notebook & notebook, ErrorString & errorDescription)
{
    // NOTE: this method expects to be called after notebook is already checked
    // for sanity of its parameters!

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace notebook"));

    Transaction transaction(m_sqlDatabase, *this, Transaction::Type::Exclusive);

    const QString localId = sqlEscapeString(notebook.localId());
    const QVariant nullValue;

    // Insert or replace common Notebook data
    {
        bool res = checkAndPrepareInsertOrReplaceNotebookQuery();
        QSqlQuery & query = m_insertOrReplaceNotebookQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(
            QStringLiteral(":localUid"),
            (localId.isEmpty() ? nullValue : localId));

        query.bindValue(
            QStringLiteral(":guid"),
            (notebook.guid() ? *notebook.guid() : nullValue));

        const QString linkedNotebookGuid =
            notebook.linkedNotebookGuid().value_or(QString{});

        query.bindValue(
            QStringLiteral(":linkedNotebookGuid"),
            (!linkedNotebookGuid.isEmpty() ? linkedNotebookGuid : nullValue));

        query.bindValue(
            QStringLiteral(":updateSequenceNumber"),
            (notebook.updateSequenceNum() ? *notebook.updateSequenceNum()
                                          : nullValue));

        query.bindValue(
            QStringLiteral(":notebookName"),
            (notebook.name() ? *notebook.name() : nullValue));

        query.bindValue(
            QStringLiteral(":notebookNameUpper"),
            (notebook.name() ? notebook.name()->toUpper() : nullValue));

        query.bindValue(
            QStringLiteral(":creationTimestamp"),
            (notebook.serviceCreated() ? *notebook.serviceCreated()
                                       : nullValue));

        query.bindValue(
            QStringLiteral(":modificationTimestamp"),
            (notebook.serviceUpdated() ? *notebook.serviceUpdated()
                                       : nullValue));

        query.bindValue(
            QStringLiteral(":isDirty"), (notebook.isLocallyModified() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":isLocal"), (notebook.isLocalOnly() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":isDefault"),
            (notebook.defaultNotebook()
                 ? static_cast<int>(*notebook.defaultNotebook())
                 : nullValue));

        bool isLastUsed = false;
        if (const auto it =
                notebook.localData().constFind(QStringLiteral("lastUsed"));
            it != notebook.localData().constEnd())
        {
            isLastUsed = it.value().toBool();
        }

        query.bindValue(
            QStringLiteral(":isLastUsed"), (isLastUsed ? 1 : nullValue));

        query.bindValue(
            QStringLiteral(":isFavorited"),
            (notebook.isLocallyFavorited() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":publishingUri"),
            (notebook.publishing()
                 ? (notebook.publishing()->uri() ? *notebook.publishing()->uri()
                                                 : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":publishingNoteSortOrder"),
            (notebook.publishing()
                 ? (notebook.publishing()->order()
                        ? static_cast<int>(*notebook.publishing()->order())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":publishingAscendingSort"),
            (notebook.publishing()
                 ? (notebook.publishing()->ascending()
                        ? static_cast<int>(*notebook.publishing()->ascending())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":publicDescription"),
            (notebook.publishing()
                 ? (notebook.publishing()->publicDescription()
                        ? *notebook.publishing()->publicDescription()
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":isPublished"),
            (notebook.published() ? static_cast<int>(*notebook.published())
                                  : nullValue));

        query.bindValue(
            QStringLiteral(":stack"),
            (notebook.stack() ? *notebook.stack() : nullValue));

        query.bindValue(
            QStringLiteral(":businessNotebookDescription"),
            (notebook.businessNotebook()
                 ? (notebook.businessNotebook()->notebookDescription()
                        ? *notebook.businessNotebook()->notebookDescription()
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":businessNotebookPrivilegeLevel"),
            (notebook.businessNotebook()
                 ? (notebook.businessNotebook()->privilege()
                        ? static_cast<int>(
                              *notebook.businessNotebook()->privilege())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":businessNotebookIsRecommended"),
            (notebook.businessNotebook()
                 ? (notebook.businessNotebook()->recommended()
                        ? static_cast<int>(
                              *notebook.businessNotebook()->recommended())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":contactId"),
            ((notebook.contact() && notebook.contact()->id())
                 ? *notebook.contact()->id()
                 : nullValue));

        query.bindValue(
            QStringLiteral(":recipientReminderNotifyEmail"),
            (notebook.recipientSettings()
                 ? (notebook.recipientSettings()->reminderNotifyEmail()
                        ? static_cast<int>(*notebook.recipientSettings()
                                                ->reminderNotifyEmail())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":recipientReminderNotifyInApp"),
            (notebook.recipientSettings()
                 ? (notebook.recipientSettings()->reminderNotifyInApp()
                        ? static_cast<int>(*notebook.recipientSettings()
                                                ->reminderNotifyInApp())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":recipientInMyList"),
            (notebook.recipientSettings()
                 ? (notebook.recipientSettings()->inMyList()
                        ? static_cast<int>(
                              *notebook.recipientSettings()->inMyList())
                        : nullValue)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":recipientStack"),
            (notebook.recipientSettings()
                 ? (notebook.recipientSettings()->stack()
                        ? *notebook.recipientSettings()->stack()
                        : nullValue)
                 : nullValue));

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    {
        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(
            QString::fromUtf8(
                "DELETE FROM NotebookRestrictions WHERE localUid='%1'")
                .arg(localId));
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (notebook.restrictions()) {
        ErrorString error;
        if (!insertOrReplaceNotebookRestrictions(
                localId, *notebook.restrictions(), error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }

    if (notebook.guid()) {
        const QString guid = sqlEscapeString(*notebook.guid());
        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(
            QString::fromUtf8(
                "DELETE FROM SharedNotebooks WHERE "
                "sharedNotebookNotebookGuid='%1'").arg(guid));
        DATABASE_CHECK_AND_SET_ERROR()

        if (notebook.sharedNotebooks() &&
            !notebook.sharedNotebooks()->isEmpty())
        {
            const auto & sharedNotebooks = *notebook.sharedNotebooks();
            for (int i = 0, size = sharedNotebooks.size(); i < size; ++i) {
                const auto & sharedNotebook = sharedNotebooks[i];
                if (!sharedNotebook.id()) {
                    QNWARNING(
                        "local_storage",
                        "Found shared notebook without primary identifier of "
                            << "the share set, skipping it: "
                            << sharedNotebook);
                    continue;
                }

                ErrorString error;
                if (!insertOrReplaceSharedNotebook(sharedNotebook, error)) {
                    errorDescription.base() = errorPrefix.base();
                    errorDescription.appendBase(error.base());
                    errorDescription.appendBase(error.additionalBases());
                    errorDescription.details() = error.details();
                    return res;
                }
            }
        }
    }

    return transaction.commit(errorDescription);
}

bool LocalStorageManagerPrivate::checkAndPrepareNotebookCountQuery() const
{
    if (Q_LIKELY(m_getNotebookCountQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage", "Preparing SQL query to get the count of notebooks");

    m_getNotebookCountQuery = QSqlQuery(m_sqlDatabase);
    const QString queryString =
        QStringLiteral("SELECT COUNT(*) FROM Notebooks");
    const bool res = m_getNotebookCountQuery.prepare(queryString);
    if (res) {
        m_getNotebookCountQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceNotebookQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNotebookQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "notebook");

    m_insertOrReplaceNotebookQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Notebooks"
        "(localUid, guid, linkedNotebookGuid, "
        "updateSequenceNumber, notebookName, notebookNameUpper, "
        "creationTimestamp, modificationTimestamp, isDirty, "
        "isLocal, isDefault, isLastUsed, isFavorited, "
        "publishingUri, publishingNoteSortOrder, "
        "publishingAscendingSort, publicDescription, isPublished, "
        "stack, businessNotebookDescription, "
        "businessNotebookPrivilegeLevel, "
        "businessNotebookIsRecommended, contactId, "
        "recipientReminderNotifyEmail, recipientReminderNotifyInApp, "
        "recipientInMyList, recipientStack) "
        "VALUES(:localUid, :guid, :linkedNotebookGuid, "
        ":updateSequenceNumber, :notebookName, :notebookNameUpper, "
        ":creationTimestamp, :modificationTimestamp, :isDirty, "
        ":isLocal, :isDefault, :isLastUsed, :isFavorited, "
        ":publishingUri, :publishingNoteSortOrder, "
        ":publishingAscendingSort, :publicDescription, "
        ":isPublished, :stack, :businessNotebookDescription, "
        ":businessNotebookPrivilegeLevel, "
        ":businessNotebookIsRecommended, :contactId, "
        ":recipientReminderNotifyEmail, "
        ":recipientReminderNotifyInApp, :recipientInMyList, "
        ":recipientStack)");

    const bool res = m_insertOrReplaceNotebookQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceNotebookQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceNotebookRestrictionsQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNotebookRestrictionsQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "notebook restrictions");

    m_insertOrReplaceNotebookRestrictionsQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NotebookRestrictions"
        "(localUid, noReadNotes, noCreateNotes, noUpdateNotes, "
        "noExpungeNotes, noShareNotes, noEmailNotes, "
        "noSendMessageToRecipients, noUpdateNotebook, "
        "noExpungeNotebook, noSetDefaultNotebook, "
        "noSetNotebookStack, noPublishToPublic, "
        "noPublishToBusinessLibrary, noCreateTags, noUpdateTags, "
        "noExpungeTags, noSetParentTag, noCreateSharedNotebooks, "
        "updateWhichSharedNotebookRestrictions, "
        "expungeWhichSharedNotebookRestrictions) "
        "VALUES(:localUid, :noReadNotes, :noCreateNotes, "
        ":noUpdateNotes, :noExpungeNotes, :noShareNotes, "
        ":noEmailNotes, :noSendMessageToRecipients, "
        ":noUpdateNotebook, :noExpungeNotebook, "
        ":noSetDefaultNotebook, :noSetNotebookStack, "
        ":noPublishToPublic, :noPublishToBusinessLibrary, "
        ":noCreateTags, :noUpdateTags, :noExpungeTags, "
        ":noSetParentTag, :noCreateSharedNotebooks, "
        ":updateWhichSharedNotebookRestrictions, "
        ":expungeWhichSharedNotebookRestrictions)");

    const bool res =
        m_insertOrReplaceNotebookRestrictionsQuery.prepare(queryString);

    if (res) {
        m_insertOrReplaceNotebookRestrictionsQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceSharedNotebookQuery()
{
    if (Q_LIKELY(m_insertOrReplaceSharedNotebookQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace shared notebook");

    m_insertOrReplaceSharedNotebookQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO SharedNotebooks"
        "(sharedNotebookShareId, sharedNotebookUserId, "
        "sharedNotebookNotebookGuid, sharedNotebookEmail, "
        "sharedNotebookCreationTimestamp, "
        "sharedNotebookModificationTimestamp, "
        "sharedNotebookGlobalId, sharedNotebookUsername, "
        "sharedNotebookPrivilegeLevel, "
        "sharedNotebookRecipientReminderNotifyEmail, "
        "sharedNotebookRecipientReminderNotifyInApp, "
        "sharedNotebookSharerUserId, "
        "sharedNotebookRecipientUsername, "
        "sharedNotebookRecipientUserId, "
        "sharedNotebookRecipientIdentityId, "
        "sharedNotebookAssignmentTimestamp, indexInNotebook) "
        "VALUES(:sharedNotebookShareId, :sharedNotebookUserId, "
        ":sharedNotebookNotebookGuid, :sharedNotebookEmail, "
        ":sharedNotebookCreationTimestamp, "
        ":sharedNotebookModificationTimestamp, "
        ":sharedNotebookGlobalId, :sharedNotebookUsername, "
        ":sharedNotebookPrivilegeLevel, "
        ":sharedNotebookRecipientReminderNotifyEmail, "
        ":sharedNotebookRecipientReminderNotifyInApp, "
        ":sharedNotebookSharerUserId, "
        ":sharedNotebookRecipientUsername, "
        ":sharedNotebookRecipientUserId, "
        ":sharedNotebookRecipientIdentityId, "
        ":sharedNotebookAssignmentTimestamp, :indexInNotebook) ");

    const bool res = m_insertOrReplaceSharedNotebookQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceSharedNotebookQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::insertOrReplaceLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription)
{
    // NOTE: this method expects to be called after the linked notebook
    // is already checked for sanity ot its parameters

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace linked notebook"));

    bool res = checkAndPrepareInsertOrReplaceLinkedNotebookQuery();
    QSqlQuery & query = m_insertOrReplaceLinkedNotebookQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(
        QStringLiteral(":guid"),
        (linkedNotebook.guid() ? *linkedNotebook.guid() : nullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (linkedNotebook.updateSequenceNum()
             ? *linkedNotebook.updateSequenceNum()
             : nullValue));

    query.bindValue(
        QStringLiteral(":shareName"),
        (linkedNotebook.shareName() ? *linkedNotebook.shareName() : nullValue));

    query.bindValue(
        QStringLiteral(":username"),
        (linkedNotebook.username() ? *linkedNotebook.username() : nullValue));

    query.bindValue(
        QStringLiteral(":shardId"),
        (linkedNotebook.shardId() ? *linkedNotebook.shardId() : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookGlobalId"),
        (linkedNotebook.sharedNotebookGlobalId()
             ? *linkedNotebook.sharedNotebookGlobalId()
             : nullValue));

    query.bindValue(
        QStringLiteral(":uri"),
        (linkedNotebook.uri() ? *linkedNotebook.uri() : nullValue));

    query.bindValue(
        QStringLiteral(":noteStoreUrl"),
        (linkedNotebook.noteStoreUrl() ? *linkedNotebook.noteStoreUrl()
                                       : nullValue));

    query.bindValue(
        QStringLiteral(":webApiUrlPrefix"),
        (linkedNotebook.webApiUrlPrefix() ? *linkedNotebook.webApiUrlPrefix()
                                          : nullValue));

    query.bindValue(
        QStringLiteral(":stack"),
        (linkedNotebook.stack() ? *linkedNotebook.stack() : nullValue));

    query.bindValue(
        QStringLiteral(":businessId"),
        (linkedNotebook.businessId() ? *linkedNotebook.businessId()
                                     : nullValue));

    query.bindValue(
        QStringLiteral(":isDirty"),
        (linkedNotebook.isLocallyModified() ? 1 : 0));

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::checkAndPrepareGetLinkedNotebookCountQuery()
    const
{
    if (Q_LIKELY(m_getLinkedNotebookCountQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to get the count of linked "
            << "notebooks");

    m_getLinkedNotebookCountQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString =
        QStringLiteral("SELECT COUNT(*) FROM LinkedNotebooks");

    const bool res = m_getLinkedNotebookCountQuery.prepare(queryString);
    if (res) {
        m_getLinkedNotebookCountQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceLinkedNotebookQuery()
{
    if (Q_LIKELY(m_insertOrReplaceLinkedNotebookQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace linked "
            << "notebook");

    m_insertOrReplaceLinkedNotebookQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO LinkedNotebooks "
        "(guid, updateSequenceNumber, shareName, "
        "username, shardId, sharedNotebookGlobalId, "
        "uri, noteStoreUrl, webApiUrlPrefix, stack, "
        "businessId, isDirty) VALUES(:guid, "
        ":updateSequenceNumber, :shareName, :username, "
        ":shardId, :sharedNotebookGlobalId, :uri, "
        ":noteStoreUrl, :webApiUrlPrefix, :stack, "
        ":businessId, :isDirty)");

    const bool res = m_insertOrReplaceLinkedNotebookQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceLinkedNotebookQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::getNoteLocalIdFromResource(
    const qevercloud::Resource & resource, QString & noteLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getNoteLocalIdFromResource: resource = "
            << resource);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get note local id for resource"));

    noteLocalId.resize(0);

    if (!resource.noteLocalId().isEmpty()) {
        noteLocalId = resource.noteLocalId();
        return true;
    }

    QNTRACE(
        "local_storage",
        "Resource doesn't have the parent local id, "
            << "trying to deduce it from note-resource linkage");

    QString column, id;
    if (resource.guid()) {
        column = QStringLiteral("resource");
        id = *resource.guid();

        if (!checkGuid(id)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("resource's guid is invalid"));
            errorDescription.details() = id;
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }
    else {
        column = QStringLiteral("localResource");
        id = resource.localId();

        if (id.isEmpty()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("both resource's local id and guid are empty"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    id = sqlEscapeString(id);

    QString queryString =
        QString::fromUtf8("SELECT localNote FROM NoteResources WHERE %1='%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        SET_NO_DATA_FOUND();
        return false;
    }

    noteLocalId = query.record().value(QStringLiteral("localNote")).toString();
    return true;
}

bool LocalStorageManagerPrivate::getNotebookLocalIdFromNote(
    const qevercloud::Note & note, QString & notebookLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getNotebookLocalIdFromNote: "
            << "note local id = " << note.localId() << ", note guid = "
            << (note.guid() ? *note.guid() : QStringLiteral("<null>")));

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get notebook local id for note"));

    notebookLocalId.resize(0);

    if (!note.notebookLocalId().isEmpty()) {
        notebookLocalId = note.notebookLocalId();
        QNTRACE(
            "local_storage",
            "Notebook local id taken from note: " << notebookLocalId);
        return true;
    }

    QNTRACE(
        "local_storage",
        "Note doesn't have the parent local id, "
            << "trying to deduce it from guid");

    if (note.notebookGuid()) {
        const QString notebookGuid = sqlEscapeString(*note.notebookGuid());
        const QString queryString =
            QString::fromUtf8(
                "SELECT localUid FROM Notebooks WHERE guid = '%1'")
                .arg(notebookGuid);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()

        if (!query.next()) {
            SET_NO_DATA_FOUND();
            return false;
        }

        notebookLocalId =
            query.record().value(QStringLiteral("localUid")).toString();

        QNTRACE(
            "local_storage",
            "Notebook local id deduced from notebook's guid "
                << notebookGuid << ": " << notebookLocalId);
    }
    else {
        QString column, id;
        if (note.guid()) {
            column = QStringLiteral("guid");
            id = *note.guid();
        }
        else {
            column = QStringLiteral("localUid");
            id = note.localId();
        }

        id = sqlEscapeString(id);

        const QString queryString =
            QString::fromUtf8(
                "SELECT notebookLocalUid FROM Notes WHERE %1='%2'")
                .arg(column, id);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()

        if (!query.next()) {
            SET_NO_DATA_FOUND();
            return false;
        }

        notebookLocalId =
            query.record().value(QStringLiteral("notebookLocalUid")).toString();

        QNTRACE(
            "local_storage",
            "Notebook local id deduced from note's "
                << column << " " << id << ": " << notebookLocalId);
    }

    if (notebookLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("found notebook local id is empty"));
        QNDEBUG("local_storage", errorDescription << ", note: " << note);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::getNotebookGuidForNote(
    const qevercloud::Note & note, QString & notebookGuid,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getNotebookGuidForNote: "
            << "note local id = " << note.localId() << ", note guid = "
            << (note.guid() ? *note.guid() : QStringLiteral("<null>")));

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get notebook guid for note"));

    notebookGuid.resize(0);

    if (note.notebookGuid()) {
        notebookGuid = *note.notebookGuid();
        return true;
    }

    QNTRACE(
        "local_storage",
        "Note doesn't have the notebook guid, trying to "
            << "deduce it from notebook local id");

    if (note.notebookLocalId().isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("note has neither parent local id nor notebook guid"));
        QNDEBUG("local_storage", errorDescription << ", note: " << note);
        return false;
    }

    const QString notebookLocalId = sqlEscapeString(note.notebookLocalId());

    const QString queryString =
        QString::fromUtf8("SELECT guid FROM Notebooks where localUid = '%1'")
            .arg(notebookLocalId);

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (!query.next()) {
        SET_NO_DATA_FOUND();
        return false;
    }

    notebookGuid = query.record().value(QStringLiteral("guid")).toString();

    QNTRACE(
        "local_storage",
        "Found notebook guid corresponding to local id "
            << notebookLocalId << ": " << notebookGuid);
    return true;
}

bool LocalStorageManagerPrivate::getNotebookLocalIdForGuid(
    const QString & notebookGuid, QString & notebookLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getNotebookLocalIdForGuid: "
            << "notebook guid = " << notebookGuid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get notebook local id for guid"));

    const QString queryString =
        QString::fromUtf8("SELECT localUid FROM Notebooks WHERE guid = '%1'")
            .arg(sqlEscapeString(notebookGuid));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        notebookLocalId =
            query.record().value(QStringLiteral("localUid")).toString();
    }

    if (notebookLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no existing local id corresponding to notebook's guid "
                       "was found"));
        errorDescription.details() = notebookGuid;
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::getNoteLocalIdForGuid(
    const QString & noteGuid, QString & noteLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getNoteLocalIdForGuid: note guid = "
            << noteGuid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get note local id for guid"));

    const QString queryString =
        QString::fromUtf8("SELECT localUid FROM Notes WHERE guid='%1'")
            .arg(sqlEscapeString(noteGuid));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        noteLocalId =
            query.record().value(QStringLiteral("localUid")).toString();
    }

    if (noteLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no existing local id corresponding to note's guid was "
                       "found"));
        errorDescription.details() = noteGuid;
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::getNoteGuidForLocalId(
    const QString & noteLocalUid, QString & noteGuid,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getNoteGuidForLocalId: note local "
            << "uid = " << noteLocalUid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get note guid for local id"));

    const QString queryString =
        QString::fromUtf8("SELECT guid FROM Notes WHERE localUid='%1'")
            .arg(sqlEscapeString(noteLocalUid));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        noteGuid = query.record().value(QStringLiteral("guid")).toString();
    }

    return true;
}

bool LocalStorageManagerPrivate::getTagLocalIdForGuid(
    const QString & tagGuid, QString & tagLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getTagLocalIdForGuid: tag guid = "
            << tagGuid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get tag local id for guid"));

    const QString queryString =
        QString::fromUtf8("SELECT localUid FROM Tags WHERE guid = '%1'")
            .arg(sqlEscapeString(tagGuid));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        tagLocalId =
            query.record().value(QStringLiteral("localUid")).toString();
    }

    if (tagLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no existing local id corresponding to tag's guid was "
                       "found"));
        errorDescription.details() = tagGuid;
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::getResourceLocalIdForGuid(
    const QString & resourceGuid, QString & resourceLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getResourceLocalIdForGuid: "
            << "resource guid = " << resourceGuid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get resource local id for guid"));

    const QString queryString =
        QString::fromUtf8(
            "SELECT resourceLocalUid FROM Resources WHERE resourceGuid = '%1'")
            .arg(sqlEscapeString(resourceGuid));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        resourceLocalId =
            query.record().value(QStringLiteral("resourceLocalUid")).toString();
    }

    if (resourceLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no existing local id corresponding to resource's "
                       "guid was found"));
        errorDescription.details() = resourceGuid;
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::getSavedSearchLocalIdForGuid(
    const QString & savedSearchGuid, QString & savedSearchLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::getSavedSearchLocalIdForGuid: "
            << "saved search guid = " << savedSearchGuid);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get saved search local id for guid"));

    const QString queryString =
        QString::fromUtf8(
            "SELECT localUid FROM SavedSearches WHERE guid = '%1'")
            .arg(sqlEscapeString(savedSearchGuid));

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    if (query.next()) {
        savedSearchLocalId =
            query.record().value(QStringLiteral("localUid")).toString();
    }

    if (savedSearchLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("no existing local id corresponding "
                       "to saved search's guid was found"));
        errorDescription.details() = savedSearchGuid;
        QNDEBUG("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceNote(
    qevercloud::Note & note, const UpdateNoteOptions options,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::insertOrReplaceNote: update tags = "
            << ((options & UpdateNoteOption::UpdateTags) ? "true" : "false")
            << ", update resource metadata = "
            << ((options & UpdateNoteOption::UpdateResourceMetadata) ? "true"
                                                                     : "false")
            << ", update resource binary data = "
            << ((options & UpdateNoteOption::UpdateResourceBinaryData)
                    ? "true"
                    : "false")
            << ", note local id = " << note.localId());

    QNTRACE("local_storage", note);

    // NOTE: this method expects to be called after the note is already checked
    // for sanity of its parameters!

    ErrorString errorPrefix(QT_TR_NOOP("can't insert or replace note"));

    Transaction transaction(m_sqlDatabase, *this, Transaction::Type::Exclusive);

    const QVariant nullValue;
    const QString localId = sqlEscapeString(note.localId());

    QString notebookLocalUid =
        (!note.notebookLocalId().isEmpty()
         ? sqlEscapeString(note.notebookLocalId())
         : QString());

    // Special logic needs to be applied if guid is being cleared from the note
    bool noteGuidIsBeingCleared = false;
    if (!note.guid()) {
        QString noteGuid;

        const bool res =
            getNoteGuidForLocalId(note.localId(), noteGuid, errorDescription);

        if (!res) {
            return false;
        }

        noteGuidIsBeingCleared = !noteGuid.isEmpty();
    }

    QNDEBUG(
        "local_storage",
        "Note guid is being cleared = "
            << (noteGuidIsBeingCleared ? "true" : "false"));

    if (noteGuidIsBeingCleared) {
        if (note.resources() &&
            (options & UpdateNoteOption::UpdateResourceMetadata)) {
            QList<qevercloud::Resource> resources = *note.resources();
            for (const auto & resource: qAsConst(resources)) {
                if (Q_UNLIKELY(resource.noteGuid())) {
                    errorDescription = errorPrefix;
                    errorDescription.appendBase(
                        QT_TR_NOOP("note's guid is being cleared but one of "
                                   "note's resources has non-empty note guid"));
                    if (resource.attributes() &&
                        resource.attributes()->fileName()) {
                        errorDescription.details() =
                            *resource.attributes()->fileName();
                    }

                    QNWARNING("local_storage", errorDescription);
                    return false;
                }

                if (Q_UNLIKELY(resource.guid())) {
                    errorDescription = errorPrefix;
                    errorDescription.appendBase(
                        QT_TR_NOOP("note's guid is being cleared but one of "
                                   "note's resources has non-empty guid"));
                    if (resource.attributes() &&
                        resource.attributes()->fileName()) {
                        errorDescription.details() =
                            *resource.attributes()->fileName();
                    }

                    QNWARNING("local_storage", errorDescription);
                    return false;
                }
            }
        }

        const QString queryString =
            QString::fromUtf8(
                "UPDATE Notes SET guid = NULL WHERE localUid='%1'")
                .arg(localId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    // Update common table with Note properties
    {
        bool res = checkAndPrepareInsertOrReplaceNoteQuery();
        QSqlQuery & query = m_insertOrReplaceNoteQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        QString titleNormalized;
        if (note.title()) {
            titleNormalized = note.title()->toLower();
            m_stringUtils.removeDiacritics(titleNormalized);
        }

        query.bindValue(QStringLiteral(":localUid"), localId);

        query.bindValue(
            QStringLiteral(":guid"), (note.guid() ? *note.guid() : nullValue));

        query.bindValue(
            QStringLiteral(":updateSequenceNumber"),
            (note.updateSequenceNum() ? *note.updateSequenceNum() : nullValue));

        query.bindValue(
            QStringLiteral(":isDirty"), (note.isLocallyModified() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":isLocal"), (note.isLocalOnly() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":isFavorited"),
            (note.isLocallyFavorited() ? 1 : 0));

        query.bindValue(
            QStringLiteral(":title"),
            (note.title() ? *note.title() : nullValue));

        query.bindValue(
            QStringLiteral(":titleNormalized"),
            (titleNormalized.isEmpty() ? nullValue : titleNormalized));

        query.bindValue(
            QStringLiteral(":content"),
            (note.content() ? *note.content() : nullValue));

        query.bindValue(
            QStringLiteral(":contentLength"),
            (note.contentLength() ? *note.contentLength() : nullValue));

        query.bindValue(
            QStringLiteral(":contentHash"),
            (note.contentHash() ? *note.contentHash() : nullValue));

        query.bindValue(
            QStringLiteral(":contentContainsFinishedToDo"),
            (note.content() ? static_cast<int>(noteContentContainsCheckedToDo(
                                  *note.content()))
                            : nullValue));

        query.bindValue(
            QStringLiteral(":contentContainsUnfinishedToDo"),
            (note.content() ? static_cast<int>(noteContentContainsUncheckedToDo(
                                  *note.content()))
                            : nullValue));

        query.bindValue(
            QStringLiteral(":contentContainsEncryption"),
            (note.content()
                 ? static_cast<int>(
                       noteContentContainsEncryptedFragments(*note.content()))
                 : nullValue));

        if (note.content()) {
            ErrorString error;

            const auto plainTextAndListOfWords =
                noteContentToPlainTextAndListOfWords(*note.content(), &error);

            if (!error.isEmpty()) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(QT_TR_NOOP(
                    "can't get note's plain text and list of words"));
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING(
                    "local_storage", errorDescription << ", note: " << note);
                return false;
            }

            QString listOfWords =
                plainTextAndListOfWords.second.join(QStringLiteral(" "));

            m_stringUtils.removePunctuation(listOfWords);
            listOfWords = listOfWords.toLower();
            m_stringUtils.removeDiacritics(listOfWords);

            query.bindValue(
                QStringLiteral(":contentPlainText"),
                (plainTextAndListOfWords.first.isEmpty()
                     ? nullValue
                     : plainTextAndListOfWords.first));

            query.bindValue(
                QStringLiteral(":contentListOfWords"),
                (listOfWords.isEmpty() ? nullValue : listOfWords));
        }
        else {
            query.bindValue(QStringLiteral(":contentPlainText"), nullValue);
            query.bindValue(QStringLiteral(":contentListOfWords"), nullValue);
        }

        query.bindValue(
            QStringLiteral(":creationTimestamp"),
            (note.created() ? *note.created() : nullValue));

        query.bindValue(
            QStringLiteral(":modificationTimestamp"),
            (note.updated() ? *note.updated() : nullValue));

        query.bindValue(
            QStringLiteral(":deletionTimestamp"),
            (note.deleted() ? *note.deleted() : nullValue));

        query.bindValue(
            QStringLiteral(":isActive"),
            (note.active() ? (*note.active() ? 1 : 0) : nullValue));

        query.bindValue(
            QStringLiteral(":hasAttributes"), (note.attributes() ? 1 : 0));

        const QByteArray thumbnailData = note.thumbnailData();

        query.bindValue(
            QStringLiteral(":thumbnail"),
            (thumbnailData.isEmpty() ? nullValue : thumbnailData));

        query.bindValue(
            QStringLiteral(":notebookLocalUid"),
            (notebookLocalUid.isEmpty() ? nullValue : notebookLocalUid));

        query.bindValue(
            QStringLiteral(":notebookGuid"),
            (note.notebookGuid() ? *note.notebookGuid() : nullValue));

        if (note.attributes()) {
            const auto & attributes = *note.attributes();

#define BIND_ATTRIBUTE(name)                                                   \
    query.bindValue(                                                           \
        QStringLiteral(":" #name),                                             \
        (attributes.name() ? *attributes.name() : nullValue))

            BIND_ATTRIBUTE(subjectDate);
            BIND_ATTRIBUTE(latitude);
            BIND_ATTRIBUTE(longitude);
            BIND_ATTRIBUTE(altitude);
            BIND_ATTRIBUTE(author);
            BIND_ATTRIBUTE(source);
            BIND_ATTRIBUTE(sourceURL);
            BIND_ATTRIBUTE(sourceApplication);
            BIND_ATTRIBUTE(shareDate);
            BIND_ATTRIBUTE(reminderOrder);
            BIND_ATTRIBUTE(reminderDoneTime);
            BIND_ATTRIBUTE(reminderTime);
            BIND_ATTRIBUTE(placeName);
            BIND_ATTRIBUTE(contentClass);
            BIND_ATTRIBUTE(lastEditedBy);
            BIND_ATTRIBUTE(creatorId);
            BIND_ATTRIBUTE(lastEditorId);
            BIND_ATTRIBUTE(sharedWithBusiness);
            BIND_ATTRIBUTE(conflictSourceNoteGuid);
            BIND_ATTRIBUTE(noteTitleQuality);

#undef BIND_ATTRIBUTE

            if (attributes.applicationData()) {
                const auto & lazyMap = *attributes.applicationData();

                if (lazyMap.keysOnly()) {
                    const QSet<QString> & keysOnly = *lazyMap.keysOnly();
                    QString keysOnlyString;

                    for (const auto & key: keysOnly) {
                        keysOnlyString += QStringLiteral("'");
                        keysOnlyString += key;
                        keysOnlyString += QStringLiteral("'");
                    }

                    QNDEBUG(
                        "local_storage",
                        "Application data keys only string: "
                            << keysOnlyString);

                    query.bindValue(
                        QStringLiteral(":applicationDataKeysOnly"),
                        keysOnlyString);
                }
                else {
                    query.bindValue(
                        QStringLiteral(":applicationDataKeysOnly"), nullValue);
                }

                if (lazyMap.fullMap()) {
                    const QMap<QString, QString> & fullMap = *lazyMap.fullMap();
                    QString fullMapKeysString;
                    QString fullMapValuesString;

                    for (const auto & it: qevercloud::toRange(fullMap)) {
                        fullMapKeysString += QStringLiteral("'");
                        fullMapKeysString += it.key();
                        fullMapKeysString += QStringLiteral("'");

                        fullMapValuesString += QStringLiteral("'");
                        fullMapValuesString += it.value();
                        fullMapValuesString += QStringLiteral("'");
                    }

                    QNDEBUG(
                        "local_storage",
                        "Application data map keys: "
                            << fullMapKeysString
                            << ", application data map values: "
                            << fullMapValuesString);

                    query.bindValue(
                        QStringLiteral(":applicationDataKeysMap"),
                        fullMapKeysString);

                    query.bindValue(
                        QStringLiteral(":applicationDataValues"),
                        fullMapValuesString);
                }
                else {
                    query.bindValue(
                        QStringLiteral(":applicationDataKeysMap"), nullValue);

                    query.bindValue(
                        QStringLiteral(":applicationDataValues"), nullValue);
                }
            }
            else {
                query.bindValue(
                    QStringLiteral(":applicationDataKeysOnly"), nullValue);

                query.bindValue(
                    QStringLiteral(":applicationDataKeysMap"), nullValue);

                query.bindValue(
                    QStringLiteral(":applicationDataValues"), nullValue);
            }

            if (attributes.classifications()) {
                const auto & classifications = *attributes.classifications();
                QString classificationKeys, classificationValues;
                for (const auto & it: qevercloud::toRange(classifications)) {
                    classificationKeys += QStringLiteral("'");
                    classificationKeys += it.key();
                    classificationKeys += QStringLiteral("'");

                    classificationValues += QStringLiteral("'");
                    classificationValues += it.value();
                    classificationValues += QStringLiteral("'");
                }

                QNDEBUG(
                    "local_storage",
                    "Classification keys: " << classificationKeys
                                            << ", classification values"
                                            << classificationValues);

                query.bindValue(
                    QStringLiteral(":classificationKeys"), classificationKeys);

                query.bindValue(
                    QStringLiteral(":classificationValues"),
                    classificationValues);
            }
            else {
                query.bindValue(
                    QStringLiteral(":classificationKeys"), nullValue);

                query.bindValue(
                    QStringLiteral(":classificationValues"), nullValue);
            }
        }
        else {
#define BIND_NULL_ATTRIBUTE(name)                                              \
    query.bindValue(QStringLiteral(":" #name), nullValue)

            BIND_NULL_ATTRIBUTE(subjectDate);
            BIND_NULL_ATTRIBUTE(latitude);
            BIND_NULL_ATTRIBUTE(longitude);
            BIND_NULL_ATTRIBUTE(altitude);
            BIND_NULL_ATTRIBUTE(author);
            BIND_NULL_ATTRIBUTE(source);
            BIND_NULL_ATTRIBUTE(sourceURL);
            BIND_NULL_ATTRIBUTE(sourceApplication);
            BIND_NULL_ATTRIBUTE(shareDate);
            BIND_NULL_ATTRIBUTE(reminderOrder);
            BIND_NULL_ATTRIBUTE(reminderDoneTime);
            BIND_NULL_ATTRIBUTE(reminderTime);
            BIND_NULL_ATTRIBUTE(placeName);
            BIND_NULL_ATTRIBUTE(contentClass);
            BIND_NULL_ATTRIBUTE(lastEditedBy);
            BIND_NULL_ATTRIBUTE(creatorId);
            BIND_NULL_ATTRIBUTE(lastEditorId);
            BIND_NULL_ATTRIBUTE(sharedWithBusiness);
            BIND_NULL_ATTRIBUTE(conflictSourceNoteGuid);
            BIND_NULL_ATTRIBUTE(noteTitleQuality);
            BIND_NULL_ATTRIBUTE(applicationDataKeysOnly);
            BIND_NULL_ATTRIBUTE(applicationDataKeysMap);
            BIND_NULL_ATTRIBUTE(applicationDataValues);
            BIND_NULL_ATTRIBUTE(classificationKeys);
            BIND_NULL_ATTRIBUTE(classificationValues);

#undef BIND_NULL_ATTRIBUTE
        }

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (note.restrictions()) {
        if (!insertOrReplaceNoteRestrictions(
                localId, *note.restrictions(), errorDescription))
        {
            QNWARNING("local_storage", "Note: " << note);
            return false;
        }
    }
    else {
        const QString queryString =
            QString::fromUtf8(
                "DELETE FROM NoteRestrictions WHERE noteLocalUid='%1'")
                .arg(localId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (note.limits()) {
        const qevercloud::NoteLimits & limits = *note.limits();
        if (!insertOrReplaceNoteLimits(localId, limits, errorDescription)) {
            QNWARNING("local_storage", "Note: " << note);
            return false;
        }
    }
    else {
        const QString queryString =
            QString::fromUtf8("DELETE FROM NoteLimits WHERE noteLocalUid='%1'")
                .arg(localId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (note.guid()) {
        // Clear shared notes for a given note first, update them (if any)
        // second
        {
            const QString noteGuid = sqlEscapeString(*note.guid());
            const QString queryString =
                QString::fromUtf8(
                    "DELETE FROM SharedNotes WHERE sharedNoteNoteGuid='%1'")
                    .arg(noteGuid);

            QSqlQuery query(m_sqlDatabase);
            const bool res = query.exec(queryString);
            DATABASE_CHECK_AND_SET_ERROR()
        }

        if (note.sharedNotes() && !note.sharedNotes()->isEmpty()) {
            const auto sharedNotes = *note.sharedNotes();
            int index = 0;
            for (const auto & sharedNote: qAsConst(sharedNotes)) {
                if (!insertOrReplaceSharedNote(
                        sharedNote, *note.guid(), index, errorDescription))
                {
                    QNWARNING("local_storage", "Note: " << note);
                    return false;
                }
                ++index;
            }
        }
    }

    if (options & UpdateNoteOption::UpdateTags) {
        // Clear note-to-tag binding first, update them second
        {
            const QString queryString =
                QString::fromUtf8("DELETE From NoteTags WHERE localNote='%1'")
                    .arg(localId);

            QSqlQuery query(m_sqlDatabase);
            bool res = query.exec(queryString);
            DATABASE_CHECK_AND_SET_ERROR()
        }

        const QStringList tagLocalIds = note.tagLocalIds();

        const bool hasTagLocalIds = !tagLocalIds.isEmpty();
        const bool hasTagGuids = (note.tagGuids() != std::nullopt);

        if (hasTagLocalIds || hasTagGuids) {
            QStringList tagIds;
            if (hasTagLocalIds) {
                tagIds = tagLocalIds;
            }
            else {
                tagIds = *note.tagGuids();
            }

            const int numTagIds = tagIds.size();

            QStringList tagComplementedIds;
            tagComplementedIds.reserve(numTagIds);

            bool res = checkAndPrepareInsertOrReplaceNoteIntoNoteTagsQuery();
            QSqlQuery & query = m_insertOrReplaceNoteIntoNoteTagsQuery;
            DATABASE_CHECK_AND_SET_ERROR()

            ErrorString error;

            int tagIndexInNote = 0;
            for (const auto & tagId: qAsConst(tagIds)) {
                // NOTE: the behavior expressed here is valid since tags are
                // synchronized before notes so they must exist within the local
                // storage database; if they don't then something went really
                // wrong

                qevercloud::Tag tag;
                if (hasTagLocalIds) {
                    tag.setLocalId(tagId);
                }
                else {
                    tag.setGuid(tagId);
                }

                error.clear();
                bool res = findTag(tag, error);
                if (!res) {
                    errorDescription.base() = errorPrefix.base();
                    errorDescription.appendBase(QString::fromUtf8(
                        QT_TR_NOOP("failed to find one of note's tags")));
                    errorDescription.appendBase(error.base());
                    errorDescription.appendBase(error.additionalBases());
                    errorDescription.details() = error.details();
                    QNWARNING(
                        "local_storage",
                        errorDescription << ", note: " << note);
                    return false;
                }

                if (hasTagLocalIds) {
                    if (tag.guid()) {
                        tagComplementedIds << *tag.guid();
                    }
                }
                else {
                    tagComplementedIds << tag.localId();
                }

                query.bindValue(QStringLiteral(":localNote"), localId);

                query.bindValue(
                    QStringLiteral(":note"),
                    (note.guid() ? *note.guid() : nullValue));

                query.bindValue(QStringLiteral(":localTag"), tag.localId());

                query.bindValue(
                    QStringLiteral(":tag"),
                    (tag.guid() ? *tag.guid() : nullValue));

                query.bindValue(
                    QStringLiteral(":tagIndexInNote"), tagIndexInNote);

                res = query.exec();
                DATABASE_CHECK_AND_SET_ERROR()

                ++tagIndexInNote;
            }

            if (hasTagLocalIds) {
                note.setTagGuids(tagComplementedIds);
            }
            else {
                note.setTagLocalIds(tagComplementedIds);
            }
        }

        // NOTE: don't even attempt fo find tags by their names because
        // qevercloud::Note.tagNames has the only purpose to provide tag names
        // alternatively to guids to NoteStore::createNote method
    }

    if (options & UpdateNoteOption::UpdateResourceMetadata) {
        if (!note.resources()) {
            QNDEBUG(
                "local_storage",
                "Deleting all resources the note might "
                    << "have had");

            const QString queryString =
                QString::fromUtf8(
                    "DELETE FROM Resources WHERE noteLocalUid='%1'")
                    .arg(localId);

            QSqlQuery query(m_sqlDatabase);
            const bool res = query.exec(queryString);
            DATABASE_CHECK_AND_SET_ERROR()

            ErrorString error;
            if (!removeResourceDataFilesForNote(localId, error)) {
                errorDescription = errorPrefix;
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                return false;
            }
        }
        else {
            if (!partialUpdateNoteResources(
                    localId, *note.resources(),
                    (options & UpdateNoteOption::UpdateResourceBinaryData),
                    errorDescription))
            {
                return false;
            }
        }
    }

    return transaction.commit(errorDescription);
}

bool LocalStorageManagerPrivate::insertOrReplaceSharedNote(
    const qevercloud::SharedNote & sharedNote, const QString & noteGuid,
    const int indexInNote, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::insertOrReplaceSharedNote: note guid = "
            << noteGuid << ", shared note: " << sharedNote);

    // NOTE: this method expects to be called after the shared note is already
    // checked for sanity of its parameters!

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace shared note"));

    bool res = checkAndPrepareInsertOrReplaceSharedNoteQuery();
    QSqlQuery & query = m_insertOrReplaceSharedNoteQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(QStringLiteral(":sharedNoteNoteGuid"), noteGuid);

    query.bindValue(
        QStringLiteral(":sharedNoteSharerUserId"),
        (sharedNote.sharerUserID() ? *sharedNote.sharerUserID() : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientIdentityId"),
        (sharedNote.recipientIdentity() ? sharedNote.recipientIdentity()->id()
                                        : nullValue));

    const std::optional<qevercloud::Contact> contact =
        (sharedNote.recipientIdentity()
             ? sharedNote.recipientIdentity()->contact()
             : std::nullopt);

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactName"),
        (contact ? contact->name().value_or(QString{}) : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactId"),
        (contact ? contact->id().value_or(QString{}) : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactType"),
        (contact ? (contact->type() ? static_cast<int>(*contact->type())
                                    : nullValue)
                 : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactPhotoUrl"),
        (contact ? contact->photoUrl().value_or(QString{}) : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactPhotoLastUpdated"),
        (contact ? (contact->photoLastUpdated() ? *contact->photoLastUpdated()
                                                : nullValue)
                 : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactMessagingPermit"),
        (contact ? (contact->messagingPermit() ? *contact->messagingPermit()
                                               : nullValue)
                 : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientContactMessagingPermitExpires"),
        (contact ? (contact->messagingPermitExpires()
                        ? *contact->messagingPermitExpires()
                        : nullValue)
                 : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientUserId"),
        (sharedNote.recipientIdentity()
             ? (sharedNote.recipientIdentity()->userId()
                    ? *sharedNote.recipientIdentity()->userId()
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientDeactivated"),
        (sharedNote.recipientIdentity()
             ? (sharedNote.recipientIdentity()->deactivated()
                    ? static_cast<int>(
                          *sharedNote.recipientIdentity()->deactivated())
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientSameBusiness"),
        (sharedNote.recipientIdentity()
             ? (sharedNote.recipientIdentity()->sameBusiness()
                    ? static_cast<int>(
                          *sharedNote.recipientIdentity()->sameBusiness())
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientBlocked"),
        (sharedNote.recipientIdentity()
             ? (sharedNote.recipientIdentity()->blocked()
                    ? static_cast<int>(
                          *sharedNote.recipientIdentity()->blocked())
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientUserConnected"),
        (sharedNote.recipientIdentity()
             ? (sharedNote.recipientIdentity()->userConnected()
                    ? static_cast<int>(
                          *sharedNote.recipientIdentity()->userConnected())
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteRecipientEventId"),
        (sharedNote.recipientIdentity()
             ? (sharedNote.recipientIdentity()->eventId()
                    ? static_cast<int>(
                          *sharedNote.recipientIdentity()->eventId())
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNotePrivilegeLevel"),
        (sharedNote.privilege() ? static_cast<int>(*sharedNote.privilege())
                                : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteCreationTimestamp"),
        (sharedNote.serviceCreated() ? *sharedNote.serviceCreated()
                                     : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteModificationTimestamp"),
        (sharedNote.serviceUpdated() ? *sharedNote.serviceUpdated()
                                     : nullValue));

    query.bindValue(
        QStringLiteral(":sharedNoteAssignmentTimestamp"),
        (sharedNote.serviceAssigned() ? *sharedNote.serviceAssigned()
                                      : nullValue));

    query.bindValue(QStringLiteral(":indexInNote"), indexInNote);

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceNoteRestrictions(
    const QString & noteLocalId,
    const qevercloud::NoteRestrictions & noteRestrictions,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace note restrictions"));

    bool res = checkAndPrepareInsertOrReplaceNoteRestrictionsQuery();
    QSqlQuery & query = m_insertOrReplaceNoteRestrictionsQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

#define BIND_RESTRICTION(column, name)                                         \
    query.bindValue(                                                           \
        QStringLiteral(":" #column),                                           \
        (noteRestrictions.name() ? (*noteRestrictions.name() ? 1 : 0)          \
                                 : nullValue))

    BIND_RESTRICTION(noUpdateNoteTitle, noUpdateTitle);
    BIND_RESTRICTION(noUpdateNoteContent, noUpdateContent);
    BIND_RESTRICTION(noEmailNote, noEmail);
    BIND_RESTRICTION(noShareNote, noShare);
    BIND_RESTRICTION(noShareNotePublicly, noSharePublicly);

#undef BIND_RESTRICTION

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceNoteLimits(
    const QString & noteLocalId, const qevercloud::NoteLimits & noteLimits,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace note limits"));

    bool res = checkAndPrepareInsertOrReplaceNoteLimitsQuery();
    QSqlQuery & query = m_insertOrReplaceNoteLimitsQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

#define BIND_LIMIT(limit)                                                      \
    query.bindValue(                                                           \
        QStringLiteral(":" #limit),                                            \
        (noteLimits.limit() ? *noteLimits.limit() : nullValue))

    BIND_LIMIT(noteResourceCountMax);
    BIND_LIMIT(uploadLimit);
    BIND_LIMIT(resourceSizeMax);
    BIND_LIMIT(noteSizeMax);
    BIND_LIMIT(uploaded);

#undef BIND_LIMIT

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceNoteQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNoteQueryPrepared)) {
        return true;
    }

    QNTRACE("local_storage", "Preparing SQL query to insert or replace note");

    m_insertOrReplaceNoteQuery = QSqlQuery(m_sqlDatabase);

    const QString columns = QStringLiteral(
        "localUid, guid, updateSequenceNumber, isDirty, "
        "isLocal, isFavorited, title, titleNormalized, content, "
        "contentLength, contentHash, contentPlainText, "
        "contentListOfWords, contentContainsFinishedToDo, "
        "contentContainsUnfinishedToDo, "
        "contentContainsEncryption, creationTimestamp, "
        "modificationTimestamp, deletionTimestamp, isActive, "
        "hasAttributes, thumbnail, notebookLocalUid, notebookGuid, "
        "subjectDate, latitude, longitude, altitude, author, "
        "source, sourceURL, sourceApplication, shareDate, "
        "reminderOrder, reminderDoneTime, reminderTime, placeName, "
        "contentClass, lastEditedBy, creatorId, lastEditorId, "
        "sharedWithBusiness, conflictSourceNoteGuid, "
        "noteTitleQuality, applicationDataKeysOnly, "
        "applicationDataKeysMap, applicationDataValues, "
        "classificationKeys, classificationValues");

    const QString values = QStringLiteral(
        ":localUid, :guid, :updateSequenceNumber, :isDirty, "
        ":isLocal, :isFavorited, :title, :titleNormalized, "
        ":content, :contentLength, :contentHash, "
        ":contentPlainText, :contentListOfWords, "
        ":contentContainsFinishedToDo, "
        ":contentContainsUnfinishedToDo, "
        ":contentContainsEncryption, :creationTimestamp, "
        ":modificationTimestamp, :deletionTimestamp, :isActive, "
        ":hasAttributes, :thumbnail, :notebookLocalUid, "
        ":notebookGuid, :subjectDate, :latitude, :longitude, "
        ":altitude, :author, :source, :sourceURL, "
        ":sourceApplication, :shareDate, :reminderOrder, "
        ":reminderDoneTime, :reminderTime, :placeName, "
        ":contentClass, :lastEditedBy, :creatorId, :lastEditorId, "
        ":sharedWithBusiness, :conflictSourceNoteGuid, "
        ":noteTitleQuality, :applicationDataKeysOnly, "
        ":applicationDataKeysMap, :applicationDataValues, "
        ":classificationKeys, :classificationValues");

    const QString queryString =
        QString::fromUtf8("INSERT OR REPLACE INTO Notes(%1) VALUES(%2)")
            .arg(columns, values);

    const bool res = m_insertOrReplaceNoteQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceNoteQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceSharedNoteQuery()
{
    if (Q_LIKELY(m_insertOrReplaceSharedNoteQueryPrepared)) {
        return true;
    }

    QNTRACE(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "the shared note");

    m_insertOrReplaceSharedNoteQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO SharedNotes ("
        "sharedNoteNoteGuid, "
        "sharedNoteSharerUserId, "
        "sharedNoteRecipientIdentityId, "
        "sharedNoteRecipientContactName, "
        "sharedNoteRecipientContactId, "
        "sharedNoteRecipientContactType, "
        "sharedNoteRecipientContactPhotoUrl, "
        "sharedNoteRecipientContactPhotoLastUpdated, "
        "sharedNoteRecipientContactMessagingPermit, "
        "sharedNoteRecipientContactMessagingPermitExpires, "
        "sharedNoteRecipientUserId, "
        "sharedNoteRecipientDeactivated, "
        "sharedNoteRecipientSameBusiness, "
        "sharedNoteRecipientBlocked, "
        "sharedNoteRecipientUserConnected, "
        "sharedNoteRecipientEventId, "
        "sharedNotePrivilegeLevel, "
        "sharedNoteCreationTimestamp, "
        "sharedNoteModificationTimestamp, "
        "sharedNoteAssignmentTimestamp, "
        "indexInNote) "
        "VALUES("
        ":sharedNoteNoteGuid, "
        ":sharedNoteSharerUserId, "
        ":sharedNoteRecipientIdentityId, "
        ":sharedNoteRecipientContactName, "
        ":sharedNoteRecipientContactId, "
        ":sharedNoteRecipientContactType, "
        ":sharedNoteRecipientContactPhotoUrl, "
        ":sharedNoteRecipientContactPhotoLastUpdated, "
        ":sharedNoteRecipientContactMessagingPermit, "
        ":sharedNoteRecipientContactMessagingPermitExpires, "
        ":sharedNoteRecipientUserId, "
        ":sharedNoteRecipientDeactivated, "
        ":sharedNoteRecipientSameBusiness, "
        ":sharedNoteRecipientBlocked, "
        ":sharedNoteRecipientUserConnected, "
        ":sharedNoteRecipientEventId, "
        ":sharedNotePrivilegeLevel, "
        ":sharedNoteCreationTimestamp, "
        ":sharedNoteModificationTimestamp, "
        ":sharedNoteAssignmentTimestamp, "
        ":indexInNote)");

    const bool res = m_insertOrReplaceSharedNoteQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceSharedNoteQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceNoteRestrictionsQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNoteRestrictionsQueryPrepared)) {
        return true;
    }

    QNTRACE(
        "local_storage",
        "Preparing SQL query to insert or replace note "
            << "restrictions");

    m_insertOrReplaceNoteRestrictionsQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NoteRestrictions "
        "(noteLocalUid, noUpdateNoteTitle, noUpdateNoteContent, "
        "noEmailNote, noShareNote, noShareNotePublicly) "
        "VALUES(:noteLocalUid, :noUpdateNoteTitle, "
        ":noUpdateNoteContent, :noEmailNote, "
        ":noShareNote, :noShareNotePublicly)");

    const bool res =
        m_insertOrReplaceNoteRestrictionsQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceNoteRestrictionsQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceNoteLimitsQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNoteLimitsQueryPrepared)) {
        return true;
    }

    QNTRACE(
        "local_storage",
        "Preparing SQL query to insert or replace note "
            << "limits");

    m_insertOrReplaceNoteLimitsQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NoteLimits "
        "(noteLocalUid, noteResourceCountMax, uploadLimit, "
        "resourceSizeMax, noteSizeMax, uploaded) "
        "VALUES(:noteLocalUid, :noteResourceCountMax, "
        ":uploadLimit, :resourceSizeMax, :noteSizeMax, :uploaded)");

    const bool res = m_insertOrReplaceNoteLimitsQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceNoteLimitsQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareCanAddNoteToNotebookQuery()
    const
{
    if (Q_LIKELY(m_canAddNoteToNotebookQueryPrepared)) {
        return true;
    }

    QNTRACE(
        "local_storage",
        "Preparing SQL query to get the noCreateNotes "
            << "notebook restriction");

    m_canAddNoteToNotebookQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "SELECT noCreateNotes FROM NotebookRestrictions "
        "WHERE localUid = :notebookLocalUid");

    const bool res = m_canAddNoteToNotebookQuery.prepare(queryString);
    if (res) {
        m_canAddNoteToNotebookQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareCanUpdateNoteInNotebookQuery()
    const
{
    if (Q_LIKELY(m_canUpdateNoteInNotebookQueryPrepared)) {
        return true;
    }

    QNTRACE(
        "local_storage",
        "Preparing SQL query to get the noUpdateNotes "
            << "notebook restriction");

    m_canUpdateNoteInNotebookQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "SELECT noUpdateNotes FROM NotebookRestrictions "
        "WHERE localUid = :notebookLocalUid");

    const bool res = m_canUpdateNoteInNotebookQuery.prepare(queryString);
    if (res) {
        m_canUpdateNoteInNotebookQueryPrepared = true;
    }

    return true;
}

bool LocalStorageManagerPrivate::checkAndPrepareCanExpungeNoteInNotebookQuery()
    const
{
    if (Q_LIKELY(m_canExpungeNoteInNotebookQueryPrepared)) {
        return true;
    }

    QNTRACE(
        "local_storage",
        "Preparing SQL query to get the noExpungeNotes "
            << "notebook restriction");

    m_canExpungeNoteInNotebookQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "SELECT noExpungeNotes FROM NotebookRestrictions "
        "WHERE localUid = :notebookLocalUid");

    const bool res = m_canExpungeNoteInNotebookQuery.prepare(queryString);
    if (res) {
        m_canExpungeNoteInNotebookQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceNoteIntoNoteTagsQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNoteIntoNoteTagsQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace note "
            << "into NoteTags table");

    m_insertOrReplaceNoteIntoNoteTagsQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NoteTags"
        "(localNote, note, localTag, tag, tagIndexInNote) "
        "VALUES(:localNote, :note, :localTag, :tag, :tagIndexInNote)");

    const bool res =
        m_insertOrReplaceNoteIntoNoteTagsQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceNoteIntoNoteTagsQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::insertOrReplaceTag(
    const qevercloud::Tag & tag, ErrorString & errorDescription)
{
    // NOTE: this method expects to be called after tag is already checked
    // for sanity of its parameters!

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace tag in the local storage"));

    QString localId = tag.localId();

    bool res = checkAndPrepareInsertOrReplaceTagQuery();
    QSqlQuery & query = m_insertOrReplaceTagQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    const QVariant nullValue;

    QString tagNameNormalized;
    if (tag.name()) {
        tagNameNormalized = tag.name()->toLower();
        m_stringUtils.removeDiacritics(tagNameNormalized);
    }

    query.bindValue(
        QStringLiteral(":localUid"), (localId.isEmpty() ? nullValue : localId));

    query.bindValue(
        QStringLiteral(":guid"), (tag.guid() ? *tag.guid() : nullValue));

    const QString linkedNotebookGuid =
        tag.linkedNotebookGuid().value_or(QString{});

    query.bindValue(
        QStringLiteral(":linkedNotebookGuid"),
        (!linkedNotebookGuid.isEmpty() ? linkedNotebookGuid : nullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (tag.updateSequenceNum() ? *tag.updateSequenceNum() : nullValue));

    query.bindValue(
        QStringLiteral(":name"), (tag.name() ? *tag.name() : nullValue));

    query.bindValue(
        QStringLiteral(":nameLower"),
        (tag.name() ? tagNameNormalized : nullValue));

    query.bindValue(
        QStringLiteral(":parentGuid"),
        (tag.parentGuid() ? *tag.parentGuid() : nullValue));

    query.bindValue(
        QStringLiteral(":parentLocalUid"),
        (!tag.parentTagLocalId().isEmpty()
         ? tag.parentTagLocalId()
         : nullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (tag.isLocallyModified() ? 1 : 0));

    query.bindValue(QStringLiteral(":isLocal"), (tag.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isFavorited"), (tag.isLocallyFavorited() ? 1 : 0));

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::checkAndPrepareTagCountQuery() const
{
    if (Q_LIKELY(m_getTagCountQueryPrepared)) {
        return true;
    }

    m_getTagCountQuery = QSqlQuery(m_sqlDatabase);

    const bool res =
        m_getTagCountQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM Tags"));

    if (res) {
        m_getTagCountQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareInsertOrReplaceTagQuery()
{
    if (Q_LIKELY(m_insertOrReplaceTagQueryPrepared)) {
        return true;
    }

    m_insertOrReplaceTagQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Tags "
        "(localUid, guid, linkedNotebookGuid, updateSequenceNumber, "
        "name, nameLower, parentGuid, parentLocalUid, isDirty, "
        "isLocal, isFavorited) "
        "VALUES(:localUid, :guid, :linkedNotebookGuid, "
        ":updateSequenceNumber, :name, :nameLower, "
        ":parentGuid, :parentLocalUid, :isDirty, :isLocal, :isFavorited)");

    const bool res = m_insertOrReplaceTagQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceTagQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::complementTagParentInfo(
    qevercloud::Tag & tag, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::complementTagParentInfo: " << tag);

    if (tag.parentGuid() && !tag.parentTagLocalId().isEmpty()) {
        QNDEBUG(
            "local_storage",
            "The tag has both parent guid and parent local id, nothing to "
                << "complement");
        return true;
    }

    if (!tag.parentGuid() && tag.parentTagLocalId().isEmpty()) {
        QNDEBUG(
            "local_storage",
            "The tag has neither parent guid nor parent "
                << "local id, nothing to complement");
        return true;
    }

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't complement the parent info for a tag"));

    const QString existingColumn =
        (tag.parentGuid() ? QStringLiteral("guid")
                          : QStringLiteral("localUid"));

    const QString otherColumn =
        (tag.parentGuid() ? QStringLiteral("localUid")
                          : QStringLiteral("guid"));

    const QString uid =
        (tag.parentGuid() ? *tag.parentGuid() : tag.parentTagLocalId());

    const QString queryString =
        QString::fromUtf8("SELECT %1 FROM Tags WHERE %2='%3'")
            .arg(otherColumn, existingColumn, uid);

    QNDEBUG("local_storage", "Query string = " << queryString);

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    res = query.next();
    if (!res) {
        SET_NO_DATA_FOUND();
        return false;
    }

    const QString otherUid = query.record().value(otherColumn).toString();
    QNTRACE(
        "local_storage",
        "Tag's parent " << otherColumn << " was retrieved: " << otherUid);

    if (tag.parentGuid()) {
        tag.setParentTagLocalId(otherUid);
    }
    else {
        tag.setParentGuid(otherUid);
    }

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceResource(
    const qevercloud::Resource & resource, const int indexInNote,
    ErrorString & errorDescription, const bool setResourceBinaryData,
    const bool useSeparateTransaction)
{
    // NOTE: this method expects to be called after resource is already checked
    // for sanity of its parameters!

    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::insertOrReplaceResource: resource = "
            << resource << "\nResource index in note: " << indexInNote
            << "\nSet resource binary data = "
            << (setResourceBinaryData ? "true" : "false")
            << ", use separate transaction = "
            << (useSeparateTransaction ? "true" : "false"));

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace resource in the local storage"));

    std::unique_ptr<Transaction> pTransaction;
    if (useSeparateTransaction) {
        pTransaction.reset(new Transaction(
            m_sqlDatabase, *this, Transaction::Type::Exclusive));
    }

    const QString resourceLocalId = resource.localId();
    const QString noteLocalId = resource.noteLocalId();

    if (!insertOrReplaceResourceMetadata(
            resource, indexInNote, setResourceBinaryData, errorDescription))
    {
        return false;
    }

    if (!updateNoteResources(resource, errorDescription)) {
        return false;
    }

    // Removing resource's local id from ResourceRecognitionData table
    {
        bool res =
            checkAndPrepareDeleteResourceFromResourceRecognitionTypesQuery();
        QSqlQuery & query = m_deleteResourceFromResourceRecognitionTypesQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (resource.recognition() && resource.recognition()->body()) {
        ResourceRecognitionIndices recoIndices;
        const bool res = recoIndices.setData(*resource.recognition()->body());
        if (res && recoIndices.isValid()) {
            QString recognitionData;

            const auto items = recoIndices.items();
            const int numItems = items.size();
            for (int i = 0; i < numItems; ++i) {
                const ResourceRecognitionIndexItem & item = qAsConst(items)[i];

                auto textItems = item.textItems();
                for (const auto & textItem: qAsConst(textItems)) {
                    recognitionData += textItem.m_text + QStringLiteral(" ");
                }
            }

            recognitionData.chop(1); // Remove trailing whitespace
            m_stringUtils.removePunctuation(recognitionData);
            m_stringUtils.removeDiacritics(recognitionData);

            if (!recognitionData.isEmpty()) {
                bool res =
                    checkAndPrepareInsertOrReplaceIntoResourceRecognitionDataQuery();

                QSqlQuery & query =
                    m_insertOrReplaceIntoResourceRecognitionDataQuery;

                DATABASE_CHECK_AND_SET_ERROR()

                query.bindValue(
                    QStringLiteral(":resourceLocalUid"), resourceLocalId);

                query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

                query.bindValue(
                    QStringLiteral(":recognitionData"), recognitionData);

                res = query.exec();
                DATABASE_CHECK_AND_SET_ERROR()
            }
        }
    }

    // Removing resource from ResourceAttributes table
    {
        bool res = checkAndPrepareDeleteResourceFromResourceAttributesQuery();
        QSqlQuery & query = m_deleteResourceFromResourceAttributesQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    // Removing resource from ResourceAttributesApplicationDataKeysOnly table
    {
        bool res =
            checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery();

        QSqlQuery & query =
            m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery;

        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    // Removing resource from ResourceAttributesApplicationDataFullMap table
    {
        bool res =
            checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataFullMapQuery();

        QSqlQuery & query =
            m_deleteResourceFromResourceAttributesApplicationDataFullMapQuery;

        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":resourceLocalUid"), resourceLocalId);

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    if (resource.attributes() &&
        !insertOrReplaceResourceAttributes(
            resourceLocalId, *resource.attributes(), errorDescription))
    {
        return false;
    }

    if (setResourceBinaryData &&
        !writeResourceBinaryDataToFiles(resource, errorDescription))
    {
        return false;
    }

    if (pTransaction && !pTransaction->commit(errorDescription)) {
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::writeResourceBinaryDataToFiles(
    const qevercloud::Resource & resource, ErrorString & errorDescription)
{
    const QString resourceLocalId = resource.localId();

    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::writeResourceBinaryDataToFiles: "
            << "resource local id = " << resourceLocalId);

    ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace resource: failed to write resource "
                   "binary data to files"));

    if (resource.noteLocalId().isEmpty()) {
        errorDescription = errorPrefix;
        errorDescription.appendBase(
            QT_TR_NOOP("the resource has no note local id set"));
        const QString displayName = resourceDisplayName(resource);
        if (!displayName.isEmpty()) {
            errorDescription.details() = displayName + QStringLiteral(", ");
        }
        errorDescription.details() += QStringLiteral("resource local id = ");
        errorDescription.details() += resourceLocalId;
        QNWARNING(
            "local_storage", errorDescription << ", resource: " << resource);
        return false;
    }

    if (Q_UNLIKELY(
            (!resource.data() || !resource.data()->body()) &&
            (!resource.alternateData() || !resource.alternateData()->body())))
    {
        errorDescription = errorPrefix;
        errorDescription.appendBase(
            QT_TR_NOOP("the resource has neither data body nor alternate data "
                       "body set"));
        QString displayName = resourceDisplayName(resource);
        if (!displayName.isEmpty()) {
            errorDescription.details() = displayName + QStringLiteral(", ");
        }
        errorDescription.details() += QStringLiteral("resource local id = ");
        errorDescription.details() += resourceLocalId;
        QNWARNING(
            "local_storage", errorDescription << ", resource: " << resource);
        return false;
    }

    bool shouldReplaceOriginalFile =
        (!resource.data() || !resource.data()->body() ||
         !resource.alternateData() || !resource.alternateData()->body());

    if (resource.data() && resource.data()->body()) {
        ErrorString error;
        if (!writeResourceBinaryDataToFile(
                resourceLocalId, resource.noteLocalId(),
                *resource.data()->body(),
                /* is alternate data body = */ false, shouldReplaceOriginalFile,
                error))
        {
            errorDescription = errorPrefix;
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }

    if (resource.alternateData() && resource.alternateData()->body()) {
        ErrorString error;
        if (!writeResourceBinaryDataToFile(
                resourceLocalId, resource.noteLocalId(),
                *resource.alternateData()->body(),
                /* is alternate data body = */ true, shouldReplaceOriginalFile,
                error))
        {
            errorDescription = errorPrefix;
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }

    if (shouldReplaceOriginalFile) {
        return true;
    }

    // New data files were written for both data body and alternate data
    // body, now need to replace the old ones with the new ones

    const QString storagePath = accountPersistentStoragePath(m_currentAccount);

    /**
     * Tricky algorithm tolerant to crashes anywhere during the process:
     * 1) if alternate data file exists, move it to the file with additional
     *    ".old" suffix
     * 2) rename/move the alternate data file with ".new" suffix to the file
     *    without that suffix
     * 3) rename/move the data file with ".new" suffix to the file without
     *    that suffix (i.e. replace the old file with the new one)
     * 4) remove the alternate data file with additional ".old" suffix if it
     *    exists
     *
     * Each of these actions is atomic. If alternate data file existed and the
     * process crashed after 1 but before 2, 3 and 4 the logic reading
     * the alternate data from file would handle the case of nonexisting file
     * with existing file with additional ".old" suffix. If the process crashed
     * after 2 but before 3 and 4, it is still possible to recover because
     * the situation is unambiguous - the alternate data file with ".old"
     * extension should not exist unless something went wrong. The logic reading
     * the alternate data from file would handle it. Same goes for the case of
     * crash after 3 but before 4 - it is only different by the existence of
     * two data body files - the "usual" one and the one with ".new" suffix.
     */

    const QString alternateDataStoragePath = storagePath +
        QStringLiteral("/Resources/alternateData/") + resource.noteLocalId() +
        QStringLiteral("/") + resourceLocalId + QStringLiteral(".dat");

    QString oldFileName = alternateDataStoragePath;

    const QFileInfo oldAlternateDataFileInfo(oldFileName);
    if (oldAlternateDataFileInfo.exists() && oldAlternateDataFileInfo.isFile())
    {
        const QString oldFileBackupName =
            alternateDataStoragePath + QStringLiteral(".old");

        ErrorString error;
        if (!renameFile(oldFileName, oldFileBackupName, error)) {
            errorDescription.setBase(
                QT_TR_NOOP("failed to atomically backup old resource alternate "
                           "data file"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    QString newFileName = oldFileName + QStringLiteral(".new");

    ErrorString error;
    if (!renameFile(newFileName, oldFileName, error)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to atomically replace old alternate data "
                       "resource file with the new one"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString dataStoragePath = storagePath +
        QStringLiteral("/Resources/data/") + resource.noteLocalId() +
        QStringLiteral("/") + resourceLocalId + QStringLiteral(".dat");

    oldFileName = dataStoragePath;
    newFileName = oldFileName + QStringLiteral(".new");

    if (!renameFile(newFileName, oldFileName, error)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to atomically replace old resource file with "
                       "the new one"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QFileInfo backupAlternateDataFileInfo(
        alternateDataStoragePath + QStringLiteral(".old"));
    if (backupAlternateDataFileInfo.exists() &&
        backupAlternateDataFileInfo.isFile() &&
        !removeFile(backupAlternateDataFileInfo.absoluteFilePath()))
    {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove backup alternate data file"));
        errorDescription.details() +=
            backupAlternateDataFileInfo.absoluteFilePath();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::writeResourceBinaryDataToFile(
    const QString & resourceLocalId, const QString & noteLocalId,
    const QByteArray & dataBody, const bool isAlternateDataBody,
    const bool replaceOriginalFile, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::writeResourceBinaryDataToFile: "
            << "resource local id = " << resourceLocalId
            << ", note local id = " << noteLocalId << ", writing"
            << (isAlternateDataBody ? " alternate" : "")
            << " data body; replace original file = "
            << (replaceOriginalFile ? "true" : "false"));

    QString storagePath = accountPersistentStoragePath(m_currentAccount);
    if (isAlternateDataBody) {
        storagePath += QStringLiteral("/Resources/alternateData/");
    }
    else {
        storagePath += QStringLiteral("/Resources/data/");
    }

    storagePath += noteLocalId;

    const QDir storageDir(storagePath);
    if (!storageDir.exists() && !storageDir.mkpath(storagePath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to create directory for resource data file "
                       "storage"));
        errorDescription.details() = storagePath;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    // NOTE: for crash recovery purposes new data gets written to a new file
    // which will then replace the old file
    QFile resourceDataFile(
        storagePath + QStringLiteral("/") + resourceLocalId +
        QStringLiteral(".dat.new"));

    if (!resourceDataFile.open(QIODevice::WriteOnly)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to open resource data file for writing"));
        errorDescription.details() = resourceDataFile.fileName();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const qint64 dataSize = dataBody.size();
    const qint64 bytesWritten = resourceDataFile.write(dataBody);
    if (bytesWritten < 0) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to write resource data to file"));
        errorDescription.details() = resourceDataFile.fileName();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (bytesWritten < dataSize) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to write the whole resource data to file"));
        errorDescription.details() = resourceDataFile.fileName();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (!resourceDataFile.flush()) {
        errorDescription.setBase(QT_TR_NOOP(
            "failed to flush file after writing resource data to it"));
        errorDescription.details() = resourceDataFile.fileName();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    // NOTE: this seems to be required for the subsequent call
    // to rename to work on Windows
    resourceDataFile.close();

    if (replaceOriginalFile) {
        const QString oldFileName = storagePath + QStringLiteral("/") +
            resourceLocalId + QStringLiteral(".dat");

        ErrorString error;
        if (!renameFile(
                oldFileName + QStringLiteral(".new"), oldFileName, error)) {
            errorDescription.setBase(
                QT_TR_NOOP("failed to atomically replace old resource file "
                           "with the new one"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceResourceAttributes(
    const QString & localId, const qevercloud::ResourceAttributes & attributes,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::insertOrReplaceResourceAttributes: "
            << "local id = " << localId
            << ", resource attributes: " << attributes);

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace resource attributes"));

    const QVariant nullValue;

    // Insert or replace attributes into ResourceAttributes table
    {
        bool res = checkAndPrepareInsertOrReplaceResourceAttributesQuery();
        QSqlQuery & query = m_insertOrReplaceResourceAttributesQuery;
        DATABASE_CHECK_AND_SET_ERROR()

        query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

        query.bindValue(
            QStringLiteral(":resourceSourceURL"),
            (attributes.sourceURL() ? *attributes.sourceURL() : nullValue));

        query.bindValue(
            QStringLiteral(":timestamp"),
            (attributes.timestamp() ? *attributes.timestamp() : nullValue));

        query.bindValue(
            QStringLiteral(":resourceLatitude"),
            (attributes.latitude() ? *attributes.latitude() : nullValue));

        query.bindValue(
            QStringLiteral(":resourceLongitude"),
            (attributes.longitude() ? *attributes.longitude() : nullValue));

        query.bindValue(
            QStringLiteral(":resourceAltitude"),
            (attributes.altitude() ? *attributes.altitude() : nullValue));

        query.bindValue(
            QStringLiteral(":cameraMake"),
            (attributes.cameraMake() ? *attributes.cameraMake() : nullValue));

        query.bindValue(
            QStringLiteral(":cameraModel"),
            (attributes.cameraModel() ? *attributes.cameraModel() : nullValue));

        query.bindValue(
            QStringLiteral(":clientWillIndex"),
            (attributes.clientWillIndex()
                 ? (*attributes.clientWillIndex() ? 1 : 0)
                 : nullValue));

        query.bindValue(
            QStringLiteral(":fileName"),
            (attributes.fileName() ? *attributes.fileName() : nullValue));

        query.bindValue(
            QStringLiteral(":attachment"),
            (attributes.attachment() ? (*attributes.attachment() ? 1 : 0)
                                     : nullValue));

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR()
    }

    // Special treatment for ResourceAttributes.applicationData:
    // keysOnly + fullMap

    if (attributes.applicationData()) {
        if (attributes.applicationData()->keysOnly()) {
            bool res =
                checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataKeysOnlyQuery();

            QSqlQuery & query =
                m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQuery;

            DATABASE_CHECK_AND_SET_ERROR()

            query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

            const auto & keysOnly = *attributes.applicationData()->keysOnly();
            for (const auto & key: keysOnly) {
                query.bindValue(QStringLiteral(":resourceKey"), key);
                res = query.exec();
                DATABASE_CHECK_AND_SET_ERROR()
            }
        }

        if (attributes.applicationData()->fullMap()) {
            bool res =
                checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataFullMapQuery();

            QSqlQuery & query =
                m_insertOrReplaceResourceAttributeApplicationDataFullMapQuery;

            DATABASE_CHECK_AND_SET_ERROR()

            query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

            const auto & fullMap = *attributes.applicationData()->fullMap();
            for (auto it = fullMap.constBegin(), end = fullMap.constEnd();
                 it != end; ++it) {
                query.bindValue(QStringLiteral(":resourceMapKey"), it.key());
                query.bindValue(QStringLiteral(":resourceValue"), it.value());
                res = query.exec();
                DATABASE_CHECK_AND_SET_ERROR()
            }
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::insertOrReplaceResourceMetadata(
    const qevercloud::Resource & resource, const int indexInNote,
    const bool setResourceDataProperties, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::insertOrReplaceResourceMetadata");

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace resource: failed to update common "
                   "resource metadata"));

    const QVariant nullValue;

    bool res =
        (setResourceDataProperties
             ? checkAndPrepareInsertOrReplaceResourceMetadataWithDataPropertiesQuery()
             : checkAndPrepareUpdateResourceMetadataWithoutDataPropertiesQuery());

    QSqlQuery & query =
        (setResourceDataProperties
             ? m_insertOrReplaceResourceMetadataWithDataPropertiesQuery
             : m_updateResourceMetadataWithoutDataPropertiesQuery);
    DATABASE_CHECK_AND_SET_ERROR()

    query.bindValue(
        QStringLiteral(":resourceGuid"),
        (resource.guid() ? *resource.guid() : nullValue));

    query.bindValue(
        QStringLiteral(":noteGuid"),
        (resource.noteGuid() ? *resource.noteGuid() : nullValue));

    query.bindValue(QStringLiteral(":noteLocalUid"), resource.noteLocalId());

    query.bindValue(
        QStringLiteral(":mime"),
        (resource.mime() ? *resource.mime() : nullValue));

    query.bindValue(
        QStringLiteral(":width"),
        (resource.width() ? *resource.width() : nullValue));

    query.bindValue(
        QStringLiteral(":height"),
        (resource.height() ? *resource.height() : nullValue));

    query.bindValue(
        QStringLiteral(":recognitionDataBody"),
        ((resource.recognition() && resource.recognition()->body())
             ? *resource.recognition()->body()
             : nullValue));

    query.bindValue(
        QStringLiteral(":recognitionDataSize"),
        ((resource.recognition() && resource.recognition()->size())
             ? *resource.recognition()->size()
             : nullValue));

    query.bindValue(
        QStringLiteral(":recognitionDataHash"),
        ((resource.recognition() && resource.recognition()->bodyHash())
             ? *resource.recognition()->bodyHash()
             : nullValue));

    query.bindValue(
        QStringLiteral(":resourceUpdateSequenceNumber"),
        (resource.updateSequenceNum() ? *resource.updateSequenceNum()
                                      : nullValue));

    query.bindValue(
        QStringLiteral(":resourceIsDirty"),
        (resource.isLocallyModified() ? 1 : 0));

    query.bindValue(QStringLiteral(":resourceIndexInNote"), indexInNote);
    query.bindValue(QStringLiteral(":resourceLocalUid"), resource.localId());

    if (setResourceDataProperties) {
        query.bindValue(
            QStringLiteral(":dataSize"),
            ((resource.data() && resource.data()->size())
                 ? *resource.data()->size()
                 : nullValue));

        query.bindValue(
            QStringLiteral(":dataHash"),
            ((resource.data() && resource.data()->bodyHash())
                 ? *resource.data()->bodyHash()
                 : nullValue));

        query.bindValue(
            QStringLiteral(":alternateDataSize"),
            ((resource.alternateData() && resource.alternateData()->size())
                 ? *resource.alternateData()->size()
                 : nullValue));

        query.bindValue(
            QStringLiteral(":alternateDataHash"),
            ((resource.alternateData() && resource.alternateData()->bodyHash())
                 ? *resource.alternateData()->bodyHash()
                 : nullValue));
    }

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

bool LocalStorageManagerPrivate::updateNoteResources(
    const qevercloud::Resource & resource, ErrorString & errorDescription)
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::updateNoteResources");

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace resource: failed to update "
                   "note-resource interconnections"));

    const QVariant nullValue;

    bool res = checkAndPrepareInsertOrReplaceNoteResourceQuery();
    QSqlQuery & query = m_insertOrReplaceNoteResourceQuery;
    DATABASE_CHECK_AND_SET_ERROR()

    query.bindValue(QStringLiteral(":localNote"), resource.noteLocalId());

    query.bindValue(
        QStringLiteral(":note"),
        (resource.noteGuid() ? *resource.noteGuid() : nullValue));

    query.bindValue(QStringLiteral(":localResource"), resource.localId());

    query.bindValue(
        QStringLiteral(":resource"),
        (resource.guid() ? *resource.guid() : nullValue));

    res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    return true;
}

void LocalStorageManagerPrivate::setNoteIdsToNoteResources(
    qevercloud::Note & note) const
{
    if (!note.resources()) {
        return;
    }

    auto resources = *note.resources();
    for (auto & resource: resources) {
        resource.setNoteLocalId(note.localId());
        if (note.guid()) {
            resource.setNoteGuid(*note.guid());
        }
    }
    note.setResources(resources);
}

bool LocalStorageManagerPrivate::removeResourceDataFiles(
    const qevercloud::Resource & resource, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::removeResourceDataFiles: "
            << "resource local id = " << resource.localId()
            << ", note local id = " << resource.noteLocalId());

    if (Q_UNLIKELY(resource.noteLocalId().isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("the resource has no note local id set"));

        const QString displayName = resourceDisplayName(resource);
        if (!displayName.isEmpty()) {
            errorDescription.details() = displayName + QStringLiteral(", ");
        }

        errorDescription.details() += QStringLiteral("resource local id = ");
        errorDescription.details() += resource.localId();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const QString noteLocalId = resource.noteLocalId();
    const QString storagePath = accountPersistentStoragePath(m_currentAccount);

    QFile resourceDataFile(
        storagePath + QStringLiteral("/Resources/data/") + noteLocalId +
        QStringLiteral("/") + resource.localId() + QStringLiteral(".dat"));

    if (resourceDataFile.exists() && !resourceDataFile.remove()) {
        // Double-check, QFile::remove is known to not return
        // proper return code sometimes
        const QFileInfo resourceDataFileInfo(resourceDataFile.fileName());
        if (resourceDataFileInfo.exists()) {
            errorDescription.setBase(
                QT_TR_NOOP("failed to delete resource data file"));

            const QString displayName = resourceDisplayName(resource);
            if (!displayName.isEmpty()) {
                errorDescription.details() = displayName + QStringLiteral(", ");
            }

            errorDescription.details() +=
                QStringLiteral("resource local id = ");

            errorDescription.details() += resource.localId();
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    QFile resourceAlternateDataFile(
        storagePath + QStringLiteral("/Resources/alternateData/") +
        noteLocalId + QStringLiteral("/") + resource.localId() +
        QStringLiteral(".dat"));

    if (resourceAlternateDataFile.exists() &&
        !resourceAlternateDataFile.remove()) {
        // Double-check, QFile::remove is known to not return
        // proper return code sometimes
        const QFileInfo resourceAlternateDataFileInfo(
            resourceAlternateDataFile.fileName());

        if (resourceAlternateDataFileInfo.exists()) {
            errorDescription.setBase(
                QT_TR_NOOP("failed to delete resource alternate data file"));

            const QString displayName = resourceDisplayName(resource);
            if (!displayName.isEmpty()) {
                errorDescription.details() = displayName + QStringLiteral(", ");
            }

            errorDescription.details() +=
                QStringLiteral("resource local id = ");

            errorDescription.details() += resource.localId();
            QNWARNING("local_storage", errorDescription);
            return false;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::removeResourceDataFilesForNote(
    const QString & noteLocalId, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::removeResourceDataFilesForNote: "
            << "note local id = " << noteLocalId);

    const QString accountPath = accountPersistentStoragePath(m_currentAccount);

    const QString dataPath =
        accountPath + QStringLiteral("/Resources/data/") + noteLocalId;

    if (!removeDir(dataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource data bodies"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    QString alternateDataPath =
        accountPath + QStringLiteral("/Resources/alternateData/") + noteLocalId;

    if (!removeDir(alternateDataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource alternate data bodies"));
        errorDescription.details() = alternateDataPath;
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    return true;
}

bool LocalStorageManagerPrivate::removeResourceDataFilesForNotebook(
    const qevercloud::Notebook & notebook, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::removeResourceDataFilesForNotebook: "
            << "notebook = " << notebook);

    const ErrorString errorPrefix(
        QT_TR_NOOP("failed to remove resource data files for notebook: cannot "
                   "list note local ids per notebook"));

    QString column, id;
    if (notebook.guid()) {
        column = QStringLiteral("notebookGuid");
        id = *notebook.guid();
    }
    else {
        column = QStringLiteral("notebookLocalUid");
        id = notebook.localId();
    }

    id = sqlEscapeString(id);

    const QString queryString =
        QString::fromUtf8("SELECT localUid FROM Notes WHERE %1 = '%2'")
            .arg(column, id);

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    QStringList noteLocalIds;
    noteLocalIds.reserve(std::max(query.size(), 0));

    ErrorString error;
    while (query.next()) {
        const QString noteLocalId = query.value(0).toString();
        error.clear();
        if (!removeResourceDataFilesForNote(noteLocalId, error)) {
            errorDescription.setBase(QT_TR_NOOP(
                "failed to remove resource data files for notebook"));

            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::removeResourceDataFilesForLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::removeResourceDataFilesForLinkedNotebook: "
            << "linked notebook = " << linkedNotebook);

    const ErrorString errorPrefix(
        QT_TR_NOOP("failed to remove resource data files for linked notebook: "
                   "cannot list note local ids per linked notebook"));

    if (Q_UNLIKELY(!linkedNotebook.guid())) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove resource data files for linked "
                       "notebook: linked notebook has no guid set"));
        QNWARNING(
            "local_storage",
            errorDescription << ", linked notebook: " << linkedNotebook);
        return false;
    }

    const QString queryString =
        QString::fromUtf8(
            "SELECT localUid FROM Notes WHERE notebookLocalUid IN "
            "(SELECT localUid FROM Notebooks WHERE "
            "linkedNotebookGuid = '%1')")
            .arg(sqlEscapeString(*linkedNotebook.guid()));

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    QStringList noteLocalIds;
    noteLocalIds.reserve(std::max(query.size(), 0));

    ErrorString error;
    while (query.next()) {
        const QString noteLocalId = query.value(0).toString();
        error.clear();
        if (!removeResourceDataFilesForNote(noteLocalId, error)) {
            errorDescription.setBase(QT_TR_NOOP(
                "failed to remove resource data files for notebook"));

            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceResourceMetadataWithDataPropertiesQuery()
{
    if (Q_LIKELY(
            m_insertOrReplaceResourceMetadataWithDataPropertiesQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "the resource with binary data");

    m_insertOrReplaceResourceMetadataWithDataPropertiesQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Resources (resourceGuid, "
        "noteGuid, noteLocalUid, dataSize, dataHash, mime, "
        "width, height, recognitionDataBody, recognitionDataSize, "
        "recognitionDataHash, alternateDataSize, "
        "alternateDataHash, resourceUpdateSequenceNumber, "
        "resourceIsDirty, resourceIndexInNote, resourceLocalUid) "
        "VALUES(:resourceGuid, :noteGuid, :noteLocalUid, "
        ":dataSize, :dataHash, :mime, :width, :height, "
        ":recognitionDataBody, :recognitionDataSize, "
        ":recognitionDataHash, :alternateDataSize, "
        ":alternateDataHash, :resourceUpdateSequenceNumber, "
        ":resourceIsDirty, :resourceIndexInNote, :resourceLocalUid)");

    const bool res =
        m_insertOrReplaceResourceMetadataWithDataPropertiesQuery.prepare(
            queryString);

    if (res) {
        m_insertOrReplaceResourceMetadataWithDataPropertiesQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareUpdateResourceMetadataWithoutDataPropertiesQuery()
{
    if (Q_LIKELY(m_updateResourceMetadataWithoutDataPropertiesQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to update the resource "
            << "without binary data");

    m_updateResourceMetadataWithoutDataPropertiesQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "UPDATE Resources SET resourceGuid = :resourceGuid, "
        "noteGuid = :noteGuid, noteLocalUid = :noteLocalUid, "
        "mime = :mime, width = :width, height = :height, "
        "recognitionDataBody = :recognitionDataBody, "
        "recognitionDataSize = :recognitionDataSize, "
        "recognitionDataHash = :recognitionDataHash, "
        "resourceUpdateSequenceNumber = :resourceUpdateSequenceNumber, "
        "resourceIsDirty = :resourceIsDirty, "
        "resourceIndexInNote = :resourceIndexInNote "
        "WHERE resourceLocalUid = :resourceLocalUid");

    const bool res =
        m_updateResourceMetadataWithoutDataPropertiesQuery.prepare(queryString);

    if (res) {
        m_updateResourceMetadataWithoutDataPropertiesQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceNoteResourceQuery()
{
    if (Q_LIKELY(m_insertOrReplaceNoteResourceQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "resource into NoteResources table");

    m_insertOrReplaceNoteResourceQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NoteResources "
        "(localNote, note, localResource, resource) "
        "VALUES(:localNote, :note, :localResource, :resource)");

    const bool res = m_insertOrReplaceNoteResourceQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceNoteResourceQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareDeleteResourceFromResourceRecognitionTypesQuery()
{
    if (Q_LIKELY(m_deleteResourceFromResourceRecognitionTypesQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to delete resource from ResourceRecognitionData "
            << "table");

    m_deleteResourceFromResourceRecognitionTypesQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "DELETE FROM ResourceRecognitionData "
        "WHERE resourceLocalUid = :resourceLocalUid");

    const bool res =
        m_deleteResourceFromResourceRecognitionTypesQuery.prepare(queryString);

    if (res) {
        m_deleteResourceFromResourceRecognitionTypesQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceIntoResourceRecognitionDataQuery()
{
    if (Q_LIKELY(m_insertOrReplaceIntoResourceRecognitionDataQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "resource into ResourceRecognitionData table");

    m_insertOrReplaceIntoResourceRecognitionDataQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceRecognitionData"
        "(resourceLocalUid, noteLocalUid, recognitionData) "
        "VALUES(:resourceLocalUid, :noteLocalUid, :recognitionData)");

    const bool res =
        m_insertOrReplaceIntoResourceRecognitionDataQuery.prepare(queryString);

    if (res) {
        m_insertOrReplaceIntoResourceRecognitionDataQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareDeleteResourceFromResourceAttributesQuery()
{
    if (Q_LIKELY(m_deleteResourceFromResourceAttributesQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to delete resource from "
            << "ResourceAttributes table");

    m_deleteResourceFromResourceAttributesQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "DELETE FROM ResourceAttributes WHERE "
        "resourceLocalUid = :resourceLocalUid");

    const bool res =
        m_deleteResourceFromResourceAttributesQuery.prepare(queryString);

    if (res) {
        m_deleteResourceFromResourceAttributesQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery()
{
    if (Q_LIKELY(
            m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQueryPrepared))
    {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to delete Resource from "
            << "ResourceAttributesApplicationDataKeysOnly table");

    m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "DELETE FROM ResourceAttributesApplicationDataKeysOnly "
        "WHERE resourceLocalUid = :resourceLocalUid");

    const bool res =
        m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery
            .prepare(queryString);

    if (res) {
        m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQueryPrepared =
            true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareDeleteResourceFromResourceAttributesApplicationDataFullMapQuery()
{
    if (Q_LIKELY(
            m_deleteResourceFromResourceAttributesApplicationDataFullMapQueryPrepared))
    {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to delete Resource from "
            << "ResourceAttributesApplicationDataFullMap table");

    m_deleteResourceFromResourceAttributesApplicationDataFullMapQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "DELETE FROM ResourceAttributesApplicationDataFullMap "
        "WHERE resourceLocalUid = :resourceLocalUid");

    const bool res =
        m_deleteResourceFromResourceAttributesApplicationDataFullMapQuery
            .prepare(queryString);

    if (res) {
        m_deleteResourceFromResourceAttributesApplicationDataFullMapQueryPrepared =
            true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceResourceAttributesQuery()
{
    if (Q_LIKELY(m_insertOrReplaceResourceAttributesQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "ResourceAttributes");

    m_insertOrReplaceResourceAttributesQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAttributes"
        "(resourceLocalUid, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, "
        "fileName, attachment) VALUES(:resourceLocalUid, "
        ":resourceSourceURL, :timestamp, :resourceLatitude, "
        ":resourceLongitude, :resourceAltitude, :cameraMake, "
        ":cameraModel, :clientWillIndex, :fileName, :attachment)");

    const bool res =
        m_insertOrReplaceResourceAttributesQuery.prepare(queryString);

    if (res) {
        m_insertOrReplaceResourceAttributesQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataKeysOnlyQuery()
{
    if (Q_LIKELY(
            m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQueryPrepared))
    {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "resource attribute application data (keys only)");

    m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAttributesApplicationDataKeysOnly"
        "(resourceLocalUid, resourceKey) VALUES(:resourceLocalUid, "
        ":resourceKey)");

    const bool res =
        m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQuery.prepare(
            queryString);

    if (res) {
        m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQueryPrepared =
            true;
    }

    return res;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceResourceAttributesApplicationDataFullMapQuery()
{
    if (Q_LIKELY(
            m_insertOrReplaceResourceAttributeApplicationDataFullMapQueryPrepared))
    {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "resource attributes application data (full map)");

    m_insertOrReplaceResourceAttributeApplicationDataFullMapQuery =
        QSqlQuery(m_sqlDatabase);

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAttributesApplicationDataFullMap"
        "(resourceLocalUid, resourceMapKey, resourceValue) "
        "VALUES(:resourceLocalUid, :resourceMapKey, :resourceValue)");

    const bool res =
        m_insertOrReplaceResourceAttributeApplicationDataFullMapQuery.prepare(
            queryString);

    if (res) {
        m_insertOrReplaceResourceAttributeApplicationDataFullMapQueryPrepared =
            true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareResourceCountQuery() const
{
    if (Q_LIKELY(m_getResourceCountQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to get the count of "
            << "Resources");

    m_getResourceCountQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString =
        QStringLiteral("SELECT COUNT(*) FROM Resources");

    const bool res = m_getResourceCountQuery.prepare(queryString);
    if (res) {
        m_getResourceCountQueryPrepared = res;
    }

    return res;
}

bool LocalStorageManagerPrivate::insertOrReplaceSavedSearch(
    const qevercloud::SavedSearch & search, ErrorString & errorDescription)
{
    // NOTE: this method expects to be called after the search is already
    // checked for sanity of its parameters!

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't insert or replace saved search into the local "
                   "storage database"));

    if (!checkAndPrepareInsertOrReplaceSavedSearchQuery()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("failed to prepare the SQL query"));

        QNWARNING(
            "local_storage",
            errorDescription << m_insertOrReplaceSavedSearchQuery.lastError());

        errorDescription.details() =
            m_insertOrReplaceSavedSearchQuery.lastError().text();
        return false;
    }

    QSqlQuery & query = m_insertOrReplaceSavedSearchQuery;
    const QVariant nullValue;

    query.bindValue(QStringLiteral(":localUid"), search.localId());

    query.bindValue(
        QStringLiteral(":guid"), (search.guid() ? *search.guid() : nullValue));

    query.bindValue(
        QStringLiteral(":name"), (search.name() ? *search.name() : nullValue));

    query.bindValue(
        QStringLiteral(":nameLower"),
        (search.name() ? search.name()->toLower() : nullValue));

    query.bindValue(
        QStringLiteral(":query"),
        (search.query() ? *search.query() : nullValue));

    query.bindValue(
        QStringLiteral(":format"),
        (search.format() ? static_cast<int>(*search.format()) : nullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (search.updateSequenceNum() ? *search.updateSequenceNum() : nullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (search.isLocallyModified() ? 1 : 0));

    query.bindValue(QStringLiteral(":isLocal"), (search.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":includeAccount"),
        (search.scope() ? (search.scope()->includeAccount()
                               ? (*search.scope()->includeAccount() ? 1 : 0)
                               : nullValue)
                        : nullValue));

    query.bindValue(
        QStringLiteral(":includePersonalLinkedNotebooks"),
        (search.scope()
             ? (search.scope()->includePersonalLinkedNotebooks()
                    ? (*search.scope()->includePersonalLinkedNotebooks() ? 1
                                                                         : 0)
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":includeBusinessLinkedNotebooks"),
        (search.scope()
             ? (search.scope()->includeBusinessLinkedNotebooks()
                    ? (*search.scope()->includeBusinessLinkedNotebooks() ? 1
                                                                         : 0)
                    : nullValue)
             : nullValue));

    query.bindValue(
        QStringLiteral(":isFavorited"), (search.isLocallyFavorited() ? 1 : 0));

    const bool res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()
    return true;
}

bool LocalStorageManagerPrivate::
    checkAndPrepareInsertOrReplaceSavedSearchQuery()
{
    if (Q_LIKELY(m_insertOrReplaceSavedSearchQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to insert or replace "
            << "SavedSearch");

    const QString columns = QStringLiteral(
        "localUid, guid, name, nameLower, query, format, "
        "updateSequenceNumber, isDirty, isLocal, includeAccount, "
        "includePersonalLinkedNotebooks, "
        "includeBusinessLinkedNotebooks, isFavorited");

    const QString valuesNames = QStringLiteral(
        ":localUid, :guid, :name, :nameLower, :query, :format, "
        ":updateSequenceNumber, :isDirty, :isLocal, "
        ":includeAccount, :includePersonalLinkedNotebooks, "
        ":includeBusinessLinkedNotebooks, :isFavorited");

    const QString queryString =
        QString::fromUtf8(
            "INSERT OR REPLACE INTO SavedSearches (%1) VALUES(%2)")
            .arg(columns, valuesNames);

    m_insertOrReplaceSavedSearchQuery = QSqlQuery(m_sqlDatabase);
    const bool res = m_insertOrReplaceSavedSearchQuery.prepare(queryString);
    if (res) {
        m_insertOrReplaceSavedSearchQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::checkAndPrepareGetSavedSearchCountQuery() const
{
    if (Q_LIKELY(m_getSavedSearchCountQueryPrepared)) {
        return true;
    }

    QNDEBUG(
        "local_storage",
        "Preparing SQL query to get the count of "
            << "SavedSearches");

    m_getSavedSearchCountQuery = QSqlQuery(m_sqlDatabase);

    const QString queryString =
        QStringLiteral("SELECT COUNT(*) FROM SavedSearches");

    const bool res = m_getSavedSearchCountQuery.prepare(queryString);
    if (res) {
        m_getSavedSearchCountQueryPrepared = true;
    }

    return res;
}

bool LocalStorageManagerPrivate::complementTagsWithNoteLocalIds(
    QList<std::pair<qevercloud::Tag, QStringList>> & tagsWithNoteLocalIds,
    ErrorString & errorDescription) const
{
    if (tagsWithNoteLocalIds.isEmpty()) {
        return true;
    }

    const ErrorString errorPrefix(
        QT_TR_NOOP("Can't list tags along with their corresponding note local "
                   "ids"));

    QString queryString = QStringLiteral(
        "SELECT localTag, localNote FROM NoteTags WHERE localTag IN ('");

    for (const auto & pair: tagsWithNoteLocalIds) {
        queryString += pair.first.localId();
        queryString += QStringLiteral("', '");
    }
    queryString.chop(3); // remove trailing comma, whitespace and quotation mark

    queryString += QStringLiteral(")");

    QSqlQuery query(m_sqlDatabase);
    const bool res = query.exec(queryString);
    DATABASE_CHECK_AND_SET_ERROR()

    QMap<QString, QSet<QString>> noteLocalIdsByTagLocalId;
    while (query.next()) {
        QSqlRecord rec = query.record();

        const int localTagIndex = rec.indexOf(QStringLiteral("localTag"));
        if (Q_UNLIKELY(localTagIndex < 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("failed to list tag's note local ids - no tag "
                           "column within the result of SQL query"));
            return false;
        }

        const QString tagLocalId = rec.value(localTagIndex).toString();
        if (Q_UNLIKELY(tagLocalId.isEmpty())) {
            errorDescription.setBase(
                QT_TR_NOOP("failed to list tag's note local ids - tag local "
                           "id is empty within the result of SQL query"));
            return false;
        }

        const int localNoteIndex = rec.indexOf(QStringLiteral("localNote"));
        if (localNoteIndex >= 0) {
            QString noteLocalId = rec.value(localNoteIndex).toString();
            if (!noteLocalId.isEmpty()) {
                Q_UNUSED(
                    noteLocalIdsByTagLocalId[tagLocalId].insert(noteLocalId))
            }
        }
    }

    for (auto & pair: tagsWithNoteLocalIds) {
        const QString tagLocalId = pair.first.localId();
        auto nit = noteLocalIdsByTagLocalId.find(tagLocalId);
        if (nit == noteLocalIdsByTagLocalId.end()) {
            continue;
        }

        const auto & noteLocalIds = nit.value();
        auto & targetNoteLocalIds = pair.second;
        targetNoteLocalIds.reserve(noteLocalIds.size());
        for (const auto & noteLocalId: noteLocalIds) {
            targetNoteLocalIds << noteLocalId;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::readResourceDataFromFiles(
    qevercloud::Resource & resource, ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::readResourceDataFromFiles: "
            << "resource local id = " << resource.localId()
            << ", note local id = " << resource.noteLocalId());

    if (Q_UNLIKELY(resource.noteLocalId().isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("the resource has no note local id set"));

        QString displayName = resourceDisplayName(resource);
        if (!displayName.isEmpty()) {
            errorDescription.details() = displayName + QStringLiteral(", ");
        }

        errorDescription.details() += QStringLiteral("resource local id = ");
        errorDescription.details() += resource.localId();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (resource.data()) {
        QByteArray dataBody;
        ErrorString error;
        auto status = readResourceBinaryDataFromFile(
            resource.localId(), resource.noteLocalId(),
            /* is alternate data body = */ false, dataBody, error);

        if (status != ReadResourceBinaryDataFromFileStatus::Success) {
            if (status == ReadResourceBinaryDataFromFileStatus::FileNotFound) {
                errorDescription.setBase(
                    QT_TR_NOOP("file with resource data body was not found"));
            }
            else {
                errorDescription = error;
            }

            const QString displayName = resourceDisplayName(resource);
            if (!displayName.isEmpty()) {
                errorDescription.details() = displayName + QStringLiteral(", ");
            }

            errorDescription.details() +=
                QStringLiteral("resource local id = ");

            errorDescription.details() += resource.localId();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        resource.mutableData()->setBody(dataBody);
    }

    if (resource.alternateData()) {
        QByteArray alternateDataBody;
        ErrorString error;

        auto status = readResourceBinaryDataFromFile(
            resource.localId(), resource.noteLocalId(),
            /* is alternate data body = */ true, alternateDataBody, error);

        if (status != ReadResourceBinaryDataFromFileStatus::Success) {
            if (status == ReadResourceBinaryDataFromFileStatus::FileNotFound) {
                errorDescription.setBase(QT_TR_NOOP(
                    "file with resource alternate data was not found"));
            }
            else {
                errorDescription = error;
            }

            const QString displayName = resourceDisplayName(resource);
            if (!displayName.isEmpty()) {
                errorDescription.details() = displayName + QStringLiteral(", ");
            }

            errorDescription.details() +=
                QStringLiteral("resource local id = ");

            errorDescription.details() += resource.localId();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        resource.mutableAlternateData()->setBody(alternateDataBody);
    }

    return true;
}

LocalStorageManagerPrivate::ReadResourceBinaryDataFromFileStatus
LocalStorageManagerPrivate::readResourceBinaryDataFromFile(
    const QString & resourceLocalUid, const QString & noteLocalUid,
    const bool isAlternateDataBody, QByteArray & dataBody,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::readResourceBinaryDataFromFile: "
            << "resource local id = " << resourceLocalUid
            << ", note local id = " << noteLocalUid << ", reading "
            << (isAlternateDataBody ? "alternate" : "") << " data body");

    QString storagePath = accountPersistentStoragePath(m_currentAccount);
    if (isAlternateDataBody) {
        storagePath += QStringLiteral("/Resources/alternateData/");
    }
    else {
        storagePath += QStringLiteral("/Resources/data/");
    }

    storagePath += noteLocalUid + QStringLiteral("/") + resourceLocalUid +
        QStringLiteral(".dat");

    QFile resourceDataFile(storagePath);

    /**
     * Below there's a specialized logic of crash recovery from unsuccessful
     * attempt to update resource data body and alternate data body
     * simultaneously, see writeResourceBinaryDataToFiles method implementation
     * for more details
     */

    if (!resourceDataFile.exists()) {
        QNDEBUG(
            "local_storage",
            "Resource data file doesn't exist: "
                << QDir::toNativeSeparators(storagePath));

        if (isAlternateDataBody) {
            const QFileInfo prevAlternateDataFileInfo(
                storagePath + QStringLiteral(".old"));

            if (prevAlternateDataFileInfo.exists() &&
                prevAlternateDataFileInfo.isFile()) {
                resourceDataFile.setFileName(
                    prevAlternateDataFileInfo.absoluteFilePath());

                const QString prevAlternateDataFilePath =
                    QDir::toNativeSeparators(
                        prevAlternateDataFileInfo.absoluteFilePath());

                const QString newAlternateDataFilePath =
                    QDir::toNativeSeparators(storagePath);

                const int res = rename(
                    prevAlternateDataFilePath.toLocal8Bit().constData(),
                    newAlternateDataFilePath.toLocal8Bit().constData());

                if (res != 0) {
                    QNWARNING(
                        "local_storage",
                        "Failed to recover the previous alternate data file: "
                            << prevAlternateDataFilePath << ": "
                            << strerror(errno));
                    return ReadResourceBinaryDataFromFileStatus::FileNotFound;
                }

                QNINFO(
                    "local_storage",
                    "Recovered alternate resource data "
                        << "from file with \".old\" suffix: "
                        << prevAlternateDataFilePath);
                resourceDataFile.setFileName(storagePath);
            }
        }

        if (!resourceDataFile.exists()) {
            return ReadResourceBinaryDataFromFileStatus::FileNotFound;
        }
    }
    else if (isAlternateDataBody) {
        const QFileInfo prevAlternateDataFileInfo(
            storagePath + QStringLiteral(".old"));

        if (prevAlternateDataFileInfo.exists() &&
            prevAlternateDataFileInfo.isFile()) {
            QString resourceDataStoragePath =
                accountPersistentStoragePath(m_currentAccount);

            resourceDataStoragePath += QStringLiteral("/Resources/data/");
            resourceDataStoragePath += noteLocalUid;
            resourceDataStoragePath += QStringLiteral("/");
            resourceDataStoragePath += resourceLocalUid;
            resourceDataStoragePath += QStringLiteral(".dat");

            const QFileInfo newResourceDataFileInfo(
                resourceDataStoragePath + QStringLiteral(".new"));

            if (newResourceDataFileInfo.exists() &&
                newResourceDataFileInfo.isFile()) {
                QNDEBUG(
                    "local_storage",
                    "Old resource alternate data file "
                        << "exists + resource data file with .new suffix "
                           "exists "
                        << "=> need to use old resource alternate data file");

                resourceDataFile.setFileName(
                    prevAlternateDataFileInfo.absoluteFilePath());

                const QString prevAlternateDataFilePath =
                    QDir::toNativeSeparators(
                        prevAlternateDataFileInfo.absoluteFilePath());

                const QString newAlternateDataFilePath =
                    QDir::toNativeSeparators(storagePath);

                const int res = rename(
                    prevAlternateDataFilePath.toLocal8Bit().constData(),
                    newAlternateDataFilePath.toLocal8Bit().constData());

                if (res != 0) {
                    QNWARNING(
                        "local_storage",
                        "Failed to recover the previous "
                            << "alternate data file: "
                            << prevAlternateDataFilePath << ": "
                            << strerror(errno));
                    return ReadResourceBinaryDataFromFileStatus::FileNotFound;
                }

                Q_UNUSED(removeFile(newResourceDataFileInfo.absoluteFilePath()))

                resourceDataFile.setFileName(storagePath);
            }
            else {
                QNINFO(
                    "local_storage",
                    "Removing stale leftover resource alternate data file: "
                        << prevAlternateDataFileInfo.absoluteFilePath());

                Q_UNUSED(
                    removeFile(prevAlternateDataFileInfo.absoluteFilePath()));
            }
        }
    }
    else {
        const QFileInfo newDataFileInfo(storagePath + QStringLiteral(".new"));
        if (newDataFileInfo.exists() && newDataFileInfo.isFile()) {
            QNINFO(
                "local_storage",
                "Removing stale leftover resource data file: "
                    << newDataFileInfo.absoluteFilePath());
            Q_UNUSED(removeFile(newDataFileInfo.absoluteFilePath()))
        }
    }

    if (!resourceDataFile.open(QIODevice::ReadOnly)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to open resource data file for reading"));
        errorDescription.details() += QDir::toNativeSeparators(storagePath);
        QNWARNING("local_storage", errorDescription);
        return ReadResourceBinaryDataFromFileStatus::Failure;
    }

    dataBody = resourceDataFile.readAll();
    return ReadResourceBinaryDataFromFileStatus::Success;
}

bool LocalStorageManagerPrivate::fillResourceFromSqlRecord(
    const QSqlRecord & rec, qevercloud::Resource & resource,
    int & indexInNote, ErrorString & errorDescription) const
{
#define CHECK_AND_SET_RESOURCE_PROPERTY(property, type, localType, setter)     \
    {                                                                          \
        int index = rec.indexOf(QStringLiteral(#property));                    \
        if (index >= 0) {                                                      \
            QVariant value = rec.value(index);                                 \
            if (!value.isNull()) {                                             \
                resource.setter(                                               \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_RESOURCE_PROPERTY(
        resourceLocalUid, QString, QString, setLocalId);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        resourceIsDirty, int, bool, setLocallyModified);
    CHECK_AND_SET_RESOURCE_PROPERTY(noteGuid, QString, QString, setNoteGuid);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        localNote, QString, QString, setNoteLocalId);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        resourceUpdateSequenceNumber, int, qint32, setUpdateSequenceNum);

    CHECK_AND_SET_RESOURCE_PROPERTY(mime, QString, QString, setMime);

    CHECK_AND_SET_RESOURCE_PROPERTY(resourceGuid, QString, QString, setGuid);
    CHECK_AND_SET_RESOURCE_PROPERTY(width, int, qint16, setWidth);
    CHECK_AND_SET_RESOURCE_PROPERTY(height, int, qint16, setHeight);

#undef CHECK_AND_SET_RESOURCE_PROPERTY

#define CHECK_AND_SET_RESOURCE_PROPERTY(                                       \
    property, type, dataField, mutableDataField, dataFieldSetter, localType,   \
    setter)                                                                    \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                if (!resource.dataField()) {                                   \
                    resource.dataFieldSetter(qevercloud::Data{});              \
                }                                                              \
                resource.mutableDataField()->setter(                           \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_RESOURCE_PROPERTY(
        dataSize, int, data, mutableData, setData, qint32, setSize);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        dataHash, QByteArray, data, mutableData, setData, QByteArray,
        setBodyHash);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        recognitionDataSize, int, recognition, mutableRecognition,
        setRecognition, qint32, setSize);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        recognitionDataHash, QByteArray, recognition, mutableRecognition,
        setRecognition, QByteArray, setBodyHash);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        alternateDataSize, int, alternateData, mutableAlternateData,
        setAlternateData, qint32, setSize);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        alternateDataHash, QByteArray, alternateData, mutableAlternateData,
        setAlternateData, QByteArray, setBodyHash);

    CHECK_AND_SET_RESOURCE_PROPERTY(
        recognitionDataBody, QByteArray, recognition, mutableRecognition,
        setRecognition, QByteArray, setBody);

#undef CHECK_AND_SET_RESOURCE_PROPERTY

    const int resourceIndexInNoteIndex =
        rec.indexOf(QStringLiteral("resourceIndexInNote"));

    if (resourceIndexInNoteIndex >= 0) {
        const QVariant value = rec.value(resourceIndexInNoteIndex);
        if (!value.isNull()) {
            bool conversionResult = false;
            const int index = value.toInt(&conversionResult);
            if (!conversionResult) {
                errorDescription.setBase(
                    QT_TR_NOOP("can't convert resource's index in "
                               "note to int"));
                QNERROR("local_storage", errorDescription);
                return false;
            }
            indexInNote = index;
        }
    }

    qevercloud::ResourceAttributes localAttributes;
    auto & attributes =
        (resource.attributes() ? *resource.mutableAttributes()
                               : localAttributes);

    bool hasAttributes = fillResourceAttributesFromSqlRecord(rec, attributes);
    hasAttributes |= fillResourceAttributesApplicationDataKeysOnlyFromSqlRecord(
        rec, attributes);

    hasAttributes |= fillResourceAttributesApplicationDataFullMapFromSqlRecord(
        rec, attributes);

    if (hasAttributes && !resource.attributes()) {
        resource.setAttributes(attributes);
    }

    return true;
}

bool LocalStorageManagerPrivate::fillResourceAttributesFromSqlRecord(
    const QSqlRecord & rec, qevercloud::ResourceAttributes & attributes) const
{
    bool hasSomething = false;

#define CHECK_AND_SET_RESOURCE_ATTRIBUTE(name, setter, type, localType)        \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#name));                  \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                attributes.setter(                                             \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                hasSomething = true;                                           \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        resourceSourceURL, setSourceURL, QString, QString);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        timestamp, setTimestamp, qint64, qevercloud::Timestamp);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        resourceLatitude, setLatitude, double, double);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        resourceLongitude, setLongitude, double, double);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        resourceAltitude, setAltitude, double, double);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        cameraMake, setCameraMake, QString, QString);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        cameraModel, setCameraModel, QString, QString);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(
        clientWillIndex, setClientWillIndex, int, bool);

    CHECK_AND_SET_RESOURCE_ATTRIBUTE(fileName, setFileName, QString, QString);
    CHECK_AND_SET_RESOURCE_ATTRIBUTE(attachment, setAttachment, int, bool);

#undef CHECK_AND_SET_RESOURCE_ATTRIBUTE

    return hasSomething;
}

bool LocalStorageManagerPrivate::
    fillResourceAttributesApplicationDataKeysOnlyFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const
{
    bool hasSomething = false;

    const int index = rec.indexOf(QStringLiteral("resourceKey"));
    if (index >= 0) {
        const QVariant value = rec.value(index);
        if (!value.isNull()) {
            if (!attributes.applicationData()) {
                attributes.setApplicationData(qevercloud::LazyMap{});
            }

            if (!attributes.applicationData()->keysOnly()) {
                attributes.mutableApplicationData()->setKeysOnly(
                    QSet<QString>());
            }

            attributes.mutableApplicationData()->mutableKeysOnly()->insert(
                value.toString());

            hasSomething = true;
        }
    }

    return hasSomething;
}

bool LocalStorageManagerPrivate::
    fillResourceAttributesApplicationDataFullMapFromSqlRecord(
        const QSqlRecord & rec,
        qevercloud::ResourceAttributes & attributes) const
{
    bool hasSomething = false;

    const int keyIndex = rec.indexOf(QStringLiteral("resourceMapKey"));
    const int valueIndex = rec.indexOf(QStringLiteral("resourceValue"));
    if ((keyIndex >= 0) && (valueIndex >= 0)) {
        const QVariant key = rec.value(keyIndex);
        const QVariant value = rec.value(valueIndex);
        if (!key.isNull() && !value.isNull()) {
            if (!attributes.applicationData()) {
                attributes.setApplicationData(qevercloud::LazyMap());
            }

            if (!attributes.applicationData()->fullMap()) {
                attributes.mutableApplicationData()->setFullMap(
                    QMap<QString, QString>());
            }

            attributes.mutableApplicationData()->mutableFullMap()->insert(
                key.toString(), value.toString());

            hasSomething = true;
        }
    }

    return hasSomething;
}

void LocalStorageManagerPrivate::fillNoteAttributesFromSqlRecord(
    const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const
{
#define CHECK_AND_SET_NOTE_ATTRIBUTE(property, setter, type, localType)        \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                attributes.setter(                                             \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        subjectDate, setSubjectDate, qint64, qevercloud::Timestamp);

    CHECK_AND_SET_NOTE_ATTRIBUTE(latitude, setLatitude, double, double);
    CHECK_AND_SET_NOTE_ATTRIBUTE(longitude, setLongitude, double, double);
    CHECK_AND_SET_NOTE_ATTRIBUTE(altitude, setAltitude, double, double);
    CHECK_AND_SET_NOTE_ATTRIBUTE(author, setAuthor, QString, QString);
    CHECK_AND_SET_NOTE_ATTRIBUTE(source, setSource, QString, QString);
    CHECK_AND_SET_NOTE_ATTRIBUTE(sourceURL, setSourceURL, QString, QString);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        sourceApplication, setSourceApplication, QString, QString);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        shareDate, setShareDate, qint64, qevercloud::Timestamp);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        reminderOrder, setReminderOrder, qint64, qint64);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        reminderDoneTime, setReminderDoneTime, qint64, qevercloud::Timestamp);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        reminderTime, setReminderTime, qint64, qevercloud::Timestamp);

    CHECK_AND_SET_NOTE_ATTRIBUTE(placeName, setPlaceName, QString, QString);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        contentClass, setContentClass, QString, QString);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        lastEditedBy, setLastEditedBy, QString, QString);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        creatorId, setCreatorId, qint32, qevercloud::UserID);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        lastEditorId, setLastEditorId, qint32, qevercloud::UserID);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        sharedWithBusiness, setSharedWithBusiness, int, bool);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        conflictSourceNoteGuid, setConflictSourceNoteGuid, QString, QString);

    CHECK_AND_SET_NOTE_ATTRIBUTE(
        noteTitleQuality, setNoteTitleQuality, qint32, qint32);

#undef CHECK_AND_SET_NOTE_ATTRIBUTE
}

void LocalStorageManagerPrivate::
    fillNoteAttributesApplicationDataKeysOnlyFromSqlRecord(
        const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const
{
    const int keysOnlyIndex =
        rec.indexOf(QStringLiteral("applicationDataKeysOnly"));
    if (keysOnlyIndex < 0) {
        return;
    }

    const QVariant value = rec.value(keysOnlyIndex);
    if (value.isNull()) {
        return;
    }

    bool applicationDataWasEmpty = !attributes.applicationData();
    if (applicationDataWasEmpty) {
        attributes.setApplicationData(qevercloud::LazyMap());
    }

    if (!attributes.applicationData()->keysOnly()) {
        attributes.mutableApplicationData()->setKeysOnly(QSet<QString>());
    }

    QSet<QString> & keysOnly =
        *attributes.mutableApplicationData()->mutableKeysOnly();

    const QString keysOnlyString = value.toString();
    const int length = keysOnlyString.length();
    bool insideQuotedText = false;
    QString currentKey;
    const QChar wordSep = QChar::fromLatin1('\'');
    for (int i = 0; i < (length - 1); ++i) {
        const QChar currentChar = keysOnlyString.at(i);
        const QChar nextChar = keysOnlyString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                keysOnly.insert(currentKey);
                currentKey.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentKey.append(currentChar);
        }
    }

    if (!currentKey.isEmpty()) {
        keysOnly.insert(currentKey);
    }

    if (keysOnly.isEmpty()) {
        if (applicationDataWasEmpty) {
            attributes.mutableApplicationData() = std::nullopt;
        }
        else {
            attributes.mutableApplicationData()->mutableKeysOnly() =
                std::nullopt;
        }
    }
}

void LocalStorageManagerPrivate::
    fillNoteAttributesApplicationDataFullMapFromSqlRecord(
        const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const
{
    const int keyIndex = rec.indexOf(QStringLiteral("applicationDataKeysMap"));
    const int valueIndex = rec.indexOf(QStringLiteral("applicationDataValues"));
    if ((keyIndex < 0) || (valueIndex < 0)) {
        return;
    }

    const QVariant keys = rec.value(keyIndex);
    const QVariant values = rec.value(valueIndex);
    if (keys.isNull() || values.isNull()) {
        return;
    }

    const bool applicationDataWasEmpty = !attributes.applicationData();
    if (applicationDataWasEmpty) {
        attributes.setApplicationData(qevercloud::LazyMap());
    }

    if (!attributes.applicationData()->fullMap()) {
        attributes.mutableApplicationData()->setFullMap(
            QMap<QString, QString>());
    }

    QMap<QString, QString> & fullMap =
        *attributes.mutableApplicationData()->mutableFullMap();

    QStringList keysList, valuesList;

    const QString keysString = keys.toString();
    const int keysLength = keysString.length();
    keysList.reserve(keysLength / 2); // NOTE: just a wild guess

    bool insideQuotedText = false;
    QString currentKey;
    const QChar wordSep = QChar::fromLatin1('\'');
    for (int i = 0; i < (keysLength - 1); ++i) {
        const QChar currentChar = keysString.at(i);
        const QChar nextChar = keysString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                keysList << currentKey;
                currentKey.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentKey.append(currentChar);
        }
    }

    if (!currentKey.isEmpty()) {
        keysList << currentKey;
    }

    const QString valuesString = values.toString();
    int valuesLength = valuesString.length();
    valuesList.reserve(valuesLength / 2); // NOTE: just a wild guess

    insideQuotedText = false;
    QString currentValue;
    for (int i = 0; i < (valuesLength - 1); ++i) {
        const QChar currentChar = valuesString.at(i);
        const QChar nextChar = valuesString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                valuesList << currentValue;
                currentValue.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentValue.append(currentChar);
        }
    }

    if (!currentValue.isEmpty()) {
        valuesList << currentValue;
    }

    int numKeys = keysList.size();
    for (int i = 0; i < numKeys; ++i) {
        fullMap.insert(keysList.at(i), valuesList.at(i));
    }

    if (fullMap.isEmpty()) {
        if (applicationDataWasEmpty) {
            attributes.mutableApplicationData() = std::nullopt;
        }
        else {
            attributes.mutableApplicationData()->mutableFullMap() =
                std::nullopt;
        }
    }
}

void LocalStorageManagerPrivate::fillNoteAttributesClassificationsFromSqlRecord(
    const QSqlRecord & rec, qevercloud::NoteAttributes & attributes) const
{
    const int keyIndex = rec.indexOf(QStringLiteral("classificationKeys"));
    const int valueIndex = rec.indexOf(QStringLiteral("classificationValues"));
    if ((keyIndex < 0) || (valueIndex < 0)) {
        return;
    }

    const QVariant keys = rec.value(keyIndex);
    const QVariant values = rec.value(valueIndex);
    if (keys.isNull() || values.isNull()) {
        return;
    }

    const bool classificationsWereEmpty = !attributes.classifications();
    if (classificationsWereEmpty) {
        attributes.setClassifications(QMap<QString, QString>());
    }

    QMap<QString, QString> & classifications =
        *attributes.mutableClassifications();
    QStringList keysList, valuesList;

    const QString keysString = keys.toString();
    const int keysLength = keysString.length();
    keysList.reserve(keysLength / 2); // NOTE: just a wild guess
    bool insideQuotedText = false;
    QString currentKey;
    QChar wordSep = QChar::fromLatin1('\'');
    for (int i = 0; i < (keysLength - 1); ++i) {
        const QChar currentChar = keysString.at(i);
        const QChar nextChar = keysString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                keysList << currentKey;
                currentKey.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentKey.append(currentChar);
        }
    }

    const QString valuesString = values.toString();
    const int valuesLength = valuesString.length();
    valuesList.reserve(valuesLength / 2); // NOTE: just a wild guess

    insideQuotedText = false;
    QString currentValue;
    for (int i = 0; i < (valuesLength - 1); ++i) {
        const QChar currentChar = valuesString.at(i);
        const QChar nextChar = valuesString.at(i + 1);

        if (currentChar == wordSep) {
            insideQuotedText = !insideQuotedText;

            if (nextChar == wordSep) {
                valuesList << currentValue;
                currentValue.resize(0);
            }
        }
        else if (insideQuotedText) {
            currentValue.append(currentChar);
        }
    }

    int numKeys = keysList.size();
    for (int i = 0; i < numKeys; ++i) {
        classifications[keysList.at(i)] = valuesList.at(i);
    }

    if (classifications.isEmpty() && classificationsWereEmpty) {
        attributes.mutableClassifications() = std::nullopt;
    }
}

bool LocalStorageManagerPrivate::fillUserFromSqlRecord(
    const QSqlRecord & rec, qevercloud::User & user,
    ErrorString & errorDescription) const
{
#define FIND_AND_SET_USER_PROPERTY(                                            \
    column, setter, type, localType, isRequired)                               \
    {                                                                          \
        bool valueFound = false;                                               \
        const int index = rec.indexOf(QStringLiteral(#column));                \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(QStringLiteral(#column));         \
            if (!value.isNull()) {                                             \
                user.setter(                                                   \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                valueFound = true;                                             \
            }                                                                  \
        }                                                                      \
        if (!valueFound && isRequired) {                                       \
            errorDescription.setBase(                                          \
                QT_TR_NOOP("missing field in the result of SQL query"));       \
            errorDescription.details() = QStringLiteral(#column);              \
            QNERROR("local_storage", errorDescription);                        \
            return false;                                                      \
        }                                                                      \
    }

    bool isRequired = true;

    FIND_AND_SET_USER_PROPERTY(
        userIsDirty, setLocallyModified, int, bool, isRequired)

    FIND_AND_SET_USER_PROPERTY(userIsLocal, setLocalOnly, int, bool, isRequired)

    FIND_AND_SET_USER_PROPERTY(
        username, setUsername, QString, QString, !isRequired)

    FIND_AND_SET_USER_PROPERTY(email, setEmail, QString, QString, !isRequired)
    FIND_AND_SET_USER_PROPERTY(name, setName, QString, QString, !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        timezone, setTimezone, QString, QString, !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        privilege, setPrivilege, int, qevercloud::PrivilegeLevel, !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        userCreationTimestamp, setCreated, qint64, qevercloud::Timestamp,
        !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        userModificationTimestamp, setUpdated, qint64, qevercloud::Timestamp,
        !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        userDeletionTimestamp, setDeleted, qint64, qevercloud::Timestamp,
        !isRequired)

    FIND_AND_SET_USER_PROPERTY(userIsActive, setActive, int, bool, !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        userShardId, setShardId, QString, QString, !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        photoUrl, setPhotoUrl, QString, QString, !isRequired)

    FIND_AND_SET_USER_PROPERTY(
        photoLastUpdated, setPhotoLastUpdated, qint64, qevercloud::Timestamp,
        !isRequired)

#undef FIND_AND_SET_USER_PROPERTY

    bool foundSomeUserAttribute = false;
    qevercloud::UserAttributes attributes;
    if (user.attributes()) {
        const auto & userAttributes = *user.attributes();
        attributes.setViewedPromotions(userAttributes.viewedPromotions());
        attributes.setRecentMailedAddresses(
            userAttributes.recentMailedAddresses());
    }

    const int promotionIndex = rec.indexOf(QStringLiteral("promotion"));
    if (promotionIndex >= 0) {
        const QVariant value = rec.value(promotionIndex);
        if (!value.isNull()) {
            if (!attributes.viewedPromotions()) {
                attributes.setViewedPromotions(QStringList());
            }

            QString valueString = value.toString();
            if (!attributes.viewedPromotions()->contains(valueString)) {
                *attributes.mutableViewedPromotions() << valueString;
            }

            foundSomeUserAttribute = true;
        }
    }

    const int addressIndex = rec.indexOf(QStringLiteral("address"));
    if (addressIndex >= 0) {
        const QVariant value = rec.value(addressIndex);
        if (!value.isNull()) {
            if (!attributes.recentMailedAddresses()) {
                attributes.setRecentMailedAddresses(QStringList());
            }

            QString valueString = value.toString();
            if (!attributes.recentMailedAddresses()->contains(valueString)) {
                *attributes.mutableRecentMailedAddresses() << valueString;
            }

            foundSomeUserAttribute = true;
        }
    }

#define FIND_AND_SET_USER_ATTRIBUTE(column, setter, type, localType)           \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#column));                \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(QStringLiteral(#column));         \
            if (!value.isNull()) {                                             \
                attributes.setter(                                             \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                foundSomeUserAttribute = true;                                 \
            }                                                                  \
        }                                                                      \
    }

    FIND_AND_SET_USER_ATTRIBUTE(
        defaultLocationName, setDefaultLocationName, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        defaultLatitude, setDefaultLatitude, double, double);

    FIND_AND_SET_USER_ATTRIBUTE(
        defaultLongitude, setDefaultLongitude, double, double);

    FIND_AND_SET_USER_ATTRIBUTE(preactivation, setPreactivation, int, bool);

    FIND_AND_SET_USER_ATTRIBUTE(
        incomingEmailAddress, setIncomingEmailAddress, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(comments, setComments, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        dateAgreedToTermsOfService, setDateAgreedToTermsOfService, qint64,
        qevercloud::Timestamp);

    FIND_AND_SET_USER_ATTRIBUTE(maxReferrals, setMaxReferrals, qint32, qint32);

    FIND_AND_SET_USER_ATTRIBUTE(
        referralCount, setReferralCount, qint32, qint32);

    FIND_AND_SET_USER_ATTRIBUTE(refererCode, setRefererCode, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        sentEmailDate, setSentEmailDate, qint64, qevercloud::Timestamp);

    FIND_AND_SET_USER_ATTRIBUTE(
        sentEmailCount, setSentEmailCount, qint32, qint32);

    FIND_AND_SET_USER_ATTRIBUTE(
        dailyEmailLimit, setDailyEmailLimit, qint32, qint32);

    FIND_AND_SET_USER_ATTRIBUTE(
        emailOptOutDate, setEmailOptOutDate, qint64, qevercloud::Timestamp);

    FIND_AND_SET_USER_ATTRIBUTE(
        partnerEmailOptInDate, setPartnerEmailOptInDate, qint64,
        qevercloud::Timestamp);

    FIND_AND_SET_USER_ATTRIBUTE(
        preferredLanguage, setPreferredLanguage, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        preferredCountry, setPreferredCountry, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(clipFullPage, setClipFullPage, int, bool);

    FIND_AND_SET_USER_ATTRIBUTE(
        twitterUserName, setTwitterUserName, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(twitterId, setTwitterId, QString, QString);
    FIND_AND_SET_USER_ATTRIBUTE(groupName, setGroupName, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        recognitionLanguage, setRecognitionLanguage, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        referralProof, setReferralProof, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        educationalDiscount, setEducationalDiscount, int, bool);

    FIND_AND_SET_USER_ATTRIBUTE(
        businessAddress, setBusinessAddress, QString, QString);

    FIND_AND_SET_USER_ATTRIBUTE(
        hideSponsorBilling, setHideSponsorBilling, int, bool);

    FIND_AND_SET_USER_ATTRIBUTE(
        useEmailAutoFiling, setUseEmailAutoFiling, int, bool);

    FIND_AND_SET_USER_ATTRIBUTE(
        reminderEmailConfig, setReminderEmailConfig, int,
        qevercloud::ReminderEmailConfig);

    FIND_AND_SET_USER_ATTRIBUTE(
        emailAddressLastConfirmed, setEmailAddressLastConfirmed, qint64,
        qevercloud::Timestamp);

    FIND_AND_SET_USER_ATTRIBUTE(
        passwordUpdated, setPasswordUpdated, qint64, qevercloud::Timestamp)

    FIND_AND_SET_USER_ATTRIBUTE(
        salesforcePushEnabled, setSalesforcePushEnabled, int, bool)

    FIND_AND_SET_USER_ATTRIBUTE(
        shouldLogClientEvent, setShouldLogClientEvent, int, bool)

#undef FIND_AND_SET_USER_ATTRIBUTE

    if (foundSomeUserAttribute) {
        user.setAttributes(std::move(attributes));
    }

    bool foundSomeAccountingProperty = false;
    qevercloud::Accounting accounting;

#define FIND_AND_SET_ACCOUNTING_PROPERTY(column, setter, type, localType)      \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#column));                \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(QStringLiteral(#column));         \
            if (!value.isNull()) {                                             \
                accounting.setter(                                             \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                foundSomeAccountingProperty = true;                            \
            }                                                                  \
        }                                                                      \
    }

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        uploadLimitEnd, setUploadLimitEnd, qint64, qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        uploadLimitNextMonth, setUploadLimitNextMonth, qint64, qint64);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumServiceStatus, setPremiumServiceStatus, int,
        qevercloud::PremiumOrderStatus);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumOrderNumber, setPremiumOrderNumber, QString, QString);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumCommerceService, setPremiumCommerceService, QString, QString);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumServiceStart, setPremiumServiceStart, qint64,
        qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumServiceSKU, setPremiumServiceSKU, QString, QString);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        lastSuccessfulCharge, setLastSuccessfulCharge, qint64,
        qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        lastFailedCharge, setLastFailedCharge, qint64, qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        lastFailedChargeReason, setLastFailedChargeReason, QString, QString);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        nextPaymentDue, setNextPaymentDue, qint64, qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumLockUntil, setPremiumLockUntil, qint64, qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        updated, setUpdated, qint64, qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        premiumSubscriptionNumber, setPremiumSubscriptionNumber, QString,
        QString);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        lastRequestedCharge, setLastRequestedCharge, qint64,
        qevercloud::Timestamp);

    FIND_AND_SET_ACCOUNTING_PROPERTY(currency, setCurrency, QString, QString);
    FIND_AND_SET_ACCOUNTING_PROPERTY(unitPrice, setUnitPrice, int, qint32);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        unitDiscount, setUnitDiscount, int, qint32);

    FIND_AND_SET_ACCOUNTING_PROPERTY(
        nextChargeDate, setNextChargeDate, qint64, qevercloud::Timestamp);

#undef FIND_AND_SET_ACCOUNTING_PROPERTY

    if (foundSomeAccountingProperty) {
        user.setAccounting(std::move(accounting));
    }

    bool foundSomeAccountLimit = false;
    qevercloud::AccountLimits accountLimits;

#define FIND_AND_SET_ACCOUNT_LIMIT(property, setter, type, localType)          \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(QStringLiteral(#property));       \
            if (!value.isNull()) {                                             \
                accountLimits.setter(                                          \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                foundSomeAccountLimit = true;                                  \
            }                                                                  \
        }                                                                      \
    }

    FIND_AND_SET_ACCOUNT_LIMIT(
        userMailLimitDaily, setUserMailLimitDaily, int, qint32)

    FIND_AND_SET_ACCOUNT_LIMIT(noteSizeMax, setNoteSizeMax, qint64, qint64)

    FIND_AND_SET_ACCOUNT_LIMIT(
        resourceSizeMax, setResourceSizeMax, qint64, qint64)

    FIND_AND_SET_ACCOUNT_LIMIT(
        userLinkedNotebookMax, setUserLinkedNotebookMax, int, qint32)

    FIND_AND_SET_ACCOUNT_LIMIT(uploadLimit, setUploadLimit, qint64, qint64)

    FIND_AND_SET_ACCOUNT_LIMIT(
        userNoteCountMax, setUserNoteCountMax, int, qint32)

    FIND_AND_SET_ACCOUNT_LIMIT(
        userNotebookCountMax, setUserNotebookCountMax, int, qint32)

    FIND_AND_SET_ACCOUNT_LIMIT(userTagCountMax, setUserTagCountMax, int, qint32)
    FIND_AND_SET_ACCOUNT_LIMIT(noteTagCountMax, setNoteTagCountMax, int, qint32)

    FIND_AND_SET_ACCOUNT_LIMIT(
        userSavedSearchesMax, setUserSavedSearchesMax, int, qint32)

    FIND_AND_SET_ACCOUNT_LIMIT(
        noteResourceCountMax, setNoteResourceCountMax, int, qint32)

#undef FIND_AND_SET_ACCOUNT_LIMIT

    if (foundSomeAccountLimit) {
        user.setAccountLimits(std::move(accountLimits));
    }

    bool foundSomeBusinessUserInfoProperty = false;
    qevercloud::BusinessUserInfo businessUserInfo;

#define FIND_AND_SET_BUSINESS_USER_INFO_PROPERTY(                              \
    column, setter, type, localType)                                           \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#column));                \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(QStringLiteral(#column));         \
            if (!value.isNull()) {                                             \
                businessUserInfo.setter(                                       \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                foundSomeBusinessUserInfoProperty = true;                      \
            }                                                                  \
        }                                                                      \
    }

    FIND_AND_SET_BUSINESS_USER_INFO_PROPERTY(
        businessId, setBusinessId, qint32, qint32);

    FIND_AND_SET_BUSINESS_USER_INFO_PROPERTY(
        businessName, setBusinessName, QString, QString);

    FIND_AND_SET_BUSINESS_USER_INFO_PROPERTY(
        role, setRole, int, qevercloud::BusinessUserRole);

    FIND_AND_SET_BUSINESS_USER_INFO_PROPERTY(
        businessInfoEmail, setEmail, QString, QString);

#undef FIND_AND_SET_BUSINESS_USER_INFO_PROPERTY

    if (foundSomeBusinessUserInfoProperty) {
        user.setBusinessUserInfo(std::move(businessUserInfo));
    }

    return true;
}

bool LocalStorageManagerPrivate::fillNoteFromSqlRecord(
    const QSqlRecord & rec, qevercloud::Note & note,
    ErrorString & errorDescription) const
{
#define CHECK_AND_SET_NOTE_PROPERTY(                                           \
    propertyLocalName, setter, type, localType, isRequired)                    \
    {                                                                          \
        bool valueFound = false;                                               \
        const int propertyLocalName##index =                                   \
            rec.indexOf(QStringLiteral(#propertyLocalName));                   \
        if (propertyLocalName##index >= 0) {                                   \
            const QVariant value = rec.value(propertyLocalName##index);        \
            if (!value.isNull()) {                                             \
                note.setter(                                                   \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                valueFound = true;                                             \
            }                                                                  \
        }                                                                      \
        if (!valueFound && isRequired) {                                       \
            errorDescription.setBase(                                          \
                QT_TR_NOOP("missing field in the result of SQL query"));       \
            errorDescription.details() = QStringLiteral(#propertyLocalName);   \
            return false;                                                      \
        }                                                                      \
    }

    bool isRequired = true;

    CHECK_AND_SET_NOTE_PROPERTY(
        isDirty, setLocallyModified, int, bool, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        isLocal, setLocalOnly, int, bool, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        isFavorited, setLocallyFavorited, int, bool, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        localUid, setLocalId, QString, QString, isRequired);

    isRequired = false;

    CHECK_AND_SET_NOTE_PROPERTY(guid, setGuid, QString, QString, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        updateSequenceNumber, setUpdateSequenceNum, qint32, qint32, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        notebookGuid, setNotebookGuid, QString, QString, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        notebookLocalUid, setNotebookLocalId, QString, QString, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(title, setTitle, QString, QString, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        content, setContent, QString, QString, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        contentLength, setContentLength, qint32, qint32, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        contentHash, setContentHash, QByteArray, QByteArray, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        creationTimestamp, setCreated, qint64, qint64, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        modificationTimestamp, setUpdated, qint64, qint64, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(
        deletionTimestamp, setDeleted, qint64, qint64, isRequired);

    CHECK_AND_SET_NOTE_PROPERTY(isActive, setActive, int, bool, isRequired);

#undef CHECK_AND_SET_NOTE_PROPERTY

    const int indexOfThumbnail = rec.indexOf(QStringLiteral("thumbnail"));
    if (indexOfThumbnail >= 0) {
        QNTRACE(
            "local_storage",
            "Found thumbnail data for note within the SQL record");

        const QVariant thumbnailValue = rec.value(indexOfThumbnail);
        if (!thumbnailValue.isNull()) {
            note.setThumbnailData(thumbnailValue.toByteArray());
        }
    }

    const int hasAttributesIndex = rec.indexOf(QStringLiteral("hasAttributes"));
    if (hasAttributesIndex >= 0) {
        const QVariant hasAttributesValue = rec.value(hasAttributesIndex);
        if (!hasAttributesValue.isNull()) {
            const bool hasAttributes =
                static_cast<bool>(qvariant_cast<int>(hasAttributesValue));
            if (hasAttributes) {
                if (!note.attributes()) {
                    note.setAttributes(qevercloud::NoteAttributes{});
                }

                auto & attributes = *note.mutableAttributes();

                fillNoteAttributesFromSqlRecord(rec, attributes);

                fillNoteAttributesApplicationDataKeysOnlyFromSqlRecord(
                    rec, attributes);

                fillNoteAttributesApplicationDataFullMapFromSqlRecord(
                    rec, attributes);

                fillNoteAttributesClassificationsFromSqlRecord(rec, attributes);
            }
        }
    }

    bool foundSomeNoteRestriction = false;
    qevercloud::NoteRestrictions restrictions;

#define CHECK_AND_SET_NOTE_RESTRICTION(column, restriction, setter)            \
    const int restriction##Index = rec.indexOf(QStringLiteral(#column));       \
    if (restriction##Index >= 0) {                                             \
        const QVariant value = rec.value(restriction##Index);                  \
        if (!value.isNull()) {                                                 \
            restrictions.setter(                                               \
                static_cast<bool>(qvariant_cast<qint32>(value)));              \
            foundSomeNoteRestriction = true;                                   \
        }                                                                      \
    }

    CHECK_AND_SET_NOTE_RESTRICTION(
        noUpdateNoteTitle, noUpdateTitle, setNoUpdateTitle)

    CHECK_AND_SET_NOTE_RESTRICTION(
        noUpdateNoteContent, noUpdateContent, setNoUpdateContent)

    CHECK_AND_SET_NOTE_RESTRICTION(noEmailNote, noEmail, setNoEmail)
    CHECK_AND_SET_NOTE_RESTRICTION(noShareNote, noShare, setNoShare)

    CHECK_AND_SET_NOTE_RESTRICTION(
        noShareNotePublicly, noSharePublicly, setNoSharePublicly)

#undef CHECK_AND_SET_NOTE_RESTRICTION

    if (foundSomeNoteRestriction) {
        note.setRestrictions(std::move(restrictions));
    }

    bool foundSomeNoteLimit = false;
    qevercloud::NoteLimits limits;

#define CHECK_AND_SET_NOTE_LIMIT(limit, setter, columnType, type)              \
    const int limit##Index = rec.indexOf(QStringLiteral(#limit));              \
    if (limit##Index >= 0) {                                                   \
        const QVariant value = rec.value(limit##Index);                        \
        if (!value.isNull()) {                                                 \
            limits.setter(                                                     \
                static_cast<type>(qvariant_cast<columnType>(value)));          \
            foundSomeNoteLimit = true;                                         \
        }                                                                      \
    }

    CHECK_AND_SET_NOTE_LIMIT(
        noteResourceCountMax, setNoteResourceCountMax, qint32, qint32)

    CHECK_AND_SET_NOTE_LIMIT(uploadLimit, setUploadLimit, qint64, qint64)

    CHECK_AND_SET_NOTE_LIMIT(
        resourceSizeMax, setResourceSizeMax, qint64, qint64)

    CHECK_AND_SET_NOTE_LIMIT(noteSizeMax, setNoteSizeMax, qint64, qint64)
    CHECK_AND_SET_NOTE_LIMIT(uploaded, setUploaded, qint64, qint64)

#undef CHECK_AND_SET_NOTE_LIMIT

    if (foundSomeNoteLimit) {
        note.setLimits(std::move(limits));
    }

    return true;
}

bool LocalStorageManagerPrivate::fillSharedNoteFromSqlRecord(
    const QSqlRecord & record, qevercloud::SharedNote & sharedNote,
    int & indexInNote, ErrorString & errorDescription) const
{
#define CHECK_AND_SET_SHARED_NOTE_PROPERTY(property, type, localType, setter)  \
    {                                                                          \
        const int index = record.indexOf(QStringLiteral(#property));           \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                sharedNote.setter(                                             \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteNoteGuid, QString, QString, setNoteGuid)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteSharerUserId, qint32, qint32, setSharerUserID)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNotePrivilegeLevel, qint8, qevercloud::SharedNotePrivilegeLevel,
        setPrivilege)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteCreationTimestamp, qint64, qint64, setServiceCreated)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteModificationTimestamp, qint64, qint64, setServiceUpdated)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteAssignmentTimestamp, qint64, qint64, setServiceAssigned)

#undef CHECK_AND_SET_SHARED_NOTE_PROPERTY

#define CHECK_AND_SET_SHARED_NOTE_PROPERTY(property, type, localType, setter)  \
    {                                                                          \
        const int index = record.indexOf(QStringLiteral(#property));           \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                if (!sharedNote.recipientIdentity()) {                         \
                    sharedNote.setRecipientIdentity(qevercloud::Identity{});   \
                }                                                              \
                sharedNote.mutableRecipientIdentity()->setter(                 \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientIdentityId, qint64, qint64, setId)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientUserId, qint32, qint32, setUserId)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientDeactivated, int, bool, setDeactivated)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientSameBusiness, int, bool, setSameBusiness)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientBlocked, int, bool, setBlocked)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientUserConnected, int, bool, setUserConnected)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientEventId, qint64, qint64, setEventId)

#undef CHECK_AND_SET_SHARED_NOTE_PROPERTY

#define CHECK_AND_SET_SHARED_NOTE_PROPERTY(property, type, localType, setter)  \
    {                                                                          \
        const int index = record.indexOf(QStringLiteral(#property));           \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                if (!sharedNote.recipientIdentity()) {                         \
                    sharedNote.setRecipientIdentity(qevercloud::Identity{});   \
                }                                                              \
                if (!sharedNote.recipientIdentity()->contact()) {              \
                    sharedNote.mutableRecipientIdentity()->setContact(         \
                        qevercloud::Contact{});                                \
                }                                                              \
                sharedNote.mutableRecipientIdentity()                          \
                    ->mutableContact()                                         \
                    ->setter(                                                  \
                        static_cast<localType>(qvariant_cast<type>(value)));   \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactName, QString, QString, setName)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactId, QString, QString, setId)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactType, qint32, qevercloud::ContactType,
        setType)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactPhotoUrl, QString, QString, setPhotoUrl)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactPhotoLastUpdated, qint64, qint64,
        setPhotoLastUpdated)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactMessagingPermit, QByteArray, QByteArray,
        setMessagingPermit)

    CHECK_AND_SET_SHARED_NOTE_PROPERTY(
        sharedNoteRecipientContactMessagingPermitExpires, qint64, qint64,
        setMessagingPermitExpires)

#undef CHECK_AND_SET_SHARED_NOTE_PROPERTY

    const int indexInNoteIndex = record.indexOf(QStringLiteral("indexInNote"));
    if (indexInNoteIndex >= 0) {
        const QVariant value = record.value(indexInNoteIndex);
        if (!value.isNull()) {
            bool conversionResult = false;
            const int index = value.toInt(&conversionResult);
            if (!conversionResult) {
                errorDescription.setBase(
                    QT_TR_NOOP("can't convert shared note's index in note to "
                               "int"));
                QNERROR("local_storage", errorDescription);
                return false;
            }
            indexInNote = index;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::fillNoteTagIdFromSqlRecord(
    const QSqlRecord & record, const QString & column,
    QList<std::pair<QString, int>> & tagIdsAndIndices,
    QHash<QString, int> & tagIndexPerId, ErrorString & errorDescription) const
{
    const int tagIdIndex = record.indexOf(column);
    if (tagIdIndex < 0) {
        return true;
    }

    const QVariant value = record.value(tagIdIndex);
    if (value.isNull()) {
        return true;
    }

    const QVariant tagGuidIndexInNoteValue =
        record.value(QStringLiteral("tagIndexInNote"));

    if (tagGuidIndexInNoteValue.isNull()) {
        QNWARNING(
            "local_storage",
            "tag index in note was not found in "
                << "the result of SQL query");
        return true;
    }

    bool conversionResult = false;
    const int tagIndexInNote = tagGuidIndexInNoteValue.toInt(&conversionResult);
    if (!conversionResult) {
        errorDescription.setBase(
            QT_TR_NOOP("can't convert tag's index in note to int"));
        return false;
    }

    const QString tagId = value.toString();
    const auto it = tagIndexPerId.find(tagId);
    const bool tagIndexNotFound = (it == tagIndexPerId.end());
    if (tagIndexNotFound) {
        const int tagIndexInList = tagIdsAndIndices.size();
        tagIndexPerId[tagId] = tagIndexInList;
        tagIdsAndIndices << std::make_pair(tagId, tagIndexInNote);
        return true;
    }

    auto & tagIdAndIndexInNote = tagIdsAndIndices[it.value()];
    tagIdAndIndexInNote.first = tagId;
    tagIdAndIndexInNote.second = tagIndexInNote;
    return true;
}

bool LocalStorageManagerPrivate::fillNotebookFromSqlRecord(
    const QSqlRecord & record, qevercloud::Notebook & notebook,
    ErrorString & errorDescription) const
{
#define CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(                                      \
    attribute, setter, dbType, trueType, isRequired)                           \
    {                                                                          \
        bool valueFound = false;                                               \
        const int index = record.indexOf(QStringLiteral(#attribute));          \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                notebook.setter(                                               \
                    static_cast<trueType>((qvariant_cast<dbType>(value))));    \
                valueFound = true;                                             \
            }                                                                  \
        }                                                                      \
        if (!valueFound && isRequired) {                                       \
            errorDescription.setBase(                                          \
                QT_TR_NOOP("missing field in the result of SQL query"));       \
            errorDescription.details() = QStringLiteral(#attribute);           \
            return false;                                                      \
        }                                                                      \
    }

    bool isRequired = true;

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        isDirty, setLocallyModified, int, bool, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        isLocal, setLocalOnly, int, bool, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        localUid, setLocalId, QString, QString, isRequired);

    isRequired = false;

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        updateSequenceNumber, setUpdateSequenceNum, qint32, qint32, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        notebookName, setName, QString, QString, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        creationTimestamp, setServiceCreated, qint64, qint64, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        modificationTimestamp, setServiceUpdated, qint64, qint64, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        guid, setGuid, QString, QString, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        isFavorited, setLocallyFavorited, int, bool, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        stack, setStack, QString, QString, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        isDefault, setDefaultNotebook, int, bool, isRequired);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        isPublished, setPublished, int, bool, isRequired);

#undef CHECK_AND_SET_NOTEBOOK_ATTRIBUTE

#define CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(attribute, setter, dbType, trueType)  \
    {                                                                          \
        const int index = record.indexOf(QStringLiteral(#attribute));          \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                if (!notebook.publishing()) {                                  \
                    notebook.setPublishing(qevercloud::Publishing{});          \
                }                                                              \
                notebook.mutablePublishing()->setter(                          \
                    static_cast<trueType>((qvariant_cast<dbType>(value))));    \
            }                                                                  \
        }                                                                      \
    }

    if (notebook.published() && *notebook.published()) {
        CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
            publishingUri, setUri, QString, QString);

        CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
            publishingNoteSortOrder, setOrder, int, qevercloud::NoteSortOrder);

        CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
            publishingAscendingSort, setAscending, int, bool);

        CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
            publicDescription, setPublicDescription, QString, QString);
    }

#undef CHECK_AND_SET_NOTEBOOK_ATTRIBUTE

#define CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(attribute, setter, dbType, trueType)  \
    {                                                                          \
        const int index = record.indexOf(QStringLiteral(#attribute));          \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                if (!notebook.businessNotebook()) {                            \
                    notebook.setBusinessNotebook(                              \
                        qevercloud::BusinessNotebook{});                       \
                }                                                              \
                notebook.mutableBusinessNotebook()->setter(                    \
                    static_cast<trueType>((qvariant_cast<dbType>(value))));    \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        businessNotebookDescription, setNotebookDescription, QString, QString);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        businessNotebookPrivilegeLevel, setPrivilege, int,
        qevercloud::SharedNotebookPrivilegeLevel);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        businessNotebookIsRecommended, setRecommended, int, bool);

#undef CHECK_AND_SET_NOTEBOOK_ATTRIBUTE

#define CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(attribute, setter, dbType, trueType)  \
    {                                                                          \
        const int index = record.indexOf(QStringLiteral(#attribute));          \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                if (!notebook.recipientSettings()) {                           \
                    notebook.setRecipientSettings(                             \
                        qevercloud::NotebookRecipientSettings{});              \
                }                                                              \
                notebook.mutableRecipientSettings()->setter(                   \
                    static_cast<trueType>((qvariant_cast<dbType>(value))));    \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        recipientReminderNotifyEmail, setReminderNotifyEmail, int, bool);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        recipientReminderNotifyInApp, setReminderNotifyInApp, int, bool);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(recipientInMyList, setInMyList, int, bool);

    CHECK_AND_SET_NOTEBOOK_ATTRIBUTE(
        recipientStack, setStack, QString, QString);

#undef CHECK_AND_SET_NOTEBOOK_ATTRIBUTE

    const int isLastUsedIndex = record.indexOf(QStringLiteral("isLastUsed"));
    if (isLastUsedIndex >= 0) {
        const QVariant value = record.value(isLastUsedIndex);
        if (!value.isNull()) {
            notebook.mutableLocalData().insert(
                QStringLiteral("isLastUsed"), value.toBool());
        }
    }

    const int linkedNotebookGuidIndex =
        record.indexOf(QStringLiteral("linkedNotebookGuid"));
    if (linkedNotebookGuidIndex >= 0) {
        const QVariant value = record.value(linkedNotebookGuidIndex);
        if (!value.isNull()) {
            notebook.setLinkedNotebookGuid(value.toString());
        }
    }

    // NOTE: workarounding unset isDefaultNotebook and isLastUsed
    if (!notebook.defaultNotebook()) {
        notebook.setDefaultNotebook(false);
    }

    if (record.contains(QStringLiteral("contactId")) &&
        !record.isNull(QStringLiteral("contactId")))
    {
        if (notebook.contact()) {
            auto & contact = *notebook.mutableContact();
            contact.setId(qvariant_cast<qint32>(
                record.value(QStringLiteral("contactId"))));
        }
        else {
            qevercloud::User contact;
            contact.setId(qvariant_cast<qint32>(
                record.value(QStringLiteral("contactId"))));
            notebook.setContact(contact);
        }

        auto & user = *notebook.mutableContact();
        bool res = fillUserFromSqlRecord(record, user, errorDescription);
        if (!res) {
            return false;
        }
    }

#define SET_EN_NOTEBOOK_RESTRICTION(notebook_restriction, setter)              \
    {                                                                          \
        const int index =                                                      \
            record.indexOf(QStringLiteral(#notebook_restriction));             \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                if (!notebook.restrictions()) {                                \
                    notebook.setRestrictions(                                  \
                        qevercloud::NotebookRestrictions{});                   \
                }                                                              \
                notebook.mutableRestrictions()->setter(                        \
                    qvariant_cast<int>(value) > 0 ? true : false);             \
            }                                                                  \
        }                                                                      \
    }

    SET_EN_NOTEBOOK_RESTRICTION(noReadNotes, setNoReadNotes);
    SET_EN_NOTEBOOK_RESTRICTION(noCreateNotes, setNoCreateNotes);
    SET_EN_NOTEBOOK_RESTRICTION(noUpdateNotes, setNoUpdateNotes);
    SET_EN_NOTEBOOK_RESTRICTION(noExpungeNotes, setNoExpungeNotes);
    SET_EN_NOTEBOOK_RESTRICTION(noShareNotes, setNoShareNotes);
    SET_EN_NOTEBOOK_RESTRICTION(noEmailNotes, setNoEmailNotes);

    SET_EN_NOTEBOOK_RESTRICTION(
        noSendMessageToRecipients, setNoSendMessageToRecipients);

    SET_EN_NOTEBOOK_RESTRICTION(noUpdateNotebook, setNoUpdateNotebook);
    SET_EN_NOTEBOOK_RESTRICTION(noExpungeNotebook, setNoExpungeNotebook);
    SET_EN_NOTEBOOK_RESTRICTION(noSetDefaultNotebook, setNoSetDefaultNotebook);
    SET_EN_NOTEBOOK_RESTRICTION(noSetNotebookStack, setNoSetNotebookStack);
    SET_EN_NOTEBOOK_RESTRICTION(noPublishToPublic, setNoPublishToPublic);

    SET_EN_NOTEBOOK_RESTRICTION(
        noPublishToBusinessLibrary, setNoPublishToBusinessLibrary);

    SET_EN_NOTEBOOK_RESTRICTION(noCreateTags, setNoCreateTags);
    SET_EN_NOTEBOOK_RESTRICTION(noUpdateTags, setNoUpdateTags);
    SET_EN_NOTEBOOK_RESTRICTION(noExpungeTags, setNoExpungeTags);
    SET_EN_NOTEBOOK_RESTRICTION(noSetParentTag, setNoSetParentTag);

    SET_EN_NOTEBOOK_RESTRICTION(
        noCreateSharedNotebooks, setNoCreateSharedNotebooks);

    SET_EN_NOTEBOOK_RESTRICTION(
        noShareNotesWithBusiness, setNoShareNotesWithBusiness);

    SET_EN_NOTEBOOK_RESTRICTION(noRenameNotebook, setNoRenameNotebook);

#undef SET_EN_NOTEBOOK_RESTRICTION

#define SET_SHARED_NOTEBOOK_RESTRICTION(notebook_restriction, setter, type)    \
    {                                                                          \
        const int index =                                                      \
            record.indexOf(QStringLiteral(#notebook_restriction));             \
        if (index >= 0) {                                                      \
            const QVariant value = record.value(index);                        \
            if (!value.isNull()) {                                             \
                bool conversionResult = false;                                 \
                const int valueInt = value.toInt(&conversionResult);           \
                if (conversionResult) {                                        \
                    if (!notebook.restrictions()) {                            \
                        notebook.setRestrictions(                              \
                            qevercloud::NotebookRestrictions{});               \
                    }                                                          \
                    notebook.mutableRestrictions()->setter(                    \
                        qvariant_cast<type>(valueInt));                        \
                }                                                              \
            }                                                                  \
        }                                                                      \
    }

    SET_SHARED_NOTEBOOK_RESTRICTION(
        updateWhichSharedNotebookRestrictions,
        setUpdateWhichSharedNotebookRestrictions,
        qevercloud::SharedNotebookInstanceRestrictions)

    SET_SHARED_NOTEBOOK_RESTRICTION(
        expungeWhichSharedNotebookRestrictions,
        setExpungeWhichSharedNotebookRestrictions,
        qevercloud::SharedNotebookInstanceRestrictions)

#undef SET_SHARED_NOTEBOOK_RESTRICTION

    return true;
}

bool LocalStorageManagerPrivate::fillSharedNotebookFromSqlRecord(
    const QSqlRecord & rec, qevercloud::SharedNotebook & sharedNotebook,
    ErrorString & errorDescription) const
{
#define CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(                                \
    property, type, localType, setter)                                         \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                sharedNotebook.setter(                                         \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookShareId, qint64, qint64, setId)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookUserId, qint32, qint32, setUserId)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookNotebookGuid, QString, QString, setNotebookGuid)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookEmail, QString, QString, setEmail)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookCreationTimestamp, qint64, qevercloud::Timestamp,
        setServiceCreated)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookModificationTimestamp, qint64, qevercloud::Timestamp,
        setServiceUpdated)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookGlobalId, QString, QString, setGlobalId)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookUsername, QString, QString, setUsername)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookPrivilegeLevel, int,
        qevercloud::SharedNotebookPrivilegeLevel, setPrivilege)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookSharerUserId, qint32, qint32, setSharerUserId)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookRecipientUsername, QString, QString, setRecipientUsername)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookRecipientUserId, qint32, qint32, setRecipientUserId)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookRecipientIdentityId, qint64, qint64,
        setRecipientIdentityId)

    CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY(
        sharedNotebookAssignmentTimestamp, qint64, qint64, setServiceAssigned)

#undef CHECK_AND_SET_SHARED_NOTEBOOK_PROPERTY

#define CHECK_AND_SET_SHARED_NOTEBOOK_RECIPIENT_SETTING(                       \
    property, type, localType, setter)                                         \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                if (!sharedNotebook.recipientSettings()) {                     \
                    sharedNotebook.setRecipientSettings(                       \
                        qevercloud::SharedNotebookRecipientSettings{});        \
                }                                                              \
                sharedNotebook.mutableRecipientSettings()->setter(             \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_SHARED_NOTEBOOK_RECIPIENT_SETTING(
        sharedNotebookRecipientReminderNotifyEmail, int, bool,
        setReminderNotifyEmail)

    CHECK_AND_SET_SHARED_NOTEBOOK_RECIPIENT_SETTING(
        sharedNotebookRecipientReminderNotifyInApp, int, bool,
        setReminderNotifyInApp)

#undef CHECK_AND_SET_SHARED_NOTEBOOK_RECIPIENT_SETTING

    const int recordIndex = rec.indexOf(QStringLiteral("indexInNotebook"));
    if (recordIndex >= 0) {
        const QVariant value = rec.value(recordIndex);
        if (!value.isNull()) {
            bool conversionResult = false;
            const int indexInNotebook = value.toInt(&conversionResult);
            if (!conversionResult) {
                errorDescription.setBase(
                    QT_TR_NOOP("can't convert shared notebook's index in "
                               "notebook to int"));
                QNERROR("local_storage", errorDescription);
                return false;
            }
            sharedNotebook
                .mutableLocalData()[QStringLiteral("indexInNotebook")] =
                indexInNotebook;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::fillLinkedNotebookFromSqlRecord(
    const QSqlRecord & rec, qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) const
{
#define CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(                                \
    property, type, localType, setter, isRequired)                             \
    {                                                                          \
        bool valueFound = false;                                               \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                linkedNotebook.setter(                                         \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                valueFound = true;                                             \
            }                                                                  \
        }                                                                      \
        if (!valueFound && isRequired) {                                       \
            errorDescription.setBase(                                          \
                QT_TR_NOOP("missing field in the result of SQL query"));       \
            errorDescription.details() = QStringLiteral(#property);            \
            QNERROR("local_storage", errorDescription);                        \
            return false;                                                      \
        }                                                                      \
    }

    bool isRequired = true;
    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        guid, QString, QString, setGuid, isRequired);

    isRequired = false;
    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        isDirty, int, bool, setLocallyModified, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        updateSequenceNumber, qint32, qint32, setUpdateSequenceNum, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        shareName, QString, QString, setShareName, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        username, QString, QString, setUsername, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        shardId, QString, QString, setShardId, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        sharedNotebookGlobalId, QString, QString, setSharedNotebookGlobalId,
        isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        uri, QString, QString, setUri, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        noteStoreUrl, QString, QString, setNoteStoreUrl, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        webApiUrlPrefix, QString, QString, setWebApiUrlPrefix, isRequired);

    isRequired = false;
    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        stack, QString, QString, setStack, isRequired);

    CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY(
        businessId, qint32, qint32, setBusinessId, isRequired);

#undef CHECK_AND_SET_LINKED_NOTEBOOK_PROPERTY

    return true;
}

bool LocalStorageManagerPrivate::fillSavedSearchFromSqlRecord(
    const QSqlRecord & rec, qevercloud::SavedSearch & search,
    ErrorString & errorDescription) const
{
#define CHECK_AND_SET_SAVED_SEARCH_PROPERTY(                                   \
    property, type, localType, setter, isRequired)                             \
    {                                                                          \
        bool valueFound = false;                                               \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                search.setter(                                                 \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                valueFound = true;                                             \
            }                                                                  \
        }                                                                      \
        if (!valueFound && isRequired) {                                       \
            errorDescription.setBase(                                          \
                QT_TR_NOOP("missing field in the result of SQL query"));       \
            errorDescription.details() = QStringLiteral(#property);            \
            QNERROR("local_storage", errorDescription);                        \
            return false;                                                      \
        }                                                                      \
    }

    bool isRequired = false;
    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        guid, QString, QString, setGuid, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        name, QString, QString, setName, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        query, QString, QString, setQuery, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        format, int, qevercloud::QueryFormat, setFormat, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        updateSequenceNumber, qint32, qint32, setUpdateSequenceNum, isRequired);

    isRequired = true;
    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        localUid, QString, QString, setLocalId, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        isDirty, int, bool, setLocallyModified, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        isLocal, int, bool, setLocalOnly, isRequired);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        isFavorited, int, bool, setLocallyFavorited, isRequired);

#undef CHECK_AND_SET_SAVED_SEARCH_PROPERTY

#define CHECK_AND_SET_SAVED_SEARCH_PROPERTY(property, type, localType, setter) \
    {                                                                          \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                if (!search.scope()) {                                         \
                    search.setScope(qevercloud::SavedSearchScope{});           \
                }                                                              \
                search.mutableScope()->setter(                                 \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
            }                                                                  \
        }                                                                      \
    }

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        includeAccount, int, bool, setIncludeAccount);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        includePersonalLinkedNotebooks, int, bool,
        setIncludePersonalLinkedNotebooks);

    CHECK_AND_SET_SAVED_SEARCH_PROPERTY(
        includeBusinessLinkedNotebooks, int, bool,
        setIncludeBusinessLinkedNotebooks);

#undef CHECK_AND_SET_SAVED_SEARCH_PROPERTY

    return true;
}

bool LocalStorageManagerPrivate::fillTagFromSqlRecord(
    const QSqlRecord & rec, qevercloud::Tag & tag,
    ErrorString & errorDescription) const
{
#define CHECK_AND_SET_TAG_PROPERTY(                                            \
    property, type, localType, setter, isRequired)                             \
    {                                                                          \
        bool valueFound = false;                                               \
        const int index = rec.indexOf(QStringLiteral(#property));              \
        if (index >= 0) {                                                      \
            const QVariant value = rec.value(index);                           \
            if (!value.isNull()) {                                             \
                tag.setter(                                                    \
                    static_cast<localType>(qvariant_cast<type>(value)));       \
                valueFound = true;                                             \
            }                                                                  \
        }                                                                      \
        if (!valueFound && isRequired) {                                       \
            errorDescription.setBase(                                          \
                QT_TR_NOOP("missing field in the result of SQL query"));       \
            errorDescription.details() = QStringLiteral(#property);            \
            QNERROR("local_storage", errorDescription);                        \
            return false;                                                      \
        }                                                                      \
    }

    bool isRequired = false;
    CHECK_AND_SET_TAG_PROPERTY(guid, QString, QString, setGuid, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(
        updateSequenceNumber, qint32, qint32, setUpdateSequenceNum, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(name, QString, QString, setName, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(
        parentGuid, QString, QString, setParentGuid, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(
        parentLocalUid, QString, QString, setParentTagLocalId, isRequired);

    isRequired = true;
    CHECK_AND_SET_TAG_PROPERTY(
        localUid, QString, QString, setLocalId, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(
        isDirty, int, bool, setLocallyModified, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(isLocal, int, bool, setLocalOnly, isRequired);

    CHECK_AND_SET_TAG_PROPERTY(
        isFavorited, int, bool, setLocallyFavorited, isRequired);

#undef CHECK_AND_SET_TAG_PROPERTY

    const int linkedNotebookGuidIndex =
        rec.indexOf(QStringLiteral("linkedNotebookGuid"));
    if (linkedNotebookGuidIndex >= 0) {
        const QVariant value = rec.value(linkedNotebookGuidIndex);
        if (!value.isNull()) {
            tag.setLinkedNotebookGuid(value.toString());
        }
    }
    else {
        errorDescription.setBase(
            QT_TR_NOOP("missing linked notebook guid in the result of SQL "
                       "query"));
        QNERROR("local_storage", errorDescription);
        return false;
    }

    return true;
}

QList<qevercloud::Tag> LocalStorageManagerPrivate::fillTagsFromSqlQuery(
    QSqlQuery & query, ErrorString & errorDescription) const
{
    QList<qevercloud::Tag> tags;
    tags.reserve(qMax(query.size(), 0));

    while (query.next()) {
        tags << qevercloud::Tag();
        auto & tag = tags.back();

        const QString tagLocalId = query.value(0).toString();
        if (tagLocalId.isEmpty()) {
            errorDescription.setBase(
                QT_TR_NOOP("no tag's local id in the result of SQL query"));
            tags.clear();
            return tags;
        }

        tag.setLocalId(tagLocalId);

        bool res = findTag(tag, errorDescription);
        if (!res) {
            tags.clear();
            return tags;
        }
    }

    return tags;
}

bool LocalStorageManagerPrivate::findAndSetTagIdsPerNote(
    qevercloud::Note & note, ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't find tag guids/local ids per note"));

    const QString noteLocalId = note.localId();

    QSqlQuery query(m_sqlDatabase);
    query.prepare(
        QStringLiteral("SELECT tag, localTag, tagIndexInNote FROM "
                       "NoteTags WHERE localNote = ?"));
    query.addBindValue(noteLocalId);

    const bool res = query.exec();
    DATABASE_CHECK_AND_SET_ERROR()

    QMultiHash<int, QString> tagGuidsAndIndices;
    QMultiHash<int, QString> tagLocalIdsAndIndices;

    while (query.next()) {
        const QSqlRecord rec = query.record();

        QString tagLocalId;
        QString tagGuid;

        bool tagLocalIdFound = false;
        bool tagGuidFound = false;

        const int tagGuidIndex = rec.indexOf(QStringLiteral("tag"));
        if (tagGuidIndex >= 0) {
            tagGuid = rec.value(tagGuidIndex).toString();
            tagGuidFound = true;
        }

        int tagLocalIdIndex = rec.indexOf(QStringLiteral("localTag"));
        if (tagLocalIdIndex >= 0) {
            const QVariant value = rec.value(tagLocalIdIndex);
            if (!value.isNull()) {
                tagLocalId = value.toString();
                tagLocalIdFound = true;
            }
        }

        if (!tagLocalIdFound) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("no tag local id in the result of SQL query"));
            return false;
        }

        if (!tagGuidFound) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("no tag guid in the result of SQL query"));
            return false;
        }

        if (!tagGuid.isEmpty() && !checkGuid(tagGuid)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("found invalid tag guid for the requested note"));
            return false;
        }

        QNTRACE(
            "local_storage",
            "Found tag local id " << tagLocalId << " and tag guid " << tagGuid
                                  << " for note with local id " << noteLocalId);

        int indexInNote = -1;
        const int recordIndex = rec.indexOf(QStringLiteral("tagIndexInNote"));
        if (recordIndex >= 0) {
            const QVariant value = rec.value(recordIndex);
            if (!value.isNull()) {
                bool conversionResult = false;
                indexInNote = value.toInt(&conversionResult);
                if (!conversionResult) {
                    errorDescription.base() = errorPrefix.base();
                    errorDescription.appendBase(
                        QT_TR_NOOP("can't convert tag index in note to int"));
                    return false;
                }
            }
        }

        tagLocalIdsAndIndices.insert(indexInNote, tagLocalId);

        if (!tagGuid.isEmpty()) {
            tagGuidsAndIndices.insert(indexInNote, tagGuid);
        }
    }

    // Setting tag local ids

    const int numTagLocalIds = tagLocalIdsAndIndices.size();
    QList<std::pair<QString, int>> tagLocalIdIndexPairs;
    tagLocalIdIndexPairs.reserve(std::max(numTagLocalIds, 0));
    for (const auto & it: qevercloud::toRange(tagLocalIdsAndIndices)) {
        tagLocalIdIndexPairs << std::make_pair(it.value(), it.key());
    }

    std::sort(
        tagLocalIdIndexPairs.begin(), tagLocalIdIndexPairs.end(),
        QStringIntPairCompareByInt());

    QStringList tagLocalIds;
    tagLocalIds.reserve(std::max(numTagLocalIds, 0));
    for (int i = 0; i < numTagLocalIds; ++i) {
        tagLocalIds << tagLocalIdIndexPairs[i].first;
    }

    note.setTagLocalIds(tagLocalIds);

    // Setting tag guids

    const int numTagGuids = tagGuidsAndIndices.size();
    QList<std::pair<QString, int>> tagGuidIndexPairs;
    tagGuidIndexPairs.reserve(std::max(numTagGuids, 0));

    for (const auto & it: qevercloud::toRange(tagGuidsAndIndices)) {
        tagGuidIndexPairs << std::make_pair(it.value(), it.key());
    }

    std::sort(
        tagGuidIndexPairs.begin(), tagGuidIndexPairs.end(),
        QStringIntPairCompareByInt());

    QStringList tagGuids;
    tagGuids.reserve(std::max(numTagGuids, 0));
    for (int i = 0; i < numTagGuids; ++i) {
        const QString & guid = tagGuidIndexPairs[i].first;
        if (guid.isEmpty()) {
            continue;
        }

        tagGuids << guid;
    }

    note.setTagGuids(tagGuids);

    return true;
}

bool LocalStorageManagerPrivate::noteSearchQueryToSQL(
    const NoteSearchQuery & noteSearchQuery, QString & sql,
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't convert note search query string into SQL query"));

    // 1) Setting up initial templates
    QString sqlPrefix = QStringLiteral("SELECT DISTINCT localUid ");
    sql.resize(0);

    // 2) Determining whether "any:" modifier takes effect

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();

    const QString uniteOperator =
        (queryHasAnyModifier ? QStringLiteral("OR") : QStringLiteral("AND"));

    // 3) Processing notebook modifier (if present)

    const QString notebookName = noteSearchQuery.notebookModifier();
    QString notebookLocalId;
    if (!notebookName.isEmpty()) {
        QSqlQuery query(m_sqlDatabase);
        const QString notebookQueryString =
            QString::fromUtf8(
                "SELECT localUid FROM NotebookFTS WHERE "
                "notebookName MATCH '%1' LIMIT 1")
                .arg(sqlEscapeString(notebookName));

        const bool res = query.exec(notebookQueryString);
        DATABASE_CHECK_AND_SET_ERROR()

        if (Q_UNLIKELY(!query.next())) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("notebook with the provided name was not found"));
            return false;
        }

        const QSqlRecord rec = query.record();
        const int index = rec.indexOf(QStringLiteral("localUid"));
        if (Q_UNLIKELY(index < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(QT_TR_NOOP(
                "can't find notebook's local id by notebook name: "
                "SQL query record doesn't contain the requested item"));
            return false;
        }

        const QVariant value = rec.value(index);
        if (Q_UNLIKELY(value.isNull())) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("found null notebook's local id corresponding to "
                           "notebook's name"));
            return false;
        }

        notebookLocalId = value.toString();
        if (Q_UNLIKELY(notebookLocalId.isEmpty())) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("found empty notebook's local id corresponding to "
                           "notebook's name"));
            return false;
        }
    }

    if (!notebookLocalId.isEmpty()) {
        sql += QStringLiteral("(notebookLocalUid = '");
        sql += sqlEscapeString(notebookLocalId);
        sql += QStringLiteral("') AND ");
    }

    // 4) Processing tag names and negated tag names, if any

    if (noteSearchQuery.hasAnyTag()) {
        sql += QStringLiteral("(NoteTags.localTag IS NOT NULL) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else if (noteSearchQuery.hasNegatedAnyTag()) {
        sql += QStringLiteral("(NoteTags.localTag IS NULL) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else {
        QStringList tagLocalIds;
        QStringList tagNegatedLocalIds;

        const QStringList & tagNames = noteSearchQuery.tagNames();
        if (!tagNames.isEmpty()) {
            ErrorString error;
            if (!tagNamesToTagLocalIds(tagNames, tagLocalIds, error)) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }
        }

        if (!tagLocalIds.isEmpty()) {
            if (!queryHasAnyModifier) {
                /**
                 * In successful note search query there are exactly as many tag
                 * local ids as there are tag names; therefore, when the search
                 * is for notes with some particular tags, we need to ensure
                 * that each note's local id in the sub-query result is present
                 * there exactly as many times as there are tag local ids in
                 * the query which the note is labeled with
                 */

                const int numTagLocalIds = tagLocalIds.size();
                sql += QStringLiteral(
                    "(NoteTags.localNote IN (SELECT localNote "
                    "FROM (SELECT localNote, localTag, COUNT(*) "
                    "FROM NoteTags WHERE NoteTags.localTag IN ('");
                for (const auto & tagLocalId: qAsConst(tagLocalIds)) {
                    sql += sqlEscapeString(tagLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                sql += QStringLiteral(") GROUP BY localNote HAVING COUNT(*)=");
                sql += QString::number(numTagLocalIds);
                sql += QStringLiteral("))) ");
            }
            else {
                /**
                 * With "any:" modifier the search doesn't care about
                 * the exactness of tag-to-note map, it would instead pick just
                 * any note corresponding to any of requested tags at least once
                 */

                sql += QStringLiteral(
                    "(NoteTags.localNote IN (SELECT localNote "
                    "FROM (SELECT localNote, localTag "
                    "FROM NoteTags WHERE NoteTags.localTag IN ('");
                for (const auto & tagLocalId: qAsConst(tagLocalIds)) {
                    sql += sqlEscapeString(tagLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                sql += QStringLiteral(")))) ");
            }

            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }

        const QStringList & negatedTagNames = noteSearchQuery.negatedTagNames();
        if (!negatedTagNames.isEmpty()) {
            ErrorString error;
            if (!tagNamesToTagLocalIds(
                    negatedTagNames, tagNegatedLocalIds, error)) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }
        }

        if (!tagNegatedLocalIds.isEmpty()) {
            if (!queryHasAnyModifier) {
                /**
                 * First find all notes' local ids which actually correspond
                 * to negated tags' local ids; then simply negate that
                 * condition
                 */

                const int numTagNegatedLocalIds = tagNegatedLocalIds.size();
                sql += QStringLiteral(
                    "(NoteTags.localNote NOT IN (SELECT localNote "
                    "FROM (SELECT localNote, localTag, COUNT(*) "
                    "FROM NoteTags WHERE NoteTags.localTag IN ('");
                for (const auto & tagLocalId: qAsConst(tagNegatedLocalIds)) {
                    sql += sqlEscapeString(tagLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                sql += QStringLiteral(") GROUP BY localNote HAVING COUNT(*)=");
                sql += QString::number(numTagNegatedLocalIds);

                // Don't forget to account for the case of no tags used for note
                // so it's not even present in NoteTags table

                sql += QStringLiteral(")) OR (NoteTags.localNote IS NULL)) ");
            }
            else {
                /**
                 * With "any:" modifier the search doesn't care about the
                 * exactness of tag-to-note map, it would instead pick just any
                 * note not from the list of notes corresponding to any of
                 * requested tags at least once
                 */

                sql += QStringLiteral(
                    "(NoteTags.localNote NOT IN (SELECT "
                    "localNote FROM (SELECT localNote, localTag "
                    "FROM NoteTags WHERE NoteTags.localTag IN ('");
                for (const auto & tagLocalId: qAsConst(tagNegatedLocalIds)) {
                    sql += sqlEscapeString(tagLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                // Don't forget to account for the case of no tags used for note
                // so it's not even present in NoteTags table

                sql += QStringLiteral("))) OR (NoteTags.localNote IS NULL)) ");
            }

            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }
    }

    // 5) Processing resource mime types

    if (noteSearchQuery.hasAnyResourceMimeType()) {
        sql += QStringLiteral("(NoteResources.localResource IS NOT NULL) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else if (noteSearchQuery.hasNegatedAnyResourceMimeType()) {
        sql += QStringLiteral("(NoteResources.localResource IS NULL) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else {
        QStringList resourceLocalIdsPerMime;
        QStringList resourceNegatedLocalIdsPerMime;

        const QStringList & resourceMimeTypes =
            noteSearchQuery.resourceMimeTypes();

        const int numResourceMimeTypes = resourceMimeTypes.size();
        if (!resourceMimeTypes.isEmpty()) {
            ErrorString error;
            if (!resourceMimeTypesToResourceLocalIds(
                    resourceMimeTypes, resourceLocalIdsPerMime, error))
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }
        }

        if (!resourceLocalIdsPerMime.isEmpty()) {
            if (!queryHasAnyModifier) {
                /**
                 * Need to find notes which each have all the found resource
                 * local ids. One resource mime type can correspond to multiple
                 * resources. However, one resource corresponds to exactly one
                 * note. When searching for notes which resources have
                 * particular mime type, we need to ensure that each note's
                 * local id in the sub-query result is present there exactly as
                 * many times as there are resource mime types in the query
                 */

                sql += QStringLiteral(
                    "(NoteResources.localNote IN (SELECT "
                    "localNote FROM (SELECT localNote, "
                    "localResource, COUNT(*) "
                    "FROM NoteResources WHERE "
                    "NoteResources.localResource IN ('");

                for (const auto & resourceLocalId:
                     qAsConst(resourceLocalIdsPerMime)) {
                    sql += sqlEscapeString(resourceLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                sql += QStringLiteral(") GROUP BY localNote HAVING COUNT(*)=");
                sql += QString::number(numResourceMimeTypes);
                sql += QStringLiteral("))) ");
            }
            else {
                /**
                 * With "any:" modifier the search doesn't care about the
                 * exactness of resource mime type-to-note map, it would instead
                 * pick just any note having at least one resource with
                 * requested mime type
                 */

                sql += QStringLiteral(
                    "(NoteResources.localNote IN (SELECT "
                    "localNote FROM (SELECT localNote, "
                    "localResource FROM NoteResources WHERE "
                    "NoteResources.localResource IN ('");

                for (const auto & resourceLocalId:
                     qAsConst(resourceLocalIdsPerMime)) {
                    sql += sqlEscapeString(resourceLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                sql += QStringLiteral(")))) ");
            }

            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }

        const auto & negatedResourceMimeTypes =
            noteSearchQuery.negatedResourceMimeTypes();

        const int numNegatedResourceMimeTypes = negatedResourceMimeTypes.size();
        if (!negatedResourceMimeTypes.isEmpty()) {
            ErrorString error;
            if (!resourceMimeTypesToResourceLocalIds(
                    negatedResourceMimeTypes, resourceNegatedLocalIdsPerMime,
                    error))
            {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }
        }

        if (!resourceNegatedLocalIdsPerMime.isEmpty()) {
            if (!queryHasAnyModifier) {
                sql += QStringLiteral(
                    "(NoteResources.localNote NOT IN (SELECT "
                    "localNote FROM (SELECT localNote, "
                    "localResource, COUNT(*) "
                    "FROM NoteResources WHERE "
                    "NoteResources.localResource IN ('");

                for (const auto & resourceLocalId:
                     qAsConst(resourceNegatedLocalIdsPerMime)) {
                    sql += sqlEscapeString(resourceLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                sql += QStringLiteral(") GROUP BY localNote HAVING COUNT(*)=");
                sql += QString::number(numNegatedResourceMimeTypes);

                // Don't forget to account for the case of no resources existing
                // in the note so it's not even present in NoteResources table

                sql +=
                    QStringLiteral(")) OR (NoteResources.localNote IS NULL)) ");
            }
            else {
                sql += QStringLiteral(
                    "(NoteResources.localNote NOT IN (SELECT "
                    "localNote FROM (SELECT localNote, localResource "
                    "FROM NoteResources WHERE "
                    "NoteResources.localResource IN ('");

                for (const auto & resourceLocalId:
                     qAsConst(resourceNegatedLocalIdsPerMime)) {
                    sql += sqlEscapeString(resourceLocalId);
                    sql += QStringLiteral("', '");
                }
                // remove trailing comma, whitespace and quotation mark
                sql.chop(3);

                /**
                 * Don't forget to account for the case of no resources existing
                 * in the note so it's not even present in NoteResources table
                 */

                sql += QStringLiteral(
                    "))) OR (NoteResources.localNote IS NULL)) ");
            }

            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }
    }

    // 6) Processing other better generalizable filters

#define CHECK_AND_PROCESS_ANY_ITEM(hasAnyItem, hasNegatedAnyItem, column)      \
    if (noteSearchQuery.hasAnyItem()) {                                        \
        sql += QStringLiteral("(NoteFTS." #column " IS NOT NULL) ");           \
        sql += uniteOperator;                                                  \
        sql += QStringLiteral(" ");                                            \
    }                                                                          \
    else if (noteSearchQuery.hasNegatedAnyItem()) {                            \
        sql += QStringLiteral("(NoteFTS." #column " IS NULL) ");               \
        sql += uniteOperator;                                                  \
        sql += QStringLiteral(" ");                                            \
    }

#define CHECK_AND_PROCESS_LIST(list, column, negated, ...)                     \
    const auto & noteSearchQuery##list##column = noteSearchQuery.list();       \
    if (!noteSearchQuery##list##column.isEmpty()) {                            \
        sql += QStringLiteral("(");                                            \
        for (const auto & item: noteSearchQuery##list##column) {               \
            if (negated) {                                                     \
                sql += QStringLiteral("(localUid NOT IN ");                    \
            }                                                                  \
            else {                                                             \
                sql += QStringLiteral("(localUid IN ");                        \
            }                                                                  \
            sql += QStringLiteral("(SELECT localUid FROM NoteFTS WHERE ");     \
            sql += QStringLiteral("NoteFTS." #column " MATCH \'");             \
            sql += sqlEscapeString(__VA_ARGS__(item));                         \
            sql += QStringLiteral("\')) ");                                    \
            sql += uniteOperator;                                              \
            sql += QStringLiteral(" ");                                        \
        }                                                                      \
        sql.chop(uniteOperator.length() + 1);                                  \
        sql += QStringLiteral(")");                                            \
        sql += uniteOperator;                                                  \
        sql += QStringLiteral(" ");                                            \
    }

#define CHECK_AND_PROCESS_NUMERIC_LIST(list, column, negated, ...)             \
    const auto & noteSearchQuery##list##column = noteSearchQuery.list();       \
    if (!noteSearchQuery##list##column.isEmpty()) {                            \
        auto it = noteSearchQuery##list##column.constEnd();                    \
        if (queryHasAnyModifier) {                                             \
            if (negated) {                                                     \
                it = std::max_element(                                         \
                    noteSearchQuery##list##column.constBegin(),                \
                    noteSearchQuery##list##column.constEnd());                 \
            }                                                                  \
            else {                                                             \
                it = std::min_element(                                         \
                    noteSearchQuery##list##column.constBegin(),                \
                    noteSearchQuery##list##column.constEnd());                 \
            }                                                                  \
        }                                                                      \
        else {                                                                 \
            if (negated) {                                                     \
                it = std::min_element(                                         \
                    noteSearchQuery##list##column.constBegin(),                \
                    noteSearchQuery##list##column.constEnd());                 \
            }                                                                  \
            else {                                                             \
                it = std::max_element(                                         \
                    noteSearchQuery##list##column.constBegin(),                \
                    noteSearchQuery##list##column.constEnd());                 \
            }                                                                  \
        }                                                                      \
        if (it != noteSearchQuery##list##column.constEnd()) {                  \
            sql += QStringLiteral("(localUid IN (SELECT localUid FROM ");      \
            sql += QStringLiteral("Notes WHERE Notes." #column);               \
            if (negated) {                                                     \
                sql += QStringLiteral(" < ");                                  \
            }                                                                  \
            else {                                                             \
                sql += QStringLiteral(" >= ");                                 \
            }                                                                  \
            sql += sqlEscapeString(__VA_ARGS__(*it));                          \
            sql += QStringLiteral(")) ");                                      \
            sql += uniteOperator;                                              \
            sql += QStringLiteral(" ");                                        \
        }                                                                      \
    }

#define CHECK_AND_PROCESS_ITEM(                                                \
    list, negatedList, hasAnyItem, hasNegatedAnyItem, column, ...)             \
    CHECK_AND_PROCESS_ANY_ITEM(hasAnyItem, hasNegatedAnyItem, column)          \
    else {                                                                     \
        CHECK_AND_PROCESS_LIST(list, column, !negated, __VA_ARGS__);           \
        CHECK_AND_PROCESS_LIST(negatedList, column, negated, __VA_ARGS__);     \
    }

#define CHECK_AND_PROCESS_NUMERIC_ITEM(                                        \
    list, negatedList, hasAnyItem, hasNegatedAnyItem, column, ...)             \
    CHECK_AND_PROCESS_ANY_ITEM(hasAnyItem, hasNegatedAnyItem, column)          \
    else {                                                                     \
        CHECK_AND_PROCESS_NUMERIC_LIST(list, column, !negated, __VA_ARGS__);   \
        CHECK_AND_PROCESS_NUMERIC_LIST(                                        \
            negatedList, column, negated, __VA_ARGS__);                        \
    }

    bool negated = true;
    CHECK_AND_PROCESS_ITEM(
        titleNames, negatedTitleNames, hasAnyTitleName, hasNegatedAnyTitleName,
        title);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        creationTimestamps, negatedCreationTimestamps, hasAnyCreationTimestamp,
        hasNegatedAnyCreationTimestamp, creationTimestamp, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        modificationTimestamps, negatedModificationTimestamps,
        hasAnyModificationTimestamp, hasNegatedAnyModificationTimestamp,
        modificationTimestamp, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        subjectDateTimestamps, negatedSubjectDateTimestamps,
        hasAnySubjectDateTimestamp, hasNegatedAnySubjectDateTimestamp,
        subjectDate, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        latitudes, negatedLatitudes, hasAnyLatitude, hasNegatedAnyLatitude,
        latitude, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        longitudes, negatedLongitudes, hasAnyLongitude, hasNegatedAnyLongitude,
        longitude, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        altitudes, negatedAltitudes, hasAnyAltitude, hasNegatedAnyAltitude,
        altitude, QString::number);

    CHECK_AND_PROCESS_ITEM(
        authors, negatedAuthors, hasAnyAuthor, hasNegatedAnyAuthor, author);

    CHECK_AND_PROCESS_ITEM(
        sources, negatedSources, hasAnySource, hasNegatedAnySource, source);

    CHECK_AND_PROCESS_ITEM(
        sourceApplications, negatedSourceApplications, hasAnySourceApplication,
        hasNegatedAnySourceApplication, sourceApplication);

    CHECK_AND_PROCESS_ITEM(
        contentClasses, negatedContentClasses, hasAnyContentClass,
        hasNegatedAnyContentClass, contentClass);

    CHECK_AND_PROCESS_ITEM(
        placeNames, negatedPlaceNames, hasAnyPlaceName, hasNegatedAnyPlaceName,
        placeName);

    CHECK_AND_PROCESS_ITEM(
        applicationData, negatedApplicationData, hasAnyApplicationData,
        hasNegatedAnyApplicationData, applicationDataKeysOnly);

    CHECK_AND_PROCESS_ITEM(
        applicationData, negatedApplicationData, hasAnyApplicationData,
        hasNegatedAnyApplicationData, applicationDataKeysMap);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        reminderOrders, negatedReminderOrders, hasAnyReminderOrder,
        hasNegatedAnyReminderOrder, reminderOrder, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        reminderTimes, negatedReminderTimes, hasAnyReminderTime,
        hasNegatedAnyReminderTime, reminderTime, QString::number);

    CHECK_AND_PROCESS_NUMERIC_ITEM(
        reminderDoneTimes, negatedReminderDoneTimes, hasAnyReminderDoneTime,
        hasNegatedAnyReminderDoneTime, reminderDoneTime, QString::number);

#undef CHECK_AND_PROCESS_ITEM
#undef CHECK_AND_PROCESS_LIST
#undef CHECK_AND_PROCESS_ANY_ITEM
#undef CHECK_AND_PROCESS_NUMERIC_ITEM

    // 7) Processing ToDo items

    if (noteSearchQuery.hasAnyToDo()) {
        sql += QStringLiteral(
            "((NoteFTS.contentContainsFinishedToDo IS 1) OR "
            "(NoteFTS.contentContainsUnfinishedToDo IS 1)) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else if (noteSearchQuery.hasNegatedAnyToDo()) {
        sql += QStringLiteral(
            "((NoteFTS.contentContainsFinishedToDo IS 0) OR "
            "(NoteFTS.contentContainsFinishedToDo IS NULL)) AND "
            "((NoteFTS.contentContainsUnfinishedToDo IS 0) OR "
            "(NoteFTS.contentContainsUnfinishedToDo IS NULL)) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else {
        if (noteSearchQuery.hasFinishedToDo()) {
            sql +=
                QStringLiteral("(NoteFTS.contentContainsFinishedToDo IS 1) ");
            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }
        else if (noteSearchQuery.hasNegatedFinishedToDo()) {
            sql += QStringLiteral(
                "((NoteFTS.contentContainsFinishedToDo IS 0) OR "
                "(NoteFTS.contentContainsFinishedToDo IS NULL)) ");
            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }

        if (noteSearchQuery.hasUnfinishedToDo()) {
            sql +=
                QStringLiteral("(NoteFTS.contentContainsUnfinishedToDo IS 1) ");
            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }
        else if (noteSearchQuery.hasNegatedUnfinishedToDo()) {
            sql += QStringLiteral(
                "((NoteFTS.contentContainsUnfinishedToDo IS 0) OR "
                "(NoteFTS.contentContainsUnfinishedToDo IS NULL)) ");
            sql += uniteOperator;
            sql += QStringLiteral(" ");
        }
    }

    // 8) Processing encryption item

    if (noteSearchQuery.hasNegatedEncryption()) {
        sql += QStringLiteral(
            "((NoteFTS.contentContainsEncryption IS 0) OR "
            "(NoteFTS.contentContainsEncryption IS NULL)) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }
    else if (noteSearchQuery.hasEncryption()) {
        sql += QStringLiteral("(NoteFTS.contentContainsEncryption IS 1) ");
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }

    // 9) Processing content search terms

    if (noteSearchQuery.hasAnyContentSearchTerms()) {
        ErrorString error;
        QString contentSearchTermsSqlQueryPart;
        if (!noteSearchQueryContentSearchTermsToSQL(
                noteSearchQuery, contentSearchTermsSqlQueryPart, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        sql += contentSearchTermsSqlQueryPart;
        sql += uniteOperator;
        sql += QStringLiteral(" ");
    }

    // 10) Removing trailing unite operator from the SQL string (if any)

    QString spareEnd = uniteOperator + QStringLiteral(" ");
    if (sql.endsWith(spareEnd)) {
        sql.chop(spareEnd.size());
    }

    // 11) See whether we should bother anything regarding tags or resources

    QString sqlPostfix = QStringLiteral("FROM NoteFTS ");
    if (sql.contains(QStringLiteral("NoteTags"))) {
        sqlPrefix += QStringLiteral(", NoteTags.localTag ");
        sqlPostfix += QStringLiteral(
            "LEFT OUTER JOIN NoteTags ON "
            "NoteFTS.localUid = NoteTags.localNote ");
    }

    if (sql.contains(QStringLiteral("NoteResources"))) {
        sqlPrefix += QStringLiteral(", NoteResources.localResource ");
        sqlPostfix += QStringLiteral(
            "LEFT OUTER JOIN NoteResources ON "
            "NoteFTS.localUid = NoteResources.localNote ");
    }

    // 12) Finalize the query composed of parts

    sqlPrefix += sqlPostfix;
    sqlPrefix += QStringLiteral("WHERE ");
    sql.prepend(sqlPrefix);

    QNTRACE("local_storage", "Prepared SQL query for note search: " << sql);
    return true;
}

bool LocalStorageManagerPrivate::noteSearchQueryContentSearchTermsToSQL(
    const NoteSearchQuery & noteSearchQuery, QString & sql,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::noteSearchQueryContentSearchTermsToSQL");

    if (!noteSearchQuery.hasAnyContentSearchTerms()) {
        errorDescription.setBase(
            QT_TR_NOOP("note search query has no advanced search "
                       "modifiers and no content search terms"));
        errorDescription.details() = noteSearchQuery.queryString();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    sql.resize(0);

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();
    QString uniteOperator =
        (queryHasAnyModifier ? QStringLiteral("OR") : QStringLiteral("AND"));

    QString positiveSqlPart;
    QString negatedSqlPart;

    QString matchStatement;
    matchStatement.reserve(5);

    QString frontSearchTermModifier;
    frontSearchTermModifier.reserve(1);

    QString backSearchTermModifier;
    backSearchTermModifier.reserve(1);

    QString currentSearchTerm;

    const QStringList & contentSearchTerms =
        noteSearchQuery.contentSearchTerms();

    if (!contentSearchTerms.isEmpty()) {
        const int numContentSearchTerms = contentSearchTerms.size();
        for (int i = 0; i < numContentSearchTerms; ++i) {
            currentSearchTerm = contentSearchTerms[i];
            m_stringUtils.removePunctuation(
                currentSearchTerm, m_preservedAsterisk);
            if (currentSearchTerm.isEmpty()) {
                continue;
            }

            m_stringUtils.removeDiacritics(currentSearchTerm);

            positiveSqlPart += QStringLiteral("(");

            contentSearchTermToSQLQueryPart(
                frontSearchTermModifier, currentSearchTerm,
                backSearchTermModifier, matchStatement);

            currentSearchTerm = sqlEscapeString(currentSearchTerm);

            positiveSqlPart +=
                QString::fromUtf8(
                    "(localUid IN (SELECT localUid FROM NoteFTS "
                    "WHERE contentListOfWords %1 '%2%3%4')) OR "
                    "(localUid IN (SELECT localUid FROM NoteFTS "
                    "WHERE titleNormalized %1 '%2%3%4')) OR "
                    "(localUid IN (SELECT noteLocalUid FROM "
                    "ResourceRecognitionDataFTS WHERE "
                    "recognitionData %1 '%2%3%4')) OR "
                    "(localUid IN (SELECT localNote FROM "
                    "NoteTags LEFT OUTER JOIN TagFTS ON "
                    "NoteTags.localTag=TagFTS.localUid WHERE "
                    "(nameLower IN (SELECT nameLower FROM TagFTS "
                    "WHERE nameLower %1 '%2%3%4'))))")
                    .arg(
                        matchStatement, frontSearchTermModifier,
                        currentSearchTerm, backSearchTermModifier);

            positiveSqlPart += QStringLiteral(")");

            if (i != (numContentSearchTerms - 1)) {
                positiveSqlPart +=
                    QStringLiteral(" ") + uniteOperator + QStringLiteral(" ");
            }
        }

        if (!positiveSqlPart.isEmpty()) {
            positiveSqlPart.prepend(QStringLiteral("("));
            positiveSqlPart += QStringLiteral(")");
        }
    }

    const auto & negatedContentSearchTerms =
        noteSearchQuery.negatedContentSearchTerms();

    if (!negatedContentSearchTerms.isEmpty()) {
        const int numNegatedContentSearchTerms =
            negatedContentSearchTerms.size();

        for (int i = 0; i < numNegatedContentSearchTerms; ++i) {
            currentSearchTerm = negatedContentSearchTerms[i];

            m_stringUtils.removePunctuation(
                currentSearchTerm, m_preservedAsterisk);

            if (currentSearchTerm.isEmpty()) {
                continue;
            }

            m_stringUtils.removeDiacritics(currentSearchTerm);

            negatedSqlPart += QStringLiteral("(");

            contentSearchTermToSQLQueryPart(
                frontSearchTermModifier, currentSearchTerm,
                backSearchTermModifier, matchStatement);

            currentSearchTerm = sqlEscapeString(currentSearchTerm);

            negatedSqlPart +=
                QString::fromUtf8(
                    "(localUid NOT IN (SELECT localUid FROM "
                    "NoteFTS WHERE contentListOfWords %1 '%2%3%4')) AND "
                    "(localUid NOT IN (SELECT localUid FROM "
                    "NoteFTS WHERE titleNormalized %1 '%2%3%4')) AND "
                    "(localUid NOT IN (SELECT noteLocalUid FROM "
                    "ResourceRecognitionDataFTS WHERE "
                    "recognitionData %1 '%2%3%4')) AND "
                    "(localUid NOT IN (SELECT localNote FROM "
                    "NoteTags LEFT OUTER JOIN TagFTS ON "
                    "NoteTags.localTag=TagFTS.localUid WHERE "
                    "(nameLower IN (SELECT nameLower FROM TagFTS "
                    "WHERE nameLower %1 '%2%3%4'))))")
                    .arg(
                        matchStatement, frontSearchTermModifier,
                        currentSearchTerm, backSearchTermModifier);

            negatedSqlPart += QStringLiteral(")");

            if (i != (numNegatedContentSearchTerms - 1)) {
                negatedSqlPart +=
                    QStringLiteral(" ") + uniteOperator + QStringLiteral(" ");
            }
        }

        if (!negatedSqlPart.isEmpty()) {
            negatedSqlPart.prepend(QStringLiteral("("));
            negatedSqlPart += QStringLiteral(")");
        }
    }

    // First append all positive terms of the query
    if (!positiveSqlPart.isEmpty()) {
        sql += QStringLiteral("(") + positiveSqlPart + QStringLiteral(") ");
    }

    // Now append all negative parts of the query (if any)

    if (!negatedSqlPart.isEmpty()) {
        if (!positiveSqlPart.isEmpty()) {
            sql += QStringLiteral(" ") + uniteOperator + QStringLiteral(" ");
        }

        sql += QStringLiteral("(") + negatedSqlPart + QStringLiteral(")");
    }

    return true;
}

void LocalStorageManagerPrivate::contentSearchTermToSQLQueryPart(
    QString & frontSearchTermModifier, QString & searchTerm,
    QString & backSearchTermModifier, QString & matchStatement) const
{
    QRegExp whitespaceRegex(QStringLiteral("\\p{Z}"));
    QString asterisk = QStringLiteral("*");

    if ((whitespaceRegex.indexIn(searchTerm) >= 0) ||
        (searchTerm.contains(asterisk) && !searchTerm.endsWith(asterisk)))
    {
        // FTS "MATCH" clause doesn't work for phrased search or search with
        // asterisk somewhere but the end of the search term,
        // need to use the slow "LIKE" clause instead
        matchStatement = QStringLiteral("LIKE");

        while (searchTerm.startsWith(asterisk)) {
            searchTerm.remove(0, 1);
        }

        while (searchTerm.endsWith(asterisk)) {
            searchTerm.chop(1);
        }

        frontSearchTermModifier = QStringLiteral("%");
        backSearchTermModifier = QStringLiteral("%");

        int pos = -1;
        while ((pos = searchTerm.indexOf(asterisk)) >= 0) {
            searchTerm.replace(pos, 1, QStringLiteral("%"));
        }
    }
    else {
        matchStatement = QStringLiteral("MATCH");
        frontSearchTermModifier = QLatin1String("");
        backSearchTermModifier = QLatin1String("");
    }
}

bool LocalStorageManagerPrivate::tagNamesToTagLocalIds(
    const QStringList & tagNames, QStringList & tagLocalIds,
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get tag local ids for tag names"));

    tagLocalIds.clear();

    QSqlQuery query(m_sqlDatabase);
    QString queryString;

    const bool singleTagName = (tagNames.size() == 1);
    if (singleTagName) {
        const bool res = query.prepare(QStringLiteral(
            "SELECT localUid FROM TagFTS WHERE nameLower MATCH :names"));
        DATABASE_CHECK_AND_SET_ERROR()

        QString names = tagNames.at(0).toLower();
        names.prepend(QStringLiteral("\'"));
        names.append(QStringLiteral("\'"));
        query.bindValue(QStringLiteral(":names"), names);
    }
    else {
        bool someTagNameHasWhitespace = false;
        for (const auto & tagName: tagNames) {
            if (tagName.contains(QStringLiteral(" "))) {
                someTagNameHasWhitespace = true;
                break;
            }
        }

        if (someTagNameHasWhitespace) {
            /**
             * Unfortunately, stardard SQLite at least from Qt 4.x has standard
             * query syntax for FTS which does not support whitespaces in search
             * terms and therefore MATCH function is simply inapplicable here,
             * have to use brute-force "equal to X1 or equal to X2 or ...
             * equal to XN"
             */
            queryString = QStringLiteral("SELECT localUid FROM Tags WHERE ");

            for (const auto & tagName: tagNames) {
                queryString += QStringLiteral("(nameLower = \'");
                queryString += sqlEscapeString(tagName.toLower());
                queryString += QStringLiteral("\') OR ");
            }
            queryString.chop(4); // remove trailing " OR "
        }
        else {
            queryString = QStringLiteral("SELECT localUid FROM TagFTS WHERE ");

            for (const auto & tagName: tagNames) {
                queryString += QStringLiteral(
                    "(localUid IN (SELECT localUid FROM "
                    "TagFTS WHERE nameLower MATCH \'");
                queryString += sqlEscapeString(tagName.toLower());
                queryString += QStringLiteral("\')) OR ");
            }
            queryString.chop(4); // remove trailing " OR "
        }
    }

    bool res = false;
    if (queryString.isEmpty()) {
        res = query.exec();
    }
    else {
        res = query.exec(queryString);
    }
    DATABASE_CHECK_AND_SET_ERROR()

    while (query.next()) {
        QSqlRecord rec = query.record();
        const int index = rec.indexOf(QStringLiteral("localUid"));
        if (Q_UNLIKELY(index < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("tag's local id is not present in the result of "
                           "SQL query"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        tagLocalIds << rec.value(index).toString();
    }

    return true;
}

bool LocalStorageManagerPrivate::resourceMimeTypesToResourceLocalIds(
    const QStringList & resourceMimeTypes, QStringList & resourceLocalIds,
    ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't get resource mime types for resource local ids"));

    resourceLocalIds.clear();

    QSqlQuery query(m_sqlDatabase);
    QString queryString;

    const bool singleMimeType = (resourceMimeTypes.size() == 1);
    if (singleMimeType) {
        bool res = query.prepare(
            QStringLiteral("SELECT resourceLocalUid FROM ResourceMimeFTS "
                           "WHERE mime MATCH :mimeTypes"));
        DATABASE_CHECK_AND_SET_ERROR()

        QString mimeTypes = resourceMimeTypes.at(0);
        mimeTypes.prepend(QStringLiteral("\'"));
        mimeTypes.append(QStringLiteral("\'"));
        query.bindValue(QStringLiteral(":mimeTypes"), mimeTypes);
    }
    else {
        bool someMimeTypeHasWhitespace = false;
        for (const auto & mimeType: resourceMimeTypes) {
            if (mimeType.contains(QStringLiteral(" "))) {
                someMimeTypeHasWhitespace = true;
                break;
            }
        }

        if (someMimeTypeHasWhitespace) {
            /**
             * Unfortunately, stardard SQLite at least from Qt 4.x has standard
             * query syntax for FTS which does not support whitespaces in search
             * terms and therefore MATCH function is simply inapplicable here,
             * have to use brute-force "equal to X1 or equal to X2 or ... equal
             * to XN
             */

            queryString =
                QStringLiteral("SELECT resourceLocalUid FROM Resources WHERE ");

            for (const auto & mimeType: resourceMimeTypes) {
                queryString += QStringLiteral("(mime = \'");
                queryString += sqlEscapeString(mimeType);
                queryString += QStringLiteral("\') OR ");
            }
            queryString.chop(4); // remove trailing OR and two whitespaces
        }
        else {
            // For some reason statements like "MATCH 'x OR y'" don't work while
            // "SELECT ... MATCH 'x' UNION SELECT ... MATCH 'y'" work.

            for (const auto & mimeType: resourceMimeTypes) {
                queryString += QStringLiteral(
                    "SELECT resourceLocalUid FROM "
                    "ResourceMimeFTS WHERE mime MATCH \'");
                queryString += sqlEscapeString(mimeType);
                queryString += QStringLiteral("\' UNION ");
            }
            queryString.chop(7); // remove trailing characters
        }
    }

    bool res = false;
    if (queryString.isEmpty()) {
        res = query.exec();
    }
    else {
        res = query.exec(queryString);
    }
    DATABASE_CHECK_AND_SET_ERROR()

    while (query.next()) {
        QSqlRecord rec = query.record();
        const int index = rec.indexOf(QStringLiteral("resourceLocalUid"));
        if (Q_UNLIKELY(index < 0)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("resource's local id is not present in the result "
                           "of SQL query"));
            return false;
        }

        resourceLocalIds << rec.value(index).toString();
    }

    return true;
}

bool LocalStorageManagerPrivate::complementResourceNoteIds(
    qevercloud::Resource & resource, ErrorString & errorDescription) const
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't complement resource note ids"));

    if (!resource.noteGuid()) {
        const QString noteLocalId = sqlEscapeString(resource.noteLocalId());

        const QString queryString =
            QString::fromUtf8("SELECT guid FROM Notes WHERE localUid = '%1'")
                .arg(noteLocalId);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()

        if (query.next()) {
            resource.setNoteGuid(
                query.record().value(QStringLiteral("guid")).toString());
        }
    }
    else if (resource.noteLocalId().isEmpty()) {
        const QString noteGuid = sqlEscapeString(*resource.noteGuid());

        const QString queryString =
            QString::fromUtf8("SELECT localUid FROM Notes WHERE guid = '%1'")
                .arg(noteGuid);

        QSqlQuery query(m_sqlDatabase);
        const bool res = query.exec(queryString);
        DATABASE_CHECK_AND_SET_ERROR()

        if (query.next()) {
            resource.setNoteLocalId(
                query.record().value(QStringLiteral("localUid")).toString());
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::partialUpdateNoteResources(
    const QString & noteLocalId,
    const QList<qevercloud::Resource> & updatedNoteResources,
    const bool updateResourceBinaryData, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage",
        "LocalStorageManagerPrivate::partialUpdateNoteResources: "
        "note local id = "
            << noteLocalId << ", update resource binary data = "
            << (updateResourceBinaryData ? "true" : "false"));

    const ErrorString errorPrefix(
        QT_TR_NOOP("can't do the partial update of note's resources"));

    if (!checkDuplicatesByLocalId(updatedNoteResources)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("the list of note's resources contains resources with "
                       "the same local id"));
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    ErrorString error;
    auto previousNoteResources = listResourcesPerNoteLocalId(
        noteLocalId, /* with binary data = */ false, error);
    if (!error.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    if (compareResourcesListsWithoutBinaryData(
            previousNoteResources, updatedNoteResources))

    {
        QNDEBUG(
            "local_storage",
            "The list of resources for the note did not change");
        return true;
    }

    // Something has changed in the list of note's resources, let's figure out
    // what exactly. Compose three lists:
    // 1. Local ids of resources which no longer exist in the updated list
    // 2. Newly added resources for this note
    // 3. Resources which were somehow updated from the previous version

    QSet<QString> localIdsOfResourcesToRemove;
    QList<qevercloud::Resource> addedResources;
    QList<qevercloud::Resource> updatedResources;
    classifyNoteResources(
        previousNoteResources, updatedNoteResources,
        localIdsOfResourcesToRemove, addedResources, updatedResources);

    QNDEBUG(
        "local_storage",
        "Partial update note resources: "
            << localIdsOfResourcesToRemove.size() << " resources to remove, "
            << addedResources.size() << " resources to add, "
            << updatedResources.size() << " resources to update, "
            << previousNoteResources.size() << " previous note resources, "
            << updatedNoteResources.size() << " resources passed to the "
            << "classification");

    if (localIdsOfResourcesToRemove.isEmpty() && addedResources.isEmpty() &&
        updatedResources.isEmpty())
    {
        // The list of resources is essentially the same, only indexes of some
        // resources have changed, need to detect and update them
        Q_ASSERT(previousNoteResources.size() == updatedNoteResources.size());
        QList<std::pair<QString, int>> localIdsAndIndexesInNoteToUpdate;
        for (int i = 0; i < previousNoteResources.size(); ++i)
        {
            const auto & previousResource = qAsConst(previousNoteResources)[i];
            const auto & updatedResource = qAsConst(updatedNoteResources)[i];
            if (previousResource.localId() != updatedResource.localId()) {
                localIdsAndIndexesInNoteToUpdate
                    << std::make_pair(updatedResource.localId(), i);
            }
        }

        Q_ASSERT(!localIdsAndIndexesInNoteToUpdate.isEmpty());

        ErrorString error;
        bool res = updateResourceIndexesInNote(
            localIdsAndIndexesInNoteToUpdate, error);
        if (!res) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        return true;
    }

    for (const auto & resource: qAsConst(addedResources))
    {
        ErrorString error;
        if (!checkResource(resource, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("detected attempt to add invalid resource to "
                           "the local storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage",
                errorDescription << ", resource: " << resource);
            return false;
        }
    }

    for (const auto & resource: qAsConst(updatedResources))
    {
        ErrorString error;
        if (!checkResource(resource, error)) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("detected invalid resource on attempt to update "
                           "resource to the local storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage",
                errorDescription << ", resource: " << resource);
            return false;
        }
    }

    auto remainingResources = previousNoteResources;
    if (!localIdsOfResourcesToRemove.isEmpty())
    {
        remainingResources.erase(
            std::remove_if(
                remainingResources.begin(),
                remainingResources.end(),
                [&](const qevercloud::Resource & resource)
                {
                    return localIdsOfResourcesToRemove.contains(
                        resource.localId());
                }),
            remainingResources.end());

        ErrorString error;
        if (!expungeResources(
                localIdsOfResourcesToRemove, noteLocalId, error))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        // See whether indexes of remaining resources need to be updated
        int firstChangedIndex = -1;
        for (int i = 0; i < remainingResources.size(); ++i)
        {
            const auto & remainingResource =
                qAsConst(remainingResources)[i];

            const auto & previousResource =
                qAsConst(previousNoteResources)[i];

            if (remainingResource.localId() != previousResource.localId()) {
                firstChangedIndex = i;
                break;
            }
        }

        if (firstChangedIndex >= 0)
        {
            QList<std::pair<QString, int>> localIdsAndIndexesInNoteToUpdate;
            for (int i = firstChangedIndex; i < remainingResources.size(); ++i)
            {
                const auto & remainingResource =
                    qAsConst(remainingResources)[i];

                localIdsAndIndexesInNoteToUpdate
                    << std::make_pair(remainingResource.localId(), i);
            }

            ErrorString error;
            bool res = updateResourceIndexesInNote(
                localIdsAndIndexesInNoteToUpdate, error);
            if (!res) {
                errorDescription.base() = errorPrefix.base();
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }
        }

        if (addedResources.isEmpty() && updatedResources.isEmpty()) {
            return true;
        }
    }

    for (const auto & resource: qAsConst(updatedResources))
    {
        const auto remainingResourceIt = std::find_if(
            remainingResources.constBegin(),
            remainingResources.constEnd(),
            [&resource](const qevercloud::Resource & remainingResource)
            {
                return remainingResource.localId() == resource.localId();
            });

        if (remainingResourceIt == remainingResources.constEnd()) {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("failed to update resource in the local storage: "
                           "internal error, updated resource's index in note "
                           "was not found"));
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage",
                errorDescription << ", resource: " << resource);
            return false;
        }

        const int indexInNote = static_cast<int>(std::distance(
            remainingResources.constBegin(), remainingResourceIt));

        error.clear();
        if (!insertOrReplaceResource(
                resource, indexInNote, error, updateResourceBinaryData,
                /* useSeparateTransaction = */ false))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't update one of note's resources"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage",
                errorDescription << ", resource: " << resource);
            return false;
        }
    }

    int counter = 0;
    for (const auto & resource: qAsConst(addedResources))
    {
        ErrorString error;
        const int indexInNote = remainingResources.size() + counter;
        if (!insertOrReplaceResource(
                resource, indexInNote, error, updateResourceBinaryData,
                /* useSeparateTransaction = */ false))
        {
            errorDescription.base() = errorPrefix.base();
            errorDescription.appendBase(
                QT_TR_NOOP("can't add resource to note"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage",
                errorDescription << ", resource: " << resource);
            return false;
        }

        ++counter;
    }

    return true;
}

void LocalStorageManagerPrivate::classifyNoteResources(
    const QList<qevercloud::Resource> & previousNoteResources,
    const QList<qevercloud::Resource> & updatedNoteResources,
    QSet<QString> & localIdsOfRemovedResources,
    QList<qevercloud::Resource> & addedResources,
    QList<qevercloud::Resource> & updatedResources) const
{
    for (const auto & previousNoteResource: qAsConst(previousNoteResources))
    {
        const auto updatedResourceIt = std::find_if(
            updatedNoteResources.constBegin(),
            updatedNoteResources.constEnd(),
            [&previousNoteResource]
            (const qevercloud::Resource & updatedNoteResource)
            {
                return previousNoteResource.localId() ==
                    updatedNoteResource.localId();
            });
        if (updatedResourceIt == updatedNoteResources.constEnd()) {
            Q_UNUSED(localIdsOfRemovedResources.insert(
                previousNoteResource.localId()))
            continue;
        }

        const auto & updatedResource = *updatedResourceIt;
        if (!compareResourcesWithoutBinaryData(
                previousNoteResource, updatedResource))
        {
            updatedResources << updatedResource;
        }
    }

    for (const auto & updatedResource: qAsConst(updatedNoteResources))
    {
        const auto previousResourceIt = std::find_if(
            previousNoteResources.constBegin(),
            previousNoteResources.constEnd(),
            [&updatedResource](const qevercloud::Resource & resource)
            {
                return resource.localId() == updatedResource.localId();
            });
        if (previousResourceIt == previousNoteResources.constEnd()) {
            addedResources << updatedResource;
        }
    }
}

bool LocalStorageManagerPrivate::expungeResources(
    const QSet<QString> & localIds, const QString & noteLocalId,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't expunge resources from the local storage database"));

    QString removeResourcesQueryString =
        QStringLiteral("DELETE FROM Resources WHERE resourceLocalUid IN (");

    static const QChar apostrophe = QChar::fromLatin1('\'');
    static const QChar comma = QChar::fromLatin1(',');
    for (const auto & localId: localIds)
    {
        removeResourcesQueryString += apostrophe;
        removeResourcesQueryString += sqlEscapeString(localId);
        removeResourcesQueryString += apostrophe;
        removeResourcesQueryString += comma;
    }

    removeResourcesQueryString.chop(1); // remove trailing comma
    removeResourcesQueryString += QChar::fromLatin1(')');

    QSqlQuery query(m_sqlDatabase);
    bool res = query.exec(removeResourcesQueryString);
    DATABASE_CHECK_AND_SET_ERROR()

    for (const auto & localId: qAsConst(localIds)) {
        qevercloud::Resource resource;
        resource.setLocalId(localId);
        resource.setNoteLocalId(noteLocalId);

        ErrorString error;
        if (!removeResourceDataFiles(resource, error)) {
            errorDescription = errorPrefix;
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            return false;
        }
    }

    return true;
}

bool LocalStorageManagerPrivate::updateResourceIndexesInNote(
    const QList<std::pair<QString, int>> & resourceLocalIdsWithIndexesInNote,
    ErrorString & errorDescription)
{
    const ErrorString errorPrefix(
        QT_TR_NOOP("can't update index in note for resource"));

    QSqlQuery query(m_sqlDatabase);
    bool res = query.prepare(
        QStringLiteral(
            "UPDATE Resources SET indexInNote = :indexInNote "
            "WHERE resourceLocalUid = :resourceLocalUid"));
    if (!res) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(
            QT_TR_NOOP("can't prepare SQL query to update resource "
                       "indexes in note"));
        errorDescription.details() = query.lastError().text();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    for (const auto & pair: resourceLocalIdsWithIndexesInNote)
    {
        query.bindValue(QStringLiteral(":resourceLocalUid"), pair.first);
        query.bindValue(QStringLiteral(":indexInNote"), pair.second);

        res = query.exec();
        DATABASE_CHECK_AND_SET_ERROR();
    }

    return true;
}

void LocalStorageManagerPrivate::clearDatabaseFile()
{
    QFile databaseFile(m_databaseFilePath);
    if (!databaseFile.open(QIODevice::ReadWrite)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't open the local storage database file for both "
                       "reading and writing"));
        errorDescription.details() = databaseFile.errorString();
        throw DatabaseOpeningException(errorDescription);
    }

    databaseFile.resize(0);
    databaseFile.flush();
}

void LocalStorageManagerPrivate::clearCachedQueries()
{
    QNDEBUG("local_storage", "LocalStorageManagerPrivate::clearCachedQueries");

    m_insertOrReplaceSavedSearchQuery = QSqlQuery();
    m_insertOrReplaceSavedSearchQueryPrepared = false;

    m_getSavedSearchCountQuery = QSqlQuery();
    m_getSavedSearchCountQueryPrepared = false;

    m_insertOrReplaceResourceMetadataWithDataPropertiesQuery = QSqlQuery();
    m_insertOrReplaceResourceMetadataWithDataPropertiesQueryPrepared = false;

    m_updateResourceMetadataWithoutDataPropertiesQuery = QSqlQuery();
    m_updateResourceMetadataWithoutDataPropertiesQueryPrepared = false;

    m_insertOrReplaceNoteResourceQuery = QSqlQuery();
    m_insertOrReplaceNoteResourceQueryPrepared = false;

    m_deleteResourceFromResourceRecognitionTypesQuery = QSqlQuery();
    m_deleteResourceFromResourceRecognitionTypesQueryPrepared = false;

    m_insertOrReplaceIntoResourceRecognitionDataQuery = QSqlQuery();
    m_insertOrReplaceIntoResourceRecognitionDataQueryPrepared = false;

    m_deleteResourceFromResourceAttributesQuery = QSqlQuery();
    m_deleteResourceFromResourceAttributesQueryPrepared = false;

    m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQuery =
        QSqlQuery();
    m_deleteResourceFromResourceAttributesApplicationDataKeysOnlyQueryPrepared =
        false;

    m_deleteResourceFromResourceAttributesApplicationDataFullMapQuery =
        QSqlQuery();
    m_deleteResourceFromResourceAttributesApplicationDataFullMapQueryPrepared =
        false;

    m_insertOrReplaceResourceAttributesQuery = QSqlQuery();
    m_insertOrReplaceResourceAttributesQueryPrepared = false;

    m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQuery =
        QSqlQuery();
    m_insertOrReplaceResourceAttributeApplicationDataKeysOnlyQueryPrepared =
        false;

    m_insertOrReplaceResourceAttributeApplicationDataFullMapQuery = QSqlQuery();
    m_insertOrReplaceResourceAttributeApplicationDataFullMapQueryPrepared =
        false;

    m_getResourceCountQuery = QSqlQuery();
    m_getResourceCountQueryPrepared = false;

    m_getTagCountQuery = QSqlQuery();
    m_getTagCountQueryPrepared = false;

    m_insertOrReplaceTagQuery = QSqlQuery();
    m_insertOrReplaceTagQueryPrepared = false;

    m_insertOrReplaceNoteQuery = QSqlQuery();
    m_insertOrReplaceNoteQueryPrepared = false;

    m_insertOrReplaceSharedNoteQuery = QSqlQuery();
    m_insertOrReplaceSharedNoteQueryPrepared = false;

    m_insertOrReplaceNoteRestrictionsQuery = QSqlQuery();
    m_insertOrReplaceNoteRestrictionsQueryPrepared = false;

    m_insertOrReplaceNoteLimitsQuery = QSqlQuery();
    m_insertOrReplaceNoteLimitsQueryPrepared = false;

    m_canAddNoteToNotebookQuery = QSqlQuery();
    m_canAddNoteToNotebookQueryPrepared = false;

    m_canUpdateNoteInNotebookQuery = QSqlQuery();
    m_canUpdateNoteInNotebookQueryPrepared = false;

    m_canExpungeNoteInNotebookQuery = QSqlQuery();
    m_canExpungeNoteInNotebookQueryPrepared = false;

    m_insertOrReplaceNoteIntoNoteTagsQuery = QSqlQuery();
    m_insertOrReplaceNoteIntoNoteTagsQueryPrepared = false;

    m_getLinkedNotebookCountQuery = QSqlQuery();
    m_getLinkedNotebookCountQueryPrepared = false;

    m_insertOrReplaceLinkedNotebookQuery = QSqlQuery();
    m_insertOrReplaceLinkedNotebookQueryPrepared = false;

    m_getNotebookCountQuery = QSqlQuery();
    m_getNotebookCountQueryPrepared = false;

    m_insertOrReplaceNotebookQuery = QSqlQuery();
    m_insertOrReplaceNotebookQueryPrepared = false;

    m_insertOrReplaceNotebookRestrictionsQuery = QSqlQuery();
    m_insertOrReplaceNotebookRestrictionsQueryPrepared = false;

    m_insertOrReplaceSharedNotebookQuery = QSqlQuery();
    m_insertOrReplaceSharedNotebookQueryPrepared = false;

    m_getUserCountQuery = QSqlQuery();
    m_getUserCountQueryPrepared = false;

    m_insertOrReplaceUserQuery = QSqlQuery();
    m_insertOrReplaceUserQueryPrepared = false;

    m_insertOrReplaceUserAttributesQuery = QSqlQuery();
    m_insertOrReplaceUserAttributesQueryPrepared = false;

    m_insertOrReplaceAccountingQuery = QSqlQuery();
    m_insertOrReplaceAccountingQueryPrepared = false;

    m_insertOrReplaceAccountLimitsQuery = QSqlQuery();
    m_insertOrReplaceAccountLimitsQueryPrepared = false;

    m_insertOrReplaceBusinessUserInfoQuery = QSqlQuery();
    m_insertOrReplaceBusinessUserInfoQueryPrepared = false;

    m_insertOrReplaceUserAttributesViewedPromotionsQuery = QSqlQuery();
    m_insertOrReplaceUserAttributesViewedPromotionsQueryPrepared = false;

    m_insertOrReplaceUserAttributesRecentMailedAddressesQuery = QSqlQuery();
    m_insertOrReplaceUserAttributesRecentMailedAddressesQueryPrepared = false;

    m_deleteUserQuery = QSqlQuery();
    m_deleteUserQueryPrepared = false;
}

template <class T>
QString LocalStorageManagerPrivate::listObjectsOptionsToSqlQueryConditions(
    const ListObjectsOptions & options, ErrorString & errorDescription) const
{
    QString result;
    errorDescription.clear();

    using ListObjectsOption = ListObjectsOption;

    bool listAll = options.testFlag(ListObjectsOption::ListAll);

    bool listDirty = options.testFlag(ListObjectsOption::ListDirty);
    bool listNonDirty = options.testFlag(ListObjectsOption::ListNonDirty);

    bool listElementsWithoutGuid =
        options.testFlag(ListObjectsOption::ListElementsWithoutGuid);

    bool listElementsWithGuid =
        options.testFlag(ListObjectsOption::ListElementsWithGuid);

    bool listLocal = options.testFlag(ListObjectsOption::ListLocal);
    bool listNonLocal = options.testFlag(ListObjectsOption::ListNonLocal);

    bool listFavoritedElements =
        options.testFlag(ListObjectsOption::ListFavoritedElements);

    bool listNonFavoritedElements =
        options.testFlag(ListObjectsOption::ListNonFavoritedElements);

    if (!listAll && !listDirty && !listNonDirty && !listElementsWithoutGuid &&
        !listElementsWithGuid && !listLocal && !listNonLocal &&
        !listFavoritedElements && !listNonFavoritedElements)
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "LocalStorageManagerPrivate",
            "Can't list objects by filter: "
            "detected incorrect filter flag"));
        errorDescription.details() = QString::number(static_cast<int>(options));
        return result;
    }

    if (!(listDirty && listNonDirty)) {
        if (listDirty) {
            result += QStringLiteral("(isDirty=1) AND ");
        }

        if (listNonDirty) {
            result += QStringLiteral("(isDirty=0) AND ");
        }
    }

    if (!(listElementsWithoutGuid && listElementsWithGuid)) {
        if (listElementsWithoutGuid) {
            result += QStringLiteral("(guid IS NULL) AND ");
        }

        if (listElementsWithGuid) {
            result += QStringLiteral("(guid IS NOT NULL) AND ");
        }
    }

    if (!(listLocal && listNonLocal)) {
        if (listLocal) {
            result += QStringLiteral("(isLocal=1) AND ");
        }

        if (listNonLocal) {
            result += QStringLiteral("(isLocal=0) AND ");
        }
    }

    if (!(listFavoritedElements && listNonFavoritedElements)) {
        if (listFavoritedElements) {
            result += QStringLiteral("(isFavorited=1) AND ");
        }

        if (listNonFavoritedElements) {
            result += QStringLiteral("(isFavorited=0) AND ");
        }
    }

    return result;
}

template <>
QString LocalStorageManagerPrivate::listObjectsOptionsToSqlQueryConditions<
    qevercloud::LinkedNotebook>(
    const ListObjectsOptions & flag, ErrorString & errorDescription) const
{
    QString result;
    errorDescription.clear();

    using ListObjectsOption = ListObjectsOption;

    bool listAll = flag.testFlag(ListObjectsOption::ListAll);
    bool listDirty = flag.testFlag(ListObjectsOption::ListDirty);
    bool listNonDirty = flag.testFlag(ListObjectsOption::ListNonDirty);

    if (!listAll && !listDirty && !listNonDirty) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "LocalStorageManagerPrivate",
            "Can't list linked notebooks "
            "by filter: detected incorrect "
            "filter flag"));
        errorDescription.details() = QString::number(static_cast<int>(flag));
        return result;
    }

    if (!(listDirty && listNonDirty)) {
        if (listDirty) {
            result += QStringLiteral("(isDirty=1)");
        }

        if (listNonDirty) {
            result += QStringLiteral("(isDirty=0)");
        }
    }

    return result;
}

template <>
QString LocalStorageManagerPrivate::listObjectsGenericSqlQuery<
    qevercloud::SavedSearch>() const
{
    QString result = QStringLiteral("SELECT * FROM SavedSearches");
    return result;
}

template <>
QString
LocalStorageManagerPrivate::listObjectsGenericSqlQuery<qevercloud::Tag>() const
{
    QString result = QStringLiteral("SELECT * FROM Tags");
    return result;
}

template <>
QString LocalStorageManagerPrivate::listObjectsGenericSqlQuery<
    std::pair<qevercloud::Tag, QStringList>>() const
{
    QString result = QStringLiteral("SELECT * FROM Tags");
    return result;
}

template <>
QString LocalStorageManagerPrivate::listObjectsGenericSqlQuery<
    qevercloud::LinkedNotebook>() const
{
    QString result = QStringLiteral("SELECT * FROM LinkedNotebooks");
    return result;
}

template <>
QString LocalStorageManagerPrivate::listObjectsGenericSqlQuery<
    qevercloud::Notebook>() const
{
    QString result = QStringLiteral(
        "SELECT * FROM Notebooks LEFT OUTER JOIN NotebookRestrictions "
        "ON Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN SharedNotebooks ON ((Notebooks.guid IS NOT NULL) "
        "AND (Notebooks.guid = SharedNotebooks.sharedNotebookNotebookGuid)) "
        "LEFT OUTER JOIN Users ON Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id");
    return result;
}

template <>
QString
LocalStorageManagerPrivate::listObjectsGenericSqlQuery<qevercloud::Note>() const
{
    QString result = QStringLiteral(
        "SELECT * FROM Notes "
        "LEFT OUTER JOIN NoteRestrictions ON "
        "Notes.localUid = NoteRestrictions.noteLocalUid "
        "LEFT OUTER JOIN NoteLimits ON "
        "Notes.localUid = NoteLimits.noteLocalUid");
    return result;
}

template <>
QString LocalStorageManagerPrivate::orderByToSqlTableColumn<ListNotesOrder>(
    const ListNotesOrder & order) const
{
    QString result;

    switch (order) {
    case ListNotesOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListNotesOrder::ByTitle:
        result = QStringLiteral("title");
        break;
    case ListNotesOrder::ByCreationTimestamp:
        result = QStringLiteral("creationTimestamp");
        break;
    case ListNotesOrder::ByModificationTimestamp:
        result = QStringLiteral("modificationTimestamp");
        break;
    case ListNotesOrder::ByDeletionTimestamp:
        result = QStringLiteral("deletionTimestamp");
        break;
    case ListNotesOrder::ByAuthor:
        result = QStringLiteral("author");
        break;
    case ListNotesOrder::BySource:
        result = QStringLiteral("source");
        break;
    case ListNotesOrder::BySourceApplication:
        result = QStringLiteral("sourceApplication");
        break;
    case ListNotesOrder::ByReminderTime:
        result = QStringLiteral("reminderTime");
        break;
    case ListNotesOrder::ByPlaceName:
        result = QStringLiteral("placeName");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString LocalStorageManagerPrivate::orderByToSqlTableColumn<ListNotebooksOrder>(
    const ListNotebooksOrder & order) const
{
    QString result;

    switch (order) {
    case ListNotebooksOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListNotebooksOrder::ByNotebookName:
        result = QStringLiteral("notebookNameUpper");
        break;
    case ListNotebooksOrder::ByCreationTimestamp:
        result = QStringLiteral("creationTimestamp");
        break;
    case ListNotebooksOrder::ByModificationTimestamp:
        result = QStringLiteral("modificationTimestamp");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString
LocalStorageManagerPrivate::orderByToSqlTableColumn<ListLinkedNotebooksOrder>(
    const ListLinkedNotebooksOrder & order) const
{
    QString result;

    switch (order) {
    case ListLinkedNotebooksOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListLinkedNotebooksOrder::ByShareName:
        result = QStringLiteral("shareName");
        break;
    case ListLinkedNotebooksOrder::ByUsername:
        result = QStringLiteral("username");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString LocalStorageManagerPrivate::orderByToSqlTableColumn<ListTagsOrder>(
    const ListTagsOrder & order) const
{
    QString result;

    switch (order) {
    case ListTagsOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListTagsOrder::ByName:
        result = QStringLiteral("nameLower");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString
LocalStorageManagerPrivate::orderByToSqlTableColumn<ListSavedSearchesOrder>(
    const ListSavedSearchesOrder & order) const
{
    QString result;

    switch (order) {
    case ListSavedSearchesOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListSavedSearchesOrder::ByName:
        result = QStringLiteral("nameLower");
        break;
    case ListSavedSearchesOrder::ByFormat:
        result = QStringLiteral("format");
        break;
    default:
        break;
    }

    return result;
}

template <class T>
bool LocalStorageManagerPrivate::fillObjectsFromSqlQuery(
    QSqlQuery query, QList<T> & objects, ErrorString & errorDescription) const
{
    objects.reserve(std::max(query.size(), 0));

    while (query.next()) {
        QSqlRecord rec = query.record();

        objects << T();
        T & object = objects.back();

        bool res = fillObjectFromSqlRecord(rec, object, errorDescription);
        if (!res) {
            return false;
        }
    }

    return true;
}

template <>
bool LocalStorageManagerPrivate::fillObjectsFromSqlQuery(
    QSqlQuery query, QList<qevercloud::Note> & notes,
    ErrorString & errorDescription) const
{
    QHash<QString, int> indexByLocalId;

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const int localIdIndex = rec.indexOf(QStringLiteral("localUid"));
        if (localIdIndex < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("no localUid field in SQL record for note"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        const QString localId = rec.value(localIdIndex).toString();
        if (localId.isEmpty()) {
            errorDescription.setBase(QT_TR_NOOP(
                "found empty localUid field in SQL record for note"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        const auto it = indexByLocalId.find(localId);
        const bool notFound = (it == indexByLocalId.end());

        auto indexInList =
            (notFound
             ? indexByLocalId[localId]
             : it.value());

        if (notFound) {
            indexByLocalId[localId] = notes.size();
            notes << qevercloud::Note();
        }
        else {
            Q_ASSERT(
                indexInList >= 0 &&
                indexInList < notes.size());
        }

        auto & note = (notFound ? notes.back() : notes[indexInList]);

        if (!fillNoteFromSqlRecord(rec, note, errorDescription)) {
            return false;
        }
    }

    for (auto & note: notes)
    {
        if (!note.guid()) {
            continue;
        }

        ErrorString error;
        auto sharedNotes = listSharedNotesPerNoteGuid(*note.guid(), error);
        if (!error.isEmpty()) {
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        if (!sharedNotes.isEmpty()) {
            note.setSharedNotes(std::move(sharedNotes));
        }
    }

    return true;
}

template <>
bool LocalStorageManagerPrivate::fillObjectsFromSqlQuery<qevercloud::Notebook>(
    QSqlQuery query, QList<qevercloud::Notebook> & notebooks,
    ErrorString & errorDescription) const
{
    QMap<QString, int> indexForLocalId;

    while (query.next()) {
        const QSqlRecord rec = query.record();

        const int localIdIndex = rec.indexOf(QStringLiteral("localUid"));
        if (localIdIndex < 0) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "LocalStorageManagerPrivate",
                "no localUid field in SQL record for notebook"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        const QString localId = rec.value(localIdIndex).toString();
        if (localId.isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "LocalStorageManagerPrivate",
                "found empty localUid field in SQL record for Notebook"));
            QNWARNING("local_storage", errorDescription);
            return false;
        }

        const auto it = indexForLocalId.find(localId);
        const bool notFound = (it == indexForLocalId.end());
        if (notFound) {
            indexForLocalId[localId] = notebooks.size();
            notebooks << qevercloud::Notebook();
        }

        auto & notebook = (notFound ? notebooks.back() : notebooks[it.value()]);

        if (!fillNotebookFromSqlRecord(rec, notebook, errorDescription)) {
            return false;
        }

        if (notebook.guid())
        {
            ErrorString error;
            auto sharedNotebooks = listSharedNotebooksPerNotebookGuid(
                *notebook.guid(), error);
            if (!error.isEmpty()) {
                errorDescription.base() = error.base();
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage", errorDescription);
                return false;
            }

            if (!sharedNotebooks.isEmpty()) {
                notebook.setSharedNotebooks(std::move(sharedNotebooks));
            }
        }
    }

    return true;
}

template <>
bool LocalStorageManagerPrivate::fillObjectFromSqlRecord<
    qevercloud::SavedSearch>(
    const QSqlRecord & rec, qevercloud::SavedSearch & search,
    ErrorString & errorDescription) const
{
    return fillSavedSearchFromSqlRecord(rec, search, errorDescription);
}

template <>
bool LocalStorageManagerPrivate::fillObjectFromSqlRecord<qevercloud::Tag>(
    const QSqlRecord & rec, qevercloud::Tag & tag,
    ErrorString & errorDescription) const
{
    return fillTagFromSqlRecord(rec, tag, errorDescription);
}

template <>
bool LocalStorageManagerPrivate::fillObjectFromSqlRecord<
    qevercloud::LinkedNotebook>(
    const QSqlRecord & rec, qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) const
{
    return fillLinkedNotebookFromSqlRecord(
        rec, linkedNotebook, errorDescription);
}

template <>
bool LocalStorageManagerPrivate::fillObjectFromSqlRecord<qevercloud::Notebook>(
    const QSqlRecord & rec, qevercloud::Notebook & notebook,
    ErrorString & errorDescription) const
{
    return fillNotebookFromSqlRecord(rec, notebook, errorDescription);
}

template <>
bool LocalStorageManagerPrivate::fillObjectFromSqlRecord<qevercloud::Note>(
    const QSqlRecord & rec, qevercloud::Note & note,
    ErrorString & errorDescription) const
{
    return fillNoteFromSqlRecord(rec, note, errorDescription);
}

template <>
bool LocalStorageManagerPrivate::fillObjectsFromSqlQuery(
    QSqlQuery query,
    QList<std::pair<qevercloud::Tag, QStringList>> & tagsWithNoteLocalIds,
    ErrorString & errorDescription) const
{
    tagsWithNoteLocalIds.reserve(std::max(query.size(), 0));

    while (query.next()) {
        const QSqlRecord rec = query.record();

        tagsWithNoteLocalIds << std::pair<qevercloud::Tag, QStringList>();
        auto & pair = tagsWithNoteLocalIds.back();

        if (!fillObjectFromSqlRecord<qevercloud::Tag>(
                rec, pair.first, errorDescription))
        {
            return false;
        }
    }

    return complementTagsWithNoteLocalIds(
        tagsWithNoteLocalIds, errorDescription);
}

template <class T, class TOrderBy>
QList<T> LocalStorageManagerPrivate::listObjects(
    const ListObjectsOptions & flag, ErrorString & errorDescription,
    const std::size_t limit, const std::size_t offset, const TOrderBy & orderBy,
    const OrderDirection & orderDirection,
    const QString & additionalSqlQueryCondition) const
{
    ErrorString flagError;

    QString sqlQueryConditions =
        listObjectsOptionsToSqlQueryConditions<T>(flag, flagError);

    if (sqlQueryConditions.isEmpty() && !flagError.isEmpty()) {
        errorDescription = flagError;
        return QList<T>();
    }

    QString sumSqlQueryConditions;
    if (!sqlQueryConditions.isEmpty()) {
        sumSqlQueryConditions += sqlQueryConditions;
    }

    if (!additionalSqlQueryCondition.isEmpty()) {
        if (!sumSqlQueryConditions.isEmpty() &&
            !sumSqlQueryConditions.endsWith(QStringLiteral(" AND ")))
        {
            sumSqlQueryConditions += QStringLiteral(" AND ");
        }

        sumSqlQueryConditions += additionalSqlQueryCondition;
    }

    if (sumSqlQueryConditions.endsWith(QStringLiteral(" AND "))) {
        sumSqlQueryConditions.chop(5);
    }

    QString queryString = listObjectsGenericSqlQuery<T>();
    if (!sumSqlQueryConditions.isEmpty()) {
        sumSqlQueryConditions.prepend(QStringLiteral("("));
        sumSqlQueryConditions.append(QStringLiteral(")"));
        queryString += QStringLiteral(" WHERE ");
        queryString += sumSqlQueryConditions;
    }

    QString orderByColumn = orderByToSqlTableColumn<TOrderBy>(orderBy);
    if (!orderByColumn.isEmpty()) {
        queryString += QStringLiteral(" ORDER BY ");
        queryString += orderByColumn;

        switch (orderDirection) {
        case OrderDirection::Descending:
            queryString += QStringLiteral(" DESC");
            break;
        case OrderDirection::Ascending:
            // NOTE: intentional fall-through
        default:
            queryString += QStringLiteral(" ASC");
            break;
        }
    }

    if (limit != 0) {
        queryString += QStringLiteral(" LIMIT ") + QString::number(limit);
    }

    if (offset != 0) {
        queryString += QStringLiteral(" OFFSET ") + QString::number(offset);
    }

    QNDEBUG("local_storage", "SQL query string: " << queryString);

    QList<T> objects;

    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "LocalStorageManagerPrivate",
        "can't list objects from the local "
        "storage database by filter"));

    QSqlQuery query(m_sqlDatabase);
    if (!query.exec(queryString)) {
        errorDescription.base() = errorPrefix.base();
        QNERROR(
            "local_storage",
            errorDescription << ", last query = " << query.lastQuery()
                             << ", last error = " << query.lastError());
        errorDescription.details() = query.lastError().text();
        return objects;
    }

    ErrorString error;
    if (!fillObjectsFromSqlQuery(query, objects, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage", errorDescription);
        objects.clear();
        return objects;
    }

    QNDEBUG("local_storage", "found " << objects.size() << " objects");

    return objects;
}

bool LocalStorageManagerPrivate::SharedNotebookCompareByIndex::operator()(
    const qevercloud::SharedNotebook & lhs,
    const qevercloud::SharedNotebook & rhs) const noexcept
{
    return sharedNotebookIndexInNotebook(lhs) <
        sharedNotebookIndexInNotebook(rhs);
}

bool LocalStorageManagerPrivate::ResourceCompareByIndex::operator()(
    const qevercloud::Resource & lhs,
    const qevercloud::Resource & rhs) const noexcept
{
    return lhs.indexInNote().value_or(-1) < rhs.indexInNote().value_or(-1);
}

bool LocalStorageManagerPrivate::QStringIntPairCompareByInt::operator()(
    const std::pair<QString, int> & lhs,
    const std::pair<QString, int> & rhs) const noexcept
{
    return (lhs.second < rhs.second);
}

#undef CHECK_AND_SET_RESOURCE_PROPERTY
#undef CHECK_AND_SET_NOTEBOOK_ATTRIBUTE
#undef CHECK_AND_SET_EN_NOTEBOOK_ATTRIBUTE
#undef SET_IS_FREE_ACCOUNT_FLAG
#undef CHECK_AND_SET_EN_RESOURCE_PROPERTY
#undef CHECK_AND_SET_NOTE_PROPERTY
#undef DATABASE_CHECK_AND_SET_ERROR

} // namespace quentier
