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

#include "HttpUtils.h"

#include "HttpRequestParser.h"

#include <QEventLoop>
#include <QTcpSocket>

namespace quentier::synchronization::tests::utils {

HttpRequestData readRequestDataFromSocket(QTcpSocket & socket)
{
    if (!socket.waitForConnected()) {
        return {};
    }

    QEventLoop loop;
    HttpRequestParser extractor(socket);

    QObject::connect(
        &extractor, &HttpRequestParser::finished, &loop, &QEventLoop::quit);

    QObject::connect(
        &extractor, &HttpRequestParser::failed, &loop, &QEventLoop::quit);

    loop.exec();

    if (!extractor.status()) {
        return {};
    }

    return extractor.requestData();
}

QByteArray readRequestBodyFromSocket(QTcpSocket & socket)
{
    return readRequestDataFromSocket(socket).body;
}

bool writeBufferToSocket(const QByteArray & data, QTcpSocket & socket)
{
    qint64 remaining = data.size();
    const char * pData = data.constData();
    while (socket.isOpen() && remaining > 0) {
        // If the output buffer has become too large then wait until it has been
        // sent.
        if (socket.bytesToWrite() > 16384) {
            socket.waitForBytesWritten(-1);
        }

        qint64 written = socket.write(pData, remaining);
        if (written < 0) {
            return false;
        }

        pData += written;
        remaining -= written;
    }
    return true;
}

} // namespace quentier::synchronization::tests::utils
