/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNC_STATE_PERSISTENCE_MANAGER_H
#define LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNC_STATE_PERSISTENCE_MANAGER_H

#include <quentier/utility/Macros.h>
#include <quentier/utility/Linkage.h>
#include <quentier/types/Account.h>
#include <QObject>
#include <QHash>
#include <QString>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

class SyncStatePersistenceManager: public QObject
{
    Q_OBJECT
public:
    explicit SyncStatePersistenceManager(QObject * parent = Q_NULLPTR);

    void getPersistentSyncState(const Account & account, qint32 & userOwnDataUpdateCount,
                                qevercloud::Timestamp & userOwnDataSyncTime,
                                QHash<QString,qint32> & linkedNotebookUpdateCountsByLinkedNotebookGuid,
                                QHash<QString,qevercloud::Timestamp> & linkedNotebookSyncTimesByLinkedNotebookGuid);

    void persistSyncState(const Account & account,
                          const qint32 userOwnDataUpdateCount,
                          const qevercloud::Timestamp userOwnDataSyncTime,
                          const QHash<QString,qint32> & linkedNotebookUpdateCountsByLinkedNotebookGuid,
                          const QHash<QString,qevercloud::Timestamp> & linkedNotebookSyncTimesByLinkedNotebookGuid);

Q_SIGNALS:
    void notifyPersistentSyncStateUpdated(Account account, qint32 userOwnDataUpdateCount, qevercloud::Timestamp userOwnDataSyncTime,
                                          QHash<QString,qint32> linkedNotebookUpdateCountsByLinkedNotebookGuid,
                                          QHash<QString,qevercloud::Timestamp> linkedNotebookSyncTimesByLinkedNotebookGuid);

private:
    Q_DISABLE_COPY(SyncStatePersistenceManager);
};

} // namespace quentier

#endif // LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNC_STATE_PERSISTENCE_MANAGER_H
