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

#include "PartialUpdateNoteResources.h"
#include "Common.h"
#include "ListFromDatabaseUtils.h"
#include "PutToDatabaseUtils.h"
#include "ResourceDataFilesUtils.h"
#include "SqlUtils.h"

#include "../ErrorHandling.h"
#include "../TypeChecks.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Resource.h>

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>

namespace quentier::local_storage::sql::utils {

namespace {

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

void classifyNoteResources(
    const QList<qevercloud::Resource> & previousNoteResources,
    const QList<qevercloud::Resource> & updatedNoteResources,
    QSet<QString> & localIdsOfRemovedResources,
    QList<qevercloud::Resource> & addedResources,
    QList<qevercloud::Resource> & updatedResources)
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

[[nodiscard]] bool updateResourceIndexesInNote(
    const QList<std::pair<QString, int>> & resourceLocalIdsWithIndexesInNote,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "UPDATE Resources SET indexInNote = :indexInNote "
        "WHERE resourceLocalUid = :resourceLocalUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't update resources indexes in note: failed to prepare query"),
        false);

    for (const auto & pair: resourceLocalIdsWithIndexesInNote)
    {
        query.bindValue(QStringLiteral(":resourceLocalUid"), pair.first);
        query.bindValue(QStringLiteral(":indexInNote"), pair.second);

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Can't update resource indexes in note"),
            false);
    }

    return true;
}

[[nodiscard]] bool expungeResources(
    const QSet<QString> & localIds, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QString removeResourcesQueryString;
    QTextStream strm{&removeResourcesQueryString};

    strm << "DELETE FROM Resources WHERE resourceLocalUid IN (";

    static const QChar apostrophe = QChar::fromLatin1('\'');
    static const QChar comma = QChar::fromLatin1(',');
    for (const auto & localId: localIds)
    {
        strm << apostrophe;
        strm << sqlEscape(localId);
        strm << apostrophe;

        if (&localId != &(*localIds.crbegin())) {
            strm << comma;
        }
    }

    strm << ')';

    QSqlQuery query{database};
    bool res = query.exec(removeResourcesQueryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot bulk remove resources from the local storage database"),
        false);

    // NOTE: deliberately not removing data files corresponding to the removed
    // resources here, it will be done later, in the end of the transaction

    return true;
}

} // namespace

bool partialUpdateNoteResources(
    const QString & noteLocalId, const QDir & localStorageDir,
    const QList<qevercloud::Resource> & updatedNoteResources,
    const bool updateResourceBinaryData,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "partialUpdateNoteResources: "
        "note local id = "
            << noteLocalId << ", update resource binary data = "
            << (updateResourceBinaryData ? "true" : "false"));

    if (!checkDuplicatesByLocalId(updatedNoteResources)) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "The list of note's resources contains resources with "
            "the same local id"));
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    ErrorString error;
    auto previousNoteResources = listNoteResources(
        noteLocalId, localStorageDir,
        utils::ListNoteResourcesOption::WithoutBinaryData, database, error);
    if (!error.isEmpty()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot perform partial update of note's resources"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    if (compareResourcesListsWithoutBinaryData(
            previousNoteResources, updatedNoteResources))

    {
        QNDEBUG(
            "local_storage::sql::utils",
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
        "local_storage::sql::utils",
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
        if (!updateResourceIndexesInNote(
                localIdsAndIndexesInNoteToUpdate, database, error))
        {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot perform partial update of note's resources"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage::sql::utils", errorDescription);
            return false;
        }

        return true;
    }

    for (const auto & resource: qAsConst(addedResources))
    {
        ErrorString error;
        if (!checkResource(resource, error)) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot perform partial update of note's resources: detected "
                "attempt to add invalid resource to the local storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage::sql::utils",
                errorDescription << ", resource: " << resource);
            return false;
        }
    }

    for (const auto & resource: qAsConst(updatedResources))
    {
        ErrorString error;
        if (!checkResource(resource, error)) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot perform partial update of note's resources: detected "
                "invalid resource on attempt to update resource in the local "
                "storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage::sql::utils",
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
                localIdsOfResourcesToRemove, database, error))
        {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot perform partial update of note's resources: failed "
                "to expunge resources no longer belonging the note from the "
                "local storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING("local_storage::sql::utils", errorDescription);
            return false;
        }

        // See whether indexes of remaining resources need to be updated
        int firstChangedIndex = -1;
        for (int i = 0; i < remainingResources.size(); ++i) {
            const auto & remainingResource = qAsConst(remainingResources)[i];
            const auto & previousResource = qAsConst(previousNoteResources)[i];

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
            if (!updateResourceIndexesInNote(
                    localIdsAndIndexesInNoteToUpdate, database, error))
            {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage::sql::utils",
                    "Cannot perform partial update of note's resources"));
                errorDescription.appendBase(error.base());
                errorDescription.appendBase(error.additionalBases());
                errorDescription.details() = error.details();
                QNWARNING("local_storage::sql::utils", errorDescription);
                return false;
            }
        }

        if (addedResources.isEmpty() && updatedResources.isEmpty()) {
            return true;
        }
    }

    for (auto & resource: updatedResources)
    {
        const auto remainingResourceIt = std::find_if(
            remainingResources.constBegin(),
            remainingResources.constEnd(),
            [&resource](const qevercloud::Resource & remainingResource)
            {
                return remainingResource.localId() == resource.localId();
            });

        if (remainingResourceIt == remainingResources.constEnd()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot perform partial update of note resources: updated "
                "resource's index in note was not found"));
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage::sql::utils",
                errorDescription << ", resource: " << resource);
            return false;
        }

        const int indexInNote = static_cast<int>(std::distance(
            remainingResources.constBegin(), remainingResourceIt));

        error.clear();
        if (!putResource(
                localStorageDir, resource, indexInNote, database, error,
                (updateResourceBinaryData
                 ? PutResourceBinaryDataOption::WithBinaryData
                 : PutResourceBinaryDataOption::WithoutBinaryData),
                TransactionOption::DontUseSeparateTransaction))
        {
            errorDescription.appendBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Can't update one of note's resources in the local storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage::sql::utils",
                errorDescription << ", resource: " << resource);
            return false;
        }
    }

    int counter = 0;
    for (auto & resource: addedResources)
    {
        ErrorString error;
        const int indexInNote = remainingResources.size() + counter;
        error.clear();
        if (!putResource(
                localStorageDir, resource, indexInNote, database, error,
                (updateResourceBinaryData
                 ? PutResourceBinaryDataOption::WithBinaryData
                 : PutResourceBinaryDataOption::WithoutBinaryData),
                TransactionOption::DontUseSeparateTransaction))
        {
            errorDescription.appendBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Can't add one of note's resources to the local storage"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage::sql::utils",
                errorDescription << ", resource: " << resource);
            return false;
        }

        ++counter;
    }

    return true;
}

} // namespace quentier::local_storage::sql::utils
