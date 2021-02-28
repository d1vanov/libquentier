/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include "../../utility/TagSortByParentChildRelationsHelpers.hpp"

#include <quentier/utility/Compat.h>
#include <quentier/utility/UidGenerator.h>

#include <QSet>
#include <QTextStream>

namespace quentier {
namespace test {

template <class T>
bool checkTagsOrder(const QList<T> & tags, QString & error)
{
    QSet<QString> encounteredTagGuids;

    for (const auto & tag: ::qAsConst(tags)) {
        if (Q_UNLIKELY(!tagHasGuid(tag))) {
            continue;
        }

        QString guid = tagGuid(tag);
        Q_UNUSED(encounteredTagGuids.insert(guid))

        QString parentGuid = tagParentGuid(tag);
        if (parentGuid.isEmpty()) {
            continue;
        }

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

void tagListToQEverCloudTagList(
    const QList<Tag> & inputTags, QList<qevercloud::Tag> & outputTags)
{
    outputTags.clear();
    outputTags.reserve(inputTags.size());

    for (const auto & tag: ::qAsConst(inputTags)) {
        outputTags << tag.qevercloudTag();
    }
}

bool tagSortByParentChildRelationsTest(QString & error)
{
    Tag firstTag;
    firstTag.setName(QStringLiteral("First tag"));
    firstTag.setGuid(UidGenerator::Generate());

    Tag secondTag;
    secondTag.setName(QStringLiteral("Second tag"));
    secondTag.setGuid(UidGenerator::Generate());

    Tag thirdTag;
    thirdTag.setName(QStringLiteral("Third tag"));
    thirdTag.setGuid(UidGenerator::Generate());

    Tag fourthTag;
    fourthTag.setName(QStringLiteral("Fourth tag"));
    fourthTag.setGuid(UidGenerator::Generate());
    fourthTag.setParentGuid(firstTag.guid());
    fourthTag.setParentLocalUid(firstTag.localUid());

    Tag fifthTag;
    fifthTag.setName(QStringLiteral("Fifth tag"));
    fifthTag.setGuid(UidGenerator::Generate());
    fifthTag.setParentGuid(firstTag.guid());
    fifthTag.setParentLocalUid(firstTag.localUid());

    Tag sixthTag;
    sixthTag.setName(QStringLiteral("Sixth tag"));
    sixthTag.setGuid(UidGenerator::Generate());
    sixthTag.setParentGuid(secondTag.guid());
    sixthTag.setParentLocalUid(secondTag.localUid());

    Tag seventhTag;
    seventhTag.setName(QStringLiteral("Seventh tag"));
    seventhTag.setGuid(UidGenerator::Generate());
    seventhTag.setParentGuid(secondTag.guid());
    seventhTag.setParentLocalUid(secondTag.localUid());

    Tag eighthTag;
    eighthTag.setName(QStringLiteral("Eighth tag"));
    eighthTag.setGuid(UidGenerator::Generate());
    eighthTag.setParentGuid(thirdTag.guid());
    eighthTag.setParentLocalUid(thirdTag.localUid());

    Tag ninethTag;
    ninethTag.setName(QStringLiteral("Ninth tag"));
    ninethTag.setGuid(UidGenerator::Generate());
    ninethTag.setParentGuid(fourthTag.guid());
    ninethTag.setParentLocalUid(fourthTag.localUid());

    Tag tenthTag;
    tenthTag.setName(QStringLiteral("Tenth tag"));
    tenthTag.setGuid(UidGenerator::Generate());
    tenthTag.setParentGuid(sixthTag.guid());
    tenthTag.setParentLocalUid(sixthTag.localUid());

    Tag eleventhTag;
    eleventhTag.setName(QStringLiteral("Eleventh tag"));
    eleventhTag.setGuid(UidGenerator::Generate());
    eleventhTag.setParentGuid(eighthTag.guid());
    eleventhTag.setParentLocalUid(eighthTag.localUid());

    Tag twelvethTag;
    twelvethTag.setName(QStringLiteral("Twelveth tag"));
    twelvethTag.setGuid(UidGenerator::Generate());
    twelvethTag.setParentGuid(tenthTag.guid());
    twelvethTag.setParentLocalUid(tenthTag.localUid());

    QList<Tag> tags;
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

    QList<qevercloud::Tag> qecTags;
    tagListToQEverCloudTagList(tags, qecTags);

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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
    if (!res) {
        return false;
    }

    // Check the list of parentless tags
    tags.clear();
    tags << firstTag;
    tags << secondTag;
    tags << thirdTag;

    tagListToQEverCloudTagList(tags, qecTags);

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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
    if (!res) {
        return false;
    }

    // Check the empty list of tags
    tags.clear();
    tagListToQEverCloudTagList(tags, qecTags);

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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
    if (!res) {
        return false;
    }

    // Check the single tag list
    tags.clear();
    tags << firstTag;

    tagListToQEverCloudTagList(tags, qecTags);

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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
    if (!res) {
        return false;
    }

    // Check the list consisting of a two parentless tags
    tags.clear();
    tags << firstTag;
    tags << secondTag;

    tagListToQEverCloudTagList(tags, qecTags);

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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
    if (!res) {
        return false;
    }

    // Check the list of two tags of which one is a parent and the other one
    // is a child
    tags.clear();
    tags << firstTag;
    tags << fourthTag;

    tagListToQEverCloudTagList(tags, qecTags);

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

    errorDescription.clear();
    res = sortTagsByParentChildRelations(qecTags, errorDescription);
    if (!res) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    res = checkTagsOrder(qecTags, error);
    if (!res) {
        return false;
    }

    return true;
}

} // namespace test
} // namespace quentier
