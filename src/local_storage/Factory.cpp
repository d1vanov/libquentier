/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <quentier/local_storage/Factory.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Factory.h>

#include <local_storage/sql/ConnectionPool.h>
#include <local_storage/sql/LinkedNotebooksHandler.h>
#include <local_storage/sql/LocalStorage.h>
#include <local_storage/sql/NotebooksHandler.h>
#include <local_storage/sql/NotesHandler.h>
#include <local_storage/sql/Notifier.h>
#include <local_storage/sql/ResourcesHandler.h>
#include <local_storage/sql/SavedSearchesHandler.h>
#include <local_storage/sql/SynchronizationInfoHandler.h>
#include <local_storage/sql/TablesInitializer.h>
#include <local_storage/sql/TagsHandler.h>
#include <local_storage/sql/UsersHandler.h>
#include <local_storage/sql/VersionHandler.h>

#include <QDir>
#include <QReadWriteLock>

namespace quentier::local_storage {

ILocalStoragePtr createSqliteLocalStorage(
    const Account & account, const QDir & localStorageDir,
    threading::QThreadPoolPtr threadPool)
{
    QNDEBUG(
        "local_storage::Factory",
        "ILocalStoragePtr createSqliteLocalStorage: dir = "
            << localStorageDir.absolutePath() << ", account: " << account);

    if (!threadPool) {
        threadPool = threading::globalThreadPool();
    }

    auto localStorageMainFilePath =
        localStorageDir.absoluteFilePath(QStringLiteral("qn.storage.sqlite"));

    auto connectionPool = std::make_shared<sql::ConnectionPool>(
        QStringLiteral("localhost"), QString{}, QString{},
        std::move(localStorageMainFilePath), QStringLiteral("QSQLITE"));

    {
        auto database = connectionPool->database();
        sql::TablesInitializer::initializeTables(database);
    }

    auto resourceDataFilesLock = std::make_shared<QReadWriteLock>();

    threading::QThreadPtr writerThread;
    {
        auto deleter = [](QThread * thread) {
            thread->quit();
            thread->wait();
            thread->deleteLater();
        };
        auto writerThreadUnique = std::make_unique<QThread>();

        writerThread =
            threading::QThreadPtr{writerThreadUnique.get(), std::move(deleter)};

        Q_UNUSED(writerThreadUnique.release());
    }

    sql::Notifier * notifier = nullptr;
    {
        auto notifierUnique = std::make_unique<sql::Notifier>();
        notifierUnique->moveToThread(writerThread.get());

        QObject::connect(
            writerThread.get(), &QThread::finished, notifierUnique.get(),
            &QObject::deleteLater);

        notifier = notifierUnique.release();
    }

    writerThread->start();

    const QString localStorageDirPath = localStorageDir.absolutePath();

    auto linkedNotebooksHandler = std::make_shared<sql::LinkedNotebooksHandler>(
        connectionPool, threadPool, notifier, writerThread,
        localStorageDirPath);

    auto notebooksHandler = std::make_shared<sql::NotebooksHandler>(
        connectionPool, threadPool, notifier, writerThread, localStorageDirPath,
        resourceDataFilesLock);

    auto notesHandler = std::make_shared<sql::NotesHandler>(
        connectionPool, threadPool, notifier, writerThread, localStorageDirPath,
        resourceDataFilesLock);

    auto resourcesHandler = std::make_shared<sql::ResourcesHandler>(
        connectionPool, threadPool, notifier, writerThread, localStorageDirPath,
        resourceDataFilesLock);

    auto savedSearchesHandler = std::make_shared<sql::SavedSearchesHandler>(
        connectionPool, threadPool, notifier, writerThread);

    auto synchronizationInfoHandler =
        std::make_shared<sql::SynchronizationInfoHandler>(
            connectionPool, threadPool, writerThread);

    auto tagsHandler = std::make_shared<sql::TagsHandler>(
        connectionPool, threadPool, notifier, writerThread);

    auto versionHandler = std::make_shared<sql::VersionHandler>(
        account, connectionPool, threadPool, writerThread);

    auto usersHandler = std::make_shared<sql::UsersHandler>(
        connectionPool, std::move(threadPool), notifier, writerThread);

    return std::make_shared<sql::LocalStorage>(
        std::move(linkedNotebooksHandler), std::move(notebooksHandler),
        std::move(notesHandler), std::move(resourcesHandler),
        std::move(savedSearchesHandler), std::move(synchronizationInfoHandler),
        std::move(tagsHandler), std::move(versionHandler),
        std::move(usersHandler), notifier);
}

} // namespace quentier::local_storage
