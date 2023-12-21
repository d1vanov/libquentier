/*
 * Copyright 2023 Dmitry Ivanov
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

#include "Converter.h"
#include "HtmlData.h"
#include "HtmlUtils.h"

#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/enml/IENMLTagsConverter.h>
#include <quentier/enml/conversion_rules/ISkipRule.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Unreachable.h>

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFlags>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <libxml/xmlreader.h>

#include <utility>

namespace quentier::enml {

namespace {

enum class SkipElementOption
{
    SkipWithContents = 0x0,
    SkipButPreserveContents = 0x1,
    DontSkip = 0x2
};

QTextStream & operator<<(QTextStream & strm, SkipElementOption option);

Q_DECLARE_FLAGS(SkipElementOptions, SkipElementOption);

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] SkipElementOption skipElementOption(
    const QString & elementName, const QXmlStreamAttributes & attributes,
    const QList<conversion_rules::ISkipRulePtr> & skipRules)
{
    QNDEBUG(
        "enml::Converter",
        "skipElementOption: element name = " << elementName << ", attributes = "
                                             << attributes);

    if (skipRules.isEmpty()) {
        return SkipElementOption::DontSkip;
    }

    SkipElementOptions flags;
    flags |= SkipElementOption::DontSkip;

    const auto getShouldSkip = [](const QStringRef & checkedValue,
                                  const QString & ruleValue,
                                  const conversion_rules::MatchMode matchMode,
                                  const Qt::CaseSensitivity caseSensitivity) {
        if (checkedValue.isEmpty()) {
            return false;
        }

        switch (matchMode) {
        case conversion_rules::MatchMode::Equals:
            return checkedValue.compare(ruleValue, caseSensitivity) == 0;
        case conversion_rules::MatchMode::StartsWith:
            return checkedValue.startsWith(ruleValue, caseSensitivity);
        case conversion_rules::MatchMode::EndsWith:
            return checkedValue.endsWith(ruleValue, caseSensitivity);
        case conversion_rules::MatchMode::Contains:
            return checkedValue.contains(ruleValue, caseSensitivity);
        }

        UNREACHABLE;
    };

    for (const auto & skipRule: skipRules) {
        Q_ASSERT(skipRule);

        const auto & ruleValue = skipRule->value();
        const auto matchMode = skipRule->matchMode();
        const auto caseSensitivity = skipRule->caseSensitivity();
        const auto target = skipRule->target();
        bool shouldSkip = false;

        switch (target) {
        case conversion_rules::ISkipRule::Target::Element:
            shouldSkip = getShouldSkip(
                QStringRef{&elementName}, ruleValue, matchMode,
                caseSensitivity);
            break;
        case conversion_rules::ISkipRule::Target::AttibuteName:
        case conversion_rules::ISkipRule::Target::AttributeValue:
            for (const auto & attribute: std::as_const(attributes)) {
                const QStringRef checkedValue =
                    (target == conversion_rules::ISkipRule::Target::AttibuteName
                         ? attribute.name()
                         : attribute.value());
                shouldSkip = getShouldSkip(
                    checkedValue, ruleValue, matchMode, caseSensitivity);
                if (shouldSkip) {
                    break;
                }
            }
            break;
        }

        if (shouldSkip) {
            if (skipRule->includeContents()) {
                flags |= SkipElementOption::SkipButPreserveContents;
            }
            else {
                return SkipElementOption::SkipWithContents;
            }
        }
    }

    if (flags.testFlag(SkipElementOption::SkipButPreserveContents)) {
        return SkipElementOption::SkipButPreserveContents;
    }

    return SkipElementOption::DontSkip;
}

[[nodiscard]] Result<void, ErrorString> decryptedTextToEnml(
    QXmlStreamReader & reader, IDecryptedTextCache & decryptedTextCache,
    QXmlStreamWriter & writer)
{
    QNDEBUG("enml::Converter", "decryptedTextToEnml");

    const QXmlStreamAttributes attributes = reader.attributes();
    if (!attributes.hasAttribute(QStringLiteral("encrypted_text"))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Missing encrypted text attribute in en-decrypted div tag")};
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    QString encryptedText =
        attributes.value(QStringLiteral("encrypted_text")).toString();

    const auto decryptedTextInfo =
        decryptedTextCache.findDecryptedTextInfo(encryptedText);
    if (!decryptedTextInfo) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Can't find cached decrypted text by its encrypted text")};
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    QString actualDecryptedText;
    QXmlStreamWriter decryptedTextWriter{&actualDecryptedText};

    int nestedElementsCounter = 0;
    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            decryptedTextWriter.writeStartElement(reader.name().toString());
            decryptedTextWriter.writeAttributes(reader.attributes());
            ++nestedElementsCounter;
        }

        if (reader.isCharacters()) {
            decryptedTextWriter.writeCharacters(reader.text().toString());
        }

        if (reader.isEndElement()) {
            if (nestedElementsCounter > 0) {
                decryptedTextWriter.writeEndElement();
                --nestedElementsCounter;
            }
            else {
                break;
            }
        }
    }

    if (reader.hasError()) {
        ErrorString errorDescription{
            QT_TRANSLATE_NOOP("enml::Converter", "Text decryption failed")};
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml::Converter",
            "Couldn't read the nested contents of en-decrypted "
                << "div, reader has error: " << errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    if (decryptedTextInfo->first != actualDecryptedText) {
        QNTRACE(
            "enml::Converter",
            "Found modified decrypted text, need to re-encrypt");

        if (const auto actualEncryptedText =
                decryptedTextCache.updateDecryptedTextInfo(
                    encryptedText, actualDecryptedText))
        {
            QNTRACE(
                "enml::Converter",
                "Re-evaluated the modified decrypted text's "
                    << "encrypted text; was: " << encryptedText
                    << "; new: " << *actualEncryptedText);
            encryptedText = *actualEncryptedText;
        }
    }

    QString hint;
    if (attributes.hasAttribute(QStringLiteral("hint"))) {
        hint = attributes.value(QStringLiteral("hint")).toString();
    }

    writer.writeStartElement(QStringLiteral("en-crypt"));

    if (attributes.hasAttribute(QStringLiteral("cipher"))) {
        writer.writeAttribute(
            QStringLiteral("cipher"),
            attributes.value(QStringLiteral("cipher")).toString());
    }

    if (attributes.hasAttribute(QStringLiteral("length"))) {
        writer.writeAttribute(
            QStringLiteral("length"),
            attributes.value(QStringLiteral("length")).toString());
    }

    if (!hint.isEmpty()) {
        writer.writeAttribute(QStringLiteral("hint"), hint);
    }

    writer.writeCharacters(encryptedText);
    writer.writeEndElement();

    QNTRACE(
        "enml::Converter", "Wrote en-crypt ENML tag from en-decrypted p tag");
    return Result<void, ErrorString>{};
}

void decryptedTextToHtml(
    const QString & decryptedText, const QString & encryptedText,
    const QString & hint, const QString & cipher, const size_t keyLength,
    const quint64 enDecryptedIndex, QXmlStreamWriter & writer)
{
    writer.writeStartElement(QStringLiteral("div"));

    writer.writeAttribute(
        QStringLiteral("en-tag"), QStringLiteral("en-decrypted"));

    writer.writeAttribute(QStringLiteral("encrypted_text"), encryptedText);

    writer.writeAttribute(
        QStringLiteral("en-decrypted-id"), QString::number(enDecryptedIndex));

    writer.writeAttribute(
        QStringLiteral("class"),
        QStringLiteral("en-decrypted hvr-border-color"));

    if (!cipher.isEmpty()) {
        writer.writeAttribute(QStringLiteral("cipher"), cipher);
    }

    if (keyLength != 0) {
        writer.writeAttribute(
            QStringLiteral("length"), QString::number(keyLength));
    }

    if (!hint.isEmpty()) {
        writer.writeAttribute(QStringLiteral("hint"), hint);
    }

    QString formattedDecryptedText = decryptedText;

    formattedDecryptedText.prepend(QStringLiteral(
        "<?xml version=\"1.0\"?>"
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
        "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
        "<div id=\"decrypted_text_html_to_enml_temporary\">"));

    formattedDecryptedText.append(QStringLiteral("</div>"));

    QXmlStreamReader decryptedTextReader(formattedDecryptedText);
    bool foundFormattedText = false;

    while (!decryptedTextReader.atEnd()) {
        Q_UNUSED(decryptedTextReader.readNext());

        if (decryptedTextReader.isStartElement()) {
            const auto attributes = decryptedTextReader.attributes();
            if (attributes.hasAttribute(QStringLiteral("id")) &&
                (attributes.value(QStringLiteral("id")) ==
                 QStringLiteral("decrypted_text_html_to_enml_temporary")))
            {
                QNTRACE(
                    "enml::Converter",
                    "Skipping the start of temporarily added div");
                continue;
            }

            writer.writeStartElement(decryptedTextReader.name().toString());
            writer.writeAttributes(attributes);

            foundFormattedText = true;

            QNTRACE(
                "enml::Converter",
                "Wrote start element from decrypted text: "
                    << decryptedTextReader.name());
        }

        if (decryptedTextReader.isCharacters()) {
            writer.writeCharacters(decryptedTextReader.text().toString());

            foundFormattedText = true;

            QNTRACE(
                "enml::Converter",
                "Wrote characters from decrypted text: "
                    << decryptedTextReader.text());
        }

        if (decryptedTextReader.isEndElement()) {
            const auto attributes = decryptedTextReader.attributes();
            if (attributes.hasAttribute(QStringLiteral("id")) &&
                (attributes.value(QStringLiteral("id")) ==
                 QStringLiteral("decrypted_text_html_to_enml_temporary")))
            {
                QNTRACE(
                    "enml::Converter",
                    "Skipping the end of temporarily added div");
                continue;
            }

            writer.writeEndElement();

            QNTRACE(
                "enml::Converter",
                "Wrote end element from decrypted text: "
                    << decryptedTextReader.name());
        }
    }

    if (decryptedTextReader.hasError()) {
        QNWARNING(
            "enml::Converter",
            "Decrypted text reader has error: "
                << decryptedTextReader.errorString());
    }

    if (!foundFormattedText) {
        writer.writeCharacters(decryptedText);
        QNTRACE(
            "enml::Converter",
            "Wrote unformatted decrypted text: " << decryptedText);
    }
}

void encryptedTextToHtml(
    const QXmlStreamAttributes & enCryptAttributes,
    const QStringRef & encryptedTextCharacters, const quint64 enCryptIndex,
    const quint64 enDecryptedIndex, QXmlStreamWriter & writer,
    enml::IDecryptedTextCache & decryptedTextCache,
    bool & convertedToEnCryptNode)
{
    QNDEBUG(
        "enml::Converter",
        "encryptedTextToHtml: "
            << "encrypted text = " << encryptedTextCharacters
            << ", en-crypt index = " << enCryptIndex
            << ", en-decrypted index = " << enDecryptedIndex);

    QString cipher;
    if (enCryptAttributes.hasAttribute(QStringLiteral("cipher"))) {
        cipher = enCryptAttributes.value(QStringLiteral("cipher")).toString();
    }

    QString length;
    if (enCryptAttributes.hasAttribute(QStringLiteral("length"))) {
        length = enCryptAttributes.value(QStringLiteral("length")).toString();
    }

    QString hint;
    if (enCryptAttributes.hasAttribute(QStringLiteral("hint"))) {
        hint = enCryptAttributes.value(QStringLiteral("hint")).toString();
    }

    const auto decryptedTextInfo = decryptedTextCache.findDecryptedTextInfo(
        encryptedTextCharacters.toString());

    if (decryptedTextInfo) {
        QNTRACE(
            "enml::Converter",
            "Found encrypted text which has already been "
                << "decrypted and cached; encrypted text = "
                << encryptedTextCharacters);

        std::size_t keyLength = 0;
        if (!length.isEmpty()) {
            bool conversionResult = false;
            keyLength = static_cast<size_t>(length.toUInt(&conversionResult));
            if (!conversionResult) {
                QNWARNING(
                    "enml::Converter",
                    "Can't convert encryption key length from string to "
                        << "unsigned integer: " << length);
                keyLength = 0;
            }
        }

        decryptedTextToHtml(
            decryptedTextInfo->first, encryptedTextCharacters.toString(), hint,
            cipher, keyLength, enDecryptedIndex, writer);

        convertedToEnCryptNode = false;
        return;
    }

    convertedToEnCryptNode = true;

    writer.writeStartElement(QStringLiteral("img"));
    writer.writeAttribute(QStringLiteral("src"), QString());
    writer.writeAttribute(QStringLiteral("en-tag"), QStringLiteral("en-crypt"));

    writer.writeAttribute(
        QStringLiteral("class"), QStringLiteral("en-crypt hvr-border-color"));

    if (!hint.isEmpty()) {
        writer.writeAttribute(QStringLiteral("hint"), hint);
    }

    if (!cipher.isEmpty()) {
        writer.writeAttribute(QStringLiteral("cipher"), cipher);
    }

    if (!length.isEmpty()) {
        writer.writeAttribute(QStringLiteral("length"), length);
    }

    writer.writeAttribute(
        QStringLiteral("encrypted_text"), encryptedTextCharacters.toString());

    QNTRACE(
        "enml::Converter", "Wrote element corresponding to en-crypt ENML tag");

    writer.writeAttribute(
        QStringLiteral("en-crypt-id"), QString::number(enCryptIndex));
}

[[nodiscard]] Result<void, ErrorString> resourceInfoToHtml(
    const QXmlStreamAttributes & attributes, QXmlStreamWriter & writer)
{
    QNDEBUG("enml", "ENMLConverterPrivate::resourceInfoToHtml");

    if (!attributes.hasAttribute(QStringLiteral("hash"))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Detected incorrect en-media tag missing hash attribute")};
        QNDEBUG("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    if (!attributes.hasAttribute(QStringLiteral("type"))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Detected incorrect en-media tag missing type attribute")};
        QNDEBUG("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    const QStringRef mimeType = attributes.value(QStringLiteral("type"));
    bool inlineImage = false;
    if (mimeType.startsWith(QStringLiteral("image"), Qt::CaseInsensitive)) {
        // TODO: consider some proper high-level interface for making it
        // possible to customize ENML <--> HTML conversion
        inlineImage = true;
    }

    writer.writeStartElement(
        inlineImage ? QStringLiteral("img") : QStringLiteral("object"));

    // NOTE: Converter cannot set src attribute for img tag as it doesn't know
    // whether the resource is stored in any local file yet.
    // The user of convertEnmlToHtml should take care of those img tags and
    // their src attributes

    writer.writeAttribute(QStringLiteral("en-tag"), QStringLiteral("en-media"));

    if (inlineImage) {
        writer.writeAttributes(attributes);
        writer.writeAttribute(
            QStringLiteral("class"), QStringLiteral("en-media-image"));
    }
    else {
        writer.writeAttribute(
            QStringLiteral("class"),
            QStringLiteral("en-media-generic hvr-border-color"));

        writer.writeAttributes(attributes);
        writer.writeAttribute(
            QStringLiteral("src"),
            QStringLiteral("qrc:/generic_resource_icons/png/attachment.png"));
    }

    return Result<void, ErrorString>{};
}

void toDoTagsToHtml(
    const QXmlStreamReader & reader, const quint64 enToDoIndex,
    QXmlStreamWriter & writer)
{
    const QXmlStreamAttributes originalAttributes = reader.attributes();
    bool checked = false;
    if (originalAttributes.hasAttribute(QStringLiteral("checked"))) {
        const QStringRef checkedStr =
            originalAttributes.value(QStringLiteral("checked"));

        if (checkedStr == QStringLiteral("true")) {
            checked = true;
        }
    }

    QNTRACE(
        "enml::Converter",
        "Converting " << (checked ? "completed" : "not yet completed")
                      << " ToDo item");

    writer.writeStartElement(QStringLiteral("img"));

    QXmlStreamAttributes attributes;
    attributes.append(
        QStringLiteral("src"),
        QStringLiteral("qrc:/checkbox_icons/checkbox_") +
            (checked ? QStringLiteral("yes") : QStringLiteral("no")) +
            QStringLiteral(".png"));

    attributes.append(
        QStringLiteral("class"),
        QStringLiteral("checkbox_") +
            (checked ? QStringLiteral("checked")
                     : QStringLiteral("unchecked")));

    attributes.append(QStringLiteral("en-tag"), QStringLiteral("en-todo"));

    attributes.append(
        QStringLiteral("en-todo-id"), QString::number(enToDoIndex));

    writer.writeAttributes(attributes);
}

void xmlValidationErrorFunc(void * ctx, const char * msg, va_list args)
{
    QNDEBUG("enml::Converter", "xmlValidationErrorFunc");

    QString currentError = QString::asprintf(msg, args);

    auto * pErrorString = reinterpret_cast<QString *>(ctx);
    *pErrorString += currentError;
    QNDEBUG("enml::Converter", "Error string: " << *pErrorString);
}

} // namespace

// clang-format off
Converter::Converter(IENMLTagsConverterPtr enmlTagsConverter) :
    m_enmlTagsConverter{std::move(enmlTagsConverter)},
    m_forbiddenXhtmlTags{
        QSet<QString>{
            QStringLiteral("applet"),
            QStringLiteral("base"),
            QStringLiteral("basefont"),
            QStringLiteral("bgsound"),
            QStringLiteral("body"),
            QStringLiteral("button"),
            QStringLiteral("dir"),
            QStringLiteral("embed"),
            QStringLiteral("fieldset"),
            QStringLiteral("form"),
            QStringLiteral("frame"),
            QStringLiteral("frameset"),
            QStringLiteral("head"),
            QStringLiteral("html"),
            QStringLiteral("iframe"),
            QStringLiteral("ilayer"),
            QStringLiteral("input"),
            QStringLiteral("isindex"),
            QStringLiteral("label"),
            QStringLiteral("layer"),
            QStringLiteral("legend"),
            QStringLiteral("link"),
            QStringLiteral("marquee"),
            QStringLiteral("menu"),
            QStringLiteral("meta"),
            QStringLiteral("noframes"),
            QStringLiteral("noscript"),
            QStringLiteral("object"),
            QStringLiteral("optgroup"),
            QStringLiteral("option"),
            QStringLiteral("param"),
            QStringLiteral("plaintext"),
            QStringLiteral("script"),
            QStringLiteral("select"),
            QStringLiteral("style"),
            QStringLiteral("textarea"),
            QStringLiteral("xml")}},
    m_forbiddenXhtmlAttributes{
        QSet<QString>{
            QStringLiteral("id"),
            QStringLiteral("class"),
            QStringLiteral("onclick"),
            QStringLiteral("ondblclick"),
            QStringLiteral("accesskey"),
            QStringLiteral("data"),
            QStringLiteral("dynsrc"),
            QStringLiteral("tableindex"),
            QStringLiteral("contenteditable")}},
    m_evernoteSpecificXhtmlTags{
        QSet<QString>{
            QStringLiteral("en-note"),
            QStringLiteral("en-media"),
            QStringLiteral("en-crypt"),
            QStringLiteral("en-todo")}},
    m_allowedXhtmlTags{
        QSet<QString>{
            QStringLiteral("a"),
            QStringLiteral("abbr"),
            QStringLiteral("acronym"),
            QStringLiteral("address"),
            QStringLiteral("area"),
            QStringLiteral("b"),
            QStringLiteral("bdo"),
            QStringLiteral("big"),
            QStringLiteral("blockquote"),
            QStringLiteral("br"),
            QStringLiteral("caption"),
            QStringLiteral("center"),
            QStringLiteral("cite"),
            QStringLiteral("code"),
            QStringLiteral("col"),
            QStringLiteral("colgroup"),
            QStringLiteral("dd"),
            QStringLiteral("del"),
            QStringLiteral("dfn"),
            QStringLiteral("div"),
            QStringLiteral("dl"),
            QStringLiteral("dt"),
            QStringLiteral("em"),
            QStringLiteral("font"),
            QStringLiteral("h1"),
            QStringLiteral("h2"),
            QStringLiteral("h3"),
            QStringLiteral("h4"),
            QStringLiteral("h5"),
            QStringLiteral("h6"),
            QStringLiteral("hr"),
            QStringLiteral("i"),
            QStringLiteral("img"),
            QStringLiteral("ins"),
            QStringLiteral("kbd"),
            QStringLiteral("li"),
            QStringLiteral("map"),
            QStringLiteral("object"),
            QStringLiteral("ol"),
            QStringLiteral("p"),
            QStringLiteral("pre"),
            QStringLiteral("q"),
            QStringLiteral("s"),
            QStringLiteral("samp"),
            QStringLiteral("small"),
            QStringLiteral("span"),
            QStringLiteral("strike"),
            QStringLiteral("strong"),
            QStringLiteral("sub"),
            QStringLiteral("sup"),
            QStringLiteral("table"),
            QStringLiteral("tbody"),
            QStringLiteral("td"),
            QStringLiteral("tfoot"),
            QStringLiteral("th"),
            QStringLiteral("thead"),
            QStringLiteral("title"),
            QStringLiteral("tr"),
            QStringLiteral("tt"),
            QStringLiteral("u"),
            QStringLiteral("ul"),
            QStringLiteral("var"),
            QStringLiteral("xmp")}},
    m_allowedEnMediaAttributes{
        QSet<QString>{
            QStringLiteral("hash"),
            QStringLiteral("type"),
            QStringLiteral("align"),
            QStringLiteral("alt"),
            QStringLiteral("longdesc"),
            QStringLiteral("height"),
            QStringLiteral("width"),
            QStringLiteral("border"),
            QStringLiteral("hspace"),
            QStringLiteral("vspace"),
            QStringLiteral("usemap"),
            QStringLiteral("style"),
            QStringLiteral("title"),
            QStringLiteral("lang"),
            QStringLiteral("dir")}}
{
    if (Q_UNLIKELY(!m_enmlTagsConverter)) {
        throw InvalidArgument{ErrorString{
            "Converter ctor: ENML tags converter is null"}};
    }
}
// clang-format on

Result<QString, ErrorString> Converter::convertHtmlToEnml(
    const QString & html, IDecryptedTextCache & decryptedTextCache,
    const QList<conversion_rules::ISkipRulePtr> & skipRules) const
{
    auto res = convertHtmlToXml(html);
    if (!res.isValid()) {
        return res;
    }

    QNTRACE("enml::Converter", "HTML converted to XML: " << res.get());

    QXmlStreamReader reader{res.get()};

    QBuffer noteContentBuffer;
    if (Q_UNLIKELY(!noteContentBuffer.open(QIODevice::WriteOnly))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Failed to open the buffer to write the converted ENML")};
        errorDescription.details() = noteContentBuffer.errorString();
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamWriter writer{&noteContentBuffer};
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();
    writer.writeDTD(
        QStringLiteral("<!DOCTYPE en-note SYSTEM "
                       "\"http://xml.evernote.com/pub/enml2.dtd\">"));

    ConversionState state;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            ErrorString errorDescription;
            auto status = processElementForHtmlToNoteContentConversion(
                skipRules, state, decryptedTextCache, reader, writer,
                errorDescription);

            if (status == ProcessElementStatus::Error) {
                return Result<QString, ErrorString>{
                    std::move(errorDescription)};
            }

            if (status == ProcessElementStatus::ProcessedFully) {
                continue;
            }
        }

        if ((state.m_writeElementCounter > 0) && reader.isCharacters()) {
            if (state.m_skippedElementNestingCounter) {
                continue;
            }

            if (state.m_insideEnMediaElement) {
                continue;
            }

            if (state.m_insideEnCryptElement) {
                continue;
            }

            const QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("enml::Converter", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("enml::Converter", "Wrote characters: " << text);
            }
        }

        if (reader.isEndElement()) {
            if (state.m_skippedElementNestingCounter) {
                --state.m_skippedElementNestingCounter;
                continue;
            }

            if (state.m_skippedElementWithPreservedContentsNestingCounter) {
                --state.m_skippedElementWithPreservedContentsNestingCounter;
                continue;
            }

            if (state.m_writeElementCounter <= 0) {
                continue;
            }

            if (state.m_insideEnMediaElement) {
                state.m_insideEnMediaElement = false;
            }

            if (state.m_insideEnCryptElement) {
                state.m_insideEnCryptElement = false;
            }

            writer.writeEndElement();
            --state.m_writeElementCounter;
        }
    }

    if (reader.hasError()) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter", "Can't convert the note's html to ENML")};
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml::Converter",
            "Error reading html: " << errorDescription << ", HTML: " << html
                                   << "\nXML: " << res.get());
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QString enml = QString::fromUtf8(noteContentBuffer.buffer());
    QNTRACE("enml::Converter", "Converted ENML: " << enml);

    res = validateAndFixupEnml(enml);
    if (!res.isValid()) {
        QNWARNING(
            "enml::Converter",
            res.error() << ", ENML: " << enml << "\nHTML: " << html);
        return Result<QString, ErrorString>{std::move(res.error())};
    }

    return Result<QString, ErrorString>{std::move(res.get())};
}

Result<void, ErrorString> Converter::convertHtmlToDoc(
    const QString & html, QTextDocument & doc,
    const QList<conversion_rules::ISkipRulePtr> & skipRules) const
{
    auto res = convertHtmlToXml(html);
    if (!res.isValid()) {
        return Result<void, ErrorString>{std::move(res.error())};
    }

    QXmlStreamReader reader{res.get()};

    QBuffer simplifiedHtmlBuffer;
    if (Q_UNLIKELY(!simplifiedHtmlBuffer.open(QIODevice::WriteOnly))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Failed to open the buffer to write the simplified html into")};
        errorDescription.details() = simplifiedHtmlBuffer.errorString();
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamWriter writer{&simplifiedHtmlBuffer};
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    writer.writeDTD(
        QStringLiteral("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "
                       "\"http://www.w3.org/TR/html4/strict.dtd\">"));

    int writeElementCounter = 0;
    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    std::size_t skippedElementNestingCounter = 0;
    std::size_t skippedElementWithPreservedContentsNestingCounter = 0;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext())

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            if (skippedElementNestingCounter) {
                QNTRACE(
                    "enml::Converter",
                    "Skipping everything inside element "
                        << "skipped together with its contents");
                ++skippedElementNestingCounter;
                continue;
            }

            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            auto shouldSkip = skipElementOption(
                lastElementName, lastElementAttributes, skipRules);

            if (shouldSkip != SkipElementOption::DontSkip) {
                QNTRACE(
                    "enml::Converter",
                    "Skipping element "
                        << lastElementName
                        << " per skip rules; the contents would be "
                        << (shouldSkip == SkipElementOption::SkipWithContents
                                ? "skipped"
                                : "preserved"));

                if (shouldSkip == SkipElementOption::SkipWithContents) {
                    ++skippedElementNestingCounter;
                }
                else if (
                    shouldSkip == SkipElementOption::SkipButPreserveContents)
                {
                    ++skippedElementWithPreservedContentsNestingCounter;
                }

                continue;
            }

            if ((lastElementName == QStringLiteral("map")) ||
                (lastElementName == QStringLiteral("area")) ||
                (lastElementName == QStringLiteral("bdo")) ||
                (lastElementName == QStringLiteral("caption")) ||
                (lastElementName == QStringLiteral("col")) ||
                (lastElementName == QStringLiteral("colgroup")))
            {
                QNTRACE("enml", "Skipping element " << lastElementName);
                ++skippedElementNestingCounter;
                continue;
            }

            if (lastElementName == QStringLiteral("link")) {
                lastElementAttributes = reader.attributes();

                QStringRef relAttrRef =
                    lastElementAttributes.value(QStringLiteral("rel"));

                if (!relAttrRef.isEmpty()) {
                    QNTRACE(
                        "enml::Converter",
                        "Skipping CSS style element " << lastElementName);
                    ++skippedElementNestingCounter;
                    continue;
                }
            }

            if (lastElementName == QStringLiteral("abbr")) {
                lastElementName = QStringLiteral("div");
                QNTRACE("enml::Converter", "Replaced abbr with div");
            }
            else if (lastElementName == QStringLiteral("acronym")) {
                lastElementName = QStringLiteral("u");
                QNTRACE("enml::Converter", "Replaced acronym with u");
            }
            else if (lastElementName == QStringLiteral("del")) {
                lastElementName = QStringLiteral("s");
                QNTRACE("enml::Converter", "Replaced del with s");
            }
            else if (lastElementName == QStringLiteral("ins")) {
                lastElementName = QStringLiteral("u");
                QNTRACE("enml::Converter", "Replaced ins with u");
            }
            else if (lastElementName == QStringLiteral("q")) {
                lastElementName = QStringLiteral("blockquote");
                QNTRACE("enml::Converter", "Replaced q with blockquote");
            }
            else if (lastElementName == QStringLiteral("strike")) {
                lastElementName = QStringLiteral("s");
                QNTRACE("enml::Converter", "Replaced strike with s");
            }
            else if (lastElementName == QStringLiteral("xmp")) {
                lastElementName = QStringLiteral("tt");
                QNTRACE("enml::Converter", "Replaced xmp with tt");
            }
            writer.writeStartElement(lastElementName);

            if ((lastElementName == QStringLiteral("div")) ||
                (lastElementName == QStringLiteral("p")) ||
                (lastElementName == QStringLiteral("dl")) ||
                (lastElementName == QStringLiteral("dt")) ||
                (lastElementName == QStringLiteral("h1")) ||
                (lastElementName == QStringLiteral("h2")) ||
                (lastElementName == QStringLiteral("h3")) ||
                (lastElementName == QStringLiteral("h4")) ||
                (lastElementName == QStringLiteral("h5")) ||
                (lastElementName == QStringLiteral("h6")))
            {
                QXmlStreamAttributes filteredAttributes;

                QStringRef alignAttrRef =
                    lastElementAttributes.value(QStringLiteral("align"));

                if (!alignAttrRef.isEmpty()) {
                    QString alignAttr = alignAttrRef.toString();
                    if ((alignAttr == QStringLiteral("left")) ||
                        (alignAttr == QStringLiteral("right")) ||
                        (alignAttr == QStringLiteral("center")) ||
                        (alignAttr == QStringLiteral("justify")))
                    {
                        filteredAttributes.append(
                            QStringLiteral("align"), alignAttr);
                    }
                }

                QStringRef dirAttrRef =
                    lastElementAttributes.value(QStringLiteral("dir"));

                if (!dirAttrRef.isEmpty()) {
                    QString dirAttr = dirAttrRef.toString();
                    if ((dirAttr == QStringLiteral("ltr")) ||
                        (dirAttr == QStringLiteral("rtl")))
                    {
                        filteredAttributes.append(
                            QStringLiteral("dir"), dirAttr);
                    }
                }

                if (!filteredAttributes.isEmpty()) {
                    writer.writeAttributes(filteredAttributes);
                }
            }
            else if (
                (lastElementName == QStringLiteral("ol")) ||
                (lastElementName == QStringLiteral("ul")))
            {
                QStringRef typeAttrRef =
                    lastElementAttributes.value(QStringLiteral("type"));

                if (!typeAttrRef.isEmpty()) {
                    QString typeAttr = typeAttrRef.toString();
                    if ((typeAttr == QStringLiteral("1")) ||
                        (typeAttr == QStringLiteral("a")) ||
                        (typeAttr == QStringLiteral("A")) ||
                        (typeAttr == QStringLiteral("square")) ||
                        (typeAttr == QStringLiteral("disc")) ||
                        (typeAttr == QStringLiteral("circle")))
                    {
                        writer.writeAttribute(QStringLiteral("type"), typeAttr);
                    }
                }
            }
            else if (
                (lastElementName == QStringLiteral("td")) ||
                (lastElementName == QStringLiteral("th")))
            {
                QXmlStreamAttributes filteredAttributes;

                if (lastElementAttributes.hasAttribute(QStringLiteral("width")))
                {
                    QString widthAttr =
                        lastElementAttributes.value(QStringLiteral("width"))
                            .toString();

                    if (widthAttr.isEmpty() ||
                        (widthAttr == QStringLiteral("absolute")) ||
                        (widthAttr == QStringLiteral("relative")))
                    {
                        filteredAttributes.append(
                            QStringLiteral("width"), widthAttr);
                    }
                }

                QStringRef bgcolorAttrRef =
                    lastElementAttributes.value(QStringLiteral("bgcolor"));

                if (!bgcolorAttrRef.isEmpty()) {
                    filteredAttributes.append(
                        QStringLiteral("bgcolor"), bgcolorAttrRef.toString());
                }

                QStringRef colspanAttrRef =
                    lastElementAttributes.value(QStringLiteral("colspan"));

                if (!colspanAttrRef.isEmpty()) {
                    filteredAttributes.append(
                        QStringLiteral("colspan"), colspanAttrRef.toString());
                }

                QStringRef rowspanAttrRef =
                    lastElementAttributes.value(QStringLiteral("rowspan"));

                if (!rowspanAttrRef.isEmpty()) {
                    filteredAttributes.append(
                        QStringLiteral("rowspan"), rowspanAttrRef.toString());
                }

                QStringRef alignAttrRef =
                    lastElementAttributes.value(QStringLiteral("align"));

                if (!alignAttrRef.isEmpty()) {
                    QString alignAttr = alignAttrRef.toString();
                    if ((alignAttr == QStringLiteral("left")) ||
                        (alignAttr == QStringLiteral("right")) ||
                        (alignAttr == QStringLiteral("center")) ||
                        (alignAttr == QStringLiteral("justify")))
                    {
                        filteredAttributes.append(
                            QStringLiteral("align"), alignAttr);
                    }
                }

                QStringRef valignAttrRef =
                    lastElementAttributes.value(QStringLiteral("valign"));

                if (!valignAttrRef.isEmpty()) {
                    QString valignAttr = valignAttrRef.toString();
                    if ((valignAttr == QStringLiteral("top")) ||
                        (valignAttr == QStringLiteral("middle")) ||
                        (valignAttr == QStringLiteral("bottom")))
                    {
                        filteredAttributes.append(
                            QStringLiteral("valign"), valignAttr);
                    }
                }

                if (!filteredAttributes.isEmpty()) {
                    writer.writeAttributes(filteredAttributes);
                }
            }
            else if (lastElementName == QStringLiteral("img")) {
                QStringRef srcAttrRef =
                    lastElementAttributes.value(QStringLiteral("src"));

                if (Q_UNLIKELY(srcAttrRef.isEmpty())) {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Found img tag without src or with empty src "
                        "attribute")};
                    return Result<void, ErrorString>{
                        std::move(errorDescription)};
                }

                bool isGenericResourceImage = false;
                bool isEnCryptTag = false;

                QString enTag =
                    lastElementAttributes.value(QStringLiteral("en-tag"))
                        .toString();

                if (enTag == QStringLiteral("en-media")) {
                    QString typeAttr =
                        lastElementAttributes.value(QStringLiteral("type"))
                            .toString();

                    if (!typeAttr.isEmpty() &&
                        !typeAttr.startsWith(QStringLiteral("image/")))
                    {
                        isGenericResourceImage = true;
                    }
                }
                else if (enTag == QStringLiteral("en-crypt")) {
                    isEnCryptTag = true;
                }

                QImage img;

                bool shouldOutlineImg =
                    (isGenericResourceImage || isEnCryptTag);

                bool shouldAddImgAsResource = false;

                QString srcAttr = srcAttrRef.toString();

                QVariant existingDocImgData =
                    doc.resource(QTextDocument::ImageResource, QUrl(srcAttr));

                if (existingDocImgData.isNull() ||
                    !existingDocImgData.isValid())
                {
                    if (srcAttr.startsWith(QStringLiteral("qrc:/"))) {
                        QString srcAttrShortened = srcAttr;
                        srcAttrShortened.remove(0, 3);
                        img = QImage{srcAttrShortened, "PNG"};
                    }
                    else {
                        const QFileInfo imgFileInfo{srcAttr};
                        if (!imgFileInfo.exists()) {
                            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                                "enml::Converter",
                                "Couldn't find file corresponding to src "
                                "attribute of img tag")};
                            errorDescription.details() = srcAttr;
                            return Result<void, ErrorString>{
                                std::move(errorDescription)};
                        }

                        img = QImage{srcAttr, "PNG"};
                    }

                    shouldAddImgAsResource = true;
                }
                else {
                    QNDEBUG(
                        "enml::Converter",
                        "img tag with src = "
                            << srcAttr
                            << " already has some data associated with "
                            << "the document");
                    img = existingDocImgData.value<QImage>();
                }

                if (shouldOutlineImg) {
                    /** If the method is run by a GUI application *and* in a GUI
                     * (main) thread, we should add the outline to the image
                     */
                    auto * pApp = qobject_cast<QApplication *>(
                        QCoreApplication::instance());

                    if (pApp) {
                        auto * pCurrentThread = QThread::currentThread();
                        if (pApp->thread() == pCurrentThread) {
                            auto pixmap = QPixmap::fromImage(img);

                            QPainter painter{&pixmap};
                            painter.setRenderHints(
                                QPainter::Antialiasing,
                                /* on = */ true);

                            QPen pen;
                            pen.setWidth(2);
                            pen.setColor(Qt::lightGray);
                            painter.setPen(pen);
                            painter.drawRoundedRect(pixmap.rect(), 4, 4);
                            img = pixmap.toImage();
                        }
                        else {
                            QNTRACE(
                                "enml::Converter",
                                "Won't add the outline to the generic resource "
                                "image: the method is not run inside the main "
                                "thread");
                        }
                    }
                    else {
                        QNTRACE(
                            "enml::Converter",
                            "Won't add the outline to the generic resource "
                            "image: not running a QApplication");
                    }
                }

                if (shouldOutlineImg || shouldAddImgAsResource) {
                    doc.addResource(
                        QTextDocument::ImageResource, QUrl(srcAttr), img);
                }

                QXmlStreamAttributes filteredAttributes;
                filteredAttributes.append(QStringLiteral("src"), srcAttr);
                writer.writeAttributes(filteredAttributes);
            }

            ++writeElementCounter;

            QNTRACE(
                "enml::Converter", "Wrote element: name = " << lastElementName);
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            if (skippedElementNestingCounter) {
                continue;
            }

            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("enml::Converter", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("enml::Converter", "Wrote characters: " << text);
            }
        }

        if (reader.isEndElement()) {
            if (skippedElementNestingCounter) {
                --skippedElementNestingCounter;
                continue;
            }

            if (skippedElementWithPreservedContentsNestingCounter) {
                --skippedElementWithPreservedContentsNestingCounter;
                continue;
            }

            if (writeElementCounter <= 0) {
                continue;
            }

            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (reader.hasError()) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Can't convert the note's html to QTextDocument")};

        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml::Converter",
            "Error reading html: " << errorDescription << ", HTML: " << html
                                   << "\nXML: " << res.get());
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    QString simplifiedHtml = QString::fromUtf8(simplifiedHtmlBuffer.buffer());

    doc.setHtml(simplifiedHtml);
    if (Q_UNLIKELY(doc.isEmpty())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Can't convert the note's html to QTextDocument: the "
            "document is empty after setting the simplified HTML")};

        QNWARNING(
            "enml::Converter",
            errorDescription << ", simplified HTML: " << simplifiedHtml);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    return Result<void, ErrorString>{};
}

