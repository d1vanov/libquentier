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

#include <quentier/utility/Linkage.h>

#include <QObject>
#include <QStringList>

namespace quentier::utility {

class QUENTIER_EXPORT FileSystemWatcher : public QObject
{
    Q_OBJECT
public:
    explicit FileSystemWatcher(
        int removalTimeoutMSec = 500,
        QObject * parent = nullptr);

    explicit FileSystemWatcher(
        const QStringList & paths,
        int removalTimeoutMSec = 500,
        QObject * parent = nullptr);

    ~FileSystemWatcher() override;

    void addPath(const QString & path);
    void addPaths(const QStringList & paths);

    [[nodiscard]] QStringList directories() const;
    [[nodiscard]] QStringList files() const;

    void removePath(const QString & path);
    void removePaths(const QStringList & paths);

Q_SIGNALS:
    void directoryChanged(const QString & path);
    void directoryRemoved(const QString & path);

    void fileChanged(const QString & path);
    void fileRemoved(const QString & path);

private:
    Q_DISABLE_COPY(FileSystemWatcher)

private:
    class FileSystemWatcherPrivate;

    FileSystemWatcherPrivate * d_ptr;
    Q_DECLARE_PRIVATE(FileSystemWatcher)
};

} // namespace quentier::utility

// TODO: remove after migration to namespaced class in Quentier
namespace quentier {

using utility::FileSystemWatcher;

} // namespace quentier
