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
#include <quentier/utility/DateTime.h>
#include <quentier/utility/Unreachable.h>

#include <qevercloud/utility/ToRange.h>

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
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

#include <string_view>
#include <utility>

namespace quentier::enml {

using namespace std::string_view_literals;

namespace {

constexpr int gEnexMaxResourceDataSize = 26214400; // 25 Mb in bytes
constexpr auto gEnexDateTimeFormat = "yyyyMMdd'T'HHmmss'Z'"sv;
constexpr auto gEnexDateTimeFormatStrftime = "%Y%m%dT%H%M%SZ"sv;

enum class SkipElementOption
{
    SkipWithContents = 0x0,
    SkipButPreserveContents = 0x1,
    DontSkip = 0x2
};

Q_DECLARE_FLAGS(SkipElementOptions, SkipElementOption);

////////////////////////////////////////////////////////////////////////////////

QDebug & operator<<(QDebug & dbg, const QXmlStreamAttributes & obj)
{
    const int numAttributes = obj.size();

    dbg << "QXmlStreamAttributes(" << numAttributes << "): {\n";

    for (int i = 0; i < numAttributes; ++i) {
        const auto & attribute = obj[i];
        dbg << "  [" << i << "]: name = " << attribute.name().toString()
            << ", value = " << attribute.value().toString() << "\n";
    }

    dbg << "}\n";
    return dbg;
}

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
    QNDEBUG("enml::Converter", "ENMLConverterPrivate::resourceInfoToHtml");

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

[[nodiscard]] qevercloud::Timestamp timestampFromDateTime(
    const QDateTime & dateTime) noexcept
{
    if (!dateTime.isValid()) {
        return 0;
    }

    auto timestamp = dateTime.toMSecsSinceEpoch();
    if (Q_UNLIKELY(timestamp < 0)) {
        return 0;
    }

    return timestamp;
}

template <class T, class Attrs>
[[nodiscard]] Attrs & ensureAttributes(T & value)
{
    if (!value.attributes()) {
        value.setAttributes(Attrs{});
    }

    return *value.mutableAttributes();
}

[[nodiscard]] qevercloud::NoteAttributes & ensureNoteAttributes(
    qevercloud::Note & note)
{
    return ensureAttributes<qevercloud::Note, qevercloud::NoteAttributes>(note);
}

[[nodiscard]] qevercloud::ResourceAttributes & ensureResourceAttributes(
    qevercloud::Resource & resource)
{
    return ensureAttributes<
        qevercloud::Resource, qevercloud::ResourceAttributes>(resource);
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
                QNTRACE(
                    "enml::Converter", "Skipping element " << lastElementName);
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
                QNTRACE(
                    "enml::Converter", "Replacing en-note with \"body\" tag");
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
                "enml::Converter",
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

Result<void, ErrorString> Converter::validateEnml(const QString & enml) const
{
    QNDEBUG("enml::Converter", "Converter::validateEnml");
    return validateAgainstDtd(enml, QStringLiteral(":/enml2.dtd"));
}

Result<QString, ErrorString> Converter::validateAndFixupEnml(
    const QString & enml) const
{
    QNDEBUG("enml::Converter", "Converter::validateAndFixupEnml");

    auto res = validateEnml(enml);
    if (res.isValid()) {
        return Result<QString, ErrorString>{enml};
    }

    // If we got here, the ENML it not valid. Most probably it is due to some
    // attributes on some elements that Evernote doesn't quite like. Will try to
    // parse the names of such attributes and corresponding elements from
    // the error description and remove them during one more pass.

    // FIXME: a better approach would be to consult the DTD file which knows
    // exactly which attributes are allowed on which elements but it's kinda
    // troublesome - libxml2 is probably capable of this but it's tedious to
    // learn the exact way to do it. Hence, this simplified solution involving
    // parsing the error description.

    // I've tried to get a regex do this for me but it turned pretty bad pretty
    // quickly so here's the dirty yet working solution
    QString error = res.error().details();
    QHash<QString, QStringList> elementToForbiddenAttributes;

    int lastIndex = 0;

    QString attributePrefix = QStringLiteral("No declaration for attribute ");
    int attributePrefixSize = attributePrefix.size();

    QString elementPrefix = QStringLiteral("element ");
    int elementPrefixSize = elementPrefix.size();

    while (true) {
        int attributeNameIndex = error.indexOf(attributePrefix, lastIndex);
        if (attributeNameIndex < 0) {
            break;
        }

        attributeNameIndex += attributePrefixSize;

        int attributeNameEndIndex =
            error.indexOf(QStringLiteral(" "), attributeNameIndex);
        if (attributeNameEndIndex < 0) {
            break;
        }

        int elementNameIndex =
            error.indexOf(elementPrefix, attributeNameEndIndex);

        if (elementNameIndex < 0) {
            break;
        }

        elementNameIndex += elementPrefixSize;

        int elementNameIndexEnd =
            error.indexOf(QStringLiteral("\n"), elementNameIndex);

        if (elementNameIndexEnd < 0) {
            break;
        }

        lastIndex = elementNameIndexEnd;

        QString elementName = error.mid(
            elementNameIndex, (elementNameIndexEnd - elementNameIndex));

        QString attributeName = error.mid(
            attributeNameIndex, (attributeNameEndIndex - attributeNameIndex));

        QStringList & attributesForElement =
            elementToForbiddenAttributes[elementName];

        if (!attributesForElement.contains(attributeName)) {
            attributesForElement << attributeName;
        }
    }

    if (QuentierIsLogLevelActive(LogLevel::Trace)) {
        QNTRACE("enml::Converter", "Parsed forbidden attributes per element: ");

        for (const auto it:
             qevercloud::toRange(std::as_const(elementToForbiddenAttributes)))
        {
            QNTRACE("enml::Converter", "[" << it.key() << "]: " << it.value());
        }
    }

    QBuffer fixedUpEnmlBuffer;
    if (Q_UNLIKELY(!fixedUpEnmlBuffer.open(QIODevice::WriteOnly))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Failed to open the buffer to write the fixed up note content "
            "into")};
        errorDescription.details() = fixedUpEnmlBuffer.errorString();
        QNWARNING("enml::Converter", errorDescription);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamWriter writer(&fixedUpEnmlBuffer);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();
    writer.writeDTD(
        QStringLiteral("<!DOCTYPE en-note SYSTEM "
                       "\"http://xml.evernote.com/pub/enml2.dtd\">"));

    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    QXmlStreamReader reader(enml);
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
            lastElementName = reader.name().toString();
            lastElementAttributes = reader.attributes();

            auto it = elementToForbiddenAttributes.find(lastElementName);
            if (it == elementToForbiddenAttributes.end()) {
                QNTRACE(
                    "enml::Converter",
                    "No forbidden attributes for element " << lastElementName);

                writer.writeStartElement(lastElementName);
                writer.writeAttributes(lastElementAttributes);
                continue;
            }

            const QStringList & forbiddenAttributes = it.value();

            // Erasing forbidden attributes
            for (auto ait = lastElementAttributes.begin(); // NOLINT
                 ait != lastElementAttributes.end();)
            {
                const auto & attribute = *ait;
                const QString attributeName = attribute.name().toString();
                if (forbiddenAttributes.contains(attributeName)) {
                    QNTRACE(
                        "enml::Converter",
                        "Erasing forbidden attribute " << attributeName);

                    ait = lastElementAttributes.erase(ait);
                    continue;
                }

                ++ait;
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);

            QNTRACE(
                "enml::Converter",
                "Wrote element: name = " << lastElementName
                                         << " and its attributes");
        }

