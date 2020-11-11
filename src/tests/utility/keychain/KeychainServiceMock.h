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

#ifndef LIB_QUENTIER_TESTS_UTILITY_KEYCHAIN_KEYCHAIN_SERVICE_MOCK_H
#define LIB_QUENTIER_TESTS_UTILITY_KEYCHAIN_KEYCHAIN_SERVICE_MOCK_H

#include <quentier/utility/IKeychainService.h>

#include <functional>

namespace quentier {
namespace test {

class KeychainServiceMock final : public IKeychainService
{
    Q_OBJECT
public:
    explicit KeychainServiceMock(QObject * parent = nullptr);

    struct WritePasswordResult
    {
        QUuid m_requestId;
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

    struct ReadPasswordResult
    {
        QUuid m_requestId;
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
        QString m_password;
    };

    struct DeletePasswordResult
    {
        QUuid m_requestId;
        ErrorCode m_errorCode = ErrorCode::NoError;
        ErrorString m_errorDescription;
    };

    using WritePasswordHandler = std::function<WritePasswordResult(
        const QString & service, const QString & key,
        const QString & password)>;

    using ReadPasswordHandler = std::function<ReadPasswordResult(
        const QString & service, const QString & key)>;

    using DeletePasswordHandler = std::function<DeletePasswordResult(
        const QString & service, const QString & key)>;

    void setWritePasswordHandler(WritePasswordHandler handler);
    void setReadPasswordHandler(ReadPasswordHandler handler);
    void setDeletePasswordHandler(DeletePasswordHandler handler);

public:
    virtual QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) override;

    virtual QUuid startReadPasswordJob(
        const QString & service, const QString & key) override;

    virtual QUuid startDeletePasswordJob(
        const QString & service, const QString & key) override;

private:
    WritePasswordHandler m_writePasswordHandler;
    ReadPasswordHandler m_readPasswordHandler;
    DeletePasswordHandler m_deletePasswordHandler;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_UTILITY_KEYCHAIN_KEYCHAIN_SERVICE_MOCK_H
