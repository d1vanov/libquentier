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

#include <quentier/enml/HTMLCleaner.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/SuppressWarnings.h>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

SAVE_WARNINGS

// clang-format off
GCC_SUPPRESS_WARNING(-Wignored-qualifiers)
CLANG_SUPPRESS_WARNING(-Wignored-qualifiers)
// clang-format on

#include <tidy.h>
#include <tidybuffio.h>
#include <tidyenum.h>
#include <tidyplatform.h>

RESTORE_WARNINGS

#include <errno.h>
#include <stdio.h>

namespace quentier {

class HTMLCleaner::Impl
{
public:
    Impl() : m_tidyOutput(), m_tidyErrorBuffer(), m_tidyDoc(tidyCreate()) {}

    ~Impl()
    {
        tidyBufFree(&m_tidyOutput);
        tidyBufFree(&m_tidyErrorBuffer);
        tidyRelease(m_tidyDoc);
    }

    bool convertHtml(
        const QString & html, const TidyOptionId outputFormat, QString & output,
        QString & errorDescription);

    TidyBuffer m_tidyOutput;
    TidyBuffer m_tidyErrorBuffer;
    TidyDoc m_tidyDoc;
};

HTMLCleaner::HTMLCleaner() : m_impl(new HTMLCleaner::Impl) {}

HTMLCleaner::~HTMLCleaner()
{
    delete m_impl;
}

bool HTMLCleaner::htmlToXml(
    const QString & html, QString & output, QString & errorDescription)
{
    QNDEBUG("enml:html_cleaner", "HTMLCleaner::htmlToXml");
    QNTRACE("enml:html_cleaner", "html = " << html);

    return m_impl->convertHtml(html, TidyXmlOut, output, errorDescription);
}

bool HTMLCleaner::htmlToXhtml(
    const QString & html, QString & output, QString & errorDescription)
{
    QNDEBUG("enml:html_cleaner", "HTMLCleaner::htmlToXhtml");
    QNTRACE("enml:html_cleaner", "html = " << html);

    return m_impl->convertHtml(html, TidyXhtmlOut, output, errorDescription);
}

bool HTMLCleaner::cleanupHtml(QString & html, QString & errorDescription)
{
    QNDEBUG("enml:html_cleaner", "HTMLCleaner::cleanupHtml");
    QNTRACE("enml:html_cleaner", "html = " << html);

    return m_impl->convertHtml(html, TidyHtmlOut, html, errorDescription);
}

bool HTMLCleaner::Impl::convertHtml(
    const QString & html, const TidyOptionId outputFormat, QString & output,
    QString & errorDescription)
{
    // Clear buffers from the previous run, if any
    tidyBufClear(&m_tidyOutput);
    tidyBufClear(&m_tidyErrorBuffer);
    tidyRelease(m_tidyDoc);
    m_tidyDoc = tidyCreate();

    int rc = -1;
    Bool ok = tidyOptSetBool(m_tidyDoc, outputFormat, yes);

    QNTRACE(
        "enml:html_cleaner",
        "tidyOptSetBool: output format: ok = " << (ok ? "true" : "false"));

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyPreserveEntities, yes);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: preserve entities = yes: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyMergeDivs, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetInt: merge divs = no: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyMergeSpans, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetInt: merge spans = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyMergeEmphasis, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: merge emphasis = no: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyDropEmptyElems, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: drop empty elemens = no: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyIndentContent, TidyNoState);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetInt: indent content = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyIndentAttributes, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: indent attributes = no: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyIndentCdata, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: indent CDATA = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyVertSpace, TidyNoState);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: vert space = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyMark, no);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: tidy mark = no: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyBodyOnly, TidyYesState);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: tidy body only = yes: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyWrapLen, 0);
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetInt: wrap len = 0: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetValue(m_tidyDoc, TidyDoctype, "omit");
        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool: doctype = omit: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        rc = tidySetErrorBuffer(m_tidyDoc, &m_tidyErrorBuffer);
        QNTRACE("enml:html_cleaner", "tidySetErrorBuffer: rc = " << rc);
    }

    if (rc >= 0) {
        rc = tidyParseString(m_tidyDoc, html.toUtf8().constData());
        QNTRACE("enml:html_cleaner", "tidyParseString: rc = " << rc);
    }

    if (rc >= 0) {
        rc = tidyCleanAndRepair(m_tidyDoc);
        QNTRACE("enml:html_cleaner", "tidyCleanAndRepair: rc = " << rc);
    }

    if (rc >= 0) {
        rc = tidyRunDiagnostics(m_tidyDoc);
        QNTRACE("enml:html_cleaner", "tidyRunDiagnostics: rc = " << rc);
    }

    if (rc > 1) {
        int forceOutputRc = tidyOptSetBool(m_tidyDoc, TidyForceOutput, yes);

        QNTRACE(
            "enml:html_cleaner",
            "tidyOptSetBool (force output): rc = " << forceOutputRc);

        if (!forceOutputRc) {
            rc = -1;
        }
    }

    if (rc >= 0) {
        rc = tidySaveBuffer(m_tidyDoc, &m_tidyOutput);
        QNTRACE("enml:html_cleaner", "tidySaveBuffer: rc = " << rc);
    }

    if (rc < 0) {
        QString errorPrefix = QStringLiteral("tidy-html5 error");

        QByteArray errorBody = QByteArray(
            reinterpret_cast<const char *>(m_tidyErrorBuffer.bp),
            static_cast<int>(m_tidyErrorBuffer.size));

        QNINFO("enml:html_cleaner", errorPrefix << ": " << errorBody);
        errorDescription = errorPrefix;
        errorDescription += QStringLiteral(": ");
        errorDescription +=
            QString::fromUtf8(errorBody.constData(), errorBody.size());

        return false;
    }

    if (rc > 0) {
        QNTRACE(
            "enml:html_cleaner",
            "Tidy diagnostics: " << QByteArray(
                reinterpret_cast<const char *>(m_tidyErrorBuffer.bp),
                static_cast<int>(m_tidyErrorBuffer.size)));
    }

    output.resize(0);

    output.append(QString::fromUtf8(QByteArray(
        reinterpret_cast<const char *>(m_tidyOutput.bp),
        static_cast<int>(m_tidyOutput.size))));

    QString nbspEntityDeclaration =
        QStringLiteral("<!DOCTYPE doctypeName [<!ENTITY nbsp \"&#160;\">]>");

    bool insertedNbspEntityDeclaration = false;

    if (output.startsWith(QStringLiteral("<?xml version"))) {
        int firstEnclosingBracketIndex = output.indexOf(QChar::fromLatin1('>'));
        if (firstEnclosingBracketIndex > 0) {
            output.insert(
                firstEnclosingBracketIndex + 1, nbspEntityDeclaration);

            insertedNbspEntityDeclaration = true;
        }
    }

    if (!insertedNbspEntityDeclaration) {
        // Prepend the nbsp entity declaration
        output.prepend(nbspEntityDeclaration);
    }

    // Now need to clean up after tidy: it inserts spurious \n characters
    // in some places
    QXmlStreamReader reader(output);

    QBuffer fixedUpOutputBuffer;
    bool res = fixedUpOutputBuffer.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!res)) {
        errorDescription = QStringLiteral(
            "Failed to open the buffer to write the fixed up output: ");
        errorDescription += fixedUpOutputBuffer.errorString();
        return false;
    }

    QXmlStreamWriter writer(&fixedUpOutputBuffer);
    writer.setAutoFormatting(false);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();

    bool justProcessedEndElement = false;

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartDocument()) {
            continue;
        }

        if (reader.isDTD()) {
            writer.writeDTD(reader.text().toString());
            continue;
        }

        if (reader.isEndDocument()) {
            break;
        }

        if (reader.isStartElement()) {
            writer.writeStartElement(reader.name().toString());
            writer.writeAttributes(reader.attributes());
            continue;
        }

        if (reader.isEndElement()) {
            writer.writeEndElement();
            justProcessedEndElement = true;
            continue;
        }

        if (reader.isCharacters()) {
            if (reader.isCDATA()) {
                writer.writeCDATA(reader.text().toString());
                justProcessedEndElement = false;
                continue;
            }

            QString text = reader.text().toString();

            if (justProcessedEndElement) {
                // Need to remove the extra newline tidy added
                int firstNewlineIndex = text.indexOf(QStringLiteral("\n"));
                if (firstNewlineIndex >= 0) {
                    text.remove(firstNewlineIndex, 1);
                }

                justProcessedEndElement = false;
            }

            writer.writeCharacters(text);
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        errorDescription = QStringLiteral(
            "Error while trying to clean up the html after tidy-html5: ");

        errorDescription += reader.errorString();

        QNWARNING(
            "enml:html_cleaner",
            errorDescription << "; original HTML: " << html
                             << "\nHtml converted to XML by tidy: " << output);
        return false;
    }

    output = QString::fromUtf8(fixedUpOutputBuffer.buffer());
    return true;
}

} // namespace quentier
