/*
 * Copyright 2016 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_CORE_TESTER_H
#define LIB_QUENTIER_TESTS_CORE_TESTER_H

#include <quentier/utility/Macros.h>
#include <QObject>

namespace quentier {
namespace test {

class CoreTester: public QObject
{
    Q_OBJECT
public:
    explicit CoreTester(QObject * parent = Q_NULLPTR);
    virtual ~CoreTester();

private Q_SLOTS:
    void initTestCase();

    void noteContainsToDoTest();
    void noteContainsEncryptionTest();

    void encryptDecryptNoteTest();
    void decryptNoteAesTest();
    void decryptNoteRc2Test();

    void tagSortByParentChildRelationsTest();

    void resourceRecognitionIndicesParsingTest();

private:
    CoreTester(const CoreTester & other) Q_DECL_EQ_DELETE;
    CoreTester & operator=(const CoreTester & other) Q_DECL_EQ_DELETE;

};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_CORE_TESTER_H
