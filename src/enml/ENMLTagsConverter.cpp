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

#include "ENMLTagsConverter.h"

#include <quentier/enml/HtmlUtils.h>
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/types/Resource.h>

#include <QBuffer>
#include <QTextStream>
#include <QXmlStreamWriter>

namespace quentier::enml {

QString ENMLTagsConverter::convertEnToDo(bool checked, quint32 index) const
{
    QString html;
    QTextStream strm{&html};

    strm << R"#(<img src="qrc:/checkbox_icons/checkbox_)#";
    if (checked) {
        strm << R"#(yes.png" class="checkbox_checked" )#";
    }
    else {
        strm << R"#(no.png" class="checkbox_unchecked" )#";
    }

    strm << R"#(en-tag="en-todo" en-todo-id=")#";
    strm << index;
    strm << R"#(" />)#";
    return html;
}

QString ENMLTagsConverter::convertEncryptedText(
    const QString & encryptedText, const QString & hint, const QString & cipher,
    std::size_t keyLength, quint32 index) const
{
    QString html;
    QTextStream strm{&html};

    strm << R"#(<img en-tag="en-crypt" cipher=")#";
    strm << cipher;
    strm << R"#(" length=")#";
    strm << keyLength;

    strm << R"#(" class="en-crypt hvr-border-color" encrypted_text=")#";
    strm << encryptedText;
    strm << R"#(" en-crypt-id=")#";
    strm << index;
    strm << R"#(" )#";

    if (!hint.isEmpty()) {
        strm << R"#(hint=")#";

        QString escapedHint = utils::htmlEscapeString(
            hint,
            utils::EscapeStringOptions{} | utils::EscapeStringOption::Simplify);

        strm << escapedHint;
        strm << R"#(" )#";
    }

    strm << " />";
    return html;
}

QString ENMLTagsConverter::convertDecryptedText(
    const QString & decryptedText, const QString & encryptedText,
    const QString & hint, const QString & cipher, std::size_t keyLength,
    quint32 index) const
{
    QString result;
    QXmlStreamWriter writer{&result};

    writer.writeStartElement(QStringLiteral("div"));

    writer.writeAttribute(
        QStringLiteral("en-tag"), QStringLiteral("en-decrypted"));

    writer.writeAttribute(QStringLiteral("encrypted_text"), encryptedText);

    writer.writeAttribute(
        QStringLiteral("en-decrypted-id"), QString::number(index));

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
                continue;
            }

            writer.writeStartElement(decryptedTextReader.name().toString());
            writer.writeAttributes(attributes);

            foundFormattedText = true;
        }

        if (decryptedTextReader.isCharacters()) {
            writer.writeCharacters(decryptedTextReader.text().toString());

            foundFormattedText = true;
        }

        if (decryptedTextReader.isEndElement()) {
            const auto attributes = decryptedTextReader.attributes();
            if (attributes.hasAttribute(QStringLiteral("id")) &&
                (attributes.value(QStringLiteral("id")) ==
                 QStringLiteral("decrypted_text_html_to_enml_temporary")))
            {
                continue;
            }

            writer.writeEndElement();
        }
    }

    if (decryptedTextReader.hasError()) {
        QNWARNING(
            "enml::ENMLTagsConverter",
            "Decrypted text reader has error: "
                << decryptedTextReader.errorString());
    }

    if (!foundFormattedText) {
        writer.writeCharacters(decryptedText);
    }

    writer.writeEndElement();
    return result;
}

Result<QString, ErrorString> ENMLTagsConverter::convertResource(
    const qevercloud::Resource & resource) const
{
    if (Q_UNLIKELY(!(resource.data() && resource.data()->bodyHash()))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::ENMLTagsConverter",
            "Can't compose the resource's html "
            "representation: no data hash is set")};
        QNWARNING(
            "enml::ENMLTagsConverter",
            errorDescription << ", resource: " << resource);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    if (Q_UNLIKELY(!resource.mime())) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::ENMLTagsConverter",
            "Can't compose the resource's html "
            "representation: no mime type is set")};
        QNWARNING(
            "enml::ENMLTagsConverter",
            errorDescription << ", resource: " << resource);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamAttributes attributes;

    attributes.append(
        QStringLiteral("hash"),
        QString::fromLocal8Bit(resource.data()->bodyHash()->toHex()));

    attributes.append(QStringLiteral("type"), *resource.mime());

    QBuffer htmlBuffer;
    bool res = htmlBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::ENMLTagsConverter",
            "Can't compose the resource's html representation: "
            "can't open the buffer to write the html into")};
        errorDescription.details() = htmlBuffer.errorString();
        QNWARNING(
            "enml::ENMLTagsConverter",
            errorDescription << ", resource: " << resource);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    QXmlStreamWriter writer(&htmlBuffer);
    if (!attributes.hasAttribute(QStringLiteral("hash"))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::ENMLTagsConverter",
            "Detected incorrect en-media tag missing hash attribute")};
        QNWARNING(
            "enml::ENMLTagsConverter",
            errorDescription << ", resource: " << resource);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    if (!attributes.hasAttribute(QStringLiteral("type"))) {
        ErrorString errorDescription{QT_TRANSLATE_NOOP(
            "enml::ENMLTagsConverter",
            "Detected incorrect en-media tag missing type "
            "attribute")};
        QNWARNING(
            "enml::ENMLTagsConverter",
            errorDescription << ", resource: " << resource);
        return Result<QString, ErrorString>{std::move(errorDescription)};
    }

    const auto mimeType = attributes.value(QStringLiteral("type"));
    bool inlineImage = false;
    if (mimeType.startsWith(QStringLiteral("image"), Qt::CaseInsensitive)) {
        // TODO: consider some proper high-level interface for making it
        // possible to customize ENML <--> HTML conversion
        inlineImage = true;
    }

    writer.writeStartElement(QStringLiteral("img"));

    // NOTE: ENMLTagsConverter can't set src attribute for img tag as it
    // doesn't know whether the resource is stored in any local file yet.
    // The user of noteContentToHtml should take care of those img tags and
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

    writer.writeEndElement();
    return Result<QString, ErrorString>{QString::fromUtf8(htmlBuffer.buffer())};
}

} // namespace quentier::enml
