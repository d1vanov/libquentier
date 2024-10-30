/*
 * Copyright 2021-2024 Dmitry Ivanov
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
#include <quentier/utility/StringUtils.h>

#include <qevercloud/types/Note.h>

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>

#include <algorithm>
#include <functional>
#include <utility>

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
            errorDescription.setBase(
                QStringLiteral("Cannot find notebook with such name"));
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
        result.reserve(std::max<decltype(tagNames.size())>(tagNames.size(), 0));
        for (const auto & tagName: std::as_const(tagNames)) {
            errorDescription.clear();
            auto tagLocalId = tagLocalIdByName(
                tagName, std::nullopt, database, errorDescription);
            if (tagLocalId.isEmpty()) {
                if (errorDescription.isEmpty()) {
                    errorDescription.setBase(
                        QStringLiteral("Cannot find tag with such name"));
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

            const auto numTagLocalIds = tagLocalIds.size();
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

            const auto numTagNegatedLocalIds = tagNegatedLocalIds.size();
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

void toDoItemsInNoteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, const QString & uniteOperator,
    QTextStream & strm)
{
    if (noteSearchQuery.hasAnyToDo()) {
        strm << "((NoteFTS.contentContainsFinishedToDo IS 1) OR "
             << "(NoteFTS.contentContainsUnfinishedToDo IS 1)) ";
        strm << uniteOperator;
        strm << " ";
        return;
    }

    if (noteSearchQuery.hasNegatedAnyToDo()) {
        strm << "((NoteFTS.contentContainsFinishedToDo IS 0) OR "
             << "(NoteFTS.contentContainsFinishedToDo IS NULL)) AND "
             << "((NoteFTS.contentContainsUnfinishedToDo IS 0) OR "
             << "(NoteFTS.contentContainsUnfinishedToDo IS NULL)) ";
        strm << uniteOperator;
        strm << " ";
        return;
    }

    if (noteSearchQuery.hasFinishedToDo()) {
        strm << "(NoteFTS.contentContainsFinishedToDo IS 1) ";
        strm << uniteOperator;
        strm << " ";
    }
    else if (noteSearchQuery.hasNegatedFinishedToDo()) {
        strm << "((NoteFTS.contentContainsFinishedToDo IS 0) OR "
             << "(NoteFTS.contentContainsFinishedToDo IS NULL)) ";
        strm << uniteOperator;
        strm << " ";
    }

    if (noteSearchQuery.hasUnfinishedToDo()) {
        strm << "(NoteFTS.contentContainsUnfinishedToDo IS 1) ";
        strm << uniteOperator;
        strm << " ";
    }
    else if (noteSearchQuery.hasNegatedUnfinishedToDo()) {
        strm << "((NoteFTS.contentContainsUnfinishedToDo IS 0) OR "
             << "(NoteFTS.contentContainsUnfinishedToDo IS NULL)) ";
        strm << uniteOperator;
        strm << " ";
    }
}

void encryptionItemInNoteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, const QString & uniteOperator,
    QTextStream & strm)
{
    if (noteSearchQuery.hasNegatedEncryption()) {
        strm << "((NoteFTS.contentContainsEncryption IS 0) OR "
             << "(NoteFTS.contentContainsEncryption IS NULL)) ";
        strm << uniteOperator;
        strm << " ";
    }
    else if (noteSearchQuery.hasEncryption()) {
        strm << "(NoteFTS.contentContainsEncryption IS 1) ";
        strm << uniteOperator;
        strm << " ";
    }
}

void contentSearchTermToSqlQueryParts(
    QString & frontSearchTermModifier, QString & searchTerm,    // NOLINT
    QString & backSearchTermModifier, QString & matchStatement) // NOLINT
{
    static const QRegularExpression whitespaceRegex{QStringLiteral("\\p{Z}")};
    static const QString asterisk = QStringLiteral("*");

    if ((whitespaceRegex.match(searchTerm).isValid()) ||
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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        int pos = -1;
#else
        qsizetype pos = -1;
#endif

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

[[nodiscard]] bool contentSearchTermsToSqlQueryPart(
    const NoteSearchQuery & noteSearchQuery, QTextStream & strm,
    ErrorString & errorDescription)
{
    if (!noteSearchQuery.hasAnyContentSearchTerms()) {
        errorDescription.setBase(
            QStringLiteral("note search query has no advanced search "
                           "modifiers and no content search terms"));
        errorDescription.details() = noteSearchQuery.queryString();
        QNWARNING("local_storage", errorDescription);
        return false;
    }

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();
    QString uniteOperator =
        (queryHasAnyModifier ? QStringLiteral("OR") : QStringLiteral("AND"));

    QString positiveSqlPart;
    QTextStream positiveSqlPartStrm{&positiveSqlPart};

    QString negatedSqlPart;
    QTextStream negatedSqlPartStrm{&negatedSqlPart};

    QString matchStatement;
    matchStatement.reserve(5);

    QString frontSearchTermModifier;
    frontSearchTermModifier.reserve(1);

    QString backSearchTermModifier;
    backSearchTermModifier.reserve(1);

    const QStringList & contentSearchTerms =
        noteSearchQuery.contentSearchTerms();

    StringUtils stringUtils;
    const auto asterisk = QList<QChar>{} << QChar::fromLatin1('*');

    if (!contentSearchTerms.isEmpty()) {
        for (const auto & searchTerm: std::as_const(contentSearchTerms)) {
            auto currentSearchTerm = searchTerm;
            stringUtils.removePunctuation(currentSearchTerm, asterisk);
            if (currentSearchTerm.isEmpty()) {
                continue;
            }

            stringUtils.removeDiacritics(currentSearchTerm);

            positiveSqlPartStrm << "(";

            contentSearchTermToSqlQueryParts(
                frontSearchTermModifier, currentSearchTerm,
                backSearchTermModifier, matchStatement);

            currentSearchTerm = sqlEscape(currentSearchTerm);

            positiveSqlPartStrm
                << QString::fromUtf8(
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

            positiveSqlPartStrm << ")";

            if (&searchTerm != &contentSearchTerms.constLast()) {
                positiveSqlPartStrm << " " << uniteOperator << " ";
            }
        }

        if (!positiveSqlPart.isEmpty()) {
            positiveSqlPartStrm << ")";
            positiveSqlPart.prepend(QStringLiteral("("));
        }
    }

    const auto & negatedContentSearchTerms =
        noteSearchQuery.negatedContentSearchTerms();

    if (!negatedContentSearchTerms.isEmpty()) {
        for (const auto & searchTerm: std::as_const(negatedContentSearchTerms))
        {
            auto currentSearchTerm = searchTerm;
            stringUtils.removePunctuation(currentSearchTerm, asterisk);

            if (currentSearchTerm.isEmpty()) {
                continue;
            }

            stringUtils.removeDiacritics(currentSearchTerm);

            negatedSqlPartStrm << "(";

            contentSearchTermToSqlQueryParts(
                frontSearchTermModifier, currentSearchTerm,
                backSearchTermModifier, matchStatement);

            currentSearchTerm = sqlEscape(currentSearchTerm);

            negatedSqlPartStrm
                << QString::fromUtf8(
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

            negatedSqlPartStrm << ")";

            if (&searchTerm != &negatedContentSearchTerms.constLast()) {
                negatedSqlPartStrm << " " << uniteOperator << " ";
            }
        }

        if (!negatedSqlPart.isEmpty()) {
            negatedSqlPartStrm << ")";
            negatedSqlPart.prepend(QStringLiteral("("));
        }
    }

    // First append all positive terms of the query
    if (!positiveSqlPart.isEmpty()) {
        strm << "(" << positiveSqlPart << ") ";
    }

    // Now append all negative parts of the query (if any)

    if (!negatedSqlPart.isEmpty()) {
        if (!positiveSqlPart.isEmpty()) {
            strm << " " << uniteOperator << " ";
        }

        strm << "(" << negatedSqlPart << ")";
    }

    if (!positiveSqlPart.isEmpty() || !negatedSqlPart.isEmpty()) {
        strm << " " << uniteOperator << " ";
    }

    return true;
}

[[nodiscard]] QString noteSearchQueryToSql(
    const NoteSearchQuery & noteSearchQuery, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QString queryString;
    QTextStream strm{&queryString};

    const ErrorString errorPrefix{QStringLiteral(
        "can't convert note search query string into SQL query")};

    // 1) Setting up initial templates
    QString sqlPrefix = QStringLiteral("SELECT DISTINCT localUid ");

    // 2) Determining whether "any:" modifier takes effect

    const bool queryHasAnyModifier = noteSearchQuery.hasAnyModifier();

    const QString uniteOperator =
        (queryHasAnyModifier ? QStringLiteral("OR") : QStringLiteral("AND"));

    const auto extendError = [&](ErrorString & error,
                                 const QString & defaultBase, // NOLINT
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
            noteSearchQuery, strm, database, error))
    {
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

    // 6) Processing other better generalizable filters

    const auto processAnyQueryItem = [&](const auto & hasAnyItem,
                                         const auto & hasNegatedAnyItem,
                                         const QString & column) {
        if (std::bind(hasAnyItem, noteSearchQuery)()) { // NOLINT
            strm << "(NoteFTS." << column << " IS NOT NULL) ";
            strm << uniteOperator;
            strm << " ";
            return true;
        }

        if (std::bind(hasNegatedAnyItem, noteSearchQuery)()) { // NOLINT
            strm << "(NoteFTS." << column << " IS NULL) ";
            strm << uniteOperator;
            strm << " ";
            return true;
        }

        return false;
    };

    enum class Negated
    {
        Yes,
        No
    };

    const auto processListQueryItem = [&](const auto & listItemAccessor,
                                          const Negated negated,
                                          const QString & column) {
        const auto items =
            std::bind(listItemAccessor, noteSearchQuery)(); // NOLINT
        if (items.isEmpty()) {
            return;
        }

        strm << "(";
        for (const auto & item: std::as_const(items)) {
            if (negated == Negated::Yes) {
                strm << "(localUid NOT IN ";
            }
            else {
                strm << "(localUid IN ";
            }
            strm << "(SELECT localUid FROM NoteFTS WHERE ";
            strm << "NoteFTS." << column << " MATCH \'";
            strm << sqlEscape(item);
            strm << "\')) ";

            if (&item != &items.constLast()) {
                strm << uniteOperator;
                strm << " ";
            }
        }
        strm << ") ";
        strm << uniteOperator;
        strm << " ";
    };

    const auto processNumericListQueryItem = [&](const auto & listItemAccessor,
                                                 const Negated negated,
                                                 const QString & column) {
        const auto items =
            std::bind(listItemAccessor, noteSearchQuery)(); // NOLINT
        if (items.isEmpty()) {
            return;
        }

        auto it = items.constEnd();
        if (queryHasAnyModifier) {
            if (negated == Negated::Yes) {
                it = std::max_element(items.constBegin(), items.constEnd());
            }
            else {
                it = std::min_element(items.constBegin(), items.constEnd());
            }
        }
        else {
            if (negated == Negated::Yes) {
                it = std::min_element(items.constBegin(), items.constEnd());
            }
            else {
                it = std::max_element(items.constBegin(), items.constEnd());
            }
        }

        if (it == items.constEnd()) {
            return;
        }

        strm << "(localUid IN (SELECT localUid FROM ";
        strm << "Notes WHERE Notes." << column;
        if (negated == Negated::Yes) {
            strm << " < ";
        }
        else {
            strm << " >= ";
        }
        strm << sqlEscape(QString::number(*it));
        strm << ")) ";
        strm << uniteOperator;
        strm << " ";
    };

    const auto processQueryItem =
        [&](const auto & hasAnyItem, const auto & hasNegatedAnyItem,
            const auto & listItemAccessor, const auto & negatedListItemAccessor,
            const QString & column) {
            if (processAnyQueryItem(hasAnyItem, hasNegatedAnyItem, column)) {
                return;
            }

            processListQueryItem(listItemAccessor, Negated::No, column);
            processListQueryItem(negatedListItemAccessor, Negated::Yes, column);
        };

    const auto processNumericQueryItem =
        [&](const auto & hasAnyItem, const auto & hasNegatedAnyItem,
            const auto & listItemAccessor, const auto & negatedListItemAccessor,
            const QString & column) {
            if (processAnyQueryItem(hasAnyItem, hasNegatedAnyItem, column)) {
                return;
            }

            processNumericListQueryItem(listItemAccessor, Negated::No, column);

            processNumericListQueryItem(
                negatedListItemAccessor, Negated::Yes, column);
        };

    processQueryItem(
        &NoteSearchQuery::hasAnyTitleName,
        &NoteSearchQuery::hasNegatedAnyTitleName, &NoteSearchQuery::titleNames,
        &NoteSearchQuery::negatedTitleNames, QStringLiteral("title"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyCreationTimestamp,
        &NoteSearchQuery::hasNegatedAnyCreationTimestamp,
        &NoteSearchQuery::creationTimestamps,
        &NoteSearchQuery::negatedCreationTimestamps,
        QStringLiteral("creationTimestamp"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyModificationTimestamp,
        &NoteSearchQuery::hasNegatedAnyModificationTimestamp,
        &NoteSearchQuery::modificationTimestamps,
        &NoteSearchQuery::negatedModificationTimestamps,
        QStringLiteral("modificationTimestamp"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnySubjectDateTimestamp,
        &NoteSearchQuery::hasNegatedAnySubjectDateTimestamp,
        &NoteSearchQuery::subjectDateTimestamps,
        &NoteSearchQuery::negatedSubjectDateTimestamps,
        QStringLiteral("subjectDate"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyLatitude,
        &NoteSearchQuery::hasNegatedAnyLatitude, &NoteSearchQuery::latitudes,
        &NoteSearchQuery::negatedLatitudes, QStringLiteral("latitude"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyLongitude,
        &NoteSearchQuery::hasNegatedAnyLongitude, &NoteSearchQuery::longitudes,
        &NoteSearchQuery::negatedLongitudes, QStringLiteral("longitude"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyAltitude,
        &NoteSearchQuery::hasNegatedAnyAltitude, &NoteSearchQuery::altitudes,
        &NoteSearchQuery::negatedAltitudes, QStringLiteral("altitude"));

    processQueryItem(
        &NoteSearchQuery::hasAnyAuthor, &NoteSearchQuery::hasNegatedAnyAuthor,
        &NoteSearchQuery::authors, &NoteSearchQuery::negatedAuthors,
        QStringLiteral("authors"));

    processQueryItem(
        &NoteSearchQuery::hasAnySource, &NoteSearchQuery::hasNegatedAnySource,
        &NoteSearchQuery::sources, &NoteSearchQuery::negatedSources,
        QStringLiteral("source"));

    processQueryItem(
        &NoteSearchQuery::hasAnySourceApplication,
        &NoteSearchQuery::hasNegatedAnySourceApplication,
        &NoteSearchQuery::sourceApplications,
        &NoteSearchQuery::negatedSourceApplications,
        QStringLiteral("sourceApplication"));

    processQueryItem(
        &NoteSearchQuery::hasAnyContentClass,
        &NoteSearchQuery::hasNegatedAnyContentClass,
        &NoteSearchQuery::contentClasses,
        &NoteSearchQuery::negatedContentClasses,
        QStringLiteral("contentClass"));

    processQueryItem(
        &NoteSearchQuery::hasAnyPlaceName,
        &NoteSearchQuery::hasNegatedAnyPlaceName, &NoteSearchQuery::placeNames,
        &NoteSearchQuery::negatedPlaceNames, QStringLiteral("placeName"));

    processQueryItem(
        &NoteSearchQuery::hasAnyApplicationData,
        &NoteSearchQuery::hasNegatedAnyApplicationData,
        &NoteSearchQuery::applicationData,
        &NoteSearchQuery::negatedApplicationData,
        QStringLiteral("applicationDataKeysOnly"));

    processQueryItem(
        &NoteSearchQuery::hasAnyApplicationData,
        &NoteSearchQuery::hasNegatedAnyApplicationData,
        &NoteSearchQuery::applicationData,
        &NoteSearchQuery::negatedApplicationData,
        QStringLiteral("applicationDataKeysMap"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyReminderOrder,
        &NoteSearchQuery::hasNegatedAnyReminderOrder,
        &NoteSearchQuery::reminderOrders,
        &NoteSearchQuery::negatedReminderOrders,
        QStringLiteral("reminderOrder"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyReminderTime,
        &NoteSearchQuery::hasNegatedAnyReminderTime,
        &NoteSearchQuery::reminderTimes, &NoteSearchQuery::negatedReminderTimes,
        QStringLiteral("reminderTime"));

    processNumericQueryItem(
        &NoteSearchQuery::hasAnyReminderDoneTime,
        &NoteSearchQuery::hasNegatedAnyReminderDoneTime,
        &NoteSearchQuery::reminderDoneTimes,
        &NoteSearchQuery::negatedReminderDoneTimes,
        QStringLiteral("reminderDoneTime"));

    // 7) Processing ToDo items
    toDoItemsInNoteSearchQueryToSql(noteSearchQuery, uniteOperator, strm);

    // 8) Processing encryption item
    encryptionItemInNoteSearchQueryToSql(noteSearchQuery, uniteOperator, strm);

    // 9) Processing content search terms

    if (noteSearchQuery.hasAnyContentSearchTerms()) {
        ErrorString error;
        if (!contentSearchTermsToSqlQueryPart(noteSearchQuery, strm, error)) {
            extendError(error, {}, {});
            QNWARNING("local_storage::sql::utils", errorDescription);
            return {};
        }
    }

    strm.setDevice(nullptr);

    // 10) Removing trailing unite operator from the SQL string (if any)

    const QString spareEnd =
        QStringLiteral(" ") + uniteOperator + QStringLiteral(" ");

    if (queryString.endsWith(spareEnd)) {
        queryString.chop(spareEnd.size());
    }

    // 11) See whether we should bother anything regarding tags or resources

    QString sqlPostfix = QStringLiteral("FROM NoteFTS ");
    if (queryString.contains(QStringLiteral("NoteTags"))) {
        sqlPrefix += QStringLiteral(", NoteTags.localTag ");
        sqlPostfix += QStringLiteral(
            "LEFT OUTER JOIN NoteTags ON "
            "NoteFTS.localUid = NoteTags.localNote ");
    }

    if (queryString.contains(QStringLiteral("NoteResources"))) {
        sqlPrefix += QStringLiteral(", NoteResources.localResource ");
        sqlPostfix += QStringLiteral(
            "LEFT OUTER JOIN NoteResources ON "
            "NoteFTS.localUid = NoteResources.localNote ");
    }

    // 12) Finalize the query composed of parts

    sqlPrefix += sqlPostfix;
    sqlPrefix += QStringLiteral("WHERE ");
    queryString.prepend(sqlPrefix);

    QNTRACE(
        "local_storage::sql::utils",
        "Prepared SQL query for note search: " << queryString);
    return queryString;
}

} // namespace

QString noteGuidByLocalId(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString =
        QStringLiteral("SELECT guid FROM Notes WHERE localUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot get note guid by note local id: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":localUid"), noteLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot get note guid by note local id"), {});

    if (query.next()) {
        return query.record().value(0).toString();
    }

    return {};
}

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
            QStringLiteral(
                "Cannot determine notebook local id by notebook guid, failed "
                "to prepare query"),
            QString{});

        query.bindValue(QStringLiteral(":guid"), *notebookGuid);

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QStringLiteral(
                "Cannot determine notebook local id by notebook guid"),
            QString{});

        if (!query.next()) {
            errorDescription.setBase(
                QStringLiteral("Cannot find notebook local id for guid"));
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
        QStringLiteral(
            "Cannot determine notebook local id by note local id, failed "
            "to prepare query"),
        QString{});

    query.bindValue(QStringLiteral(":localUid"), note.localId());

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot determine notebook local id by note local id"),
        QString{});

    if (!query.next()) {
        errorDescription.setBase(
            QStringLiteral("Cannot find notebook local id for note local id"));
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
        QStringLiteral(
            "Cannot determine notebook guid by local id, failed to prepare "
            "query"),
        QString{});

    query.bindValue(QStringLiteral(":localUid"), localId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot determine notebook guid by local id"),
        QString{});

    if (!query.next()) {
        errorDescription.setBase(
            QStringLiteral("Cannot find notebook guid for local id"));
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
        QStringLiteral(
            "Cannot find note local id by guid: failed to prepare query"),
        {});

    query.bindValue(QStringLiteral(":guid"), noteGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot find note local id by guid"), {});

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

    const ErrorString errorPrefix{
        QStringLiteral("Can't find notes with the note search query")};

    ErrorString error;
    const QString queryString =
        noteSearchQueryToSql(noteSearchQuery, database, error);
    if (queryString.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        return {};
    }

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot list note local ids with note search query"),
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

QStringList noteResourceLocalIds(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT localResource FROM NoteResources WHERE localNote = :localNote");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral(
            "Cannot list resource local ids by note local id: failed to "
            "prepare query"),
        {});

    query.bindValue(QStringLiteral(":localNote"), noteLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot list resource local ids by note local id"), {});

    QStringList result;
    result.reserve(std::max(0, query.size()));
    while (query.next()) {
        result << query.value(0).toString();
    }

    return result;
}

int noteResourceCount(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "SELECT COUNT(localResource) FROM NoteResources "
        "WHERE localNote = :localNote");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot count resources by note local id: failed to "
                       "prepare query"),
        {});

    query.bindValue(QStringLiteral(":localNote"), noteLocalId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QStringLiteral("Cannot count resources by note local id"), {});

    if (!query.next()) {
        errorDescription.setBase(QStringLiteral(
            "Cannot count resources by note local id: no result from query"));
        return -1;
    }

    bool conversionResult = false;
    int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(QStringLiteral(
            "Cannot count resources by note local id: no result from query: "
            "failed to convert count to int"));
        QNWARNING("local_storage::sql::utils", errorDescription);
        return -1;
    }

    return count;
}

} // namespace quentier::local_storage::sql::utils
