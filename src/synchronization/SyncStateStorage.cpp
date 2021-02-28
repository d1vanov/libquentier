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

#include "SyncStateStorage.h"

#include "SynchronizationShared.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/QuentierCheckPtr.h>

namespace quentier {

SyncStateStorage::SyncStateStorage(QObject * parent) : ISyncStateStorage(parent)
{}

ISyncStateStorage::ISyncStatePtr SyncStateStorage::getSyncState(
    const Account & account)
{
    QNDEBUG(
        "synchronization:state_storage",
        "SyncStateStorage::getPersistentSyncState: account = " << account);

    auto syncState = std::make_shared<SyncState>();

    ApplicationSettings appSettings(account, SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = QStringLiteral("Synchronization/") +
        account.evernoteHost() + QStringLiteral("/") +
        QString::number(account.id()) + QStringLiteral("/") +
        LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    QVariant lastUpdateCountVar =
        appSettings.value(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY);

    if (!lastUpdateCountVar.isNull()) {
        bool conversionResult = false;
        int userDataUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (conversionResult) {
            syncState->m_userDataUpdateCount = userDataUpdateCount;
        }
        else {
            QNWARNING(
                "synchronization:state_storage",
                "Couldn't read last "
                    << "update count from persistent application settings");
        }
    }

    QVariant lastSyncTimeVar = appSettings.value(keyGroup + LAST_SYNC_TIME_KEY);
    if (!lastUpdateCountVar.isNull()) {
        bool conversionResult = false;

        qevercloud::Timestamp userDataLastSyncTime =
            lastSyncTimeVar.toLongLong(&conversionResult);

        if (conversionResult) {
            syncState->m_userDataLastSyncTime = userDataLastSyncTime;
        }
        else {
            QNWARNING(
                "synchronization:state_storage",
                "Couldn't read last "
                    << "sync time from persistent application settings");
        }
    }

    int numLinkedNotebooksSyncParams = appSettings.beginReadArray(
        keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS);

    for (int i = 0; i < numLinkedNotebooksSyncParams; ++i) {
        appSettings.setArrayIndex(i);

        QString guid = appSettings.value(LINKED_NOTEBOOK_GUID_KEY).toString();
        if (guid.isEmpty()) {
            QNWARNING(
                "synchronization:state_storage",
                "Couldn't read linked "
                    << "notebook's guid from persistent application settings");
            continue;
        }

        QVariant lastUpdateCountVar =
            appSettings.value(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY);

        bool conversionResult = false;
        int lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(
                "synchronization:state_storage",
                "Couldn't read linked "
                    << "notebook's last update count from persistent "
                       "application "
                    << "settings");
            continue;
        }

        QVariant lastSyncTimeVar =
            appSettings.value(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY);

        conversionResult = false;

        qevercloud::Timestamp lastSyncTime =
            lastSyncTimeVar.toLongLong(&conversionResult);

        if (!conversionResult) {
            QNWARNING(
                "synchronization:state_storage",
                "Couldn't read linked "
                    << "notebook's last sync time from persistent application "
                    << "settings");
            continue;
        }

        syncState->m_updateCountsByLinkedNotebookGuid[guid] = lastUpdateCount;
        syncState->m_lastSyncTimesByLinkedNotebookGuid[guid] = lastSyncTime;
    }
    appSettings.endArray();

    return syncState;
}

void SyncStateStorage::setSyncState(
    const Account & account, ISyncStatePtr syncState)
{
    QUENTIER_CHECK_PTR("synchronization: state_storage", syncState.get())

    ApplicationSettings appSettings(account, SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = QStringLiteral("Synchronization/") +
        account.evernoteHost() + QStringLiteral("/") +
        QString::number(account.id()) + QStringLiteral("/") +
        LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    appSettings.setValue(
        keyGroup + LAST_SYNC_UPDATE_COUNT_KEY,
        syncState->userDataUpdateCount());

    appSettings.setValue(
        keyGroup + LAST_SYNC_TIME_KEY, syncState->userDataLastSyncTime());

    const auto updateCountsByLinkedNotebookGuid =
        syncState->linkedNotebookUpdateCounts();

    const auto lastSyncTimesByLinkedNotebookGuid =
        syncState->linkedNotebookLastSyncTimes();

    int numLinkedNotebooksSyncParams = updateCountsByLinkedNotebookGuid.size();

    appSettings.beginWriteArray(
        keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS,
        numLinkedNotebooksSyncParams);

    int counter = 0;
    for (auto it:
         qevercloud::toRange(::qAsConst(updateCountsByLinkedNotebookGuid))) {
        const QString & guid = it.key();
        auto syncTimeIt = lastSyncTimesByLinkedNotebookGuid.constFind(guid);

        if (syncTimeIt == lastSyncTimesByLinkedNotebookGuid.constEnd()) {
            QNWARNING(
                "synchronization:state_storage",
                "Detected inconsistent "
                    << "last sync parameters for one of linked notebooks: last "
                    << "update count is present while last sync time is not, "
                    << "skipping writing the persistent settings entry for "
                       "this "
                    << "linked notebook");
            continue;
        }

        appSettings.setArrayIndex(counter);
        appSettings.setValue(LINKED_NOTEBOOK_GUID_KEY, guid);
        appSettings.setValue(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY, it.value());

        appSettings.setValue(
            LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY, syncTimeIt.value());

        QNTRACE(
            "synchronization:state_storage",
            "Persisted last sync "
                << "parameters for a linked notebook: guid = " << guid
                << ", update count = " << it.value() << ", sync time = "
                << printableDateTimeFromTimestamp(syncTimeIt.value()));
        ++counter;
    }

    appSettings.endArray();

    QNTRACE(
        "synchronization:state_storage",
        "Wrote " << counter
                 << " last sync params entries for linked notebooks");

    Q_EMIT notifySyncStateUpdated(account, syncState);
}

} // namespace quentier
