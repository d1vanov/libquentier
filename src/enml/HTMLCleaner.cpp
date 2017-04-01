/*
 * Copyright 2016 Dmitry Ivanov
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
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <tidy.h>
#include <tidyenum.h>
#include <tidybuffio.h>
#include <tidyplatform.h>
#include <stdio.h>
#include <errno.h>

namespace quentier {

class HTMLCleaner::Impl
{
public:
    Impl() :
        m_tidyOutput(),
        m_tidyErrorBuffer(),
        m_tidyDoc(tidyCreate())
    {}

    ~Impl()
    {
        tidyBufFree(&m_tidyOutput);
        tidyBufFree(&m_tidyErrorBuffer);
        tidyRelease(m_tidyDoc);
    }

    bool convertHtml(const QString & html, const TidyOptionId outputFormat,
                     QString & output, QString & errorDescription);

    TidyBuffer  m_tidyOutput;
    TidyBuffer  m_tidyErrorBuffer;
    TidyDoc     m_tidyDoc;
};

HTMLCleaner::HTMLCleaner() :
    m_impl(new HTMLCleaner::Impl)
{}

HTMLCleaner::~HTMLCleaner()
{
    delete m_impl;
}

bool HTMLCleaner::htmlToXml(const QString & html, QString & output, QString & errorDescription)
{
    QNDEBUG(QStringLiteral("HTMLCleaner::htmlToXml"));
    QNTRACE(QStringLiteral("html = ") << html);

    return m_impl->convertHtml(html, TidyXmlOut, output, errorDescription);
}

bool HTMLCleaner::htmlToXhtml(const QString & html, QString & output, QString & errorDescription)
{
    QNDEBUG(QStringLiteral("HTMLCleaner::htmlToXhtml"));
    QNTRACE(QStringLiteral("html = ") << html);

    return m_impl->convertHtml(html, TidyXhtmlOut, output, errorDescription);
}

bool HTMLCleaner::cleanupHtml(QString & html, QString & errorDescription)
{
    QNDEBUG(QStringLiteral("HTMLCleaner::cleanupHtml"));
    QNTRACE(QStringLiteral("html = ") << html);

    return m_impl->convertHtml(html, TidyHtmlOut, html, errorDescription);
}

bool HTMLCleaner::Impl::convertHtml(const QString & html, const TidyOptionId outputFormat, QString & output,
                                    QString & errorDescription)
{
    // Clear buffers from the previous run, if any
    tidyBufClear(&m_tidyOutput);
    tidyBufClear(&m_tidyErrorBuffer);
    tidyRelease(m_tidyDoc);
    m_tidyDoc = tidyCreate();

    int rc = -1;
    Bool ok = tidyOptSetBool(m_tidyDoc, outputFormat, yes);
    QNTRACE(QStringLiteral("tidyOptSetBool: output format: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyPreserveEntities, yes);
        QNTRACE(QStringLiteral("tidyOptSetBool: preserve entities = yes: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyMergeDivs, no);
        QNTRACE(QStringLiteral("tidyOptSetInt: merge divs = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetInt(m_tidyDoc, TidyMergeSpans, no);
        QNTRACE(QStringLiteral("tidyOptSetInt: merge spans = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyMergeEmphasis, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: merge emphasis = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyDropEmptyElems, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: drop empty elemens = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyIndentContent, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: indent content = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyIndentAttributes, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: indent attributes = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        ok = tidyOptSetBool(m_tidyDoc, TidyIndentCdata, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: indent CDATA = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        rc = tidyOptSetBool(m_tidyDoc, TidyVertSpace, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: vert space = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        rc = tidyOptSetBool(m_tidyDoc, TidyMark, no);
        QNTRACE(QStringLiteral("tidyOptSetBool: tidy mark = no: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        rc = tidyOptSetBool(m_tidyDoc, TidyBodyOnly, yes);
        QNTRACE(QStringLiteral("tidyOptSetBool: tidy body only = yes: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        rc = tidyOptSetInt(m_tidyDoc, TidyWrapLen, 0);
        QNTRACE(QStringLiteral("tidyOptSetInt: wrap len = 0: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        rc = tidyOptSetValue(m_tidyDoc, TidyDoctype, "omit");
        QNTRACE(QStringLiteral("tidyOptSetBool: doctype = omit: ok = ") << (ok ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (ok) {
        rc = tidySetErrorBuffer(m_tidyDoc, &m_tidyErrorBuffer);
        QNTRACE(QStringLiteral("tidySetErrorBuffer: rc = ") << rc);
    }

    if (rc >= 0) {
        rc = tidyParseString(m_tidyDoc, html.toLocal8Bit().constData());
        QNTRACE(QStringLiteral("tidyParseString: rc = ") << rc);
    }

    if (rc >= 0) {
        rc = tidyCleanAndRepair(m_tidyDoc);
        QNTRACE(QStringLiteral("tidyCleanAndRepair: rc = ") << rc);
    }

    if (rc >= 0) {
        rc = tidyRunDiagnostics(m_tidyDoc);
        QNTRACE(QStringLiteral("tidyRunDiagnostics: rc = ") << rc);
    }

    if (rc > 1) {
        int forceOutputRc = tidyOptSetBool(m_tidyDoc, TidyForceOutput, yes);
        QNTRACE(QStringLiteral("tidyOptSetBool (force output): rc = ") << forceOutputRc);
        if (!forceOutputRc) {
            rc = -1;
        }
    }

    if (rc >= 0) {
        rc = tidySaveBuffer(m_tidyDoc, &m_tidyOutput);
        QNTRACE(QStringLiteral("tidySaveBuffer: rc = ") << rc);
    }

    if (rc < 0)
    {
        QString errorPrefix = QStringLiteral("tidy-html5 error");
        QByteArray errorBody = QByteArray(reinterpret_cast<const char*>(m_tidyErrorBuffer.bp),
                                          static_cast<int>(m_tidyErrorBuffer.size));
        QNINFO(errorPrefix << QStringLiteral(": ") << errorBody);
        errorDescription = errorPrefix;
        errorDescription += QStringLiteral(": ");
        errorDescription += QString::fromUtf8(errorBody.constData(), errorBody.size());
        return false;
    }

    if (rc > 0) {
        QNTRACE(QStringLiteral("Tidy diagnostics: ") << QByteArray(reinterpret_cast<const char*>(m_tidyErrorBuffer.bp),
                                                                   static_cast<int>(m_tidyErrorBuffer.size)));
    }

    output.resize(0);
    output.append(QByteArray(reinterpret_cast<const char*>(m_tidyOutput.bp),
                             static_cast<int>(m_tidyOutput.size)));

    // Prepend the nbsp entity declaration
    output.prepend(QStringLiteral("<!DOCTYPE doctypeName [<!ENTITY nbsp \"&#160;\">]>"));

    // Now need to clean up after tidy: it inserts spurious \n characters in some places
    QXmlStreamReader reader(output);

    QString fixedUpOutput;
    QXmlStreamWriter writer(&fixedUpOutput);
    writer.setAutoFormatting(true);
    writer.setCodec("UTF-8");
    writer.writeStartDocument();

    bool justProcessedEndElement = false;

    while(!reader.atEnd())
    {
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

        if (reader.isCharacters())
        {
            if (reader.isCDATA()) {
                writer.writeCDATA(reader.text().toString());
                justProcessedEndElement = false;
                continue;
            }

            QString text = reader.text().toString();

            if (justProcessedEndElement)
            {
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
        errorDescription = QStringLiteral("Error while trying to clean up the html after tidy-html5: ");
        errorDescription += reader.errorString();
        QNWARNING(errorDescription << QStringLiteral("; original HTML: ") << html
                  << QStringLiteral("\nHtml converted to XML by tidy: ") << output);
        return false;
    }

    output = fixedUpOutput;
    return true;
}

} // namespace quentier
