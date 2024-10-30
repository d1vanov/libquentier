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

#include "AddHyperlinkToSelectedTextDelegate.h"

#include "../NoteEditor_p.h"
#include "../dialogs/EditHyperlinkDialog.h"

#include <quentier/logging/QuentierLogger.h>

#include <memory>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor.page());         \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "AddHyperlinkToSelectedTextDelegate",                              \
            "Can't add hyperlink to the selected text: no note editor page")); \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

AddHyperlinkToSelectedTextDelegate::AddHyperlinkToSelectedTextDelegate(
    NoteEditorPrivate & noteEditor, const quint64 hyperlinkIdToAdd) :
    QObject(&noteEditor), m_noteEditor(noteEditor),
    m_hyperlinkId(hyperlinkIdToAdd)
{}

void AddHyperlinkToSelectedTextDelegate::start()
{
    QNDEBUG(
        "note_editor:delegate", "AddHyperlinkToSelectedTextDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &AddHyperlinkToSelectedTextDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        addHyperlinkToSelectedText();
    }
}

void AddHyperlinkToSelectedTextDelegate::startWithPresetHyperlink(
    const QString & presetHyperlink, const QString & replacementLinkText)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::"
            << "startWithPresetHyperlink: preset hyperlink = "
            << presetHyperlink
            << ", replacement link text = " << replacementLinkText);

    m_shouldGetHyperlinkFromDialog = false;
    m_presetHyperlink = presetHyperlink;
    m_replacementLinkText = replacementLinkText;

    start();
}

void AddHyperlinkToSelectedTextDelegate::onOriginalPageConvertedToNote(
    qevercloud::Note note) // NOLINT
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &AddHyperlinkToSelectedTextDelegate::onOriginalPageConvertedToNote);

    addHyperlinkToSelectedText();
}

void AddHyperlinkToSelectedTextDelegate::addHyperlinkToSelectedText()
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::addHyperlinkToSelectedText");

    if (m_shouldGetHyperlinkFromDialog || m_replacementLinkText.isEmpty()) {
        const QString javascript = QStringLiteral("getSelectionHtml();");
        GET_PAGE()

        page->executeJavaScript(
            javascript,
            JsCallback(
                *this,
                &AddHyperlinkToSelectedTextDelegate::
                    onInitialHyperlinkDataReceived));

        return;
    }

    setHyperlinkToSelection(m_presetHyperlink, m_replacementLinkText);
}

void AddHyperlinkToSelectedTextDelegate::onInitialHyperlinkDataReceived(
    const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::"
            << "onInitialHyperlinkDataReceived: " << data);

    const QString initialText = data.toString();

    if (m_shouldGetHyperlinkFromDialog) {
        raiseAddHyperlinkDialog(initialText);
    }
    else {
        setHyperlinkToSelection(
            m_presetHyperlink,
            (m_replacementLinkText.isEmpty() ? initialText
                                             : m_replacementLinkText));
    }
}

void AddHyperlinkToSelectedTextDelegate::raiseAddHyperlinkDialog(
    const QString & initialText)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::raiseAddHyperlinkDialog: "
            << "initial text = " << initialText);

    const auto pEditHyperlinkDialog =
        std::make_unique<EditHyperlinkDialog>(&m_noteEditor, initialText);

    pEditHyperlinkDialog->setWindowModality(Qt::WindowModal);

    QObject::connect(
        pEditHyperlinkDialog.get(), &EditHyperlinkDialog::editHyperlinkAccepted,
        this,
        &AddHyperlinkToSelectedTextDelegate::onAddHyperlinkDialogFinished);

    QNTRACE("note_editor:delegate", "Will exec add hyperlink dialog now");
    if (pEditHyperlinkDialog->exec() == QDialog::Rejected) {
        QNTRACE("note_editor:delegate", "Cancelled add hyperlink dialog");
        Q_EMIT cancelled();
    }
}

void AddHyperlinkToSelectedTextDelegate::onAddHyperlinkDialogFinished(
    QString text, QUrl url, quint64 hyperlinkId, // NOLINT
    bool startupUrlWasEmpty)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::"
            << "onAddHyperlinkDialogFinished: text = " << text
            << ", url = " << url);

    Q_UNUSED(hyperlinkId);
    Q_UNUSED(startupUrlWasEmpty);

    setHyperlinkToSelection(url.toString(QUrl::FullyEncoded), text);
}

void AddHyperlinkToSelectedTextDelegate::setHyperlinkToSelection(
    const QString & url, const QString & text)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::setHyperlinkToSelection: "
            << "url = " << url << ", text = " << text);

    const QString javascript =
        QStringLiteral("hyperlinkManager.setHyperlinkToSelection('") + text +
        QStringLiteral("', '") + url + QStringLiteral("', ") +
        QString::number(m_hyperlinkId) + QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(
            *this,
            &AddHyperlinkToSelectedTextDelegate::onHyperlinkSetToSelection));
}

void AddHyperlinkToSelectedTextDelegate::onHyperlinkSetToSelection(
    const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddHyperlinkToSelectedTextDelegate::onHyperlinkSetToSelection");

    const auto resultMap = data.toMap();

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of the attempt to "
                       "set the hyperlink to selection from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of the attempt to set "
                           "the hyperlink to selection from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't set the hyperlink to selection"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
