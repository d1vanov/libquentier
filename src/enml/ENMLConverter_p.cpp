/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "ENMLConverter_p.h"

#include <quentier/enml/DecryptedTextManager.h>
#include <quentier/enml/HTMLCleaner.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/NoteUtils.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/utility/ToRange.h>

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>
#include <QThread>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <libxml/xmlreader.h>

#include <functional>
#include <memory>

// 25 Mb in bytes
#define ENEX_MAX_RESOURCE_DATA_SIZE (26214400)

#define ENEX_DATE_TIME_FORMAT          "yyyyMMdd'T'HHmmss'Z'"
#define ENEX_DATE_TIME_FORMAT_STRFTIME "%Y%m%dT%H%M%SZ"

namespace quentier {

#define WRAP(x) << QStringLiteral(x)

ENMLConverterPrivate::ENMLConverterPrivate(QObject * parent) :
    QObject(parent), m_forbiddenXhtmlTags(QSet<QString>()
#include "forbiddenXhtmlTags.inl"
                                              ),
    m_forbiddenXhtmlAttributes(QSet<QString>()
#include "forbiddenXhtmlAttributes.inl"
                                   ),
    m_evernoteSpecificXhtmlTags(QSet<QString>()
#include "evernoteSpecificXhtmlTags.inl"
                                    ),
    m_allowedXhtmlTags(QSet<QString>()
#include "allowedXhtmlTags.inl"
                           ),
    m_allowedEnMediaAttributes(QSet<QString>()
#include "allowedEnMediaAttributes.inl"
                                   ),
    m_pHtmlCleaner(nullptr), m_cachedConvertedXml()
{}

#undef WRAP

namespace {

void xmlValidationErrorFunc(void * ctx, const char * msg, va_list args)
{
    QNDEBUG("enml", "xmlValidationErrorFunc");

    QString currentError = QString::asprintf(msg, args);

    auto * pErrorString = reinterpret_cast<QString *>(ctx);
    *pErrorString += currentError;
    QNDEBUG("enml", "Error string: " << *pErrorString);
}

[[nodiscard]] qevercloud::Timestamp timestampFromDateTime(
    const QDateTime & dateTime)
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

} // namespace

////////////////////////////////////////////////////////////////////////////////

ENMLConverterPrivate::~ENMLConverterPrivate()
{
    delete m_pHtmlCleaner;
}

bool ENMLConverterPrivate::htmlToNoteContent(
    const QString & html, const QList<SkipHtmlElementRule> & skipRules,
    QString & noteContent, DecryptedTextManager & decryptedTextManager,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::htmlToNoteContent: "
            << html << "\nskip element rules: " << skipRules);

    if (!m_pHtmlCleaner) {
        m_pHtmlCleaner = new HTMLCleaner;
    }

    QString error;
    m_cachedConvertedXml.resize(0);
    if (!m_pHtmlCleaner->htmlToXml(html, m_cachedConvertedXml, error)) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to clean up the note's html"));
        errorDescription.details() = error;
        return false;
    }

    QNTRACE("enml", "HTML converted to XML by tidy: " << m_cachedConvertedXml);

    QXmlStreamReader reader{m_cachedConvertedXml};

    noteContent.resize(0);
    QBuffer noteContentBuffer;
    if (Q_UNLIKELY(!noteContentBuffer.open(QIODevice::WriteOnly))) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to open the buffer to write the converted note "
                       "content into"));
        errorDescription.details() = noteContentBuffer.errorString();
        return false;
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
            auto status = processElementForHtmlToNoteContentConversion(
                skipRules, state, decryptedTextManager, reader, writer,
                errorDescription);

            if (status == ProcessElementStatus::Error) {
                return false;
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
                QNTRACE("enml", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("enml", "Wrote characters: " << text);
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
        errorDescription.setBase(
            QT_TR_NOOP("Can't convert the note's html to ENML"));
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml",
            "Error reading html: " << errorDescription << ", HTML: " << html
                                   << "\nXML: " << m_cachedConvertedXml);
        return false;
    }

    noteContent = QString::fromUtf8(noteContentBuffer.buffer());
    QNTRACE("enml", "Converted ENML: " << noteContent);

    ErrorString validationError;
    if (!validateAndFixupEnml(noteContent, validationError)) {
        errorDescription = validationError;
        QNWARNING(
            "enml",
            errorDescription << ", ENML: " << noteContent
                             << "\nHTML: " << html);
        return false;
    }

    return true;
}

bool ENMLConverterPrivate::htmlToQTextDocument(
    const QString & html, QTextDocument & doc, ErrorString & errorDescription,
    const QList<SkipHtmlElementRule> & skipRules) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::htmlToQTextDocument: " << html);

    if (!m_pHtmlCleaner) {
        m_pHtmlCleaner = new HTMLCleaner;
    }

    QString error;
    m_cachedConvertedXml.resize(0);
    bool res = m_pHtmlCleaner->htmlToXml(html, m_cachedConvertedXml, error);
    if (!res) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to clean up the note's html"));
        errorDescription.details() = error;
        return false;
    }

    QNTRACE("enml", "HTML converted to XML by tidy: " << m_cachedConvertedXml);

    QXmlStreamReader reader(m_cachedConvertedXml);

    QBuffer simplifiedHtmlBuffer;
    res = simplifiedHtmlBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to open the buffer to write "
                       "the simplified html into"));
        errorDescription.details() = simplifiedHtmlBuffer.errorString();
        return false;
    }

    QXmlStreamWriter writer(&simplifiedHtmlBuffer);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    writer.writeDTD(
        QStringLiteral("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "
                       "\"http://www.w3.org/TR/html4/strict.dtd\">"));

    int writeElementCounter = 0;
    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

    size_t skippedElementNestingCounter = 0;
    size_t skippedElementWithPreservedContentsNestingCounter = 0;

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
                    "enml",
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
                    "enml",
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
                    shouldSkip == SkipElementOption::SkipButPreserveContents) {
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
                        "enml",
                        "Skipping CSS style element " << lastElementName);
                    ++skippedElementNestingCounter;
                    continue;
                }
            }

            if (lastElementName == QStringLiteral("abbr")) {
                lastElementName = QStringLiteral("div");
                QNTRACE("enml", "Replaced abbr with div");
            }
            else if (lastElementName == QStringLiteral("acronym")) {
                lastElementName = QStringLiteral("u");
                QNTRACE("enml", "Replaced acronym with u");
            }
            else if (lastElementName == QStringLiteral("del")) {
                lastElementName = QStringLiteral("s");
                QNTRACE("enml", "Replaced del with s");
            }
            else if (lastElementName == QStringLiteral("ins")) {
                lastElementName = QStringLiteral("u");
                QNTRACE("enml", "Replaced ins with u");
            }
            else if (lastElementName == QStringLiteral("q")) {
                lastElementName = QStringLiteral("blockquote");
                QNTRACE("enml", "Replaced q with blockquote");
            }
            else if (lastElementName == QStringLiteral("strike")) {
                lastElementName = QStringLiteral("s");
                QNTRACE("enml", "Replaced strike with s");
            }
            else if (lastElementName == QStringLiteral("xmp")) {
                lastElementName = QStringLiteral("tt");
                QNTRACE("enml", "Replaced xmp with tt");
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
                        (dirAttr == QStringLiteral("rtl"))) {
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
                    errorDescription.setBase(
                        QT_TR_NOOP("Found img tag without src or with empty "
                                   "src attribute"));
                    return false;
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

                    if (!typeAttr.isEmpty()) {
                        if (!typeAttr.startsWith(QStringLiteral("image/"))) {
                            isGenericResourceImage = true;
                        }
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
                    !existingDocImgData.isValid()) {
                    if (srcAttr.startsWith(QStringLiteral("qrc:/"))) {
                        QString srcAttrShortened = srcAttr;
                        srcAttrShortened.remove(0, 3);
                        img = QImage{srcAttrShortened, "PNG"};
                    }
                    else {
                        const QFileInfo imgFileInfo{srcAttr};
                        if (!imgFileInfo.exists()) {
                            errorDescription.setBase(
                                QT_TR_NOOP("Couldn't find the file "
                                           "corresponding to the src attribute "
                                           "of img tag"));
                            errorDescription.details() = srcAttr;
                            return false;
                        }

                        img = QImage{srcAttr, "PNG"};
                    }

                    shouldAddImgAsResource = true;
                }
                else {
                    QNDEBUG(
                        "enml",
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
                                "enml",
                                "Won't add the outline to the generic resource "
                                "image: the method is not run inside the main "
                                "thread");
                        }
                    }
                    else {
                        QNTRACE(
                            "enml",
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

            QNTRACE("enml", "Wrote element: name = " << lastElementName);
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            if (skippedElementNestingCounter) {
                continue;
            }

            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("enml", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("enml", "Wrote characters: " << text);
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
        errorDescription.setBase(
            QT_TR_NOOP("Can't convert the note's html to QTextDocument"));

        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml",
            "Error reading html: " << errorDescription << ", HTML: " << html
                                   << "\nXML: " << m_cachedConvertedXml);
        return false;
    }

    QString simplifiedHtml = QString::fromUtf8(simplifiedHtmlBuffer.buffer());

    doc.setHtml(simplifiedHtml);
    if (Q_UNLIKELY(doc.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't convert the note's html to QTextDocument: the "
                       "document is empty after setting the simplified HTML"));

        QNWARNING(
            "enml",
            errorDescription << ", simplified HTML: " << simplifiedHtml);
        return false;
    }

    return true;
}

