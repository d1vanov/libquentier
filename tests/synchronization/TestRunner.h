/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/types/Account.h>

#include <qevercloud/Fwd.h>

#include <QObject>
#include <QTemporaryDir>

#include <memory>
#include <optional>

namespace quentier::synchronization::tests {

class TestRunner : public QObject
{
    Q_OBJECT
public:
    struct Options
    {
        bool useNetworkTransportLayer = false;
    };

    explicit TestRunner(const Options & options, QObject * parent = nullptr);
    ~TestRunner() override;

private Q_SLOTS:
    void init();
    void cleanup();

    void initTestCase();
    void cleanupTestCase();

    void runTestScenario();
    void runTestScenario_data();

private:
    void createBaseRequestContext();

private:
    const FakeAuthenticatorPtr m_fakeAuthenticator;
    const FakeKeychainServicePtr m_fakeKeychainService;

    Account m_testAccount;
    std::optional<QTemporaryDir> m_tempDir;
    local_storage::ILocalStoragePtr m_localStorage;

    FakeNoteStoreBackend * m_noteStoreBackend = nullptr;
    FakeUserStoreBackend * m_userStoreBackend = nullptr;

    bool m_useTestServers = false;
    NoteStoreServer * m_noteStoreServer = nullptr;
    UserStoreServer * m_userStoreServer = nullptr;

    INoteStoreFactoryPtr m_noteStoreFactory;
    IUserStoreFactoryPtr m_userStoreFactory;

    SyncEventsCollector * m_syncEventsCollector = nullptr;
    std::shared_ptr<FakeSyncStateStorage> m_fakeSyncStateStorage;
    qevercloud::IRequestContextPtr m_ctx;
};

} // namespace quentier::synchronization::tests
