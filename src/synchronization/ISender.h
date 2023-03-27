/*
 * Copyright 2022 Dmitry Ivanov
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

#include <quentier/utility/cancelers/Fwd.h>

#include <synchronization/types/Fwd.h>

#include <qevercloud/types/LinkedNotebook.h>

#include <QFuture>
#include <QHash>

#include <memory>

namespace quentier::synchronization {

class ISender
{
public:
    virtual ~ISender() = default;

    class ICallback
    {
    public:
        virtual ~ICallback() = default;

        virtual void onUserOwnSendStatusUpdate(
            SendStatusPtr sendStatus) = 0;

        virtual void onLinkedNotebookSendStatusUpdate(
            const qevercloud::Guid & linkedNotebookGuid,
            SendStatusPtr sendStatus) = 0;
    };

    using ICallbackWeakPtr = std::weak_ptr<ICallback>;

    struct Result
    {
        // Send status for user own account
        SendStatusPtr userOwnResult;

        // Send statuses for modified data in linked notebooks
        QHash<qevercloud::Guid, SendStatusPtr> linkedNotebookResults;
    };

    [[nodiscard]] virtual QFuture<Result> send(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) = 0;
};

} // namespace quentier::synchronization