Result<QString, ErrorString> Converter::convertHtmlToXml(
    const QString & html) const
{
    return utils::convertHtmlToXml(html);
}

Result<QString, ErrorString> Converter::convertHtmlToXhtml(
    const QString & html) const
{
    return utils::convertHtmlToXhtml(html);
}

Result<IHtmlDataPtr, ErrorString> Converter::convertEnmlToHtml(
    const QString & enml, IDecryptedTextCache & decryptedTextCache) const
{
    auto htmlData = std::make_shared<HtmlData>();

    QBuffer htmlBuffer;
    if (Q_UNLIKELY(!htmlBuffer.open(QIODevice::WriteOnly))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Failed to open the buffer to write the html into")};
        errorDescription.details() = htmlBuffer.errorString();
        return Result<IHtmlDataPtr, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamReader reader{enml};

    QXmlStreamWriter writer{&htmlBuffer};
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    int writeElementCounter = 0;

    bool insideEnCryptTag = false;

    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            ++writeElementCounter;
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            if (lastElementName == QStringLiteral("en-note")) {
                QNTRACE("enml", "Replacing en-note with \"body\" tag");
                lastElementName = QStringLiteral("body");
            }
            else if (lastElementName == QStringLiteral("en-media")) {
                auto res = resourceInfoToHtml(lastElementAttributes, writer);
                if (!res.isValid()) {
                    return Result<IHtmlDataPtr, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }
            else if (lastElementName == QStringLiteral("en-crypt")) {
                insideEnCryptTag = true;
                continue;
            }
            else if (lastElementName == QStringLiteral("en-todo")) {
                quint64 enToDoIndex = htmlData->m_enToDoNodes + 1;
                toDoTagsToHtml(reader, enToDoIndex, writer);
                ++htmlData->m_enToDoNodes;
                continue;
            }
            else if (lastElementName == QStringLiteral("a")) {
                quint64 hyperlinkIndex = htmlData->m_hyperlinkNodes + 1;

                lastElementAttributes.append(
                    QStringLiteral("en-hyperlink-id"),
                    QString::number(hyperlinkIndex));

                ++htmlData->m_hyperlinkNodes;
            }

            // NOTE: do not attempt to process en-todo tags here, it would be
            // done below

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);

            QNTRACE(
                "enml",
                "Wrote start element: " << lastElementName
                                        << " and its attributes");
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            if (insideEnCryptTag) {
                quint64 enCryptIndex = htmlData->m_enCryptNodes + 1;
                quint64 enDecryptedIndex = htmlData->m_enDecryptedNodes + 1;
                bool convertedToEnCryptNode = false;

                encryptedTextToHtml(
                    lastElementAttributes, reader.text(), enCryptIndex,
                    enDecryptedIndex, writer, decryptedTextCache,
                    convertedToEnCryptNode);

                if (convertedToEnCryptNode) {
                    ++htmlData->m_enCryptNodes;
                }
                else {
                    ++htmlData->m_enDecryptedNodes;
                }

                insideEnCryptTag = false;
                continue;
            }

            QString data = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(data);
                QNTRACE("enml::Converter", "Wrote CDATA: " << data);
            }
            else {
                writer.writeCharacters(data);
                QNTRACE("enml::Converter", "Wrote characters: " << data);
            }
        }

        if ((writeElementCounter > 0) && reader.isEndElement()) {
            if (lastElementName != QStringLiteral("br")) {
                // NOTE: the following trick seems to prevent the occurrence of
                // self-closing empty XML tags which are sometimes
                // misinterpreted by web engines as unclosed tags
                writer.writeCharacters(QLatin1String(""));
            }

            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (reader.hasError()) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Failed to convert ENML to HTML: error reading ENML")};
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml::Converter", "Error reading ENML: " << errorDescription);
        return Result<IHtmlDataPtr, ErrorString>{std::move(errorDescription)};
    }

    htmlData->m_html = QString::fromUtf8(htmlBuffer.buffer());
    return Result<IHtmlDataPtr, ErrorString>{std::move(htmlData)};
}

