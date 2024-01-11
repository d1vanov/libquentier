/*
 * Copyright 2017-2024 Dmitry Ivanov
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

#include "TagSortByParentChildRelationsTest.h"

#include <quentier/types/ErrorString.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/Tag.h>

#include <QSet>
#include <QTextStream>

#include <utility>

namespace quentier::test {

namespace {

[[nodiscard]] bool checkTagsOrder(
    const QList<qevercloud::Tag> & tags, QString & error)
{
    QSet<QString> encounteredTagGuids;

    for (const auto & tag: std::as_const(tags)) {
        if (Q_UNLIKELY(!tag.guid())) {
            continue;
        }

        const QString guid = *tag.guid();
        Q_UNUSED(encounteredTagGuids.insert(guid))

        if (Q_UNLIKELY(!tag.parentGuid())) {
            continue;
        }

        const QString parentGuid = *tag.parentGuid();
        if (Q_UNLIKELY(guid == parentGuid)) {
            QTextStream strm(&error);
            strm << "Found tag which guid matches its parent guid: ";
            strm << tag;
            return false;
        }

        if (!encounteredTagGuids.contains(parentGuid)) {
            QTextStream strm(&error);
            strm << "Found a child tag before its parent: ";
            strm << tag;
            return false;
        }
    }

    return true;
}

} // namespace

bool tagSortByParentChildRelationsTest(QString & error)
{
    qevercloud::Tag firstTag;
    firstTag.setName(QStringLiteral("First tag"));
    firstTag.setGuid(UidGenerator::Generate());

    qevercloud::Tag secondTag;
    secondTag.setName(QStringLiteral("Second tag"));
    secondTag.setGuid(UidGenerator::Generate());

    qevercloud::Tag thirdTag;
    thirdTag.setName(QStringLiteral("Third tag"));
    thirdTag.setGuid(UidGenerator::Generate());

    qevercloud::Tag fourthTag;
    fourthTag.setName(QStringLiteral("Fourth tag"));
    fourthTag.setGuid(UidGenerator::Generate());
    fourthTag.setParentGuid(firstTag.guid());
    fourthTag.setParentTagLocalId(firstTag.localId());

    qevercloud::Tag fifthTag;
    fifthTag.setName(QStringLiteral("Fifth tag"));
    fifthTag.setGuid(UidGenerator::Generate());
    fifthTag.setParentGuid(firstTag.guid());
    fifthTag.setParentTagLocalId(firstTag.localId());

    qevercloud::Tag sixthTag;
    sixthTag.setName(QStringLiteral("Sixth tag"));
    sixthTag.setGuid(UidGenerator::Generate());
    sixthTag.setParentGuid(secondTag.guid());
    sixthTag.setParentTagLocalId(secondTag.localId());

    qevercloud::Tag seventhTag;
    seventhTag.setName(QStringLiteral("Seventh tag"));
    seventhTag.setGuid(UidGenerator::Generate());
    seventhTag.setParentGuid(secondTag.guid());
    seventhTag.setParentTagLocalId(secondTag.localId());

    qevercloud::Tag eighthTag;
    eighthTag.setName(QStringLiteral("Eighth tag"));
    eighthTag.setGuid(UidGenerator::Generate());
    eighthTag.setParentGuid(thirdTag.guid());
    eighthTag.setParentTagLocalId(thirdTag.localId());

    qevercloud::Tag ninethTag;
    ninethTag.setName(QStringLiteral("Ninth tag"));
    ninethTag.setGuid(UidGenerator::Generate());
    ninethTag.setParentGuid(fourthTag.guid());
    ninethTag.setParentTagLocalId(fourthTag.localId());

    qevercloud::Tag tenthTag;
    tenthTag.setName(QStringLiteral("Tenth tag"));
    tenthTag.setGuid(UidGenerator::Generate());
    tenthTag.setParentGuid(sixthTag.guid());
    tenthTag.setParentTagLocalId(sixthTag.localId());

    qevercloud::Tag eleventhTag;
    eleventhTag.setName(QStringLiteral("Eleventh tag"));
    eleventhTag.setGuid(UidGenerator::Generate());
    eleventhTag.setParentGuid(eighthTag.guid());
    eleventhTag.setParentTagLocalId(eighthTag.localId());

    qevercloud::Tag twelvethTag;
    twelvethTag.setName(QStringLiteral("Twelveth tag"));
    twelvethTag.setGuid(UidGenerator::Generate());
    twelvethTag.setParentGuid(tenthTag.guid());
    twelvethTag.setParentTagLocalId(tenthTag.localId());

    QList<qevercloud::Tag> tags;
    tags.reserve(12);
    tags << tenthTag;
    tags << firstTag;
    tags << twelvethTag;
    tags << thirdTag;
    tags << sixthTag;
    tags << secondTag;
    tags << eleventhTag;
    tags << fifthTag;
    tags << fourthTag;
    tags << seventhTag;
    tags << ninethTag;
    tags << eighthTag;

    ErrorString errorDescription;
    bool res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(tags, error);
    if (!res) {
        return false;
    }

    // Check the already sorted list
    errorDescription.clear();
    res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(tags, error);
    if (!res) {
        return false;
    }

    // Check the list of parentless tags
    tags.clear();
    tags << firstTag;
    tags << secondTag;
    tags << thirdTag;

    errorDescription.clear();
    res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(tags, error);
    if (!res) {
        return false;
    }

    // Check the empty list of tags
    tags.clear();

    errorDescription.clear();
    res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(tags, error);
    if (!res) {
        return false;
    }

    // Check the single tag list
    tags.clear();
    tags << firstTag;

    errorDescription.clear();
    res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(tags, error);
    if (!res) {
        return false;
    }

    // Check the list consisting of a two parentless tags
    tags.clear();
    tags << firstTag;
    tags << secondTag;

    errorDescription.clear();
    res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(tags, error);
    if (!res) {
        return false;
    }

    // Check the list of two tags of which one is a parent and the other one
    // is a child
    tags.clear();
    tags << firstTag;
    tags << fourthTag;

    errorDescription.clear();
    res = sortTagsByParentChildRelations(tags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    return checkTagsOrder(tags, error);
}

} // namespace quentier::test
