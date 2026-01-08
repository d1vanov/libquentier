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
#include <QUuid>

namespace quentier {

class NoteEditorPrivate;

/**
 * @brief The AddHyperlinkToSelectedTextDelegate class encapsulates a chain of
 * callbacks required for proper implementation of adding a hyperlink to
 * the currently selected text considering the details of wrapping this action
 * around undo stack and necessary switching of note editor page during the
 * process
 */
class AddHyperlinkToSelectedTextDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit AddHyperlinkToSelectedTextDelegate(
        NoteEditorPrivate & noteEditor, quint64 hyperlinkIdToAdd);

    void start();

    void startWithPresetHyperlink(
        const QString & presetHyperlink,
        const QString & replacementLinkText = {});

Q_SIGNALS:
    void finished();
    void cancelled();
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);
    void onInitialHyperlinkDataReceived(const QVariant & data);

    void onAddHyperlinkDialogFinished(
        QString text, QUrl url, quint64 hyperlinkId, bool startupUrlWasEmpty);

    void onHyperlinkSetToSelection(const QVariant & data);

private:
    void requestPageScroll();
    void addHyperlinkToSelectedText();
    void raiseAddHyperlinkDialog(const QString & initialText);
    void setHyperlinkToSelection(const QString & url, const QString & text);

private:
    using JsCallback =
        JsResultCallbackFunctor<AddHyperlinkToSelectedTextDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;

    bool m_shouldGetHyperlinkFromDialog = true;
    QString m_presetHyperlink;
    QString m_replacementLinkText;

    const quint64 m_hyperlinkId;
};

} // namespace quentier
