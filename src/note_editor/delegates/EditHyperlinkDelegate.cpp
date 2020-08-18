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

#include "EditHyperlinkDelegate.h"

#include "../NoteEditorPage.h"
#include "../NoteEditor_p.h"
#include "../dialogs/EditHyperlinkDialog.h"

#include <quentier/logging/QuentierLogger.h>

#include <QStringList>

#include <memory>

namespace quentier {

EditHyperlinkDelegate::EditHyperlinkDelegate(
    NoteEditorPrivate & noteEditor, const quint64 hyperlinkId) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor), m_hyperlinkId(hyperlinkId)
{}

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor.page());         \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "EditHyperlinkDelegate",                                           \
            "Can't edit hyperlink: no note editor page"));                     \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

void EditHyperlinkDelegate::start()
{
    QNDEBUG("note_editor:delegate", "EditHyperlinkDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &EditHyperlinkDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        doStart();
    }
}

void EditHyperlinkDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(
        "note_editor:delegate",
        "EditHyperlinkDelegate"
            << "::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &EditHyperlinkDelegate::onOriginalPageConvertedToNote);

    doStart();
}

void EditHyperlinkDelegate::onHyperlinkDataReceived(const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "EditHyperlinkDelegate"
            << "::onHyperlinkDataReceived: data = " << data);

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink data "
                       "request from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink data "
                           "request from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't get hyperlink data from JavaScript"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto dataIt = resultMap.find(QStringLiteral("data"));
    if (Q_UNLIKELY(dataIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("No hyperlink data received from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    QStringList hyperlinkDataList = dataIt.value().toStringList();
    if (hyperlinkDataList.isEmpty()) {
        ErrorString error(
            QT_TR_NOOP("Can't edit hyperlink: can't find hyperlink "
                       "text and link"));
        Q_EMIT notifyError(error);
        return;
    }

    if (hyperlinkDataList.size() != 2) {
        ErrorString error(
            QT_TR_NOOP("Can't edit hyperlink: can't parse hyperlink "
                       "text and link"));

        QNWARNING(
            "note_editor:delegate",
            error << "; hyperlink data: "
                  << hyperlinkDataList.join(QStringLiteral(",")));

        Q_EMIT notifyError(error);
        return;
    }

    raiseEditHyperlinkDialog(hyperlinkDataList[0], hyperlinkDataList[1]);
}

void EditHyperlinkDelegate::doStart()
{
    QNDEBUG("note_editor:delegate", "EditHyperlinkDelegate::doStart");

    QString javascript = QStringLiteral("hyperlinkManager.getHyperlinkData(") +
        QString::number(m_hyperlinkId) + QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(*this, &EditHyperlinkDelegate::onHyperlinkDataReceived));
}

void EditHyperlinkDelegate::raiseEditHyperlinkDialog(
    const QString & startupHyperlinkText, const QString & startupHyperlinkUrl)
{
    QNDEBUG(
        "note_editor:delegate",
        "EditHyperlinkDelegate"
            << "::raiseEditHyperlinkDialog: original text = "
            << startupHyperlinkText
            << ", original url: " << startupHyperlinkUrl);

    auto pEditHyperlinkDialog = std::make_unique<EditHyperlinkDialog>(
        &m_noteEditor, startupHyperlinkText, startupHyperlinkUrl);

    pEditHyperlinkDialog->setWindowModality(Qt::WindowModal);

    QObject::connect(
        pEditHyperlinkDialog.get(), &EditHyperlinkDialog::editHyperlinkAccepted,
        this, &EditHyperlinkDelegate::onHyperlinkDataEdited);

    QNTRACE("note_editor:delegate", "Will exec edit hyperlink dialog now");

    int res = pEditHyperlinkDialog->exec();
    if (res == QDialog::Rejected) {
        QNTRACE("note_editor:delegate", "Cancelled editing the hyperlink");
        Q_EMIT cancelled();
        return;
    }
}

void EditHyperlinkDelegate::onHyperlinkDataEdited(
    QString text, QUrl url, quint64 hyperlinkId, bool startupUrlWasEmpty)
{
    QNDEBUG(
        "note_editor:delegate",
        "EditHyperlinkDelegate"
            << "::onHyperlinkDataEdited: text = " << text << ", url = " << url
            << ", hyperlink id = " << hyperlinkId);

    Q_UNUSED(hyperlinkId)
    Q_UNUSED(startupUrlWasEmpty)

    QString urlString = url.toString(QUrl::FullyEncoded);

    QString javascript = QStringLiteral("hyperlinkManager.setHyperlinkData('") +
        text + QStringLiteral("', '") + urlString + QStringLiteral("', ") +
        QString::number(m_hyperlinkId) + QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(*this, &EditHyperlinkDelegate::onHyperlinkModified));
}

void EditHyperlinkDelegate::onHyperlinkModified(const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "EditHyperlinkDelegate"
            << "::onHyperlinkModified");

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of hyperlink edit "
                       "from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of hyperlink editing "
                           "from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't edit hyperlink: "));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
