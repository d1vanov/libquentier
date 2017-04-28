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

#include "AuthenticationManager_p.h"
#include "SynchronizationPersistenceName.h"
#include <quentier/utility/Utility.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/logging/QuentierLogger.h>

#define EXPIRATION_TIMESTAMP_KEY QStringLiteral("ExpirationTimestamp")
#define LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX QStringLiteral("LinkedNotebookExpirationTimestamp_")
#define LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART QStringLiteral("_LinkedNotebookAuthToken_")
#define LINKED_NOTEBOOK_SHARD_ID_KEY_PART QStringLiteral("_LinkedNotebookShardId_")
#define READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB QStringLiteral("readLinkedNotebookAuthToken")
#define READ_LINKED_NOTEBOOK_SHARD_ID_JOB QStringLiteral("readLinkedNotebookShardId")
#define WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB QStringLiteral("writeLinkedNotebookAuthToken")
#define WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB QStringLiteral("writeLinkedNotebookShardId")
#define NOTE_STORE_URL_KEY QStringLiteral("NoteStoreUrl")
#define WEB_API_URL_PREFIX_KEY QStringLiteral("WebApiUrlPrefix")

namespace quentier {

AuthenticationManagerPrivate::AuthenticationManagerPrivate(const QString & consumerKey, const QString & consumerSecret,
                                                           const QString & host, const Account & account, QObject * parent) :
    QObject(parent),
    m_consumerKey(consumerKey),
    m_consumerSecret(consumerSecret),
    m_host(host),
    m_account(account),
    m_OAuthWebView(),
    m_OAuthResult(),
    m_authenticationInProgress(false),
    m_noteStore(QSharedPointer<qevercloud::NoteStore>(new qevercloud::NoteStore)),
    m_authContext(AuthContext::Blank),
    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid(),
    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid(),
    m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth(),
    m_authenticateToLinkedNotebooksPostponeTimerId(-1),
    m_readAuthTokenJob(QApplication::applicationName() + QStringLiteral("_read_auth_token")),
    m_readShardIdJob(QApplication::applicationName() + QStringLiteral("_read_shard_id")),
    m_readingAuthToken(false),
    m_readingShardId(false),
    m_writeAuthTokenJob(QApplication::applicationName() + QStringLiteral("_write_auth_token")),
    m_writeShardIdJob(QApplication::applicationName() + QStringLiteral("_write_shard_id")),
    m_writingAuthToken(false),
    m_writingShardId(false),
    m_writtenOAuthResult(),
    m_deleteAuthTokenJob(QApplication::applicationName() + QStringLiteral("_delete_auth_token")),
    m_deleteShardIdJob(QApplication::applicationName() + QStringLiteral("delete_shard_id")),
    m_deletingAuthToken(false),
    m_deletingShardId(false),
    m_lastRevokedAuthenticationUserId(-1),
    m_readLinkedNotebookAuthTokenJobsByGuid(),
    m_readLinkedNotebookShardIdJobsByGuid(),
    m_writeLinkedNotebookAuthTokenJobsByGuid(),
    m_writeLinkedNotebookShardIdJobsByGuid(),
    m_linkedNotebookGuidsWithoutLocalAuthData()
{
    m_OAuthResult.userId = -1;
    m_writtenOAuthResult.userId = -1;

    m_readAuthTokenJob.setAutoDelete(false);
    m_readShardIdJob.setAutoDelete(false);
    m_writeAuthTokenJob.setAutoDelete(false);
    m_writeShardIdJob.setAutoDelete(false);
    m_deleteAuthTokenJob.setAutoDelete(false);
    m_deleteShardIdJob.setAutoDelete(false);

    createConnections();
}

bool AuthenticationManagerPrivate::isInProgress() const
{
    return m_authenticationInProgress;
}

void AuthenticationManagerPrivate::onRequestAuthenticationToken()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRequestAuthenticationToken"));

    if (validAuthentication()) {
        QNDEBUG(QStringLiteral("Found valid auth token and shard id, returning them"));
        emit sendAuthenticationTokenAndShardId(m_OAuthResult.authenticationToken, m_OAuthResult.shardId, m_OAuthResult.expires);
        return;
    }

    authenticateImpl(AuthContext::Request);
}