Result<QString, ErrorString> Converter::convertEnmlToPlainText(
    const QString & enml) const
{
    QNTRACE("enml::Converter", "Converter::noteContentToPlainText: " << enml);

    QString plainText;
    QTextStream strm{&plainText};

    QXmlStreamReader reader{enml};

    bool skipIteration = false;
    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            const QStringRef element = reader.name();
            if ((element == QStringLiteral("en-media")) ||
                (element == QStringLiteral("en-crypt")))
            {
                skipIteration = true;
            }

            continue;
        }

        if (reader.isEndElement()) {
            const QStringRef element = reader.name();
            if ((element == QStringLiteral("en-media")) ||
                (element == QStringLiteral("en-crypt")))
            {
                skipIteration = false;
            }

            continue;
        }

        if (reader.isCharacters() && !skipIteration) {
            strm << reader.text();
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Failed to convert the note content to plain text")};
        errorDescription.details() = reader.errorString();
        errorDescription.details() += QStringLiteral(", error code ");
        errorDescription.details() += QString::number(reader.error());
        QNWARNING("enml::Converter", errorDescription);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    return Result<QString, ErrorString>{std::move(plainText)};
}

Result<QStringList, ErrorString> Converter::convertEnmlToWordsList(
    const QString & enml) const
{
    QString plainText;

    auto res = convertEnmlToPlainText(enml);
    if (Q_UNLIKELY(!res.isValid())) {
        return Result<QStringList, ErrorString>{std::move(res.error())};
    }

    auto wordsList = convertPlainTextToWordsList(res.get());
    return Result<QStringList, ErrorString>{std::move(wordsList)};
}

