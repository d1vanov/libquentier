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

#include <QObject>
#include <QString>

namespace quentier {

class FileCopierPrivate final : public QObject
{
    Q_OBJECT
public:
    explicit FileCopierPrivate(QObject * parent = nullptr);

    [[nodiscard]] bool isIdle() const noexcept
    {
        return m_idle;
    }

    [[nodiscard]] bool isCancelled() const noexcept
    {
        return m_cancelled;
    }

    [[nodiscard]] const QString & sourceFilePath() const noexcept
    {
        return m_sourcePath;
    }

    [[nodiscard]] const QString & destinationFilePath() const noexcept
    {
        return m_destPath;
    }

    [[nodiscard]] double currentProgress() const noexcept
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
