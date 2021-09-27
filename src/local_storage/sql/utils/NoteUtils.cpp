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

#include "NoteUtils.h"
#include "NotebookUtils.h"
#include "ResourceUtils.h"
#include "SqlUtils.h"
#include "TagUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Note.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>

#include <algorithm>

namespace quentier::local_storage::sql::utils {

namespace {

[[nodiscard]] bool notebookNameInNoteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, QTextStream & strm,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    const QString notebookName = noteSearchQuery.notebookModifier();
    if (notebookName.isEmpty()) {
        return true;
    }

    QString notebookLocalId;
    notebookLocalId = notebookLocalIdByName(
        notebookName, std::nullopt, database, errorDescription);
    if (notebookLocalId.isEmpty()) {
        if (errorDescription.isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot find notebook with such name"));
            errorDescription.setDetails(notebookName);
        }
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    strm << "(notebookLocalUid = '" << sqlEscape(notebookLocalId) << "') AND ";
    return true;
}

[[nodiscard]] bool tagsInNoteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, const QString & uniteOperator,
    QTextStream & strm, QSqlDatabase & database, ErrorString & errorDescription)
{
    if (noteSearchQuery.hasAnyTag()) {
        strm << "(NoteTags.localTag IS NOT NULL) ";
        strm << uniteOperator;
        strm << " ";
        return true;
    }

    if (noteSearchQuery.hasNegatedAnyTag()) {
        strm << "(NoteTags.localTag IS NULL) ";
        strm << uniteOperator;
        strm << " ";
        return true;
    }

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();

    const auto tagLocalIdsByNames = [&](const QStringList & tagNames) {
        if (tagNames.isEmpty()) {
            return QStringList{};
        }

        QStringList result;
        result.reserve(std::max(tagNames.size(), 0));
        for (const auto & tagName: qAsConst(tagNames)) {
            errorDescription.clear();
            auto tagLocalId = tagLocalIdByName(
                tagName, std::nullopt, database, errorDescription);
            if (tagLocalId.isEmpty()) {
                if (errorDescription.isEmpty()) {
                    errorDescription.setBase(QT_TRANSLATE_NOOP(
                        "local_storage::sql::utils",
                        "Cannot find tag with such name"));
                    errorDescription.setDetails(tagName);
                }
                QNWARNING("local_storage::sql::utils", errorDescription);
                return QStringList{};
            }

            result << tagLocalId;
        }

        return result;
    };

    const QStringList tagLocalIds =
        tagLocalIdsByNames(noteSearchQuery.tagNames());
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
            strm << "(NoteTags.localNote IN (SELECT localNote "
                 << "FROM (SELECT localNote, localTag, COUNT(*) "
                 << "FROM NoteTags WHERE NoteTags.localTag IN ("
                 << toQuotedSqlList(tagLocalIds)
                 << ") GROUP BY localNote HAVING COUNT(*)=" << numTagLocalIds
                 << "))) ";
        }
        else {
            /**
             * With "any:" modifier the search doesn't care about
             * the exactness of tag-to-note map, it would instead pick just
             * any note corresponding to any of requested tags at least once
             */

            strm << "(NoteTags.localNote IN (SELECT localNote "
                 << "FROM (SELECT localNote, localTag "
                 << "FROM NoteTags WHERE NoteTags.localTag IN ("
                 << toQuotedSqlList(tagLocalIds) << ")))) ";
        }

        strm << uniteOperator;
        strm << " ";
    }

    const QStringList tagNegatedLocalIds =
        tagLocalIdsByNames(noteSearchQuery.negatedTagNames());

    if (!tagNegatedLocalIds.isEmpty()) {
        if (!queryHasAnyModifier) {
            /**
             * First find all notes' local ids which actually correspond
             * to negated tags' local ids; then simply negate that
             * condition
             */

            const int numTagNegatedLocalIds = tagNegatedLocalIds.size();
            strm << "(NoteTags.localNote NOT IN (SELECT localNote "
                 << "FROM (SELECT localNote, localTag, COUNT(*) "
                 << "FROM NoteTags WHERE NoteTags.localTag IN ("
                 << toQuotedSqlList(tagNegatedLocalIds)
                 << ") GROUP BY localNote HAVING COUNT(*)="
                 << numTagNegatedLocalIds;

            // Don't forget to account for the case of no tags used for note
            // so it's not even present in NoteTags table
            strm << ")) OR (NoteTags.localNote IS NULL)) ";
        }
        else {
            /**
             * With "any:" modifier the search doesn't care about the
             * exactness of tag-to-note map, it would instead pick just any
             * note not from the list of notes corresponding to any of
             * requested tags at least once
             */

            strm << "(NoteTags.localNote NOT IN (SELECT "
                 << "localNote FROM (SELECT localNote, localTag "
                 << "FROM NoteTags WHERE NoteTags.localTag IN ("
                 << toQuotedSqlList(tagNegatedLocalIds);

            // Don't forget to account for the case of no tags used for note
            // so it's not even present in NoteTags table
            strm << "))) OR (NoteTags.localNote IS NULL)) ";
        }

        strm << uniteOperator;
        strm << " ";
    }

    return true;
}

