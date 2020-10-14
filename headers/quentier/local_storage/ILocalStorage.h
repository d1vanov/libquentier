/*
 * Copyright 2020 Dmitry Ivanov
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

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/Result.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/User.h>
#include <quentier/utility/Linkage.h>

#include <QFlags>
#include <QFuture>
#include <QThreadPool>
#include <QVector>

#include <utility>

class QTextStream;
class QDebug;

namespace quentier::local_storage {

class QUENTIER_EXPORT ILocalStorage
{
public:
    virtual ~ILocalStorage() = default;

public:
    enum class StartupOption
    {
        ClearDatabase = 1 << 1,
        OverrideLock = 1 << 2
    };

    Q_DECLARE_FLAGS(StartupOptions, StartupOption);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const StartupOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const StartupOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const StartupOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const StartupOptions options);

public:
    enum class ListObjectsOption
    {
        ListAll = 1 << 0,
        ListDirty = 1 << 1,
        ListNonDirty = 1 << 2,
        ListElementsWithoutGuid = 1 << 3,
        ListElementsWithGuid = 1 << 4,
        ListLocal = 1 << 5,
        ListNonLocal = 1 << 6,
        ListFavoritedElements = 1 << 7,
        ListNonFavoritedElements = 1 << 8
    };

    Q_DECLARE_FLAGS(ListObjectsOptions, ListObjectsOption)

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsOption option);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsOption option);

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const ListObjectsOptions options);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const ListObjectsOptions options);

public:
    // Versions/upgrade API
    virtual QFuture<Result<bool>> isVersionTooHigh() = 0;
    virtual QFuture<Result<bool>> requiresUpgrade() = 0;
    virtual QFuture<Result<QVector<ILocalStoragePatchPtr>>> requiredPatches() = 0;
    virtual QFuture<Result<qint32>> version() = 0;
    virtual QFuture<qint32> highestSupportedVersion() = 0;

    // Users API
    virtual QFuture<Result<qint32>> userCount() = 0;
    virtual QFuture<Result<void>> putUser(User user) = 0;
    virtual QFuture<Result<User>> findUser(qint32 userId) = 0;
    virtual QFuture<Result<void>> expungeUser(qint32 userId) = 0;

    // Notebooks API
    virtual QFuture<Result<qint32>> notebookCount() = 0;
    virtual QFuture<Result<void>> putNotebook(Notebook notebook) = 0;

    enum class FindNotebookBy
    {
        LocalUid,
        Guid,
        Name
    };

    friend QUENTIER_EXPORT QTextStream & operator<<(
        QTextStream & strm, const FindNotebookBy what);

    friend QUENTIER_EXPORT QDebug & operator<<(
        QDebug & dbg, const FindNotebookBy what);

    virtual QFuture<Result<Notebook>> findNotebook(FindNotebookBy what, QString value);

    virtual QFuture<Result<Notebook>> findDefaultNotebook();
    virtual QFuture<Result<Notebook>> findLastUsedNotebook();
    virtual QFuture<Result<Notebook>> findDefaultOrLastUsedNotebook();

    virtual QFuture<Result<void>> expungeNotebook(Notebook notebook);
};

} // namespace quentier::local_storage
