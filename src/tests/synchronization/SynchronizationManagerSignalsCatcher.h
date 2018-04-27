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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Account.h>
#include <QObject>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SynchronizationManager)

class SynchronizationManagerSignalsCatcher: public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerSignalsCatcher(SynchronizationManager * pSynchronizationManager,
                                         QObject * parent = Q_NULLPTR);

    bool receivedStartedSignal() const { return m_receivedStartedSignal; }
    // TODO: continue from here

private Q_SLOTS:
    void onStart();
    void onStop();
    void onFailure(ErrorString errorDescription);
    void onFinish(Account account, bool somethingDownloaded, bool somethingSent);
    void onAuthenticationRevoked(bool success, ErrorString errorDescription, qevercloud::UserID userId);
    void onAuthenticationFinished(bool success, ErrorString errorDescription, Account account);
    void onRemoteToLocalSyncStopped();
    void onSendLocalChangesStopped();
    void onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges();
    void onDetectedConflictDuringLocalChangesSending();
    void onRateLimitExceeded(qint32 rateLimitSeconds);
    void onRemoteToLocalSyncDone(bool somethingDownloaded);
    void onSyncChunksDownloaded();
    void onLinkedNotebookSyncChunksDownloaded();
    void onPreparedDirtyObjectsForSending();
    void onPreparedLinkedNotebookDirtyObjectsForSending();

private:
    bool            m_receivedStartedSignal;
    bool            m_receivedStoppedSignal;

    bool            m_receivedFailedSignal;
    ErrorString     m_failureErrorDescription;

    bool            m_receivedFinishedSignal;
    Account         m_finishedAccount;
    bool            m_finishedSomethingDownloaded;
    bool            m_finishedSomethingSent;

    bool            m_receivedAuthenticationRevokedSignal;
    bool            m_authenticationRevokeSuccess;
    ErrorString     m_authenticationRevokeErrorDescription;
    qevercloud::UserID  m_authenticationRevokeUserId;

    bool            m_receivedAuthenticationFinishedSignal;
    bool            m_authenticationSuccess;
    ErrorString     m_authenticationErrorDescription;
    Account         m_authenticationAccount;

    bool            m_receivedRemoteToLocalSyncStopped;
    bool            m_receivedSendLocalChangesStopped;
    bool            m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges;
    bool            m_receivedDetectedConflictDuringLocalChangesSending;

    bool            m_receivedRateLimitExceeded;
    qint32          m_rateLimitSeconds;

    bool            m_receivedRemoteToLocalSyncDone;
    bool            m_remoteToLocalSyncDoneSomethingDownloaded;

    bool            m_receivedSyncChunksDownloaded;
    bool            m_receivedLinkedNotebookSyncChunksDownloaded;

    bool            m_receivedPreparedDirtyObjectsForSending;
    bool            m_receivedPreparedLinkedNotebookDirtyObjectsForSending;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H
