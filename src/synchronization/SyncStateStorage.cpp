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

#include "SyncStateStorage.h"
#include "types/SyncState.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/QuentierCheckPtr.h>

namespace quentier::synchronization {

namespace {

const QString gSynchronizationPersistenceName =
    QStringLiteral("SynchronizationPersistence");

const QString gLastSyncParamsGroup = QStringLiteral("last_sync_params");
const QString gLastSyncUpdateCount = QStringLiteral("last_sync_update_count");
const QString gLastSyncTime = QStringLiteral("last_sync_time");
const QString gLinkedNotebookGuid = QStringLiteral("linked_notebook_guid");

const QString gLastSyncLinkedNotebookParams =
    QStringLiteral("last_sync_linked_notebooks_params");

const QString gLinkedNotebookLastUpdateCount =
    QStringLiteral("linked_notebook_last_update_count");

const QString gLinkedNotebookLastSyncTime =
    QStringLiteral("linked_notebook_last_sync_time");

} // namespace

SyncStateStorage::SyncStateStorage(QObject * parent) : ISyncStateStorage(parent)
{}

ISyncStatePtr SyncStateStorage::getSyncState(
    const Account & account)
{
    QNDEBUG(
        "synchronization::SyncStateStorage",
        "SyncStateStorage::getPersistentSyncState: account = " << account);

    auto syncState = std::make_shared<synchronization::SyncState>();

    ApplicationSettings appSettings{account, gSynchronizationPersistenceName};

    const QString keyGroup = QStringLiteral("Synchronization/") +
        account.evernoteHost() + QStringLiteral("/") +
        QString::number(account.id()) + QStringLiteral("/") +
        gLastSyncParamsGroup + QStringLiteral("/");

    const QVariant lastUpdateCountVar =
        appSettings.value(keyGroup + gLastSyncUpdateCount);

    if (!lastUpdateCountVar.isNull()) {
        bool conversionResult = false;

        const int userDataUpdateCount =
            lastUpdateCountVar.toInt(&conversionResult);

        if (conversionResult) {
            syncState->m_userDataUpdateCount = userDataUpdateCount;
        }
        else {
            QNWARNING(
                "synchronization::SyncStateStorage",
                "Couldn't read last update count from persistent application "
                    << "settings");
        }
    }

    const QVariant lastSyncTimeVar =
        appSettings.value(keyGroup + gLastSyncTime);

    if (!lastUpdateCountVar.isNull()) {
        bool conversionResult = false;

        qevercloud::Timestamp userDataLastSyncTime =
            lastSyncTimeVar.toLongLong(&conversionResult);

        if (conversionResult) {
            syncState->m_userDataLastSyncTime = userDataLastSyncTime;
        }
        else {
            QNWARNING(
                "synchronization::SyncStateStorage",
                "Couldn't read last sync time from persistent application "
                    << "settings");
        }
    }

    const int numLinkedNotebooksSyncParams = appSettings.beginReadArray(
        keyGroup + gLastSyncLinkedNotebookParams);

    for (int i = 0; i < numLinkedNotebooksSyncParams; ++i) {
        appSettings.setArrayIndex(i);

        const QString guid =
            appSettings.value(gLinkedNotebookGuid).toString();

        if (guid.isEmpty()) {
            QNWARNING(
                "synchronization::SyncStateStorage",
                "Couldn't read linked notebook's guid from persistent "
                    << "application settings");
            continue;
        }

        const QVariant lastUpdateCountVar =
            appSettings.value(gLinkedNotebookLastUpdateCount);

        bool conversionResult = false;
        const int lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(
                "synchronization::SyncStateStorage",
                "Couldn't read linked notebook's last update count from "
                    << "persistent application settings");
            continue;
        }

        const QVariant lastSyncTimeVar =
            appSettings.value(gLinkedNotebookLastSyncTime);

        conversionResult = false;

        const qevercloud::Timestamp lastSyncTime =
            lastSyncTimeVar.toLongLong(&conversionResult);

        if (!conversionResult) {
            QNWARNING(
                "synchronization::SyncStateStorage",
                "Couldn't read linked notebook's last sync time from "
                    << "persistent application settings");
            continue;
        }

        syncState->m_linkedNotebookUpdateCounts[guid] = lastUpdateCount;
        syncState->m_linkedNotebookLastSyncTimes[guid] = lastSyncTime;
    }
    appSettings.endArray();

    return syncState;
}

void SyncStateStorage::setSyncState(
    const Account & account, ISyncStatePtr syncState)
{
    QUENTIER_CHECK_PTR("synchronization: state_storage", syncState.get())

    ApplicationSettings appSettings{account, gSynchronizationPersistenceName};

    const QString keyGroup = QStringLiteral("Synchronization/") +
        account.evernoteHost() + QStringLiteral("/") +
        QString::number(account.id()) + QStringLiteral("/") +
        gLastSyncParamsGroup + QStringLiteral("/");

    appSettings.setValue(
        keyGroup + gLastSyncUpdateCount,
        syncState->userDataUpdateCount());

    appSettings.setValue(
        keyGroup + gLastSyncTime, syncState->userDataLastSyncTime());

    const auto updateCountsByLinkedNotebookGuid =
        syncState->linkedNotebookUpdateCounts();

    const auto lastSyncTimesByLinkedNotebookGuid =
        syncState->linkedNotebookLastSyncTimes();

    const int numLinkedNotebooksSyncParams =
        updateCountsByLinkedNotebookGuid.size();

    appSettings.beginWriteArray(
        keyGroup + gLastSyncLinkedNotebookParams,
        numLinkedNotebooksSyncParams);

    int counter = 0;
    for (auto it:
         qevercloud::toRange(::qAsConst(updateCountsByLinkedNotebookGuid))) {
        const QString & guid = it.key();

        const auto syncTimeIt =
            lastSyncTimesByLinkedNotebookGuid.constFind(guid);

        if (syncTimeIt == lastSyncTimesByLinkedNotebookGuid.constEnd()) {
            QNWARNING(
                "synchronization::SyncStateStorage",
                "Detected inconsistent last sync parameters for one of linked "
                    << "notebooks: last update count is present while last "
                    << "sync time is not, skipping writing the persistent "
                    << "settings entry for this linked notebook");
            continue;
        }

        appSettings.setArrayIndex(counter);
        appSettings.setValue(gLinkedNotebookGuid, guid);
        appSettings.setValue(gLinkedNotebookLastUpdateCount, it.value());

        appSettings.setValue(
            gLinkedNotebookLastSyncTime, syncTimeIt.value());

        QNTRACE(
            "synchronization::SyncStateStorage",
            "Persisted last sync parameters for a linked notebook: guid = "
                << guid << ", update count = " << it.value() << ", sync time = "
                << printableDateTimeFromTimestamp(syncTimeIt.value()));
        ++counter;
    }

    appSettings.endArray();

    QNTRACE(
        "synchronization::SyncStateStorage",
        "Wrote " << counter
                 << " last sync params entries for linked notebooks");

    Q_EMIT notifySyncStateUpdated(account, syncState);
}

} // namespace quentier::synchronization
