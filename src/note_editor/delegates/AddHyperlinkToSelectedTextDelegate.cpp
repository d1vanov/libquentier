/*
 * Copyright 2016-2019 Dmitry Ivanov
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
#include <QScopedPointer>

#ifndef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebFrame>
#endif

namespace quentier {

#define GET_PAGE() \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditor.page()); \
    if (Q_UNLIKELY(!page)) { \
        ErrorString error(QT_TRANSLATE_NOOP("AddHyperlinkToSelectedTextDelegate", \
                                            "Can't add hyperlink to the selected "\
                                            "text: no note editor page")); \
        QNWARNING(error); \
        Q_EMIT notifyError(error); \
        return; \
    }

AddHyperlinkToSelectedTextDelegate::AddHyperlinkToSelectedTextDelegate(
        NoteEditorPrivate & noteEditor, const quint64 hyperlinkIdToAdd) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor),
    m_shouldGetHyperlinkFromDialog(true),
    m_presetHyperlink(),
    m_replacementLinkText(),
    m_hyperlinkId(hyperlinkIdToAdd)
{}

void AddHyperlinkToSelectedTextDelegate::start()
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::start"));

    if (m_noteEditor.isEditorPageModified())
    {
        QObject::connect(&m_noteEditor,
                         QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                         this,
                         QNSLOT(AddHyperlinkToSelectedTextDelegate,
                                onOriginalPageConvertedToNote,Note));
        m_noteEditor.convertToNote();
    }
    else
    {
        addHyperlinkToSelectedText();
    }
}

void AddHyperlinkToSelectedTextDelegate::startWithPresetHyperlink(
    const QString & presetHyperlink, const QString & replacementLinkText)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::")
            << QStringLiteral("startWithPresetHyperlink: preset hyperlink = ")
            << presetHyperlink << QStringLiteral(", replacement link text = ")
            << replacementLinkText);

    m_shouldGetHyperlinkFromDialog = false;
    m_presetHyperlink = presetHyperlink;
    m_replacementLinkText = replacementLinkText;

    start();
}

void AddHyperlinkToSelectedTextDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::"
                           "onOriginalPageConvertedToNote"));

    Q_UNUSED(note)

    QObject::disconnect(&m_noteEditor,
                        QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                        this,
                        QNSLOT(AddHyperlinkToSelectedTextDelegate,
                               onOriginalPageConvertedToNote,Note));

    addHyperlinkToSelectedText();
}

void AddHyperlinkToSelectedTextDelegate::addHyperlinkToSelectedText()
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::"
                           "addHyperlinkToSelectedText"));

    if (m_shouldGetHyperlinkFromDialog || m_replacementLinkText.isEmpty())
    {
        QString javascript = QStringLiteral("getSelectionHtml();");
        GET_PAGE()
        page->executeJavaScript(
            javascript,
            JsCallback(
                *this,
                &AddHyperlinkToSelectedTextDelegate::onInitialHyperlinkDataReceived));

        return;
    }

    setHyperlinkToSelection(m_presetHyperlink, m_replacementLinkText);
}

void AddHyperlinkToSelectedTextDelegate::onInitialHyperlinkDataReceived(
    const QVariant & data)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::")
            << QStringLiteral("onInitialHyperlinkDataReceived: ") << data);

    QString initialText = data.toString();

    if (m_shouldGetHyperlinkFromDialog)
    {
        raiseAddHyperlinkDialog(initialText);
    }
    else
    {
        setHyperlinkToSelection(
            m_presetHyperlink,
            (m_replacementLinkText.isEmpty()
             ? initialText
             : m_replacementLinkText));
    }
}

void AddHyperlinkToSelectedTextDelegate::raiseAddHyperlinkDialog(
    const QString & initialText)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::")
            << QStringLiteral("raiseAddHyperlinkDialog: initial text = ")
            << initialText);

    QScopedPointer<EditHyperlinkDialog> pEditHyperlinkDialog(
        new EditHyperlinkDialog(&m_noteEditor, initialText));

    pEditHyperlinkDialog->setWindowModality(Qt::WindowModal);
    QObject::connect(pEditHyperlinkDialog.data(),
                     QNSIGNAL(EditHyperlinkDialog,accepted,
                              QString,QUrl,quint64,bool),
                     this,
                     QNSLOT(AddHyperlinkToSelectedTextDelegate,
                            onAddHyperlinkDialogFinished,
                            QString,QUrl,quint64,bool));
    QNTRACE(QStringLiteral("Will exec add hyperlink dialog now"));
    int res = pEditHyperlinkDialog->exec();
    if (res == QDialog::Rejected) {
        QNTRACE(QStringLiteral("Cancelled add hyperlink dialog"));
        Q_EMIT cancelled();
    }
}

void AddHyperlinkToSelectedTextDelegate::onAddHyperlinkDialogFinished(
    QString text, QUrl url, quint64 hyperlinkId, bool startupUrlWasEmpty)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::")
            << QStringLiteral("onAddHyperlinkDialogFinished: text = ")
            << text << QStringLiteral(", url = ") << url);

    Q_UNUSED(hyperlinkId);
    Q_UNUSED(startupUrlWasEmpty);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QString urlString = url.toString(QUrl::FullyEncoded);
#else
    QString urlString = url.toString(QUrl::None);
#endif

    setHyperlinkToSelection(urlString, text);
}

void AddHyperlinkToSelectedTextDelegate::setHyperlinkToSelection(const QString & url,
                                                                 const QString & text)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::")
            << QStringLiteral("setHyperlinkToSelection: url = ") << url
            << QStringLiteral(", text = ") << text);

    QString javascript = QStringLiteral("hyperlinkManager.setHyperlinkToSelection('") +
                         text + QStringLiteral("', '") + url + QStringLiteral("', ") +
                         QString::number(m_hyperlinkId) + QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(
            *this,
            &AddHyperlinkToSelectedTextDelegate::onHyperlinkSetToSelection));
}

void AddHyperlinkToSelectedTextDelegate::onHyperlinkSetToSelection(const QVariant & data)
{
    QNDEBUG(QStringLiteral("AddHyperlinkToSelectedTextDelegate::onHyperlinkSetToSelection"));

    QMap<QString,QVariant> resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end()))
    {
        ErrorString error(QT_TR_NOOP("Can't parse the result of the attempt to "
                                     "set the hyperlink to selection from JavaScript"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res)
    {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end()))
        {
            error.setBase(QT_TR_NOOP("Can't parse the error of the attempt to set "
                                     "the hyperlink to selection from JavaScript"));
        }
        else
        {
            error.setBase(QT_TR_NOOP("Can't set the hyperlink to selection"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