bool ENMLConverterPrivate::cleanupExternalHtml(
    const QString & inputHtml, QString & cleanedUpHtml,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::cleanupExternalHtml: input HTML = "
            << inputHtml);

    if (!m_pHtmlCleaner) {
        m_pHtmlCleaner = new HTMLCleaner;
    }

    QString supplementedHtml = QStringLiteral("<html><body>");
    supplementedHtml += inputHtml;
    supplementedHtml += QStringLiteral("</body></html>");

    QString error;
    m_cachedConvertedXml.resize(0);

    bool res = m_pHtmlCleaner->htmlToXml(
        supplementedHtml, m_cachedConvertedXml, error);

    if (!res) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to clean up the input HTML"));
        errorDescription.details() = error;
        return false;
    }

    QNTRACE("enml", "HTML converted to XML: " << m_cachedConvertedXml);

    QXmlStreamReader reader(m_cachedConvertedXml);

    QBuffer outputSupplementedHtmlBuffer;
    res = outputSupplementedHtmlBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to open the buffer to write the clean html "
                       "into"));
        errorDescription.details() = outputSupplementedHtmlBuffer.errorString();
        return false;
    }

    QXmlStreamWriter writer(&outputSupplementedHtmlBuffer);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");

    int writeElementCounter = 0;
    QString lastElementName;
    QXmlStreamAttributes lastElementAttributes;

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
            lastElementName = reader.name().toString();

            auto tagIt = m_forbiddenXhtmlTags.constFind(lastElementName);
            if (tagIt != m_forbiddenXhtmlTags.constEnd()) {
                QNTRACE("enml", "Skipping forbidden tag: " << lastElementName);
                continue;
            }

            tagIt = m_allowedXhtmlTags.constFind(lastElementName);
            if (tagIt == m_allowedXhtmlTags.end()) {
                QNTRACE(
                    "enml",
                    "Haven't found tag "
                        << lastElementName
                        << " within the list of allowed XHTML tags, skipping "
                        << "it");
                continue;
            }

            lastElementAttributes = reader.attributes();

            // Erasing forbidden attributes
            for (auto it = lastElementAttributes.begin(); // NOLINT
                 it != lastElementAttributes.end();)
            {
                QStringRef attributeName = it->name();
                if (isForbiddenXhtmlAttribute(attributeName.toString())) {
                    QNTRACE(
                        "enml",
                        "Erasing forbidden attribute " << attributeName);
                    it = lastElementAttributes.erase(it);
                    continue;
                }

                ++it;
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);
            ++writeElementCounter;

            QNTRACE(
                "enml",
                "Wrote element: name = " << lastElementName
                                         << " and its attributes");
        }

        if ((writeElementCounter > 0) && reader.isCharacters()) {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("enml", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("enml", "Wrote characters: " << text);
            }
        }

        if (reader.isEndElement()) {
            if (writeElementCounter <= 0) {
                continue;
            }

            writer.writeEndElement();
            --writeElementCounter;
        }
    }

    if (reader.hasError()) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to clean up the input HTML"));
        errorDescription.details() = reader.errorString();

        QNWARNING(
            "enml",
            "Error reading the input HTML: "
                << errorDescription << ", input HTML: " << inputHtml
                << "\n\nSupplemented input HTML: " << supplementedHtml
                << "\n\nHTML converted to XML: " << m_cachedConvertedXml);
        return false;
    }

    cleanedUpHtml = QString::fromUtf8(outputSupplementedHtmlBuffer.buffer());
    QNDEBUG("enml", "Cleaned up HTML: " << cleanedUpHtml);

    return true;
}

bool ENMLConverterPrivate::noteContentToHtml(
    const QString & noteContent, QString & html, ErrorString & errorDescription,
    DecryptedTextManager & decryptedTextManager,
    NoteContentToHtmlExtraData & extraData) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::noteContentToHtml: " << noteContent);

    extraData.m_numEnToDoNodes = 0;
    extraData.m_numHyperlinkNodes = 0;
    extraData.m_numEnCryptNodes = 0;
    extraData.m_numEnDecryptedNodes = 0;

    html.resize(0);
    errorDescription.clear();
    QBuffer htmlBuffer;
    bool res = htmlBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to open the buffer to "
                       "write the html into"));
        errorDescription.details() = htmlBuffer.errorString();
        return false;
    }

    QXmlStreamReader reader(noteContent);

    QXmlStreamWriter writer(&htmlBuffer);
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
                bool res = resourceInfoToHtml(
                    lastElementAttributes, writer, errorDescription);

                if (!res) {
                    return false;
                }

                continue;
            }
            else if (lastElementName == QStringLiteral("en-crypt")) {
                insideEnCryptTag = true;
                continue;
            }
            else if (lastElementName == QStringLiteral("en-todo")) {
                quint64 enToDoIndex = extraData.m_numEnToDoNodes + 1;
                toDoTagsToHtml(reader, enToDoIndex, writer);
                ++extraData.m_numEnToDoNodes;
                continue;
            }
            else if (lastElementName == QStringLiteral("a")) {
                quint64 hyperlinkIndex = extraData.m_numHyperlinkNodes + 1;

                lastElementAttributes.append(
                    QStringLiteral("en-hyperlink-id"),
                    QString::number(hyperlinkIndex));

                ++extraData.m_numHyperlinkNodes;
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
                quint64 enCryptIndex = extraData.m_numEnCryptNodes + 1;
                quint64 enDecryptedIndex = extraData.m_numEnDecryptedNodes + 1;
                bool convertedToEnCryptNode = false;

                if (!encryptedTextToHtml(
                    lastElementAttributes, reader.text(), enCryptIndex,
                    enDecryptedIndex, writer, decryptedTextManager,
                    convertedToEnCryptNode))
                {
                    return false;
                }

                if (convertedToEnCryptNode) {
                    ++extraData.m_numEnCryptNodes;
                }
                else {
                    ++extraData.m_numEnDecryptedNodes;
                }

                insideEnCryptTag = false;
                continue;
            }

            QString data = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(data);
                QNTRACE("enml", "Wrote CDATA: " << data);
            }
            else {
                writer.writeCharacters(data);
                QNTRACE("enml", "Wrote characters: " << data);
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
        QNWARNING("enml", "Error reading ENML: " << reader.errorString());
        return false;
    }

    html = QString::fromUtf8(htmlBuffer.buffer());
    return true;
}

bool ENMLConverterPrivate::validateEnml(
    const QString & enml, ErrorString & errorDescription) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::validateEnml");

    return validateAgainstDtd(
        enml, QStringLiteral(":/enml2.dtd"), errorDescription);
}

bool ENMLConverterPrivate::validateAndFixupEnml(
    QString & enml, ErrorString & errorDescription) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::validateAndFixupEnml: " << enml);

    bool res = validateEnml(enml, errorDescription);
    if (res) {
        return true;
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

    QString error = errorDescription.details();
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
        QNTRACE("enml", "Parsed forbidden attributes per element: ");

        for (const auto it:
             qevercloud::toRange(qAsConst(elementToForbiddenAttributes)))
        {
            QNTRACE("enml", "[" << it.key() << "]: " << it.value());
        }
    }

    QBuffer fixedUpEnmlBuffer;
    res = fixedUpEnmlBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Failed to open the buffer to write the fixed up note "
                       "content into"));
        errorDescription.details() = fixedUpEnmlBuffer.errorString();
        return false;
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
                    "enml",
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
                QString attributeName = ait->name().toString();
                if (forbiddenAttributes.contains(attributeName)) {
                    QNTRACE(
                        "enml",
                        "Erasing forbidden attribute " << attributeName);

                    ait = lastElementAttributes.erase(ait);
                    continue;
                }

                ++ait;
            }

            writer.writeStartElement(lastElementName);
            writer.writeAttributes(lastElementAttributes);

            QNTRACE(
                "enml",
                "Wrote element: name = " << lastElementName
                                         << " and its attributes");
        }

        if (reader.isCharacters()) {
            QString text = reader.text().toString();

            if (reader.isCDATA()) {
                writer.writeCDATA(text);
                QNTRACE("enml", "Wrote CDATA: " << text);
            }
            else {
                writer.writeCharacters(text);
                QNTRACE("enml", "Wrote characters: " << text);
            }
        }

        if (reader.isEndElement()) {
            writer.writeEndElement();
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        QNWARNING(
            "enml",
            "Wasn't able to fixup the ENML as it is "
                << "a malformed XML: " << reader.errorString());
        return false;
    }

    enml = QString::fromUtf8(fixedUpEnmlBuffer.buffer());
    QNTRACE("enml", "ENML after fixing up: " << enml);

    return validateEnml(enml, errorDescription);
}

bool ENMLConverterPrivate::noteContentToPlainText(
    const QString & noteContent, QString & plainText,
    ErrorString & errorMessage)
{
    QNTRACE(
        "enml",
        "ENMLConverterPrivate::noteContentToPlainText: " << noteContent);

    plainText.resize(0);

    QXmlStreamReader reader(noteContent);

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
            plainText += reader.text();
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        errorMessage.setBase(
            QT_TR_NOOP("Failed to convert the note content to plain text"));
        errorMessage.details() = reader.errorString();
        errorMessage.details() += QStringLiteral(", error code ");
        errorMessage.details() += QString::number(reader.error());
        QNWARNING("enml", errorMessage);
        return false;
    }

    return true;
}

bool ENMLConverterPrivate::noteContentToListOfWords(
    const QString & noteContent, QStringList & listOfWords,
    ErrorString & errorMessage, QString * plainText)
{
    QString localPlainText;

    bool res =
        noteContentToPlainText(noteContent, localPlainText, errorMessage);

    if (!res) {
        listOfWords.clear();
        return false;
    }

    if (plainText) {
        *plainText = localPlainText;
    }

    listOfWords = plainTextToListOfWords(localPlainText);
    return true;
}

QStringList ENMLConverterPrivate::plainTextToListOfWords(
    const QString & plainText)
{
    // Simply remove all non-word characters from plain text
    return plainText.split(
        QRegularExpression{QStringLiteral("\\W+")},
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        Qt::SkipEmptyParts);
#else
        QString::SkipEmptyParts);
#endif
}

