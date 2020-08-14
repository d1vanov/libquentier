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

#ifndef LIB_QUENTIER_TYPES_DATA_LINKED_NOTEBOOK_DATA_H
#define LIB_QUENTIER_TYPES_DATA_LINKED_NOTEBOOK_DATA_H

#include <quentier/types/ErrorString.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedData>

namespace quentier {

class Q_DECL_HIDDEN LinkedNotebookData final : public QSharedData
{
public:
    LinkedNotebookData() = default;
    LinkedNotebookData(const LinkedNotebookData & other) = default;
    LinkedNotebookData(LinkedNotebookData && other) = default;

    LinkedNotebookData & operator=(const LinkedNotebookData & other) = delete;
    LinkedNotebookData & operator=(LinkedNotebookData && other) = delete;

    LinkedNotebookData(const qevercloud::LinkedNotebook & other);
    LinkedNotebookData(qevercloud::LinkedNotebook && other);

    virtual ~LinkedNotebookData() = default;

    bool operator==(const LinkedNotebookData & other) const;
    bool operator!=(const LinkedNotebookData & other) const;

    void clear();
    bool checkParameters(ErrorString & errorDescription) const;

public:
    qevercloud::LinkedNotebook m_qecLinkedNotebook;
    bool m_isDirty = true;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_LINKED_NOTEBOOK_DATA_H
