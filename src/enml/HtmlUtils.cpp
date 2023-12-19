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

#include "HtmlUtils.h"

#include <memory>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>
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

#include <cerrno>
#include <cstdio>

namespace quentier::enml {

namespace {

////////////////////////////////////////////////////////////////////////////////

class TidyBufferWrapper
{
public:
    TidyBufferWrapper() noexcept
    {
        tidyBufInit(&m_buffer);
    }

    ~TidyBufferWrapper() noexcept
    {
        tidyBufFree(&m_buffer);
    }

    TidyBuffer & buf() noexcept
    {
        return m_buffer;
    }

private:
    TidyBuffer m_buffer;
};

////////////////////////////////////////////////////////////////////////////////

class TidyDocWrapper
{
public:
    TidyDocWrapper() noexcept : m_doc{tidyCreate()} {}

    ~TidyDocWrapper() noexcept
    {
        tidyRelease(m_doc);
    }

    TidyDoc & doc() noexcept
    {
        return m_doc;
    }

private:
    TidyDoc m_doc;
};

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] Result<QString, ErrorString> convertHtml(
    const QString & html, const TidyOptionId outputFormat)
{
    TidyDocWrapper docWrapper;
    TidyBufferWrapper outBufWrapper;
    TidyBufferWrapper errBufWrapper;

    auto & doc = docWrapper.doc();
    auto & outBuf = outBufWrapper.buf();
    auto & errBuf = errBufWrapper.buf();

    int rc = -1;
    Bool ok = tidyOptSetBool(doc, outputFormat, yes);

    QNTRACE(
        "enml::HtmlConverter",
        "tidyOptSetBool: output format: ok = " << (ok ? "true" : "false"));

    if (ok) {
        ok = tidyOptSetBool(doc, TidyPreserveEntities, yes);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: preserve entities = yes: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(doc, TidyMergeDivs, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetInt: merge divs = no: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(doc, TidyMergeSpans, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetInt: merge spans = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(doc, TidyMergeEmphasis, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: merge emphasis = no: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(doc, TidyDropEmptyElems, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: drop empty elemens = no: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(doc, TidyIndentContent, TidyNoState);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetInt: indent content = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(doc, TidyIndentAttributes, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: indent attributes = no: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(doc, TidyIndentCdata, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: indent CDATA = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(doc, TidyVertSpace, TidyNoState);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: vert space = no: ok = "
                << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetBool(doc, TidyMark, no);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: tidy mark = no: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(doc, TidyBodyOnly, TidyYesState);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: tidy body only = yes: "
                << "ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetInt(doc, TidyWrapLen, 0);
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetInt: wrap len = 0: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        ok = tidyOptSetValue(doc, TidyDoctype, "omit");
        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool: doctype = omit: ok = " << (ok ? "true" : "false"));
    }

    if (ok) {
        rc = tidySetErrorBuffer(doc, &errBuf);
        QNTRACE("enml::HtmlConverter", "tidySetErrorBuffer: rc = " << rc);
    }

    if (rc >= 0) {
        rc = tidyParseString(doc, html.toUtf8().constData());
        QNTRACE("enml::HtmlConverter", "tidyParseString: rc = " << rc);
    }

    if (rc >= 0) {
        rc = tidyCleanAndRepair(doc);
        QNTRACE("enml::HtmlConverter", "tidyCleanAndRepair: rc = " << rc);
    }

    if (rc >= 0) {
        rc = tidyRunDiagnostics(doc);
        QNTRACE("enml::HtmlConverter", "tidyRunDiagnostics: rc = " << rc);
    }

    if (rc > 1) {
        int forceOutputRc = tidyOptSetBool(doc, TidyForceOutput, yes);

        QNTRACE(
            "enml::HtmlConverter",
            "tidyOptSetBool (force output): rc = " << forceOutputRc);

        if (!forceOutputRc) {
            rc = -1;
        }
    }

    if (rc >= 0) {
        rc = tidySaveBuffer(doc, &outBuf);
        QNTRACE("enml::HtmlConverter", "tidySaveBuffer: rc = " << rc);
    }

    if (rc < 0) {
        const QString errorPrefix = QStringLiteral("tidy-html5 error");

        const QByteArray errorBody = QByteArray(
            reinterpret_cast<const char *>(errBuf.bp),
            static_cast<int>(errBuf.size));

        QNINFO("enml::HtmlConverter", errorPrefix << ": " << errorBody);
        ErrorString errorDescription{errorPrefix};
        errorDescription.details() = QStringLiteral(": ");
        errorDescription.details() +=
            QString::fromUtf8(errorBody.constData(), errorBody.size());

        return Result<QString, ErrorString>{errorDescription};
    }

    if (rc > 0) {
        QNTRACE(
            "enml::HtmlConverter",
            "Tidy diagnostics: " << QByteArray(
                reinterpret_cast<const char *>(errBuf.bp),
                static_cast<int>(errBuf.size)));
    }

    QString output;
    output.append(QString::fromUtf8(QByteArray(
        reinterpret_cast<const char *>(outBuf.bp),
        static_cast<int>(outBuf.size))));

    const QString nbspEntityDeclaration =
        QStringLiteral("<!DOCTYPE doctypeName [<!ENTITY nbsp \"&#160;\">]>");

    bool insertedNbspEntityDeclaration = false;

    if (output.startsWith(QStringLiteral("<?xml version"))) {
        const int firstEnclosingBracketIndex =
            output.indexOf(QChar::fromLatin1('>'));

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
    QXmlStreamReader reader{output};

    QBuffer fixedUpOutputBuffer;
    if (!fixedUpOutputBuffer.open(QIODevice::WriteOnly)) {
        ErrorString errorDescription{
            "Failed to open the buffer to write the fixed up output"};
        errorDescription.details() = fixedUpOutputBuffer.errorString();
        return Result<QString, ErrorString>{errorDescription};
    }

    QXmlStreamWriter writer{&fixedUpOutputBuffer};
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
                const int firstNewlineIndex =
                    text.indexOf(QStringLiteral("\n"));
                if (firstNewlineIndex >= 0) {
                    text.remove(firstNewlineIndex, 1);
                }

                justProcessedEndElement = false;
            }

            writer.writeCharacters(text);
        }
    }

    if (Q_UNLIKELY(reader.hasError())) {
        ErrorString errorDescription{
            "Error while trying to clean up the html after tidy-html5"};

        errorDescription.details() = reader.errorString();

        QNWARNING(
            "enml::HtmlConverter",
            errorDescription << "; original HTML: " << html
                             << "\nHtml converted to XML by tidy: " << output);
        return Result<QString, ErrorString>{errorDescription};
    }

    return Result<QString, ErrorString>{
        QString::fromUtf8(fixedUpOutputBuffer.buffer())};
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

Result<QString, ErrorString> convertHtmlToXml(const QString & html)
{
    return convertHtml(html, TidyXmlOut);
}

Result<QString, ErrorString> convertHtmlToXhtml(const QString & html)
{
    return convertHtml(html, TidyXhtmlOut);
}

Result<QString, ErrorString> cleanupHtml(const QString & html)
{
    return convertHtml(html, TidyHtmlOut);
}

} // namespace quentier::enml
