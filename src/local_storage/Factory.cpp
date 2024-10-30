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

namespace quentier::local_storage {

ILocalStoragePtr createSqliteLocalStorage(
    const Account & account, const QDir & localStorageDir,
    threading::QThreadPtr thread)
{
    QNDEBUG(
        "local_storage::Factory",
        "ILocalStoragePtr createSqliteLocalStorage: dir = "
            << localStorageDir.absolutePath() << ", account: " << account);

    auto localStorageMainFilePath =
        localStorageDir.absoluteFilePath(QStringLiteral("qn.storage.sqlite"));

    auto connectionPool = std::make_shared<sql::ConnectionPool>(
        QStringLiteral("localhost"), QString{}, QString{},
        std::move(localStorageMainFilePath), QStringLiteral("QSQLITE"));

    {
        auto database = connectionPool->database();
        sql::TablesInitializer::initializeTables(database);
    }

    if (!thread) {
        auto deleter = [](QThread * thread) {
            thread->quit();
            thread->wait();
            thread->deleteLater();
        };
        auto threadUnique = std::make_unique<QThread>();
        thread = threading::QThreadPtr{threadUnique.get(), std::move(deleter)};
        Q_UNUSED(threadUnique.release()); // NOLINT
    }

    sql::Notifier * notifier = nullptr;
    {
        auto notifierUnique = std::make_unique<sql::Notifier>();
        notifierUnique->moveToThread(thread.get());

        QObject::connect(
            thread.get(), &QThread::finished, notifierUnique.get(),
            &QObject::deleteLater);

        notifier = notifierUnique.release();
    }

    thread->start();

    const QString localStorageDirPath = localStorageDir.absolutePath();

    auto linkedNotebooksHandler = std::make_shared<sql::LinkedNotebooksHandler>(
        connectionPool, notifier, thread, localStorageDirPath);

    auto notebooksHandler = std::make_shared<sql::NotebooksHandler>(
        connectionPool, notifier, thread, localStorageDirPath);

    auto notesHandler = std::make_shared<sql::NotesHandler>(
        connectionPool, notifier, thread, localStorageDirPath);

    auto resourcesHandler = std::make_shared<sql::ResourcesHandler>(
        connectionPool, notifier, thread, localStorageDirPath);

    auto savedSearchesHandler = std::make_shared<sql::SavedSearchesHandler>(
        connectionPool, notifier, thread);

    auto synchronizationInfoHandler =
        std::make_shared<sql::SynchronizationInfoHandler>(
            connectionPool, thread);

    auto tagsHandler =
        std::make_shared<sql::TagsHandler>(connectionPool, notifier, thread);

    auto versionHandler =
        std::make_shared<sql::VersionHandler>(account, connectionPool, thread);

    auto usersHandler = std::make_shared<sql::UsersHandler>(
        connectionPool, notifier, std::move(thread));

    return std::make_shared<sql::LocalStorage>(
        std::move(linkedNotebooksHandler), std::move(notebooksHandler),
        std::move(notesHandler), std::move(resourcesHandler),
        std::move(savedSearchesHandler), std::move(synchronizationInfoHandler),
        std::move(tagsHandler), std::move(versionHandler),
        std::move(usersHandler), notifier);
}

} // namespace quentier::local_storage