        if (reader.isCharacters()) {
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
            writer.writeEndElement();
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Could not fixup ENML as it is not a valid XML document")};
        errorDescription.details() = reader.errorString();
        QNWARNING("enml::Converter", errorDescription);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    auto fixedUpEnml = QString::fromUtf8(fixedUpEnmlBuffer.buffer());
    QNTRACE("enml::Converter", "ENML after fixing up: " << fixedUpEnml);

    res = validateEnml(fixedUpEnml);
    if (!res.isValid()) {
        return Result<QString, ErrorString>{std::move(res.error())};
    }

    return Result<QString, ErrorString>{std::move(fixedUpEnml)};
}

Result<QString, ErrorString> Converter::exportNotesToEnex(
    const QList<qevercloud::Note> & notes,
    const QHash<QString, QString> & tagNamesByTagLocalIds,
    EnexExportTags exportTagsOption, const QString & version) const
{
    QNDEBUG(
        "enml::Converter",
        "Converter::exportNotesToEnex: num notes = "
            << notes.size() << ", num tag names by tag local ids = "
            << tagNamesByTagLocalIds.size() << ", export tags option = "
            << ((exportTagsOption == EnexExportTags::Yes) ? "Yes" : "No")
            << ", version = " << version);

    if (notes.isEmpty()) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter", "Can't export note(s) to ENEX: no notes")};
        QNWARNING("enml::Converter", errorDescription);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QString enex;
    bool foundNoteEligibleForExport = false;
    for (const auto & note: std::as_const(notes)) {
        if (!note.title() && !note.content() &&
            (!note.resources() || note.resources()->isEmpty()) &&
            note.tagLocalIds().isEmpty())
        {
            continue;
        }

        foundNoteEligibleForExport = true;
        break;
    }

    if (!foundNoteEligibleForExport) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Can't export note(s) to ENEX: no notes eligible for export")};
        QNWARNING("enml::Converter", errorDescription);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QBuffer enexBuffer;
    if (Q_UNLIKELY(!enexBuffer.open(QIODevice::WriteOnly))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Can't export note(s) to ENEX: can't open the buffer to write the "
            "ENEX into")};
        errorDescription.details() = enexBuffer.errorString();
        QNWARNING("enml::Converter", errorDescription);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamWriter writer(&enexBuffer);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();
    writer.writeDTD(QStringLiteral(
        "<!DOCTYPE en-export SYSTEM "
        "\"http://xml.evernote.com/pub/evernote-export3.dtd\">"));

    writer.writeStartElement(QStringLiteral("en-export"));

    QXmlStreamAttributes enExportAttributes;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    DateTimePrintOptions dateTimePrintOptions;
#else
    DateTimePrintOptions dateTimePrintOptions{0}; // NOLINT
#endif

    const qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();

    enExportAttributes.append(
        QStringLiteral("export-date"),
        printableDateTimeFromTimestamp(
            currentTimestamp, dateTimePrintOptions,
            gEnexDateTimeFormatStrftime.data()));

    enExportAttributes.append(
        QStringLiteral("application"), QCoreApplication::applicationName());

    enExportAttributes.append(QStringLiteral("version"), version);

    writer.writeAttributes(enExportAttributes);

    for (const auto & note: std::as_const(notes)) {
        if (!note.title() && !note.content() &&
            (!note.resources() || note.resources()->isEmpty()) &&
            ((exportTagsOption != EnexExportTags::Yes) ||
             note.tagLocalIds().isEmpty()))
        {
            QNINFO(
                "enml::Converter",
                "Skipping note without title, content, resources or tags in "
                "export to ENML");
            continue;
        }

        writer.writeStartElement(QStringLiteral("note"));

        // NOTE: per DTD, title and content tags have to exist while created
        // and updated don't have to
        writer.writeStartElement(QStringLiteral("title"));
        if (note.title()) {
            writer.writeCharacters(*note.title());
        }
        writer.writeEndElement(); // title

        writer.writeStartElement(QStringLiteral("content"));
        if (note.content()) {
            writer.writeCDATA(*note.content());
        }
        writer.writeEndElement(); // content

        if (note.created()) {
            writer.writeStartElement(QStringLiteral("created"));
            writer.writeCharacters(printableDateTimeFromTimestamp(
                *note.created(), dateTimePrintOptions,
                gEnexDateTimeFormatStrftime.data()));
            writer.writeEndElement(); // created
        }

        if (note.updated()) {
            writer.writeStartElement(QStringLiteral("updated"));
            writer.writeCharacters(printableDateTimeFromTimestamp(
                *note.updated(), dateTimePrintOptions,
                gEnexDateTimeFormatStrftime.data()));
            writer.writeEndElement(); // updated
        }

        if (exportTagsOption == EnexExportTags::Yes) {
            const QStringList & tagLocalIds = note.tagLocalIds();
            for (auto tagIt = tagLocalIds.constBegin(),
                      tagEnd = tagLocalIds.constEnd();
                 tagIt != tagEnd; ++tagIt)
            {
                auto tagNameIt = tagNamesByTagLocalIds.find(*tagIt);
                if (Q_UNLIKELY(tagNameIt == tagNamesByTagLocalIds.end())) {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Can't export note(s) to ENEX: one of notes has tag "
                        "local uid for which no tag name was found")};
                    QNWARNING("enml::Converter", errorDescription);
                    return Result<QString, ErrorString>{
                        std::move(errorDescription)};
                }

                const QString & tagName = tagNameIt.value();
                if (Q_UNLIKELY(tagName.isEmpty())) {
                    QNWARNING(
                        "enml::Converter",
                        "Skipping tag with empty name, "
                            << " tag local uid = " << *tagIt
                            << ", note: " << note);
                    continue;
                }

                writer.writeStartElement(QStringLiteral("tag"));
                writer.writeCharacters(tagName);
                writer.writeEndElement();
            }
        }

