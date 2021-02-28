/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TYPES_DATA_SHARED_NOTEBOOK_DATA_H
#define LIB_QUENTIER_TYPES_DATA_SHARED_NOTEBOOK_DATA_H

#include <QSharedData>

#include <qt5qevercloud/QEverCloud.h>

namespace quentier {

class Q_DECL_HIDDEN SharedNotebookData final : public QSharedData
{
public:
    SharedNotebookData() = default;
    SharedNotebookData(const SharedNotebookData & other) = default;
    SharedNotebookData(SharedNotebookData && other) = default;

    SharedNotebookData(const qevercloud::SharedNotebook & other);
    SharedNotebookData(qevercloud::SharedNotebook && other);

    SharedNotebookData & operator=(const SharedNotebookData & other) = delete;
    SharedNotebookData & operator=(SharedNotebookData && other) = delete;

    virtual ~SharedNotebookData() = default;

public:
    qevercloud::SharedNotebook m_qecSharedNotebook;
    int m_indexInNotebook = -1;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_SHARED_NOTEBOOK_DATA_H
