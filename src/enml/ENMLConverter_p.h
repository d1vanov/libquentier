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

#ifndef LIB_QUENTIER_ENML_ENML_CONVERTER_P_H
#define LIB_QUENTIER_ENML_ENML_CONVERTER_P_H

#include <quentier/enml/ENMLConverter.h>
#include <quentier/types/ErrorString.h>

#include <QFlag>
#include <QStringList>
#include <QXmlStreamAttributes>
#include <QtGlobal>

class QXmlStreamReader;
class QXmlStreamWriter;

namespace quentier {

class DecryptedTextManager;
class HTMLCleaner;

enum class SkipElementOption
{
    SkipWithContents = 0x0,
    SkipButPreserveContents = 0x1,
    DontSkip = 0x2
};

QTextStream & operator<<(QTextStream & strm, const SkipElementOption option);

Q_DECLARE_FLAGS(SkipElementOptions, SkipElementOption);

class Q_DECL_HIDDEN ENMLConverterPrivate final : public QObject
{
    Q_OBJECT
public:
    explicit ENMLConverterPrivate(QObject * parent = nullptr);
    ~ENMLConverterPrivate() override;

    using NoteContentToHtmlExtraData =
        ENMLConverter::NoteContentToHtmlExtraData;

    using SkipHtmlElementRule = ENMLConverter::SkipHtmlElementRule;