QStringList Converter::convertPlainTextToWordsList(
    const QString & plainText) const
{
    // Simply remove all non-word characters from plain text
    return plainText.split(
        QRegularExpression{QStringLiteral("([[:punct:]]|[[:space:]])+")},
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        Qt::SkipEmptyParts);
#else
        QString::SkipEmptyParts);
#endif
}

Result<void, ErrorString> Converter::validateEnml(
    const QString & enml) const
{
    QNDEBUG("enml::Converter", "Converter::validateEnml");
    return validateAgainstDtd(enml, QStringLiteral(":/enml2.dtd"));
}

Converter::ProcessElementStatus
    Converter::processElementForHtmlToNoteContentConversion(
        const QList<conversion_rules::ISkipRulePtr> & skipRules,
        ConversionState & state, IDecryptedTextCache & decryptedTextCache,
        QXmlStreamReader & reader, QXmlStreamWriter & writer,
        ErrorString & errorDescription) const
{
    if (state.m_skippedElementNestingCounter) {
        QNTRACE(
            "enml::Converter",
            "Skipping everything inside element skipped together with its "
                << "contents by the rules");
        ++state.m_skippedElementNestingCounter;
        return ProcessElementStatus::ProcessedFully;
    }

    state.m_lastElementName = reader.name().toString();
    if (state.m_lastElementName == QStringLiteral("form")) {
        QNTRACE("enml::Converter", "Skipping <form> tag");
        return ProcessElementStatus::ProcessedFully;
    }

    if (state.m_lastElementName == QStringLiteral("html")) {
        QNTRACE("enml::Converter", "Skipping <html> tag");
        return ProcessElementStatus::ProcessedFully;
    }

    if (state.m_lastElementName == QStringLiteral("title")) {
        QNTRACE("enml::Converter", "Skipping <title> tag");
        return ProcessElementStatus::ProcessedFully;
    }

    if (state.m_lastElementName == QStringLiteral("body")) {
        state.m_lastElementName = QStringLiteral("en-note");
        QNTRACE(
            "enml::Converter",
            "Found \"body\" HTML tag, will replace it "
                << "with \"en-note\" tag for written ENML");
    }

    auto tagIt = m_forbiddenXhtmlTags.constFind(state.m_lastElementName);
    if ((tagIt != m_forbiddenXhtmlTags.constEnd()) &&
        (state.m_lastElementName != QStringLiteral("object")))
    {
        QNTRACE(
            "enml::Converter",
            "Skipping forbidden XHTML tag: " << state.m_lastElementName);
        return ProcessElementStatus::ProcessedFully;
    }

    tagIt = m_allowedXhtmlTags.constFind(state.m_lastElementName);
    if (tagIt == m_allowedXhtmlTags.constEnd()) {
        tagIt = m_evernoteSpecificXhtmlTags.constFind(state.m_lastElementName);
        if (tagIt == m_evernoteSpecificXhtmlTags.constEnd()) {
            QNTRACE(
                "enml::Converter",
                "Haven't found tag "
                    << state.m_lastElementName
                    << " in the list of allowed XHTML tags or within "
                    << "Evernote-specific tags, skipping it");
            return ProcessElementStatus::ProcessedFully;
        }
    }

    state.m_lastElementAttributes = reader.attributes();

    const auto shouldSkip = skipElementOption(
        state.m_lastElementName, state.m_lastElementAttributes, skipRules);

    if (shouldSkip != SkipElementOption::DontSkip) {
        QNTRACE(
            "enml::Converter",
            "Skipping element "
                << state.m_lastElementName
                << " per skip rules; the contents would be "
                << (shouldSkip == SkipElementOption::SkipWithContents
                        ? "skipped"
                        : "preserved"));

        if (shouldSkip == SkipElementOption::SkipWithContents) {
            ++state.m_skippedElementNestingCounter;
        }
        else if (shouldSkip == SkipElementOption::SkipButPreserveContents) {
            ++state.m_skippedElementWithPreservedContentsNestingCounter;
        }

        return ProcessElementStatus::ProcessedFully;
    }

    if (((state.m_lastElementName == QStringLiteral("img")) ||
         (state.m_lastElementName == QStringLiteral("object")) ||
         (state.m_lastElementName == QStringLiteral("div"))) &&
        state.m_lastElementAttributes.hasAttribute(QStringLiteral("en-tag")))
    {
        const QStringRef enTag =
            state.m_lastElementAttributes.value(QStringLiteral("en-tag"));

        if (enTag == QStringLiteral("en-decrypted")) {
            QNTRACE(
                "enml::Converter",
                "Found decrypted text area, need to "
                    << "convert it back to en-crypt form");

            auto res = decryptedTextToEnml(reader, decryptedTextCache, writer);
            if (!res.isValid()) {
                errorDescription = std::move(res.error());
                return ProcessElementStatus::Error;
            }

            return ProcessElementStatus::ProcessedFully;
        }

        if (enTag == QStringLiteral("en-todo")) {
            if (!state.m_lastElementAttributes.hasAttribute(
                    QStringLiteral("src")))
            {
                QNWARNING(
                    "enml::Converter",
                    "Found en-todo tag without src attribute");
                return ProcessElementStatus::ProcessedFully;
            }

            const QStringRef srcValue =
                state.m_lastElementAttributes.value(QStringLiteral("src"));

            if (srcValue.contains(
                    QStringLiteral("qrc:/checkbox_icons/checkbox_no.png")))
            {
                writer.writeStartElement(QStringLiteral("en-todo"));
                ++state.m_writeElementCounter;
                return ProcessElementStatus::ProcessedFully;
            }

            if (srcValue.contains(
                    QStringLiteral("qrc:/checkbox_icons/checkbox_yes.png")))
            {
                writer.writeStartElement(QStringLiteral("en-todo"));

                writer.writeAttribute(
                    QStringLiteral("checked"), QStringLiteral("true"));

                ++state.m_writeElementCounter;
                return ProcessElementStatus::ProcessedFully;
            }
        }
        else if (enTag == QStringLiteral("en-crypt")) {
            const QXmlStreamAttributes attributes = reader.attributes();
            QXmlStreamAttributes enCryptAttributes;

            if (attributes.hasAttribute(QStringLiteral("cipher"))) {
                enCryptAttributes.append(
                    QStringLiteral("cipher"),
                    attributes.value(QStringLiteral("cipher")).toString());
            }

            if (attributes.hasAttribute(QStringLiteral("length"))) {
                enCryptAttributes.append(
                    QStringLiteral("length"),
                    attributes.value(QStringLiteral("length")).toString());
            }

            if (!attributes.hasAttribute(QStringLiteral("encrypted_text"))) {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Found en-crypt tag without encrypted_text attribute"));
                QNWARNING("enml::Converter", errorDescription);
                return ProcessElementStatus::Error;
            }

            if (attributes.hasAttribute(QStringLiteral("hint"))) {
                enCryptAttributes.append(
                    QStringLiteral("hint"),
                    attributes.value(QStringLiteral("hint")).toString());
            }

            writer.writeStartElement(QStringLiteral("en-crypt"));
            writer.writeAttributes(enCryptAttributes);

            writer.writeCharacters(
                attributes.value(QStringLiteral("encrypted_text")).toString());

            ++state.m_writeElementCounter;
            QNTRACE("enml::Converter", "Started writing en-crypt tag");
            state.m_insideEnCryptElement = true;
            return ProcessElementStatus::ProcessedFully;
        }
        else if (enTag == QStringLiteral("en-media")) {
            const bool isImage =
                (state.m_lastElementName == QStringLiteral("img"));

            state.m_lastElementName = QStringLiteral("en-media");
            writer.writeStartElement(state.m_lastElementName);
            ++state.m_writeElementCounter;
            state.m_enMediaAttributes.clear();
            state.m_insideEnMediaElement = true;

            const int numAttributes = state.m_lastElementAttributes.size();
            for (int i = 0; i < numAttributes; ++i) {
                const auto & attribute = state.m_lastElementAttributes[i];

                const QString attributeQualifiedName =
                    attribute.qualifiedName().toString();

                const QString attributeValue = attribute.value().toString();

                if (!isImage) {
                    if (attributeQualifiedName ==
                        QStringLiteral("resource-mime-type"))
                    {
                        state.m_enMediaAttributes.append(
                            QStringLiteral("type"), attributeValue);
                    }
                    else {
                        const bool contains =
                            m_allowedEnMediaAttributes.contains(
                                attributeQualifiedName);

                        if (contains &&
                            (attributeQualifiedName != QStringLiteral("type")))
                        {
                            state.m_enMediaAttributes.append(
                                attributeQualifiedName, attributeValue);
                        }
                    }
                }
                else if (m_allowedEnMediaAttributes.contains(
                             attributeQualifiedName))
                {
                    // img
                    state.m_enMediaAttributes.append(
                        attributeQualifiedName, attributeValue);
                }
            }

            writer.writeAttributes(state.m_enMediaAttributes);
            state.m_enMediaAttributes.clear();
            QNTRACE(
                "enml::Converter",
                "Wrote en-media element from img element in HTML");

            return ProcessElementStatus::ProcessedFully;
        }
    }

    // Erasing forbidden attributes
    for (auto it = state.m_lastElementAttributes.begin(); // NOLINT
         it != state.m_lastElementAttributes.end();)
    {
        const auto & attribute = *it;
        const QString attributeName = attribute.name().toString();
        if (isForbiddenXhtmlAttribute(attributeName)) {
            QNTRACE(
                "enml::Converter",
                "Erasing forbidden attribute " << attributeName);
            it = state.m_lastElementAttributes.erase(it);
            continue;
        }

        if ((state.m_lastElementName == QStringLiteral("a")) &&
            (attributeName == QStringLiteral("en-hyperlink-id")))
        {
            QNTRACE("enml", "Erasing custom attribute en-hyperlink-id");
            it = state.m_lastElementAttributes.erase(it);
            continue;
        }

        ++it;
    }

    writer.writeStartElement(state.m_lastElementName);
    writer.writeAttributes(state.m_lastElementAttributes);
    ++state.m_writeElementCounter;
    QNTRACE(
        "enml::Converter",
        "Wrote element: name = " << state.m_lastElementName
                                 << " and its attributes");

    return ProcessElementStatus::ProcessedPartially;
}