        if (note.attributes()) {
            const auto & noteAttributes = *note.attributes();

            if (noteAttributes.latitude() || noteAttributes.longitude() ||
                noteAttributes.altitude() || noteAttributes.author() ||
                noteAttributes.source() || noteAttributes.sourceURL() ||
                noteAttributes.sourceApplication() ||
                noteAttributes.reminderOrder() ||
                noteAttributes.reminderTime() ||
                noteAttributes.reminderDoneTime() ||
                noteAttributes.placeName() || noteAttributes.contentClass() ||
                noteAttributes.subjectDate() ||
                noteAttributes.applicationData())
            {
                writer.writeStartElement(QStringLiteral("note-attributes"));

                if (noteAttributes.subjectDate()) {
                    writer.writeStartElement(QStringLiteral("subject-date"));
                    writer.writeCharacters(printableDateTimeFromTimestamp(
                        *noteAttributes.subjectDate(), dateTimePrintOptions,
                        gEnexDateTimeFormatStrftime.data()));
                    writer.writeEndElement();
                }

                if (noteAttributes.latitude()) {
                    writer.writeStartElement(QStringLiteral("latitude"));
                    writer.writeCharacters(
                        QString::number(*noteAttributes.latitude()));
                    writer.writeEndElement();
                }

                if (noteAttributes.longitude()) {
                    writer.writeStartElement(QStringLiteral("longitude"));
                    writer.writeCharacters(
                        QString::number(*noteAttributes.longitude()));
                    writer.writeEndElement();
                }

                if (noteAttributes.altitude()) {
                    writer.writeStartElement(QStringLiteral("altitude"));
                    writer.writeCharacters(
                        QString::number(*noteAttributes.altitude()));
                    writer.writeEndElement();
                }

                if (noteAttributes.author()) {
                    writer.writeStartElement(QStringLiteral("author"));
                    writer.writeCharacters(*noteAttributes.author());
                    writer.writeEndElement();
                }

                if (noteAttributes.source()) {
                    writer.writeStartElement(QStringLiteral("source"));
                    writer.writeCharacters(*noteAttributes.source());
                    writer.writeEndElement();
                }

                if (noteAttributes.sourceURL()) {
                    writer.writeStartElement(QStringLiteral("source-url"));
                    writer.writeCharacters(*noteAttributes.sourceURL());
                    writer.writeEndElement();
                }

                if (noteAttributes.sourceApplication()) {
                    writer.writeStartElement(
                        QStringLiteral("source-application"));

                    writer.writeCharacters(*noteAttributes.sourceApplication());

                    writer.writeEndElement();
                }

                if (noteAttributes.reminderOrder()) {
                    writer.writeStartElement(QStringLiteral("reminder-order"));

                    writer.writeCharacters(
                        QString::number(*noteAttributes.reminderOrder()));

                    writer.writeEndElement();
                }

                if (noteAttributes.reminderTime()) {
                    writer.writeStartElement(QStringLiteral("reminder-time"));
                    writer.writeCharacters(printableDateTimeFromTimestamp(
                        *noteAttributes.reminderTime(), dateTimePrintOptions,
                        gEnexDateTimeFormatStrftime.data()));
                    writer.writeEndElement();
                }

                if (noteAttributes.reminderDoneTime()) {
                    writer.writeStartElement(
                        QStringLiteral("reminder-done-time"));

                    writer.writeCharacters(printableDateTimeFromTimestamp(
                        *noteAttributes.reminderDoneTime(),
                        dateTimePrintOptions,
                        gEnexDateTimeFormatStrftime.data()));

                    writer.writeEndElement();
                }

                if (noteAttributes.placeName()) {
                    writer.writeStartElement(QStringLiteral("place-name"));
                    writer.writeCharacters(*noteAttributes.placeName());
                    writer.writeEndElement();
                }

                if (noteAttributes.contentClass()) {
                    writer.writeStartElement(QStringLiteral("content-class"));
                    writer.writeCharacters(*noteAttributes.contentClass());
                    writer.writeEndElement();
                }

                if (noteAttributes.applicationData()) {
                    const auto & appData = *noteAttributes.applicationData();
                    if (appData.fullMap()) {
                        const auto & fullMap = *appData.fullMap();

                        for (const auto mapIt: qevercloud::toRange(fullMap)) {
                            writer.writeStartElement(
                                QStringLiteral("application-data"));
                            writer.writeAttribute(
                                QStringLiteral("key"), mapIt.key());
                            writer.writeCharacters(mapIt.value());
                            writer.writeEndElement();
                        }
                    }
                }

                writer.writeEndElement(); // note-attributes
            }
        }

        if (note.resources()) {
            auto resources = *note.resources();

            for (const auto & resource: std::as_const(resources)) {
                if (!resource.data() || !resource.data()->body()) {
                    QNINFO(
                        "enml::Converter",
                        "Skipping ENEX export of a resource "
                            << "without data body: " << resource);
                    continue;
                }

                if (!resource.mime()) {
                    QNINFO(
                        "enml::Converter",
                        "Skipping ENEX export of a resource "
                            << "without mime type: " << resource);
                    continue;
                }

                writer.writeStartElement(QStringLiteral("resource"));

                const QByteArray & resourceData = *resource.data()->body();
                if (resourceData.size() > gEnexMaxResourceDataSize) {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Can't export note(s) to ENEX: found resource larger "
                        "than 25 Mb")};
                    QNINFO(
                        "enml::Converter",
                        errorDescription << ", resource: " << resource);
                    return Result<QString, ErrorString>{
                        std::move(errorDescription)};
                }

                writer.writeStartElement(QStringLiteral("data"));

                writer.writeAttribute(
                    QStringLiteral("encoding"), QStringLiteral("base64"));

                writer.writeCharacters(
                    QString::fromLocal8Bit(resourceData.toBase64()));

                writer.writeEndElement(); // data

                writer.writeStartElement(QStringLiteral("mime"));
                writer.writeCharacters(*resource.mime());
                writer.writeEndElement(); // mime

                if (resource.width()) {
                    writer.writeStartElement(QStringLiteral("width"));
                    writer.writeCharacters(QString::number(*resource.width()));
                    writer.writeEndElement(); // width
                }

