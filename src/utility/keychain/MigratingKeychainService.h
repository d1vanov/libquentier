/*
 * Copyright 2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_KEYCHAIN_MIGRATING_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_UTILITY_KEYCHAIN_MIGRATING_KEYCHAIN_SERVICE_H

#include <quentier/utility/IKeychainService.h>
#include <quentier/utility/SuppressWarnings.h>

#include <QHash>
#include <QSet>

SAVE_WARNINGS

MSVC_SUPPRESS_WARNING(4834)

#include <boost/bimap.hpp>

RESTORE_WARNINGS

namespace quentier {

/**
 * @brief The MigratingKeychainService class implements IKeychainService
 * interface and works as follows: it tries to gradually migrate the password
 * data from the source keychain into the sink keychain.
 *
 * This class can be used for keychain migrations from some old one to some new
 * one. First the old keychain is switched to the migrating keychain and then,
 * one-two releases later, migrating keychain can be replaced with just the new
 * keychain. Presumably by this time all existing users passwords should have
 * been migrated from the old keychain to the new one via the migrating
 * keychain.
 */
class Q_DECL_HIDDEN MigratingKeychainService final : public IKeychainService
{
    Q_OBJECT
public:
    explicit MigratingKeychainService(
        IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain,
        QObject * parent = nullptr);

    virtual ~MigratingKeychainService() override;

    /**
     * Write password jobs go to the sink keychain only. The idea is that new
     * data should go only to the sink keychain.
     */
    virtual QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) override;

    /**
     * Read password jobs try to read from the sink keychain first. If response
     * code is "entry not found", it attempts to read from the source keychain.
     * If successful, it returns the password to the user and silently writes
     * the password to the sink keychain so that the next time it can be read
     * from there. After successful writing to the sink keychain it also tries
     * to delete the password from the source keychain.
     */
    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) override;

    /**
     * Delete password jobs go to the sink keychain first. If response code is
     * "entry not found", it attempts to delete the password from the source
     * keychain.
     */
    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) override;

private Q_SLOTS:
    void onSinkKeychainWritePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    void onSinkKeychainReadPasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
        QString password);

    void onSourceKeychainReadPasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
        QString password);

    void onSinkKeychainDeletePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    void onSourceKeychainDeletePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

private:
    void createConnections();

    using RequestIdToServiceAndKey = QHash<QUuid, std::pair<QString, QString>>;

    using IdBimap = boost::bimap<QUuid, QUuid>;

    struct ReadPasswordJobData
    {
        QUuid m_sinkKeychainReadRequestId;
        QString m_service;
        QString m_key;
        QString m_password;
    };

    struct DeletePasswordJobStatus
    {
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

private:
    const IKeychainServicePtr m_sourceKeychain;
    const IKeychainServicePtr m_sinkKeychain;

    QSet<QUuid> m_sinkKeychainWriteRequestIds;

    RequestIdToServiceAndKey m_sinkKeychainReadRequestIdsToServiceAndKey;
    QHash<QUuid, ReadPasswordJobData> m_sourceKeychainReadRequestData;

    // sink <=> source keychains
    IdBimap m_deletePasswordJobIds;
    QHash<QUuid, DeletePasswordJobStatus> m_completedDeletePasswordJobs;

    RequestIdToServiceAndKey
        m_internalSinkKeychainWriteRequestIdsToServiceAndKey;
    QSet<QUuid> m_internalSourceKeychainDeleteRequestIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_KEYCHAIN_MIGRATING_KEYCHAIN_SERVICE_H
