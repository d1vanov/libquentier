/*
 * Copyright 2017 Dmitry Ivanov
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

#include <quentier/types/ErrorString.h>
#include <quentier/types/Tag.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

#include <QList>

namespace quentier {

/**
 * Sorts the tags within the passed in list in such a manner that all parent tags
 * go before their child tags
 * @param tagList - the input-output list of tags to be sorted by parent-child relations
 * @param errorDescription - the textual description of the error if the sorting could not be performed
 * @return true if the sorting was performed successfully, false otherwise
 */
bool QUENTIER_EXPORT sortTagsByParentChildRelations(QList<qevercloud::Tag> & tagList, ErrorString & errorDescription);

/**
 * Sorts the tags within the passed in list in such a manner that all parent tags
 * go before their child tags. NOTE: the sorting logics uses guids and parent tag guids, not local uids and parent tag
 * local uids!
 * @param tagList - the input-output list of tags to be sorted by parent-child relations
 * @param errorDescription - the textual description of the error if the sorting could not be performed
 * @return true if the sorting was performed successfully, false otherwise
 */
bool QUENTIER_EXPORT sortTagsByParentChildRelations(QList<Tag> & tagList, ErrorString errorDescription);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_TAG_SORT_BY_PARENT_CHILD_RELATIONS_H