                if (resource.height()) {
                    writer.writeStartElement(QStringLiteral("height"));
                    writer.writeCharacters(QString::number(*resource.height()));
                    writer.writeEndElement();
                }

                if (resource.recognition() && resource.recognition()->body()) {
                    const auto & recognitionData =
                        *resource.recognition()->body();

                    const auto recoIndexRes =
                        validateRecoIndex(QString::fromUtf8(recognitionData));

                    if (Q_UNLIKELY(!recoIndexRes.isValid())) {
                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Can't export note(s) to ENEX: found invalid "
                            "resource recognition index at one of notes")};
                        const auto & error = recoIndexRes.error();
                        errorDescription.appendBase(error.base());
                        errorDescription.appendBase(error.additionalBases());
                        errorDescription.details() = error.details();
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QString, ErrorString>{
                            std::move(errorDescription)};
                    }

                    writer.writeStartElement(QStringLiteral("recognition"));
                    writer.writeCDATA(QString::fromUtf8(recognitionData));
                    writer.writeEndElement(); // recognition
                }

                if (resource.attributes()) {
                    const auto & resourceAttributes = *resource.attributes();
                    if (resourceAttributes.sourceURL() ||
                        resourceAttributes.timestamp() ||
                        resourceAttributes.latitude() ||
                        resourceAttributes.longitude() ||
                        resourceAttributes.altitude() ||
                        resourceAttributes.cameraMake() ||
                        resourceAttributes.recoType() ||
                        resourceAttributes.fileName() ||
                        resourceAttributes.attachment() ||
                        resourceAttributes.applicationData())
                    {
                        writer.writeStartElement(
                            QStringLiteral("resource-attributes"));

                        if (resourceAttributes.sourceURL()) {
                            writer.writeStartElement(
                                QStringLiteral("source-url"));

                            writer.writeCharacters(
                                *resourceAttributes.sourceURL());

                            writer.writeEndElement(); // source-url
                        }

                        if (resourceAttributes.timestamp()) {
                            writer.writeStartElement(
                                QStringLiteral("timestamp"));

                            writer.writeCharacters(
                                printableDateTimeFromTimestamp(
                                    *resourceAttributes.timestamp(),
                                    dateTimePrintOptions,
                                    gEnexDateTimeFormatStrftime.data()));

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.latitude()) {
                            writer.writeStartElement(
                                QStringLiteral("latitude"));

                            writer.writeCharacters(QString::number(
                                *resourceAttributes.latitude()));

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.longitude()) {
                            writer.writeStartElement(
                                QStringLiteral("longitude"));

                            writer.writeCharacters(QString::number(
                                *resourceAttributes.longitude()));

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.altitude()) {
                            writer.writeStartElement(
                                QStringLiteral("altitude"));

                            writer.writeCharacters(QString::number(
                                *resourceAttributes.altitude()));

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.cameraMake()) {
                            writer.writeStartElement(
                                QStringLiteral("camera-make"));

                            writer.writeCharacters(
                                *resourceAttributes.cameraMake());

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.recoType()) {
                            writer.writeStartElement(
                                QStringLiteral("reco-type"));

                            writer.writeCharacters(
                                *resourceAttributes.recoType());

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.fileName()) {
                            writer.writeStartElement(
                                QStringLiteral("file-name"));

                            writer.writeCharacters(
                                *resourceAttributes.fileName());

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.attachment()) {
                            writer.writeStartElement(
                                QStringLiteral("attachment"));

                            writer.writeCharacters(
                                *resourceAttributes.attachment()
                                    ? QStringLiteral("true")
                                    : QStringLiteral("false"));

                            writer.writeEndElement();
                        }

                        if (resourceAttributes.applicationData()) {
                            const auto & appData =
                                *resourceAttributes.applicationData();

                            if (appData.fullMap()) {
                                const auto & fullMap = *appData.fullMap();

                                for (const auto mapIt:
                                     qevercloud::toRange(fullMap))
                                {
                                    writer.writeStartElement(
                                        QStringLiteral("application-data"));

                                    writer.writeAttribute(
                                        QStringLiteral("key"), mapIt.key());

                                    writer.writeCharacters(mapIt.value());
                                    writer.writeEndElement();
                                }
                            }
                        }

                        writer.writeEndElement(); // resource-attributes
                    }
                }

                if (resource.alternateData() &&
                    resource.alternateData()->body())
                {
                    const auto & resourceAltData =
                        *resource.alternateData()->body();

                    writer.writeStartElement(QStringLiteral("alternate-data"));

                    writer.writeAttribute(
                        QStringLiteral("encoding"), QStringLiteral("base64"));

                    writer.writeCharacters(
                        QString::fromLocal8Bit(resourceAltData.toBase64()));

                    writer.writeEndElement(); // alternate-data
                }

                writer.writeEndElement(); // resource
            }
        }

        writer.writeEndElement(); // note
    }

    writer.writeEndElement(); // en-export
    writer.writeEndDocument();

    enex = QString::fromUtf8(enexBuffer.buffer());

    auto res = validateEnex(enex);
    if (Q_UNLIKELY(!res.isValid())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter", "Cannot export note(s) to ENEX")};
        const auto & error = res.error();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.setDetails(error.details());
        QNWARNING("enml::Converter", errorDescription << ", enex: " << enex);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    return Result<QString, ErrorString>{std::move(enex)};
}

