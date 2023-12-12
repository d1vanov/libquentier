/*
 * Copyright 2020-2023 Dmitry Ivanov
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

#include <quentier/synchronization/types/Fwd.h>
#include <quentier/types/Account.h>
#include <quentier/utility/Linkage.h>

#include <QObject>

namespace quentier::synchronization {

/**
 * @brief The ISyncStateStorage interface represents the interface of a class
 * which stores sync state for given accounts persistently and provides access
 * to previously stores sync states
 */
class QUENTIER_EXPORT ISyncStateStorage : public QObject
{
    Q_OBJECT
protected:
    explicit ISyncStateStorage(QObject * parent = nullptr);

public:
    ~ISyncStateStorage() override;

    [[nodiscard]] virtual ISyncStatePtr getSyncState(
        const Account & account) = 0;

    virtual void setSyncState(
        const Account & account, ISyncStatePtr syncState) = 0;

Q_SIGNALS:
    /**
     * Classes implementing ISyncStateStorage interface are expected to emit
     * notifySyncStateUpdated signal each time when sync state for
     * the corresponding account is updated
     */
    void notifySyncStateUpdated(Account account, ISyncStatePtr syncState);
};

} // namespace quentier::synchronization
