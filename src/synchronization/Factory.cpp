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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/synchronization/Factory.h>
#include <quentier/utility/IKeychainService.h>

#include <synchronization/AccountSynchronizerFactory.h>
#include <synchronization/AuthenticationInfoProvider.h>
#include <synchronization/Authenticator.h>
#include <synchronization/NoteStoreFactory.h>
#include <synchronization/Synchronizer.h>
#include <synchronization/UserInfoProvider.h>

#include <qevercloud/services/IUserStore.h>

namespace quentier::synchronization {

IAuthenticatorPtr createQEverCloudAuthenticator(
    QString consumerKey, QString consumerSecret, QUrl serverUrl,
    threading::QThreadPtr uiThread, QWidget * parentWidget)
{
    return std::make_shared<Authenticator>(
        std::move(consumerKey), std::move(consumerSecret), std::move(serverUrl),
        std::move(uiThread), parentWidget);
}

ISynchronizerPtr createSynchronizer(
    const QUrl & serverUrl, const QDir & synchronizationPersistenceDir,
    IAuthenticatorPtr authenticator, ISyncStateStoragePtr syncStateStorage,
    IKeychainServicePtr keychainService, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    if (!authenticator) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Cannot create synchronizer: authenticator is null")}};
    }

    if (!keychainService) {
        keychainService = newQtKeychainService();
    }

    QString host = serverUrl.host();

    auto userStoreUrl =
        QString::fromUtf8("%1%2/edam/user").arg(serverUrl.scheme(), host);

    auto userStore =
        qevercloud::newUserStore(std::move(userStoreUrl), ctx, retryPolicy);

    auto userInfoProvider =
        std::make_shared<UserInfoProvider>(std::move(userStore));

    auto noteStoreFactory = std::make_shared<NoteStoreFactory>();

    auto authenticationInfoProvider =
        std::make_shared<AuthenticationInfoProvider>(
            std::move(authenticator), std::move(keychainService),
            std::move(userInfoProvider), std::move(noteStoreFactory),
            std::move(ctx), std::move(retryPolicy), std::move(host));

    auto accountSynchronizerFactory =
        std::make_shared<AccountSynchronizerFactory>(
            std::move(syncStateStorage), authenticationInfoProvider,
            synchronizationPersistenceDir);

    return std::make_shared<Synchronizer>(
        std::move(accountSynchronizerFactory),
        std::move(authenticationInfoProvider));
}

} // namespace quentier::synchronization
