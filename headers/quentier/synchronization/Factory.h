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

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/Fwd.h>

#include <QString>
#include <QUrl>
#include <QtGlobal>

class QDir;
class QWidget;

namespace quentier::synchronization {

[[nodiscard]] QUENTIER_EXPORT IAuthenticatorPtr createQEverCloudAuthenticator(
    QString consumerKey, QString consumerSecret, QUrl serverUrl,
    threading::QThreadPtr uiThread, QWidget * parentWidget = nullptr);

[[nodiscard]] QUENTIER_EXPORT ISynchronizerPtr createSynchronizer(
    const QUrl & userStoreUrl, const QDir & synchronizationPersistenceDir,
    IAuthenticatorPtr authenticator,
    ISyncStateStoragePtr syncStateStorage = nullptr,
    IKeychainServicePtr keychainService = nullptr,
    qevercloud::IRequestContextPtr ctx = nullptr,
    qevercloud::IRetryPolicyPtr retryPolicy = nullptr);

[[nodiscard]] QUENTIER_EXPORT ISyncConflictResolverPtr
    createSimpleSyncConflictResolver(
        local_storage::ILocalStoragePtr localStorage);

[[nodiscard]] QUENTIER_EXPORT ISyncStateStoragePtr
    createSyncStateStorage(QObject * parent = nullptr);

} // namespace quentier::synchronization
