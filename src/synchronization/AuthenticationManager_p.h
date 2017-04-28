/*
 * Copyright 2017 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
#define LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H

#include <quentier/synchronization/AuthenticationManager.h>
#include "NoteStore.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloudOAuth.h>
#else
#include <qt4qevercloud/QEverCloudOAuth.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5keychain/keychain.h>
#else
#include <qtkeychain/keychain.h>
#endif

#include <QSharedPointer>

namespace quentier {

class AuthenticationManagerPrivate: public QObject
{
    Q_OBJECT
public:
    explicit AuthenticationManagerPrivate(const QString & consumerKey, const QString & consumerSecret,
                                          const QString & host, const Account & account, QObject * parent = Q_NULLPTR);

    bool isInProgress() const;

Q_SIGNALS:
    void sendAuthenticationTokenAndShardId(QString authToken, QString shardId, qevercloud::Timestamp expirationTime);
    void sendAuthenticationTokensForLinkedNotebooks(QHash<QString,QPair<QString,QString> > authenticationTokensAndShardIdsByLinkedNotebookGuids,
                                                    QHash<QString,qevercloud::Timestamp> authenticatonTokenExpirationTimesByLinkedNotebookGuids);
    void authenticationRevokeReply(bool success, ErrorString errorDescription, qevercloud::UserID userId);
    void notifyError(ErrorString errorDescription);

public Q_SLOTS:
    void onRequestAuthenticationToken();
    void onRequestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString,QString> > linkedNotebookGuidsAndShareKeys);
    void onRequestAuthenticationRevoke(qevercloud::UserID userId);

private Q_SLOTS:
    void onOAuthResult(bool result);
    void onOAuthSuccess();
    void onOAuthFailure();

    void onKeychainJobFinished(QKeychain::Job * pJob);

private:
    void createConnections();

    bool validAuthentication() const;
    bool checkIfTimestampIsAboutToExpireSoon(const qevercloud::Timestamp timestamp) const;
    void authenticateToLinkedNotebooks();

    void onReadAuthTokenFinished();
    void onReadShardIdFinished();
    void onWriteAuthTokenFinished();
    void onWriteShardIdFinished();
    void onDeleteAuthTokenFinished();
    void onDeleteShardIdFinished();

    struct AuthContext
    {
        enum type {
            Blank = 0,
            Request,
            AuthToLinkedNotebooks,
        };
    };

    void launchOAuth();
    void authenticateImpl(const AuthContext::type authContext);
    void finalizeAuthentication();

    void launchStoreOAuthResult(const qevercloud::EvernoteOAuthWebView::OAuthResult & result);
    void finalizeStoreOAuthResult();

    void finalizeRevokeAuthentication();

private:
    Q_DISABLE_COPY(AuthenticationManagerPrivate)

private:
    QString         m_consumerKey;
    QString         m_consumerSecret;
    QString         m_host;
    Account         m_account;

    qevercloud::EvernoteOAuthWebView                m_OAuthWebView;
    qevercloud::EvernoteOAuthWebView::OAuthResult   m_OAuthResult;
    bool                                            m_authenticationInProgress;

    NoteStore                               m_noteStore;
    AuthContext::type                       m_authContext;

    QHash<QString,QPair<QString,QString> >  m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid;
    QHash<QString,qevercloud::Timestamp>    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid;
    QVector<QPair<QString,QString> >        m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth;

    int                                     m_authenticateToLinkedNotebooksPostponeTimerId;
    QKeychain::ReadPasswordJob              m_readAuthTokenJob;
    QKeychain::ReadPasswordJob              m_readShardIdJob;
    bool                                    m_readingAuthToken;
    bool                                    m_readingShardId;

    QKeychain::WritePasswordJob             m_writeAuthTokenJob;
    QKeychain::WritePasswordJob             m_writeShardIdJob;
    bool                                    m_writingAuthToken;
    bool                                    m_writingShardId;

    qevercloud::EvernoteOAuthWebView::OAuthResult   m_writtenOAuthResult;

    QKeychain::DeletePasswordJob            m_deleteAuthTokenJob;
    QKeychain::DeletePasswordJob            m_deleteShardIdJob;
    bool                                    m_deletingAuthToken;
    bool                                    m_deletingShardId;

    qevercloud::UserID                      m_lastRevokedAuthenticationUserId;

    QHash<QString,QSharedPointer<QKeychain::ReadPasswordJob> >      m_readLinkedNotebookAuthTokenJobsByGuid;
    QHash<QString,QSharedPointer<QKeychain::ReadPasswordJob> >      m_readLinkedNotebookShardIdJobsByGuid;
    QHash<QString,QSharedPointer<QKeychain::WritePasswordJob> >     m_writeLinkedNotebookAuthTokenJobsByGuid;
    QHash<QString,QSharedPointer<QKeychain::WritePasswordJob> >     m_writeLinkedNotebookShardIdJobsByGuid;

    QSet<QString>                           m_linkedNotebookGuidsWithoutLocalAuthData;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_AUTHENTICATION_MANAGER_PRIVATE_H