bool Converter::isForbiddenXhtmlAttribute(
    const QString & attributeName) const noexcept
{
    const auto it = m_forbiddenXhtmlAttributes.constFind(attributeName);
    if (it != m_forbiddenXhtmlAttributes.constEnd()) {
        return true;
    }

    return attributeName.startsWith(QStringLiteral("on"));
}

Result<void, ErrorString> Converter::validateAgainstDtd(
    const QString & input, const QString & dtdFilePath) const
{
    QNDEBUG(
        "enml::Converter",
        "Converter::validateAgainstDtd: dtd file " << dtdFilePath);

    const QByteArray inputBuffer = input.toUtf8();

    const std::unique_ptr<xmlDoc, void (*)(xmlDocPtr)> doc{
        xmlParseMemory(inputBuffer.constData(), inputBuffer.size()),
        xmlFreeDoc};

    if (Q_UNLIKELY(!doc)) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Could not validate document, can't parse the input into xml doc")};
        QNWARNING("enml::Converter", errorDescription << ": input = " << input);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    QFile dtdFile{dtdFilePath};
    if (Q_UNLIKELY(!dtdFile.open(QIODevice::ReadOnly))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Could not validate document, can't open the resource file with "
            "DTD")};
        QNWARNING(
            "enml::Converter",
            errorDescription << ": input = " << input
                             << ", DTD file path = " << dtdFilePath);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    const QByteArray dtdRawData = dtdFile.readAll();

    std::unique_ptr<xmlParserInputBuffer, void (*)(xmlParserInputBufferPtr)>
        buf{xmlParserInputBufferCreateMem(
                dtdRawData.constData(), dtdRawData.size(),
                XML_CHAR_ENCODING_NONE),
            xmlFreeParserInputBuffer};

    if (!buf) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Could not validate document, can't allocate the input buffer for "
            "dtd validation")};
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    // NOTE: xmlIOParseDTD should have "consumed" the input buffer so one
    // should not attempt to free it manually
    const std::unique_ptr<xmlDtd, void (*)(xmlDtdPtr)> dtd{
        xmlIOParseDTD(nullptr, buf.release(), XML_CHAR_ENCODING_NONE),
        xmlFreeDtd};

    if (!dtd) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Could not validate document, failed to parse DTD")};
        QNWARNING("enml", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    const std::unique_ptr<xmlValidCtxt, void (*)(xmlValidCtxtPtr)> context{
        xmlNewValidCtxt(), xmlFreeValidCtxt};

    if (!context) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Could not validate document, can't allocate parser context")};
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    QString errorString;
    context->userData = &errorString;
    context->error = (xmlValidityErrorFunc)xmlValidationErrorFunc;

    if (!xmlValidateDtd(context.get(), doc.get(), dtd.get())) {
        ErrorString errorDescription{
            QT_TRANSLATE_NOOP("enml::Converter", "Document is invalid")};
        if (!errorString.isEmpty()) {
            errorDescription.details() = QStringLiteral(": ");
            errorDescription.details() += errorString;
        }

        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    }

    return Result<void, ErrorString>{};
}

} // namespace quentier::enml
