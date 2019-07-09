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

#include "EditHyperlinkDelegate.h"
#include "../NoteEditor_p.h"
#include "../NoteEditorPage.h"
#include "../dialogs/EditHyperlinkDialog.h"

#include <quentier/logging/QuentierLogger.h>

#include <QScopedPointer>
#include <QStringList>

namespace quentier {

EditHyperlinkDelegate::EditHyperlinkDelegate(
        NoteEditorPrivate & noteEditor, const quint64 hyperlinkId) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor),
    m_hyperlinkId(hyperlinkId)
{}

#define GET_PAGE()                                                             \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditor.page());\
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP("EditHyperlinkDelegate",           \
                                            "Can't edit hyperlink: no note "   \
                                            "editor page"));                   \
        QNWARNING(error);                                                      \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

void EditHyperlinkDelegate::start()
{
    QNDEBUG("EditHyperlinkDelegate::start");

    if (m_noteEditor.isEditorPageModified())
    {
        QObject::connect(&m_noteEditor,
                         QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                         this,
                         QNSLOT(EditHyperlinkDelegate,
                                onOriginalPageConvertedToNote,Note));
        m_noteEditor.convertToNote();
    }
    else
    {
        doStart();
    }
}

void EditHyperlinkDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG("EditHyperlinkDelegate::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(&m_noteEditor,
                        QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                        this,
                        QNSLOT(EditHyperlinkDelegate,
                               onOriginalPageConvertedToNote,Note));

    doStart();
}

void EditHyperlinkDelegate::onHyperlinkDataReceived(const QVariant & data)
{
    QNDEBUG("EditHyperlinkDelegate::onHyperlinkDataReceived: data = " << data);

    QMap<QString,QVariant> resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end()))
    {
        ErrorString error(QT_TR_NOOP("Can't parse the result of hyperlink data "
                                     "request from JavaScript"));
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
            error.setBase(QT_TR_NOOP("Can't parse the error of hyperlink data "
                                     "request from JavaScript"));
        }
        else
        {
            error.setBase(QT_TR_NOOP("Can't get hyperlink data from JavaScript"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    auto dataIt = resultMap.find(QStringLiteral("data"));
    if (Q_UNLIKELY(dataIt == resultMap.end())) {
        ErrorString error(QT_TR_NOOP("No hyperlink data received from JavaScript"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    QStringList hyperlinkDataList = dataIt.value().toStringList();
    if (hyperlinkDataList.isEmpty())
    {
        ErrorString error(QT_TR_NOOP("Can't edit hyperlink: can't find hyperlink "
                                     "text and link"));
        Q_EMIT notifyError(error);
        return;
    }

    if (hyperlinkDataList.size() != 2)
    {
        ErrorString error(QT_TR_NOOP("Can't edit hyperlink: can't parse hyperlink "
                                     "text and link"));
        QNWARNING(error << "; hyperlink data: "
                  << hyperlinkDataList.join(QStringLiteral(",")));
        Q_EMIT notifyError(error);
        return;
    }

    raiseEditHyperlinkDialog(hyperlinkDataList[0], hyperlinkDataList[1]);
}

void EditHyperlinkDelegate::doStart()
{
    QNDEBUG("EditHyperlinkDelegate::doStart");

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
    QNDEBUG("EditHyperlinkDelegate::raiseEditHyperlinkDialog: original text = "
            << startupHyperlinkText << ", original url: "
            << startupHyperlinkUrl);

    QScopedPointer<EditHyperlinkDialog> pEditHyperlinkDialog(
        new EditHyperlinkDialog(&m_noteEditor, startupHyperlinkText,
                                startupHyperlinkUrl));
    pEditHyperlinkDialog->setWindowModality(Qt::WindowModal);
    QObject::connect(pEditHyperlinkDialog.data(),
                     QNSIGNAL(EditHyperlinkDialog,accepted,
                              QString,QUrl,quint64,bool),
                     this,
                     QNSLOT(EditHyperlinkDelegate,onHyperlinkDataEdited,
                            QString,QUrl,quint64,bool));
    QNTRACE("Will exec edit hyperlink dialog now");
    int res = pEditHyperlinkDialog->exec();
    if (res == QDialog::Rejected) {
        QNTRACE("Cancelled editing the hyperlink");
        Q_EMIT cancelled();
        return;
    }
}

void EditHyperlinkDelegate::onHyperlinkDataEdited(
    QString text, QUrl url, quint64 hyperlinkId, bool startupUrlWasEmpty)
{
    QNDEBUG("EditHyperlinkDelegate::onHyperlinkDataEdited: text = "
            << text << ", url = " << url
            << ", hyperlink id = " << hyperlinkId);

    Q_UNUSED(hyperlinkId)
    Q_UNUSED(startupUrlWasEmpty)

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QString urlString = url.toString(QUrl::FullyEncoded);
#else
    QString urlString = url.toString(QUrl::None);
#endif

    QString javascript = QStringLiteral("hyperlinkManager.setHyperlinkData('") +
                         text + QStringLiteral("', '") + urlString +
                         QStringLiteral("', ") + QString::number(m_hyperlinkId) +
                         QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(*this, &EditHyperlinkDelegate::onHyperlinkModified));
}

void EditHyperlinkDelegate::onHyperlinkModified(const QVariant & data)
{
    QNDEBUG("EditHyperlinkDelegate::onHyperlinkModified");

    QMap<QString,QVariant> resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end()))
    {
        ErrorString error(QT_TR_NOOP("Can't parse the result of hyperlink edit "
                                     "from JavaScript"));
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
            error.setBase(QT_TR_NOOP("Can't parse the error of hyperlink editing "
                                     "from JavaScript"));
        }
        else
        {
            error.setBase(QT_TR_NOOP("Can't edit hyperlink: "));
            error.details() = errorIt.value().toString();
        }

        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