[[nodiscard]] bool resourceMimeTypesInNoteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, const QString & uniteOperator,
    QTextStream & strm, QSqlDatabase & database, ErrorString & errorDescription)
{
    if (noteSearchQuery.hasAnyResourceMimeType()) {
        strm << "(NoteResources.localResource IS NOT NULL) ";
        strm << uniteOperator;
        strm << " ";
        return true;
    }

    if (noteSearchQuery.hasNegatedAnyResourceMimeType()) {
        strm << "(NoteResources.localResource IS NULL) ";
        strm << uniteOperator;
        strm << " ";
        return true;
    }

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();
    const QStringList & resourceMimeTypes = noteSearchQuery.resourceMimeTypes();

    const QStringList resourceLocalIdsPerMime = findResourceLocalIdsByMimeTypes(
        resourceMimeTypes, database, errorDescription);

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

            strm << "(NoteResources.localNote IN (SELECT "
                 << "localNote FROM (SELECT localNote, "
                 << "localResource, COUNT(*) "
                 << "FROM NoteResources WHERE "
                 << "NoteResources.localResource IN ("
                 << toQuotedSqlList(resourceLocalIdsPerMime)
                 << ") GROUP BY localNote HAVING COUNT(*)="
                 << QString::number(resourceMimeTypes.size()) << "))) ";
        }
        else {
            /**
             * With "any:" modifier the search doesn't care about the
             * exactness of resource mime type-to-note map, it would instead
             * pick just any note having at least one resource with
             * requested mime type
             */

            strm << "(NoteResources.localNote IN (SELECT "
                 << "localNote FROM (SELECT localNote, "
                 << "localResource FROM NoteResources WHERE "
                 << "NoteResources.localResource IN ("
                 << toQuotedSqlList(resourceLocalIdsPerMime) << ")))) ";
        }

        strm << uniteOperator;
        strm << " ";
    }

    const QStringList & negatedResourceMimeTypes =
        noteSearchQuery.negatedResourceMimeTypes();

    const QStringList resourceNegatedLocalIdsPerMime =
        findResourceLocalIdsByMimeTypes(
            negatedResourceMimeTypes, database, errorDescription);

    if (!resourceNegatedLocalIdsPerMime.isEmpty()) {
        if (!queryHasAnyModifier) {
            strm << "(NoteResources.localNote NOT IN (SELECT "
                 << "localNote FROM (SELECT localNote, "
                 << "localResource, COUNT(*) "
                 << "FROM NoteResources WHERE "
                 << "NoteResources.localResource IN ("
                 << toQuotedSqlList(resourceNegatedLocalIdsPerMime)
                 << ") GROUP BY localNote HAVING COUNT(*)="
                 << QString::number(negatedResourceMimeTypes.size());

            // Don't forget to account for the case of no resources existing
            // in the note so it's not even present in NoteResources table
            strm << ")) OR (NoteResources.localNote IS NULL)) ";
        }
        else {
            strm << "(NoteResources.localNote NOT IN (SELECT "
                 << "localNote FROM (SELECT localNote, localResource "
                 << "FROM NoteResources WHERE "
                 << "NoteResources.localResource IN ("
                 << toQuotedSqlList(resourceNegatedLocalIdsPerMime);

            /**
             * Don't forget to account for the case of no resources existing
             * in the note so it's not even present in NoteResources table
             */
            strm << "))) OR (NoteResources.localNote IS NULL)) ";
        }

        strm << uniteOperator;
        strm << " ";
    }

    return true;
}

