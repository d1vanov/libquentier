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
#include "HtmlUtils.h"

#include <quentier/logging/QuentierLogger.h>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace quentier::enml {

// clang-format off
Converter::Converter() :
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
{}
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
        ErrorString errorDescription{
            QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Failed to open the buffer to write the converted ENML")};
        errorDescription.details() = noteContentBuffer.errorString();
        return Result<QString, ErrorString>{errorDescription};
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
                return Result<QString, ErrorString>{errorDescription};
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
        ErrorString errorDescription{
            QT_TRANSLATE_NOOP(
                "enml::Converter",
                "Can't convert the note's html to ENML")};
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml::Converter",
            "Error reading html: " << errorDescription << ", HTML: " << html
                                   << "\nXML: " << res.get());
        return Result<QString, ErrorString>{errorDescription};
    }

    QString enml = QString::fromUtf8(noteContentBuffer.buffer());
    QNTRACE("enml::Converter", "Converted ENML: " << enml);

    res = validateAndFixupEnml(enml);
    if (!res.isValid()) {
        QNWARNING(
            "enml::Converter",
            res.error() << ", ENML: " << enml
                             << "\nHTML: " << html);
        return Result<QString, ErrorString>{res.error()};
    }

    return Result<QString, ErrorString>{res.get()};
}

} // namespace quentier::enml
