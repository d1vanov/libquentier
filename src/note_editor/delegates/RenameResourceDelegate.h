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

#include "JsResultCallbackFunctor.hpp"

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Resource.h>

#include <QHash>
#include <QObject>
#include <QUuid>

namespace quentier {

class GenericResourceImageManager;
class NoteEditorPrivate;

/**
 * @brief The RenameResourceDelegate class encapsulates a chain of callbacks
 * required for proper implementation of renaming a resource displayed on
 * the note editor's page considering the details of wrapping this action
 * around undo stack
 */
class Q_DECL_HIDDEN RenameResourceDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit RenameResourceDelegate(
        const qevercloud::Resource & resource, NoteEditorPrivate & noteEditor,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> &
            genericResourceImageFilePathsByResourceHash,
        bool performingUndo = false);

    void start();

    void startWithPresetNames(
        const QString & oldResourceName, const QString & newResourceName);

Q_SIGNALS:
    void finished(
        QString oldResourceName, QString newResourceName,
        qevercloud::Resource resource, bool performingUndo);

    void cancelled();
    void notifyError(ErrorString);

// private signals
    void saveGenericResourceImageToFile(
        QString noteLocalId, QString resourceLocalId,
        QByteArray resourceImageData, QString resourceFileSuffix,
        QByteArray resourceActualHash, QString resourceDisplayName,
        QUuid requestId);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);
    void onRenameResourceDialogFinished(QString newResourceName);

    void onGenericResourceImageWriterFinished(
        bool success, QByteArray resourceHash, QString filePath,
        ErrorString errorDescription, QUuid requestId);

    void onGenericResourceImageUpdated(const QVariant & data);

private:
    void doStart();
    void raiseRenameResourceDialog();
    void buildAndSaveGenericResourceImage();

private:
    using JsCallback = JsResultCallbackFunctor<RenameResourceDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;
    GenericResourceImageManager * m_pGenericResourceImageManager;
    QHash<QByteArray, QString> & m_genericResourceImageFilePathsByResourceHash;
    qevercloud::Resource m_resource;

    QString m_oldResourceName;
    QString m_newResourceName;
    bool m_shouldGetResourceNameFromDialog = true;

    bool m_performingUndo;

    qevercloud::Note * m_pNote;

    QUuid m_genericResourceImageWriterRequestId;
};

} // namespace quentier
