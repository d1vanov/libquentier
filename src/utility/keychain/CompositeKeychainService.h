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

#ifndef LIB_QUENTIER_UTILITY_KEYCHAIN_COMPOSITE_KEYCHAIN_SERVICE_H
#define LIB_QUENTIER_UTILITY_KEYCHAIN_COMPOSITE_KEYCHAIN_SERVICE_H

#include <quentier/utility/IKeychainService.h>

#include <QHash>

#include <boost/bimap.hpp>

namespace quentier {

/**
 * @brief The CompositeKeychainService class implements IKeychainService
 * interface; it uses two other keychain services to implement its functionality
 */
class Q_DECL_HIDDEN CompositeKeychainService final : public IKeychainService
{
public:
    explicit CompositeKeychainService(
        IKeychainServicePtr primaryKeychain,
        IKeychainServicePtr secondaryKeychain);

    virtual ~CompositeKeychainService() override;

    /**
     * Write password jobs go to both primary and secondary keychains. The
     * results are handled as follows:
     * 1. If both jobs succeed, the status is reported to the user
     * 2. If writing fails for the primary keychain but succeeds for the
     *    secondary one, an attempt is made to delete the password from the
     *    primary keychain and service + key pair is stored in a dedicated
     *    ApplicationSettings as the one available for which the password is
     *    available only in the secondary keychain.
     * 3. If writing succeeds for the primary keychain but fails for the
     *    secondary one, success status is returned to the user but an attempt
     *    is made to delete the password from the secondary keychain and service
     *    + key pair is stored in a dedicated ApplicationSettings as the one
     *    for which the password is available only in the primary keychain.
     * 4. If both keychains fail, the failure is propagated to the user.
     *
     * Id of write password job for the primary keychain is returned from the
     * method.
     */
    virtual QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) override;

    /**
     * Reading password jobs go to the primary keychain first unless service +
     * key pair has been recorded as the one for which the password is available
     * only in the secondary keychain. If reading from the primary keychain
     * fails, an attempt is made to read from the secondary keychain. The first
     * successful status (if any) is propagated to the user.
     *
     * Id of the first attempted read password job is returned from the method.
     */
    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) override;

    /**
     * Delete password jobs attempt to delete passwords from both primary and
     * secondary keychains and error is propagated to the user if deletion fails
     * for either of them.
     *
     * If of delete password job for the primary keychain is returned from the
     * method.
     */
    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) override;

private Q_SLOTS:
    void onPrimaryKeychainWritePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    void onSecondaryKeychainWritePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    void onPrimaryKeychainReadPasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
        QString password);

    void onSecondaryKeychainReadPasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
        QString password);

    void onPrimaryKeychainDeletePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

    void onSecondaryKeychainDeletePasswordJobFinished(
        QUuid requestId, ErrorCode errorCode, ErrorString errorDescription);

private:
    void markServiceKeyPairAsUnavailableInPrimaryKeychain(
        const QString & service, const QString & key);

    bool isServiceKeyPairAvailableInPrimaryKeychain(
        const QString & service, const QString & key) const;

    void markServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key);

    bool isServiceKeyPairAvailableInSecondaryKeychain(
        const QString & service, const QString & key) const;

private:
    struct WritePasswordJobStatus
    {
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

    struct ReadPasswordJobStatus
    {
        QString m_password;
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

    struct DeletePasswordJobStatus
    {
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

private:
    IKeychainServicePtr m_primaryKeychain;
    IKeychainServicePtr m_secondaryKeychain;

    QHash<QUuid, WritePasswordJobStatus> m_completedWritePasswordJobs;
    QHash<QUuid, ReadPasswordJobStatus> m_completedReadPasswordJobs;
    QHash<QUuid, DeletePasswordJobStatus> m_completedDeletePasswordJobs;

    // Mapping job ids: primary <=> secondary keychains
    boost::bimap<QUuid, QUuid> m_writePasswordJobIds;
    boost::bimap<QUuid, QUuid> m_readPasswordJobIds;
    boost::bimap<QUuid, QUuid> m_deletePasswordJobIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_KEYCHAIN_COMPOSITE_KEYCHAIN_SERVICE_H