QString noteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QString queryString;
    QTextStream strm{&queryString};

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "can't convert note search query string into SQL query"});

    // 1) Setting up initial templates
    const QString sqlPrefix = QStringLiteral("SELECT DISTINCT localUid ");

    // 2) Determining whether "any:" modifier takes effect

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();

    const QString uniteOperator =
        (queryHasAnyModifier ? QStringLiteral("OR") : QStringLiteral("AND"));

    const auto extendError = [&](ErrorString & error,
                                 const QString & defaultBase,
                                 QString defaultDetails) {
        if (error.isEmpty()) {
            error.setBase(defaultBase);
            error.details() = std::move(defaultDetails);
        }
        errorDescription = errorPrefix;
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
    };

    // 3) Processing notebook modifier (if present)

    ErrorString error;
    if (!notebookNameInNoteSearchQueryToSql(
            noteSearchQuery, strm, database, error)) {
        extendError(error, {}, {});
        QNWARNING("local_storage::sql::utils", errorDescription);
        return {};
    }

    // 4) Processing tag names and negated tag names, if any

    error.clear();
    if (!tagsInNoteSearchQueryToSql(
            noteSearchQuery, uniteOperator, strm, database, error))
    {
        extendError(error, {}, {});
        QNWARNING("local_storage::sql::utils", errorDescription);
        return {};
    }

    // 5) Processing resource mime types

    error.clear();
    if (!resourceMimeTypesInNoteSearchQueryToSql(
            noteSearchQuery, uniteOperator, strm, database, error))
    {
        extendError(error, {}, {});
        QNWARNING("local_storage::sql::utils", errorDescription);
        return {};
    }

    // TODO: continue from here
    return {};
}

} // namespace

QString notebookLocalId(
    const qevercloud::Note & note, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QString notebookLocalId = note.notebookLocalId();
    if (!notebookLocalId.isEmpty()) {
        return notebookLocalId;
    }

    QSqlQuery query{database};
    const auto & notebookGuid = note.notebookGuid();
    if (notebookGuid) {
        bool res = query.prepare(QStringLiteral(
            "SELECT localUid FROM Notebooks WHERE guid = :guid"));
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot determine notebook local id by notebook guid, failed "
                "to prepare query"),
            QString{});

        query.bindValue(QStringLiteral(":guid"), *notebookGuid);

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot determine notebook local id by notebook guid"),
            QString{});

        if (!query.next()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot find notebook local id for guid"));
            errorDescription.details() = *notebookGuid;
            QNWARNING("local_storage::sql::utils", errorDescription);
            return QString{};
        }

        return query.value(0).toString();
    }

    // No notebookGuid set to note, try to deduce notebook local id by note
    // local id
    bool res = query.prepare(QStringLiteral(
        "SELECT notebookLocalUid FROM Notes WHERE localUid = :localUid"));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook local id by note local id, failed "
            "to prepare query"),
        QString{});

    query.bindValue(QStringLiteral(":localUid"), note.localId());

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook local id by note local id"),
        QString{});

    if (!query.next()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find notebook local id for note local id"));
        errorDescription.details() = note.localId();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return QString{};
    }

    return query.value(0).toString();
}

QString notebookGuid(
    const qevercloud::Note & note, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    const auto & notebookGuid = note.notebookGuid();
    if (notebookGuid) {
        return *notebookGuid;
    }

    const auto localId = notebookLocalId(note, database, errorDescription);
    if (localId.isEmpty()) {
        return {};
    }

    QSqlQuery query{database};
    bool res = query.prepare(QStringLiteral(
        "SELECT guid FROM Notebooks WHERE localUid = :localUid"));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook guid by local id, failed to prepare "
            "query"),
        QString{});

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot determine notebook guid by local id"),
        QString{});

    if (!query.next()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find notebook guid for local id"));
        errorDescription.details() = localId;
        QNWARNING("local_storage::sql::utils", errorDescription);
        return QString{};
    }

    return query.value(0).toString();
}

QString noteLocalIdByGuid(
    const qevercloud::Guid & noteGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString =
        QStringLiteral("SELECT localUid FROM Notes WHERE guid = :guid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot find note local id by guid: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":guid"), noteGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils", "Cannot find note local id by guid"),
        {});

    if (!query.next()) {
        return {};
    }

    return query.value(0).toString();
}

QStringList queryNoteLocalIds(
    const NoteSearchQuery & noteSearchQuery, QSqlDatabase & database,
    ErrorString & errorDescription, const TransactionOption transactionOption)
{
    if (!noteSearchQuery.isMatcheable()) {
        return {};
    }

    std::optional<Transaction> transaction;
    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        transaction.emplace(database, Transaction::Type::Selection);
    }

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't find notes with the note search query")};

    ErrorString error;
    const QString queryString =
        noteSearchQueryToSql(noteSearchQuery, database, error);
    if (queryString.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return QStringList();
    }

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot list note local ids with note search query"),
        {});

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

} // namespace quentier::local_storage::sql::utils
