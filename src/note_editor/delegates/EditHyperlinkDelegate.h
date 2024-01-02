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

#include <QObject>

namespace quentier {

class NoteEditorPage;
class NoteEditorPrivate;

class EditHyperlinkDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit EditHyperlinkDelegate(
        NoteEditorPrivate & noteEditor, quint64 hyperlinkId);

    void start();

Q_SIGNALS:
    void finished();
    void cancelled();
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);
    void onHyperlinkDataReceived(const QVariant & data);

    void onHyperlinkDataEdited(
        QString text, QUrl url, quint64 hyperlinkId, bool startupUrlWasEmpty);

    void onHyperlinkModified(const QVariant & data);

private:
    void doStart();

    void raiseEditHyperlinkDialog(
        const QString & startupHyperlinkText,
        const QString & startupHyperlinkUrl);

private:
    using JsCallback = JsResultCallbackFunctor<EditHyperlinkDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;
    const quint64 m_hyperlinkId;
};

} // namespace quentier
