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

#ifndef LIB_QUENTIER_TESTS_ENML_ENML_TESTER_H
#define LIB_QUENTIER_TESTS_ENML_ENML_TESTER_H

#include <QObject>

namespace quentier {
namespace test {

class ENMLTester final : public QObject
{
    Q_OBJECT
public:
    ENMLTester(QObject * parent = nullptr);
    virtual ~ENMLTester() override;

private Q_SLOTS:
    void init();

    void enmlConverterSimpleTest();
    void enmlConverterToDoTest();
    void enmlConverterEnCryptTest();
    void enmlConverterEnCryptWithModifiedDecryptedTextTest();
    void enmlConverterEnMediaTest();
    void enmlConverterComplexTest();
    void enmlConverterComplexTest2();
    void enmlConverterComplexTest3();
    void enmlConverterComplexTest4();
    void enmlConverterHtmlWithTableHelperTags();
    void enmlConverterHtmlWithTableAndHilitorHelperTags();

    void enexExportImportSingleSimpleNoteTest();
    void enexExportImportSingleNoteWithTagsTest();
    void enexExportImportSingleNoteWithResourcesTest();
    void enexExportImportSingleNoteWithTagsAndResourcesTest();
    void enexExportImportSingleNoteWithTagsButSkipTagsTest();
    void enexExportImportMultipleNotesWithTagsAndResourcesTest();
    void importRealWorldEnexTest();
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_ENML_ENML_TESTER_H
