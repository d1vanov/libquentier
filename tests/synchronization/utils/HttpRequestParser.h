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

#pragma once

#include "HttpRequestData.h"

#include <QObject>

QT_BEGIN_NAMESPACE

class QTcpSocket;

QT_END_NAMESPACE

namespace quentier::synchronization::tests::utils {

// Simplistic parser of HTTP request data from QTcpSocket
class HttpRequestParser : public QObject
{
    Q_OBJECT
public:
    HttpRequestParser(QTcpSocket & socket, QObject * parent = nullptr);

    [[nodiscard]] bool status() const noexcept;
    [[nodiscard]] const QByteArray & data() const noexcept;
    [[nodiscard]] HttpRequestData requestData() const;

Q_SIGNALS:
    void finished();
    void failed();

private Q_SLOTS:
    void onSocketReadyRead();

private:
    void tryParseData();

private:
    bool m_status = false;
    HttpRequestData m_requestData;
    QByteArray m_data;
};

} // namespace quentier::synchronization::tests::utils
