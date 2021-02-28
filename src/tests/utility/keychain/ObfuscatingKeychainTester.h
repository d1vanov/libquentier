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

#ifndef LIB_QUENTIER_TESTS_UTILITY_KEYCHAIN_OBFUSCATING_KEYCHAIN_TESTER_H
#define LIB_QUENTIER_TESTS_UTILITY_KEYCHAIN_OBFUSCATING_KEYCHAIN_TESTER_H

#include <QObject>

namespace quentier {
namespace test {

class ObfuscatingKeychainTester final : public QObject
{
    Q_OBJECT
public:
    explicit ObfuscatingKeychainTester(QObject * parent = nullptr);

private Q_SLOTS:
    void checkWriteReadPassword();
    void checkWriteDeletePassword();
    void checkDeletePasswordWithoutWriting();

private:
    QString settingsGroupName() const;

private:
    const QString m_service = QStringLiteral("service");
    const QString m_key = QStringLiteral("key");
    const QString m_password = QStringLiteral("password");
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_UTILITY_KEYCHAIN_OBFUSCATING_KEYCHAIN_TESTER_H