QString ENMLConverterPrivate::toDoCheckboxHtml(
    const bool checked, const quint64 idNumber)
{
    QString html = QStringLiteral("<img src=\"qrc:/checkbox_icons/checkbox_");
    if (checked) {
        html += QStringLiteral("yes.png\" class=\"checkbox_checked\" ");
    }
    else {
        html += QStringLiteral("no.png\" class=\"checkbox_unchecked\" ");
    }

    html += QStringLiteral("en-tag=\"en-todo\" en-todo-id=\"");
    html += QString::number(idNumber);
    html += QStringLiteral("\" />");
    return html;
}

QString ENMLConverterPrivate::encryptedTextHtml(
    const QString & encryptedText, const QString & hint, const QString & cipher,
    const size_t keyLength, const quint64 enCryptIndex)
{
    QString encryptedTextHtmlObject;

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    encryptedTextHtmlObject = QStringLiteral("<img ");
#else
    encryptedTextHtmlObject =
        QStringLiteral("<object type=\"application/vnd.quentier.encrypt\" ");
#endif
    encryptedTextHtmlObject += QStringLiteral("en-tag=\"en-crypt\" cipher=\"");
    encryptedTextHtmlObject += cipher;
    encryptedTextHtmlObject += QStringLiteral("\" length=\"");
    encryptedTextHtmlObject += QString::number(keyLength);

    encryptedTextHtmlObject += QStringLiteral(
        "\" class=\"en-crypt hvr-border-color\" "
        "encrypted_text=\"");

    encryptedTextHtmlObject += encryptedText;
    encryptedTextHtmlObject += QStringLiteral("\" en-crypt-id=\"");
    encryptedTextHtmlObject += QString::number(enCryptIndex);
    encryptedTextHtmlObject += QStringLiteral("\" ");

    if (!hint.isEmpty()) {
        encryptedTextHtmlObject += QStringLiteral("hint=\"");

        QString hintWithEscapedDoubleQuotes = hint;
        escapeString(hintWithEscapedDoubleQuotes, /* simplify = */ true);

        encryptedTextHtmlObject += hintWithEscapedDoubleQuotes;
        encryptedTextHtmlObject += QStringLiteral("\" ");
    }

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    encryptedTextHtmlObject += QStringLiteral(" />");
#else
    encryptedTextHtmlObject += QStringLiteral(
        ">some fake characters to prevent self-enclosing html "
        "tag confusing webkit</object>");
#endif

    return encryptedTextHtmlObject;
}

QString ENMLConverterPrivate::decryptedTextHtml(
    const QString & decryptedText, const QString & encryptedText,
    const QString & hint, const QString & cipher, const size_t keyLength,
    const quint64 enDecryptedIndex)
{
    QString result;
    QXmlStreamWriter writer(&result);

    decryptedTextHtml(
        decryptedText, encryptedText, hint, cipher, keyLength, enDecryptedIndex,
        writer);

    writer.writeEndElement();
    return result;
}

QString ENMLConverterPrivate::resourceHtml(
    const qevercloud::Resource & resource, ErrorString & errorDescription)
{
    QNDEBUG("enml", "ENMLConverterPrivate::resourceHtml");

    if (Q_UNLIKELY(!resource.data() || !resource.data()->bodyHash())) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't compose the resource's html "
                       "representation: no data hash is set"));
        QNWARNING("enml", errorDescription << ", resource: " << resource);
        return QString();
    }

    if (Q_UNLIKELY(!resource.mime())) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't compose the resource's html "
                       "representation: no mime type is set"));
        QNWARNING("enml", errorDescription << ", resource: " << resource);
        return QString();
    }

    QXmlStreamAttributes attributes;

    attributes.append(
        QStringLiteral("hash"),
        QString::fromLocal8Bit(resource.data()->bodyHash()->toHex()));

    attributes.append(QStringLiteral("type"), *resource.mime());

    QBuffer htmlBuffer;
    bool res = htmlBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't compose the resource's html representation: "
                       "can't open the buffer to write the html into"));
        errorDescription.details() = htmlBuffer.errorString();
        return QString();
    }

    QXmlStreamWriter writer(&htmlBuffer);

    res = resourceInfoToHtml(attributes, writer, errorDescription);
    if (Q_UNLIKELY(!res)) {
        QNWARNING("enml", errorDescription << ", resource: " << resource);
        return QString();
    }

    writer.writeEndElement();
    return QString::fromUtf8(htmlBuffer.buffer());
}

void ENMLConverterPrivate::escapeString(QString & string, const bool simplify)
{
    QNTRACE("enml", "String before escaping: " << string);

    string.replace(
        QStringLiteral("\'"), QStringLiteral("\\x27"), Qt::CaseInsensitive);

    string.replace(
        QStringLiteral("\""), QStringLiteral("\\x22"), Qt::CaseInsensitive);

    if (simplify) {
        string = string.simplified();
    }
    QNTRACE("enml", "String after escaping: " << string);
}

bool ENMLConverterPrivate::exportNotesToEnex(
    const QList<qevercloud::Note> & notes,
    const QHash<QString, QString> & tagNamesByTagLocalIds,
    const ENMLConverter::EnexExportTags exportTagsOption, QString & enex,
    ErrorString & errorDescription, const QString & version) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::exportNotesToEnex: num notes = "
            << notes.size() << ", num tag names by tag local ids = "
            << tagNamesByTagLocalIds.size() << ", export tags option = "
            << ((exportTagsOption == ENMLConverter::EnexExportTags::Yes) ? "Yes"
                                                                         : "No")
            << ", version = " << version);

    enex.resize(0);

    if (notes.isEmpty()) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note(s) to ENEX: no notes"));
        QNWARNING("enml", errorDescription);
        return false;
    }

    bool foundNoteEligibleForExport = false;
    for (const auto & note: qAsConst(notes)) {
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
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note(s) to ENEX: "
                       "no notes eligible for export"));
        QNWARNING("enml", errorDescription);
        return false;
    }

    QBuffer enexBuffer;
    bool res = enexBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't export note(s) to ENEX: can't "
                       "open the buffer to write the ENEX into"));
        errorDescription.details() = enexBuffer.errorString();
        QNWARNING("enml", errorDescription);
        return false;
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
    DateTimePrintOptions dateTimePrintOptions(0); // NOLINT
