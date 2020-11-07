/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_QT_KEYCHAIN_WRAPPER_H
#define LIB_QUENTIER_UTILITY_QT_KEYCHAIN_WRAPPER_H

#include <quentier/utility/IKeychainService.h>

#include <qt5keychain/keychain.h>

#include <QHash>
#include <QObject>
#include <QUuid>

namespace quentier {

/**
 * QtKeychain library doesn't play nice with threading. For this reason wrapping
 * the calls to this library into a separate QObject which doesn't have parent
 * and is not intended to be moved into any thread past creation. The
 * communication with this wrapper object occurs via signals and slots.
 */
class Q_DECL_HIDDEN QtKeychainWrapper final : public QObject
{
    Q_OBJECT
public:
    QtKeychainWrapper();
    virtual ~QtKeychainWrapper();

public Q_SLOTS:
    void onStartWritePasswordJob(
        QUuid jobId, QString service, QString key, QString password);

    void onStartReadPasswordJob(QUuid jobId, QString service, QString key);

    void onStartDeletePasswordJob(QUuid jobId, QString service, QString key);

Q_SIGNALS:
    void writePasswordJobFinished(
        QUuid requestId, IKeychainService::ErrorCode errorCode,
        ErrorString errorDescription);

    void readPasswordJobFinished(
        QUuid requestId, IKeychainService::ErrorCode errorCode,
        ErrorString errorDescription, QString password);

    void deletePasswordJobFinished(
        QUuid requestId, IKeychainService::ErrorCode errorCode,
        ErrorString errorDescription);

private Q_SLOTS:
    void onWritePasswordJobFinished(QKeychain::Job * pJob);

    void onReadPasswordJobFinished(QKeychain::Job * pJob);

    void onDeletePasswordJobFinished(QKeychain::Job * pJob);

private:
    IKeychainService::ErrorCode translateErrorCode(
        const QKeychain::Error errorCode) const;

private:
    QHash<QKeychain::ReadPasswordJob *, QUuid> m_readPasswordJobs;
    QHash<QKeychain::WritePasswordJob *, QUuid> m_writePasswordJobs;
    QHash<QKeychain::DeletePasswordJob *, QUuid> m_deletePasswordJobs;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_QT_KEYCHAIN_WRAPPER_H
