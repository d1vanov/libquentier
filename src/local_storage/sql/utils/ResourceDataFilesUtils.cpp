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

#include "ResourceDataFilesUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/FileSystem.h>

#include <QUuid>

namespace quentier::local_storage::sql::utils {

namespace {

[[nodiscard]] QString generateTransactionId()
{
    auto id = QUuid::createUuid().toString();
    // Remove curvy braces
    id.remove(id.size() - 1, 1);
    id.remove(0, 1);
    return id;
}

} // namespace

ResourceDataFileTransaction::ResourceDataFileTransaction(
    const QDir & localStorageDir) :
    m_id{generateTransactionId()}
{
    m_resourcesDataDir.setPath(
        localStorageDir.absolutePath() + QStringLiteral("/Resources/data"));

    if (Q_UNLIKELY(!m_resourcesDataDir.exists())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot start resource data files transaction: resources data dir "
            "does not exist")};
        errorDescription.details() = m_resourcesDataDir.absolutePath();
        QNWARNING("local_storage::sql::utils", errorDescription);
        throw InvalidArgument{std::move(errorDescription)};
    }

    QNDEBUG(
        "local_storage::sql::utils",
        "Created resource data files transaction with id " << m_id);
}

ResourceDataFileTransaction::~ResourceDataFileTransaction()
{
    if (!m_committed && !m_rolledBack)
    {
        QNDEBUG(
            "local_storage::sql::utils",
            "Automatically rolling back uncommitted resource data files "
            "transaction with id " << m_id);

        rollback();
    }
}

bool ResourceDataFileTransaction::putResourceDataBodyToFile(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QByteArray & resourceDataBody, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(resourceDataBody)
    Q_UNUSED(errorDescription)
    return true;
}

bool ResourceDataFileTransaction::putResourceAlternateDataBodyToFile(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QByteArray & alternateDataBody, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(alternateDataBody)
    Q_UNUSED(errorDescription)
    return true;
}

bool ResourceDataFileTransaction::removeResourceDataBodyFile(
    const QString & noteLocalId, const QString & resourceLocalId,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(errorDescription)
    return true;
}

bool ResourceDataFileTransaction::removeResourceAlternateDataBodyFile(
    const QString & noteLocalId, const QString & resourceLocalId,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(errorDescription)
    return true;
}

void ResourceDataFileTransaction::commit()
{
    if (Q_UNLIKELY(m_committed)) {
        QNWARNING(
            "local_storage::sql::utils",
            "Resource data files transaction with id " << m_id
                << " has already been committed");
        return;
    }

    QNDEBUG(
        "local_storage::sql::utils",
        "Committing resource data files transaction with id " << m_id);

    // TODO: implement
}

void ResourceDataFileTransaction::rollback()
{
    if (Q_UNLIKELY(m_rolledBack)) {
        QNWARNING(
            "local_storage::sql::utils",
            "Resource data files transaction with id " << m_id
                << " has already been rolled back");
        return;
    }

    QNDEBUG(
        "local_storage::sql::utils",
        "Rolling back resource data files transaction with id " << m_id);

    // TODO: implement
}

bool removeResourceDataFilesForNote(
    const QString & noteLocalId, const QDir & localStorageDir,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "removeResourceDataFilesForNote: note local id = " << noteLocalId);

    const QString dataPath =
        localStorageDir.absolutePath() + QStringLiteral("/Resources/data/") +
        noteLocalId;

    if (!removeDir(dataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource data bodies"));
        errorDescription.details() = dataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    const QString alternateDataPath =
        localStorageDir.absolutePath() +
        QStringLiteral("/Resources/alternateData/") + noteLocalId;

    if (!removeDir(alternateDataPath)) {
        errorDescription.setBase(
            QT_TR_NOOP("failed to remove the folder containing "
                       "note's resource alternate data bodies"));
        errorDescription.details() = alternateDataPath;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    return true;
}

bool readResourceDataFromFiles(
    qevercloud::Resource & resource, const QDir & localStorageDir,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(resource)
    Q_UNUSED(localStorageDir)
    Q_UNUSED(errorDescription)
    return true;
}

bool removeResourceDataFiles(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QDir & localStorageDir, ErrorString & errorDescription)
{
    Q_UNUSED(noteLocalId)
    Q_UNUSED(resourceLocalId)
    Q_UNUSED(localStorageDir)
    Q_UNUSED(errorDescription)
    return true;
}

} // namespace quentier::local_storage::sql::utils