#endif

    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();

    enExportAttributes.append(
        QStringLiteral("export-date"),
        printableDateTimeFromTimestamp(
            currentTimestamp, dateTimePrintOptions,
            ENEX_DATE_TIME_FORMAT_STRFTIME));

    enExportAttributes.append(
        QStringLiteral("application"), QCoreApplication::applicationName());

    enExportAttributes.append(QStringLiteral("version"), version);

    writer.writeAttributes(enExportAttributes);

    for (const auto & note: qAsConst(notes)) {
        if (!note.title() && !note.content() &&
            (!note.resources() || note.resources()->isEmpty()) &&
            ((exportTagsOption != ENMLConverter::EnexExportTags::Yes) ||
             note.tagLocalIds().isEmpty()))
        {
            QNINFO(
                "enml",
                "Skipping note without title, content, "
                    << "resources or tags in export to ENML");
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
                ENEX_DATE_TIME_FORMAT_STRFTIME));
            writer.writeEndElement(); // created
        }

        if (note.updated()) {
            writer.writeStartElement(QStringLiteral("updated"));
            writer.writeCharacters(printableDateTimeFromTimestamp(
                *note.updated(), dateTimePrintOptions,
                ENEX_DATE_TIME_FORMAT_STRFTIME));
            writer.writeEndElement(); // updated
        }

        if (exportTagsOption == ENMLConverter::EnexExportTags::Yes)
        {
            const QStringList & tagLocalIds = note.tagLocalIds();
            for (auto tagIt = tagLocalIds.constBegin(),
                      tagEnd = tagLocalIds.constEnd();
                 tagIt != tagEnd; ++tagIt)
            {
                auto tagNameIt = tagNamesByTagLocalIds.find(*tagIt);
                if (Q_UNLIKELY(tagNameIt == tagNamesByTagLocalIds.end())) {
                    enex.clear();
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't export note(s) to ENEX: one of notes "
                                   "has tag local uid for which no tag name "
                                   "was found"));
                    QNWARNING("enml", errorDescription);
                    return false;
                }

                const QString & tagName = tagNameIt.value();
                if (Q_UNLIKELY(tagName.isEmpty())) {
                    QNWARNING(
                        "enml",
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

            if (noteAttributes.latitude() ||
                noteAttributes.longitude() ||
                noteAttributes.altitude() ||
                noteAttributes.author() ||
                noteAttributes.source() ||
                noteAttributes.sourceURL() ||
                noteAttributes.sourceApplication() ||
                noteAttributes.reminderOrder() ||
                noteAttributes.reminderTime() ||
                noteAttributes.reminderDoneTime() ||
                noteAttributes.placeName() ||
                noteAttributes.contentClass() ||
                noteAttributes.subjectDate() ||
                noteAttributes.applicationData())
            {
                writer.writeStartElement(QStringLiteral("note-attributes"));

                if (noteAttributes.subjectDate()) {
                    writer.writeStartElement(QStringLiteral("subject-date"));
                    writer.writeCharacters(printableDateTimeFromTimestamp(
                        *noteAttributes.subjectDate(), dateTimePrintOptions,
                        ENEX_DATE_TIME_FORMAT_STRFTIME));
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

                    writer.writeCharacters(
                        *noteAttributes.sourceApplication());

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
                        ENEX_DATE_TIME_FORMAT_STRFTIME));
                    writer.writeEndElement();
                }

                if (noteAttributes.reminderDoneTime()) {
                    writer.writeStartElement(
                        QStringLiteral("reminder-done-time"));

                    writer.writeCharacters(printableDateTimeFromTimestamp(
                        *noteAttributes.reminderDoneTime(),
                        dateTimePrintOptions, ENEX_DATE_TIME_FORMAT_STRFTIME));

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

            for (const auto & resource: qAsConst(resources)) {
                if (!resource.data() || !resource.data()->body()) {
                    QNINFO(
                        "enml",
                        "Skipping ENEX export of a resource "
                            << "without data body: " << resource);
                    continue;
                }

                if (!resource.mime()) {
                    QNINFO(
                        "enml",
                        "Skipping ENEX export of a resource "
                            << "without mime type: " << resource);
                    continue;
                }

                writer.writeStartElement(QStringLiteral("resource"));

                const QByteArray & resourceData = *resource.data()->body();
                if (resourceData.size() > ENEX_MAX_RESOURCE_DATA_SIZE) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't export note(s) to ENEX: found "
                                   "resource larger than 25 Mb"));

                    QNINFO(
                        "enml", errorDescription << ", resource: " << resource);
                    return false;
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
                    ErrorString error;
                    if (Q_UNLIKELY(!validateRecoIndex(
                            QString::fromUtf8(recognitionData), error)))
                    {
                        errorDescription.setBase(
                            QT_TR_NOOP("Can't export note(s) to ENEX: found "
                                       "invalid resource recognition index at "
                                       "one of notes"));
                        errorDescription.appendBase(error.base());
                        errorDescription.appendBase(error.additionalBases());
                        errorDescription.details() = error.details();
                        QNWARNING("enml", errorDescription);
                        return false;
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
                                    ENEX_DATE_TIME_FORMAT_STRFTIME));

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
                                     qevercloud::toRange(fullMap)) {
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

    res = validateEnex(enex, errorDescription);
    if (!res) {
        ErrorString error(QT_TR_NOOP("Can't export note(s) to ENEX"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        errorDescription = error;
        QNWARNING("enml", errorDescription << ", enex: " << enex);
        return false;
    }

    return true;
}

bool ENMLConverterPrivate::importEnex(
    const QString & enex, QList<qevercloud::Note> & notes,
    QHash<QString, QStringList> & tagNamesByNoteLocalId,
    ErrorString & errorDescription) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::importEnex");

    if (Q_UNLIKELY(enex.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't import ENEX: the input is empty"));
        QNWARNING("enml", errorDescription << ", enex: " << enex);
        return false;
    }

    notes.clear();
    tagNamesByNoteLocalId.clear();

    const QString dateTimeFormat = QStringLiteral(ENEX_DATE_TIME_FORMAT);

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

    QXmlStreamReader reader(enex);

    const auto readNoteTimestamp =
        [&dateTimeFormat, &reader, &insideNote, &currentNote]
           (const std::function<void(
                qevercloud::Note*, qevercloud::Timestamp)> & setter,
            const char * fieldName, ErrorString & errorDescription) -> bool
        {
            if (!insideNote) {
                errorDescription.setBase(
                    QT_TR_NOOP("Detected timestamp tag related to note outside "
                               "of note tag"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const QString timestampString = reader.readElementText(
                QXmlStreamReader::SkipChildElements);

            QNTRACE("enml", fieldName << ": " << timestampString);

            const auto dateTime = QDateTime::fromString(
                timestampString, dateTimeFormat);

            if (Q_UNLIKELY(!dateTime.isValid())) {
                errorDescription.setBase(
                    QT_TR_NOOP("Failed to parse timestamp from string"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const auto timestamp = timestampFromDateTime(dateTime);
            setter(&currentNote, timestamp);
            QNTRACE("enml", "Set " << fieldName << " to " << timestamp);
            return true;
        };

    const auto readDoubleNoteOrResourceAttribute =
        [&insideNote, &insideNoteAttributes, &insideResourceAttributes, &reader,
         &currentNote, &currentResource]
         (const std::function<void(
              qevercloud::ResourceAttributes *, double)> & resourceSetter,
          const std::function<void(
              qevercloud::NoteAttributes *, double)> & noteSetter,
          const char * fieldName, ErrorString & errorDescription) -> bool
        {
            if (!insideNote) {
                errorDescription.setBase(
                    QT_TR_NOOP("Detected tag of double type related to note "
                               "outside of note tag"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const QString valueString = reader.readElementText(
                QXmlStreamReader::SkipChildElements);

            bool conversionResult = false;
            const double num = valueString.toDouble(&conversionResult);
            if (Q_UNLIKELY(!conversionResult)) {
                errorDescription.setBase(
                    QT_TR_NOOP("Failed to parse attribute of double type"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (insideNoteAttributes) {
                auto & attributes = ensureNoteAttributes(currentNote);
                noteSetter(&attributes, num);
                QNTRACE(
                    "enml",
                    "Set note " << fieldName << " attribute to " << num);
                return true;
            }

            if (insideResourceAttributes) {
                auto & attributes = ensureResourceAttributes(currentResource);
                resourceSetter(&attributes, num);
                QNTRACE(
                    "enml",
                    "Set resource " << fieldName << " attribute to " << num);
                return true;
            }

            errorDescription.setBase(
                QT_TR_NOOP("Detected tag of double type outside of note "
                           "attributes or resource attributes"));
            errorDescription.setDetails(fieldName);
            QNWARNING("enml", errorDescription);
            return false;
        };

    const auto readStringNoteAttribute =
        [&insideNote, &insideNoteAttributes, &reader, &currentNote]
        (const std::function<void(
            qevercloud::NoteAttributes *, QString)> & setter,
         const char * fieldName, ErrorString & errorDescription) -> bool
        {
            if (!insideNote || !insideNoteAttributes)
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Detected tag of string type outside of note or "
                               "note attributes"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const QString value = reader.readElementText(
                QXmlStreamReader::SkipChildElements);

            auto & attributes = ensureNoteAttributes(currentNote);
            setter(&attributes, value);
            QNTRACE(
                "enml",
                "Set " << fieldName << " note attribute to " << value);
            return true;
        };

    const auto readTimestampNoteAttribute =
        [&insideNote, &insideNoteAttributes, &reader, &currentNote,
         &dateTimeFormat]
        (const std::function<void(
                qevercloud::NoteAttributes *, qevercloud::Timestamp)> & setter,
         const char * fieldName, ErrorString & errorDescription) -> bool
        {
            if (!insideNote || !insideNoteAttributes)
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Detected tag of timestamp type outside of note "
                               "or note attributes"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const QString timestampString = reader.readElementText(
                QXmlStreamReader::SkipChildElements);

            QNTRACE("enml", fieldName << ": " << timestampString);

            const auto dateTime = QDateTime::fromString(
                timestampString, dateTimeFormat);

            if (Q_UNLIKELY(!dateTime.isValid())) {
                errorDescription.setBase(
                    QT_TR_NOOP("Failed to parse timestamp from string"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const auto timestamp = timestampFromDateTime(dateTime);
            auto & attributes = ensureNoteAttributes(currentNote);
            setter(&attributes, timestamp);
            QNTRACE("enml", "Set " << fieldName << " to " << timestamp);
            return true;
        };

    const auto readStringResourceAttribute =
        [&insideResource, &insideResourceAttributes, &reader, &currentResource]
        (const std::function<void(
                qevercloud::ResourceAttributes *, QString)> & setter,
         const char * fieldName, ErrorString & errorDescription) -> bool
        {
            if (!insideResource || !insideResourceAttributes)
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Detected tag of string type outside of "
                               "resource or resource attributes"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const QString value = reader.readElementText(
                QXmlStreamReader::SkipChildElements);

            auto & attributes = ensureResourceAttributes(currentResource);
            setter(&attributes, value);
            QNTRACE(
                "enml",
                "Set " << fieldName << " resource attribute to " << value);
            return true;
        };

    const auto readStringNoteOrResourceAttribute =
        [&insideNote, &insideNoteAttributes, &insideResource,
         &insideResourceAttributes, &reader, &currentNote, &currentResource]
        (const std::function<void(
            qevercloud::NoteAttributes *, QString)> & noteSetter,
         const std::function<void(
            qevercloud::ResourceAttributes *, QString)> & resourceSetter,
         const char * fieldName, ErrorString & errorDescription)
        {
            if (!insideNote) {
                errorDescription.setBase(
                    QT_TR_NOOP("Detected tag of string type related to note "
                               "outside of note tag"));
                errorDescription.setDetails(fieldName);
                QNWARNING("enml", errorDescription);
                return false;
            }

            const QString value = reader.readElementText(
                QXmlStreamReader::SkipChildElements);

            if (insideNoteAttributes) {
                auto & attributes = ensureNoteAttributes(currentNote);
                noteSetter(&attributes, value);
                QNTRACE(
                    "enml",
                    "Set " << fieldName << " note attribute to " << value);
                return true;
            }

            if (insideResource && insideResourceAttributes) {
                auto & attributes = ensureResourceAttributes(currentResource);
                resourceSetter(&attributes, value);
                QNTRACE(
                    "enml",
                    "Set " << fieldName << " resource attribute to " << value);
                return true;
            }

            errorDescription.setBase(
                QT_TR_NOOP("Detected tag of string type outside of "
                           "note attributes or resource attributes"));
            errorDescription.setDetails(fieldName);
            QNWARNING("enml", errorDescription);
            return false;
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
                    "enml",
                    "export date: " << reader.readElementText(
                        QXmlStreamReader::SkipChildElements));
                continue;
            }

            if (elementName == QStringLiteral("application")) {
                QNTRACE(
                    "enml",
                    "application: " << reader.readElementText(
                        QXmlStreamReader::SkipChildElements));
                continue;
            }

            if (elementName == QStringLiteral("version")) {
                QNTRACE(
                    "enml",
                    "version" << reader.readElementText(
                        QXmlStreamReader::SkipChildElements));
                continue;
            }

            if (elementName == QStringLiteral("note")) {
                QNTRACE("enml", "Starting a new note");
                currentNote = qevercloud::Note{};
                insideNote = true;
                continue;
            }

            if (elementName == QStringLiteral("title")) {
                if (insideNote) {
                    QString title = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    QNTRACE("enml", "Note title: " << title);
                    if (!title.isEmpty()) {
                        currentNote.setTitle(title);
                    }
                    else {
                        currentNote.setTitle(std::nullopt);
                    }

                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected title tag outside of note tag"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("content")) {
                if (insideNote) {
                    QNTRACE("enml", "Start of note content");
                    insideNoteContent = true;
                    currentNoteContent.resize(0);
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected content tag outside of note tag"));

                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("created")) {
                if (!readNoteTimestamp(
                        &qevercloud::Note::setCreated,
                        "creation timestamp", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("updated")) {
                if (!readNoteTimestamp(
                        &qevercloud::Note::setUpdated,
                        "modification timestamp", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("tag")) {
                if (insideNote) {
                    QString tagName = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);
                    const QString & noteLocalId = currentNote.localId();

                    QStringList & tagNames =
                        tagNamesByNoteLocalId[noteLocalId];

                    if (!tagNames.contains(tagName)) {
                        tagNames << tagName;
                        QNTRACE(
                            "enml",
                            "Added tag name " << tagName
                                              << " for note local id "
                                              << noteLocalId);
                    }

                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected tag outside of note"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("note-attributes")) {
                if (insideNote) {
                    QNTRACE("enml", "Start of note attributes");
                    insideNoteAttributes = true;
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected note-attributes tag outside of note"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("latitude")) {
                if (!readDoubleNoteOrResourceAttribute(
                        &qevercloud::ResourceAttributes::setLatitude,
                        &qevercloud::NoteAttributes::setLatitude,
                        "latitude", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("longitude")) {
                if (!readDoubleNoteOrResourceAttribute(
                        &qevercloud::ResourceAttributes::setLongitude,
                        &qevercloud::NoteAttributes::setLongitude,
                        "longitude", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("altitude")) {
                if (!readDoubleNoteOrResourceAttribute(
                        &qevercloud::ResourceAttributes::setAltitude,
                        &qevercloud::NoteAttributes::setAltitude,
                        "altitude", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("author")) {
                if (!readStringNoteAttribute(
                        &qevercloud::NoteAttributes::setAuthor,
                        "author", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("source")) {
                if (!readStringNoteAttribute(
                        &qevercloud::NoteAttributes::setSource,
                        "source", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("source-url")) {
                if (!readStringNoteOrResourceAttribute(
                        &qevercloud::NoteAttributes::setSourceURL,
                        &qevercloud::ResourceAttributes::setSourceURL,
                        "source-url", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("source-application")) {
                if (!readStringNoteAttribute(
                        &qevercloud::NoteAttributes::setSourceApplication,
                        "source-application", errorDescription))
                {
                    return false;
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
                        errorDescription.setBase(
                            QT_TR_NOOP("Failed to parse reminder order"));
                        errorDescription.details() = reminderOrder;
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    auto & attributes = ensureNoteAttributes(currentNote);
                    attributes.setReminderOrder(reminderOrderNum);

                    QNTRACE(
                        "enml",
                        "Set the reminder order to " << reminderOrderNum);
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected reminder-order tag "
                               "outside of note or note attributes"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("reminder-time")) {
                if (!readTimestampNoteAttribute(
                        &qevercloud::NoteAttributes::setReminderTime,
                        "reminder-time", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("subject-date")) {
                if (!readTimestampNoteAttribute(
                        &qevercloud::NoteAttributes::setSubjectDate,
                        "subject-date", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("reminder-done-time")) {
                if (!readTimestampNoteAttribute(
                        &qevercloud::NoteAttributes::setReminderDoneTime,
                        "reminder-done-time", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("place-name")) {
                if (!readStringNoteAttribute(
                        &qevercloud::NoteAttributes::setPlaceName,
                        "place-name", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("content-class")) {
                if (!readStringNoteAttribute(
                        &qevercloud::NoteAttributes::setContentClass,
                        "content-class", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("application-data")) {
                if (insideNote) {
                    auto appDataAttributes = reader.attributes();
                    if (insideNoteAttributes) {
                        if (appDataAttributes.hasAttribute(
                                QStringLiteral("key"))) {
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
                                "enml",
                                "Inserted note application data entry: key = "
                                    << key << ", value = " << value);
                            continue;
                        }

                        errorDescription.setBase(
                            QT_TR_NOOP("Failed to parse application-data "
                                        "tag for note: no key attribute"));
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    if (insideResourceAttributes) {
                        if (appDataAttributes.hasAttribute(
                                QStringLiteral("key"))) {
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
                                "enml",
                                "Inserted resource application data entry: key "
                                    << "= " << key << ", value = " << value);
                            continue;
                        }

                        errorDescription.setBase(
                            QT_TR_NOOP("Failed to parse application-data "
                                        "tag for resource: no key "
                                        "attribute"));
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    errorDescription.setBase(
                        QT_TR_NOOP("Detected application-data tag outside "
                                   "of note attributes or resource "
                                   "attributes"));
                    QNWARNING("enml", errorDescription);
                    return false;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected application-data tag outside of "
                               "note"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("resource")) {
                QNTRACE("enml", "Start of resource tag");
                insideResource = true;

                currentResource = qevercloud::Resource{};

                currentResourceData.resize(0);
                currentResourceRecognitionData.resize(0);
                currentResourceAlternateData.resize(0);

                continue;
            }

            if (elementName == QStringLiteral("data")) {
                if (insideResource) {
                    QNTRACE("enml", "Start of resource data");
                    insideResourceData = true;
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected data tag outside of resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("mime")) {
                if (insideResource) {
                    const QString mime = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    currentResource.setMime(mime);
                    QNTRACE("enml", "Set resource mime to " << mime);
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected mime tag outside of resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("width")) {
                if (insideResource) {
                    const QString width = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    bool conversionResult = false;
                    const qint16 widthNum = width.toShort(&conversionResult);
                    if (Q_UNLIKELY(!conversionResult)) {
                        errorDescription.setBase(
                            QT_TR_NOOP("Failed to parse resource width "
                                       "from string"));
                        errorDescription.details() = width;
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    currentResource.setWidth(widthNum);
                    QNTRACE("enml", "Set resource width to " << widthNum);
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected width tag outside of resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("height")) {
                if (insideResource) {
                    const QString height = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    bool conversionResult = false;
                    const qint16 heightNum = height.toShort(&conversionResult);
                    if (Q_UNLIKELY(!conversionResult)) {
                        errorDescription.setBase(
                            QT_TR_NOOP("Failed to parse resource height from "
                                       "string"));
                        errorDescription.details() = height;
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    currentResource.setHeight(heightNum);
                    QNTRACE("enml", "Set resource height to " << heightNum);
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected height tag outside of resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("recognition")) {
                if (insideResource) {
                    QNTRACE("enml", "Start of resource recognition data");
                    insideResourceRecognitionData = true;
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected recognition tag outside of resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("resource-attributes")) {
                if (insideResource) {
                    QNTRACE("enml", "Start of resource attributes");
                    insideResourceAttributes = true;
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected resource-attributes tag outside of "
                               "resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("timestamp")) {
                if (insideResource && insideResourceAttributes) {
                    const QString timestampString = reader.readElementText(
                        QXmlStreamReader::SkipChildElements);

                    const auto timestampDateTime =
                        QDateTime::fromString(timestampString, dateTimeFormat);

                    if (Q_UNLIKELY(!timestampDateTime.isValid())) {
                        errorDescription.setBase(
                            QT_TR_NOOP("Failed to parse the resource timestamp "
                                       "from string"));
                        errorDescription.details() = timestampString;
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    const qint64 timestamp =
                        timestampFromDateTime(timestampDateTime);

                    auto & resourceAttributes =
                        ensureResourceAttributes(currentResource);

                    resourceAttributes.setTimestamp(timestamp);
                    QNTRACE("enml", "Set resource timestamp to " << timestamp);
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected timestamp tag outside of "
                               "resource or resource attributes"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("camera-make")) {
                if (!readStringResourceAttribute(
                        &qevercloud::ResourceAttributes::setCameraMake,
                        "camera-make", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("reco-type")) {
                if (!readStringResourceAttribute(
                        &qevercloud::ResourceAttributes::setRecoType,
                        "reco-type", errorDescription))
                {
                    return false;
                }

                continue;
            }

            if (elementName == QStringLiteral("file-name")) {
                if (!readStringResourceAttribute(
                        &qevercloud::ResourceAttributes::setFileName,
                        "file-name", errorDescription))
                {
                    return false;
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
                        QNTRACE("enml", "Set attachment to true");
                    }
                    else if (attachment == QStringLiteral("false")) {
                        resourceAttributes.setAttachment(false);
                        QNTRACE("enml", "Set attachment to false");
                    }
                    else {
                        errorDescription.setBase(
                            QT_TR_NOOP("Detected attachment tag with "
                                       "wrong value, must be true or false"));
                        QNWARNING("enml", errorDescription);
                        return false;
                    }

                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected attachment tag outside of "
                               "resource or resource attributes"));
                QNWARNING("enml", errorDescription);
                return false;
            }

            if (elementName == QStringLiteral("alternate-data")) {
                if (insideResource) {
                    QNTRACE("enml", "Start of resource alternate data");
                    insideResourceAlternateData = true;
                    continue;
                }

                errorDescription.setBase(
                    QT_TR_NOOP("Detected alternate-data tag outside of "
                               "resource"));
                QNWARNING("enml", errorDescription);
                return false;
            }
        }

        if (reader.isCharacters()) {
            if (insideNote) {
                if (insideNoteContent && reader.isCDATA()) {
                    currentNoteContent = reader.text().toString();
                    QNTRACE(
                        "enml", "Current note content: " << currentNoteContent);
                    continue;
                }

                if (insideResource) {
                    if (insideResourceData) {
                        currentResourceData = QByteArray::fromBase64(
                            reader.text().toString().toLocal8Bit());
                        QNTRACE("enml", "Read resource data");
                        continue;
                    }

                    if (insideResourceRecognitionData) {
                        currentResourceRecognitionData =
                            reader.text().toString().toUtf8();

                        QNTRACE("enml", "Read resource recognition data");

                        ErrorString error;
                        if (!validateRecoIndex(
                                QString::fromUtf8(currentResourceRecognitionData),
                                error))
                        {
                            errorDescription.setBase(
                                QT_TR_NOOP("Resource recognition index is "
                                           "invalid"));
                            errorDescription.appendBase(error.base());
                            errorDescription.appendBase(
                                error.additionalBases());
                            errorDescription.details() = error.details();
                            QNWARNING("enml", errorDescription);
                            return false;
                        }

                        continue;
                    }

                    if (insideResourceAlternateData) {
                        currentResourceAlternateData = QByteArray::fromBase64(
                            reader.text().toString().toLocal8Bit());
                        QNTRACE("enml", "Read resource alternate data");
                        continue;
                    }
                }
            }
        }

        if (reader.isEndElement()) {
            const QStringRef elementName = reader.name();

            if (elementName == QStringLiteral("content")) {
                QNTRACE("enml", "End of note content: " << currentNoteContent);
                currentNote.setContent(currentNoteContent);
                insideNoteContent = false;
                continue;
            }

            if (elementName == QStringLiteral("note-attributes")) {
                QNTRACE("enml", "End of note attributes");
                insideNoteAttributes = false;
                continue;
            }

            if (elementName == QStringLiteral("resource-attributes")) {
                QNTRACE("enml", "End of resource attributes");
                insideResourceAttributes = false;
                continue;
            }

            if (elementName == QStringLiteral("data")) {
                QNTRACE("enml", "End of resource data");

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
                QNTRACE("enml", "End of resource recognition data");

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
                QNTRACE("enml", "End of resource alternate data");

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
                QNTRACE("enml", "End of resource");

                if (Q_UNLIKELY(!currentResource.data() ||
                               !currentResource.data()->body()))
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Parsed resource without a data body"));
                    QNWARNING(
                        "enml",
                        errorDescription << ", resource: " << currentResource);
                    return false;
                }

                if (Q_UNLIKELY(!currentResource.data() ||
                               !currentResource.data()->bodyHash()))
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Internal error: data hash is not computed "
                                   "for the resource"));
                    QNWARNING(
                        "enml",
                        errorDescription << ", resource: " << currentResource);
                    return false;
                }

                if (Q_UNLIKELY(!currentResource.data() ||
                               !currentResource.data()->size()))
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Internal error: data size is not computed "
                                   "for the resource"));
                    QNWARNING(
                        "enml",
                        errorDescription << ", resource: " << currentResource);
                    return false;
                }

                if (Q_UNLIKELY(!currentResource.mime())) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Parsed resource without a mime type"));
                    QNWARNING(
                        "enml",
                        errorDescription << ", resource: " << currentResource);
                    return false;
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

    QNDEBUG("enml", "ENEX import end: num notes = " << notes.size());
    return true;
}

bool ENMLConverterPrivate::isForbiddenXhtmlTag(const QString & tagName) const
{
    const auto it = m_forbiddenXhtmlTags.find(tagName);
    return it != m_forbiddenXhtmlTags.end();
}

bool ENMLConverterPrivate::isForbiddenXhtmlAttribute(
    const QString & attributeName) const
{
    const auto it = m_forbiddenXhtmlAttributes.find(attributeName);
    if (it != m_forbiddenXhtmlAttributes.end()) {
        return true;
    }

    return attributeName.startsWith(QStringLiteral("on"));
}

bool ENMLConverterPrivate::isEvernoteSpecificXhtmlTag(
    const QString & tagName) const
{
    const auto it = m_evernoteSpecificXhtmlTags.find(tagName);
    return it != m_evernoteSpecificXhtmlTags.end();
}

bool ENMLConverterPrivate::isAllowedXhtmlTag(const QString & tagName) const
{
    const auto it = m_allowedXhtmlTags.find(tagName);
    return it != m_allowedXhtmlTags.end();
}

void ENMLConverterPrivate::toDoTagsToHtml(
    const QXmlStreamReader & reader, const quint64 enToDoIndex,
    QXmlStreamWriter & writer) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::toDoTagsToHtml");

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
        "enml",
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

bool ENMLConverterPrivate::encryptedTextToHtml(
    const QXmlStreamAttributes & enCryptAttributes,
    const QStringRef & encryptedTextCharacters, const quint64 enCryptIndex,
    const quint64 enDecryptedIndex, QXmlStreamWriter & writer,
    DecryptedTextManager & decryptedTextManager,
    bool & convertedToEnCryptNode) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::encryptedTextToHtml: "
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

    QString decryptedText;
    bool rememberForSession = false;
    const bool foundDecryptedText =
        decryptedTextManager.findDecryptedTextByEncryptedText(
            encryptedTextCharacters.toString(), decryptedText,
            rememberForSession);

    if (foundDecryptedText) {
        QNTRACE(
            "enml",
            "Found encrypted text which has already been "
                << "decrypted and cached; encrypted text = "
                << encryptedTextCharacters);

        size_t keyLength = 0;
        if (!length.isEmpty()) {
            bool conversionResult = false;
            keyLength = static_cast<size_t>(length.toUInt(&conversionResult));
            if (!conversionResult) {
                QNWARNING(
                    "enml",
                    "Can't convert encryption key length "
                        << "from string to unsigned integer: " << length);
                keyLength = 0;
            }
        }

        decryptedTextHtml(
            decryptedText, encryptedTextCharacters.toString(), hint, cipher,
            keyLength, enDecryptedIndex, writer);

        convertedToEnCryptNode = false;
        return true;
    }

    convertedToEnCryptNode = true;

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    writer.writeStartElement(QStringLiteral("object"));
    writer.writeAttribute(
        QStringLiteral("type"),
        QStringLiteral("application/vnd.quentier.encrypt"));
#else
    writer.writeStartElement(QStringLiteral("img"));
    writer.writeAttribute(QStringLiteral("src"), QString());
#endif

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
    QNTRACE("enml", "Wrote element corresponding to en-crypt ENML tag");

    writer.writeAttribute(
        QStringLiteral("en-crypt-id"), QString::number(enCryptIndex));

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    // Required for webkit, otherwise it can't seem to handle
    // self-enclosing object tag properly
    writer.writeCharacters(
        QStringLiteral("some fake characters to prevent "
                       "self-enclosing html tag confusing webkit"));
#endif
    return true;
}

bool ENMLConverterPrivate::resourceInfoToHtml(
    const QXmlStreamAttributes & attributes, QXmlStreamWriter & writer,
    ErrorString & errorDescription)
{
    QNDEBUG("enml", "ENMLConverterPrivate::resourceInfoToHtml");

    if (!attributes.hasAttribute(QStringLiteral("hash"))) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected incorrect en-media tag missing hash "
                       "attribute"));
        QNDEBUG("enml", errorDescription);
        return false;
    }

    if (!attributes.hasAttribute(QStringLiteral("type"))) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected incorrect en-media tag missing type "
                       "attribute"));
        QNDEBUG("enml", errorDescription);
        return false;
    }

    const QStringRef mimeType = attributes.value(QStringLiteral("type"));
    bool inlineImage = false;
    if (mimeType.startsWith(QStringLiteral("image"), Qt::CaseInsensitive)) {
        // TODO: consider some proper high-level interface for making it
        // possible to customize ENML <--> HTML conversion
        inlineImage = true;
    }

#ifndef QUENTIER_USE_QT_WEB_ENGINE
    writer.writeStartElement(
        inlineImage ? QStringLiteral("img") : QStringLiteral("object"));
#else
    writer.writeStartElement(QStringLiteral("img"));
#endif

    // NOTE: ENMLConverterPrivate can't set src attribute for img tag as it
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

#ifndef QUENTIER_USE_QT_WEB_ENGINE
        writer.writeAttribute(
            QStringLiteral("type"),
            QStringLiteral("application/vnd.quentier.resource"));

        const int numAttributes = attributes.size();
        for (int i = 0; i < numAttributes; ++i) {
            const QXmlStreamAttribute & attribute = attributes[i];
            const QString qualifiedName = attribute.qualifiedName().toString();
            if (qualifiedName == QStringLiteral("en-tag")) {
                continue;
            }

            const QString value = attribute.value().toString();

            if (qualifiedName == QStringLiteral("type")) {
                writer.writeAttribute(
                    QStringLiteral("resource-mime-type"), value);
            }
            else {
                writer.writeAttribute(qualifiedName, value);
            }
        }

        // Required for webkit, otherwise it can't seem to handle self-enclosing
        // object tag properly
        writer.writeCharacters(
            QStringLiteral("some fake characters to prevent "
                           "self-enclosing html tag confusing webkit"));
#else
        writer.writeAttributes(attributes);
        writer.writeAttribute(
            QStringLiteral("src"),
            QStringLiteral("qrc:/generic_resource_icons/png/attachment.png"));
#endif
    }

    return true;
}

bool ENMLConverterPrivate::decryptedTextToEnml(
    QXmlStreamReader & reader, DecryptedTextManager & decryptedTextManager,
    QXmlStreamWriter & writer, ErrorString & errorDescription) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::decryptedTextToEnml");

    const QXmlStreamAttributes attributes = reader.attributes();
    if (!attributes.hasAttribute(QStringLiteral("encrypted_text"))) {
        errorDescription.setBase(
            QT_TR_NOOP("Missing encrypted text attribute "
                       "within en-decrypted div tag"));
        QNDEBUG("enml", errorDescription);
        return false;
    }

    QString encryptedText =
        attributes.value(QStringLiteral("encrypted_text")).toString();

    QString storedDecryptedText;
    bool rememberForSession = false;
    if (!decryptedTextManager.findDecryptedTextByEncryptedText(
            encryptedText, storedDecryptedText, rememberForSession))
    {
        errorDescription.setBase(
            QT_TR_NOOP("Can't find the decrypted text by its encrypted text"));
        QNWARNING("enml", errorDescription);
        return false;
    }

    QString actualDecryptedText;
    QXmlStreamWriter decryptedTextWriter(&actualDecryptedText);

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
        errorDescription.setBase(QT_TR_NOOP("Text decryption failed"));
        errorDescription.details() = reader.errorString();
        QNWARNING(
            "enml",
            "Couldn't read the nested contents of en-decrypted "
                << "div, reader has error: " << errorDescription);
        return false;
    }

    if (storedDecryptedText != actualDecryptedText) {
        QNTRACE("enml", "Found modified decrypted text, need to re-encrypt");

        QString actualEncryptedText;
        if (decryptedTextManager.modifyDecryptedText(
                encryptedText, actualDecryptedText, actualEncryptedText))
        {
            QNTRACE(
                "enml",
                "Re-evaluated the modified decrypted text's "
                    << "encrypted text; was: " << encryptedText
                    << "; new: " << actualEncryptedText);
            encryptedText = actualEncryptedText;
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

    QNTRACE("enml", "Wrote en-crypt ENML tag from en-decrypted p tag");
    return true;
}

void ENMLConverterPrivate::decryptedTextHtml(
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
                QNTRACE("enml", "Skipping the start of temporarily added div");
                continue;
            }

            writer.writeStartElement(decryptedTextReader.name().toString());
            writer.writeAttributes(attributes);

            foundFormattedText = true;

            QNTRACE(
                "enml",
                "Wrote start element from decrypted text: "
                    << decryptedTextReader.name());
        }

        if (decryptedTextReader.isCharacters()) {
            writer.writeCharacters(decryptedTextReader.text().toString());

            foundFormattedText = true;

            QNTRACE(
                "enml",
                "Wrote characters from decrypted text: "
                    << decryptedTextReader.text());
        }

        if (decryptedTextReader.isEndElement()) {
            const auto attributes = decryptedTextReader.attributes();
            if (attributes.hasAttribute(QStringLiteral("id")) &&
                (attributes.value(QStringLiteral("id")) ==
                 QStringLiteral("decrypted_text_html_to_enml_temporary")))
            {
                QNTRACE("enml", "Skipping the end of temporarily added div");
                continue;
            }

            writer.writeEndElement();

            QNTRACE(
                "enml",
                "Wrote end element from decrypted text: "
                    << decryptedTextReader.name());
        }
    }

    if (decryptedTextReader.hasError()) {
        QNWARNING(
            "enml",
            "Decrypted text reader has error: "
                << decryptedTextReader.errorString());
    }

    if (!foundFormattedText) {
        writer.writeCharacters(decryptedText);
        QNTRACE("enml", "Wrote unformatted decrypted text: " << decryptedText);
    }
}

bool ENMLConverterPrivate::validateEnex(
    const QString & enex, ErrorString & errorDescription) const
{
    QNDEBUG("enml", "ENMLConverterPrivate::validateEnex");

    return validateAgainstDtd(
        enex, QStringLiteral(":/evernote-export3.dtd"), errorDescription);
}

bool ENMLConverterPrivate::validateRecoIndex(
    const QString & recoIndex, ErrorString & errorDescription) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::validateRecoIndex: reco index = " << recoIndex);

    return validateAgainstDtd(
        recoIndex, QStringLiteral(":/recoIndex.dtd"), errorDescription);
}

bool ENMLConverterPrivate::validateAgainstDtd(
    const QString & input, const QString & dtdFilePath,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::validateAgainstDtd: dtd file " << dtdFilePath);

    errorDescription.clear();
    const QByteArray inputBuffer = input.toUtf8();

    const std::unique_ptr<xmlDoc, void(*)(xmlDocPtr)> pDoc(
        xmlParseMemory(inputBuffer.constData(), inputBuffer.size()),
        xmlFreeDoc);

    if (!pDoc) {
        errorDescription.setBase(
            QT_TR_NOOP("Could not validate document, can't "
                       "parse the input into xml doc"));
        QNWARNING("enml", errorDescription << ": input = " << input);
        return false;
    }

    QFile dtdFile{dtdFilePath};
    if (!dtdFile.open(QIODevice::ReadOnly)) {
        errorDescription.setBase(
            QT_TR_NOOP("Could not validate document, can't "
                       "open the resource file with DTD"));
        QNWARNING(
            "enml",
            errorDescription << ": input = " << input
                             << ", DTD file path = " << dtdFilePath);
        return false;
    }

    const QByteArray dtdRawData = dtdFile.readAll();

    std::unique_ptr<
        xmlParserInputBuffer,
        void(*)(xmlParserInputBufferPtr)> pBuf(
            xmlParserInputBufferCreateMem(
                dtdRawData.constData(), dtdRawData.size(),
                XML_CHAR_ENCODING_NONE),
            xmlFreeParserInputBuffer);

    if (!pBuf) {
        errorDescription.setBase(
            QT_TR_NOOP("Could not validate document, can't allocate the input "
                       "buffer for dtd validation"));
        QNWARNING("enml", errorDescription);
        return false;
    }

    // NOTE: xmlIOParseDTD should have "consumed" the input buffer so one
    // should not attempt to free it manually
    const std::unique_ptr<xmlDtd, void(*)(xmlDtdPtr)> pDtd(
        xmlIOParseDTD(nullptr, pBuf.release(), XML_CHAR_ENCODING_NONE),
        xmlFreeDtd);

    if (!pDtd) {
        errorDescription.setBase(
            QT_TR_NOOP("Could not validate document, failed to parse DTD"));
        QNWARNING("enml", errorDescription);
        return false;
    }

    const std::unique_ptr<xmlParserCtxt, void(*)(xmlParserCtxtPtr)> pContext(
        xmlNewParserCtxt(),
        xmlFreeParserCtxt);

    if (!pContext) {
        errorDescription.setBase(
            QT_TR_NOOP("Could not validate document, can't allocate parser "
                       "context"));
        QNWARNING("enml", errorDescription);
        return false;
    }

    QString errorString;
    pContext->vctxt.userData = &errorString;
    pContext->vctxt.error = (xmlValidityErrorFunc)xmlValidationErrorFunc;

    if (!xmlValidateDtd(&pContext->vctxt, pDoc.get(), pDtd.get())) {
        errorDescription.setBase(QT_TR_NOOP("Document is invalid"));

        if (!errorString.isEmpty()) {
            errorDescription.details() = QStringLiteral(": ");
            errorDescription.details() += errorString;
        }
    }

    return true;
}

SkipElementOption ENMLConverterPrivate::skipElementOption(
    const QString & elementName, const QXmlStreamAttributes & attributes,
    const QList<SkipHtmlElementRule> & skipRules) const
{
    QNDEBUG(
        "enml",
        "ENMLConverterPrivate::skipElementOption: element name = "
            << elementName << ", attributes = " << attributes);

    if (skipRules.isEmpty()) {
        return SkipElementOption::DontSkip;
    }

    SkipElementOptions flags;
    flags |= SkipElementOption::DontSkip;

#define CHECK_IF_SHOULD_SKIP()                                                 \
    if (shouldSkip) {                                                          \
        if (rule.m_includeElementContents) {                                   \
            flags |= SkipElementOption::SkipButPreserveContents;               \
        }                                                                      \
        else {                                                                 \
            return SkipElementOption::SkipWithContents;                        \
        }                                                                      \
    }

    const int numAttributes = attributes.size();
    const int numSkipRules = skipRules.size();
    for (int i = 0; i < numSkipRules; ++i) {
        const SkipHtmlElementRule & rule = skipRules[i];

        if (!rule.m_elementNameToSkip.isEmpty()) {
            bool shouldSkip = false;

            switch (rule.m_elementNameComparisonRule) {
            case SkipHtmlElementRule::ComparisonRule::Equals:
            {
                if (rule.m_elementNameCaseSensitivity == Qt::CaseSensitive) {
                    shouldSkip = (elementName == rule.m_elementNameToSkip);
                }
                else {
                    shouldSkip =
                        (elementName.toUpper() ==
                         rule.m_elementNameToSkip.toUpper());
                }
                break;
            }
            case SkipHtmlElementRule::ComparisonRule::StartsWith:
                shouldSkip = elementName.startsWith(
                    rule.m_elementNameToSkip,
                    rule.m_elementNameCaseSensitivity);
                break;
            case SkipHtmlElementRule::ComparisonRule::EndsWith:
                shouldSkip = elementName.endsWith(
                    rule.m_elementNameToSkip,
                    rule.m_elementNameCaseSensitivity);
                break;
            case SkipHtmlElementRule::ComparisonRule::Contains:
                shouldSkip = elementName.contains(
                    rule.m_elementNameToSkip,
                    rule.m_elementNameCaseSensitivity);
                break;
            default:
                QNWARNING(
                    "enml",
                    "Detected unhandled "
                        << "SkipHtmlElementRule::ComparisonRule");
                break;
            }

            CHECK_IF_SHOULD_SKIP()
        }

        if (!rule.m_attributeNameToSkip.isEmpty()) {
            for (int j = 0; j < numAttributes; ++j) {
                bool shouldSkip = false;

                const QXmlStreamAttribute & attribute = attributes[j];

                switch (rule.m_attributeNameComparisonRule) {
                case SkipHtmlElementRule::ComparisonRule::Equals:
                {
                    if (rule.m_attributeNameCaseSensitivity ==
                        Qt::CaseSensitive) {
                        shouldSkip =
                            (attribute.name() == rule.m_attributeNameToSkip);
                    }
                    else {
                        shouldSkip =
                            (attribute.name().toString().toUpper() ==
                             rule.m_attributeNameToSkip.toUpper());
                    }
                    break;
                }
                case SkipHtmlElementRule::ComparisonRule::StartsWith:
                    shouldSkip = attribute.name().startsWith(
                        rule.m_attributeNameToSkip,
                        rule.m_attributeNameCaseSensitivity);
                    break;
                case SkipHtmlElementRule::ComparisonRule::EndsWith:
                    shouldSkip = attribute.name().endsWith(
                        rule.m_attributeNameToSkip,
                        rule.m_attributeNameCaseSensitivity);
                    break;
                case SkipHtmlElementRule::ComparisonRule::Contains:
                    shouldSkip = attribute.name().contains(
                        rule.m_attributeNameToSkip,
                        rule.m_attributeNameCaseSensitivity);
                    break;
                default:
                    QNWARNING(
                        "enml",
                        "Detected unhandled "
                            << "SkipHtmlElementRule::ComparisonRule");
                    break;
                }

                CHECK_IF_SHOULD_SKIP()
            }
        }

        if (!rule.m_attributeValueToSkip.isEmpty()) {
            for (int j = 0; j < numAttributes; ++j) {
                bool shouldSkip = false;

                const QXmlStreamAttribute & attribute = attributes[j];

                switch (rule.m_attributeValueComparisonRule) {
                case SkipHtmlElementRule::ComparisonRule::Equals:
                {
                    if (rule.m_attributeValueCaseSensitivity ==
                        Qt::CaseSensitive) {
                        shouldSkip =
                            (attribute.value() == rule.m_attributeValueToSkip);
                    }
                    else {
                        shouldSkip =
                            (attribute.value().toString().toUpper() ==
                             rule.m_attributeValueToSkip.toUpper());
                    }
                    break;
                }
                case SkipHtmlElementRule::ComparisonRule::StartsWith:
                    shouldSkip = attribute.value().startsWith(
                        rule.m_attributeValueToSkip,
                        rule.m_attributeValueCaseSensitivity);
                    break;
                case SkipHtmlElementRule::ComparisonRule::EndsWith:
                    shouldSkip = attribute.value().endsWith(
                        rule.m_attributeValueToSkip,
                        rule.m_attributeValueCaseSensitivity);
                    break;
                case SkipHtmlElementRule::ComparisonRule::Contains:
                    shouldSkip = attribute.value().contains(
                        rule.m_attributeValueToSkip,
                        rule.m_attributeValueCaseSensitivity);
                    break;
                default:
                    QNWARNING(
                        "enml",
                        "Detected unhandled "
                            << "SkipHtmlElementRule::ComparisonRule");
                    break;
                }

                CHECK_IF_SHOULD_SKIP()
            }
        }
    }

    if (flags & SkipElementOption::SkipButPreserveContents) {
        return SkipElementOption::SkipButPreserveContents;
    }

    return SkipElementOption::DontSkip;
}

ENMLConverterPrivate::ProcessElementStatus
ENMLConverterPrivate::processElementForHtmlToNoteContentConversion(
    const QList<SkipHtmlElementRule> & skipRules, ConversionState & state,
    DecryptedTextManager & decryptedTextManager, QXmlStreamReader & reader,
    QXmlStreamWriter & writer, ErrorString & errorDescription) const
{
    if (state.m_skippedElementNestingCounter) {
        QNTRACE(
            "enml",
            "Skipping everything inside element skipped "
                << "together with its contents by the rules");
        ++state.m_skippedElementNestingCounter;
        return ProcessElementStatus::ProcessedFully;
    }

    state.m_lastElementName = reader.name().toString();
    if (state.m_lastElementName == QStringLiteral("form")) {
        QNTRACE("enml", "Skipping <form> tag");
        return ProcessElementStatus::ProcessedFully;
    }

    if (state.m_lastElementName == QStringLiteral("html")) {
        QNTRACE("enml", "Skipping <html> tag");
        return ProcessElementStatus::ProcessedFully;
    }

    if (state.m_lastElementName == QStringLiteral("title")) {
        QNTRACE("enml", "Skipping <title> tag");
        return ProcessElementStatus::ProcessedFully;
    }

    if (state.m_lastElementName == QStringLiteral("body")) {
        state.m_lastElementName = QStringLiteral("en-note");
        QNTRACE(
            "enml",
            "Found \"body\" HTML tag, will replace it "
                << "with \"en-note\" tag for written ENML");
    }

    auto tagIt = m_forbiddenXhtmlTags.find(state.m_lastElementName);
    if ((tagIt != m_forbiddenXhtmlTags.constEnd()) &&
        (state.m_lastElementName != QStringLiteral("object")))
    {
        QNTRACE(
            "enml",
            "Skipping forbidden XHTML tag: " << state.m_lastElementName);
        return ProcessElementStatus::ProcessedFully;
    }

    tagIt = m_allowedXhtmlTags.find(state.m_lastElementName);
    if (tagIt == m_allowedXhtmlTags.end()) {
        tagIt = m_evernoteSpecificXhtmlTags.find(state.m_lastElementName);
        if (tagIt == m_evernoteSpecificXhtmlTags.end()) {
            QNTRACE(
                "enml",
                "Haven't found tag "
                    << state.m_lastElementName
                    << " within the list of allowed XHTML tags or within "
                    << "Evernote-specific tags, skipping it");
            return ProcessElementStatus::ProcessedFully;
        }
    }

    state.m_lastElementAttributes = reader.attributes();

    auto shouldSkip = skipElementOption(
        state.m_lastElementName, state.m_lastElementAttributes, skipRules);

    if (shouldSkip != SkipElementOption::DontSkip) {
        QNTRACE(
            "enml",
            "Skipping element " << state.m_lastElementName
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
        const QString enTag =
            state.m_lastElementAttributes.value(QStringLiteral("en-tag"))
                .toString();

        if (enTag == QStringLiteral("en-decrypted")) {
            QNTRACE(
                "enml",
                "Found decrypted text area, need to "
                    << "convert it back to en-crypt form");

            if (!decryptedTextToEnml(
                    reader, decryptedTextManager, writer, errorDescription))
            {
                return ProcessElementStatus::Error;
            }

            return ProcessElementStatus::ProcessedFully;
        }

        if (enTag == QStringLiteral("en-todo")) {
            if (!state.m_lastElementAttributes.hasAttribute(
                    QStringLiteral("src"))) {
                QNWARNING("enml", "Found en-todo tag without src attribute");
                return ProcessElementStatus::ProcessedFully;
            }

            const QStringRef srcValue =
                state.m_lastElementAttributes.value(QStringLiteral("src"));

            if (srcValue.contains(
                    QStringLiteral("qrc:/checkbox_icons/checkbox_no.png"))) {
                writer.writeStartElement(QStringLiteral("en-todo"));
                ++state.m_writeElementCounter;
                return ProcessElementStatus::ProcessedFully;
            }

            if (srcValue.contains(
                    QStringLiteral("qrc:/checkbox_icons/checkbox_yes.png"))) {
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
                errorDescription.setBase(
                    QT_TR_NOOP("Found en-crypt tag without "
                               "encrypted_text attribute"));
                QNDEBUG("enml", errorDescription);
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
            QNTRACE("enml", "Started writing en-crypt tag");
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
                        const bool contains = m_allowedEnMediaAttributes.contains(
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
                             attributeQualifiedName)) {
                    // img
                    state.m_enMediaAttributes.append(
                        attributeQualifiedName, attributeValue);
                }
            }

            writer.writeAttributes(state.m_enMediaAttributes);
            state.m_enMediaAttributes.clear();
            QNTRACE("enml", "Wrote en-media element from img element in HTML");

            return ProcessElementStatus::ProcessedFully;
        }
    }

    // Erasing forbidden attributes
    for (auto it = state.m_lastElementAttributes.begin(); // NOLINT
         it != state.m_lastElementAttributes.end();)
    {
        const QStringRef attributeName = it->name();
        if (isForbiddenXhtmlAttribute(attributeName.toString())) {
            QNTRACE("enml", "Erasing forbidden attribute " << attributeName);
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
        "enml",
        "Wrote element: name = " << state.m_lastElementName
                                 << " and its attributes");

    return ProcessElementStatus::ProcessedPartially;
}

} // namespace quentier

////////////////////////////////////////////////////////////////////////////////

QTextStream & operator<<(
    QTextStream & strm, const QXmlStreamAttributes & obj)
{
    const int numAttributes = obj.size();

    strm << "QXmlStreamAttributes(" << numAttributes << "): {\n";

    for (int i = 0; i < numAttributes; ++i) {
        const auto & attribute = obj[i];
        strm << "  [" << i << "]: name = " << attribute.name().toString()
             << ", value = " << attribute.value().toString() << "\n";
    }

    strm << "}\n";
    return strm;
}

QTextStream & operator<<(
    QTextStream & strm,
    const QList<quentier::ENMLConverter::SkipHtmlElementRule> & obj)
{
    strm << "SkipHtmlElementRules";

    if (obj.isEmpty()) {
        strm << ": <empty>";
        return strm;
    }

    const int numRules = obj.size();

    strm << "(" << numRules << "): {\n";

    using SkipHtmlElementRule = quentier::ENMLConverter::SkipHtmlElementRule;

    for (int i = 0; i < numRules; ++i) {
        const SkipHtmlElementRule & rule = obj[i];
        strm << " [" << i << "]: " << rule << "\n";
    }

    strm << "}\n";
    return strm;
}

QTextStream & operator<<(
    QTextStream & strm, const quentier::SkipElementOption option)
{
    switch (option) {
    case quentier::SkipElementOption::SkipWithContents:
        strm << "Skip with contents";
        break;
    case quentier::SkipElementOption::SkipButPreserveContents:
        strm << "Skip but preserve contents";
        break;
    case quentier::SkipElementOption::DontSkip:
        strm << "Do not skip";
        break;
    default:
        strm << "Unknown (" << static_cast<qint64>(option) << ")";
        break;
    }

    return strm;
}