    [[nodiscard]] bool htmlToNoteContent(
        const QString & html, const QList<SkipHtmlElementRule> & skipRules,
        QString & noteContent, DecryptedTextManager & decryptedTextManager,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool cleanupExternalHtml(
        const QString & inputHtml, QString & cleanedUpHtml,
        ErrorString & errorDescription) const;

    [[nodiscard]] bool noteContentToHtml(
        const QString & noteContent, QString & html,
        ErrorString & errorDescription,
        DecryptedTextManager & decryptedTextManager,
        NoteContentToHtmlExtraData & extraData) const;

    [[nodiscard]] bool htmlToQTextDocument(
        const QString & html, QTextDocument & doc,
        ErrorString & errorDescription,
        const QList<SkipHtmlElementRule> & skipRules) const;

    [[nodiscard]] bool validateEnml(
        const QString & enml, ErrorString & errorDescription) const;

    [[nodiscard]] bool validateAndFixupEnml(
        QString & enml, ErrorString & errorDescription) const;

    [[nodiscard]] static bool noteContentToPlainText(
        const QString & noteContent, QString & plainText,
        ErrorString & errorMessage);

    [[nodiscard]] static bool noteContentToListOfWords(
        const QString & noteContent, QStringList & listOfWords,
        ErrorString & errorMessage, QString * plainText = nullptr);

    [[nodiscard]] static QStringList plainTextToListOfWords(
        const QString & plainText);

    [[nodiscard]] static QString toDoCheckboxHtml(
        const bool checked, const quint64 idNumber);

    [[nodiscard]] static QString encryptedTextHtml(
        const QString & encryptedText, const QString & hint,
        const QString & cipher, const size_t keyLength,
        const quint64 enCryptIndex);

    [[nodiscard]] static QString decryptedTextHtml(
        const QString & decryptedText, const QString & encryptedText,
        const QString & hint, const QString & cipher, const size_t keyLength,
        const quint64 enDecryptedIndex);

    [[nodiscard]] static QString resourceHtml(
        const qevercloud::Resource & resource, ErrorString & errorDescription);

    static void escapeString(QString & string, const bool simplify);

    [[nodiscard]] bool exportNotesToEnex(
        const QList<qevercloud::Note> & notes,
        const QHash<QString, QString> & tagNamesByTagLocalIds,
        const ENMLConverter::EnexExportTags exportTagsOption, QString & enex,
        ErrorString & errorDescription, const QString & version) const;

    [[nodiscard]] bool importEnex(
        const QString & enex, QList<qevercloud::Note> & notes,
        QHash<QString, QStringList> & tagNamesByNoteLocalId,
        ErrorString & errorDescription) const;

private:
    [[nodiscard]] bool isForbiddenXhtmlTag(const QString & tagName) const;

    [[nodiscard]] bool isForbiddenXhtmlAttribute(
        const QString & attributeName) const;

    [[nodiscard]] bool isEvernoteSpecificXhtmlTag(
        const QString & tagName) const;

    [[nodiscard]] bool isAllowedXhtmlTag(const QString & tagName) const;

    // convert <div> element with decrypted text to ENML <en-crypt> tag
    [[nodiscard]] bool decryptedTextToEnml(
        QXmlStreamReader & reader, DecryptedTextManager & decryptedTextManager,
        QXmlStreamWriter & writer, ErrorString & errorDescription) const;

    // convert ENML en-crypt tag to HTML <object> tag
    [[nodiscard]] bool encryptedTextToHtml(
        const QXmlStreamAttributes & enCryptAttributes,
        const QStringRef & encryptedTextCharacters, const quint64 enCryptIndex,
        const quint64 enDecryptedIndex, QXmlStreamWriter & writer,
        DecryptedTextManager & decryptedTextManager,
        bool & convertedToEnCryptNode) const;

    // convert ENML <en-media> tag to HTML <object> tag
    [[nodiscard]] static bool resourceInfoToHtml(
        const QXmlStreamAttributes & attributes, QXmlStreamWriter & writer,
        ErrorString & errorDescription);

    void toDoTagsToHtml(
        const QXmlStreamReader & reader, const quint64 enToDoIndex,
        QXmlStreamWriter & writer) const;

    static void decryptedTextHtml(
        const QString & decryptedText, const QString & encryptedText,
        const QString & hint, const QString & cipher, const size_t keyLength,
        const quint64 enDecryptedIndex, QXmlStreamWriter & writer);

    [[nodiscard]] bool validateEnex(
        const QString & enex, ErrorString & errorDescription) const;

    [[nodiscard]] bool validateRecoIndex(
        const QString & recoIndex, ErrorString & errorDescription) const;

    [[nodiscard]] bool validateAgainstDtd(
        const QString & input, const QString & dtdFilePath,
        ErrorString & errorDescription) const;

    [[nodiscard]] SkipElementOption skipElementOption(
        const QString & elementName, const QXmlStreamAttributes & attributes,
        const QList<SkipHtmlElementRule> & skipRules) const;

    struct ConversionState
    {
        int m_writeElementCounter = 0;
        QString m_lastElementName;
        QXmlStreamAttributes m_lastElementAttributes;

        bool m_insideEnCryptElement = false;
        bool m_insideEnMediaElement = false;

        QXmlStreamAttributes m_enMediaAttributes;

        size_t m_skippedElementNestingCounter = 0;
        size_t m_skippedElementWithPreservedContentsNestingCounter = 0;
    };

    enum class ProcessElementStatus
    {
        ProcessedPartially = 0,
        ProcessedFully,
        Error
    };

    [[nodiscard]] ProcessElementStatus
    processElementForHtmlToNoteContentConversion(
        const QList<SkipHtmlElementRule> & skipRules, ConversionState & state,
        DecryptedTextManager & decryptedTextManager, QXmlStreamReader & reader,
        QXmlStreamWriter & writer, ErrorString & errorDescription) const;

private:
    Q_DISABLE_COPY(ENMLConverterPrivate)

private:
    const QSet<QString> m_forbiddenXhtmlTags;
    const QSet<QString> m_forbiddenXhtmlAttributes;
    const QSet<QString> m_evernoteSpecificXhtmlTags;
    const QSet<QString> m_allowedXhtmlTags;
    const QSet<QString> m_allowedEnMediaAttributes;

    mutable HTMLCleaner * m_pHtmlCleaner;

    // Cached memory for the html converted to valid xml
    mutable QString m_cachedConvertedXml;
};

} // namespace quentier

QUENTIER_DECLARE_PRINTABLE(QXmlStreamAttributes)

QUENTIER_DECLARE_PRINTABLE(
    QList<quentier::ENMLConverter::SkipHtmlElementRule>)

#endif // LIB_QUENTIER_ENML_ENML_CONVERTER_P_H
