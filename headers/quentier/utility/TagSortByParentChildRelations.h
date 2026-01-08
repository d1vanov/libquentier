/*
 * Copyright 2017-2025 Dmitry Ivanov
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

#include <quentier/types/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/Fwd.h>

#include <QList>

namespace quentier::utility {

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

} // namespace quentier::utility
