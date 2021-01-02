/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_H
#define LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_H

#include <quentier/utility/Linkage.h>

#include <QList>

namespace qevercloud {

class Tag;

} // namespace qevercloud

namespace quentier {

class ErrorString;

/**
 * Sorts the tags within the passed in list in such a manner that all parent
 * tags go before their child tags
 *
 * @param tagList           The input-output list of tags to be sorted
 *                          by parent-child relations
 * @param errorDescription  The textual description of the error if the sorting
 *                          could not be performed
 * @return                  True if the sorting was performed successfully,
 *                          false otherwise
 */
bool QUENTIER_EXPORT sortTagsByParentChildRelations(
    QList<qevercloud::Tag> & tagList, ErrorString & errorDescription);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_H
