/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_UTILITY_UTILITY_TESTER_H
#define LIB_QUENTIER_TESTS_UTILITY_UTILITY_TESTER_H

#include <QObject>

namespace quentier {
namespace test {

class UtilityTester final : public QObject
{
    Q_OBJECT
public:
    explicit UtilityTester(QObject * parent = nullptr);
    virtual ~UtilityTester() override;

private Q_SLOTS:
    void init();

    void encryptDecryptNoteTest();
    void decryptNoteAesTest();
    void decryptNoteRc2Test();

    void tagSortByParentChildRelationsTest();

    void lruCacheTests();

private:
    Q_DISABLE_COPY(UtilityTester)
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_UTILITY_UTILITY_TESTER_H
