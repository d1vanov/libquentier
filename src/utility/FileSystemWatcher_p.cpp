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

#include "FileSystemWatcher_p.h"

#include <quentier/logging/QuentierLogger.h>

#include <QFileInfo>
#include <QTimerEvent>

namespace quentier {

FileSystemWatcherPrivate::FileSystemWatcherPrivate(
    FileSystemWatcher & parent, const int removalTimeoutMSec) :
    QObject(&parent),
    m_parent(parent), m_removalTimeoutMSec(removalTimeoutMSec)
{
    createConnections();
}

FileSystemWatcherPrivate::FileSystemWatcherPrivate(
    FileSystemWatcher & parent, const QStringList & paths,
    const int removalTimeoutMSec) :
    QObject(&parent),
    m_parent(parent), m_watcher(paths), m_removalTimeoutMSec(removalTimeoutMSec)
{
    createConnections();
}

FileSystemWatcherPrivate::~FileSystemWatcherPrivate() {}

void FileSystemWatcherPrivate::addPath(const QString & path)
{
    QFileInfo info(path);
    if (info.isFile()) {
        Q_UNUSED(m_watchedFiles.insert(path));
    }
    else if (info.isDir()) {
        Q_UNUSED(m_watchedDirectories.insert(path));
    }

    m_watcher.addPath(path);
}

void FileSystemWatcherPrivate::addPaths(const QStringList & paths)
{
    const int numPaths = paths.size();
    for (int i = 0; i < numPaths; ++i) {
        addPath(paths[i]);
    }
}

QStringList FileSystemWatcherPrivate::directories() const
{
    return m_watcher.directories();
}

QStringList FileSystemWatcherPrivate::files() const
{
    return m_watcher.files();
}

void FileSystemWatcherPrivate::removePath(const QString & path)
{
    auto fileIt = m_watchedFiles.find(path);
    if (fileIt != m_watchedFiles.end()) {
        Q_UNUSED(m_watchedFiles.erase(fileIt));
    }
    else {
        auto dirIt = m_watchedDirectories.find(path);
        if (dirIt != m_watchedDirectories.end()) {
            Q_UNUSED(m_watchedDirectories.erase(dirIt));
        }
    }

    m_watcher.removePath(path);
}

void FileSystemWatcherPrivate::removePaths(const QStringList & paths)
{
    const int numPaths = paths.size();
    for (int i = 0; i < numPaths; ++i) {
        removePath(paths[i]);
    }
}

void FileSystemWatcherPrivate::onFileChanged(const QString & path)
{
    auto fileIt = m_watchedFiles.find(path);
    if (Q_UNLIKELY(fileIt == m_watchedFiles.end())) {
        QNWARNING(
            "utility:fs_watcher",
            "Received file changed event for file "
                << "not listed as watched");
        return;
    }

    QFileInfo info(path);
    if (!info.isFile()) {
        processFileRemoval(path);
    }
    else {
        m_watcher.addPath(path);
        Q_EMIT fileChanged(path);
    }
}

void FileSystemWatcherPrivate::onDirectoryChanged(const QString & path)
{
    QNTRACE(
        "utility:fs_watcher",
        "FileSystemWatcherPrivate::onDirectoryChanged: " << path);

    auto dirIt = m_watchedDirectories.find(path);
    if (dirIt != m_watchedDirectories.end()) {
        QFileInfo info(path);
        if (!info.isDir()) {
            processDirectoryRemoval(path);
        }
        else {
            m_watcher.addPath(path);
            Q_EMIT directoryChanged(path);
        }
    }
}

void FileSystemWatcherPrivate::createConnections()
{
    QObject::connect(
        this, &FileSystemWatcherPrivate::fileChanged, &m_parent,
        &FileSystemWatcher::fileChanged);

    QObject::connect(
        this, &FileSystemWatcherPrivate::fileRemoved, &m_parent,
        &FileSystemWatcher::fileRemoved);

    QObject::connect(
        this, &FileSystemWatcherPrivate::directoryChanged, &m_parent,
        &FileSystemWatcher::directoryChanged);

    QObject::connect(
        this, &FileSystemWatcherPrivate::directoryRemoved, &m_parent,
        &FileSystemWatcher::directoryRemoved);

    QObject::connect(
        &m_watcher, &QFileSystemWatcher::fileChanged, this,
        &FileSystemWatcherPrivate::onFileChanged);

    QObject::connect(
        &m_watcher, &QFileSystemWatcher::directoryChanged, this,
        &FileSystemWatcherPrivate::onDirectoryChanged);
}

void FileSystemWatcherPrivate::processFileRemoval(const QString & path)
{
    auto it = m_justRemovedFilePathsWithPostRemovalTimerIds.left.find(path);
    if (it != m_justRemovedFilePathsWithPostRemovalTimerIds.left.end()) {
        return;
    }

    QNTRACE(
        "utility:fs_watcher",
        "It appears the watched file has been "
            << "removed recently and no timer has been set up yet to track its "
            << "removal; setting up such timer now");

    int timerId = startTimer(m_removalTimeoutMSec);

    m_justRemovedFilePathsWithPostRemovalTimerIds.insert(
        PathWithTimerId::value_type(path, timerId));

    QNTRACE(
        "utility:fs_watcher",
        "Set up timer with id " << timerId << " for " << m_removalTimeoutMSec
                                << " to see if file " << path
                                << " would re-appear again soon");
}

void FileSystemWatcherPrivate::processDirectoryRemoval(const QString & path)
{
    auto it =
        m_justRemovedDirectoryPathsWithPostRemovalTimerIds.left.find(path);

    if (it != m_justRemovedDirectoryPathsWithPostRemovalTimerIds.left.end()) {
        return;
    }

    QNTRACE(
        "utility:fs_watcher",
        "It appears the watched directory has been "
            << "removed recently and no timer has been set up yet to track its "
            << "removal; setting up such timer now");

    int timerId = startTimer(m_removalTimeoutMSec);

    m_justRemovedDirectoryPathsWithPostRemovalTimerIds.insert(
        PathWithTimerId::value_type(path, timerId));

    QNTRACE(
        "utility:fs_watcher",
        "Set up timer with id " << timerId << " for " << m_removalTimeoutMSec
                                << " to see if directory " << path
                                << " would re-appear again soon");
}

void FileSystemWatcherPrivate::timerEvent(QTimerEvent * pEvent)
{
    if (Q_UNLIKELY(!pEvent)) {
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);

    auto fileIt =
        m_justRemovedFilePathsWithPostRemovalTimerIds.right.find(timerId);

    if (fileIt != m_justRemovedFilePathsWithPostRemovalTimerIds.right.end()) {
        const QString & filePath = fileIt->second;
        QFileInfo info(filePath);
        if (!info.isFile()) {
            QNTRACE(
                "utility:fs_watcher",
                "File " << filePath
                        << " doesn't exist after some time since its removal");

            auto it = m_watchedFiles.find(filePath);
            if (it != m_watchedFiles.end()) {
                Q_UNUSED(m_watchedFiles.erase(it));
                Q_EMIT fileRemoved(filePath);
            }
        }
        else {
            QNTRACE(
                "utility:fs_watcher",
                "File " << filePath
                        << " exists again after some time since its removal");

            auto it = m_watchedFiles.find(filePath);
            if (it != m_watchedFiles.end()) {
                m_watcher.addPath(filePath);
                Q_EMIT fileChanged(filePath);
            }
        }

        Q_UNUSED(
            m_justRemovedFilePathsWithPostRemovalTimerIds.right.erase(fileIt));

        return;
    }

    auto dirIt =
        m_justRemovedDirectoryPathsWithPostRemovalTimerIds.right.find(timerId);

    if (dirIt != m_justRemovedDirectoryPathsWithPostRemovalTimerIds.right.end())
    {
        const QString & directoryPath = dirIt->second;
        QFileInfo info(directoryPath);
        if (!info.isDir()) {
            QNTRACE(
                "utility:fs_watcher",
                "Directory "
                    << directoryPath
                    << " doesn't exist after some time since its removal");

            auto it = m_watchedDirectories.find(directoryPath);
            if (it != m_watchedDirectories.end()) {
                Q_UNUSED(m_watchedDirectories.erase(it));
                Q_EMIT directoryRemoved(directoryPath);
            }
        }
        else {
            QNTRACE(
                "utility:fs_watcher",
                "Directory "
                    << directoryPath
                    << " exists again after some time since its removal");

            auto it = m_watchedDirectories.find(directoryPath);
            if (it != m_watchedDirectories.end()) {
                m_watcher.addPath(directoryPath);
                Q_EMIT directoryChanged(directoryPath);
            }
        }

        Q_UNUSED(m_justRemovedDirectoryPathsWithPostRemovalTimerIds.right.erase(
            dirIt));

        return;
    }
}

} // namespace quentier
