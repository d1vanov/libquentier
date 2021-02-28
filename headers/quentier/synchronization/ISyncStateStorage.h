/*
 * Copyright 2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_I_SYNC_STATE_STORAGE_H
#define LIB_QUENTIER_SYNCHRONIZATION_I_SYNC_STATE_STORAGE_H

#include <quentier/synchronization/ForwardDeclarations.h>
#include <quentier/types/Account.h>
#include <quentier/utility/Linkage.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QHash>
#include <QObject>
#include <QString>

#include <memory>

namespace quentier {

/**
 * @brief The ISyncStateStorage interface represents the interface of a class
 * which stores sync state for given accounts persistently and provides access
 * to previously stores sync states
 */
class QUENTIER_EXPORT ISyncStateStorage : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief The ISyncState interface provides accessory methods to determine
     * the sync state for the account
     */
    class QUENTIER_EXPORT ISyncState : public Printable
    {
    public:
        virtual qint32 userDataUpdateCount() const = 0;
        virtual qevercloud::Timestamp userDataLastSyncTime() const = 0;
        virtual QHash<QString, qint32> linkedNotebookUpdateCounts() const = 0;

        virtual QHash<QString, qevercloud::Timestamp>
        linkedNotebookLastSyncTimes() const = 0;

        virtual QTextStream & print(QTextStream & strm) const override;
    };

    using ISyncStatePtr = std::shared_ptr<ISyncState>;

public:
    explicit ISyncStateStorage(QObject * parent = nullptr) : QObject(parent) {}

    virtual ~ISyncStateStorage() = default;

    virtual ISyncStatePtr getSyncState(const Account & account) = 0;

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

QUENTIER_EXPORT ISyncStateStoragePtr
newSyncStateStorage(QObject * parent = nullptr);

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_SYNC_STATE_STORAGE_H
