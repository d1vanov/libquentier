/*
 * Copyright 2023 Dmitry Ivanov
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

#include "INotebookFinder.h"

#include <quentier/local_storage/Fwd.h>

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QMutex>

#include <memory>

namespace quentier::synchronization {

class NotebookFinder final :
    public INotebookFinder,
    public std::enable_shared_from_this<NotebookFinder>
{
public:
    explicit NotebookFinder(local_storage::ILocalStoragePtr localStorage);

    // Method which sets up the connections with local storage. In the ideal
    // world it would not be required as stuff like this belongs
    // to the constructor but unfortunately weak_from_this() is not available
    // in the constructor.
    // Needs to be called exactly once after constructing the object.
    void init();

    ~NotebookFinder() override;

public: // INotebookFinder
    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByNoteLocalId(const QString & noteLocalId) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByLocalId(const QString & notebookLocalId) override;

private:
    [[nodiscard]] QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByNoteLocalIdImpl(const QString & noteLocalId);

    void removeFutureByNoteLocalId(const QString & noteLocalId);

    void removeFuturesByNotebookLocalId(const QString & notebookLocalId);

private:
    const local_storage::ILocalStoragePtr m_localStorage;

    QHash<QString, QFuture<std::optional<qevercloud::Notebook>>>
        m_notebooksByNoteLocalId;

    QMutex m_notebooksByNoteLocalIdMutex;

    QHash<QString, QFuture<std::optional<qevercloud::Notebook>>>
        m_notebooksByLocalId;

    QMutex m_notebooksByLocalIdMutex;

    QList<QMetaObject::Connection> m_localStorageConnections;
};

} // namespace quentier::synchronization
