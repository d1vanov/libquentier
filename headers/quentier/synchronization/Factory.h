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

#include <quentier/synchronization/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/utility/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/Fwd.h>

#include <QString>
#include <QtGlobal>

QT_BEGIN_NAMESPACE;

class QDir;
class QUrl;
class QWidget;

QT_END_NAMESPACE;

namespace quentier::synchronization {

[[nodiscard]] QUENTIER_EXPORT IAuthenticatorPtr createQEverCloudAuthenticator(
    QString consumerKey, QString consumerSecret, QString host,
    threading::QThreadPtr uiThread, QWidget * parentWidget = nullptr);

[[nodiscard]] QUENTIER_EXPORT ISynchronizerPtr createSynchronizer(
    const QUrl & serverUrl, const QDir & synchronizationPersistenceDir,
    IAuthenticatorPtr authenticator,
    ISyncStateStoragePtr syncStateStorage = nullptr,
    IKeychainServicePtr keychainService = nullptr,
    qevercloud::IRequestContextPtr ctx = nullptr,
    qevercloud::IRetryPolicyPtr retryPolicy = nullptr);

} // namespace quentier::synchronization
