/*
 * Copyright 2018-2024 Dmitry Ivanov
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

#include <QObject>
#include <QString>

class QDebug;
class QTextStream;

namespace quentier {

class FileCopierPrivate;

class QUENTIER_EXPORT FileCopier : public QObject
{
    Q_OBJECT
public:
    explicit FileCopier(QObject * parent = nullptr);

    enum class State
    {
        Idle = 0,
        Copying,
        Cancelling
    };

    friend QDebug & operator<<(QDebug & dbg, State state);
    friend QTextStream & operator<<(QTextStream & strm, State state);

    [[nodiscard]] State state() const;

    [[nodiscard]] QString sourceFilePath() const;
    [[nodiscard]] QString destinationFilePath() const;

    [[nodiscard]] double currentProgress() const;

Q_SIGNALS:
    void progressUpdate(double progress);
    void finished(QString sourcePath, QString destPath);
    void cancelled(QString sourcePath, QString destPath);
    void notifyError(ErrorString error);

public Q_SLOTS:
    void copyFile(QString sourcePath, QString destPath);
    void cancel();

private:
    Q_DISABLE_COPY(FileCopier)

private:
    FileCopierPrivate * d_ptr;
    Q_DECLARE_PRIVATE(FileCopier)
};

} // namespace quentier
