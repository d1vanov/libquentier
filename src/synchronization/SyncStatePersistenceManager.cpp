/*
 * Copyright 2018-2019 Dmitry Ivanov
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

#include <quentier_private/synchronization/SyncStatePersistenceManager.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include "SynchronizationShared.h"

namespace quentier {

SyncStatePersistenceManager::SyncStatePersistenceManager(QObject * parent) :
    QObject(parent)
{}

void SyncStatePersistenceManager::getPersistentSyncState(
    const Account & account, qint32 & userOwnDataUpdateCount,
    qevercloud::Timestamp & userOwnDataSyncTime,
    QHash<QString,qint32> & linkedNotebookUpdateCountsByLinkedNotebookGuid,
    QHash<QString,qevercloud::Timestamp> & linkedNotebookSyncTimesByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("SyncStatePersistenceManager::getPersistentSyncState: ")
            << QStringLiteral("account = ") << account);

    userOwnDataUpdateCount = 0;
    userOwnDataSyncTime = 0;
    linkedNotebookUpdateCountsByLinkedNotebookGuid.clear();
    linkedNotebookSyncTimesByLinkedNotebookGuid.clear();

    ApplicationSettings appSettings(account, SYNCHRONIZATION_PERSISTENCE_NAME);
    const QString keyGroup = QStringLiteral("Synchronization/") +
                             account.evernoteHost() + QStringLiteral("/") +
                             QString::number(account.id()) + QStringLiteral("/") +
                             LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    QVariant lastUpdateCountVar =
        appSettings.value(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY);
    if (!lastUpdateCountVar.isNull())
    {
        bool conversionResult = false;
        userOwnDataUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read last update count from "
                                     "persistent application settings"));
            userOwnDataUpdateCount = 0;
        }
    }

    QVariant lastSyncTimeVar = appSettings.value(keyGroup + LAST_SYNC_TIME_KEY);
    if (!lastUpdateCountVar.isNull())
    {
        bool conversionResult = false;
        userOwnDataSyncTime = lastSyncTimeVar.toLongLong(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read last sync time from "
                                     "persistent application settings"));
            userOwnDataSyncTime = 0;
        }
    }

    int numLinkedNotebooksSyncParams =
        appSettings.beginReadArray(keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS);
    for(int i = 0; i < numLinkedNotebooksSyncParams; ++i)
    {
        appSettings.setArrayIndex(i);

        QString guid = appSettings.value(LINKED_NOTEBOOK_GUID_KEY).toString();
        if (guid.isEmpty()) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's guid "
                                     "from persistent application settings"));
            continue;
        }

        QVariant lastUpdateCountVar =
            appSettings.value(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY);
        bool conversionResult = false;
        qint32 lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's last "
                                     "update count from persistent application "
                                     "settings"));
            continue;
        }

        QVariant lastSyncTimeVar = appSettings.value(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY);
        conversionResult = false;
        qevercloud::Timestamp lastSyncTime =
            lastSyncTimeVar.toLongLong(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's last "
                                     "sync time from persistent application "
                                     "settings"));
            continue;
        }

        linkedNotebookUpdateCountsByLinkedNotebookGuid[guid] = lastUpdateCount;
        linkedNotebookSyncTimesByLinkedNotebookGuid[guid] = lastSyncTime;
    }
    appSettings.endArray();
}

void SyncStatePersistenceManager::persistSyncState(
    const Account & account, const qint32 userOwnDataUpdateCount,
    const qevercloud::Timestamp userOwnDataSyncTime,
    const QHash<QString,qint32> & linkedNotebookUpdateCountsByLinkedNotebookGuid,
    const QHash<QString,qevercloud::Timestamp> & linkedNotebookSyncTimesByLinkedNotebookGuid)
{
    ApplicationSettings appSettings(account, SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = QStringLiteral("Synchronization/") +
                             account.evernoteHost() + QStringLiteral("/") +
                             QString::number(account.id()) + QStringLiteral("/") +
                             LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");
    appSettings.setValue(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY, userOwnDataUpdateCount);
    appSettings.setValue(keyGroup + LAST_SYNC_TIME_KEY, userOwnDataSyncTime);

    int numLinkedNotebooksSyncParams = linkedNotebookUpdateCountsByLinkedNotebookGuid.size();
    appSettings.beginWriteArray(keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS,
                                numLinkedNotebooksSyncParams);

    int counter = 0;
    auto updateCountEnd = linkedNotebookUpdateCountsByLinkedNotebookGuid.constEnd();
    auto syncTimeEnd = linkedNotebookSyncTimesByLinkedNotebookGuid.constEnd();
    for(auto updateCountIt = linkedNotebookUpdateCountsByLinkedNotebookGuid.constBegin();
        updateCountIt != updateCountEnd; ++updateCountIt)
    {
        const QString & guid = updateCountIt.key();
        auto syncTimeIt = linkedNotebookSyncTimesByLinkedNotebookGuid.find(guid);
        if (syncTimeIt == syncTimeEnd) {
            QNWARNING(QStringLiteral("Detected inconsistent last sync parameters "
                                     "for one of linked notebooks: last update "
                                     "count is present while last sync time is "
                                     "not, skipping writing the persistent "
                                     "settings entry for this linked notebook"));
            continue;
        }

        appSettings.setArrayIndex(counter);
        appSettings.setValue(LINKED_NOTEBOOK_GUID_KEY, guid);
        appSettings.setValue(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY, updateCountIt.value());
        appSettings.setValue(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY, syncTimeIt.value());
        QNTRACE(QStringLiteral("Persisted last sync parameters for a linked ")
                << QStringLiteral("notebook: guid = ") << guid
                << QStringLiteral(", update count = ") << updateCountIt.value()
                << QStringLiteral(", sync time = ")
                << printableDateTimeFromTimestamp(syncTimeIt.value()));

        ++counter;
    }

    appSettings.endArray();

    QNTRACE(QStringLiteral("Wrote ") << counter
            << QStringLiteral(" last sync params entries for linked notebooks"));

    Q_EMIT notifyPersistentSyncStateUpdated(account, userOwnDataUpdateCount,
                                            userOwnDataSyncTime,
                                            linkedNotebookUpdateCountsByLinkedNotebookGuid,
                                            linkedNotebookSyncTimesByLinkedNotebookGuid);
}

} // namespace quentier