void AuthenticationManagerPrivate::onRequestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString,QString> > linkedNotebookGuidsAndShareKeys)
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onRequestAuthenticationTokensForLinkedNotebooks"));
    m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth = linkedNotebookGuidsAndShareKeys;
    authenticateToLinkedNotebooks();
}

void AuthenticationManagerPrivate::onRequestAuthenticationRevoke(qevercloud::UserID userId)
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onRequestAuthenticationRevoke: user id = ") << userId);

    m_lastRevokedAuthenticationUserId = userId;

    m_deleteAuthTokenJob.setKey(QApplication::applicationName() + QStringLiteral("_") +
                                m_host + QStringLiteral("_") + QString::number(m_lastRevokedAuthenticationUserId));
    m_deletingAuthToken = true;
    m_deleteAuthTokenJob.start();

    m_deleteShardIdJob.setKey(QApplication::applicationName() + QStringLiteral("_") +
                              m_host + QStringLiteral("_") + QString::number(m_lastRevokedAuthenticationUserId));
    m_deletingShardId = true;
    m_deleteShardIdJob.start();
}

void AuthenticationManagerPrivate::onOAuthResult(bool result)
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onOAuthResult: ")
            << (result ? QStringLiteral("success") : QStringLiteral("failure")));

    if (result) {
        onOAuthSuccess();
    }
    else {
        onOAuthFailure();
    }
}

void AuthenticationManagerPrivate::onOAuthSuccess()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onOAuthSuccess"));

    m_authenticationInProgress = false;

    if (m_authContext != AuthContext::Request) {
        m_OAuthResult = m_OAuthWebView.oauthResult();
        m_noteStore.setNoteStoreUrl(m_OAuthResult.noteStoreUrl);
        m_noteStore.setAuthenticationToken(m_OAuthResult.authenticationToken);
    }

    launchStoreOAuthResult(m_OAuthWebView.oauthResult());
}

void AuthenticationManagerPrivate::onOAuthFailure()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onOAuthFailure"));

    m_authenticationInProgress = false;

    ErrorString error(QT_TRANSLATE_NOOP("", "OAuth failed"));
    QString oauthError = m_OAuthWebView.oauthError();
    if (!oauthError.isEmpty()) {
        error.details() = oauthError;
    }

    emit notifyError(error);
}

