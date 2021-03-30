/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_UTILITY_FILE_SYSTEM_WATCHER_PRIVATE_H
#define LIB_QUENTIER_UTILITY_FILE_SYSTEM_WATCHER_PRIVATE_H

#include <quentier/utility/FileSystemWatcher.h>
#include <quentier/utility/SuppressWarnings.h>

#include <QFileSystemWatcher>
#include <QSet>

SAVE_WARNINGS

MSVC_SUPPRESS_WARNING(4834)

#include <boost/bimap.hpp>

RESTORE_WARNINGS

namespace quentier {

class Q_DECL_HIDDEN FileSystemWatcherPrivate final : public QObject
{
    Q_OBJECT
public:
    explicit FileSystemWatcherPrivate(
        FileSystemWatcher & parent, const int removalTimeoutMSec);

    explicit FileSystemWatcherPrivate(
        FileSystemWatcher & parent, const QStringList & paths,
        const int removalTimeoutMSec);

    virtual ~FileSystemWatcherPrivate();

    void addPath(const QString & path);
    void addPaths(const QStringList & paths);

    QStringList directories() const;
    QStringList files() const;

    void removePath(const QString & path);
    void removePaths(const QStringList & paths);

Q_SIGNALS:
    void directoryChanged(const QString & path);
    void directoryRemoved(const QString & path);

    void fileChanged(const QString & path);
    void fileRemoved(const QString & path);

private Q_SLOTS:
    void onFileChanged(const QString & path);
    void onDirectoryChanged(const QString & path);

private:
    void createConnections();
    void processFileRemoval(const QString & path);
    void processDirectoryRemoval(const QString & path);

private:
    virtual void timerEvent(QTimerEvent * pEvent) override;

private:
    Q_DISABLE_COPY(FileSystemWatcherPrivate)

private:
    using PathWithTimerId = boost::bimap<QString, int>;

private:
    FileSystemWatcher & m_parent;
    QFileSystemWatcher m_watcher;
    int m_removalTimeoutMSec;

    QSet<QString> m_watchedFiles;
    QSet<QString> m_watchedDirectories;

    PathWithTimerId m_justRemovedFilePathsWithPostRemovalTimerIds;
    PathWithTimerId m_justRemovedDirectoryPathsWithPostRemovalTimerIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_FILE_SYSTEM_WATCHER_PRIVATE_H
