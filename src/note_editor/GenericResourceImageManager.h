/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <qevercloud/types/Note.h>

#include <QObject>
#include <QUuid>

#include <memory>

namespace quentier {

/**
 * @brief The GenericResourceImageManager class is a worker for the I/O thread
 * which would write two files in a folder accessible for note editor's page:
 * the composed image for a generic resource and the hash of that resource.
 * It would also listen to the current note changes and remove stale generic
 * resource images as appropriate.
 */
class GenericResourceImageManager final : public QObject
{
    Q_OBJECT
public:
    explicit GenericResourceImageManager(QObject * parent = nullptr);

    void setStorageFolderPath(const QString & storageFolderPath);

Q_SIGNALS:
    void genericResourceImageWriteReply(
        bool success, QByteArray resourceHash, QString filePath,
        ErrorString errorDescription, QUuid requestId);

public Q_SLOTS:
    void onGenericResourceImageWriteRequest(
        QString noteLocalId, QString resourceLocalId,
        QByteArray resourceImageData, QString resourceFileSuffix,
        QByteArray resourceActualHash, QString resourceDisplayName,
        QUuid requestId);

    void onCurrentNoteChanged(qevercloud::Note note);

private:
    void removeStaleGenericResourceImageFilesFromCurrentNote();

private:
    QString m_storageFolderPath;
    std::unique_ptr<qevercloud::Note> m_pCurrentNote;
};

} // namespace quentier