void AuthenticationManagerPrivate::onKeychainJobFinished(QKeychain::Job * pJob)
{
    if (!pJob) {
        ErrorString error(QT_TRANSLATE_NOOP("", "qtkeychain error: null pointer to keychain job on finish"));
        emit notifyError(error);
        return;
    }

    if (pJob == &m_readAuthTokenJob)
    {
        onReadAuthTokenFinished();
    }
    else if (pJob == &m_readShardIdJob)
    {
        onReadShardIdFinished();
    }
    else if (pJob == &m_writeAuthTokenJob)
    {
        onWriteAuthTokenFinished();
    }
    else if (pJob == &m_writeShardIdJob)
    {
        onWriteShardIdFinished();
    }
    else if (pJob == &m_deleteAuthTokenJob)
    {
        onDeleteAuthTokenFinished();
    }
    else if (pJob == &m_deleteShardIdJob)
    {
        onDeleteShardIdFinished();
    }
    else
    {
        for(auto it = m_writeLinkedNotebookShardIdJobsByGuid.begin(), end = m_writeLinkedNotebookShardIdJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == pJob)
            {
                if (pJob->error() != QKeychain::NoError) {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error saving linked notebook's shard id to the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(pJob->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += pJob->errorString();
                    QNWARNING(error);
                    emit notifyError(error);
                }

                Q_UNUSED(m_writeLinkedNotebookShardIdJobsByGuid.erase(it))
                return;
            }
        }

        for(auto it = m_writeLinkedNotebookAuthTokenJobsByGuid.begin(), end = m_writeLinkedNotebookAuthTokenJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == pJob)
            {
                if (pJob->error() != QKeychain::NoError) {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error saving linked notebook's authentication token to the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(pJob->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += pJob->errorString();
                    QNWARNING(error);
                    emit notifyError(error);
                }

                Q_UNUSED(m_writeLinkedNotebookAuthTokenJobsByGuid.erase(it))
                return;
            }
        }

        for(auto it = m_readLinkedNotebookAuthTokenJobsByGuid.begin(), end = m_readLinkedNotebookAuthTokenJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == pJob)
            {
                if (pJob->error() == QKeychain::NoError)
                {
                    QNDEBUG(QStringLiteral("Successfully read the authentication token for linked notebook from the keychain: "
                                           "linked notebook guid: ") << it.key());
                    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[it.key()].first = cachedJob->textData();
                }
                else if (pJob->error() == QKeychain::EntryNotFound)
                {
                    QNDEBUG(QStringLiteral("Could not find authentication token for linked notebook in the keychain: "
                                           "linked notebook guid: ") << it.key());
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }
                else
                {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error reading linked notebook's authentication token from the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(pJob->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += pJob->errorString();
                    QNWARNING(error);
                    emit notifyError(error);

                    // Try to recover by making user to authenticate again in the blind hope that
                    // the next time the persistence of auth settings in the keychain would work
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }

                authenticateToLinkedNotebooks();
                Q_UNUSED(m_readLinkedNotebookAuthTokenJobsByGuid.erase(it))
                return;
            }
        }

        for(auto it = m_readLinkedNotebookShardIdJobsByGuid.begin(), end = m_readLinkedNotebookShardIdJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == pJob)
            {
                if (pJob->error() == QKeychain::NoError)
                {
                    QNDEBUG(QStringLiteral("Successfully read the shard id for linked notebook from the keychain: "
                                           "linked notebook guid: ") << it.key());
                    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[it.key()].second = cachedJob->textData();
                }
                else if (pJob->error() == QKeychain::EntryNotFound)
                {
                    QNDEBUG(QStringLiteral("Could not find shard id for linked notebook in the keychain: "
                                           "linked notebook guid: ") << it.key());
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }
                else
                {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error reading linked notebook's shard id from the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(pJob->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += pJob->errorString();
                    QNWARNING(error);
                    emit notifyError(error);

                    // Try to recover by making user to authenticate again in the blind hope that
                    // the next time the persistence of auth settings in the keychain would work
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }

                authenticateToLinkedNotebooks();
                Q_UNUSED(m_readLinkedNotebookShardIdJobsByGuid.erase(it))
                return;
            }
        }

        ErrorString error(QT_TRANSLATE_NOOP("", "Unknown keychain job finished event"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }
}

void AuthenticationManagerPrivate::createConnections()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::createConnections"));

    // Connections with OAuth handler
    QObject::connect(&m_OAuthWebView, QNSIGNAL(qevercloud::EvernoteOAuthWebView,authenticationFinished,bool),
                     this, QNSLOT(AuthenticationManagerPrivate,onOAuthResult,bool));
    QObject::connect(&m_OAuthWebView, QNSIGNAL(qevercloud::EvernoteOAuthWebView,authenticationSuceeded),
                     this, QNSLOT(AuthenticationManagerPrivate,onOAuthSuccess));
    QObject::connect(&m_OAuthWebView, QNSIGNAL(qevercloud::EvernoteOAuthWebView,authenticationFailed),
                     this, QNSLOT(AuthenticationManagerPrivate,onOAuthFailure));
}

bool AuthenticationManagerPrivate::validAuthentication() const
{
    if (m_OAuthResult.expires == static_cast<qint64>(0)) {
        // The value is not set
        return false;
    }

    return !checkIfTimestampIsAboutToExpireSoon(m_OAuthResult.expires);
}

bool AuthenticationManagerPrivate::checkIfTimestampIsAboutToExpireSoon(const qevercloud::Timestamp timestamp) const
{
    qevercloud::Timestamp currentTimestamp = QDateTime::currentMSecsSinceEpoch();

    if (currentTimestamp - timestamp < SIX_HOURS_IN_MSEC) {
        return true;
    }

    return false;
}

