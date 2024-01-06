/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "HttpRequestParser.h"

#include <quentier/logging/QuentierLogger.h>

#include <QTcpSocket>

namespace quentier::synchronization::tests::utils {

HttpRequestParser::HttpRequestParser(QTcpSocket & socket, QObject * parent) :
    QObject(parent)
{
    QObject::connect(
        &socket, &QIODevice::readyRead, this,
        &HttpRequestParser::onSocketReadyRead, Qt::QueuedConnection);
}

bool HttpRequestParser::status() const noexcept
{
    return m_status;
}

const QByteArray & HttpRequestParser::data() const noexcept
{
    return m_requestData.body;
}

HttpRequestData HttpRequestParser::requestData() const
{
    return m_requestData;
}

void HttpRequestParser::onSocketReadyRead()
{
    auto * pSocket = qobject_cast<QTcpSocket *>(sender());
    Q_ASSERT(pSocket);

    m_data.append(pSocket->read(pSocket->bytesAvailable()));
    tryParseData();
}

void HttpRequestParser::tryParseData()
{
    // Data read from socket should be a HTTP request with headers and body.
    // Maybe incomplete in which case we postpone the attempt to parse
    // the request.

    // The first line of a HTTP request should be the request line:
    // method<space>request-uri<space>http-version<crlf>
    auto methodEndIndex = m_data.indexOf(" ");
    if (methodEndIndex < 0) {
        // No first space symbol, probably not all data has arrived yet
        return;
    }

    const QByteArray method{m_data.constData(), methodEndIndex};
    if (method == QByteArray::fromRawData("GET", 3)) {
        m_requestData.method = HttpRequestData::Method::GET;
    }
    else if (method == QByteArray::fromRawData("POST", 4)) {
        m_requestData.method = HttpRequestData::Method::POST;
    }

    auto resourceUriEndIndex = m_data.indexOf(" ", methodEndIndex + 1);
    if (resourceUriEndIndex < 0) {
        // No resource URI end index, probably not all data has arrived yet
        return;
    }

    m_requestData.uri = QByteArray{
        m_data.constData() + methodEndIndex + 1,
        resourceUriEndIndex - methodEndIndex - 1};

    if (m_requestData.method == HttpRequestData::Method::GET) {
        // GET requests don't include Content-Length header so we need to look
        // for two consequent carriage returns with nothing in between them
        // to determine that the whole HTTP request has been received.
        const auto requestEndIndex =
            m_data.indexOf("\r\n\r\n", resourceUriEndIndex + 1);
        if (requestEndIndex < 0) {
            return;
        }

        m_status = true;
        Q_EMIT finished();
        return;
    }

    // For POST request will parse headers, find Content-Length one to figure
    // out the size of request body
    auto contentLengthIndex =
        m_data.indexOf("Content-Length:", resourceUriEndIndex + 1);
    if (contentLengthIndex < 0) {
        // No Content-Length header, probably not all data has arrived yet
        return;
    }

    auto contentLengthLineEndIndex = m_data.indexOf("\r\n", contentLengthIndex);
    if (contentLengthLineEndIndex < 0) {
        // No line end after Content-Length header, probably not all data
        // has arrived yet
        return;
    }

    auto contentLengthLen = contentLengthLineEndIndex - contentLengthIndex - 15;
    QString contentLengthStr = QString::fromUtf8(
        m_data.mid(contentLengthIndex + 15, contentLengthLen));

    bool conversionResult = false;
    int contentLength = contentLengthStr.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        QNWARNING(
            "quentier::synchronization::tests::utils::http",
            "Failed to convert content length header value to int: "
                << contentLengthStr);
        m_status = false;
        Q_EMIT failed();
        return;
    }

    // Now see whether whole body data is present
    auto headersEndIndex = m_data.indexOf("\r\n\r\n", contentLengthLineEndIndex);
    if (headersEndIndex < 0) {
        // No empty line after http headers, probably not all data has
        // arrived yet
        return;
    }

    QByteArray body = m_data;
    body.remove(0, headersEndIndex + 4);
    if (body.size() < contentLength) {
        // Not all data has arrived yet
        return;
    }

    m_requestData.body = body;
    m_status = true;
    Q_EMIT finished();
}

} // namespace quentier::synchronization::tests::utils
