/*
 * Copyright 2024-2025 Dmitry Ivanov
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

#include <synchronization/AccountSyncPersistenceDirProvider.h>

#include <quentier/utility/StandardPaths.h>

#include <QByteArray>
#include <QTemporaryDir>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

class AccountSyncPersistenceDirProviderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_originalPersistenceStoragePath =
            qgetenv(LIBQUENTIER_PERSISTENCE_STORAGE_PATH);

        qputenv(
            LIBQUENTIER_PERSISTENCE_STORAGE_PATH,
            m_temporaryDir.path().toLocal8Bit());
    }

    void TearDown() override
    {
        qputenv(
            LIBQUENTIER_PERSISTENCE_STORAGE_PATH,
            m_originalPersistenceStoragePath);
    }

protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    QByteArray m_originalPersistenceStoragePath;
    QTemporaryDir m_temporaryDir;
};

TEST_F(AccountSyncPersistenceDirProviderTest, syncPersistenceDir)
{
    AccountSyncPersistenceDirProvider provider;
    const auto dir = provider.syncPersistenceDir(m_account);

    EXPECT_EQ(
        dir.absolutePath(),
        m_temporaryDir.path() + QStringLiteral("/EvernoteAccounts/") +
            m_account.name() + QStringLiteral("_") + m_account.evernoteHost() +
            QStringLiteral("_") + QString::number(m_account.id()) +
            QStringLiteral("/sync_data"));
}

} // namespace quentier::synchronization::tests
