/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_FILE_COPIER_H
#define LIB_QUENTIER_UTILITY_FILE_COPIER_H

#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <QObject>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QDebug)
QT_FORWARD_DECLARE_CLASS(QTextStream)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(FileCopierPrivate)

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

    friend QDebug & operator<<(QDebug & dbg, const State state);
    friend QTextStream & operator<<(QTextStream & strm, const State state);

    State state() const;

    QString sourceFilePath() const;
    QString destinationFilePath() const;

    double currentProgress() const;

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

#endif // LIB_QUENTIER_UTILITY_FILE_COPIER_H
