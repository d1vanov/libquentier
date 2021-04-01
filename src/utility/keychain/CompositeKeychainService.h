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
#include <quentier/utility/SuppressWarnings.h>

#include <QHash>
#include <QSet>

SAVE_WARNINGS

MSVC_SUPPRESS_WARNING(4834)

#include <boost/bimap.hpp>

RESTORE_WARNINGS

namespace quentier {

/**
 * @brief The CompositeKeychainService class implements IKeychainService
 * interface; it uses two other keychain services to implement its functionality
 */
class Q_DECL_HIDDEN CompositeKeychainService final : public IKeychainService
{
    Q_OBJECT
public:
    /**
     * CompositeKeychainService constructor
     * @param name                  Name of the composite keychain, required to
     *                              distinguish one composite keychain from
     *                              the other
     * @param primaryKeychain       Primary keychain
     * @param secondaryKeychain     Secondary keychain
     */
    explicit CompositeKeychainService(
        QString name, IKeychainServicePtr primaryKeychain,
        IKeychainServicePtr secondaryKeychain, QObject * parent = nullptr);

    virtual ~CompositeKeychainService() override;

    /**
     * Write password jobs go to both primary and secondary keychains. The
     * results are handled as follows:
     * 1. If both jobs succeed, the status is reported to the user
     * 2. If writing fails for the primary keychain but succeeds for the
     *    secondary one, service + key pair is stored in a dedicated
     *    ApplicationSettings as the one for which the password is
     *    available only in the secondary keychain.
     * 3. If writing succeeds for the primary keychain but fails for the
     *    secondary one, success status is returned to the user but service
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
     * Reading password jobs go as follows:
     * 1. If service and key pair is not marked as the one for which the
     *    password is not available in the primary keychain, the read request
     *    goes to the primary keychain first.
     * 2. If reading from the primary keychain fails or if it is skipped
     *    altogether due to the reasons from p. 1, an attempt is made to read
     *    the password from the secondary keychain unless service and key pair
     *    is marked as the one for which the password is not available in the
     *    secondary keychain.
     * 3. If the password is not available in either keychain, reading fails.
     *    Otherwise the first successful result (if any) is propagated back to
     *    the user.
     *
     * Id of the first attempted read password job is returned from the method.
     * If no real read password jobs are issued (because service and key pair
     * is marked for both keychains), the synthetic id is returned.
     */
    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) override;

    /**
     * Delete password jobs attempt to delete passwords from both primary and
     * secondary keychains unless service and key pair is marked as unavailable
     * in either of these keychains. The mark is stored persistently in a
     * dedicated ApplicationSettings object. If deletion fails for either
     * keychain, service and key pair is marked as the one for which the
     * password is not available in the corresponding keychain.
     *
     * If service and key pair is not marked as the one for which the password
     * is not available in the primary keychain, id of delete password job from
     * the primary keychain is returned from the method. Otherwise id of delete
     * password job from the secondary keychain is returned unless service and
     * key pair is marked as unavailable in the secondary keychain too. In which
     * case the synthetic id is returned.
     */
    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) override;

    /**
     * Checks whether primary keychain appears to be operational: if there are
     * 100 or more entries corresponding to failed writes to the primary
     * keychain, it is considered non-operational, otherwise it is considered
     * operational
     *
     * @return true if the primary keychain is considered operational, false
     *         otherwise
     */
    bool isPrimaryKeychainOperational() const;

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
    void createConnections();

    void markServiceKeyPairAsUnavailableInPrimaryKeychain(
        const QString & service, const QString & key);

    void unmarkServiceKeyPairAsUnavailableInPrimaryKeychain(
        const QString & service, const QString & key);

    bool isServiceKeyPairAvailableInPrimaryKeychain(
        const QString & service, const QString & key) const;

    void markServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key);

    void unmarkServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key);

    bool isServiceKeyPairAvailableInSecondaryKeychain(
        const QString & service, const QString & key) const;

    void persistUnavailableServiceKeyPairs(
        const char * groupName, const QString & service, const QString & key);

    std::pair<QString, QString> serviceAndKeyForRequestId(
        const QUuid & requestId) const;

    void cleanupServiceAndKeyForRequestId(const QUuid & requestId);

    void checkAndInitializeServiceKeysCaches() const;

    using ServiceKeyPairsCache = QHash<QString, QSet<QString>>;

    ServiceKeyPairsCache readServiceKeyPairsUnavailableInPrimaryKeychain()
        const;
    ServiceKeyPairsCache readServiceKeyPairsUnavailableInSecondaryKeychain()
        const;

    ServiceKeyPairsCache readServiceKeyPairsUnavailableInKeychainImpl(
        const char * groupName) const;

private:
    struct WritePasswordJobStatus
    {
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

    struct DeletePasswordJobStatus
    {
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

    using IdBimap = boost::bimap<QUuid, QUuid>;

private:
    const QString m_name;
    const IKeychainServicePtr m_primaryKeychain;
    const IKeychainServicePtr m_secondaryKeychain;

    mutable ServiceKeyPairsCache m_serviceKeysUnavailableInPrimaryKeychain;
    mutable ServiceKeyPairsCache m_serviceKeysUnavailableInSecondaryKeychain;
    mutable bool m_serviceKeysCachesInitialized = false;

    QHash<QUuid, WritePasswordJobStatus> m_completedWritePasswordJobs;
    QHash<QUuid, DeletePasswordJobStatus> m_completedDeletePasswordJobs;

    QHash<QUuid, std::pair<QString, QString>> m_serviceAndKeyByRequestId;

    QSet<QUuid> m_primaryKeychainReadPasswordJobIds;
    QHash<QUuid, QUuid>
        m_secondaryKeychainReadPasswordJobIdsToPrimaryKeychainJobIds;

    QSet<QUuid> m_primaryKeychainSingleDeletePasswordJobIds;
    QSet<QUuid> m_secondaryKeychainSingleDeletePasswordJobIds;

    // Mapping job ids: primary <=> secondary keychains
    IdBimap m_writePasswordJobIds;
    IdBimap m_readPasswordJobIds;
    IdBimap m_deletePasswordJobIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_KEYCHAIN_COMPOSITE_KEYCHAIN_SERVICE_H
