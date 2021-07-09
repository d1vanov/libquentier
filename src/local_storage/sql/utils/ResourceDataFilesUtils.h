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

#pragma once

#include <qevercloud/types/Fwd.h>

#include <QDir>
#include <QString>

#include <vector>

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

/**
 * @brief The ResourceDataFileTransaction class supports transactional changes
 * to resource data files. It allows to accumulate potentially many changes
 * to resource data files and then to either apply them all or roll them back.
 *
 * @note The methods of this class are not thread-safe.
 */
class ResourceDataFileTransaction
{
public:
    explicit ResourceDataFileTransaction(const QDir & localStorageDir);
    ~ResourceDataFileTransaction();

    [[nodiscard]] bool putResourceDataBodyToFile(
        const QString & noteLocalId, const QString & resourceLocalId,
        const QByteArray & resourceDataBody, ErrorString & errorDescription);

    [[nodiscard]] bool putResourceAlternateDataBodyToFile(
        const QString & noteLocalId, const QString & resourceLocalId,
        const QByteArray & alternateDataBody, ErrorString & errorDescription);

    [[nodiscard]] bool removeResourceDataBodyFile(
        const QString & noteLocalId, const QString & resourceLocalId,
        ErrorString & errorDescription);

    [[nodiscard]] bool removeResourceAlternateDataBodyFile(
        const QString & noteLocalId, const QString & resourceLocalId,
        ErrorString & errorDescription);

    void commit();
    void rollback();

private:
    struct PendingPutRequest
    {
        QString noteLocalId;
        QString resourceLocalId;
    };

    struct PendingRemoveRequest
    {
        QString noteLocalId;
        QString resourceLocalId;
    };

private:
    // Unique identifier of the transaction
    const QString m_id;
    QDir m_resourcesDataDir;
    std::vector<PendingPutRequest> m_pendingDataBodyRequests;
    std::vector<PendingPutRequest> m_pendingAlternateDataBodyRequests;
    bool m_committed = false;
    bool m_rolledBack = false;
};

[[nodiscard]] bool removeResourceDataFilesForNote(
    const QString & noteLocalId, const QDir & localStorageDir,
    ErrorString & errorDescription);

[[nodiscard]] bool readResourceDataFromFiles(
    qevercloud::Resource & resource, const QDir & localStorageDir,
    ErrorString & errorDescription);

[[nodiscard]] bool removeResourceDataFiles(
    const QString & noteLocalId, const QString & resourceLocalId,
    const QDir & localStorageDir, ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils
