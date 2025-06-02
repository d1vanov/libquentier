/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <QEventLoop>

class QDebug;
class QTextStream;

namespace quentier::utility {

class QUENTIER_EXPORT EventLoopWithExitStatus : public QEventLoop
{
    Q_OBJECT
public:
    explicit EventLoopWithExitStatus(QObject * parent = nullptr);

    enum class ExitStatus
    {
        Success,
        Failure,
        Timeout
    };

    friend QDebug & operator<<(QDebug & dbg, ExitStatus status);
    friend QTextStream & operator<<(QTextStream & strm, ExitStatus status);

    [[nodiscard]] ExitStatus exitStatus() const;
    [[nodiscard]] const ErrorString & errorDescription() const;

public Q_SLOTS:
    void exitAsSuccess();
    void exitAsFailure();
    void exitAsFailureWithError(QString errorDescription);
    void exitAsFailureWithErrorString(ErrorString errorDescription);
    void exitAsTimeout();

private:
    ExitStatus m_exitStatus;
    ErrorString m_errorDescription;
};

} // namespace quentier::utility