Result<QList<qevercloud::Note>, ErrorString> Converter::importEnex(
    const QString & enex) const
{
    QNDEBUG("enml::Converter", "Converter::importEnex");

    if (Q_UNLIKELY(enex.isEmpty())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter", "Can't import ENEX: the input is empty")};
        QNWARNING("enml::Converter", errorDescription << ", enex: " << enex);
        return Result<QList<qevercloud::Note>, ErrorString>{
            std::move(errorDescription)};
    }

    QList<qevercloud::Note> notes;

    const QString dateTimeFormat = QString::fromUtf8(
        gEnexDateTimeFormat.data(),
        static_cast<int>(gEnexDateTimeFormat.size()));

    bool insideNote = false;
    bool insideNoteContent = false;
    bool insideNoteAttributes = false;
    bool insideResource = false;
    bool insideResourceData = false;
    bool insideResourceRecognitionData = false;
    bool insideResourceAlternateData = false;
    bool insideResourceAttributes = false;

    qevercloud::Note currentNote;
    QString currentNoteContent;

    qevercloud::Resource currentResource;
    QByteArray currentResourceData;
    QByteArray currentResourceRecognitionData;
    QByteArray currentResourceAlternateData;

    QXmlStreamReader reader{enex};

    const auto readNoteTimestamp =
        [&dateTimeFormat, &reader, &insideNote, &currentNote](
            const std::function<void(
                qevercloud::Note *, qevercloud::Timestamp)> & setter,
            const char * fieldName) -> Result<void, ErrorString> {
        if (!insideNote) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Detected timestamp tag related to note outside of note "
                "tag")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const QString timestampString =
            reader.readElementText(QXmlStreamReader::SkipChildElements);

        QNTRACE("enml::Converter", fieldName << ": " << timestampString);

        const auto dateTime =
            QDateTime::fromString(timestampString, dateTimeFormat);

        if (Q_UNLIKELY(!dateTime.isValid())) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter", "Failed to parse timestamp from string")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const auto timestamp = timestampFromDateTime(dateTime);
        setter(&currentNote, timestamp);
        QNTRACE("enml::Converter", "Set " << fieldName << " to " << timestamp);
        return Result<void, ErrorString>{};
    };

    const auto readDoubleNoteOrResourceAttribute =
        [&insideNote, &insideNoteAttributes, &insideResourceAttributes, &reader,
         &currentNote, &currentResource](
            const std::function<void(
                qevercloud::ResourceAttributes *, double)> & resourceSetter,
            const std::function<void(qevercloud::NoteAttributes *, double)> &
                noteSetter,
            const char * fieldName) -> Result<void, ErrorString> {
        if (!insideNote) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Detected tag of double type related to note outside of "
                "note tag")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const QString valueString =
            reader.readElementText(QXmlStreamReader::SkipChildElements);

        bool conversionResult = false;
        const double num = valueString.toDouble(&conversionResult);
        if (Q_UNLIKELY(!conversionResult)) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter", "Failed to parse attribute of double type")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        if (insideNoteAttributes) {
            auto & attributes = ensureNoteAttributes(currentNote);
            noteSetter(&attributes, num);
            QNTRACE(
                "enml::Converter",
                "Set note " << fieldName << " attribute to " << num);
            return Result<void, ErrorString>{};
        }

        if (insideResourceAttributes) {
            auto & attributes = ensureResourceAttributes(currentResource);
            resourceSetter(&attributes, num);
            QNTRACE(
                "enml::Converter",
                "Set resource " << fieldName << " attribute to " << num);
            return Result<void, ErrorString>{};
        }

        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Detected tag of double type outside of note attributes or "
            "resource attributes")};
        errorDescription.setDetails(fieldName);
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    };

    const auto readStringNoteAttribute =
        [&insideNote, &insideNoteAttributes, &reader, &currentNote](
            const std::function<void(qevercloud::NoteAttributes *, QString)> &
                setter,
            const char * fieldName) -> Result<void, ErrorString> {
        if (!insideNote || !insideNoteAttributes) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Detected tag of string type outside of note or note "
                "attributes")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const QString value =
            reader.readElementText(QXmlStreamReader::SkipChildElements);

        auto & attributes = ensureNoteAttributes(currentNote);
        setter(&attributes, value);
        QNTRACE(
            "enml::Converter",
            "Set " << fieldName << " note attribute to " << value);
        return Result<void, ErrorString>{};
    };

    const auto readTimestampNoteAttribute =
        [&insideNote, &insideNoteAttributes, &reader, &currentNote,
         &dateTimeFormat](
            const std::function<void(
                qevercloud::NoteAttributes *, qevercloud::Timestamp)> & setter,
            const char * fieldName) -> Result<void, ErrorString> {
        if (!insideNote || !insideNoteAttributes) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Detected tag of timestamp type outside of note or note "
                "attributes")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const QString timestampString =
            reader.readElementText(QXmlStreamReader::SkipChildElements);

        QNTRACE("enml::Converter", fieldName << ": " << timestampString);

        const auto dateTime =
            QDateTime::fromString(timestampString, dateTimeFormat);

        if (Q_UNLIKELY(!dateTime.isValid())) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter", "Failed to parse timestamp from string")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const auto timestamp = timestampFromDateTime(dateTime);
        auto & attributes = ensureNoteAttributes(currentNote);
        setter(&attributes, timestamp);
        QNTRACE("enml::Converter", "Set " << fieldName << " to " << timestamp);
        return Result<void, ErrorString>{};
    };

    const auto readStringResourceAttribute =
        [&insideResource, &insideResourceAttributes, &reader, &currentResource](
            const std::function<void(
                qevercloud::ResourceAttributes *, QString)> & setter,
            const char * fieldName) -> Result<void, ErrorString> {
        if (!insideResource || !insideResourceAttributes) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Detected tag of string type outside of "
                "resource or resource attributes")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml::Converter", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const QString value =
            reader.readElementText(QXmlStreamReader::SkipChildElements);

        auto & attributes = ensureResourceAttributes(currentResource);
        setter(&attributes, value);
        QNTRACE(
            "enml::Converter",
            "Set " << fieldName << " resource attribute to " << value);
        return Result<void, ErrorString>{};
    };

    const auto readStringNoteOrResourceAttribute =
        [&insideNote, &insideNoteAttributes, &insideResource,
         &insideResourceAttributes, &reader, &currentNote, &currentResource](
            const std::function<void(qevercloud::NoteAttributes *, QString)> &
                noteSetter,
            const std::function<void(
                qevercloud::ResourceAttributes *, QString)> & resourceSetter,
            const char * fieldName) -> Result<void, ErrorString> {
        if (!insideNote) {
            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Detected tag of string type related to note "
                "outside of note tag")};
            errorDescription.setDetails(fieldName);
            QNWARNING("enml", errorDescription);
            return Result<void, ErrorString>{std::move(errorDescription)};
        }

        const QString value =
            reader.readElementText(QXmlStreamReader::SkipChildElements);

        if (insideNoteAttributes) {
            auto & attributes = ensureNoteAttributes(currentNote);
            noteSetter(&attributes, value);
            QNTRACE(
                "enml::Converter",
                "Set " << fieldName << " note attribute to " << value);
            return Result<void, ErrorString>{};
        }

        if (insideResource && insideResourceAttributes) {
            auto & attributes = ensureResourceAttributes(currentResource);
            resourceSetter(&attributes, value);
            QNTRACE(
                "enml::Converter",
                "Set " << fieldName << " resource attribute to " << value);
            return Result<void, ErrorString>{};
        }

        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::Converter",
            "Detected tag of string type outside of note attributes or "
            "resource attributes")};
        errorDescription.setDetails(fieldName);
        QNWARNING("enml::Converter", errorDescription);
        return Result<void, ErrorString>{std::move(errorDescription)};
    };

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext())

        if (reader.isStartElement()) {
            QStringRef elementName = reader.name();

            if (elementName == QStringLiteral("en-export")) {
                continue;
            }

            if (elementName == QStringLiteral("export-date")) {
                QNTRACE(
                    "enml::Converter",
                    "export date: " << reader.readElementText(
                        QXmlStreamReader::SkipChildElements));
                continue;
            }

            if (elementName == QStringLiteral("application")) {
                QNTRACE(
                    "enml::Converter",
                    "application: " << reader.readElementText(
                        QXmlStreamReader::SkipChildElements));
                continue;
            }

            if (elementName == QStringLiteral("version")) {
                QNTRACE(
                    "enml::Converter",
                    "version" << reader.readElementText(
                        QXmlStreamReader::SkipChildElements));
                continue;
            }

            if (elementName == QStringLiteral("note")) {
                QNTRACE("enml::Converter", "Starting a new note");
                currentNote = qevercloud::Note{};
                insideNote = true;
                continue;
            }

            if (elementName == QStringLiteral("title")) {
                if (insideNote) {
                    QString title = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    QNTRACE("enml::Converter", "Note title: " << title);
                    if (!title.isEmpty()) {
                        currentNote.setTitle(title);
                    }
                    else {
                        currentNote.setTitle(std::nullopt);
                    }

                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected title tag outside of note tag")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("content")) {
                if (insideNote) {
                    QNTRACE("enml::Converter", "Start of note content");
                    insideNoteContent = true;
                    currentNoteContent.resize(0);
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected content tag outside of note tag")};

                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("created")) {
                auto res = readNoteTimestamp(
                    &qevercloud::Note::setCreated, "creation timestamp");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("updated")) {
                auto res = readNoteTimestamp(
                    &qevercloud::Note::setUpdated, "modification timestamp");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("tag")) {
                if (insideNote) {
                    QString tagName = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    if (!currentNote.tagNames()) {
                        currentNote.setTagNames(QList<QString>{});
                    }

                    if (!currentNote.tagNames()->contains(tagName)) {
                        currentNote.mutableTagNames()->append(tagName);
                        QNTRACE(
                            "enml::Converted",
                            "Added tag name " << tagName
                                              << " for note local id "
                                              << currentNote.localId());
                    }

                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter", "Detected tag outside of note")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("note-attributes")) {
                if (insideNote) {
                    QNTRACE("enml", "Start of note attributes");
                    insideNoteAttributes = true;
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected note-attributes tag outside of note")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("latitude")) {
                auto res = readDoubleNoteOrResourceAttribute(
                    &qevercloud::ResourceAttributes::setLatitude,
                    &qevercloud::NoteAttributes::setLatitude, "latitude");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("longitude")) {
                auto res = readDoubleNoteOrResourceAttribute(
                    &qevercloud::ResourceAttributes::setLongitude,
                    &qevercloud::NoteAttributes::setLongitude, "longitude");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("altitude")) {
                auto res = readDoubleNoteOrResourceAttribute(
                    &qevercloud::ResourceAttributes::setAltitude,
                    &qevercloud::NoteAttributes::setAltitude, "altitude");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("author")) {
                auto res = readStringNoteAttribute(
                    &qevercloud::NoteAttributes::setAuthor, "author");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("source")) {
                auto res = readStringNoteAttribute(
                    &qevercloud::NoteAttributes::setSource, "source");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("source-url")) {
                auto res = readStringNoteOrResourceAttribute(
                    &qevercloud::NoteAttributes::setSourceURL,
                    &qevercloud::ResourceAttributes::setSourceURL,
                    "source-url");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("source-application")) {
                auto res = readStringNoteAttribute(
                    &qevercloud::NoteAttributes::setSourceApplication,
                    "source-application");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("reminder-order")) {
                if (insideNote && insideNoteAttributes) {
                    QString reminderOrder = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    bool conversionResult = false;

                    qint64 reminderOrderNum =
                        reminderOrder.toLongLong(&conversionResult);

                    if (Q_UNLIKELY(!conversionResult)) {
                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Failed to parse reminder order")};
                        errorDescription.details() = reminderOrder;
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    auto & attributes = ensureNoteAttributes(currentNote);
                    attributes.setReminderOrder(reminderOrderNum);

                    QNTRACE(
                        "enml::Converter",
                        "Set the reminder order to " << reminderOrderNum);
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected reminder-order tag outside of note or note "
                    "attributes")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("reminder-time")) {
                auto res = readTimestampNoteAttribute(
                    &qevercloud::NoteAttributes::setReminderTime,
                    "reminder-time");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("subject-date")) {
                auto res = readTimestampNoteAttribute(
                    &qevercloud::NoteAttributes::setSubjectDate,
                    "subject-date");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("reminder-done-time")) {
                auto res = readTimestampNoteAttribute(
                    &qevercloud::NoteAttributes::setReminderDoneTime,
                    "reminder-done-time");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("place-name")) {
                auto res = readStringNoteAttribute(
                    &qevercloud::NoteAttributes::setPlaceName, "place-name");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("content-class")) {
                auto res = readStringNoteAttribute(
                    &qevercloud::NoteAttributes::setContentClass,
                    "content-class");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("application-data")) {
                if (insideNote) {
                    auto appDataAttributes = reader.attributes();
                    if (insideNoteAttributes) {
                        if (appDataAttributes.hasAttribute(
                                QStringLiteral("key")))
                        {
                            const QString key =
                                appDataAttributes.value(QStringLiteral("key"))
                                    .toString();

                            const QString value = reader.readElementText(
                                QXmlStreamReader::SkipChildElements);

                            auto & noteAttributes =
                                ensureNoteAttributes(currentNote);

                            auto & appData =
                                noteAttributes.mutableApplicationData();

                            if (!appData) {
                                appData = qevercloud::LazyMap();
                            }

                            if (!appData->keysOnly()) {
                                appData->setKeysOnly(QSet<QString>());
                            }

                            if (!appData->fullMap()) {
                                appData->setFullMap(QMap<QString, QString>());
                            }

                            Q_UNUSED(appData->mutableKeysOnly()->insert(key));
                            (*appData->mutableFullMap())[key] = value;

                            QNTRACE(
                                "enml::Converter",
                                "Inserted note application data entry: key = "
                                    << key << ", value = " << value);
                            continue;
                        }

                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Failed to parse application-data tag for note: no "
                            "key attribute")};
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    if (insideResourceAttributes) {
                        if (appDataAttributes.hasAttribute(
                                QStringLiteral("key")))
                        {
                            const QString key =
                                appDataAttributes.value(QStringLiteral("key"))
                                    .toString();

                            const QString value = reader.readElementText(
                                QXmlStreamReader::SkipChildElements);

                            auto & resourceAttributes =
                                ensureResourceAttributes(currentResource);

                            auto & appData =
                                resourceAttributes.mutableApplicationData();

                            if (!appData) {
                                appData = qevercloud::LazyMap();
                            }

                            if (!appData->keysOnly()) {
                                appData->setKeysOnly(QSet<QString>());
                            }

                            if (!appData->fullMap()) {
                                appData->setFullMap(QMap<QString, QString>());
                            }

                            Q_UNUSED(appData->mutableKeysOnly()->insert(key));
                            (*appData->mutableFullMap())[key] = value;

                            QNTRACE(
                                "enml::Converter",
                                "Inserted resource application data entry: key "
                                    << "= " << key << ", value = " << value);
                            continue;
                        }

                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Failed to parse application-data tag for "
                            "resource: no key attribute")};
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Detected application-data tag outside of note "
                        "attributes or resource attributes")};
                    QNWARNING("enml::Converter", errorDescription);
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(errorDescription)};
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected application-data tag outside of note")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("resource")) {
                QNTRACE("enml::Converter", "Start of resource tag");
                insideResource = true;

                currentResource = qevercloud::Resource{};

                currentResourceData.resize(0);
                currentResourceRecognitionData.resize(0);
                currentResourceAlternateData.resize(0);

                continue;
            }

            if (elementName == QStringLiteral("data")) {
                if (insideResource) {
                    QNTRACE("enml::Converter", "Start of resource data");
                    insideResourceData = true;
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected data tag outside of resource")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("mime")) {
                if (insideResource) {
                    const QString mime = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    currentResource.setMime(mime);
                    QNTRACE("enml::Converter", "Set resource mime to " << mime);
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected mime tag outside of resource")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("width")) {
                if (insideResource) {
                    const QString width = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    bool conversionResult = false;
                    const qint16 widthNum = width.toShort(&conversionResult);
                    if (Q_UNLIKELY(!conversionResult)) {
                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Failed to parse resource width from string")};
                        errorDescription.details() = width;
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    currentResource.setWidth(widthNum);
                    QNTRACE(
                        "enml::Converter",
                        "Set resource width to " << widthNum);
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected width tag outside of resource")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("height")) {
                if (insideResource) {
                    const QString height = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    bool conversionResult = false;
                    const qint16 heightNum = height.toShort(&conversionResult);
                    if (Q_UNLIKELY(!conversionResult)) {
                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Failed to parse resource height from string")};
                        errorDescription.details() = height;
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    currentResource.setHeight(heightNum);
                    QNTRACE(
                        "enml::Converter",
                        "Set resource height to " << heightNum);
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected height tag outside of resource")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("recognition")) {
                if (insideResource) {
                    QNTRACE(
                        "enml::Converter",
                        "Start of resource recognition data");
                    insideResourceRecognitionData = true;
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected recognition tag outside of resource")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("resource-attributes")) {
                if (insideResource) {
                    QNTRACE("enml::Converter", "Start of resource attributes");
                    insideResourceAttributes = true;
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected resource-attributes tag outside of resource")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("timestamp")) {
                if (insideResource && insideResourceAttributes) {
                    const QString timestampString = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    const auto timestampDateTime =
                        QDateTime::fromString(timestampString, dateTimeFormat);

                    if (Q_UNLIKELY(!timestampDateTime.isValid())) {
                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Failed to parse the resource timestamp from "
                            "string")};
                        errorDescription.details() = timestampString;
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    const qint64 timestamp =
                        timestampFromDateTime(timestampDateTime);

                    auto & resourceAttributes =
                        ensureResourceAttributes(currentResource);

                    resourceAttributes.setTimestamp(timestamp);
                    QNTRACE(
                        "enml::Converter",
                        "Set resource timestamp to " << timestamp);
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected timestamp tag outside of resource or resource "
                    "attributes")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("camera-make")) {
                auto res = readStringResourceAttribute(
                    &qevercloud::ResourceAttributes::setCameraMake,
                    "camera-make");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("reco-type")) {
                auto res = readStringResourceAttribute(
                    &qevercloud::ResourceAttributes::setRecoType, "reco-type");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("file-name")) {
                auto res = readStringResourceAttribute(
                    &qevercloud::ResourceAttributes::setFileName, "file-name");
                if (!res.isValid()) {
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(res.error())};
                }

                continue;
            }

            if (elementName == QStringLiteral("attachment")) {
                if (insideResource && insideResourceAttributes) {
                    const QString attachment = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    auto & resourceAttributes =
                        ensureResourceAttributes(currentResource);

                    if (attachment == QStringLiteral("true")) {
                        resourceAttributes.setAttachment(true);
                        QNTRACE("enml::Converter", "Set attachment to true");
                    }
                    else if (attachment == QStringLiteral("false")) {
                        resourceAttributes.setAttachment(false);
                        QNTRACE("enml::Converter", "Set attachment to false");
                    }
                    else {
                        ErrorString errorDescription{QT_TRANSLATE_NOOP(
                            "enml::Converter",
                            "Detected attachment tag with wrong value, must be "
                            "true or false")};
                        QNWARNING("enml::Converter", errorDescription);
                        return Result<QList<qevercloud::Note>, ErrorString>{
                            std::move(errorDescription)};
                    }

                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected attachment tag outside of resource or resource "
                    "attributes")};
                QNWARNING("enml::Converter", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }

            if (elementName == QStringLiteral("alternate-data")) {
                if (insideResource) {
                    QNTRACE(
                        "enml::Converter", "Start of resource alternate data");
                    insideResourceAlternateData = true;
                    continue;
                }

                ErrorString errorDescription{QT_TRANSLATE_NOOP(
                    "enml::Converter",
                    "Detected alternate-data tag outside of resource")};
                QNWARNING("enml", errorDescription);
                return Result<QList<qevercloud::Note>, ErrorString>{
                    std::move(errorDescription)};
            }
        }

        if (reader.isCharacters()) {
            if (insideNote) {
                if (insideNoteContent && reader.isCDATA()) {
                    currentNoteContent = reader.text().toString();
                    QNTRACE(
                        "enml::Converter",
                        "Current note content: " << currentNoteContent);
                    continue;
                }

                if (insideResource) {
                    if (insideResourceData) {
                        currentResourceData = QByteArray::fromBase64(
                            reader.text().toString().toLocal8Bit());
                        QNTRACE("enml::Converter", "Read resource data");
                        continue;
                    }

                    if (insideResourceRecognitionData) {
                        currentResourceRecognitionData =
                            reader.text().toString().toUtf8();

                        QNTRACE(
                            "enml::Converter",
                            "Read resource recognition data");

                        auto res = validateRecoIndex(
                            QString::fromUtf8(currentResourceRecognitionData));
                        if (!res.isValid()) {
                            ErrorString errorDescription{QT_TRANSLATE_NOOP(
                                "enml::Converter",
                                "Resource recognition index is invalid")};
                            const auto & error = res.error();
                            errorDescription.appendBase(error.base());
                            errorDescription.appendBase(
                                error.additionalBases());
                            errorDescription.details() = error.details();
                            QNWARNING("enml::Converter", errorDescription);
                            return Result<QList<qevercloud::Note>, ErrorString>{
                                std::move(errorDescription)};
                        }

                        continue;
                    }

                    if (insideResourceAlternateData) {
                        currentResourceAlternateData = QByteArray::fromBase64(
                            reader.text().toString().toLocal8Bit());
                        QNTRACE(
                            "enml::Converter", "Read resource alternate data");
                        continue;
                    }
                }
            }
        }

        if (reader.isEndElement()) {
            const QStringRef elementName = reader.name();

            if (elementName == QStringLiteral("content")) {
                QNTRACE(
                    "enml::Converter",
                    "End of note content: " << currentNoteContent);
                currentNote.setContent(currentNoteContent);
                insideNoteContent = false;
                continue;
            }

            if (elementName == QStringLiteral("note-attributes")) {
                QNTRACE("enml::Converter", "End of note attributes");
                insideNoteAttributes = false;
                continue;
            }

            if (elementName == QStringLiteral("resource-attributes")) {
                QNTRACE("enml::Converter", "End of resource attributes");
                insideResourceAttributes = false;
                continue;
            }

            if (elementName == QStringLiteral("data")) {
                QNTRACE("enml::Converter", "End of resource data");

                if (!currentResource.data()) {
                    currentResource.setData(qevercloud::Data{});
                }
                currentResource.mutableData()->setBody(currentResourceData);

                currentResource.mutableData()->setBodyHash(
                    QCryptographicHash::hash(
                        currentResourceData, QCryptographicHash::Md5));

                currentResource.mutableData()->setSize(
                    currentResourceData.size());

                insideResourceData = false;
                continue;
            }

            if (elementName == QStringLiteral("recognition")) {
                QNTRACE("enml::Converter", "End of resource recognition data");

                if (!currentResource.recognition()) {
                    currentResource.setRecognition(qevercloud::Data{});
                }

                currentResource.mutableRecognition()->setBody(
                    currentResourceRecognitionData);

                currentResource.mutableRecognition()->setBodyHash(
                    QCryptographicHash::hash(
                        currentResourceRecognitionData,
                        QCryptographicHash::Md5));

                currentResource.mutableRecognition()->setSize(
                    currentResourceRecognitionData.size());

                insideResourceRecognitionData = false;
                continue;
            }

            if (elementName == QStringLiteral("alternate-data")) {
                QNTRACE("enml::Converter", "End of resource alternate data");

                if (!currentResource.alternateData()) {
                    currentResource.setAlternateData(qevercloud::Data{});
                }

                currentResource.mutableAlternateData()->setBody(
                    currentResourceAlternateData);

                currentResource.mutableAlternateData()->setBodyHash(
                    QCryptographicHash::hash(
                        currentResourceAlternateData, QCryptographicHash::Md5));

                currentResource.mutableAlternateData()->setSize(
                    currentResourceAlternateData.size());

                insideResourceAlternateData = false;
                continue;
            }

            if (elementName == QStringLiteral("resource")) {
                QNTRACE("enml::Converter", "End of resource");

                if (Q_UNLIKELY(
                        !(currentResource.data() &&
                          currentResource.data()->body())))
                {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Parsed resource without a data body")};
                    QNWARNING(
                        "enml::Converter",
                        errorDescription << ", resource: " << currentResource);
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(errorDescription)};
                }

                if (Q_UNLIKELY(
                        !(currentResource.data() &&
                          currentResource.data()->bodyHash())))
                {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Internal error: data hash is not computed for the "
                        "resource")};
                    QNWARNING(
                        "enml::Converter",
                        errorDescription << ", resource: " << currentResource);
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(errorDescription)};
                }

                if (Q_UNLIKELY(
                        !(currentResource.data() &&
                          currentResource.data()->size())))
                {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Internal error: data size is not computed "
                        "for the resource")};
                    QNWARNING(
                        "enml::Converter",
                        errorDescription << ", resource: " << currentResource);
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(errorDescription)};
                }

                if (Q_UNLIKELY(!currentResource.mime())) {
                    ErrorString errorDescription{QT_TRANSLATE_NOOP(
                        "enml::Converter",
                        "Parsed resource without a mime type")};
                    QNWARNING(
                        "enml::Converter",
                        errorDescription << ", resource: " << currentResource);
                    return Result<QList<qevercloud::Note>, ErrorString>{
                        std::move(errorDescription)};
                }

                insideResource = false;

                if (!currentNote.resources()) {
                    currentNote.setResources(QList<qevercloud::Resource>());
                }

                currentNote.mutableResources()->push_back(currentResource);
                QNTRACE("enml", "Added resource to note: " << currentResource);

                currentResource = qevercloud::Resource{};
                continue;
            }

            if (elementName == QStringLiteral("note")) {
                QNTRACE("enml", "End of note: " << currentNote);
                notes << currentNote;
                currentNote = qevercloud::Note{};
                insideNote = false;
                continue;
            }
        }
    }

    QNDEBUG("enml::Converter", "ENEX import end: num notes = " << notes.size());
    return Result<QList<qevercloud::Note>, ErrorString>{std::move(notes)};
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
            QNTRACE(
                "enml::Converter", "Erasing custom attribute en-hyperlink-id");
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
        QNWARNING("enml::Converter", errorDescription);
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

Result<void, ErrorString> Converter::validateRecoIndex(
    const QString & recoIndex) const
{
    QNDEBUG(
        "enml::Converter",
        "Converter::validateRecoIndex: reco index = " << recoIndex);

    return validateAgainstDtd(recoIndex, QStringLiteral(":/recoIndex.dtd"));
}

Result<void, ErrorString> Converter::validateEnex(const QString & enex) const
{
    QNDEBUG("enml::Converter", "Converter::validateEnex");

    return validateAgainstDtd(enex, QStringLiteral(":/evernote-export3.dtd"));
}

} // namespace quentier::enml