void AuthenticationManagerPrivate::authenticateToLinkedNotebooks()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::authenticateToLinkedNotebooks"));

    if (Q_UNLIKELY(m_OAuthResult.userId < 0)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Detected attempt to authenticate to linked notebooks while "
                                            "there is no user id set to the synchronization manager"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    const int numLinkedNotebooks = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.size();
    if (numLinkedNotebooks == 0) {
        QNDEBUG(QStringLiteral("No linked notebooks waiting for authentication, sending "
                               "cached auth tokens, shard ids and expiration times"));
        emit sendAuthenticationTokensForLinkedNotebooks(m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
                                                        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
        return;
    }

    ApplicationSettings appSettings(m_account, SYNCHRONIZATION_PERSISTENCE_NAME);
    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_OAuthResult.userId) + QStringLiteral("/");

    QHash<QString,QPair<QString,QString> >  authTokensAndShardIdsToCacheByGuid;
    QHash<QString,qevercloud::Timestamp>    authTokenExpirationTimestampsToCacheByGuid;

    QString keyPrefix = QApplication::applicationName() + QStringLiteral("_") + m_host +
                        QStringLiteral("_") + QString::number(m_OAuthResult.userId);

    for(auto it = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.begin();
        it != m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.end(); )
    {
        const QPair<QString,QString> & pair = *it;

        const QString & guid = pair.first;
        const QString & sharedNotebookGlobalId = pair.second;

        bool forceRemoteAuth = false;
        auto linkedNotebookAuthTokenIt = m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.find(guid);
        if (linkedNotebookAuthTokenIt == m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.end())
        {
            auto noAuthDataIt = m_linkedNotebookGuidsWithoutLocalAuthData.find(guid);
            if (noAuthDataIt != m_linkedNotebookGuidsWithoutLocalAuthData.end())
            {
                forceRemoteAuth = true;
                Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.erase(noAuthDataIt))
            }
            else
            {
                QNDEBUG(QStringLiteral("Haven't found authentication token and shard id for linked notebook guid ") << guid
                        << QStringLiteral(" in the local cache, will try to read them from the keychain"));

                // 1) Set up the job of reading the authentication token, if it hasn't been started already
                auto readAuthTokenJobIt = m_readLinkedNotebookAuthTokenJobsByGuid.find(guid);
                if (readAuthTokenJobIt == m_readLinkedNotebookAuthTokenJobsByGuid.end())
                {
                    QSharedPointer<QKeychain::ReadPasswordJob> pReadAuthTokenJob =
                        QSharedPointer<QKeychain::ReadPasswordJob>(new QKeychain::ReadPasswordJob(READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB));

                    pReadAuthTokenJob->setKey(keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid);
                    QObject::connect(pReadAuthTokenJob.data(), QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                                     this, QNSLOT(AuthenticationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
                    pReadAuthTokenJob->start();

                    Q_UNUSED(m_readLinkedNotebookAuthTokenJobsByGuid.insert(guid, pReadAuthTokenJob))
                }

                // 2) Set up the job reading the shard id, if it hasn't been started already
                auto readShardIdJobIt = m_readLinkedNotebookShardIdJobsByGuid.find(guid);
                if (readShardIdJobIt == m_readLinkedNotebookShardIdJobsByGuid.end())
                {
                    QSharedPointer<QKeychain::ReadPasswordJob> pReadShardIdJob =
                        QSharedPointer<QKeychain::ReadPasswordJob>(new QKeychain::ReadPasswordJob(READ_LINKED_NOTEBOOK_SHARD_ID_JOB));

                    pReadShardIdJob->setKey(keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid);
                    QObject::connect(pReadShardIdJob.data(), QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                                     this, QNSLOT(AuthenticationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
                    pReadShardIdJob->start();

                    Q_UNUSED(m_readLinkedNotebookShardIdJobsByGuid.insert(guid, pReadShardIdJob))
                }

                ++it;
                continue;
            }
        }

        if (!forceRemoteAuth)
        {
            auto linkedNotebookAuthTokenExpirationIt = m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.find(guid);
            if (linkedNotebookAuthTokenExpirationIt == m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end())
            {
                QVariant expirationTimeVariant = appSettings.value(keyGroup + LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + guid);
                if (!expirationTimeVariant.isNull())
                {
                    bool conversionResult = false;
                    qevercloud::Timestamp expirationTime = expirationTimeVariant.toLongLong(&conversionResult);
                    if (conversionResult) {
                        linkedNotebookAuthTokenExpirationIt = m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.insert(guid, expirationTime);
                    }
                    else {
                        QNWARNING(QStringLiteral("Can't convert linked notebook's authentication token's expiration time from QVariant retrieved from ")
                                  << QStringLiteral("app settings into timestamp: linked notebook guid = ") << guid
                                  << QStringLiteral(", variant = ") << expirationTimeVariant);
                    }
                }
            }

            if ( (linkedNotebookAuthTokenExpirationIt != m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end()) &&
                 !checkIfTimestampIsAboutToExpireSoon(linkedNotebookAuthTokenExpirationIt.value()) )
            {
                QNDEBUG(QStringLiteral("Found authentication data for linked notebook guid ") << guid
                        << QStringLiteral(" + verified its expiration timestamp"));
                it = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.erase(it);
                continue;
            }
        }

        QNDEBUG(QStringLiteral("Authentication data for linked notebook guid ") << guid
                << QStringLiteral(" was either not found in local cache (and/or app settings / keychain) ")
                << QStringLiteral("or has expired, need to receive that from remote Evernote service"));

        if (m_authenticateToLinkedNotebooksPostponeTimerId >= 0) {
            QNDEBUG(QStringLiteral("Authenticate to linked notebook postpone timer is active, will wait "
                                   "to preserve the breach of Evernote rate API limit"));
            ++it;
            continue;
        }

        if (m_authContext != AuthContext::Blank) {
            QNDEBUG(QStringLiteral("Authentication context variable is not set to blank which means ")
                    << QStringLiteral("that authentication must be in progress: ")
                    << m_authContext << QStringLiteral("; won't attempt to call remote Evernote API at this time"));
            ++it;
            continue;
        }

        qevercloud::AuthenticationResult authResult;
        ErrorString errorDescription;
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = m_noteStore.authenticateToSharedNotebook(sharedNotebookGlobalId, authResult,
                                                                    errorDescription, rateLimitSeconds);
        if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
        {
            if (validAuthentication()) {
                ErrorString error(QT_TRANSLATE_NOOP("", "Unexpected AUTH_EXPIRED error"));
                error.additionalBases().append(errorDescription.base());
                error.additionalBases().append(errorDescription.additionalBases());
                error.details() = errorDescription.details();
                emit notifyError(error);
            }
            else {
                authenticateImpl(AuthContext::AuthToLinkedNotebooks);
            }

            ++it;
            continue;
        }
        else if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
        {
            if (rateLimitSeconds <= 0) {
                errorDescription.base() = QString::fromUtf8(QT_TRANSLATE_NOOP("", "Rate limit reached but the number of seconds to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                emit notifyError(errorDescription);
                return;
            }

            m_authenticateToLinkedNotebooksPostponeTimerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));

            ++it;
            continue;
        }
        else if (errorCode != 0)
        {
            emit notifyError(errorDescription);
            return;
        }

        QString shardId;
        if (authResult.user.isSet() && authResult.user->shardId.isSet()) {
            shardId = authResult.user->shardId.ref();
        }

        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] = QPair<QString, QString>(authResult.authenticationToken, shardId);
        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] = authResult.expiration;

        QPair<QString,QString> & authTokenAndShardId = authTokensAndShardIdsToCacheByGuid[guid];
        authTokenAndShardId.first = authResult.authenticationToken;
        authTokenAndShardId.second = shardId;

        authTokenExpirationTimestampsToCacheByGuid[guid] = authResult.expiration;

        it = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.erase(it);
    }

    if (m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.empty()) {
        QNDEBUG(QStringLiteral("Retrieved authentication data for all requested linked notebooks, sending the answer now"));
        emit sendAuthenticationTokensForLinkedNotebooks(m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
                                                        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
    }

    // Caching linked notebook's authentication token's expiration time in app settings
    typedef QHash<QString,qevercloud::Timestamp>::const_iterator ExpirationTimeCIter;
    ExpirationTimeCIter authTokenExpirationTimesToCacheEnd = authTokenExpirationTimestampsToCacheByGuid.end();
    for(ExpirationTimeCIter it = authTokenExpirationTimestampsToCacheByGuid.begin();
        it != authTokenExpirationTimesToCacheEnd; ++it)
    {
        QString key = LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + it.key();
        appSettings.setValue(keyGroup + key, QVariant(it.value()));
    }

    // Caching linked notebook's authentication tokens and shard ids in the keychain

    for(auto it = authTokensAndShardIdsToCacheByGuid.begin(), end = authTokensAndShardIdsToCacheByGuid.end();
        it != end; ++it)
    {
        const QString & guid = it.key();
        const QString & token = it.value().first;
        const QString & shardId = it.value().second;

        // 1) Set up the job writing the auth token to the keychain
        QString key = keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid;
        QSharedPointer<QKeychain::WritePasswordJob> pWriteAuthTokenJob(new QKeychain::WritePasswordJob(WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB));
        Q_UNUSED(m_writeLinkedNotebookAuthTokenJobsByGuid.insert(key, pWriteAuthTokenJob))
        pWriteAuthTokenJob->setKey(key);
        pWriteAuthTokenJob->setTextData(token);
        QObject::connect(pWriteAuthTokenJob.data(), QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                         this, QNSLOT(AuthenticationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
        pWriteAuthTokenJob->start();

        // 2) Set up the job writing the shard id to the keychain
        key = keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid;
        QSharedPointer<QKeychain::WritePasswordJob> pWriteShardIdJob(new QKeychain::WritePasswordJob(WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB));
        Q_UNUSED(m_writeLinkedNotebookShardIdJobsByGuid.insert(key, pWriteShardIdJob))
        pWriteShardIdJob->setKey(key);
        pWriteShardIdJob->setTextData(shardId);
        QObject::connect(pWriteShardIdJob.data(), QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                         this, QNSLOT(AuthenticationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
        pWriteShardIdJob->start();
    }
}

void AuthenticationManagerPrivate::onReadAuthTokenFinished()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onReadAuthTokenFinished"));

    m_readingAuthToken = false;

    QKeychain::Error errorCode = m_readAuthTokenJob.error();
    if (errorCode == QKeychain::EntryNotFound)
    {
        ErrorString error(QT_TRANSLATE_NOOP("", "Unexpectedly missing OAuth token in the keychain"));
        error.details() = m_readAuthTokenJob.errorString();
        QNWARNING(error);
        emit notifyError(error);
        return;
    }
    else if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to read the authentication token returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_readAuthTokenJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to read the stored authentication token from the keychain"));
        error.details() = m_readAuthTokenJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully restored the authentication token"));
    m_OAuthResult.authenticationToken = m_readAuthTokenJob.textData();

    if (!m_readingShardId) {
        finalizeAuthentication();
    }
}

void AuthenticationManagerPrivate::onReadShardIdFinished()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onReadShardIdFinished"));

    m_readingShardId = false;

    QKeychain::Error errorCode = m_readShardIdJob.error();
    if (errorCode == QKeychain::EntryNotFound)
    {
        ErrorString error(QT_TRANSLATE_NOOP("", "Unexpectedly missing OAuth shard id in the keychain"));
        error.details() = m_readShardIdJob.errorString();
        QNWARNING(error);
        emit notifyError(error);
        return;
    }
    else if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to read the shard id returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_readShardIdJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to read the stored shard id from the keychain"));
        error.details() = m_readShardIdJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully restored the shard id"));
    m_OAuthResult.shardId = m_readShardIdJob.textData();

    if (!m_readingAuthToken) {
        finalizeAuthentication();
    }
}

void AuthenticationManagerPrivate::onWriteAuthTokenFinished()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onWriteAuthTokenFinished"));

    m_writingAuthToken = false;

    QKeychain::Error errorCode = m_writeAuthTokenJob.error();
    if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to write the authentication token returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_writeAuthTokenJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to write the oauth token to the keychain"));
        error.details() = m_writeAuthTokenJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully stored the authentication token in the keychain"));

    if (!m_writingShardId) {
        finalizeStoreOAuthResult();
    }
}

void AuthenticationManagerPrivate::onWriteShardIdFinished()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onWriteShardIdFinished"));

    m_writingShardId = false;

    QKeychain::Error errorCode = m_writeShardIdJob.error();
    if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to write the shard id returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_writeShardIdJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to write the oauth shard id to the keychain"));
        error.details() = m_writeShardIdJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully stored the shard id in the keychain"));

    if (!m_writingAuthToken) {
        finalizeStoreOAuthResult();
    }
}

void AuthenticationManagerPrivate::onDeleteAuthTokenFinished()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onDeleteAuthTokenFinished: user id = ")
            << m_lastRevokedAuthenticationUserId);

    m_deletingAuthToken = false;

    if (!m_deletingShardId) {
        finalizeRevokeAuthentication();
    }
}

