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

#ifndef LIB_QUENTIER_UTILITY_FILE_COPIER_PRIVATE_H
#define LIB_QUENTIER_UTILITY_FILE_COPIER_PRIVATE_H

#include <quentier/types/ErrorString.h>

#include <QObject>
#include <QString>

namespace quentier {

class Q_DECL_HIDDEN FileCopierPrivate final : public QObject
{
    Q_OBJECT
public:
    explicit FileCopierPrivate(QObject * parent = nullptr);

    bool isIdle() const
    {
        return m_idle;
    }

    bool isCancelled() const
    {
        return m_cancelled;
    }

    const QString & sourceFilePath() const
    {
        return m_sourcePath;
    }

    const QString & destinationFilePath() const
    {
        return m_destPath;
    }

    double currentProgress() const
    {
        return m_currentProgress;
    }

    void copyFile(const QString & sourcePath, const QString & destPath);
    void cancel();

Q_SIGNALS:
    void progressUpdate(double progress);
    void finished(QString sourcePath, QString destPath);
    void cancelled(QString sourcePath, QString destPath);
    void notifyError(ErrorString error);

private:
    void clear();

private:
    Q_DISABLE_COPY(FileCopierPrivate)

private:
    QString m_sourcePath;
    QString m_destPath;

    bool m_idle = true;
    bool m_cancelled = false;
    double m_currentProgress = 0.0;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_FILE_COPIER_PRIVATE_H
