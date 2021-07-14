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

#include "Patch2To3.h"
#include "PatchUtils.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/FileCopier.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>

#include <utility/Threading.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <utility/Qt5Promise.h>
#else
#include <QPromise>
#endif

#include <algorithm>
#include <cmath>

namespace quentier::local_storage::sql {

namespace {

const QString gDbFileName = QStringLiteral("qn.storage.sqlite");

} // namespace

Patch2To3::Patch2To3(
    Account account, ConnectionPoolPtr connectionPool,
    QThreadPtr writerThread) :
    PatchBase(
        std::move(connectionPool), std::move(writerThread),
        accountPersistentStoragePath(account),
        accountPersistentStoragePath(account) +
            QStringLiteral("/backup_upgrade_2_to_3_") +
            QDateTime::currentDateTime().toString(Qt::ISODate)),
    m_account{std::move(account)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::patches::Patch2To3",
                "Patch2To3 ctor: account is empty")}};
    }
}

QString Patch2To3::patchShortDescription() const
{
    return tr(
        "Proper support for transactional updates of resource data files");
}

QString Patch2To3::patchLongDescription() const
{
    QString result;

    result += tr("This patch slightly changes the placement of attachment data "
                 "files within the local storage directory: it adds one more "
                 "intermediate dir which has the meaning of unique version id "
                 "of the attachment file.");

    result += QStringLiteral("\n");

    result += tr("Prior to this patch resource data files were stored "
                 "according to the following scheme:");

    result += QStringLiteral("\n\n");

    result += QStringLiteral(
        "Resources/data/<note local id>/<resource local id>.dat");

    result += QStringLiteral("\n\n");

    result += tr("After this patch there would be one additional element "
                 "in the path:");

    result += QStringLiteral("\n\n");

    result += QStringLiteral(
        "Resources/data/<note local id>/<version id>/<resource local id>.dat");

    result += QStringLiteral("\n\n");

    result += tr("The change is required in order to implement full support "
                 "for transactional updates and removals of resource data "
                 "files. Without this change interruptions of local storage "
                 "operations (such as application crashes, computer switching "
                 "off due to power failure etc.) could leave it in "
                 "inconsistent state.");

    result += QStringLiteral("\n\n");

    result += tr("The patch should not take long to apply as it just "
                 "creates a couple more helper tables in the database and "
                 "creates subdirs for existing resource data files");

    return result;
}

bool Patch2To3::backupLocalStorageSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches",
        "Patch2To3::backupLocalStorageSync");

    return utils::backupLocalStorageDatabaseFiles(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch2To3::restoreLocalStorageFromBackupSync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::patches",
        "Patch2To3::restoreLocalStorageFromBackupImpl");

    return utils::restoreLocalStorageDatabaseFilesFromBackup(
        m_localStorageDir.absolutePath(), m_backupDir.absolutePath(), promise,
        errorDescription);
}

bool Patch2To3::removeLocalStorageBackupSync(
    ErrorString & errorDescription)
{
    QNINFO(
        "local_storage::sql::patches",
        "Patch2To3::removeLocalStorageBackupSync");

    return utils::removeLocalStorageDatabaseFilesBackup(
        m_backupDir.absolutePath(), errorDescription);
}

bool Patch2To3::applySync(
    QPromise<void> & promise, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(promise)
    Q_UNUSED(errorDescription)
    return true;
}

} // namespace quentier::local_storage::sql
