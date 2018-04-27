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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H

#include "FakeNoteStore.h"
#include "FakeUserStore.h"
#include "FakeAuthenticationManager.h"
#include <quentier/utility/Macros.h>
#include <quentier/types/Account.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/synchronization/SynchronizationManager.h>
#include <QObject>
#include <QScopedPointer>
#include <QThread>

namespace quentier {
namespace test {

class SynchronizationTester: public QObject
{
    Q_OBJECT
public:
    SynchronizationTester(QObject * parent = Q_NULLPTR);
    virtual ~SynchronizationTester();

private Q_SLOTS:
    void init();
    void cleanup();

    void initTestCase();
    void cleanupTestCase();

    void testSimpleRemoteToLocalFullSync();

private:
    void setUserOwnItemsToRemoteStorage();

private:
    Account                         m_testAccount;
    QThread *                       m_pLocalStorageManagerThread;
    LocalStorageManagerAsync *      m_pLocalStorageManagerAsync;
    FakeNoteStore *                 m_pFakeNoteStore;
    FakeUserStore *                 m_pFakeUserStore;
    FakeAuthenticationManager *     m_pFakeAuthenticationManager;
    SynchronizationManager *        m_pSynchronizationManager;
    bool                            m_detectedTestFailure;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