void AuthenticationManagerPrivate::onDeleteShardIdFinished()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::onDeleteShardIdFinished: user id = ")
            << m_lastRevokedAuthenticationUserId);

    m_deletingShardId = false;

    if (!m_deletingAuthToken) {
        finalizeRevokeAuthentication();
    }
}

void AuthenticationManagerPrivate::launchOAuth()
{
    m_authenticationInProgress = true;
    m_OAuthWebView.authenticate(m_host, m_consumerKey, m_consumerSecret);
}

void AuthenticationManagerPrivate::authenticateImpl(const AuthContext::type authContext)
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::authenticateImpl: auth context = ") << authContext);

    m_authContext = authContext;

    if (m_authContext == AuthContext::Request) {
        QNDEBUG(QStringLiteral("Authentication of the new user is requested, proceeding to OAuth"));
        launchOAuth();
        return;
    }

    if (m_OAuthResult.userId < 0) {
        QNDEBUG(QStringLiteral("No current user id, launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    if (validAuthentication()) {
        QNDEBUG(QStringLiteral("Found already valid authentication info"));
        finalizeAuthentication();
        return;
    }

    QNTRACE(QStringLiteral("Trying to restore persistent authentication settings..."));

    ApplicationSettings appSettings(m_account, SYNCHRONIZATION_PERSISTENCE_NAME);
    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_OAuthResult.userId) + QStringLiteral("/");

    QVariant tokenExpirationValue = appSettings.value(keyGroup + EXPIRATION_TIMESTAMP_KEY);
    if (tokenExpirationValue.isNull()) {
        QNINFO(QStringLiteral("Authentication token expiration timestamp was not found within application settings, "
                              "assuming it has never been written & launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    bool conversionResult = false;
    qevercloud::Timestamp tokenExpirationTimestamp = tokenExpirationValue.toLongLong(&conversionResult);
    if (!conversionResult) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: failed to convert QVariant with authentication token "
                                            "expiration timestamp to the actual timestamp"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    if (checkIfTimestampIsAboutToExpireSoon(tokenExpirationTimestamp)) {
        QNINFO(QStringLiteral("Authentication token stored in persistent application settings is about to expire soon enough, "
                              "launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    m_OAuthResult.expires = tokenExpirationTimestamp;

    QNTRACE(QStringLiteral("Restoring persistent note store url"));

    QVariant noteStoreUrlValue = appSettings.value(keyGroup + NOTE_STORE_URL_KEY);
    if (noteStoreUrlValue.isNull()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to find the note store url within persistent application settings"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    QString noteStoreUrl = noteStoreUrlValue.toString();
    if (noteStoreUrl.isEmpty()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: failed to convert the note store url from QVariant to QString"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    m_OAuthResult.noteStoreUrl = noteStoreUrl;

    QNDEBUG(QStringLiteral("Restoring persistent web api url prefix"));

    QVariant webApiUrlPrefixValue = appSettings.value(keyGroup + WEB_API_URL_PREFIX_KEY);
    if (webApiUrlPrefixValue.isNull()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to find the web API url prefix within persistent application settings"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    QString webApiUrlPrefix = webApiUrlPrefixValue.toString();
    if (webApiUrlPrefix.isEmpty()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to convert the web api url prefix from QVariant to QString"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    m_OAuthResult.webApiUrlPrefix = webApiUrlPrefix;

    QNDEBUG(QStringLiteral("Trying to restore the authentication token and the shard id from the keychain"));

    m_readAuthTokenJob.setKey(QApplication::applicationName() + QStringLiteral("_auth_token_") +
                              m_host + QStringLiteral("_") + QString::number(m_OAuthResult.userId));
    m_readingAuthToken = true;
    m_readAuthTokenJob.start();

    m_readShardIdJob.setKey(QApplication::applicationName() + QStringLiteral("_shard_id_") +
                            m_host + QStringLiteral("_") + QString::number(m_OAuthResult.userId));
    m_readingShardId = true;
    m_readShardIdJob.start();
}

void AuthenticationManagerPrivate::finalizeAuthentication()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::finalizeAuthentication: result = ") << m_OAuthResult);

    switch(m_authContext)
    {
    case AuthContext::Blank:
        {
            ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: incorrect authentication context: blank"));
            emit notifyError(error);
            break;
        }
    case AuthContext::Request:
        emit sendAuthenticationTokenAndShardId(m_OAuthResult.authenticationToken,
                                               m_OAuthResult.shardId, m_OAuthResult.expires);
        break;
    case AuthContext::AuthToLinkedNotebooks:
        authenticateToLinkedNotebooks();
        break;
    default:
        {
            ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: unknown authentication context"));
            error.details() = ToString(m_authContext);
            emit notifyError(error);
            break;
        }
    }

    m_authContext = AuthContext::Blank;
}

void AuthenticationManagerPrivate::launchStoreOAuthResult(const qevercloud::EvernoteOAuthWebView::OAuthResult & result)
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::launchStoreOAuthResult: ") << result);

    m_writtenOAuthResult = result;

    m_writeAuthTokenJob.setKey(QApplication::applicationName() + QStringLiteral("_auth_token_") +
                               m_host + QStringLiteral("_") + QString::number(result.userId));
    m_writeAuthTokenJob.setTextData(result.authenticationToken);
    m_writingAuthToken = true;
    m_writeAuthTokenJob.start();

    m_writeShardIdJob.setKey(QApplication::applicationName() + QStringLiteral("_shard_id_") +
                             m_host + QStringLiteral("_") + QString::number(result.userId));
    m_writeShardIdJob.setTextData(result.shardId);
    m_writingShardId = true;
    m_writeShardIdJob.start();
}

void AuthenticationManagerPrivate::finalizeStoreOAuthResult()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::finalizeStoreOAuthResult"));

    ApplicationSettings appSettings(m_account, SYNCHRONIZATION_PERSISTENCE_NAME);

    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_writtenOAuthResult.userId) + QStringLiteral("/");

    appSettings.setValue(keyGroup + NOTE_STORE_URL_KEY, m_writtenOAuthResult.noteStoreUrl);
    appSettings.setValue(keyGroup + EXPIRATION_TIMESTAMP_KEY, m_writtenOAuthResult.expires);
    appSettings.setValue(keyGroup + WEB_API_URL_PREFIX_KEY, m_writtenOAuthResult.webApiUrlPrefix);

    QNDEBUG(QStringLiteral("Successfully wrote the authentication result info to the application settings for host ")
            << m_host << QStringLiteral(", user id ") << m_writtenOAuthResult.userId << QStringLiteral(": ")
            << QStringLiteral(": auth token expiration timestamp = ") << printableDateTimeFromTimestamp(m_writtenOAuthResult.expires)
            << QStringLiteral(", web API url prefix = ") << m_writtenOAuthResult.webApiUrlPrefix);

    finalizeAuthentication();
}

void AuthenticationManagerPrivate::finalizeRevokeAuthentication()
{
    QNDEBUG(QStringLiteral("AuthenticationManagerPrivate::finalizeRevokeAuthentication: user id = ")
            << m_lastRevokedAuthenticationUserId);

    QKeychain::Error errorCode = m_deleteAuthTokenJob.error();
    if ((errorCode != QKeychain::NoError) && (errorCode != QKeychain::EntryNotFound)) {
        QNWARNING(QStringLiteral("Attempt to delete the auth token returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_deleteAuthTokenJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to delete authentication token from the keychain"));
        error.details() = m_deleteAuthTokenJob.errorString();
        emit authenticationRevokeReply(/* success = */ false, error, m_lastRevokedAuthenticationUserId);
        return;
    }

    errorCode = m_deleteShardIdJob.error();
    if ((errorCode != QKeychain::NoError) && (errorCode != QKeychain::EntryNotFound)) {
        QNWARNING(QStringLiteral("Attempt to delete the shard id returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_deleteShardIdJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to delete shard id from the keychain"));
        error.details() = m_deleteShardIdJob.errorString();
        emit authenticationRevokeReply(/* success = */ false, error, m_lastRevokedAuthenticationUserId);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully revoked the authentication for user id ")
            << m_lastRevokedAuthenticationUserId
            << QStringLiteral(": both auth token and shard id either deleted or didn't exist"));
    emit authenticationRevokeReply(/* success = */ true, ErrorString(),
                                   m_lastRevokedAuthenticationUserId);
}

} // namespace quentier
