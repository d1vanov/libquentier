/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#include <quentier/enml/ENMLConverter.h>
#include "ENMLConverter_p.h"
#include <quentier/types/Resource.h>

namespace quentier {

ENMLConverter::ENMLConverter() :
    d_ptr(new ENMLConverterPrivate)
{}

ENMLConverter::~ENMLConverter()
{
    delete d_ptr;
}

bool ENMLConverter::htmlToNoteContent(const QString & html, QString & noteContent,
                                      DecryptedTextManager & decryptedTextManager,
                                      ErrorString & errorDescription,
                                      const QVector<SkipHtmlElementRule> & skipRules) const
{
    Q_D(const ENMLConverter);
    return d->htmlToNoteContent(html, skipRules, noteContent,
                                decryptedTextManager, errorDescription);
}

bool ENMLConverter::cleanupExternalHtml(const QString & inputHtml,
                                        QString & cleanedUpHtml,
                                        ErrorString & errorDescription) const
{
    Q_D(const ENMLConverter);
    return d->cleanupExternalHtml(inputHtml, cleanedUpHtml, errorDescription);
}

bool ENMLConverter::htmlToQTextDocument(const QString & html, QTextDocument & doc,
                                        ErrorString & errorDescription,
                                        const QVector<SkipHtmlElementRule> & skipRules) const
{
    Q_D(const ENMLConverter);
    return d->htmlToQTextDocument(html, doc, errorDescription, skipRules);
}

bool ENMLConverter::noteContentToHtml(const QString & noteContent, QString & html,
                                      ErrorString & errorDescription,
                                      DecryptedTextManager & decryptedTextManager,
                                      NoteContentToHtmlExtraData & extraData) const
{
    Q_D(const ENMLConverter);
    return d->noteContentToHtml(noteContent, html, errorDescription,
                                decryptedTextManager, extraData);
}

bool ENMLConverter::validateEnml(const QString & enml, ErrorString & errorDescription) const
{
    Q_D(const ENMLConverter);
    return d->validateEnml(enml, errorDescription);
}

bool ENMLConverter::validateAndFixupEnml(QString & enml, ErrorString & errorDescription) const
{
    Q_D(const ENMLConverter);
    return d->validateAndFixupEnml(enml, errorDescription);
}

bool ENMLConverter::noteContentToPlainText(const QString & noteContent,
                                           QString & plainText,
                                           ErrorString & errorMessage)
{
    return ENMLConverterPrivate::noteContentToPlainText(noteContent, plainText,
                                                        errorMessage);
}

bool ENMLConverter::noteContentToListOfWords(const QString & noteContent,
                                             QStringList & listOfWords,
                                             ErrorString & errorMessage,
                                             QString * plainText)
{
    return ENMLConverterPrivate::noteContentToListOfWords(noteContent, listOfWords,
                                                          errorMessage, plainText);
}

QStringList ENMLConverter::plainTextToListOfWords(const QString & plainText)
{
    return ENMLConverterPrivate::plainTextToListOfWords(plainText);
}

QString ENMLConverter::toDoCheckboxHtml(const bool checked, const quint64 idNumber)
{
    return ENMLConverterPrivate::toDoCheckboxHtml(checked, idNumber);
}

QString ENMLConverter::encryptedTextHtml(const QString & encryptedText,
                                         const QString & hint,
                                         const QString & cipher,
                                         const size_t keyLength,
                                         const quint64 enCryptIndex)
{
    return ENMLConverterPrivate::encryptedTextHtml(encryptedText, hint, cipher,
                                                   keyLength, enCryptIndex);
}

QString ENMLConverter::decryptedTextHtml(const QString & decryptedText,
                                         const QString & encryptedText,
                                         const QString & hint, const QString & cipher,
                                         const size_t keyLength,
                                         const quint64 enDecryptedIndex)
{
    return ENMLConverterPrivate::decryptedTextHtml(decryptedText, encryptedText,
                                                   hint, cipher, keyLength,
                                                   enDecryptedIndex);
}

QString ENMLConverter::resourceHtml(const Resource & resource,
                                    ErrorString & errorDescription)
{
    return ENMLConverterPrivate::resourceHtml(resource, errorDescription);
}

void ENMLConverter::escapeString(QString & string, const bool simplify)
{
    ENMLConverterPrivate::escapeString(string, simplify);
}

bool ENMLConverter::exportNotesToEnex(const QVector<Note> & notes,
                                      const QHash<QString, QString> & tagNamesByTagLocalUids,
                                      const EnexExportTags::type exportTagsOption,
                                      QString & enex, ErrorString & errorDescription,
                                      const QString & version) const
{
    Q_D(const ENMLConverter);
    return d->exportNotesToEnex(notes, tagNamesByTagLocalUids, exportTagsOption,
                                enex, errorDescription, version);
}

bool ENMLConverter::importEnex(const QString & enex, QVector<Note> & notes,
                               QHash<QString, QStringList> & tagNamesByNoteLocalUid,
                               ErrorString & errorDescription) const
{
    Q_D(const ENMLConverter);
    return d->importEnex(enex, notes, tagNamesByNoteLocalUid, errorDescription);
}

QTextStream & ENMLConverter::SkipHtmlElementRule::print(QTextStream & strm) const
{
#define PRINT_COMPARISON_RULE(rule)                                            \
    switch(rule)                                                               \
    {                                                                          \
        case Equals:                                                           \
            strm << "Equals";                                                  \
            break;                                                             \
        case StartsWith:                                                       \
            strm << "Starts with";                                             \
            break;                                                             \
        case EndsWith:                                                         \
            strm << "Ends with";                                               \
            break;                                                             \
        case Contains:                                                         \
            strm << "Contains";                                                \
            break;                                                             \
        default:                                                               \
            strm << "Unknown (" << static_cast<qint64>(rule) << ")";           \
            break;                                                             \
    }                                                                          \
// PRINT_COMPARISON_RULE

    strm << "SkipHtmlElementRule: {\n";
    strm << "  element name to skip = " << m_elementNameToSkip << ", rule: ";
    PRINT_COMPARISON_RULE(m_elementNameComparisonRule)
    strm << ", case "
         << ((m_elementNameCaseSensitivity == Qt::CaseSensitive)
             ? "sensitive"
             : "insensitive");
    strm << "\n";

    strm << "  attribute name to skip = "
         << m_attributeNameToSkip
         << ", rule: ";
    PRINT_COMPARISON_RULE(m_attributeNameComparisonRule)
    strm << ", case "
         << ((m_attributeNameCaseSensitivity == Qt::CaseSensitive)
             ? "sensitive"
             : "insensitive");
    strm << "\n";

    strm << "  attribute value to skip = "
         << m_attributeValueToSkip
         << ", rule: ";
    PRINT_COMPARISON_RULE(m_attributeValueComparisonRule)
    strm << ", case "
         << ((m_attributeValueCaseSensitivity == Qt::CaseSensitive)
             ? "sensitive"
             : "insensitive");
    strm << "\n}\n";

    return strm;
}

} // namespace quentier

