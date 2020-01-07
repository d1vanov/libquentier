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

#ifndef LIB_QUENTIER_ENML_ENML_CONVERTER_P_H
#define LIB_QUENTIER_ENML_ENML_CONVERTER_P_H

#include <quentier/enml/ENMLConverter.h>
#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>

#include <QtGlobal>
#include <QStringList>
#include <QFlag>

QT_FORWARD_DECLARE_CLASS(QXmlStreamReader)
QT_FORWARD_DECLARE_CLASS(QXmlStreamWriter)
QT_FORWARD_DECLARE_CLASS(QXmlStreamAttributes)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Resource)
QT_FORWARD_DECLARE_CLASS(DecryptedTextManager)
QT_FORWARD_DECLARE_CLASS(HTMLCleaner)

class Q_DECL_HIDDEN ShouldSkipElementResult: public Printable
{
public:
    enum type
    {
        SkipWithContents = 0x0,
        SkipButPreserveContents = 0x1,
        ShouldNotSkip = 0x2
    };

    Q_DECLARE_FLAGS(Types, type)

    virtual QTextStream & print(QTextStream & strm) const override;
};

class Q_DECL_HIDDEN ENMLConverterPrivate: public QObject
{
    Q_OBJECT
public:
    explicit ENMLConverterPrivate(QObject * parent = nullptr);
    ~ENMLConverterPrivate();

    typedef ENMLConverter::NoteContentToHtmlExtraData NoteContentToHtmlExtraData;
    typedef ENMLConverter::SkipHtmlElementRule SkipHtmlElementRule;

    bool htmlToNoteContent(const QString & html,
                           const QVector<SkipHtmlElementRule> & skipRules,
                           QString & noteContent,
                           DecryptedTextManager & decryptedTextManager,
                           ErrorString & errorDescription) const;

    bool cleanupExternalHtml(const QString & inputHtml, QString & cleanedUpHtml,
                             ErrorString & errorDescription) const;

    bool noteContentToHtml(const QString & noteContent, QString & html,
                           ErrorString & errorDescription,
                           DecryptedTextManager & decryptedTextManager,
                           NoteContentToHtmlExtraData & extraData) const;

    bool htmlToQTextDocument(const QString & html, QTextDocument & doc,
                             ErrorString & errorDescription,
                             const QVector<SkipHtmlElementRule> & skipRules) const;

    bool validateEnml(const QString & enml, ErrorString & errorDescription) const;

    bool validateAndFixupEnml(QString & enml, ErrorString & errorDescription) const;

    static bool noteContentToPlainText(const QString & noteContent,
                                       QString & plainText,
                                       ErrorString & errorMessage);

    static bool noteContentToListOfWords(const QString & noteContent,
                                         QStringList & listOfWords,
                                         ErrorString & errorMessage,
                                         QString * plainText = nullptr);

    static QStringList plainTextToListOfWords(const QString & plainText);

    static QString toDoCheckboxHtml(const bool checked, const quint64 idNumber);

    static QString encryptedTextHtml(const QString & encryptedText,
                                     const QString & hint, const QString & cipher,
                                     const size_t keyLength,
                                     const quint64 enCryptIndex);

    static QString decryptedTextHtml(const QString & decryptedText,
                                     const QString & encryptedText,
                                     const QString & hint, const QString & cipher,
                                     const size_t keyLength,
                                     const quint64 enDecryptedIndex);

    static QString resourceHtml(const Resource & resource,
                                ErrorString & errorDescription);

    static void escapeString(QString & string, const bool simplify);

    bool exportNotesToEnex(const QVector<Note> & notes,
                           const QHash<QString, QString> & tagNamesByTagLocalUids,
                           const ENMLConverter::EnexExportTags::type exportTagsOption,
                           QString & enex, ErrorString & errorDescription,
                           const QString & version) const;

    bool importEnex(const QString & enex, QVector<Note> & notes,
                    QHash<QString, QStringList> & tagNamesByNoteLocalUid,
                    ErrorString & errorDescription) const;

private:
    bool isForbiddenXhtmlTag(const QString & tagName) const;
    bool isForbiddenXhtmlAttribute(const QString & attributeName) const;
    bool isEvernoteSpecificXhtmlTag(const QString & tagName) const;
    bool isAllowedXhtmlTag(const QString & tagName) const;

    // convert <div> element with decrypted text to ENML <en-crypt> tag
    bool decryptedTextToEnml(QXmlStreamReader & reader,
                             DecryptedTextManager & decryptedTextManager,
                             QXmlStreamWriter & writer,
                             ErrorString & errorDescription) const;

    // convert ENML en-crypt tag to HTML <object> tag
    bool encryptedTextToHtml(const QXmlStreamAttributes & enCryptAttributes,
                             const QStringRef & encryptedTextCharacters,
                             const quint64 enCryptIndex, const quint64 enDecryptedIndex,
                             QXmlStreamWriter & writer,
                             DecryptedTextManager & decryptedTextManager,
                             bool & convertedToEnCryptNode) const;

    // convert ENML <en-media> tag to HTML <object> tag
    static bool resourceInfoToHtml(const QXmlStreamAttributes & attributes,
                                   QXmlStreamWriter & writer,
                                   ErrorString & errorDescription);

    void toDoTagsToHtml(const QXmlStreamReader & reader, const quint64 enToDoIndex,
                        QXmlStreamWriter & writer) const;

    static void decryptedTextHtml(const QString & decryptedText,
                                  const QString & encryptedText,
                                  const QString & hint, const QString & cipher,
                                  const size_t keyLength, const quint64 enDecryptedIndex,
                                  QXmlStreamWriter & writer);

    bool validateEnex(const QString & enex, ErrorString & errorDescription) const;

    bool validateRecoIndex(const QString & recoIndex, ErrorString & errorDescription) const;

    bool validateAgainstDtd(const QString & input, const QString & dtdFilePath,
                            ErrorString & errorDescription) const;

    qint64 timestampFromDateTime(const QDateTime & dateTime) const;

    ShouldSkipElementResult::type shouldSkipElement(const QString & elementName,
                                                    const QXmlStreamAttributes & attributes,
                                                    const QVector<SkipHtmlElementRule> & skipRules) const;
private:
    Q_DISABLE_COPY(ENMLConverterPrivate)

private:
    const QSet<QString>     m_forbiddenXhtmlTags;
    const QSet<QString>     m_forbiddenXhtmlAttributes;
    const QSet<QString>     m_evernoteSpecificXhtmlTags;
    const QSet<QString>     m_allowedXhtmlTags;
    const QSet<QString>     m_allowedEnMediaAttributes;

    mutable HTMLCleaner *   m_pHtmlCleaner;
    mutable QString         m_cachedConvertedXml;   // Cached memory for the html converted to valid xml
};

} // namespace quentier

QUENTIER_DECLARE_PRINTABLE(QXmlStreamAttributes)
QUENTIER_DECLARE_PRINTABLE(QVector<quentier::ENMLConverter::SkipHtmlElementRule>)

#endif // LIB_QUENTIER_ENML_ENML_CONVERTER_P_H
